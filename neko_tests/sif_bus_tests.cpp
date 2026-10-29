#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include "catch.hpp"
#include "neko_system.hpp"
#include "sif.hpp"

namespace
{
  constexpr std::uint32_t EE_BASE = 0x1000f200;
  constexpr std::uint32_t IOP_BASE = 0x1d000000;
  constexpr std::uint32_t IOP_DOORBELL = 0x1f801450;
  constexpr std::array<SIFRegister, 5> registers = {{
    SIFRegister::MSCOM, SIFRegister::SMCOM,
    SIFRegister::MSFLAG, SIFRegister::SMFLAG, SIFRegister::CTRL
  }};

  class SignalRecorder final : public SIFSignalReceiver
  {
    public:
      explicit SignalRecorder(const SIF &owner) : sif(owner) {}

      void requestSIFSignal(SIFSignal signal) noexcept override
      {
        if (signal == SIFSignal::RequestEE)
        {
          ++eeRequests;
        }
        else if (signal == SIFSignal::RequestIOP)
        {
          ++iopRequests;
        }
        else
        {
          ++emptyRequests;
        }
        controlSeen = sif.control();
        doorbellSeen = sif.iopDoorbell();
      }

      const SIF &sif;
      std::size_t eeRequests = 0;
      std::size_t iopRequests = 0;
      std::size_t emptyRequests = 0;
      std::uint32_t controlSeen = 0;
      std::uint32_t doorbellSeen = 0;
  };

  struct Fixture
  {
    NekoSystem system;
    SIF &sif = system.sif();
  };

  void seed(SIF &sif)
  {
    sif.writeEE(SIFRegister::MSCOM, 0x87654321);
    sif.writeIOP(SIFRegister::SMCOM, 0xfedcba98);
    sif.writeEE(SIFRegister::MSFLAG, 0xfffefdfc);
    sif.writeIOP(SIFRegister::SMFLAG, 0xfbfaf9f8);
    sif.writeIOP(SIFRegister::CTRL, 0x30);
    sif.writeEE(SIFRegister::CTRL, 0x100);
    sif.writeIOPDoorbell(0x12345678);
    REQUIRE(sif.pushSIF0Prologue({{1, 2, 3, 4}}) ==
      SIFTransferOutcome::Completed);
    REQUIRE(sif.pushSIF0PayloadWord(5, SIFPayloadBoundary::Continuing) ==
      SIFTransferOutcome::Completed);
    REQUIRE(sif.pushSIF1Quadword({{6, 7, 8, 9}}) ==
      SIFTransferOutcome::Completed);
  }

  void requireUnchanged(const SIF &sif, const SIF &before)
  {
    for (auto reg : registers)
    {
      REQUIRE(sif.readEE(reg) == before.readEE(reg));
      REQUIRE(sif.readIOP(reg) == before.readIOP(reg));
    }
    REQUIRE(sif.control() == before.control());
    REQUIRE(sif.iopDoorbell() == before.iopDoorbell());
    REQUIRE(sif.sif0PackingLatch() == before.sif0PackingLatch());
    REQUIRE(sif.sif0PackingCursor() == before.sif0PackingCursor());
    REQUIRE(sif.freeWordCount() == before.freeWordCount());
    for (auto direction : {SIFDirection::SIF0, SIFDirection::SIF1})
    {
      REQUIRE(sif.wordCount(direction) == before.wordCount(direction));
      for (std::size_t index = 0; index < sif.wordCount(direction); ++index)
      {
        REQUIRE(sif.wordAt(direction, index) == before.wordAt(direction, index));
      }
    }
  }

  std::uint32_t memoryInstruction(
    std::uint8_t opcode, std::uint16_t offset = 0)
  {
    return (std::uint32_t(opcode) << 26) | (1u << 21) | (2u << 16) | offset;
  }

  void executeEE(
    Fixture &fixture, std::uint8_t opcode, std::uint32_t address,
    std::uint32_t value, std::uint16_t offset = 0)
  {
    auto &system = fixture.system;
    system.eeBus().write32(0, memoryInstruction(opcode, offset));
    system.eeCore().setGeneralRegister(1, {address, 0});
    system.eeCore().setGeneralRegister(2, {value, 0});
    system.eeCore().startExecution(0xa0000000);
    system.stepEEInstruction(16);
  }

