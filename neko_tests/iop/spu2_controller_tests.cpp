#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "catch.hpp"
#include "iop_trace_test_utils.hpp"
#include "neko_system.hpp"
#include "save_state_mutation.hpp"

namespace
{
  constexpr std::array<IOPAddress, 4> configurationAddresses = {{
    0x1f9007c6, 0x1f9007c8, 0x1f9007cc, 0x1f9007ca
  }};
  constexpr std::array<std::uint16_t, 4> configurationValues = {{
    0x0900, 0xa55a, 0x8001, 0x0008
  }};

  void programConfiguration(NekoSystem &system)
  {
    for (std::size_t index = 0; index < configurationAddresses.size(); ++index)
    {
      REQUIRE(system.iopBus().write16(
        configurationAddresses[index], configurationValues[index]).status ==
        IOPBusStatus::Completed);
    }
  }

  void installConfigurationProgram(NekoSystem &system, IOPAddress address)
  {
    const std::array<IOPWord, 13> words = {{
      0x3c010000 | (address >> 16), 0x34210000 | (address & 0xffff),
      0x34028900, 0x10000002, 0xa4220000, 0xffffffff,
      0x84230000, 0xac030100, 0xac030104,
      0x94240000, 0xac040108, 0xac04010c, 0x24051234
    }};
    for (std::size_t index = 0; index < words.size(); ++index)
    {
      REQUIRE(system.iopBus().write32(static_cast<IOPAddress>(index * 4),
        words[index]).status == IOPBusStatus::Completed);
    }
    system.iopCore().startExecution(0xa0000000);
  }

  void requireConfigurationResult(NekoSystem &system, std::size_t selected)
  {
    REQUIRE(system.iopCore().retiredInstructions() == 12);
    REQUIRE(system.iopCore().programCounter() == 0xa0000034);
    REQUIRE(system.iopCore().exception().kind == IOPException::None);
    REQUIRE(system.iopCore().executionState() == IOPExecutionState::Running);
    REQUIRE_FALSE(system.iopCore().branchContinuation().active);
    REQUIRE(system.iopCore().delayedResultContinuation().source ==
      IOPDelayedResultSource::None);
    REQUIRE(system.iopCore().generalRegister(3) == 0xffff8900);
    REQUIRE(system.iopCore().generalRegister(4) == 0x8900);
    REQUIRE(system.iopCore().generalRegister(5) == 0x1234);
    REQUIRE(system.iopBus().read32(0x100).value == 0);
    REQUIRE(system.iopBus().read32(0x104).value == 0xffff8900);
    REQUIRE(system.iopBus().read32(0x108).value == 0);
    REQUIRE(system.iopBus().read32(0x10c).value == 0x8900);
    for (std::size_t index = 0; index < configurationAddresses.size(); ++index)
    {
      REQUIRE(system.iopBus().read16(configurationAddresses[index]).value ==
        (index == selected ? 0x8900 : configurationValues[index]));
    }
    REQUIRE(system.iopInterruptController().status() == 0);
  }
}

TEST_CASE("SPU2 S/PDIF configuration retains independent halfwords",
  "[iop][spu2]")
{
  NekoSystem system;
  for (auto address : configurationAddresses)
  {
    CAPTURE(address);
    REQUIRE(system.iopBus().read16(address).status == IOPBusStatus::Completed);
    REQUIRE(system.iopBus().read16(address).value == 0);
  }
  programConfiguration(system);
  for (std::size_t selected = 0; selected < configurationAddresses.size(); ++selected)
  {
    for (auto value : {0xffff, 0, 0x0008, 0x0900, 0x55aa, 0x8001})
    {
      REQUIRE(system.iopBus().write16(configurationAddresses[selected],
        static_cast<std::uint16_t>(value)).status == IOPBusStatus::Completed);
      for (std::size_t index = 0; index < configurationAddresses.size(); ++index)
      {
        REQUIRE(system.iopBus().read16(configurationAddresses[index]).value ==
          (index == selected ? value : configurationValues[index]));
      }
    }
    programConfiguration(system);
  }
  REQUIRE(system.iopInterruptController().status() == 0);
}

