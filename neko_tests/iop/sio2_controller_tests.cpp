#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "catch.hpp"
#include "iop_trace_test_utils.hpp"
#include "neko_system.hpp"

namespace
{
  constexpr IOPAddress controlAddress = 0x1f808268;
  constexpr IOPWord startupControl = 0x3bc;

  template<std::size_t Size>
  void installProgram(NekoSystem &system, const std::array<IOPWord, Size> &words)
  {
    for (std::size_t index = 0; index < words.size(); ++index)
    {
      REQUIRE(system.iopBus().write32(static_cast<IOPAddress>(index * 4),
        words[index]).status == IOPBusStatus::Completed);
    }
    system.iopCore().startExecution(0xa0000000);
  }

  std::array<IOPWord, 15> startupProgram(IOPAddress address)
  {
    return {{
      0x3c010000 | (address >> 16),
      0x34210000 | (address & 0xffff),
      0x240203bc, // addiu r2, r0, startup control
      0x24031234,
      0x8c230000, // lw r3, CTRL (cold)
      0xac030100, // preceding r3 in the load delay slot
      0xac030104,
      0xac220000, // sw r2, CTRL
      0x8c230000, // lw r3, CTRL (programmed)
      0xac030108, // preceding r3 in the load delay slot
      0x10620002, // beq r3, r2, +2
      0xac220000, // repeat initialization in the taken branch delay slot
      0xffffffff, // skipped
      0xac03010c,
      0x24640001
    }};
  }

  IOPTraceTest::Events controlEvents(const NekoSystem &system, IOPAddress address)
  {
    IOPTraceTest::Events events;
    for (const auto &event : system.trace())
    {
      if (event.subsystem == NekoTraceSubsystem::IOP &&
          event.type == NekoTraceEventType::MemoryAccess &&
          event.value0 == address)
      {
        events.push_back(event);
      }
    }
    return events;
  }

  IOPTraceTest::Events expectedStartupEvents(IOPAddress address)
  {
    namespace Memory = NekoIOPTraceMemory;
    const auto route = address < 0xa0000000
      ? Memory::CacheRoute::Cached : Memory::CacheRoute::Uncached;
    const auto load = nekoPackIOPMemoryTraceMetadata({
      controlAddress, 4, Memory::AccessKind::DataLoad,
      Memory::Outcome::Completed, route, true, 0});
    const auto store = nekoPackIOPMemoryTraceMetadata({
      controlAddress, 4, Memory::AccessKind::DataStore,
      Memory::Outcome::Completed, route, true, 0xf});
    return {
      {33, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess, address, 0, 0, load},
      {57, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, startupControl, 0, store},
      {65, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, startupControl, 0, load},
      {89, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, startupControl, 0, store}
    };
  }

  void requireStartupResult(const NekoSystem &system)
  {
    REQUIRE(system.iopCore().retiredInstructions() == 14);
    REQUIRE(system.iopCore().programCounter() == 0xa000003c);
    REQUIRE(system.iopCore().executionState() == IOPExecutionState::Running);
    REQUIRE(system.iopCore().exception().kind == IOPException::None);
    REQUIRE(system.iopCore().generalRegister(3) == startupControl);
    REQUIRE(system.iopCore().generalRegister(4) == startupControl + 1);
    REQUIRE_FALSE(system.iopCore().branchContinuation().active);
    REQUIRE(system.iopCore().delayedResultContinuation().source ==
      IOPDelayedResultSource::None);
    REQUIRE(system.sio2Controller().readControl() == startupControl);
    REQUIRE(system.iopBus().readDMAC32(0x100).value == 0x1234);
    REQUIRE(system.iopBus().readDMAC32(0x104).value == 0);
    REQUIRE(system.iopBus().readDMAC32(0x108).value == 0);
    REQUIRE(system.iopBus().readDMAC32(0x10c).value == startupControl);
  }
}

TEST_CASE("SIO2 retains only the selected startup control word", "[iop][sio2]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  REQUIRE(bus.read32(controlAddress).status == IOPBusStatus::Completed);
  REQUIRE(bus.read32(controlAddress).value == 0);
  for (unsigned repeat = 0; repeat < 2; ++repeat)
  {
    REQUIRE(bus.write32(controlAddress, startupControl).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.read32(controlAddress).value == startupControl);
  }
  for (IOPWord value : {0u, 0xcu, 1u, 0x3bdu, 0xffffffffu})
  {
    CAPTURE(value);
    REQUIRE(bus.write32(controlAddress, value).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read32(controlAddress).value == startupControl);
  }
}