  void prepareIOP(
    Fixture &fixture, std::uint8_t opcode, std::uint32_t address,
    std::uint32_t value, std::uint16_t offset = 0)
  {
    auto &bus = fixture.system.iopBus();
    const std::array<std::uint32_t, 6> program = {{
      0x3c010000 | (address >> 16),
      0x34210000 | (address & 0xffff),
      0x3c020000 | (value >> 16),
      0x34420000 | (value & 0xffff),
      memoryInstruction(opcode, offset),
      0
    }};
    for (std::size_t index = 0; index < program.size(); ++index)
    {
      REQUIRE(bus.write32(static_cast<IOPAddress>(4 * index),
        program[index]).status == IOPBusStatus::Completed);
    }
    auto &core = fixture.system.iopCore();
    core.reset();
    core.startExecution(0xa0000000);
    for (unsigned int index = 0; index < 4; ++index)
    {
      core.stepInstruction();
    }
    REQUIRE(core.retiredInstructions() == 4);
  }

  void requireSingleEEWordStore(
    const Fixture &fixture, bool completed, std::uint32_t value)
  {
    std::size_t accesses = 0;
    for (const auto &event : fixture.system.trace())
    {
      if (event.subsystem != NekoTraceSubsystem::EE ||
          event.type != NekoTraceEventType::MemoryAccess ||
          event.value0 != 0xb000f240)
      {
        continue;
      }
      ++accesses;
      REQUIRE(event.value1 == value);
      REQUIRE(event.value2 == 0);
      REQUIRE(event.value3 == nekoPackEEMemoryTraceMetadata({
        EE_BASE + 0x40, 4,
        NekoEETraceMemory::AccessKind::DataStore,
        NekoEETraceMemory::TranslationOutcome::Translated,
        NekoEETraceMemory::CacheRoute::Uncached, 2, false, true,
        NekoEETraceMemory::CacheAccess::Bypassed,
        completed ? NekoEETraceMemory::TransferOutcome::Completed
                  : NekoEETraceMemory::TransferOutcome::PhysicalBusError,
        completed ? NekoEETraceMemory::FailurePhase::None
                  : NekoEETraceMemory::FailurePhase::Direct
      }));
    }
    REQUIRE(accesses == 1);
  }
}

TEST_CASE("SIF bus register views share one state owner", "[sif][bus]")
{
  Fixture fixture;
  auto &ee = fixture.system.eeBus();
  auto &iop = fixture.system.iopBus();
  REQUIRE(EEMemoryMap::SIF_BASE == EE_BASE);
  REQUIRE(IOPBus::SIF_BASE == IOP_BASE);
  REQUIRE(IOPBus::SIF_DOORBELL == IOP_DOORBELL);
  for (std::size_t index = 0; index < registers.size(); ++index)
  {
    const auto offset = static_cast<std::uint32_t>(index * 16);
    std::uint32_t value = 0x12345678;
    REQUIRE(ee.readData32(EE_BASE + offset, &value));
    REQUIRE(value == fixture.sif.readEE(registers[index]));
    const auto result = iop.read32(IOP_BASE + offset);
    REQUIRE(result.status == IOPBusStatus::Completed);
    REQUIRE(result.value == fixture.sif.readIOP(registers[index]));
  }
  REQUIRE(ee.writeData32(EE_BASE, 0x87654321));
  REQUIRE(iop.read32(IOP_BASE).value == 0x87654321);
  REQUIRE(iop.write32(IOP_BASE, 0).status == IOPBusStatus::Completed);
  REQUIRE(ee.read32(EE_BASE) == 0x87654321);
  REQUIRE(iop.write32(IOP_BASE + 0x10, 0xfedcba98).status ==
    IOPBusStatus::Completed);
  REQUIRE(ee.read32(EE_BASE + 0x10) == 0xfedcba98);
  REQUIRE(ee.writeData32(EE_BASE + 0x10, 0));
  REQUIRE(iop.read32(IOP_BASE + 0x10).value == 0xfedcba98);
  REQUIRE(ee.writeData32(EE_BASE + 0x20, 0xffffffff));
  REQUIRE(iop.write32(IOP_BASE + 0x20, 0x55555555).status ==
    IOPBusStatus::Completed);
  REQUIRE(ee.read32(EE_BASE + 0x20) == 0xaaaaaaaa);
  REQUIRE(iop.write32(IOP_BASE + 0x30, 0xffffffff).status ==
    IOPBusStatus::Completed);
  REQUIRE(ee.writeData32(EE_BASE + 0x30, 0xaaaaaaaa));
  REQUIRE(iop.read32(IOP_BASE + 0x30).value == 0x55555555);
  REQUIRE(ee.writeData32(EE_BASE + 0x40, 0));
  REQUIRE(ee.read32(EE_BASE + 0x40) == 0xf0000102);
  REQUIRE(iop.read32(IOP_BASE + 0x40).value == 0xf0000002);
  REQUIRE(iop.write32(IOP_BASE + 0x40, 0x10).status ==
    IOPBusStatus::Completed);
  REQUIRE(iop.write32(IOP_BASE + 0x40, 0x30).status ==
    IOPBusStatus::Completed);
  REQUIRE(fixture.sif.control() == 0x2000);
  REQUIRE(fixture.system.interruptController().status() == 0);
  REQUIRE(fixture.system.iopInterruptController().status() == 0);
}

