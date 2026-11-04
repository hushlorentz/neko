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
  constexpr IOPWord passiveControl = 0xc;
  constexpr IOPAddress portControlBase = 0x1f808240;
  constexpr std::array<IOPWord, 8> portValues = {{
    0x89abcdef, 0x01060014, 0xffffffff, 0, 0x80000000, 1, 0x12345678, 0xa5a55a5a
  }};

  struct ControlTransition
  {
    IOPWord initial;
    IOPWord programmed;
  };

  constexpr ControlTransition controlTransitions[] = {
    {0, startupControl},
    {0, passiveControl},
    {startupControl, passiveControl},
    {passiveControl, startupControl}
  };

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

  std::array<IOPWord, 15> controlProgram(IOPAddress address, IOPWord programmed)
  {
    return {{
      0x3c010000 | (address >> 16),
      0x34210000 | (address & 0xffff),
      0x24020000 | programmed, // addiu r2, r0, selected control
      0x24031234,
      0x8c230000, // lw r3, CTRL (old value)
      0xac030100, // preceding r3 in the load delay slot
      0xac030104,
      0xac220000, // sw r2, CTRL
      0x8c230000, // lw r3, CTRL (programmed)
      0xac030108, // preceding r3 in the load delay slot
      0x10620002, // beq r3, r2, +2
      0xac220000, // repeat programming in the taken branch delay slot
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

  IOPTraceTest::Events expectedControlEvents(
    IOPAddress address, ControlTransition transition)
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
      {33, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, transition.initial, 0, load},
      {57, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, transition.programmed, 0, store},
      {65, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, transition.programmed, 0, load},
      {89, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, transition.programmed, 0, store}
    };
  }

  void requireControlResult(const NekoSystem &system, ControlTransition transition)
  {
    REQUIRE(system.iopCore().retiredInstructions() == 14);
    REQUIRE(system.iopCore().programCounter() == 0xa000003c);
    REQUIRE(system.iopCore().executionState() == IOPExecutionState::Running);
    REQUIRE(system.iopCore().exception().kind == IOPException::None);
    REQUIRE(system.iopCore().generalRegister(3) == transition.programmed);
    REQUIRE(system.iopCore().generalRegister(4) == transition.programmed + 1);
    REQUIRE_FALSE(system.iopCore().branchContinuation().active);
    REQUIRE(system.iopCore().delayedResultContinuation().source ==
      IOPDelayedResultSource::None);
    REQUIRE(system.sio2Controller().readControl() == transition.programmed);
    REQUIRE(system.iopBus().readDMAC32(0x100).value == 0x1234);
    REQUIRE(system.iopBus().readDMAC32(0x104).value == transition.initial);
    REQUIRE(system.iopBus().readDMAC32(0x108).value == transition.initial);
    REQUIRE(system.iopBus().readDMAC32(0x10c).value == transition.programmed);
  }

  void installPortProgram(NekoSystem &system, IOPAddress address)
  {
    for (std::size_t index = 0; index < portValues.size(); ++index)
    {
      REQUIRE(system.iopBus().write32(portControlBase +
        static_cast<IOPAddress>(index * 4), portValues[index]).status ==
        IOPBusStatus::Completed);
    }
    installProgram(system, std::array<IOPWord, 16>{{
      0x3c010000 | (address >> 16),
      0x34210000 | (address & 0xffff),
      0x3c02c0c0,
      0x3442050f,
      0x24031234,
      0x8c230000,
      0xac030100, // preceding r3 in the load delay slot
      0xac030104,
      0xac220000,
      0x8c230000,
      0xac030108, // preceding r3 in the load delay slot
      0x10620002,
      0xac220000, // repeat programming in the taken branch delay slot
      0xffffffff,
      0xac03010c,
      0x24640001
    }});
  }

  IOPTraceTest::Events expectedPortEvents(IOPAddress address, std::size_t index)
  {
    namespace Memory = NekoIOPTraceMemory;
    const auto physical = portControlBase + static_cast<IOPAddress>(index * 4);
    const auto route = address < 0xa0000000
      ? Memory::CacheRoute::Cached : Memory::CacheRoute::Uncached;
    const auto load = nekoPackIOPMemoryTraceMetadata({
      physical, 4, Memory::AccessKind::DataLoad,
      Memory::Outcome::Completed, route, true, 0});
    const auto store = nekoPackIOPMemoryTraceMetadata({
      physical, 4, Memory::AccessKind::DataStore,
      Memory::Outcome::Completed, route, true, 0xf});
    return {
      {41, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, portValues[index], 0, load},
      {65, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, 0xc0c0050f, 0, store},
      {73, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, 0xc0c0050f, 0, load},
      {97, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, 0xc0c0050f, 0, store}
    };
  }

  void requirePortResult(const NekoSystem &system, std::size_t selected)
  {
    REQUIRE(system.iopCore().retiredInstructions() == 15);
    REQUIRE(system.iopCore().programCounter() == 0xa0000040);
    REQUIRE(system.iopCore().executionState() == IOPExecutionState::Running);
    REQUIRE(system.iopCore().exception().kind == IOPException::None);
    REQUIRE(system.iopCore().generalRegister(3) == 0xc0c0050f);
    REQUIRE(system.iopCore().generalRegister(4) == 0xc0c00510);
    REQUIRE_FALSE(system.iopCore().branchContinuation().active);
    REQUIRE(system.iopCore().delayedResultContinuation().source ==
      IOPDelayedResultSource::None);
    REQUIRE(system.sio2Controller().readControl() == 0);
    REQUIRE(system.iopBus().readDMAC32(0x100).value == 0x1234);
    for (std::size_t index = 0; index < portValues.size(); ++index)
    {
      REQUIRE(system.sio2Controller().readPortControl(index) ==
        (index == selected ? 0xc0c0050f : portValues[index]));
    }
    REQUIRE(system.iopBus().readDMAC32(0x104).value == portValues[selected]);
    REQUIRE(system.iopBus().readDMAC32(0x108).value == portValues[selected]);
    REQUIRE(system.iopBus().readDMAC32(0x10c).value == 0xc0c0050f);
  }
}

