#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "catch.hpp"
#include "iop_core.hpp"
#include "iop_interrupt_controller.hpp"
#include "iop_timer_timing.hpp"
#include "neko_system.hpp"
#include "save_state_mutation.hpp"
#include "ssbus_test_utils.hpp"

namespace
{
  constexpr auto ssbusAddresses = SSBUSTest::addresses;

  void loadInstruction(NekoSystem *system, IOPAddress address, IOPWord word)
  {
    REQUIRE(system->iopBus().write32(address, word).status ==
      IOPBusStatus::Completed);
  }

  std::shared_ptr<const BootROMImage> rom(std::uint8_t marker)
  {
    std::vector<std::uint8_t> bytes(BootROMImage::SIZE, 0);
    bytes[0] = marker;
    return BootROMImage::create(bytes);
  }

  void advanceTimers(NekoSystem *system, unsigned cycles)
  {
    for (unsigned cycle = 0; cycle < cycles; ++cycle)
    {
      const auto sample = system->iopTimerTiming().advance(
        cycle % 8 == 0, {cycle % 7 < 3, cycle % 11 < 5});
      const auto requests = system->iopTimers().advance(sample);
      for (std::size_t index = 0; index < requests.count; ++index)
      {
        system->iopInterruptController().assertSource(
          requests.sources[index]);
      }
    }
  }
}

TEST_CASE("IOP persistence owns attached devices while the CPU remains halted")
{
  NekoSystem system;
  const auto initial = system.iopStateHash();
  REQUIRE(system.iopCore().hasAttachedBus());
  REQUIRE(system.iopCore().executionState() == IOPExecutionState::Halted);
  system.runMasterCycles(80);
  REQUIRE(system.iopCore().elapsedCycles() == 0);
  REQUIRE(system.iopTimers().counter(0).count == 10);
  REQUIRE(system.iopTimerTiming().pixelPhase() == 5424);
  REQUIRE(system.iopStateHash() != initial);
  system.iopInterruptController().writeMask(0x10);
  system.iopInterruptController().writeControl(1);
  system.iopInterruptController().assertSource(IOPInterruptSource::Timer0);
  REQUIRE(system.iopBus().interruptControllerPending());
  REQUIRE(system.iopBus().read32(IOPBus::INTC_STAT).value == 0x10);
  system.iopBus().write32(0x1f801108, 37);
  REQUIRE(system.iopTimers().counter(0).target == 37);
}

TEST_CASE("IOP SSBUS canonical state hashes every bit and resets to zero",
  "[iop][ssbus][save-state]")
{
  NekoSystem system;
  const auto initial = system.iopStateHash();
  const auto eeHash = system.eeStateHash();
  for (IOPAddress address : ssbusAddresses)
  {
    CAPTURE(address);
    for (unsigned bit = 0; bit < 32; ++bit)
    {
      CAPTURE(bit);
      const auto value = IOPWord(1) << bit;
      REQUIRE(system.iopBus().write32(address, value).status ==
        IOPBusStatus::Completed);
      REQUIRE(system.iopBus().read32(address).value == value);
      for (IOPAddress peer : ssbusAddresses)
      {
        if (peer != address)
        {
          REQUIRE(system.iopBus().read32(peer).value == 0);
        }
      }
      REQUIRE(system.iopStateHash() != initial);
    }
    REQUIRE(system.eeStateHash() == eeHash);
    REQUIRE(system.iopBus().write32(address, 0).status ==
      IOPBusStatus::Completed);
    REQUIRE(system.iopStateHash() == initial);
  }
  REQUIRE(system.eeStateHash() == eeHash);
  SaveStateMutation candidate(system.saveState());
  for (const auto &reg : SSBUSTest::registers)
  {
    candidate.writeScalar(reg.field, 0xffffffff);
  }
  candidate.refreshChecksum();
  system.loadState(candidate);
  for (IOPAddress address : ssbusAddresses)
  {
    REQUIRE(system.iopBus().read32(address).value == 0xffffffff);
  }
  REQUIRE(system.saveState() == candidate.bytes());
  system.reset();
  for (IOPAddress address : ssbusAddresses)
  {
    REQUIRE(system.iopBus().read32(address).value == 0);
  }
  REQUIRE(system.iopStateHash() == initial);
}

TEST_CASE("IOP SSBUS PIO bits round trip in combined canonical state",
  "[iop][ssbus][save-state]")
{
  NekoSystem system;
  const auto eeHash = system.eeStateHash();
  SaveStateMutation candidate(system.saveState());
  const auto initial = inspectSaveState(candidate);
  const auto *initialChecksum = initial.find("container.checksum");
  REQUIRE(initialChecksum != nullptr);
  for (unsigned bit = 0; bit < 32; ++bit)
  {
    CAPTURE(bit);
    const auto value = IOPWord(1) << bit;
    candidate.writeScalar("system.iop.bus.ssbusPioDelay", value);
    candidate.refreshChecksum();
    const auto snapshot = inspectSaveState(candidate);
    const auto *checksum = snapshot.find("container.checksum");
    REQUIRE(checksum != nullptr);
    REQUIRE(checksum->scalarValue != initialChecksum->scalarValue);
    system.loadState(candidate);
    REQUIRE(system.iopBus().read32(0x1f80101c).value == value);
    REQUIRE(system.iopBus().read32(0x1f801010).value == 0);
    REQUIRE(system.iopBus().read32(0x1f801020).value == 0);
    REQUIRE(system.eeStateHash() == eeHash);
    REQUIRE(system.saveState() == candidate.bytes());
  }
}