TEST_CASE("SIF bus trigger effects are delivered once after mutation", "[sif][bus]")
{
  NekoSystem system;
  SIF sif;
  SignalRecorder signals(sif);
  EEBus ee(&system.vif0(), &system.vif1(), &system.gifRegisters(),
    &system.gifPath3(), &system.gs(), &system.interruptController());
  IOPBus iop;
  ee.attachSIF(&sif, &signals);
  iop.attachSIF(&sif, &signals);
  for (std::size_t repeat = 1; repeat <= 3; ++repeat)
  {
    REQUIRE(ee.writeData32(EE_BASE + 0x40, 0x40100));
    REQUIRE(signals.iopRequests == repeat);
    REQUIRE(signals.controlSeen == 0x100);
    REQUIRE(iop.write32(IOP_DOORBELL, 0xabcdef03).status ==
      IOPBusStatus::Completed);
    REQUIRE(signals.eeRequests == repeat);
    REQUIRE(signals.doorbellSeen == 0xabcdef03);
    REQUIRE(iop.read32(IOP_DOORBELL).status == IOPBusStatus::Completed);
    REQUIRE(iop.read32(IOP_DOORBELL).value == 0xabcdef03);
  }
  REQUIRE(iop.write32(IOP_DOORBELL, 0).status == IOPBusStatus::Completed);
  REQUIRE(ee.writeData32(EE_BASE + 0x40, 0));
  REQUIRE(signals.eeRequests == 3);
  REQUIRE(signals.iopRequests == 3);
  REQUIRE(signals.emptyRequests == 0);
  REQUIRE(system.interruptController().status() == 0);
  REQUIRE(system.iopInterruptController().status() == 0);
  REQUIRE(system.masterClockScheduler().currentCycle() == 0);
}

TEST_CASE("SIF bus IOP register60 word reads return zero without effects",
  "[sif][bus][register60]")
{
  Fixture fixture;
  for (bool seeded : {false, true})
  {
    if (seeded)
    {
      seed(fixture.sif);
    }
    const SIF before = fixture.sif;
    const auto hash = fixture.system.sifStateHash();
    for (unsigned int repeat = 0; repeat < 3; ++repeat)
    {
      const auto result = fixture.system.iopBus().read32(IOP_BASE + 0x60);
      REQUIRE(result.status == IOPBusStatus::Completed);
      REQUIRE(result.value == 0);
    }
    requireUnchanged(fixture.sif, before);
    REQUIRE(fixture.system.sifStateHash() == hash);
  }
  auto &iop = fixture.system.iopBus();
  for (std::uint32_t lane : {1u, 2u, 3u})
  {
    REQUIRE(iop.read32(IOP_BASE + 0x60 + lane).status == IOPBusStatus::Misaligned);
  }
  REQUIRE(iop.read32(0xbd000060).status == IOPBusStatus::Unmapped);
  IOPBus detached;
  REQUIRE(detached.read32(IOP_BASE + 0x60).status == IOPBusStatus::Unmapped);
  std::array<std::uint8_t, 4> bytes = {{1, 2, 3, 4}};
  REQUIRE(iop.inspectPhysical(IOP_BASE + 0x60, bytes.data(), bytes.size()) ==
    IOPBusStatus::Unmapped);
  REQUIRE(bytes == std::array<std::uint8_t, 4>{{1, 2, 3, 4}});
  REQUIRE(fixture.system.interruptController().status() == 0);
  REQUIRE(fixture.system.iopInterruptController().status() == 0);
  REQUIRE(fixture.system.masterClockScheduler().currentCycle() == 0);
}