TEST_CASE("SPU2 configuration excludes neighbors widths and non-CPU transports",
  "[iop][spu2]")
{
  NekoSystem system;
  programConfiguration(system);
  auto &bus = system.iopBus();
  const auto before = system.iopStateHash();
  for (auto address : configurationAddresses)
  {
    CAPTURE(address);
    REQUIRE(bus.read8(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read8(address + 1).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(address, 0xff).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(address + 1, 0xff).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read16(address + 1).status == IOPBusStatus::Misaligned);
    REQUIRE(bus.write16(address + 1, 0).status == IOPBusStatus::Misaligned);
    const auto wordAddress = address & ~IOPAddress(3);
    REQUIRE(bus.read32(wordAddress).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(wordAddress, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.readInstruction32(wordAddress).status == IOPBusStatus::Unmapped);
    for (std::uint8_t mask = 1; mask <= 0xf; ++mask)
    {
      REQUIRE(bus.writeMasked32(wordAddress, 0, mask).status ==
        IOPBusStatus::Unmapped);
    }
    REQUIRE(bus.readDMAC32(wordAddress).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.writeDMAC32(wordAddress, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.dmaWriteStatus(wordAddress) == IOPBusStatus::Unmapped);
    std::array<std::uint8_t, 2> bytes = {{0x12, 0x34}};
    REQUIRE(bus.loadPhysical(address, bytes.data(), bytes.size()) ==
      IOPBusStatus::Unmapped);
    REQUIRE(bus.inspectPhysical(address, bytes.data(), bytes.size()) ==
      IOPBusStatus::Unmapped);
    REQUIRE(bytes == (std::array<std::uint8_t, 2>{{0x12, 0x34}}));
    for (auto segment : {0x80000000u, 0xa0000000u})
    {
      REQUIRE(bus.read16(address | segment).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write16(address | segment, 0).status == IOPBusStatus::Unmapped);
    }
    std::uint16_t eeValue = 0x4321;
    REQUIRE_FALSE(system.eeBus().readData16(address, &eeValue));
    REQUIRE(eeValue == 0x4321);
    REQUIRE_FALSE(system.eeBus().writeData16(address, 0));
    IOPBus unattached;
    REQUIRE(unattached.read16(address).status == IOPBusStatus::Unmapped);
    REQUIRE(unattached.write16(address, 0).status == IOPBusStatus::Unmapped);
  }
  for (IOPAddress address : {0x1f9007c0u, 0x1f9007c2u, 0x1f9007c4u,
      0x1f9007ceu, 0x1f9007d0u, 0x1f9003c6u})
  {
    REQUIRE(bus.read16(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write16(address, 0xffff).status == IOPBusStatus::Unmapped);
  }
  REQUIRE(system.iopStateHash() == before);
}

TEST_CASE("SPU2 configuration survives time and local resets but not owner reset",
  "[iop][spu2]")
{
  NekoSystem system;
  programConfiguration(system);
  system.runMasterCycles(80);
  system.iopBus().reset();
  system.iopCore().reset();
  for (std::size_t index = 0; index < configurationAddresses.size(); ++index)
  {
    REQUIRE(system.iopBus().read16(configurationAddresses[index]).value ==
      configurationValues[index]);
  }
  system.spu2Controller().reset();
  for (auto address : configurationAddresses)
  {
    REQUIRE(system.iopBus().read16(address).value == 0);
  }
  programConfiguration(system);
  system.reset();
  for (auto address : configurationAddresses)
  {
    REQUIRE(system.iopBus().read16(address).value == 0);
    REQUIRE(system.iopBus().write16(address, 0xffff).status ==
      IOPBusStatus::Completed);
  }
}

TEST_CASE("SPU2 halfword guests preserve aliases delay slots and signed load delays",
  "[iop][spu2][trace]")
{
  for (std::size_t selected = 0; selected < configurationAddresses.size(); ++selected)
  for (auto segment : {0u, 0x80000000u, 0xa0000000u})
  {
    const auto physical = configurationAddresses[selected];
    const auto address = physical | segment;
    CAPTURE(address);
    NekoSystem system;
    programConfiguration(system);
    installConfigurationProgram(system, address);
    system.startTrace();
    system.runMasterCycles(96);
    requireConfigurationResult(system, selected);
    IOPTraceTest::Events actual;
    unsigned delayStores = 0;
    for (const auto &event : system.trace())
    {
      if (event.subsystem != NekoTraceSubsystem::IOP)
      {
        continue;
      }
      if (event.type == NekoTraceEventType::MemoryAccess &&
          event.value0 == address)
      {
        actual.push_back(event);
      }
      if (event.type == NekoTraceEventType::InstructionIssued &&
          event.value0 == 0xa0000010)
      {
        REQUIRE(event.masterCycle == 33);
        REQUIRE(event.value1 == 0xa4220000);
        REQUIRE(event.value3 == NekoIOPTraceInstruction::DELAY_SLOT);
        ++delayStores;
      }
    }
    REQUIRE(delayStores == 1);
    namespace Memory = NekoIOPTraceMemory;
    const auto route = segment == 0xa0000000
      ? Memory::CacheRoute::Uncached : Memory::CacheRoute::Cached;
    const auto store = nekoPackIOPMemoryTraceMetadata({
      physical, 2, Memory::AccessKind::DataStore,
      Memory::Outcome::Completed, route, true,
      static_cast<std::uint8_t>((physical & 2) != 0 ? 0xc : 3)});
    const auto load = nekoPackIOPMemoryTraceMetadata({
      physical, 2, Memory::AccessKind::DataLoad,
      Memory::Outcome::Completed, route, true, 0});
    IOPTraceTest::requireEvents(actual, {
      {33, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, 0x8900, 0, store},
      {41, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, 0x8900, 0, load},
      {65, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, 0x8900, 0, load}
    });
  }
}

TEST_CASE("SPU2 pending stores and loads restore traced and plain canonically",
  "[iop][spu2][save-state][trace]")
{
  for (std::size_t selected : {0u, 3u})
  for (unsigned cut : {32u, 48u, 72u})
  {
    CAPTURE(selected);
    CAPTURE(cut);
    NekoSystem original;
    programConfiguration(original);
    installConfigurationProgram(original,
      configurationAddresses[selected] | 0xa0000000u);
    original.runMasterCycles(cut);
    REQUIRE(original.iopCore().branchContinuation().active == (cut == 32));
    REQUIRE((original.iopCore().delayedResultContinuation().source ==
      IOPDelayedResultSource::Load) == (cut != 32));
    const auto checkpoint = original.saveState();
    NekoSystem restored;
    NekoSystem plain;
    restored.loadState(checkpoint);
    plain.loadState(checkpoint);
    original.startTrace();
    restored.startTrace();
    original.runMasterCycles(96 - cut);
    restored.runMasterCycles(96 - cut);
    plain.runMasterCycles(96 - cut);
    requireConfigurationResult(original, selected);
    requireConfigurationResult(restored, selected);
    requireConfigurationResult(plain, selected);
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireMachine(plain, original);
  }
}

TEST_CASE("SPU2 canonical state hashes every configuration word and restores local wiring",
  "[iop][spu2][save-state]")
{
  NekoSystem source;
  const auto cold = source.saveState();
  auto previousHash = source.iopStateHash();
  for (std::size_t index = 0; index < configurationAddresses.size(); ++index)
  {
    REQUIRE(source.iopBus().write16(configurationAddresses[index],
      configurationValues[index]).status == IOPBusStatus::Completed);
    REQUIRE(source.iopStateHash() != previousHash);
    previousHash = source.iopStateHash();
  }
  const auto programmed = source.saveState();
  const auto fields = inspectSaveState(programmed);
  for (std::size_t index = 0; index < configurationAddresses.size(); ++index)
  {
    const auto *field = fields.find(
      "system.iop.spu2.spdifConfiguration[" + std::to_string(index) + "].value");
    REQUIRE(field != nullptr);
    REQUIRE(field->kind == SaveStateFieldKind::U16);
    REQUIRE(field->size == 2);
    REQUIRE(field->scalarValue == configurationValues[index]);
  }
  NekoSystem destination;
  destination.loadState(programmed);
  REQUIRE(destination.iopStateHash() == previousHash);
  REQUIRE(destination.saveState() == programmed);
  source.spu2Controller().reset();
  for (std::size_t index = 0; index < configurationAddresses.size(); ++index)
  {
    REQUIRE(destination.iopBus().read16(configurationAddresses[index]).value ==
      configurationValues[index]);
    REQUIRE(destination.spu2Controller().readSPDIFConfiguration(index) ==
      configurationValues[index]);
    REQUIRE(destination.iopBus().write16(configurationAddresses[index], 0xffff).status ==
      IOPBusStatus::Completed);
    REQUIRE(source.iopBus().read16(configurationAddresses[index]).value == 0);
  }
  destination.loadState(cold);
  REQUIRE(destination.saveState() == cold);
  SPU2Controller foreign;
  destination.iopBus().attachSPU2(&foreign);
  REQUIRE_THROWS_AS(destination.saveState(), std::logic_error);
  REQUIRE_THROWS_AS(destination.iopStateHash(), std::logic_error);
  destination.loadState(programmed);
  REQUIRE(destination.iopBus().write16(configurationAddresses[0], 0x1234).status ==
    IOPBusStatus::Completed);
  REQUIRE(destination.spu2Controller().readSPDIFConfiguration(0) == 0x1234);
  REQUIRE(foreign.readSPDIFConfiguration(0) == 0);
}

TEST_CASE("SPU2 changed images and late invalid state reject without partial adoption",
  "[iop][spu2][save-state]")
{
  NekoSystem source;
  programConfiguration(source);
  const auto candidate = source.saveState();
  NekoSystem destination;
  REQUIRE(destination.iopBus().write16(configurationAddresses[0], 0xffff).status ==
    IOPBusStatus::Completed);
  const auto before = destination.saveState();
  SaveStateMutation badChecksum(candidate);
  badChecksum.writeScalar("system.iop.spu2.spdifConfiguration[3].value", 0x1234);
  REQUIRE_THROWS_AS(destination.loadState(badChecksum), std::invalid_argument);
  REQUIRE(destination.saveState() == before);
  SaveStateMutation lateInvalid(candidate);
  lateInvalid.writeScalar("system.iop.timing.pixelPhase", 0xffff);
  lateInvalid.refreshChecksum();
  REQUIRE_THROWS_AS(destination.loadState(lateInvalid), std::invalid_argument);
  REQUIRE(destination.saveState() == before);
  SaveStateMutation previousVersion(candidate);
  previousVersion.writeScalar("container.version", 81);
  previousVersion.refreshChecksum();
  REQUIRE_THROWS_AS(destination.loadState(previousVersion), std::invalid_argument);
  REQUIRE(destination.saveState() == before);
  REQUIRE(destination.iopBus().write16(configurationAddresses[1], 0x5678).status ==
    IOPBusStatus::Completed);
  REQUIRE(destination.spu2Controller().readSPDIFConfiguration(1) == 0x5678);
}