TEST_CASE("IOP SSBUS grouped canonical fields retain independent full values",
  "[iop][ssbus][save-state]")
{
  NekoSystem source;
  NekoSystem destination;
  const auto eeHash = source.eeStateHash();
  for (const auto &reg : SSBUSTest::registers)
  {
    REQUIRE(source.iopBus().write32(reg.address, reg.address ^ 0xfedcba98).status ==
      IOPBusStatus::Completed);
    REQUIRE(destination.iopBus().write32(reg.address, ~reg.address).status ==
      IOPBusStatus::Completed);
  }
  const auto state = source.saveState();
  const auto snapshot = inspectSaveState(state);
  std::size_t previousOffset = 0;
  for (const auto &reg : SSBUSTest::registers)
  {
    CAPTURE(reg.address);
    const auto *field = snapshot.find(reg.field);
    REQUIRE(field != nullptr);
    REQUIRE(field->kind == SaveStateFieldKind::U32);
    REQUIRE(field->size == 4);
    REQUIRE(field->scalarValue == (reg.address ^ 0xfedcba98));
    if (previousOffset != 0)
    {
      REQUIRE(field->containerOffset == previousOffset + 4);
    }
    previousOffset = field->containerOffset;
  }
  destination.loadState(state);
  for (const auto &reg : SSBUSTest::registers)
  {
    REQUIRE(destination.iopBus().read32(reg.address).value ==
      (reg.address ^ 0xfedcba98));
  }
  REQUIRE(destination.eeStateHash() == eeHash);
  REQUIRE(destination.iopStateHash() == source.iopStateHash());
  REQUIRE(destination.saveState() == state);
}

TEST_CASE("IOP SSBUS restore continues guest load and branch boundaries",
  "[iop][ssbus][save-state]")
{
  for (IOPAddress base :
       {0x1f801010u, 0x1f801020u, 0x1f80101cu, 0x1f801004u, 0x1f801420u})
  {
    CAPTURE(base);
    NekoSystem original;
    for (IOPAddress peer : ssbusAddresses)
    {
      if (peer != base)
      {
        REQUIRE(original.iopBus().write32(peer, peer ^ 0x1225).status ==
          IOPBusStatus::Completed);
      }
    }
    const IOPWord program[] = {
      0x3c01bf80, // lui r1, 0xbf80
      0x34210000 | (base & 0xffff), // ori r1, r1, register offset
      0x3c02ffff, // lui r2, 0xffff
      0x3442ffff, // ori r2, r2, 0xffff
      0xac220000, // sw r2, 0(r1)
      0x8c230000, // lw r3, 0(r1)
      0x10600002, // beq r3, r0, +2: sees old r3 in load-delay slot
      0x24640001, // addiu r4, r3, 1: branch delay sees completed load
      0xffffffff, // skipped unsupported instruction
      0xac030100, // sw r3, 0x100(r0)
      0
    };
    IOPAddress address = 0;
    for (const auto instruction : program)
    {
      loadInstruction(&original, address, instruction);
      address += 4;
    }
    original.iopCore().startExecution(0x80000000);
    for (unsigned step = 0; step < 6; ++step)
    {
      original.iopCore().stepInstruction();
    }
    REQUIRE(original.iopCore().exception().kind == IOPException::None);
    REQUIRE(original.iopCore().delayedResultContinuation().source ==
      IOPDelayedResultSource::Load);
    REQUIRE(original.iopCore().generalRegister(3) == 0);
    const auto state = original.saveState();
    REQUIRE_NOTHROW(inspectSaveState(state));
    NekoSystem restored;
    for (IOPAddress address : ssbusAddresses)
    {
      REQUIRE(restored.iopBus().write32(address, address ^ 0x87654321).status ==
        IOPBusStatus::Completed);
    }
    restored.loadState(state);
    REQUIRE(restored.iopBus().read32(base).value == 0xffffffff);
    for (IOPAddress peer : ssbusAddresses)
    {
      if (peer != base)
      {
        REQUIRE(restored.iopBus().read32(peer).value == (peer ^ 0x1225));
      }
    }
    REQUIRE(restored.iopStateHash() == original.iopStateHash());
    for (unsigned step = 0; step < 4; ++step)
    {
      original.iopCore().stepInstruction();
      restored.iopCore().stepInstruction();
      REQUIRE(restored.iopStateHash() == original.iopStateHash());
      if (step == 0)
      {
        REQUIRE(restored.iopCore().branchContinuation().active);
        const auto branchState = restored.saveState();
        restored.loadState(branchState);
        REQUIRE(restored.saveState() == branchState);
      }
    }
    REQUIRE(restored.iopCore().exception().kind == IOPException::None);
    REQUIRE(restored.iopCore().generalRegister(3) == 0xffffffff);
    REQUIRE(restored.iopCore().generalRegister(4) == 0);
    REQUIRE(restored.iopBus().read32(0x100).value == 0xffffffff);
    REQUIRE(restored.saveState() == original.saveState());
    REQUIRE(restored.iopBus().write32(base, 7).status ==
      IOPBusStatus::Completed);
    REQUIRE(original.iopBus().read32(base).value == 0xffffffff);
  }
}