TEST_CASE("SIF bus IOP register60 guest load respects delay and trace contracts",
  "[sif][bus][guest][register60]")
{
  std::uint64_t untracedIOPHash = 0;
  for (bool traced : {false, true})
  {
    Fixture fixture;
    seed(fixture.sif);
    const SIF before = fixture.sif;
    const auto hash = fixture.system.sifStateHash();
    prepareIOP(fixture, 0x23, 0xbd000060, 0x87654321);
    if (traced)
    {
      fixture.system.startTrace();
    }
    auto &core = fixture.system.iopCore();
    fixture.system.runMasterCycles(1);
    REQUIRE(core.exception().kind == IOPException::None);
    REQUIRE(core.stopReason() == IOPStopReason::None);
    REQUIRE(core.retiredInstructions() == 5);
    REQUIRE(core.generalRegister(2) == 0x87654321);
    fixture.system.runMasterCycles(8);
    REQUIRE(core.retiredInstructions() == 6);
    REQUIRE(core.generalRegister(2) == 0);
    REQUIRE(core.exception().kind == IOPException::None);
    REQUIRE(core.stopReason() == IOPStopReason::None);
    REQUIRE(core.programCounter() == 0xa0000018);
    requireUnchanged(fixture.sif, before);
    REQUIRE(fixture.system.sifStateHash() == hash);
    REQUIRE(fixture.system.interruptController().status() == 0);
    REQUIRE(fixture.system.iopInterruptController().status() == 0);
    if (!traced)
    {
      untracedIOPHash = fixture.system.iopStateHash();
      REQUIRE(fixture.system.trace().empty());
      continue;
    }
    fixture.system.stopTrace();
    REQUIRE(fixture.system.iopStateHash() == untracedIOPHash);
    std::size_t reads = 0;
    namespace Memory = NekoIOPTraceMemory;
    for (const auto &event : fixture.system.trace())
    {
      REQUIRE(event.subsystem != NekoTraceSubsystem::SIF);
      if (event.subsystem != NekoTraceSubsystem::IOP ||
          event.type != NekoTraceEventType::MemoryAccess ||
          event.value0 != 0xbd000060)
      {
        continue;
      }
      ++reads;
      REQUIRE(event.masterCycle == 1);
      REQUIRE(event.value1 == 0);
      REQUIRE(event.value2 == 0);
      REQUIRE(event.value3 == nekoPackIOPMemoryTraceMetadata({
        IOP_BASE + 0x60, 4, Memory::AccessKind::DataLoad,
        Memory::Outcome::Completed, Memory::CacheRoute::Uncached, true, 0}));
    }
    REQUIRE(reads == 1);
  }
}

TEST_CASE("SIF bus decode rejects padding auxiliary words and shadow aliases",
  "[sif][bus]")
{
  Fixture fixture;
  seed(fixture.sif);
  const SIF before = fixture.sif;
  for (std::uint32_t offset = 0; offset < 0x100; offset += 4)
  {
    if (offset <= 0x40 && offset % 16 == 0)
    {
      continue;
    }
    CAPTURE(offset);
    std::uint32_t output = 0x76543210;
    REQUIRE_FALSE(fixture.system.eeBus().readData32(EE_BASE + offset, &output));
    REQUIRE(output == 0x76543210);
    REQUIRE_FALSE(fixture.system.eeBus().writeData32(EE_BASE + offset, 0xffffffff));
    if (offset != 0x60)
    {
      REQUIRE(fixture.system.iopBus().read32(IOP_BASE + offset).status ==
        IOPBusStatus::Unmapped);
    }
    REQUIRE(fixture.system.iopBus().write32(IOP_BASE + offset, 0xffffffff).status ==
      IOPBusStatus::Unmapped);
  }
  for (std::uint32_t offset : {0x100u, 0x800u, 0x1000u})
  {
    std::uint32_t value = 0;
    REQUIRE_FALSE(fixture.system.eeBus().readData32(EE_BASE + offset, &value));
    REQUIRE_FALSE(fixture.system.eeBus().writeData32(EE_BASE + offset, 0xffffffff));
    REQUIRE(fixture.system.iopBus().read32(IOP_BASE + offset).status ==
      IOPBusStatus::Unmapped);
    REQUIRE(fixture.system.iopBus().write32(IOP_BASE + offset, 0xffffffff).status ==
      IOPBusStatus::Unmapped);
  }
  requireUnchanged(fixture.sif, before);
  REQUIRE(fixture.system.interruptController().status() == 0);
  REQUIRE(fixture.system.iopInterruptController().status() == 0);
}

