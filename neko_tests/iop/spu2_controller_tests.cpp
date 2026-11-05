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
  constexpr std::array<IOPAddress, 4> masterVolumeAddresses = {{
    0x1f900760, 0x1f900762, 0x1f900788, 0x1f90078a
  }};
  constexpr std::array<std::uint16_t, 4> masterVolumeValues = {{
    0x1234, 0x3fff, 0x4000, 0x7fff
  }};
  constexpr std::array<IOPAddress, 2> unknownInitializationAddresses = {{
    0x1f900b60, 0x1f900b62
  }};
  constexpr std::array<IOPAddress, 2> coreAttributeAddresses = {{
    0x1f90019a, 0x1f90059a
  }};
  struct HalfwordProgramming
  {
    std::array<IOPAddress, 4> addresses;
    std::uint16_t value;
    IOPWord signedLoad;
  };
  constexpr std::array<HalfwordProgramming, 2> halfwordProgramming = {{
    {configurationAddresses, 0x8900, 0xffff8900},
    {masterVolumeAddresses, 0x4900, 0x4900}
  }};

  void programMasterVolumes(NekoSystem &system)
  {
    for (std::size_t index = 0; index < masterVolumeAddresses.size(); ++index)
    {
      REQUIRE(system.iopBus().write16(
        masterVolumeAddresses[index], masterVolumeValues[index]).status ==
        IOPBusStatus::Completed);
    }
  }

  void programConfiguration(NekoSystem &system)
  {
    for (std::size_t index = 0; index < configurationAddresses.size(); ++index)
    {
      REQUIRE(system.iopBus().write16(
        configurationAddresses[index], configurationValues[index]).status ==
        IOPBusStatus::Completed);
    }
  }

  void installConfigurationProgram(
    NekoSystem &system, IOPAddress address, std::uint16_t value)
  {
    const std::array<IOPWord, 13> words = {{
      0x3c010000 | (address >> 16), 0x34210000 | (address & 0xffff),
      0x34020000u | value, 0x10000002, 0xa4220000, 0xffffffff,
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

  void requireConfigurationResult(
    NekoSystem &system, IOPAddress physical, IOPWord signedLoad)
  {
    REQUIRE(system.iopCore().retiredInstructions() == 12);
    REQUIRE(system.iopCore().programCounter() == 0xa0000034);
    REQUIRE(system.iopCore().exception().kind == IOPException::None);
    REQUIRE(system.iopCore().executionState() == IOPExecutionState::Running);
    REQUIRE_FALSE(system.iopCore().branchContinuation().active);
    REQUIRE(system.iopCore().delayedResultContinuation().source ==
      IOPDelayedResultSource::None);
    const auto value = signedLoad & 0xffff;
    REQUIRE(system.iopCore().generalRegister(3) == signedLoad);
    REQUIRE(system.iopCore().generalRegister(4) == value);
    REQUIRE(system.iopCore().generalRegister(5) == 0x1234);
    REQUIRE(system.iopBus().read32(0x100).value == 0);
    REQUIRE(system.iopBus().read32(0x104).value == signedLoad);
    REQUIRE(system.iopBus().read32(0x108).value == 0);
    REQUIRE(system.iopBus().read32(0x10c).value == value);
    for (std::size_t index = 0; index < configurationAddresses.size(); ++index)
    {
      REQUIRE(system.iopBus().read16(configurationAddresses[index]).value ==
        (configurationAddresses[index] == physical ? value :
          configurationValues[index]));
      REQUIRE(system.iopBus().read16(masterVolumeAddresses[index]).value ==
        (masterVolumeAddresses[index] == physical ? value :
          masterVolumeValues[index]));
    }
    REQUIRE(system.iopInterruptController().status() == 0);
  }

  void installInitializationStoreProgram(NekoSystem &system, IOPAddress address,
    std::uint16_t value)
  {
    const std::array<IOPWord, 7> words = {{
      0x3c010000 | (address >> 16), 0x34210000 | (address & 0xffff),
      0x34020000u | value, 0x10000002, 0xa4220000, 0xffffffff, 0x24031234
    }};
    for (std::size_t index = 0; index < words.size(); ++index)
    {
      REQUIRE(system.iopBus().write32(static_cast<IOPAddress>(index * 4),
        words[index]).status == IOPBusStatus::Completed);
    }
    system.iopCore().startExecution(0xa0000000);
  }

  void installCoreAttributeReadProgram(
    NekoSystem &system, IOPAddress address, IOPWord load)
  {
    const std::array<IOPWord, 9> words = {{
      0x3c010000 | (address >> 16), 0x34210000 | (address & 0xffff),
      0x2402ffff, 0x10000002, load, 0xffffffff,
      0x00401821, 0x00402021, 0x24051234
    }};
    for (std::size_t index = 0; index < words.size(); ++index)
    {
      REQUIRE(system.iopBus().write32(static_cast<IOPAddress>(index * 4),
        words[index]).status == IOPBusStatus::Completed);
    }
    system.iopCore().startExecution(0xa0000000);
  }
}

TEST_CASE("SPU2 unprogrammed core attributes admit only zero halfword writes",
  "[iop][spu2][spu2-attr]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  for (const auto address : coreAttributeAddresses)
  {
    CAPTURE(address);
    const auto cold = bus.read16(address);
    REQUIRE(cold.status == IOPBusStatus::Completed);
    REQUIRE(cold.value == 0);
  }
  programConfiguration(system);
  programMasterVolumes(system);
  system.iopInterruptController().assertSource(IOPInterruptSource::SPU);
  system.iopDMAController().writeAuxiliaryControl(0x12345678);
  const auto before = system.iopStateHash();
  for (const auto address : coreAttributeAddresses)
  {
    CAPTURE(address);
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Completed);
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Completed);
    for (std::uint32_t value = 1; value <= 0xffff; ++value)
    {
      if (bus.write16(address, static_cast<std::uint16_t>(value)).status !=
          IOPBusStatus::Unmapped)
      {
        FAIL("Nonzero core ATTR write admitted: " << value);
      }
    }
    REQUIRE(bus.read16(address).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(address).value == 0);
    REQUIRE(bus.read16(address).value == 0);
  }
  REQUIRE(system.iopStateHash() == before);
  REQUIRE(system.iopDMAController().auxiliaryControl() == 0x12345678);
  for (std::size_t index = 0; index < configurationAddresses.size(); ++index)
  {
    REQUIRE(bus.read16(configurationAddresses[index]).value ==
      configurationValues[index]);
    REQUIRE(bus.read16(masterVolumeAddresses[index]).value ==
      masterVolumeValues[index]);
  }
  system.runMasterCycles(80);
  for (const auto address : coreAttributeAddresses)
  {
    REQUIRE(bus.read16(address).value == 0);
  }
  REQUIRE(system.iopInterruptController().status() == (1u << 9));
  bus.reset();
  system.iopCore().reset();
  system.spu2Controller().reset();
  for (const auto address : coreAttributeAddresses)
  {
    REQUIRE(bus.read16(address).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(address).value == 0);
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Completed);
  }
  system.reset();
  for (const auto address : coreAttributeAddresses)
  {
    REQUIRE(bus.read16(address).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(address).value == 0);
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Completed);
  }
}