TEST_CASE("IOP SSBUS rejected restore preserves destination state atomically",
  "[iop][ssbus][save-state]")
{
  NekoSystem source;
  for (IOPAddress address : ssbusAddresses)
  {
    REQUIRE(source.iopBus().write32(address, address ^ 0xfedcba98).status ==
      IOPBusStatus::Completed);
  }
  REQUIRE(source.iopBus().write32(0x1f801010, 0xfedcba98).status ==
    IOPBusStatus::Completed);
  REQUIRE(source.iopBus().write32(0x1f801020, 0x1225).status ==
    IOPBusStatus::Completed);
  REQUIRE(source.iopBus().write32(0x1f80101c, 0xff).status ==
    IOPBusStatus::Completed);
  const auto candidate = source.saveState();
  NekoSystem destination;
  for (IOPAddress address : ssbusAddresses)
  {
    REQUIRE(destination.iopBus().write32(address, address ^ 0x87654321).status ==
      IOPBusStatus::Completed);
  }
  REQUIRE(destination.iopBus().write32(0x1f801010, 0x12345678).status ==
    IOPBusStatus::Completed);
  REQUIRE(destination.iopBus().write32(0x1f801020, 0x87654321).status ==
    IOPBusStatus::Completed);
  REQUIRE(destination.iopBus().write32(0x1f80101c, 0xabcdef01).status ==
    IOPBusStatus::Completed);
  loadInstruction(&destination, 0x100, 0xdeadbeef);
  destination.iopCore().startExecution(0xa0000000);
  destination.startTrace();
  destination.runMasterCycles(8);
  destination.iopCore().haltExecution();
  REQUIRE_FALSE(destination.trace().empty());
  const auto traceHash = destination.traceHash();
  const auto traceSize = destination.trace().size();
  const auto before = destination.saveState();
  const auto hash = destination.iopStateHash();
  for (const auto &mutation :
       std::vector<std::pair<std::string, std::uint64_t>>{
         {"system.iop.interruptController.control", 2},
         {"container.version", 47},
         {"container.version", 46},
         {"container.version", 45}})
  {
    CAPTURE(mutation.first);
    CAPTURE(mutation.second);
    SaveStateMutation invalid(candidate);
    invalid.writeScalar(mutation.first, mutation.second);
    invalid.refreshChecksum();
    REQUIRE_THROWS_AS(inspectSaveState(invalid), std::invalid_argument);
    REQUIRE_THROWS_AS(destination.loadState(invalid), std::invalid_argument);
    REQUIRE(destination.iopBus().read32(0x1f801010).value == 0x12345678);
    REQUIRE(destination.iopBus().read32(0x1f801020).value == 0x87654321);
    REQUIRE(destination.iopBus().read32(0x1f80101c).value == 0xabcdef01);
    REQUIRE(destination.iopStateHash() == hash);
    REQUIRE(destination.saveState() == before);
    REQUIRE(destination.traceHash() == traceHash);
    REQUIRE(destination.trace().size() == traceSize);
    REQUIRE(destination.iopCore().hasAttachedBus());
  }
  auto truncated = candidate;
  truncated.pop_back();
  SaveStateMutation badChecksum(candidate);
  badChecksum.writeScalar("system.iop.bus.ssbusDev9Delay1", 0xffffffff);
  auto truncatedConfiguration = candidate;
  const auto fields = inspectSaveState(candidate);
  const auto *lastWord = fields.find("system.iop.bus.ssbusDev9Delay1");
  REQUIRE(lastWord != nullptr);
  truncatedConfiguration.resize(lastWord->containerOffset + 2);
  for (const auto &invalid :
       {truncated, badChecksum.bytes(), truncatedConfiguration})
  {
    REQUIRE_THROWS_AS(inspectSaveState(invalid), std::invalid_argument);
    REQUIRE_THROWS_AS(destination.loadState(invalid), std::invalid_argument);
    REQUIRE(destination.iopStateHash() == hash);
    REQUIRE(destination.saveState() == before);
    REQUIRE(destination.traceHash() == traceHash);
    REQUIRE(destination.trace().size() == traceSize);
  }
  REQUIRE(source.iopBus().installRom(rom(7)));
  const auto incompatibleROM = source.saveState();
  REQUIRE_NOTHROW(inspectSaveState(incompatibleROM));
  REQUIRE_THROWS_AS(destination.loadState(incompatibleROM), std::invalid_argument);
  REQUIRE(destination.iopBus().read32(0x1f801010).value == 0x12345678);
  REQUIRE(destination.iopBus().read32(0x1f801020).value == 0x87654321);
  REQUIRE(destination.iopBus().read32(0x1f80101c).value == 0xabcdef01);
  REQUIRE(destination.iopStateHash() == hash);
  REQUIRE(destination.saveState() == before);
  REQUIRE(destination.traceHash() == traceHash);
  REQUIRE(destination.trace().size() == traceSize);
}

TEST_CASE("IOP stateless platform writes leave canonical machine state unchanged",
  "[iop][post][high-control][save-state]")
{
  NekoSystem system;
  for (IOPAddress address : ssbusAddresses)
  {
    REQUIRE(system.iopBus().write32(address, address ^ 0x89abcdef).status ==
      IOPBusStatus::Completed);
  }
  loadInstruction(&system, 0x100, 0x12345678);
  REQUIRE(system.iopCore().readData32(0x80000100).value == 0x12345678);
  system.iopCore().startExecution(0x80000200);
  system.iopCore().stepInstruction();
  system.iopCore().haltExecution();
  REQUIRE(system.iopBus().write32(IOPBus::SCRATCHPAD_BASE, 0x87654321).status ==
    IOPBusStatus::Completed);
  system.iopTimers().writeMode(0, 0x10);
  system.iopTimers().writeTarget(0, 3);
  system.iopInterruptController().assertSource(IOPInterruptSource::Timer0);
  system.iopInterruptController().writeMask(0x10);
  system.iopInterruptController().writeControl(1);
  REQUIRE(system.iopBus().write32(0x1f8010f0, 0x76543210).status ==
    IOPBusStatus::Completed);
  REQUIRE(system.iopBus().write32(IOPBus::SIF_BASE + 0x10, 0x12345678).status ==
    IOPBusStatus::Completed);
  REQUIRE(system.iopBus().write32(IOPBus::SIF_BASE + 0x30, 0x40000001).status ==
    IOPBusStatus::Completed);
  REQUIRE(system.iopBus().write32(IOPBus::SIF_DOORBELL, 2).status ==
    IOPBusStatus::Completed);
  REQUIRE(system.sif().pushSIF1Quadword({{1, 2, 3, 4}}) ==
    SIFTransferOutcome::Completed);
  const auto state = system.saveState();
  const auto iopHash = system.iopStateHash();
  const auto eeHash = system.eeStateHash();
  const auto sifHash = system.sifStateHash();
  system.startTrace();
  for (IOPWord value : {0u, 0xfcu, 0xffffffffu, 0xaaaaaaaau, 0x55555555u})
  {
    REQUIRE(system.iopBus().write8(0x1f802070, static_cast<std::uint8_t>(value)).status ==
      IOPBusStatus::Completed);
    REQUIRE(system.iopBus().write32(0x1f802070, value).status ==
      IOPBusStatus::Completed);
    REQUIRE(system.iopBus().writeMasked32(0x1f802070, value, 0xf).status ==
      IOPBusStatus::Completed);
    REQUIRE(system.iopBus().write8(0x1fa00000, static_cast<std::uint8_t>(value)).status ==
      IOPBusStatus::Completed);
    REQUIRE(system.iopBus().read8(0x1f802070).status == IOPBusStatus::Unmapped);
    REQUIRE(system.iopBus().read8(0x1fa00000).status == IOPBusStatus::Unmapped);
    for (IOPAddress address : {0xfffe0140u, 0xfffe0144u})
    {
      REQUIRE(system.iopBus().write32(address, value).status ==
        IOPBusStatus::Completed);
      REQUIRE(system.iopBus().writeMasked32(address, ~value, 0xf).status ==
        IOPBusStatus::Completed);
      REQUIRE(system.iopBus().read32(address).status == IOPBusStatus::Unmapped);
    }
  }
  REQUIRE(system.iopStateHash() == iopHash);
  REQUIRE(system.eeStateHash() == eeHash);
  REQUIRE(system.sifStateHash() == sifHash);
  REQUIRE(system.saveState() == state);
  REQUIRE(system.trace().empty());
}