TEST_CASE("SIF bus narrow and EE wide accesses reject without effects", "[sif][bus]")
{
  Fixture fixture;
  seed(fixture.sif);
  const SIF before = fixture.sif;
  auto &ee = fixture.system.eeBus();
  auto &iop = fixture.system.iopBus();
  for (std::uint32_t offset : {0u, 0x10u, 0x20u, 0x30u, 0x40u, 0x60u})
  {
    for (std::uint32_t lane = 0; lane < 4; ++lane)
    {
      std::uint8_t byte = 0x5a;
      REQUIRE_FALSE(ee.readData8(EE_BASE + offset + lane, &byte));
      REQUIRE(byte == 0x5a);
      REQUIRE_FALSE(ee.writeData8(EE_BASE + offset + lane, 0xff));
      REQUIRE(iop.read8(IOP_BASE + offset + lane).status == IOPBusStatus::Unmapped);
      REQUIRE(iop.write8(IOP_BASE + offset + lane, 0xff).status ==
        IOPBusStatus::Unmapped);
    }
    for (std::uint32_t lane : {0u, 2u})
    {
      std::uint16_t half = 0x5a5a;
      REQUIRE_FALSE(ee.readData16(EE_BASE + offset + lane, &half));
      REQUIRE(half == 0x5a5a);
      REQUIRE_FALSE(ee.writeData16(EE_BASE + offset + lane, 0xffff));
      REQUIRE(iop.read16(IOP_BASE + offset + lane).status == IOPBusStatus::Unmapped);
      REQUIRE(iop.write16(IOP_BASE + offset + lane, 0xffff).status ==
        IOPBusStatus::Unmapped);
    }
    std::uint64_t wide = 0x123456789abcdef0;
    EEQuadword quad = {0x12345678, 0xabcdef00};
    REQUIRE_FALSE(ee.readData64(EE_BASE + offset, &wide));
    REQUIRE(wide == 0x123456789abcdef0);
    REQUIRE_FALSE(ee.writeData64(EE_BASE + offset, 0x40100));
    REQUIRE_FALSE(ee.readData128(EE_BASE + offset, &quad));
    REQUIRE(quad.low == 0x12345678);
    REQUIRE(quad.high == 0xabcdef00);
    REQUIRE_FALSE(ee.writeData128(EE_BASE + offset, {0x40100, 0}));
    REQUIRE(ee.writeGuestData128(EE_BASE + offset, {0x40100, 0}) ==
      EEDataWriteResult::Failed);
  }
  REQUIRE(iop.read8(IOP_DOORBELL).status == IOPBusStatus::Unmapped);
  REQUIRE(iop.read16(IOP_DOORBELL).status == IOPBusStatus::Unmapped);
  REQUIRE(iop.write8(IOP_DOORBELL, 2).status == IOPBusStatus::Unmapped);
  REQUIRE(iop.write16(IOP_DOORBELL, 2).status == IOPBusStatus::Unmapped);
  REQUIRE(iop.read32(IOP_DOORBELL + 4).status == IOPBusStatus::Unmapped);
  requireUnchanged(fixture.sif, before);
  REQUIRE(fixture.system.interruptController().status() == 0);
  REQUIRE(fixture.system.iopInterruptController().status() == 0);
}

TEST_CASE("SIF bus host wide fallbacks cannot commit a first word", "[sif][bus]")
{
  Fixture fixture;
  seed(fixture.sif);
  const SIF before = fixture.sif;
  auto &ee = fixture.system.eeBus();
  for (std::uint32_t offset : {0u, 0x10u, 0x20u, 0x30u, 0x40u})
  {
    REQUIRE_THROWS_AS(ee.write64(EE_BASE + offset, 0x40100), std::out_of_range);
    requireUnchanged(fixture.sif, before);
    REQUIRE_THROWS_AS(ee.read64(EE_BASE + offset), std::out_of_range);
    REQUIRE_THROWS_AS(ee.readQuadword(EE_BASE + offset), std::out_of_range);
    REQUIRE_THROWS_AS(ee.writeQuadword(EE_BASE + offset, {{0x40100, 0, 0, 0}}),
      std::out_of_range);
  }
  REQUIRE_THROWS_AS(ee.write64(0xb000f240, 0x40100), std::out_of_range);
  requireUnchanged(fixture.sif, before);
  REQUIRE(fixture.system.interruptController().status() == 0);
  REQUIRE(fixture.system.iopInterruptController().status() == 0);
}