TEST_CASE("SIO2 bus admission excludes partial and nondata routes", "[iop][sio2]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  REQUIRE(bus.writeMasked32(controlAddress, startupControl, 0xf).status ==
    IOPBusStatus::Completed);
  REQUIRE(bus.writeMasked32(controlAddress, 0, 0xf).status ==
    IOPBusStatus::Unmapped);
  for (std::uint8_t mask = 1; mask < 0xf; ++mask)
  {
    CAPTURE(mask);
    REQUIRE(bus.writeMasked32(controlAddress, startupControl, mask).status ==
      IOPBusStatus::Unmapped);
  }
  for (IOPAddress address = 0x1f808200; address < 0x1f808300; address += 4)
  {
    CAPTURE(address);
    REQUIRE(bus.read8(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read16(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(address, 0xbc).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write16(address, startupControl).status == IOPBusStatus::Unmapped);
    if (address != controlAddress)
    {
      REQUIRE(bus.read32(address).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write32(address, startupControl).status == IOPBusStatus::Unmapped);
    }
  }
  for (IOPAddress offset : {1u, 2u, 3u})
  {
    CAPTURE(offset);
    REQUIRE(bus.read8(controlAddress + offset).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(controlAddress + offset, 0xbc).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read32(controlAddress + offset).status == IOPBusStatus::Misaligned);
    REQUIRE(bus.write32(controlAddress + offset, startupControl).status ==
      IOPBusStatus::Misaligned);
    REQUIRE(bus.readInstruction32(controlAddress + offset).status ==
      IOPBusStatus::Misaligned);
  }
  REQUIRE(bus.read16(controlAddress + 2).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.write16(controlAddress + 2, startupControl).status ==
    IOPBusStatus::Unmapped);
  REQUIRE(bus.readInstruction32(controlAddress).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.readDMAC32(controlAddress).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.dmaWriteStatus(controlAddress) == IOPBusStatus::Unmapped);
  REQUIRE(bus.writeDMAC32(controlAddress, startupControl).status ==
    IOPBusStatus::Unmapped);
  std::uint8_t byte = 0x5a;
  REQUIRE(bus.loadPhysical(controlAddress, &byte, 1) == IOPBusStatus::Unmapped);
  REQUIRE(bus.inspectPhysical(controlAddress, &byte, 1) == IOPBusStatus::Unmapped);
  REQUIRE(byte == 0x5a);
  for (IOPAddress alias : {0x9f808268u, 0xbf808268u})
  {
    REQUIRE(bus.read32(alias).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(alias, startupControl).status == IOPBusStatus::Unmapped);
  }
  REQUIRE_THROWS_AS(system.eeBus().read32(controlAddress), std::out_of_range);
  REQUIRE(bus.read32(controlAddress).value == startupControl);

  IOPBus unattached;
  REQUIRE(unattached.read32(controlAddress).status == IOPBusStatus::Unmapped);
  REQUIRE(unattached.write32(controlAddress, startupControl).status ==
    IOPBusStatus::Unmapped);
}

TEST_CASE("SIO2 control has an explicit device and machine reset lifetime",
  "[iop][sio2]")
{
  SIO2Controller controller;
  REQUIRE(controller.readControl() == 0);
  REQUIRE(controller.writeStartupControl(0x3bc) == SIO2AccessStatus::Accepted);
  REQUIRE(controller.writeStartupControl(0) == SIO2AccessStatus::Unsupported);
  REQUIRE(controller.readControl() == 0x3bc);
  controller.reset();
  REQUIRE(controller.readControl() == 0);

  NekoSystem system;
  REQUIRE(system.iopBus().write32(controlAddress, startupControl).status ==
    IOPBusStatus::Completed);
  system.iopBus().reset();
  REQUIRE(system.iopBus().read32(controlAddress).value == startupControl);
  system.iopCore().reset();
  REQUIRE(system.iopBus().read32(controlAddress).value == startupControl);
  system.runMasterCycles(80);
  REQUIRE(system.iopCore().elapsedCycles() == 0);
  REQUIRE(system.iopBus().read32(controlAddress).value == startupControl);
  system.sio2Controller().reset();
  REQUIRE(system.iopBus().read32(controlAddress).value == 0);
  REQUIRE(system.iopBus().write32(controlAddress, startupControl).status ==
    IOPBusStatus::Completed);
  system.reset();
  REQUIRE(system.iopBus().read32(controlAddress).status == IOPBusStatus::Completed);
  REQUIRE(system.iopBus().read32(controlAddress).value == 0);
  REQUIRE(system.iopBus().write32(controlAddress, startupControl).status ==
    IOPBusStatus::Completed);
  REQUIRE(system.sio2Controller().readControl() == startupControl);
}

TEST_CASE("SIO2 programming preserves existing IRQ DMA and frontend input",
  "[iop][sio2]")
{
  NekoSystem system;
  auto &interrupts = system.iopInterruptController();
  auto &dma = system.iopDMAController();
  NekoInputState input;
  input.buttons = NekoButton::CROSS | NekoButton::START;
  input.leftStickX = 0x23;
  input.leftStickY = 0x45;
  input.rightStickX = 0x67;
  input.rightStickY = 0x89;
  system.setInput(input);
  interrupts.writeMask(1u << 17);
  interrupts.writeControl(1);
  dma.writePriority(IOPDMABank::Primary, 0x12345678);
  dma.writePriority(IOPDMABank::Secondary, 0x76543210);
  dma.writeAuxiliaryControl(0xabcdef01);
  dma.signalChannelCompletion(IOPDMAChannel::SIF1);
  const auto primary = dma.interruptStatus(IOPDMABank::Primary);
  const auto secondary = dma.interruptStatus(IOPDMABank::Secondary);
  for (bool pending : {false, true})
  {
    CAPTURE(pending);
    if (pending)
    {
      interrupts.assertSource(IOPInterruptSource::SIO2);
    }
    const auto status = interrupts.status();
    for (IOPWord value : {startupControl, 0u, 1u, 0xcu, 0x3bdu, startupControl})
    {
      CAPTURE(value);
      REQUIRE(system.iopBus().write32(controlAddress, value).status ==
        (value == startupControl ? IOPBusStatus::Completed : IOPBusStatus::Unmapped));
      REQUIRE(system.iopBus().read32(controlAddress).value == startupControl);
      REQUIRE(interrupts.status() == status);
      REQUIRE(interrupts.mask() == (1u << 17));
      REQUIRE(interrupts.control() == 1);
      REQUIRE(interrupts.interruptPending() == pending);
      REQUIRE(dma.priority(IOPDMABank::Primary) == 0x12345678);
      REQUIRE(dma.priority(IOPDMABank::Secondary) == 0x76543210);
      REQUIRE(dma.auxiliaryControl() == 0xabcdef01);
      REQUIRE(dma.interruptStatus(IOPDMABank::Primary) == primary);
      REQUIRE(dma.interruptStatus(IOPDMABank::Secondary) == secondary);
      REQUIRE(system.input().buttons == input.buttons);
      REQUIRE(system.input().leftStickX == input.leftStickX);
      REQUIRE(system.input().leftStickY == input.leftStickY);
      REQUIRE(system.input().rightStickX == input.rightStickX);
      REQUIRE(system.input().rightStickY == input.rightStickY);
    }
  }
}

TEST_CASE("SIO2 guest aliases preserve delayed operands and exact control traces",
  "[iop][sio2][sio2-conformance][trace]")
{
  for (IOPAddress address : {controlAddress, 0x9f808268u, 0xbf808268u})
  {
    CAPTURE(address);
    NekoSystem system;
    installProgram(system, startupProgram(address));
    system.startTrace();
    system.runMasterCycles(112);
    requireStartupResult(system);
    IOPTraceTest::requireEvents(controlEvents(system, address),
      expectedStartupEvents(address));
    unsigned branchDelayStores = 0;
    for (const auto &event : system.trace())
    {
      if (event.subsystem == NekoTraceSubsystem::IOP &&
          event.type == NekoTraceEventType::InstructionIssued &&
          event.value0 == 0xa000002c)
      {
        REQUIRE(event.masterCycle == 89);
        REQUIRE(event.value1 == 0xac220000);
        REQUIRE(event.value3 == NekoIOPTraceInstruction::DELAY_SLOT);
        ++branchDelayStores;
      }
    }
    REQUIRE(branchDelayStores == 1);
  }
}

TEST_CASE("SIO2 guest store failure retains the preceding readback load",
  "[iop][sio2][sio2-conformance][trace]")
{
  namespace Memory = NekoIOPTraceMemory;
  for (IOPAddress address : {controlAddress, 0x9f808268u, 0xbf808268u})
  {
    CAPTURE(address);
    NekoSystem system;
    REQUIRE(system.iopBus().write32(controlAddress, startupControl).status ==
      IOPBusStatus::Completed);
    installProgram(system, std::array<IOPWord, 4>{{
      0x3c010000 | (address >> 16),
      0x34210000 | (address & 0xffff),
      0x8c220000,
      0xac220000
    }});
    system.startTrace();
    system.runMasterCycles(32);
    const auto &core = system.iopCore();
    REQUIRE(core.retiredInstructions() == 3);
    REQUIRE(core.programCounter() == 0xa000000c);
    REQUIRE(core.stopReason() == IOPStopReason::StoreFailure);
    REQUIRE(core.exception().kind == IOPException::None);
    REQUIRE(core.generalRegister(2) == 0);
    REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::Load);
    REQUIRE(core.delayedResultContinuation().destination == 2);
    REQUIRE(core.delayedResultContinuation().value == startupControl);
    REQUIRE(core.storeFailure().width == IOPStoreWidth::Word);
    REQUIRE(core.storeFailure().value == 0);
    REQUIRE(core.storeFailure().mergeByteEnable == 0);
    REQUIRE(core.storeFailure().access.virtualAddress == address);
    REQUIRE(core.storeFailure().access.physicalAddress == controlAddress);
    REQUIRE(core.storeFailure().access.outcome == IOPAccessOutcome::Unmapped);
    REQUIRE(system.sio2Controller().readControl() == startupControl);
    const auto route = address < 0xa0000000
      ? Memory::CacheRoute::Cached : Memory::CacheRoute::Uncached;
    IOPTraceTest::requireEvents(controlEvents(system, address), {
      {17, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, startupControl, 0, nekoPackIOPMemoryTraceMetadata({
          controlAddress, 4, Memory::AccessKind::DataLoad,
          Memory::Outcome::Completed, route, true, 0})},
      {25, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, 0, 0, nekoPackIOPMemoryTraceMetadata({
          controlAddress, 4, Memory::AccessKind::DataStore,
          Memory::Outcome::Unmapped, route, true, 0xf})}
    });
  }
}