TEST_CASE("IOP diagnostic writes continue across lifecycle and branch restore",
  "[iop][post][save-state]")
{
  NekoSystem original;
  const auto image = rom(0x42);
  REQUIRE(original.iopBus().installRom(image));
  const IOPWord program[] = {
    0x3c01bf80, // lui r1, 0xbf80
    0x3c03bfa0, // lui r3, 0xbfa0
    0x8c020100, // lw r2, 0x100(r0)
    0x10000002, // beq r0, r0, +2: load-delay slot
    0xac222070, // sw r2, 0x2070(r1): branch delay slot
    0xffffffff, // skipped
    0xa0620000, // sb r2, 0(r3)
    0
  };
  for (std::size_t index = 0; index < sizeof(program) / sizeof(program[0]); ++index)
  {
    loadInstruction(&original, static_cast<IOPAddress>(index * 4), program[index]);
  }
  loadInstruction(&original, 0x100, 0x123456fc);
  original.iopCore().startExecution(0xa0000000);
  for (unsigned step = 0; step < 3; ++step)
  {
    original.iopCore().stepInstruction();
  }
  REQUIRE(original.iopCore().delayedResultContinuation().source ==
    IOPDelayedResultSource::Load);
  REQUIRE(original.iopCore().generalRegister(2) == 0);
  REQUIRE(original.iopBus().write8(0x1f802070, 0xff).status ==
    IOPBusStatus::Completed);
  REQUIRE(original.iopBus().write8(0x1fa00000, 0xff).status ==
    IOPBusStatus::Completed);
  original.iopCore().haltExecution();
  original.iopCore().resumeExecution();
  const auto state = original.saveState();
  NekoSystem restored;
  REQUIRE(restored.iopBus().installRom(image));
  restored.loadState(state);
  REQUIRE(restored.iopCore().delayedResultContinuation().source ==
    IOPDelayedResultSource::Load);
  for (auto *system : {&original, &restored})
  {
    system->iopCore().stepInstruction();
    REQUIRE(system->iopCore().branchContinuation().active);
    REQUIRE(system->iopCore().generalRegister(2) == 0x123456fc);
  }
  REQUIRE(original.iopStateHash() == restored.iopStateHash());
  restored.loadState(original.saveState());
  REQUIRE(restored.iopCore().branchContinuation().active);
  const auto replacement = rom(0x24);
  for (auto *system : {&original, &restored})
  {
    REQUIRE(system->iopBus().installRom(replacement));
  }
  for (unsigned step = 0; step < 3; ++step)
  {
    original.iopCore().stepInstruction();
    restored.iopCore().stepInstruction();
    REQUIRE(original.iopCore().exception().kind == IOPException::None);
    REQUIRE(original.iopCore().executionState() == IOPExecutionState::Running);
    REQUIRE(original.iopStateHash() == restored.iopStateHash());
  }
  REQUIRE(original.iopCore().programCounter() == 0xa0000020);
  REQUIRE(original.iopCore().retiredInstructions() == 7);
  REQUIRE(original.saveState() == restored.saveState());
  for (auto *system : {&original, &restored})
  {
    system->iopCore().haltExecution();
    system->iopCore().startExecution(0xa0000000);
    REQUIRE(system->iopCore().writeData8(0xbf802070, 0).outcome ==
      IOPAccessOutcome::Completed);
    REQUIRE(system->iopCore().writeData8(0xbfa00000, 0).outcome ==
      IOPAccessOutcome::Completed);
    system->reset();
    REQUIRE(system->iopBus().write32(0x1f802070, 0xfc).status ==
      IOPBusStatus::Completed);
    REQUIRE(system->iopBus().write8(0x1fa00000, 0xfc).status ==
      IOPBusStatus::Completed);
    REQUIRE(system->iopBus().read8(0x1f802070).status == IOPBusStatus::Unmapped);
    REQUIRE(system->iopBus().read8(0x1fa00000).status == IOPBusStatus::Unmapped);
  }
  REQUIRE(original.saveState() == restored.saveState());
}

TEST_CASE("IOP DCIC zero writes have the same canonical effects as NOP",
  "[iop][cop0][dcic][save-state]")
{
  NekoSystem disabledDCIC;
  NekoSystem nop;
  for (auto *system : {&disabledDCIC, &nop})
  {
    loadInstruction(system, 0x100, 0x12345678);
    REQUIRE(system->iopCore().readData32(0x80000100).value == 0x12345678);
    system->iopCore().startExecution(0x80000200);
    system->iopCore().stepInstruction();
    system->iopCore().haltExecution();
    loadInstruction(system, 0, system == &disabledDCIC ? 0x40803800 : 0);
    system->iopTimers().writeMode(0, 0x10);
    system->iopTimers().writeTarget(0, 3);
    system->iopInterruptController().assertSource(IOPInterruptSource::Timer0);
    system->iopInterruptController().writeMask(0x10);
    REQUIRE(system->sif().pushSIF1Quadword({{1, 2, 3, 4}}) ==
      SIFTransferOutcome::Completed);
    system->iopCore().startExecution(0xa0000000);
    system->iopCore().stepInstruction();
    REQUIRE(system->iopCore().exception().kind == IOPException::None);
    REQUIRE(system->iopCore().programCounter() == 0xa0000004);
  }
  loadInstruction(&nop, 0, 0x40803800);
  REQUIRE(disabledDCIC.iopStateHash() == nop.iopStateHash());
  REQUIRE(disabledDCIC.saveState() == nop.saveState());
  for (auto *system : {&disabledDCIC, &nop})
  {
    system->reset();
  }
  REQUIRE(disabledDCIC.saveState() == nop.saveState());
}