TEST_CASE("SIF bus partial IOP byte enables reject before register commands", "[sif][bus]")
{
  Fixture fixture;
  seed(fixture.sif);
  const SIF before = fixture.sif;
  for (std::uint32_t address :
      {IOP_BASE, IOP_BASE + 0x10, IOP_BASE + 0x20,
       IOP_BASE + 0x30, IOP_BASE + 0x40, IOP_BASE + 0x60, IOP_DOORBELL})
  {
    for (std::uint8_t mask = 1; mask < 0xf; ++mask)
    {
      REQUIRE(fixture.system.iopBus().writeMasked32(address, 0xffffffff, mask).status ==
        IOPBusStatus::Unmapped);
    }
  }
  requireUnchanged(fixture.sif, before);
  REQUIRE(fixture.system.interruptController().status() == 0);
  REQUIRE(fixture.system.iopInterruptController().status() == 0);
  REQUIRE(fixture.system.iopBus().writeMasked32(IOP_DOORBELL, 0x1234567b, 0xf).status ==
    IOPBusStatus::Completed);
  REQUIRE(fixture.sif.iopDoorbell() == 0x1234567b);
  REQUIRE(fixture.system.interruptController().status() == 2);
}

TEST_CASE("SIF bus combined unsupported EE CTRL commands reject all effects",
  "[sif][bus]")
{
  Fixture fixture;
  seed(fixture.sif);
  const SIF before = fixture.sif;
  for (std::uint32_t value : {0x80000u, 0xc0000u, 0xc0100u, 0xffffffffu})
  {
    REQUIRE_FALSE(fixture.system.eeBus().writeData32(EE_BASE + 0x40, value));
    REQUIRE_THROWS_AS(fixture.system.eeBus().write32(EE_BASE + 0x40, value),
      std::invalid_argument);
    requireUnchanged(fixture.sif, before);
  }
  REQUIRE(fixture.system.iopInterruptController().status() == 0);
}

TEST_CASE("SIF bus alignment and physical-only boundaries precede devices",
  "[sif][bus]")
{
  Fixture fixture;
  seed(fixture.sif);
  const SIF before = fixture.sif;
  auto &ee = fixture.system.eeBus();
  auto &iop = fixture.system.iopBus();
  std::uint32_t word = 0x12345678;
  REQUIRE_THROWS_AS(ee.readData32(EE_BASE + 1, &word), std::invalid_argument);
  REQUIRE_THROWS_AS(ee.writeData32(EE_BASE + 1, 0xffffffff), std::invalid_argument);
  REQUIRE(word == 0x12345678);
  REQUIRE(iop.read32(IOP_BASE + 1).status == IOPBusStatus::Misaligned);
  REQUIRE(iop.write32(IOP_BASE + 1, 0xffffffff).status == IOPBusStatus::Misaligned);
  REQUIRE(iop.writeMasked32(IOP_DOORBELL + 1, 2, 0xf).status ==
    IOPBusStatus::Misaligned);
  REQUIRE_FALSE(ee.readData32(0xb000f200, &word));
  REQUIRE_FALSE(ee.writeData32(0xb000f200, 0xffffffff));
  REQUIRE(iop.read32(0xbd000000).status == IOPBusStatus::Unmapped);
  REQUIRE(iop.write32(0xbd000000, 0xffffffff).status == IOPBusStatus::Unmapped);
  REQUIRE(iop.read32(0xbf801450).status == IOPBusStatus::Unmapped);
  REQUIRE(iop.write32(0xbf801450, 2).status == IOPBusStatus::Unmapped);
  std::array<std::uint8_t, 4> bytes = {{1, 2, 3, 4}};
  REQUIRE(iop.inspectPhysical(IOP_BASE, bytes.data(), bytes.size()) ==
    IOPBusStatus::Unmapped);
  REQUIRE(iop.loadPhysical(IOP_DOORBELL, bytes.data(), bytes.size()) ==
    IOPBusStatus::Unmapped);
  REQUIRE(bytes == std::array<std::uint8_t, 4>{{1, 2, 3, 4}});
  REQUIRE_FALSE(ee.readInstruction32(EE_BASE, &word));
  requireUnchanged(fixture.sif, before);
}