TEST_CASE("SIO2 retains only the selected passive control words", "[iop][sio2]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  REQUIRE(bus.read32(controlAddress).status == IOPBusStatus::Completed);
  REQUIRE(bus.read32(controlAddress).value == 0);
  for (IOPWord programmed : {passiveControl, passiveControl, startupControl,
         startupControl, passiveControl, startupControl})
  {
    CAPTURE(programmed);
    REQUIRE(bus.write32(controlAddress, programmed).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.read32(controlAddress).value == programmed);
    for (IOPWord value : {0u, 1u, 0xdu, 0x3bdu, 0x8000000cu, 0xffffffffu})
    {
      CAPTURE(value);
      REQUIRE(bus.write32(controlAddress, value).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.read32(controlAddress).value == programmed);
    }
  }
}

TEST_CASE("SIO2 bus admission excludes partial and nondata routes", "[iop][sio2]")
{
  IOPWord programmedControl = startupControl;
  SECTION("Startup programming")
  {
  }
  SECTION("Follow-on programming")
  {
    programmedControl = passiveControl;
  }
  NekoSystem system;
  auto &bus = system.iopBus();
  REQUIRE(bus.writeMasked32(controlAddress, programmedControl, 0xf).status ==
    IOPBusStatus::Completed);
  REQUIRE(bus.writeMasked32(controlAddress, 0, 0xf).status ==
    IOPBusStatus::Unmapped);
  for (std::uint8_t mask = 1; mask < 0xf; ++mask)
  {
    CAPTURE(mask);
    REQUIRE(bus.writeMasked32(controlAddress, programmedControl, mask).status ==
      IOPBusStatus::Unmapped);
  }
  for (IOPAddress address = 0x1f808200; address < 0x1f808300; address += 4)
  {
    CAPTURE(address);
    REQUIRE(bus.read8(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read16(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(address, 0xbc).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write16(address, programmedControl).status == IOPBusStatus::Unmapped);
    if (address != controlAddress &&
        (address < portControlBase || address >= portControlBase + 32))
    {
      REQUIRE(bus.read32(address).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write32(address, programmedControl).status == IOPBusStatus::Unmapped);
    }
  }
  for (IOPAddress offset : {1u, 2u, 3u})
  {
    CAPTURE(offset);
    REQUIRE(bus.read8(controlAddress + offset).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(controlAddress + offset, 0xbc).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read32(controlAddress + offset).status == IOPBusStatus::Misaligned);
    REQUIRE(bus.write32(controlAddress + offset, programmedControl).status ==
      IOPBusStatus::Misaligned);
    REQUIRE(bus.readInstruction32(controlAddress + offset).status ==
      IOPBusStatus::Misaligned);
  }
  REQUIRE(bus.read16(controlAddress + 2).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.write16(controlAddress + 2, programmedControl).status ==
    IOPBusStatus::Unmapped);
  REQUIRE(bus.readInstruction32(controlAddress).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.readDMAC32(controlAddress).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.dmaWriteStatus(controlAddress) == IOPBusStatus::Unmapped);
  REQUIRE(bus.writeDMAC32(controlAddress, programmedControl).status ==
    IOPBusStatus::Unmapped);
  std::uint8_t byte = 0x5a;
  REQUIRE(bus.loadPhysical(controlAddress, &byte, 1) == IOPBusStatus::Unmapped);
  REQUIRE(bus.inspectPhysical(controlAddress, &byte, 1) == IOPBusStatus::Unmapped);
  REQUIRE(byte == 0x5a);
  for (IOPAddress alias : {0x9f808268u, 0xbf808268u})
  {
    REQUIRE(bus.read32(alias).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(alias, programmedControl).status == IOPBusStatus::Unmapped);
  }
  REQUIRE_THROWS_AS(system.eeBus().read32(controlAddress), std::out_of_range);
  REQUIRE(bus.read32(controlAddress).value == programmedControl);

  IOPBus unattached;
  REQUIRE(unattached.read32(controlAddress).status == IOPBusStatus::Unmapped);
  REQUIRE(unattached.write32(controlAddress, programmedControl).status ==
    IOPBusStatus::Unmapped);
}

TEST_CASE("SIO2 port words retain independent full-width programming",
  "[iop][sio2][sio2-port]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  for (std::size_t index = 0; index < portValues.size(); ++index)
  {
    const auto address = portControlBase + static_cast<IOPAddress>(index * 4);
    CAPTURE(index);
    REQUIRE(bus.read32(address).status == IOPBusStatus::Completed);
    REQUIRE(bus.read32(address).value == 0);
    REQUIRE(bus.write32(address, portValues[index]).status == IOPBusStatus::Completed);
    for (std::size_t other = 0; other < portValues.size(); ++other)
    {
      REQUIRE(bus.read32(portControlBase + static_cast<IOPAddress>(other * 4)).value ==
        (other <= index ? portValues[other] : 0));
    }
  }
  for (std::size_t index = 0; index < portValues.size(); ++index)
  {
    const auto address = portControlBase + static_cast<IOPAddress>(index * 4);
    CAPTURE(index);
    for (IOPWord value : {0xc0c0050fu, 0x01060014u, 0xffffffffu, 0u})
    {
      CAPTURE(value);
      REQUIRE(bus.writeMasked32(address, value, 0xf).status == IOPBusStatus::Completed);
      REQUIRE(bus.read32(address).value == value);
      REQUIRE(bus.write32(address, value).status == IOPBusStatus::Completed);
      REQUIRE(bus.read32(address).value == value);
    }
  }
  REQUIRE(bus.read32(controlAddress).value == 0);
}

TEST_CASE("SIO2 port admission excludes partial narrow and nondata access",
  "[iop][sio2][sio2-port]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  IOPBus unattached;
  for (std::size_t index = 0; index < portValues.size(); ++index)
  {
    const auto address = portControlBase + static_cast<IOPAddress>(index * 4);
    CAPTURE(index);
    REQUIRE(bus.write32(address, portValues[index]).status == IOPBusStatus::Completed);
    for (std::uint8_t mask = 1; mask < 0xf; ++mask)
    {
      CAPTURE(mask);
      REQUIRE(bus.writeMasked32(address, ~portValues[index], mask).status ==
        IOPBusStatus::Unmapped);
    }
    for (IOPAddress offset = 0; offset < 4; ++offset)
    {
      CAPTURE(offset);
      REQUIRE(bus.read8(address + offset).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write8(address + offset, 0xbc).status == IOPBusStatus::Unmapped);
      if (offset % 2 == 0)
      {
        REQUIRE(bus.read16(address + offset).status == IOPBusStatus::Unmapped);
        REQUIRE(bus.write16(address + offset, 0x050f).status == IOPBusStatus::Unmapped);
      }
      if (offset != 0)
      {
        REQUIRE(bus.read32(address + offset).status == IOPBusStatus::Misaligned);
        REQUIRE(bus.write32(address + offset, 1).status == IOPBusStatus::Misaligned);
        REQUIRE(bus.writeMasked32(address + offset, 1, 0xf).status ==
          IOPBusStatus::Misaligned);
      }
    }
    REQUIRE(bus.readInstruction32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.readDMAC32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.dmaWriteStatus(address) == IOPBusStatus::Unmapped);
    REQUIRE(bus.writeDMAC32(address, 1).status == IOPBusStatus::Unmapped);
    std::uint8_t byte = 0x5a;
    REQUIRE(bus.loadPhysical(address, &byte, 1) == IOPBusStatus::Unmapped);
    REQUIRE(bus.inspectPhysical(address, &byte, 1) == IOPBusStatus::Unmapped);
    REQUIRE(byte == 0x5a);
    for (IOPAddress alias : {address | 0x80000000u, address | 0xa0000000u})
    {
      REQUIRE(bus.read32(alias).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write32(alias, 1).status == IOPBusStatus::Unmapped);
    }
    REQUIRE_THROWS_AS(system.eeBus().read32(address), std::out_of_range);
    REQUIRE(unattached.read32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(unattached.write32(address, 1).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read32(address).value == portValues[index]);
  }
  for (IOPAddress address : {portControlBase - 4, portControlBase + 32,
         portControlBase + 36})
  {
    CAPTURE(address);
    REQUIRE(bus.read32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(address, 1).status == IOPBusStatus::Unmapped);
  }
}

TEST_CASE("SIO2 port lifetime distinguishes passive control from owner reset",
  "[iop][sio2][sio2-port]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  REQUIRE_THROWS_AS(system.sio2Controller().readPortControl(8), std::out_of_range);
  REQUIRE_THROWS_AS(system.sio2Controller().writePortControl(8, 1), std::out_of_range);
  for (std::size_t index = 0; index < portValues.size(); ++index)
  {
    REQUIRE(bus.write32(portControlBase + static_cast<IOPAddress>(index * 4),
      portValues[index]).status == IOPBusStatus::Completed);
  }
  for (IOPWord control : {passiveControl, startupControl, passiveControl, 0u, 1u})
  {
    REQUIRE(bus.write32(controlAddress, control).status ==
      (control == 0 || control == 1 ? IOPBusStatus::Unmapped : IOPBusStatus::Completed));
    bus.reset();
    system.iopCore().reset();
    system.runMasterCycles(80);
    for (std::size_t index = 0; index < portValues.size(); ++index)
    {
      REQUIRE(bus.read32(portControlBase + static_cast<IOPAddress>(index * 4)).value ==
        portValues[index]);
    }
  }
  system.sio2Controller().reset();
  REQUIRE(bus.read32(controlAddress).value == 0);
  for (std::size_t index = 0; index < portValues.size(); ++index)
  {
    const auto address = portControlBase + static_cast<IOPAddress>(index * 4);
    REQUIRE(bus.read32(address).value == 0);
    REQUIRE(bus.write32(address, portValues[index]).status == IOPBusStatus::Completed);
  }
  system.reset();
  for (std::size_t index = 0; index < portValues.size(); ++index)
  {
    REQUIRE(system.iopBus().read32(portControlBase +
      static_cast<IOPAddress>(index * 4)).value == 0);
  }
}

TEST_CASE("SIO2 port guests preserve read delays and exact alias traces",
  "[iop][sio2][sio2-port][trace]")
{
  for (std::size_t index = 0; index < portValues.size(); ++index)
  for (IOPAddress segment : {0u, 0x80000000u, 0xa0000000u})
  {
    const auto address = segment | (portControlBase + static_cast<IOPAddress>(index * 4));
    CAPTURE(index);
    CAPTURE(address);
    NekoSystem system;
    installPortProgram(system, address);
    system.startTrace();
    system.runMasterCycles(120);
    requirePortResult(system, index);
    IOPTraceTest::requireEvents(controlEvents(system, address),
      expectedPortEvents(address, index));
    unsigned branchDelayStores = 0;
    for (const auto &event : system.trace())
    {
      if (event.subsystem == NekoTraceSubsystem::IOP &&
          event.type == NekoTraceEventType::InstructionIssued &&
          event.value0 == 0xa0000030)
      {
        REQUIRE(event.masterCycle == 97);
        REQUIRE(event.value1 == 0xac220000);
        REQUIRE(event.value3 == NekoIOPTraceInstruction::DELAY_SLOT);
        ++branchDelayStores;
      }
    }
    REQUIRE(branchDelayStores == 1);
  }
}

TEST_CASE("SIO2 port guests restore before writes loads and branch-delay repeats",
  "[iop][sio2][sio2-port][save-state][trace]")
{
  for (std::size_t index : {std::size_t{0}, std::size_t{7}})
  for (unsigned cut : {64u, 72u, 80u, 96u})
  {
    const auto address = 0xa0000000u | (portControlBase +
      static_cast<IOPAddress>(index * 4));
    CAPTURE(index);
    CAPTURE(cut);
    NekoSystem original;
    installPortProgram(original, address);
    original.runMasterCycles(cut);
    const auto &core = original.iopCore();
    REQUIRE(core.retiredInstructions() == cut / 8);
    REQUIRE(core.programCounter() == 0xa0000000 + cut / 2);
    REQUIRE(original.sio2Controller().readPortControl(index) ==
      (cut == 64 ? portValues[index] : 0xc0c0050f));
    REQUIRE(core.generalRegister(3) == (cut < 96 ? portValues[index] : 0xc0c0050f));
    REQUIRE(core.branchContinuation().active == (cut == 96));
    REQUIRE(core.delayedResultContinuation().source == (cut == 80
      ? IOPDelayedResultSource::Load : IOPDelayedResultSource::None));
    if (cut == 80)
    {
      REQUIRE(core.delayedResultContinuation().destination == 3);
      REQUIRE(core.delayedResultContinuation().value == 0xc0c0050f);
    }
    if (cut == 96)
    {
      REQUIRE(core.branchContinuation().instructionAddress == 0xa000002c);
      REQUIRE(core.branchContinuation().targetAddress == 0xa0000038);
    }
    const auto checkpoint = original.saveState();
    NekoSystem restored;
    NekoSystem plain;
    for (NekoSystem *destination : {&restored, &plain})
    {
      REQUIRE(destination->sio2Controller().writeControl(startupControl) ==
        SIO2AccessStatus::Accepted);
      for (std::size_t port = 0; port < portValues.size(); ++port)
      {
        destination->sio2Controller().writePortControl(port, ~portValues[port]);
      }
      destination->loadState(checkpoint);
      REQUIRE(destination->saveState() == checkpoint);
    }
    original.startTrace();
    restored.startTrace();
    original.runMasterCycles(120 - cut);
    restored.runMasterCycles(120 - cut);
    plain.runMasterCycles(120 - cut);
    requirePortResult(original, index);
    requirePortResult(restored, index);
    requirePortResult(plain, index);
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    REQUIRE_FALSE(original.trace().empty());
    REQUIRE(plain.trace().empty());
    REQUIRE_FALSE(plain.traceEnabled());
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireMachine(plain, original);
    IOPTraceTest::Events expected;
    for (const auto &event : expectedPortEvents(address, index))
    {
      if (event.masterCycle > cut)
      {
        expected.push_back(event);
      }
    }
    IOPTraceTest::requireEvents(controlEvents(restored, address), expected);
  }
}

TEST_CASE("SIO2 control has an explicit device and machine reset lifetime",
  "[iop][sio2]")
{
  IOPWord programmedControl = startupControl;
  SECTION("Startup programming")
  {
  }
  SECTION("Follow-on programming")
  {
    programmedControl = passiveControl;
  }
  SIO2Controller controller;
  REQUIRE(controller.readControl() == 0);
  REQUIRE(controller.writeControl(programmedControl) ==
    SIO2AccessStatus::Accepted);
  REQUIRE(controller.writeControl(0) == SIO2AccessStatus::Unsupported);
  REQUIRE(controller.readControl() == programmedControl);
  controller.reset();
  REQUIRE(controller.readControl() == 0);

  NekoSystem system;
  REQUIRE(system.iopBus().write32(controlAddress, programmedControl).status ==
    IOPBusStatus::Completed);
  system.iopBus().reset();
  REQUIRE(system.iopBus().read32(controlAddress).value == programmedControl);
  system.iopCore().reset();
  REQUIRE(system.iopBus().read32(controlAddress).value == programmedControl);
  system.runMasterCycles(80);
  REQUIRE(system.iopCore().elapsedCycles() == 0);
  REQUIRE(system.iopBus().read32(controlAddress).value == programmedControl);
  system.sio2Controller().reset();
  REQUIRE(system.iopBus().read32(controlAddress).value == 0);
  REQUIRE(system.iopBus().write32(controlAddress, programmedControl).status ==
    IOPBusStatus::Completed);
  system.reset();
  REQUIRE(system.iopBus().read32(controlAddress).status == IOPBusStatus::Completed);
  REQUIRE(system.iopBus().read32(controlAddress).value == 0);
  REQUIRE(system.iopBus().write32(controlAddress, programmedControl).status ==
    IOPBusStatus::Completed);
  REQUIRE(system.sio2Controller().readControl() == programmedControl);
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
  IOPWord retained = 0;
  for (bool pending : {false, true})
  {
    CAPTURE(pending);
    if (pending)
    {
      interrupts.assertSource(IOPInterruptSource::SIO2);
    }
    const auto status = interrupts.status();
    for (IOPWord value : {passiveControl, passiveControl, startupControl,
           0u, 1u, passiveControl, 0xdu, 0x3bdu, startupControl})
    {
      CAPTURE(value);
      const bool accepted = value == startupControl || value == passiveControl;
      REQUIRE(system.iopBus().write32(controlAddress, value).status ==
        (accepted ? IOPBusStatus::Completed : IOPBusStatus::Unmapped));
      if (accepted)
      {
        retained = value;
      }
      for (std::size_t index = 0; index < portValues.size(); ++index)
      {
        const auto address = portControlBase + static_cast<IOPAddress>(index * 4);
        REQUIRE(system.iopBus().write32(address, portValues[index]).status ==
          IOPBusStatus::Completed);
        REQUIRE(system.iopBus().read32(address).value == portValues[index]);
      }
      REQUIRE(system.iopBus().read32(controlAddress).value == retained);
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
  for (const auto &transition : controlTransitions)
  for (IOPAddress address : {controlAddress, 0x9f808268u, 0xbf808268u})
  {
    CAPTURE(transition.initial);
    CAPTURE(transition.programmed);
    CAPTURE(address);
    NekoSystem system;
    if (transition.initial != 0)
    {
      REQUIRE(system.sio2Controller().writeControl(transition.initial) ==
        SIO2AccessStatus::Accepted);
    }
    installProgram(system, controlProgram(address, transition.programmed));
    system.startTrace();
    system.runMasterCycles(112);
    requireControlResult(system, transition);
    IOPTraceTest::requireEvents(controlEvents(system, address),
      expectedControlEvents(address, transition));
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
  for (IOPWord programmed : {startupControl, passiveControl})
  for (IOPAddress address : {controlAddress, 0x9f808268u, 0xbf808268u})
  {
    CAPTURE(programmed);
    CAPTURE(address);
    NekoSystem system;
    REQUIRE(system.iopBus().write32(controlAddress, programmed).status ==
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
    REQUIRE(core.delayedResultContinuation().value == programmed);
    REQUIRE(core.storeFailure().width == IOPStoreWidth::Word);
    REQUIRE(core.storeFailure().value == 0);
    REQUIRE(core.storeFailure().mergeByteEnable == 0);
    REQUIRE(core.storeFailure().access.virtualAddress == address);
    REQUIRE(core.storeFailure().access.physicalAddress == controlAddress);
    REQUIRE(core.storeFailure().access.outcome == IOPAccessOutcome::Unmapped);
    REQUIRE(system.sio2Controller().readControl() == programmed);
    const auto route = address < 0xa0000000
      ? Memory::CacheRoute::Cached : Memory::CacheRoute::Uncached;
    IOPTraceTest::requireEvents(controlEvents(system, address), {
      {17, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, programmed, 0, nekoPackIOPMemoryTraceMetadata({
          controlAddress, 4, Memory::AccessKind::DataLoad,
          Memory::Outcome::Completed, route, true, 0})},
      {25, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, 0, 0, nekoPackIOPMemoryTraceMetadata({
          controlAddress, 4, Memory::AccessKind::DataStore,
          Memory::Outcome::Unmapped, route, true, 0xf})}
    });
  }
}

TEST_CASE("SIO2 control transitions and readback restore with traced and plain continuations",
  "[iop][sio2][sio2-conformance][save-state][trace]")
{
  constexpr IOPAddress address = 0xbf808268;
  for (const auto &transition : controlTransitions)
  for (unsigned cut : {56u, 64u, 72u, 88u})
  {
    CAPTURE(transition.initial);
    CAPTURE(transition.programmed);
    CAPTURE(cut);
    NekoSystem original;
    if (transition.initial != 0)
    {
      REQUIRE(original.sio2Controller().writeControl(transition.initial) ==
        SIO2AccessStatus::Accepted);
    }
    installProgram(original, controlProgram(address, transition.programmed));
    original.runMasterCycles(cut);
    const auto &core = original.iopCore();
    REQUIRE(core.retiredInstructions() == cut / 8);
    const IOPWord retained = cut == 56 ? transition.initial : transition.programmed;
    REQUIRE(original.sio2Controller().readControl() == retained);
    REQUIRE(core.programCounter() == 0xa0000000 + cut / 2);
    REQUIRE(core.generalRegister(3) ==
      (cut < 88 ? transition.initial : transition.programmed));
    REQUIRE(core.branchContinuation().active == (cut == 88));
    REQUIRE(core.delayedResultContinuation().source == (cut == 72
      ? IOPDelayedResultSource::Load : IOPDelayedResultSource::None));
    if (cut == 72)
    {
      REQUIRE(core.delayedResultContinuation().destination == 3);
      REQUIRE(core.delayedResultContinuation().value == transition.programmed);
    }
    if (cut == 88)
    {
      REQUIRE(core.branchContinuation().instructionAddress == 0xa0000028);
      REQUIRE(core.branchContinuation().targetAddress == 0xa0000034);
    }
    const auto checkpoint = original.saveState();
    NekoSystem restored;
    NekoSystem plain;
    const IOPWord destinationControl = retained == startupControl
      ? passiveControl : startupControl;
    REQUIRE(restored.sio2Controller().writeControl(destinationControl) ==
      SIO2AccessStatus::Accepted);
    REQUIRE(plain.sio2Controller().writeControl(destinationControl) ==
      SIO2AccessStatus::Accepted);
    restored.loadState(checkpoint);
    plain.loadState(checkpoint);
    REQUIRE(restored.saveState() == checkpoint);
    REQUIRE(plain.saveState() == checkpoint);
    original.startTrace();
    restored.startTrace();
    original.runMasterCycles(112 - cut);
    restored.runMasterCycles(112 - cut);
    plain.runMasterCycles(112 - cut);
    requireControlResult(original, transition);
    requireControlResult(restored, transition);
    requireControlResult(plain, transition);
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    REQUIRE_FALSE(original.trace().empty());
    REQUIRE(plain.trace().empty());
    REQUIRE_FALSE(plain.traceEnabled());
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireMachine(plain, original);
    auto expected = expectedControlEvents(address, transition);
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