TEST_CASE("IOP DCIC admission continues across delayed-load and branch restore",
  "[iop][cop0][dcic][save-state]")
{
  NekoSystem original;
  const IOPWord program[] = {
    0x8c020100, // lw r2, 0x100(r0)
    0x40823800, // mtc0 r2, $7: captures zero before the delayed load
    0x10000002, // beq r0, r0, +2
    0x40803800, // mtc0 r0, $7: branch delay slot
    0xffffffff, // skipped
    0x40823800  // mtc0 r2, $7: nonzero remains unsupported
  };
  for (std::size_t index = 0; index < sizeof(program) / sizeof(program[0]); ++index)
  {
    loadInstruction(&original, static_cast<IOPAddress>(index * 4), program[index]);
  }
  loadInstruction(&original, 0x100, 0x12345678);
  original.iopCore().startExecution(0xa0000000);
  original.iopCore().stepInstruction();
  original.iopCore().haltExecution();
  original.iopCore().resumeExecution();
  NekoSystem restored;
  restored.loadState(original.saveState());
  REQUIRE(restored.iopCore().delayedResultContinuation().source ==
    IOPDelayedResultSource::Load);
  for (auto *system : {&original, &restored})
  {
    system->iopCore().stepInstruction();
    REQUIRE(system->iopCore().generalRegister(2) == 0x12345678);
    REQUIRE(system->iopCore().exception().kind == IOPException::None);
    REQUIRE(system->iopCore().delayedResultContinuation().source ==
      IOPDelayedResultSource::None);
    system->iopCore().stepInstruction();
    REQUIRE(system->iopCore().branchContinuation().active);
  }
  REQUIRE(original.saveState() == restored.saveState());
  restored.loadState(original.saveState());
  for (auto *system : {&original, &restored})
  {
    system->iopCore().haltExecution();
    system->iopCore().resumeExecution();
    system->iopCore().stepInstruction();
    REQUIRE(system->iopCore().programCounter() == 0xa0000014);
    REQUIRE(system->iopCore().retiredInstructions() == 4);
    REQUIRE(system->iopCore().exception().kind == IOPException::None);
    REQUIRE_FALSE(system->iopCore().branchContinuation().active);
    system->iopCore().stepInstruction();
    REQUIRE(system->iopCore().exception().kind == IOPException::ReservedInstruction);
    REQUIRE(system->iopCore().exceptionProgramCounter() == 0xa0000014);
    REQUIRE(system->iopCore().retiredInstructions() == 4);
  }
  REQUIRE(original.saveState() == restored.saveState());
}

TEST_CASE("IOP high-address writes continue across lifecycle and branch restore",
  "[iop][high-control][save-state]")
{
  NekoSystem original;
  const auto image = rom(0x42);
  REQUIRE(original.iopBus().installRom(image));
  const IOPWord program[] = {
    0x3c01fffe, // lui r1, 0xfffe
    0x8c020100, // lw r2, 0x100(r0)
    0xac220140, // sw r2, 0x140(r1): captures old r2 in load-delay slot
    0x10000002, // beq r0, r0, +2
    0xac220144, // sw r2, 0x144(r1): branch delay slot
    0xffffffff, // skipped
    0
  };
  for (std::size_t index = 0; index < sizeof(program) / sizeof(program[0]); ++index)
  {
    loadInstruction(&original, static_cast<IOPAddress>(index * 4), program[index]);
  }
  loadInstruction(&original, 0x100, 0x12345678);
  original.iopCore().startExecution(0xa0000000);
  original.iopCore().stepInstruction();
  original.iopCore().stepInstruction();
  REQUIRE(original.iopCore().delayedResultContinuation().source ==
    IOPDelayedResultSource::Load);
  REQUIRE(original.iopCore().generalRegister(2) == 0);
  original.iopCore().haltExecution();
  original.iopCore().resumeExecution();
  NekoSystem restored;
  REQUIRE(restored.iopBus().installRom(image));
  restored.loadState(original.saveState());
  REQUIRE(restored.iopCore().delayedResultContinuation().source ==
    IOPDelayedResultSource::Load);
  for (auto *system : {&original, &restored})
  {
    system->iopCore().stepInstruction();
    REQUIRE(system->iopCore().generalRegister(2) == 0x12345678);
    REQUIRE(system->iopCore().delayedResultContinuation().source ==
      IOPDelayedResultSource::None);
    system->iopCore().stepInstruction();
    REQUIRE(system->iopCore().branchContinuation().active);
  }
  REQUIRE(original.saveState() == restored.saveState());
  restored.loadState(original.saveState());
  REQUIRE(restored.iopCore().branchContinuation().active);
  const auto replacement = rom(0x24);
  for (auto *system : {&original, &restored})
  {
    REQUIRE(system->iopBus().installRom(replacement));
    system->iopCore().haltExecution();
    system->iopCore().resumeExecution();
    system->iopCore().stepInstruction();
    REQUIRE(system->iopCore().programCounter() == 0xa0000018);
    REQUIRE(system->iopCore().executionState() == IOPExecutionState::Running);
    REQUIRE(system->iopCore().exception().kind == IOPException::None);
    REQUIRE(system->iopCore().retiredInstructions() == 5);
  }
  REQUIRE(original.saveState() == restored.saveState());
  for (auto *system : {&original, &restored})
  {
    system->iopCore().haltExecution();
    system->iopCore().startExecution(0xa0000000);
    REQUIRE(system->iopCore().writeData32(0xfffe0140, 0x13579bdf).outcome ==
      IOPAccessOutcome::Completed);
    REQUIRE(system->iopCore().writeData32(0xfffe0144, 0x2468ace0).outcome ==
      IOPAccessOutcome::Completed);
    system->reset();
    const auto state = system->saveState();
    for (IOPAddress address : {0xfffe0140u, 0xfffe0144u})
    {
      REQUIRE(system->iopBus().write32(address, 0xffffffff).status ==
        IOPBusStatus::Completed);
      REQUIRE(system->iopBus().read32(address).status == IOPBusStatus::Unmapped);
    }
    REQUIRE(system->saveState() == state);
  }
  REQUIRE(original.saveState() == restored.saveState());
}