TEST_CASE("SIF bus attachments require one owner and a live signal receiver",
  "[sif][bus]")
{
  NekoSystem system;
  SIF sif;
  SignalRecorder signals(sif);
  EEBus ee(&system.vif0(), &system.vif1(), &system.gifRegisters(),
    &system.gifPath3(), &system.gs(), &system.interruptController());
  IOPBus iop;
  std::uint32_t value = 0x12345678;
  REQUIRE_FALSE(ee.readData32(EE_BASE, &value));
  REQUIRE(value == 0x12345678);
  REQUIRE_FALSE(ee.writeData32(EE_BASE, 0xffffffff));
  REQUIRE(iop.read32(IOP_BASE).status == IOPBusStatus::Unmapped);
  REQUIRE(iop.write32(IOP_DOORBELL, 2).status == IOPBusStatus::Unmapped);
  REQUIRE_THROWS_AS(ee.attachSIF(nullptr, &signals), std::invalid_argument);
  REQUIRE_THROWS_AS(ee.attachSIF(&sif, nullptr), std::invalid_argument);
  REQUIRE_THROWS_AS(iop.attachSIF(nullptr, &signals), std::invalid_argument);
  REQUIRE_THROWS_AS(iop.attachSIF(&sif, nullptr), std::invalid_argument);
  ee.attachSIF(&sif, &signals);
  iop.attachSIF(&sif, &signals);
  REQUIRE_THROWS_AS(ee.attachSIF(&sif, &signals), std::logic_error);
  REQUIRE_THROWS_AS(iop.attachSIF(&sif, &signals), std::logic_error);
  seed(sif);
  const SIF before = sif;
  iop.reset();
  requireUnchanged(sif, before);
  REQUIRE(iop.read32(IOP_BASE).value == before.readIOP(SIFRegister::MSCOM));
  REQUIRE(signals.eeRequests == 0);
  REQUIRE(signals.iopRequests == 0);
}

TEST_CASE("SIF bus guest aliases reach the canonical views", "[sif][bus][guest]")
{
  Fixture fixture;
  executeEE(fixture, 0x2b, 0xb000f200, 0x87654321);
  REQUIRE(fixture.system.eeCore().pendingException() == EEException::None);
  REQUIRE(fixture.sif.readIOP(SIFRegister::MSCOM) == 0x87654321);
  executeEE(fixture, 0x23, 0xb000f200, 0);
  REQUIRE(fixture.system.eeCore().generalRegister(2).low == UINT64_C(0xffffffff87654321));
  prepareIOP(fixture, 0x2b, 0xbd000010, 0xfedcba98);
  fixture.system.iopCore().stepInstruction();
  REQUIRE(fixture.system.iopCore().stopReason() == IOPStopReason::None);
  REQUIRE(fixture.sif.readEE(SIFRegister::SMCOM) == 0xfedcba98);
  prepareIOP(fixture, 0x23, 0xbd000000, 0);
  fixture.system.iopCore().stepInstruction();
  REQUIRE(fixture.system.iopCore().generalRegister(2) == 0);
  fixture.system.iopCore().stepInstruction();
  REQUIRE(fixture.system.iopCore().generalRegister(2) == 0x87654321);
  prepareIOP(fixture, 0x2b, 0xbf801450, 0x80000002);
  fixture.system.iopCore().stepInstruction();
  REQUIRE(fixture.sif.iopDoorbell() == 0x80000002);
  REQUIRE(fixture.system.interruptController().status() == 2);
}

TEST_CASE("SIF bus EE merge stores reject partial lanes before any command",
  "[sif][bus][guest]")
{
  Fixture fixture;
  seed(fixture.sif);
  for (std::uint8_t opcode : {0x2a, 0x2e})
  {
    for (std::uint16_t lane = 0; lane < 4; ++lane)
    {
      if (lane == (opcode == 0x2a ? 3 : 0))
      {
        continue;
      }
      CAPTURE(opcode);
      CAPTURE(lane);
      const SIF before = fixture.sif;
      fixture.system.clearTrace();
      fixture.system.startTrace();
      executeEE(fixture, opcode, 0xb000f240, 0, lane);
      fixture.system.stopTrace();
      REQUIRE(fixture.system.eeCore().pendingException() == EEException::DataBusErrorStore);
      REQUIRE(fixture.system.eeCore().exceptionAddress() == 0xb000f240 + lane);
      requireUnchanged(fixture.sif, before);
      REQUIRE(fixture.system.iopInterruptController().status() == 0);
      requireSingleEEWordStore(fixture, false, 0);
    }
  }
}

TEST_CASE("SIF bus full EE merge stores issue one direct word command",
  "[sif][bus][guest]")
{
  Fixture fixture;
  for (std::uint8_t opcode : {0x2a, 0x2e})
  {
    fixture.system.iopInterruptController().writeStatus(~UINT32_C(2));
    fixture.system.clearTrace();
    fixture.system.startTrace();
    executeEE(fixture, opcode, 0xb000f240, 0x40100, opcode == 0x2a ? 3 : 0);
    fixture.system.stopTrace();
    REQUIRE(fixture.system.eeCore().pendingException() == EEException::None);
    REQUIRE(fixture.sif.control() == 0x100);
    REQUIRE(fixture.system.iopInterruptController().status() == 2);
    requireSingleEEWordStore(fixture, true, 0x40100);
  }
}