TEST_CASE("SIO2 initialization and readback restore with traced and plain continuations",
  "[iop][sio2][sio2-conformance][save-state][trace]")
{
  constexpr IOPAddress address = 0xbf808268;
  for (unsigned cut : {56u, 64u, 72u, 88u})
  {
    CAPTURE(cut);
    NekoSystem original;
    installProgram(original, startupProgram(address));
    original.runMasterCycles(cut);
    const auto &core = original.iopCore();
    REQUIRE(core.retiredInstructions() == cut / 8);
    REQUIRE(original.sio2Controller().readControl() == (cut == 56 ? 0 : startupControl));
    REQUIRE(core.programCounter() == 0xa0000000 + cut / 2);
    REQUIRE(core.generalRegister(3) == (cut < 88 ? 0 : startupControl));
    REQUIRE(core.branchContinuation().active == (cut == 88));
    REQUIRE(core.delayedResultContinuation().source == (cut == 72
      ? IOPDelayedResultSource::Load : IOPDelayedResultSource::None));
    if (cut == 72)
    {
      REQUIRE(core.delayedResultContinuation().destination == 3);
      REQUIRE(core.delayedResultContinuation().value == startupControl);
    }
    if (cut == 88)
    {
      REQUIRE(core.branchContinuation().instructionAddress == 0xa0000028);
      REQUIRE(core.branchContinuation().targetAddress == 0xa0000034);
    }
    const auto checkpoint = original.saveState();
    NekoSystem restored;
    NekoSystem plain;
    if (cut == 56)
    {
      REQUIRE(restored.sio2Controller().writeStartupControl(startupControl) ==
        SIO2AccessStatus::Accepted);
    }
    restored.loadState(checkpoint);
    plain.loadState(checkpoint);
    REQUIRE(restored.saveState() == checkpoint);
    REQUIRE(plain.saveState() == checkpoint);
    original.startTrace();
    restored.startTrace();
    original.runMasterCycles(112 - cut);
    restored.runMasterCycles(112 - cut);
    plain.runMasterCycles(112 - cut);
    requireStartupResult(original);
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    REQUIRE_FALSE(original.trace().empty());
    REQUIRE(plain.trace().empty());
    REQUIRE_FALSE(plain.traceEnabled());
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireMachine(plain, original);
    auto expected = expectedStartupEvents(address);
    IOPTraceTest::Events suffix;
    for (const auto &event : expected)
    {
      if (event.masterCycle > cut)
      {
        suffix.push_back(event);
      }
    }
    IOPTraceTest::requireEvents(controlEvents(original, address), suffix);
  }
}