TEST_CASE("IOP persistence continues branch and delayed load without copied pointers")
{
  NekoSystem original;
  loadInstruction(&original, 0, 0x8c010100); // lw r1, 0x100(r0)
  loadInstruction(&original, 4, 0x10000002); // beq r0, r0, 0x10
  loadInstruction(&original, 8, 0x8c020104); // lw r2, 0x104(r0)
  loadInstruction(&original, 0x10, 0x24430001); // addiu r3, r2, 1
  loadInstruction(&original, 0x14, 0);
  loadInstruction(&original, 0x100, 0x12345678);
  loadInstruction(&original, 0x104, 0x87654321);
  original.iopCore().startExecution(0x80000000);
  original.iopCore().stepInstruction();
  const auto loadState = original.saveState();
  NekoSystem restored;
  restored.loadState(loadState);
  REQUIRE(restored.iopCore().delayedResultContinuation().source ==
    IOPDelayedResultSource::Load);
  REQUIRE(restored.iopStateHash() == original.iopStateHash());
  for (unsigned step = 0; step < 4; ++step)
  {
    original.iopCore().stepInstruction();
    restored.iopCore().stepInstruction();
    REQUIRE(restored.iopStateHash() == original.iopStateHash());
    REQUIRE(restored.saveState() == original.saveState());
    if (step == 0)
    {
      REQUIRE(restored.iopCore().branchContinuation().active);
      const auto branchState = restored.saveState();
      restored.loadState(branchState);
      REQUIRE(restored.saveState() == branchState);
    }
  }
  REQUIRE(restored.iopCore().generalRegister(1) == 0x12345678);
  REQUIRE(restored.iopCore().generalRegister(2) == 0x87654321);
  REQUIRE(restored.iopCore().generalRegister(3) == 1);
  original.iopBus().write32(0x100, 0xaaaaaaaa);
  REQUIRE(restored.iopBus().read32(0x100).value == 0x12345678);
  restored.iopInterruptController().writeMask(1);
  restored.iopInterruptController().writeControl(1);
  restored.iopInterruptController().assertSource(IOPInterruptSource::VBlankStart);
  REQUIRE(restored.iopBus().interruptControllerPending());
  REQUIRE_FALSE(original.iopBus().interruptControllerPending());
}

TEST_CASE("IOP persistence preserves timer requests phases and storage")
{
  NekoSystem original;
  original.iopTimers().writeTarget(0, 1);
  original.iopTimers().writeMode(0, IOPTimerMode::INTERRUPT_ON_TARGET);
  original.iopTimers().writeMode(2, IOPTimerMode::DIVIDE_BY_8);
  original.iopTimers().writeMode(4, IOPTimerMode::PRESCALER_MASK);
  original.iopTimers().writeMode(1, IOPTimerMode::GATE_ENABLE | 6);
  original.iopBus().write32(IOPBus::SCRATCHPAD_BASE + 1020, 0xdeadbeef);
  original.iopBus().write32(IOPBus::RAM_SIZE - 4, 0xcafebabe);
  advanceTimers(&original, 1);
  REQUIRE(original.iopTimers().counter(0).pulseReleasePending);
  const auto state = original.saveState();
  REQUIRE_NOTHROW(inspectSaveState(state));
  NekoSystem restored;
  restored.loadState(state);
  REQUIRE(restored.saveState() == state);
  REQUIRE(restored.iopStateHash() == original.iopStateHash());
  for (unsigned step = 0; step < 20; ++step)
  {
    advanceTimers(&original, 17);
    advanceTimers(&restored, 17);
    REQUIRE(restored.iopStateHash() == original.iopStateHash());
  }
  REQUIRE(restored.saveState() == original.saveState());
  REQUIRE(restored.iopBus().read32(IOPBus::SCRATCHPAD_BASE + 1020).value ==
    0xdeadbeef);
  restored.iopTimers().writeTarget(0, 99);
  restored.loadState(state);
  restored.iopBus().write32(0x1f801108, 23);
  REQUIRE(restored.iopTimers().counter(0).target == 23);
  REQUIRE(original.iopTimers().counter(0).target == 1);
}

TEST_CASE("IOP persistence ROM compatibility uses contents not host identity")
{
  NekoSystem original;
  REQUIRE(original.iopBus().installRom(rom(0x12)));
  REQUIRE(original.iopCore().readData32(0xbfc00000).outcome ==
    IOPAccessOutcome::Completed);
  const auto state = original.saveState();
  REQUIRE_NOTHROW(inspectSaveState(state));
  NekoSystem restored;
  REQUIRE(restored.iopBus().installRom(rom(0x12)));
  restored.loadState(state);
  REQUIRE(restored.saveState() == state);
  REQUIRE(restored.iopStateHash() == original.iopStateHash());
  REQUIRE(restored.iopCore().readData32(0xbfc00000).value == 0x12);
  REQUIRE(restored.saveState() == original.saveState());
  SECTION("different image")
  {
    REQUIRE(restored.iopBus().installRom(rom(0x13)));
  }
  SECTION("missing image")
  {
    NekoSystem absent;
    const auto before = absent.saveState();
    REQUIRE_THROWS_AS(absent.loadState(state), std::invalid_argument);
    REQUIRE(absent.saveState() == before);
    return;
  }
  const auto before = restored.saveState();
  REQUIRE_THROWS_AS(restored.loadState(state), std::invalid_argument);
  REQUIRE(restored.saveState() == before);
}