TEST_CASE("SIF bus IOP merge byte enables preserve whole-word-only commands",
  "[sif][bus][guest]")
{
  Fixture fixture;
  for (std::uint32_t address :
      {0xbd000030u, 0xbd000040u, 0xbf801450u})
  {
    for (std::uint8_t opcode : {0x2a, 0x2e})
    {
      for (std::uint16_t lane = 0; lane < 4; ++lane)
      {
        CAPTURE(address);
        CAPTURE(opcode);
        CAPTURE(lane);
        fixture.sif.reset();
        seed(fixture.sif);
        SIF expected = fixture.sif;
        const bool fullWord = lane == (opcode == 0x2a ? 3 : 0);
        fixture.system.interruptController().acknowledge(2);
        if (fullWord)
        {
          if (address == 0xbf801450)
          {
            expected.writeIOPDoorbell(0xffffffb3);
          }
          else
          {
            expected.writeIOP(
              address == 0xbd000030 ? SIFRegister::SMFLAG : SIFRegister::CTRL,
              0xffffffb3);
          }
        }
        prepareIOP(fixture, opcode, address, 0xffffffb3, lane);
        auto &core = fixture.system.iopCore();
        core.stepInstruction();
        REQUIRE(core.stopReason() ==
          (fullWord ? IOPStopReason::None : IOPStopReason::StoreFailure));
        REQUIRE(core.retiredInstructions() == (fullWord ? 5 : 4));
        if (!fullWord)
        {
          const auto mask = opcode == 0x2a
            ? (1u << (lane + 1)) - 1 : (0xfu << lane) & 0xf;
          REQUIRE(core.storeFailure().mergeByteEnable == mask);
        }
        requireUnchanged(fixture.sif, expected);
        REQUIRE(fixture.system.interruptController().status() ==
          (fullWord && address == 0xbf801450 ? 2 : 0));
        REQUIRE(fixture.system.iopInterruptController().status() == 0);
      }
    }
  }
}

TEST_CASE("SIF bus rejected guest accesses retain CPU failure contracts",
  "[sif][bus][guest]")
{
  Fixture fixture;
  seed(fixture.sif);
  const SIF before = fixture.sif;
  for (std::uint8_t opcode : {0x28, 0x29, 0x3f, 0x1f})
  {
    executeEE(fixture, opcode, 0xb000f240, 0x40100);
    REQUIRE(fixture.system.eeCore().pendingException() == EEException::DataBusErrorStore);
    REQUIRE(fixture.system.eeCore().exceptionAddress() == 0xb000f240);
    requireUnchanged(fixture.sif, before);
  }
  executeEE(fixture, 0x2b, 0xb000f240, 0xc0100);
  REQUIRE(fixture.system.eeCore().pendingException() == EEException::DataBusErrorStore);
  requireUnchanged(fixture.sif, before);
  for (std::uint8_t opcode : {0x28, 0x29, 0x2b, 0x2a, 0x2e})
  {
    const std::uint16_t lane = opcode == 0x2e ? 1 : 0;
    const auto address = opcode == 0x2b ? 0xbd000044 : 0xbf801450;
    prepareIOP(fixture, opcode, address, 0xffffffff, lane);
    auto &core = fixture.system.iopCore();
    core.stepInstruction();
    REQUIRE(core.stopReason() == IOPStopReason::StoreFailure);
    REQUIRE(core.retiredInstructions() == 4);
    REQUIRE(core.programCounter() == 0xa0000010);
    REQUIRE(core.exception().kind == IOPException::None);
    REQUIRE(core.storeFailure().access.outcome == IOPAccessOutcome::Unmapped);
    REQUIRE(core.storeFailure().access.virtualAddress == address + lane);
    REQUIRE(core.storeFailure().access.physicalAddress ==
      (address & 0x1fffffff) + lane);
    if (opcode == 0x2a || opcode == 0x2e)
    {
      REQUIRE(core.storeFailure().mergeByteEnable == (opcode == 0x2a ? 1 : 0xe));
    }
    requireUnchanged(fixture.sif, before);
  }
  REQUIRE(fixture.system.interruptController().status() == 0);
  REQUIRE(fixture.system.iopInterruptController().status() == 0);
}