TEST_CASE("SPU2 core attributes reject neighbors and other transports",
  "[iop][spu2][spu2-attr]")
{
  for (const auto address : coreAttributeAddresses)
  {
    CAPTURE(address);
    NekoSystem system;
    auto &bus = system.iopBus();
    const auto before = system.iopStateHash();
    REQUIRE(bus.read8(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read8(address + 1).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(address + 1, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read16(address + 1).status == IOPBusStatus::Misaligned);
    REQUIRE(bus.write16(address + 1, 0).status == IOPBusStatus::Misaligned);
    REQUIRE(bus.read32(address).status == IOPBusStatus::Misaligned);
    REQUIRE(bus.write32(address, 0).status == IOPBusStatus::Misaligned);
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
    for (IOPAddress other : {address - 2, address + 2, address + 0x800,
        address | 0x80000000u, address | 0xa0000000u})
    {
      CAPTURE(other);
      REQUIRE(bus.read16(other).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write16(other, 0).status == IOPBusStatus::Unmapped);
    }
    std::uint16_t eeValue = 0x4321;
    REQUIRE_FALSE(system.eeBus().readData16(address, &eeValue));
    REQUIRE(eeValue == 0x4321);
    REQUIRE_FALSE(system.eeBus().writeData16(address, 0));
    IOPBus unattached;
    REQUIRE(unattached.read16(address).status == IOPBusStatus::Unmapped);
    REQUIRE(unattached.write16(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(system.iopStateHash() == before);
  }
}

TEST_CASE("SPU2 core attribute guest loads preserve aliases and branch load delays",
  "[iop][spu2][spu2-attr][trace]")
{
  for (const auto physical : coreAttributeAddresses)
  for (auto segment : {0u, 0x80000000u, 0xa0000000u})
  for (IOPWord load : {0x84220000u, 0x94220000u})
  {
    const auto address = physical | segment;
    CAPTURE(address);
    CAPTURE(load);
    NekoSystem system;
    programConfiguration(system);
    programMasterVolumes(system);
    installCoreAttributeReadProgram(system, address, load);
    system.startTrace();
    system.runMasterCycles(64);
    REQUIRE(system.iopCore().executionState() == IOPExecutionState::Running);
    REQUIRE(system.iopCore().exception().kind == IOPException::None);
    REQUIRE(system.iopCore().retiredInstructions() == 8);
    REQUIRE(system.iopCore().programCounter() == 0xa0000024);
    REQUIRE(system.iopCore().generalRegister(2) == 0);
    REQUIRE(system.iopCore().generalRegister(3) == 0xffffffff);
    REQUIRE(system.iopCore().generalRegister(4) == 0);
    REQUIRE(system.iopCore().generalRegister(5) == 0x1234);
    REQUIRE_FALSE(system.iopCore().branchContinuation().active);
    REQUIRE(system.iopCore().delayedResultContinuation().source ==
      IOPDelayedResultSource::None);
    IOPTraceTest::Events actual;
    unsigned delayLoads = 0;
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
        REQUIRE(event.value1 == load);
        REQUIRE(event.value3 == NekoIOPTraceInstruction::DELAY_SLOT);
        ++delayLoads;
      }
    }
    REQUIRE(delayLoads == 1);
    namespace Memory = NekoIOPTraceMemory;
    const auto route = segment == 0xa0000000
      ? Memory::CacheRoute::Uncached : Memory::CacheRoute::Cached;
    const auto metadata = nekoPackIOPMemoryTraceMetadata({
      physical, 2, Memory::AccessKind::DataLoad,
      Memory::Outcome::Completed, route, true, 0});
    IOPTraceTest::requireEvents(actual, {
      {33, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, 0, 0, metadata}
    });
    for (std::size_t index = 0; index < configurationAddresses.size(); ++index)
    {
      REQUIRE(system.iopBus().read16(configurationAddresses[index]).value ==
        configurationValues[index]);
      REQUIRE(system.iopBus().read16(masterVolumeAddresses[index]).value ==
        masterVolumeValues[index]);
    }
    REQUIRE(system.iopInterruptController().status() == 0);
  }
}

TEST_CASE("SPU2 core attribute loads restore before and after the delayed result",
  "[iop][spu2][spu2-attr][save-state][trace]")
{
  for (const auto address : coreAttributeAddresses)
  for (unsigned cut : {32u, 40u, 48u})
  {
    CAPTURE(address);
    CAPTURE(cut);
    NekoSystem original;
    programConfiguration(original);
    programMasterVolumes(original);
    installCoreAttributeReadProgram(original,
      address | 0xa0000000u, 0x94220000);
    original.runMasterCycles(cut);
    REQUIRE(original.iopCore().branchContinuation().active == (cut == 32));
    REQUIRE((original.iopCore().delayedResultContinuation().source ==
      IOPDelayedResultSource::Load) == (cut == 40));
    const auto checkpoint = original.saveState();
    NekoSystem restored;
    NekoSystem plain;
    restored.loadState(checkpoint);
    plain.loadState(checkpoint);
    REQUIRE(plain.iopBus().read16(address).status == IOPBusStatus::Completed);
    REQUIRE(plain.iopBus().read16(address).value == 0);
    REQUIRE(plain.saveState() == checkpoint);
    original.startTrace();
    restored.startTrace();
    original.runMasterCycles(64 - cut);
    restored.runMasterCycles(64 - cut);
    plain.runMasterCycles(64 - cut);
    REQUIRE(original.iopCore().retiredInstructions() == 8);
    REQUIRE(original.iopCore().generalRegister(3) == 0xffffffff);
    REQUIRE(original.iopCore().generalRegister(4) == 0);
    REQUIRE(original.iopCore().generalRegister(5) == 0x1234);
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireMachine(plain, original);
  }
}

TEST_CASE("SPU2 unknown initialization admits only exact zero halfword stores",
  "[iop][spu2][spu2-initialization]")
{
  for (const auto address : unknownInitializationAddresses)
  {
    CAPTURE(address);
    NekoSystem system;
    programConfiguration(system);
    programMasterVolumes(system);
    auto &bus = system.iopBus();
    const auto before = system.iopStateHash();
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Completed);
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Completed);
    for (std::uint32_t value = 1; value <= 0xffff; ++value)
    {
      const auto result = bus.write16(address, static_cast<std::uint16_t>(value));
      if (result.status != IOPBusStatus::Unmapped)
      {
        FAIL("Nonzero initialization admitted: " << value);
      }
    }
    REQUIRE(system.iopStateHash() == before);
    REQUIRE(system.iopInterruptController().status() == 0);
    for (std::size_t index = 0; index < configurationAddresses.size(); ++index)
    {
      REQUIRE(bus.read16(configurationAddresses[index]).value ==
        configurationValues[index]);
      REQUIRE(bus.read16(masterVolumeAddresses[index]).value ==
        masterVolumeValues[index]);
    }
    system.runMasterCycles(80);
    bus.reset();
    system.iopCore().reset();
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Completed);
    system.spu2Controller().reset();
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Completed);
    system.reset();
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Completed);
  }
}