TEST_CASE("IOP persistence rejects malformed candidate state atomically")
{
  const std::vector<std::pair<std::string, std::uint64_t>> mutations = {
    {"system.iop.core.state", 2},
    {"system.iop.core.status", 0xffffffff},
    {"system.iop.core.cause", 1},
    {"system.iop.core.directInterruptInputs", 1},
    {"system.iop.core.hiWriteHazardInstructions", 3},
    {"system.iop.core.delayedResult.destination", 32},
    {"system.iop.core.delayedResult.source", 3},
    {"system.iop.core.exception.kind", 11},
    {"system.iop.core.storeFailure.outcome", 5},
    {"system.iop.core.branch.active", 2},
    {"system.iop.core.cache.profile", 0},
    {"system.iop.core.cache.instructionLineWords", 1},
    {"system.iop.core.cache.instructionLines[0].physicalTag", 1},
    {"system.iop.core.cache.dataLines[0].valid", 2},
    {"system.iop.interruptController.status", 0x04000000},
    {"system.iop.interruptController.mask", 0x04000000},
    {"system.iop.interruptController.control", 2},
    {"system.iop.timers.counters[0].count", 0x10000},
    {"system.iop.timers.counters[1].mode", 0x8000},
    {"system.iop.timers.counters[2].prescalerRemainder", 1},
    {"system.iop.timers.counters[3].gateWaiting", 1},
    {"system.iop.timers.counters[4].pulseReleasePending", 1},
    {"system.iop.timing.pixelPhase", 8192},
    {"system.iop.bus.ram.size", 1}
  };
  NekoSystem system;
  system.iopBus().write32(0x100, 0x1234);
  const auto before = system.saveState();
  const auto hash = system.iopStateHash();
  for (const auto &mutation : mutations)
  {
    CAPTURE(mutation.first);
    SaveStateMutation invalid(before);
    invalid.writeScalar(mutation.first, mutation.second);
    invalid.refreshChecksum();
    REQUIRE_THROWS_AS(system.loadState(invalid), std::invalid_argument);
    REQUIRE(system.saveState() == before);
    REQUIRE(system.iopStateHash() == hash);
  }
}

TEST_CASE("IOP persistence reset preserves external ROM and clears mutable owners")
{
  NekoSystem system;
  const auto image = rom(0x42);
  REQUIRE(system.iopBus().installRom(image));
  system.iopCore().startExecution(0x80000000);
  system.iopCore().stepInstruction();
  advanceTimers(&system, 19);
  system.iopBus().write32(0x100, 1);
  system.reset();
  REQUIRE(system.iopBus().romIdentity() == image);
  REQUIRE(system.iopCore().executionState() == IOPExecutionState::Halted);
  REQUIRE(system.iopCore().elapsedCycles() == 0);
  REQUIRE(system.iopBus().read32(0x100).value == 0);
  REQUIRE(system.iopTimers().counter(0).count == 0);
  REQUIRE(system.iopTimerTiming().pixelPhase() == 0);
  REQUIRE(system.iopCore().readData32(0xbfc00000).value == 0x42);
}

TEST_CASE("IOP persistence retains invalid cache payloads tags validity and CM")
{
  NekoSystem original;
  SaveStateMutation seeded(original.saveState());
  seeded.writeScalar("system.iop.core.status",
    original.iopCore().status() | IOPCOP0Status::CACHE_ISOLATED |
      IOPCOP0Status::CACHE_PROBE_RESULT);
  for (const std::string bank : {"instructionLines", "dataLines"})
  {
    const bool instruction = bank == "instructionLines";
    const auto lines = instruction
      ? IOPCache::INSTRUCTION_LINE_COUNT : IOPCache::DATA_LINE_COUNT;
    const auto words = instruction
      ? IOPCache::INSTRUCTION_LINE_WORDS : IOPCache::DATA_LINE_WORDS;
    for (std::size_t line = 0; line < lines; ++line)
    {
      const auto path = "system.iop.core.cache." + bank +
        "[" + std::to_string(line) + "]";
      seeded.writeScalar(path + ".physicalTag", 0x40000000);
      seeded.writeScalar(path + ".valid", line % 2);
      for (std::size_t word = 0; word < words; ++word)
      {
        seeded.writeScalar(path + ".words[" + std::to_string(word) + "].value",
          1 + line * words + word);
      }
    }
  }
  seeded.refreshChecksum();
  original.loadState(seeded);
  REQUIRE(original.saveState() == seeded.bytes());
  const auto hash = original.iopStateHash();
  REQUIRE(original.iopStateHash() == hash);
  REQUIRE(original.saveState() == seeded.bytes());
  NekoSystem restored;
  restored.loadState(original.saveState());
  REQUIRE(restored.iopStateHash() == hash);
  for (std::size_t line = 0; line < IOPCache::INSTRUCTION_LINE_COUNT; ++line)
  {
    REQUIRE(restored.iopCore().cache().instructionLine(line).words ==
      original.iopCore().cache().instructionLine(line).words);
    REQUIRE(restored.iopCore().cache().instructionLine(line).physicalTag ==
      0x40000000);
    REQUIRE(restored.iopCore().cache().instructionLine(line).valid ==
      (line % 2 != 0));
    REQUIRE(restored.iopCore().cache().dataLine(line).words ==
      original.iopCore().cache().dataLine(line).words);
    REQUIRE(restored.iopCore().cache().dataLine(line).valid == (line % 2 != 0));
  }
  REQUIRE(restored.iopCore().readData32(0xa0000000).value == 1);
  REQUIRE((restored.iopCore().status() & IOPCOP0Status::CACHE_PROBE_RESULT) != 0);
  REQUIRE(restored.iopCore().cache().dataLine(0).words[0] == 1);
}