TEST_CASE("SPU2 unknown initialization rejects reads neighbors and other transports",
  "[iop][spu2][spu2-initialization]")
{
  for (const auto address : unknownInitializationAddresses)
  {
    CAPTURE(address);
    NekoSystem system;
    auto &bus = system.iopBus();
    const auto before = system.iopStateHash();
    REQUIRE(bus.read16(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read8(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read8(address + 1).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(address + 1, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read16(address + 1).status == IOPBusStatus::Misaligned);
    REQUIRE(bus.write16(address + 1, 0).status == IOPBusStatus::Misaligned);
    const auto wordStatus = address % 4 == 0
      ? IOPBusStatus::Unmapped : IOPBusStatus::Misaligned;
    REQUIRE(bus.read32(address).status == wordStatus);
    REQUIRE(bus.write32(address, 0).status == wordStatus);
    REQUIRE(bus.readInstruction32(address).status == wordStatus);
    for (std::uint8_t mask = 1; mask <= 0xf; ++mask)
    {
      REQUIRE(bus.writeMasked32(address, 0, mask).status == wordStatus);
    }
    REQUIRE(bus.readDMAC32(address).status == wordStatus);
    REQUIRE(bus.writeDMAC32(address, 0).status == wordStatus);
    REQUIRE(bus.dmaWriteStatus(address) == wordStatus);
    std::array<std::uint8_t, 2> bytes = {{0x12, 0x34}};
    REQUIRE(bus.loadPhysical(address, bytes.data(), bytes.size()) ==
      IOPBusStatus::Unmapped);
    REQUIRE(bus.inspectPhysical(address, bytes.data(), bytes.size()) ==
      IOPBusStatus::Unmapped);
    REQUIRE(bytes == (std::array<std::uint8_t, 2>{{0x12, 0x34}}));
    for (IOPAddress other : {0x1f900b5eu, 0x1f900b64u,
        0x1f900360u, 0x1f900362u, 0x1f900f60u, 0x1f900f62u,
        address | 0x80000000u, address | 0xa0000000u})
    {
      CAPTURE(other);
      REQUIRE(bus.read16(other).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write16(other, 0).status == IOPBusStatus::Unmapped);
    }
    std::uint16_t eeValue = 0x4321;
    REQUIRE_FALSE(system.eeBus().readData16(address, &eeValue));
    REQUIRE(eeValue == 0x4321);
    REQUIRE_FALSE(system.eeBus().writeData16(address, 0));
    IOPBus unattached;
    REQUIRE(unattached.write16(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(system.iopStateHash() == before);
  }
}

TEST_CASE("SPU2 zero initialization guests preserve aliases and delay-slot outcomes",
  "[iop][spu2][spu2-initialization][spu2-attr][trace]")
{
  for (const auto &addresses : {
      unknownInitializationAddresses, coreAttributeAddresses})
  for (const auto physical : addresses)
  for (auto segment : {0u, 0x80000000u, 0xa0000000u})
  for (std::uint16_t value : {0, 1, 0x8000, 0xffff})
  {
    const auto address = physical | segment;
    CAPTURE(address);
    CAPTURE(value);
    NekoSystem system;
    programConfiguration(system);
    programMasterVolumes(system);
    installInitializationStoreProgram(system, address, value);
    system.startTrace();
    system.runMasterCycles(48);
    REQUIRE(system.iopCore().exception().kind == IOPException::None);
    if (value == 0)
    {
      REQUIRE(system.iopCore().executionState() == IOPExecutionState::Running);
      REQUIRE(system.iopCore().retiredInstructions() == 6);
      REQUIRE(system.iopCore().programCounter() == 0xa000001c);
      REQUIRE(system.iopCore().generalRegister(3) == 0x1234);
      REQUIRE_FALSE(system.iopCore().branchContinuation().active);
    }
    else
    {
      REQUIRE(system.iopCore().executionState() == IOPExecutionState::Halted);
      REQUIRE(system.iopCore().stopReason() == IOPStopReason::StoreFailure);
      REQUIRE(system.iopCore().retiredInstructions() == 4);
      REQUIRE(system.iopCore().programCounter() == 0xa0000010);
      REQUIRE(system.iopCore().storeFailure().value == value);
      REQUIRE(system.iopCore().generalRegister(3) == 0);
    }
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
    const auto metadata = nekoPackIOPMemoryTraceMetadata({
      physical, 2, Memory::AccessKind::DataStore,
      value == 0 ? Memory::Outcome::Completed : Memory::Outcome::Unmapped,
      route, true, static_cast<std::uint8_t>(3u << (physical & 3))});
    IOPTraceTest::requireEvents(actual, {
      {33, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, value, 0, metadata}
    });
    if (addresses == coreAttributeAddresses)
    {
      REQUIRE(system.iopBus().read16(physical).status == IOPBusStatus::Completed);
      REQUIRE(system.iopBus().read16(physical).value == 0);
    }
    for (std::size_t index = 0; index < configurationAddresses.size(); ++index)
    {
      REQUIRE(system.iopBus().read16(configurationAddresses[index]).value ==
        configurationValues[index]);
      REQUIRE(system.iopBus().read16(masterVolumeAddresses[index]).value ==
        masterVolumeValues[index]);
    }
    REQUIRE(system.iopInterruptController().status() == 0);
  }
}

TEST_CASE("SPU2 zero initialization resumes pending stores without new state",
  "[iop][spu2][spu2-initialization][spu2-attr][save-state][trace]")
{
  for (const auto &addresses : {
      unknownInitializationAddresses, coreAttributeAddresses})
  for (const auto address : addresses)
  for (unsigned cut : {32u, 40u})
  {
    CAPTURE(address);
    CAPTURE(cut);
    NekoSystem original;
    programConfiguration(original);
    programMasterVolumes(original);
    installInitializationStoreProgram(original,
      address | 0xa0000000u, 0);
    original.runMasterCycles(cut);
    REQUIRE(original.iopCore().branchContinuation().active == (cut == 32));
    const auto checkpoint = original.saveState();
    NekoSystem restored;
    NekoSystem plain;
    restored.loadState(checkpoint);
    plain.loadState(checkpoint);
    const auto before = plain.iopStateHash();
    REQUIRE(plain.iopBus().write16(address, 0).status ==
      IOPBusStatus::Completed);
    REQUIRE(plain.iopStateHash() == before);
    if (addresses == coreAttributeAddresses)
    {
      REQUIRE(plain.iopBus().read16(address).status == IOPBusStatus::Completed);
      REQUIRE(plain.iopBus().read16(address).value == 0);
    }
    REQUIRE(plain.saveState() == checkpoint);
    original.startTrace();
    restored.startTrace();
    original.runMasterCycles(48 - cut);
    restored.runMasterCycles(48 - cut);
    plain.runMasterCycles(48 - cut);
    REQUIRE(original.iopCore().executionState() == IOPExecutionState::Running);
    REQUIRE(original.iopCore().retiredInstructions() == 6);
    REQUIRE(original.iopCore().generalRegister(3) == 0x1234);
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireMachine(plain, original);
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

TEST_CASE("SPU2 fixed master volumes retain controls and reject sweep modes",
  "[iop][spu2]")
{
  NekoSystem system;
  programConfiguration(system);
  for (auto address : masterVolumeAddresses)
  {
    CAPTURE(address);
    REQUIRE(system.iopBus().read16(address).status == IOPBusStatus::Completed);
    REQUIRE(system.iopBus().read16(address).value == 0);
  }
  programMasterVolumes(system);
  for (std::size_t selected = 0; selected < masterVolumeAddresses.size(); ++selected)
  {
    CAPTURE(selected);
    for (auto value : {0, 1, 0x3fff, 0x4000, 0x7fff})
    {
      REQUIRE(system.iopBus().write16(masterVolumeAddresses[selected],
        static_cast<std::uint16_t>(value)).status == IOPBusStatus::Completed);
      for (std::size_t index = 0; index < masterVolumeAddresses.size(); ++index)
      {
        REQUIRE(system.iopBus().read16(masterVolumeAddresses[index]).value ==
          (index == selected ? value : masterVolumeValues[index]));
        REQUIRE(system.iopBus().read16(configurationAddresses[index]).value ==
          configurationValues[index]);
      }
    }
    programMasterVolumes(system);
    const auto before = system.iopStateHash();
    for (auto value : {0x8000, 0x9000, 0xa000, 0xb000,
        0xc000, 0xd000, 0xe000, 0xf000, 0xffff})
    {
      REQUIRE(system.iopBus().write16(masterVolumeAddresses[selected],
        static_cast<std::uint16_t>(value)).status == IOPBusStatus::Unmapped);
      REQUIRE(system.iopBus().read16(masterVolumeAddresses[selected]).value ==
        masterVolumeValues[selected]);
    }
    REQUIRE(system.iopStateHash() == before);
  }
  system.runMasterCycles(80);
  for (std::size_t index = 0; index < masterVolumeAddresses.size(); ++index)
  {
    REQUIRE(system.iopBus().read16(masterVolumeAddresses[index]).value ==
      masterVolumeValues[index]);
  }
  REQUIRE(system.iopInterruptController().status() == 0);
}

TEST_CASE("SPU2 configuration excludes neighbors widths and non-CPU transports",
  "[iop][spu2]")
{
  NekoSystem system;
  programConfiguration(system);
  programMasterVolumes(system);
  auto &bus = system.iopBus();
  const auto before = system.iopStateHash();
  for (IOPAddress address : {0x1f9007c6u, 0x1f9007c8u, 0x1f9007ccu, 0x1f9007cau,
      0x1f900760u, 0x1f900762u, 0x1f900788u, 0x1f90078au})
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
      0x1f9007ceu, 0x1f9007d0u, 0x1f9003c6u, 0x1f90075eu,
      0x1f900764u, 0x1f900766u, 0x1f900768u, 0x1f90076au,
      0x1f90076cu, 0x1f90076eu, 0x1f900770u, 0x1f900772u,
      0x1f900774u, 0x1f900786u, 0x1f90078cu, 0x1f90078eu,
      0x1f900790u, 0x1f900792u, 0x1f900794u, 0x1f900796u,
      0x1f900798u, 0x1f90079au, 0x1f90079cu})
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
  programMasterVolumes(system);
  system.runMasterCycles(80);
  system.iopBus().reset();
  system.iopCore().reset();
  for (std::size_t index = 0; index < configurationAddresses.size(); ++index)
  {
    REQUIRE(system.iopBus().read16(configurationAddresses[index]).value ==
      configurationValues[index]);
    REQUIRE(system.iopBus().read16(masterVolumeAddresses[index]).value ==
      masterVolumeValues[index]);
  }
  system.spu2Controller().reset();
  for (auto address : configurationAddresses)
  {
    REQUIRE(system.iopBus().read16(address).value == 0);
  }
  for (auto address : masterVolumeAddresses)
  {
    REQUIRE(system.iopBus().read16(address).value == 0);
  }
  programConfiguration(system);
  programMasterVolumes(system);
  system.reset();
  for (auto address : configurationAddresses)
  {
    REQUIRE(system.iopBus().read16(address).value == 0);
    REQUIRE(system.iopBus().write16(address, 0xffff).status ==
      IOPBusStatus::Completed);
  }
  for (auto address : masterVolumeAddresses)
  {
    REQUIRE(system.iopBus().read16(address).value == 0);
    REQUIRE(system.iopBus().write16(address, 0x7fff).status ==
      IOPBusStatus::Completed);
  }
}

TEST_CASE("SPU2 sweep guest stores stop before changing fixed controls",
  "[iop][spu2][execution]")
{
  for (auto physical : masterVolumeAddresses)
  {
    CAPTURE(physical);
    NekoSystem system;
    programConfiguration(system);
    programMasterVolumes(system);
    installConfigurationProgram(system, physical | 0xa0000000u, 0x8000);
    system.runMasterCycles(40);
    REQUIRE(system.iopCore().executionState() == IOPExecutionState::Halted);
    REQUIRE(system.iopCore().stopReason() == IOPStopReason::StoreFailure);
    REQUIRE(system.iopCore().programCounter() == 0xa0000010);
    REQUIRE(system.iopCore().retiredInstructions() == 4);
    REQUIRE(system.iopCore().exception().kind == IOPException::None);
    REQUIRE(system.iopCore().storeFailure().value == 0x8000);
    for (std::size_t index = 0; index < masterVolumeAddresses.size(); ++index)
    {
      REQUIRE(system.iopBus().read16(masterVolumeAddresses[index]).value ==
        masterVolumeValues[index]);
      REQUIRE(system.iopBus().read16(configurationAddresses[index]).value ==
        configurationValues[index]);
    }
    REQUIRE(system.iopInterruptController().status() == 0);
  }
}

TEST_CASE("SPU2 halfword guests preserve aliases delay slots and signed load delays",
  "[iop][spu2][trace]")
{
  for (const auto &programming : halfwordProgramming)
  for (std::size_t selected = 0; selected < programming.addresses.size(); ++selected)
  for (auto segment : {0u, 0x80000000u, 0xa0000000u})
  {
    const auto physical = programming.addresses[selected];
    const auto address = physical | segment;
    CAPTURE(address);
    NekoSystem system;
    programConfiguration(system);
    programMasterVolumes(system);
    installConfigurationProgram(system, address, programming.value);
    system.startTrace();
    system.runMasterCycles(96);
    requireConfigurationResult(system, physical, programming.signedLoad);
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
        address, programming.value, 0, store},
      {41, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, programming.value, 0, load},
      {65, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, programming.value, 0, load}
    });
  }
}

TEST_CASE("SPU2 pending stores and loads restore traced and plain canonically",
  "[iop][spu2][save-state][trace]")
{
  for (const auto &programming : halfwordProgramming)
  for (std::size_t selected : {0u, 3u})
  for (unsigned cut : {32u, 48u, 72u})
  {
    CAPTURE(selected);
    CAPTURE(cut);
    NekoSystem original;
    programConfiguration(original);
    programMasterVolumes(original);
    const auto physical = programming.addresses[selected];
    CAPTURE(physical);
    installConfigurationProgram(original,
      physical | 0xa0000000u, programming.value);
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
    requireConfigurationResult(original, physical, programming.signedLoad);
    requireConfigurationResult(restored, physical, programming.signedLoad);
    requireConfigurationResult(plain, physical, programming.signedLoad);
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
  for (std::size_t index = 0; index < masterVolumeAddresses.size(); ++index)
  {
    REQUIRE(source.iopBus().write16(masterVolumeAddresses[index],
      masterVolumeValues[index]).status == IOPBusStatus::Completed);
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
    const auto *master = fields.find(
      "system.iop.spu2.masterVolumeControls[" + std::to_string(index) + "].value");
    REQUIRE(master != nullptr);
    REQUIRE(master->kind == SaveStateFieldKind::U16);
    REQUIRE(master->size == 2);
    REQUIRE(master->scalarValue == masterVolumeValues[index]);
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
    REQUIRE(destination.iopBus().read16(masterVolumeAddresses[index]).value ==
      masterVolumeValues[index]);
    REQUIRE(destination.spu2Controller().readMasterVolumeControl(index) ==
      masterVolumeValues[index]);
    REQUIRE(destination.iopBus().write16(masterVolumeAddresses[index], 0x7fff).status ==
      IOPBusStatus::Completed);
    REQUIRE(source.iopBus().read16(masterVolumeAddresses[index]).value == 0);
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
  REQUIRE(destination.iopBus().write16(masterVolumeAddresses[3], 0x4321).status ==
    IOPBusStatus::Completed);
  REQUIRE(destination.spu2Controller().readMasterVolumeControl(3) == 0x4321);
  REQUIRE(foreign.readMasterVolumeControl(3) == 0);
}

TEST_CASE("SPU2 changed images and late invalid state reject without partial adoption",
  "[iop][spu2][save-state]")
{
  NekoSystem source;
  programConfiguration(source);
  programMasterVolumes(source);
  const auto candidate = source.saveState();
  NekoSystem destination;
  REQUIRE(destination.iopBus().write16(configurationAddresses[0], 0xffff).status ==
    IOPBusStatus::Completed);
  REQUIRE(destination.iopBus().write16(masterVolumeAddresses[0], 0x4567).status ==
    IOPBusStatus::Completed);
  const auto before = destination.saveState();
  SaveStateMutation badChecksum(candidate);
  badChecksum.writeScalar("system.iop.spu2.spdifConfiguration[3].value", 0x1234);
  REQUIRE_THROWS_AS(destination.loadState(badChecksum), std::invalid_argument);
  REQUIRE(destination.saveState() == before);
  SaveStateMutation badMasterChecksum(candidate);
  badMasterChecksum.writeScalar("system.iop.spu2.masterVolumeControls[3].value", 0x1234);
  REQUIRE_THROWS_AS(destination.loadState(badMasterChecksum), std::invalid_argument);
  REQUIRE(destination.saveState() == before);
  for (std::size_t index = 0; index < masterVolumeAddresses.size(); ++index)
  {
    CAPTURE(index);
    SaveStateMutation unsupportedSweep(candidate);
    unsupportedSweep.writeScalar(
      "system.iop.spu2.masterVolumeControls[" + std::to_string(index) + "].value",
      0x8000 | masterVolumeValues[index]);
    unsupportedSweep.refreshChecksum();
    REQUIRE_THROWS_AS(destination.loadState(unsupportedSweep), std::invalid_argument);
    REQUIRE(destination.saveState() == before);
  }
  SaveStateMutation lateInvalid(candidate);
  lateInvalid.writeScalar("system.iop.timing.pixelPhase", 0xffff);
  lateInvalid.refreshChecksum();
  REQUIRE_THROWS_AS(destination.loadState(lateInvalid), std::invalid_argument);
  REQUIRE(destination.saveState() == before);
  SaveStateMutation previousVersion(candidate);
  previousVersion.writeScalar("container.version", 82);
  previousVersion.refreshChecksum();
  REQUIRE_THROWS_AS(destination.loadState(previousVersion), std::invalid_argument);
  REQUIRE(destination.saveState() == before);
  REQUIRE(destination.iopBus().write16(configurationAddresses[1], 0x5678).status ==
    IOPBusStatus::Completed);
  REQUIRE(destination.spu2Controller().readSPDIFConfiguration(1) == 0x5678);
}