TEST_CASE("IOP persistence hashes every register cache field and device continuation")
{
  NekoSystem system;
  const auto before = system.saveState();
  const auto initialHash = system.iopStateHash();
  std::vector<std::pair<std::string, std::uint64_t>> mutations = {
    {"system.iop.core.hi", 1},
    {"system.iop.core.lo", 1},
    {"system.iop.core.hiWriteHazardInstructions", 1},
    {"system.iop.core.loWriteHazardInstructions", 1},
    {"system.iop.core.unreadMultiplyDivideHI", 1},
    {"system.iop.core.unreadMultiplyDivideLO", 1},
    {"system.iop.core.status", system.iopCore().status() |
      IOPCOP0Status::CACHE_PROBE_RESULT},
    {"system.iop.core.cause", IOPCOP0Cause::SOFTWARE_INTERRUPT_PENDING},
    {"system.iop.core.directInterruptInputs", 1 << 10},
    {"system.iop.core.cache.instructionLines[0].physicalTag", 0x1000},
    {"system.iop.core.cache.instructionLines[0].valid", 1},
    {"system.iop.core.cache.instructionLines[255].words[3].value", 1},
    {"system.iop.core.cache.dataLines[0].physicalTag", 0x400},
    {"system.iop.core.cache.dataLines[0].valid", 1},
    {"system.iop.core.cache.dataLines[255].words[0].value", 1},
    {"system.iop.interruptController.status", 1},
    {"system.iop.interruptController.mask", 1},
    {"system.iop.interruptController.control", 1},
    {"system.iop.timers.horizontalBlank", 1},
    {"system.iop.timers.verticalBlank", 1},
    {"system.iop.timers.counters[0].count", 1},
    {"system.iop.timers.counters[5].target", 1},
    {"system.iop.timing.pixelPhase", 8191}
  };
  for (unsigned reg = 1; reg < 32; ++reg)
  {
    mutations.push_back({
      "system.iop.core.generalRegisters[" + std::to_string(reg) + "].value", reg});
  }
  for (const auto &mutation : mutations)
  {
    CAPTURE(mutation.first);
    SaveStateMutation changed(before);
    changed.writeScalar(mutation.first, mutation.second);
    changed.refreshChecksum();
    system.loadState(changed);
    REQUIRE(system.iopStateHash() != initialHash);
    REQUIRE(system.saveState() == changed.bytes());
    system.loadState(before);
    REQUIRE(system.iopStateHash() == initialHash);
  }
  system.iopBus().write32(IOPBus::RAM_SIZE - 4, 1);
  REQUIRE(system.iopStateHash() != initialHash);
  system.loadState(before);
  system.iopBus().write32(IOPBus::SCRATCHPAD_BASE, 1);
  REQUIRE(system.iopStateHash() != initialHash);
}

TEST_CASE("IOP persistence retains deferred ROM invalidation across restore")
{
  NekoSystem original;
  REQUIRE(original.iopBus().installRom(rom(0x12)));
  REQUIRE(original.iopCore().readData32(0x9fc00000).value == 0x12);
  REQUIRE(original.iopCore().cache().dataLine(0).valid);
  const auto synchronized = original.iopStateHash();
  // A new identity still invalidates caches when the bytes happen to match.
  REQUIRE(original.iopBus().installRom(rom(0x12)));
  REQUIRE(original.iopStateHash() != synchronized);
  const auto state = original.saveState();
  NekoSystem restored;
  REQUIRE(restored.iopBus().installRom(rom(0x12)));
  restored.loadState(state);
  REQUIRE(restored.saveState() == state);
  REQUIRE(restored.iopCore().cache().dataLine(0).valid);
  REQUIRE(original.iopCore().readData32(0xa0000000).value == 0);
  REQUIRE(restored.iopCore().readData32(0xa0000000).value == 0);
  REQUIRE_FALSE(restored.iopCore().cache().dataLine(0).valid);
  REQUIRE(restored.iopStateHash() == original.iopStateHash());
}

TEST_CASE("IOP persistence preserves multiply hazards COP0 delay and exception entry")
{
  NekoSystem original;
  const std::vector<IOPWord> program = {
    0x24010007, 0x24020003, 0x00220018, 0x00001810,
    0, 0, 0x00002012, 0x40056000, 0, 0x0000000c
  };
  for (std::size_t index = 0; index < program.size(); ++index)
  {
    loadInstruction(&original, static_cast<IOPAddress>(index * 4), program[index]);
  }
  original.iopCore().startExecution(0x80000000);
  for (std::size_t step = 0; step < program.size(); ++step)
  {
    NekoSystem restored;
    const auto state = original.saveState();
    restored.loadState(state);
    REQUIRE(restored.saveState() == state);
    original.iopCore().stepInstruction();
    restored.iopCore().stepInstruction();
    REQUIRE(restored.iopStateHash() == original.iopStateHash());
    REQUIRE(restored.saveState() == original.saveState());
  }
  REQUIRE(original.iopCore().exception().kind == IOPException::SystemCall);
  NekoSystem restored;
  restored.loadState(original.saveState());
  REQUIRE(restored.iopCore().exception().kind == IOPException::SystemCall);
  REQUIRE(restored.iopStateHash() == original.iopStateHash());
}

TEST_CASE("IOP persistence retains rejected store and its unretired delayed result")
{
  NekoSystem original;
  loadInstruction(&original, 0, 0x3c011000); // lui r1, 0x1000
  loadInstruction(&original, 4, 0x10000003); // beq r0, r0, 0x14
  loadInstruction(&original, 8, 0x8c020100); // lw r2, 0x100(r0)
  loadInstruction(&original, 0x14, 0xac200000); // sw r0, 0(r1)
  loadInstruction(&original, 0x100, 0x12345678);
  original.iopCore().startExecution(0x80000000);
  for (unsigned step = 0; step < 4; ++step)
  {
    original.iopCore().stepInstruction();
  }
  REQUIRE(original.iopCore().stopReason() == IOPStopReason::StoreFailure);
  REQUIRE(original.iopCore().delayedResultContinuation().source ==
    IOPDelayedResultSource::Load);
  const auto state = original.saveState();
  NekoSystem restored;
  restored.loadState(state);
  REQUIRE(restored.iopStateHash() == original.iopStateHash());
  REQUIRE(restored.saveState() == state);
  REQUIRE(restored.iopCore().storeFailure().width == IOPStoreWidth::Word);
  REQUIRE(restored.iopCore().storeFailure().access.physicalAddress == 0x10000000);
  REQUIRE_THROWS_AS(restored.iopCore().resumeExecution(), std::logic_error);
}
