#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "catch.hpp"
#include "iop_trace_test_utils.hpp"
#include "neko_system.hpp"
#include "save_state_diagnostics.hpp"

namespace
{
  constexpr IOPAddress controlAddress = 0x1f808268;
  constexpr IOPWord startupControl = 0x3bc;
  constexpr IOPWord passiveControl = 0xc;
  constexpr IOPAddress portControlBase = 0x1f808240;
  constexpr std::array<IOPWord, 8> portValues = {{
    0x89abcdef, 0x01060014, 0xffffffff, 0, 0x80000000, 1, 0x12345678, 0xa5a55a5a
  }};
  constexpr IOPAddress descriptorBase = 0x1f808200;
  constexpr IOPAddress transmitAddress = 0x1f808260;
  constexpr IOPAddress receiveAddress = 0x1f808264;
  constexpr IOPAddress interruptAddress = 0x1f808280;
  constexpr IOPWord transferControl = 0xb1;
  constexpr IOPWord sio2IRQ = 1u << 17;
  constexpr std::array<std::uint8_t, 7> infraredPacket = {{
    0x61, 0x06, 0x03, 0, 0, 0, 0
  }};
  constexpr std::array<IOPWord, 16> descriptorValues = {{
    0x001c0740, 0, 0xffffffff, 0x80000000,
    1, 0x12345678, 0xa5a55a5a, 0x01060014,
    0x89abcdef, 2, 0x55aa55aa, 0xc0c0050f,
    0x00000100, 0x00040000, 0x7fffffff, 0x76543210
  }};

  struct ProgrammingBank
  {
    IOPAddress base;
    const IOPWord *values;
    std::size_t count;
    IOPWord programmed;
  };

  const ProgrammingBank programmingBanks[] = {
    {portControlBase, portValues.data(), portValues.size(), 0xc0c0050f},
    {descriptorBase, descriptorValues.data(), descriptorValues.size(), 0x001c0740}
  };

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

  void stageInfraredTransfer(NekoSystem &system, IOPWord descriptor = 0x001c0740)
  {
    REQUIRE(system.iopBus().write32(descriptorBase, descriptor).status ==
      IOPBusStatus::Completed);
    REQUIRE(system.iopBus().write32(descriptorBase + 4, 0).status ==
      IOPBusStatus::Completed);
    for (auto byte : infraredPacket)
    {
      REQUIRE(system.iopBus().write8(transmitAddress, byte).status ==
        IOPBusStatus::Completed);
    }
  }

  void installInfraredProgram(NekoSystem &system, IOPAddress address)
  {
    installProgram(system, std::array<IOPWord, 41>{{
      0x3c010000 | (address >> 16),
      0x34210000 | (address & 0xffff),
      0x3c02001c, 0x34420740,
      0xac220000, 0xac200004,
      0x24020061, 0xa0220060,
      0x24020006, 0xa0220060,
      0x24020003, 0xa0220060,
      0xa0200060, 0xa0200060, 0xa0200060, 0xa0200060,
      0x240200b1,
      0x10000002,
      0xac220068, // START in the taken branch delay slot
      0xffffffff,
      0x80230064, // lb
      0xac030100, 0xac030104,
      0x90240064, // lbu
      0xac040108, 0xac04010c,
      0x8c25006c, 0x8c260070, 0x8c270074, 0x8c280080,
      0x24090001, 0xac290080,
      0x902a0064, 0x902b0064, 0x902c0064, 0x902d0064, 0x902e0064,
      0, 0x8c2f0080, 0, 0x24101234
    }});
  }

  void requireInfraredGuestResult(NekoSystem &system)
  {
    REQUIRE(system.iopCore().retiredInstructions() == 40);
    REQUIRE(system.iopCore().programCounter() == 0xa00000a4);
    REQUIRE(system.iopCore().executionState() == IOPExecutionState::Running);
    REQUIRE(system.iopCore().exception().kind == IOPException::None);
    REQUIRE_FALSE(system.iopCore().branchContinuation().active);
    REQUIRE(system.iopCore().delayedResultContinuation().source ==
      IOPDelayedResultSource::None);
    REQUIRE(system.iopCore().generalRegister(3) == 0xffffffff);
    REQUIRE(system.iopCore().generalRegister(4) == 0xff);
    REQUIRE(system.iopCore().generalRegister(5) == 0x1d100);
    REQUIRE(system.iopCore().generalRegister(6) == 0xf);
    REQUIRE(system.iopCore().generalRegister(7) == 0);
    REQUIRE(system.iopCore().generalRegister(8) == 1);
    for (std::size_t reg = 10; reg <= 14; ++reg)
    {
      REQUIRE(system.iopCore().generalRegister(reg) == 0xff);
    }
    REQUIRE(system.iopCore().generalRegister(15) == 0);
    REQUIRE(system.iopCore().generalRegister(16) == 0x1234);
    REQUIRE(system.iopBus().read32(0x100).value == 0);
    REQUIRE(system.iopBus().read32(0x104).value == 0xffffffff);
    REQUIRE(system.iopBus().read32(0x108).value == 0);
    REQUIRE(system.iopBus().read32(0x10c).value == 0xff);
    REQUIRE(system.iopBus().read8(receiveAddress).status == IOPBusStatus::Unmapped);
    REQUIRE(system.sio2Controller().transmitInputSize() == 0);
    REQUIRE(system.iopInterruptController().status() == sio2IRQ);
  }

  void installSuccessiveInfraredProgram(NekoSystem &system, IOPAddress address)
  {
    stageInfraredTransfer(system);
    REQUIRE(system.iopBus().write32(controlAddress, transferControl).status ==
      IOPBusStatus::Completed);
    for (std::size_t index = 0; index < infraredPacket.size(); ++index)
    {
      REQUIRE(system.iopBus().read8(receiveAddress).value == 0xff);
    }
    system.iopInterruptController().writeStatus(~sio2IRQ);
    installProgram(system, std::array<IOPWord, 33>{{
      0x3c010000 | (address >> 16),
      0x34210000 | (address & 0xffff),
      0x2402000c, 0xac220068,
      0x3c02001c, 0x34420741,
      0xac220000, 0xac200004,
      0x24020061, 0xa0220060,
      0x24020006, 0xa0220060,
      0x24020003, 0xa0220060,
      0xa0200060, 0xa0200060, 0xa0200060, 0xa0200060,
      0x240200b1,
      0x10000002,
      0xac220068, // Successive START in the taken branch delay slot
      0xffffffff,
      0x8c230080, 0x8c24006c,
      0x90250064, 0x90260064, 0x90270064, 0x90280064,
      0x90290064, 0x902a0064, 0x902b0064,
      0, 0x240c1234
    }});
  }

  void requireSuccessiveInfraredGuestResult(NekoSystem &system)
  {
    REQUIRE(system.iopCore().retiredInstructions() == 32);
    REQUIRE(system.iopCore().programCounter() == 0xa0000084);
    REQUIRE(system.iopCore().executionState() == IOPExecutionState::Running);
    REQUIRE(system.iopCore().exception().kind == IOPException::None);
    REQUIRE_FALSE(system.iopCore().branchContinuation().active);
    REQUIRE(system.iopCore().delayedResultContinuation().source ==
      IOPDelayedResultSource::None);
    REQUIRE(system.iopCore().generalRegister(3) == 1);
    REQUIRE(system.iopCore().generalRegister(4) == 0x1d100);
    for (std::size_t reg = 5; reg <= 11; ++reg)
    {
      REQUIRE(system.iopCore().generalRegister(reg) == 0xff);
    }
    REQUIRE(system.iopCore().generalRegister(12) == 0x1234);
    REQUIRE(system.iopBus().read8(receiveAddress).status == IOPBusStatus::Unmapped);
    REQUIRE(system.sio2Controller().transmitInputSize() == 0);
    REQUIRE(system.iopBus().read32(interruptAddress).value == 1);
    REQUIRE(system.iopInterruptController().status() == 0);
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

  void installTransmitProgram(NekoSystem &system, IOPAddress address)
  {
    installProgram(system, std::array<IOPWord, 13>{{
      0x3c010000 | (address >> 16),
      0x34210000 | (address & 0xffff),
      0x24020061,
      0xa0220000,
      0x240200ff,
      0xa0220000,
      0x10000002,
      0xa0200000, // a byte in the taken branch delay slot
      0xffffffff,
      0x2402007f,
      0xa0220000,
      0xa0220000,
      0x24031234
    }});
  }

  IOPTraceTest::Events expectedTransmitEvents(IOPAddress address)
  {
    namespace Memory = NekoIOPTraceMemory;
    const auto route = address < 0xa0000000
      ? Memory::CacheRoute::Cached : Memory::CacheRoute::Uncached;
    const auto store = nekoPackIOPMemoryTraceMetadata({
      transmitAddress, 1, Memory::AccessKind::DataStore,
      Memory::Outcome::Completed, route, true, 1});
    return {
      {25, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, 0x61, 0, store},
      {41, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, 0xff, 0, store},
      {57, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, 0, 0, store},
      {73, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, 0x7f, 0, store},
      {81, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, 0x7f, 0, store}
    };
  }

  void requireTransmitResult(const NekoSystem &system)
  {
    REQUIRE(system.iopCore().retiredInstructions() == 12);
    REQUIRE(system.iopCore().programCounter() == 0xa0000034);
    REQUIRE(system.iopCore().executionState() == IOPExecutionState::Running);
    REQUIRE(system.iopCore().exception().kind == IOPException::None);
    REQUIRE(system.iopCore().generalRegister(3) == 0x1234);
    REQUIRE_FALSE(system.iopCore().branchContinuation().active);
    const std::uint8_t expected[] = {0x61, 0xff, 0, 0x7f, 0x7f};
    REQUIRE(system.sio2Controller().transmitInputSize() == 5);
    for (std::size_t index = 0; index < 5; ++index)
    {
      REQUIRE(system.sio2Controller().transmitInputByte(index) == expected[index]);
    }
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

  void installProgrammingProgram(NekoSystem &system, IOPAddress address,
    const ProgrammingBank &bank)
  {
    for (std::size_t index = 0; index < bank.count; ++index)
    {
      REQUIRE(system.iopBus().write32(bank.base +
        static_cast<IOPAddress>(index * 4), bank.values[index]).status ==
        IOPBusStatus::Completed);
    }
    installProgram(system, std::array<IOPWord, 16>{{
      0x3c010000 | (address >> 16),
      0x34210000 | (address & 0xffff),
      0x3c020000 | (bank.programmed >> 16),
      0x34420000 | (bank.programmed & 0xffff),
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

  IOPTraceTest::Events expectedProgrammingEvents(IOPAddress address, std::size_t index,
    const ProgrammingBank &bank)
  {
    namespace Memory = NekoIOPTraceMemory;
    const auto physical = bank.base + static_cast<IOPAddress>(index * 4);
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
        address, bank.values[index], 0, load},
      {65, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, bank.programmed, 0, store},
      {73, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, bank.programmed, 0, load},
      {97, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, bank.programmed, 0, store}
    };
  }

  void requireProgrammingResult(NekoSystem &system, std::size_t selected,
    const ProgrammingBank &bank)
  {
    REQUIRE(system.iopCore().retiredInstructions() == 15);
    REQUIRE(system.iopCore().programCounter() == 0xa0000040);
    REQUIRE(system.iopCore().executionState() == IOPExecutionState::Running);
    REQUIRE(system.iopCore().exception().kind == IOPException::None);
    REQUIRE(system.iopCore().generalRegister(3) == bank.programmed);
    REQUIRE(system.iopCore().generalRegister(4) == bank.programmed + 1);
    REQUIRE_FALSE(system.iopCore().branchContinuation().active);
    REQUIRE(system.iopCore().delayedResultContinuation().source ==
      IOPDelayedResultSource::None);
    REQUIRE(system.sio2Controller().readControl() == 0);
    REQUIRE(system.iopBus().readDMAC32(0x100).value == 0x1234);
    for (std::size_t index = 0; index < bank.count; ++index)
    {
      REQUIRE(system.iopBus().read32(bank.base +
        static_cast<IOPAddress>(index * 4)).value ==
        (index == selected ? bank.programmed : bank.values[index]));
    }
    REQUIRE(system.iopBus().readDMAC32(0x104).value == bank.values[selected]);
    REQUIRE(system.iopBus().readDMAC32(0x108).value == bank.values[selected]);
    REQUIRE(system.iopBus().readDMAC32(0x10c).value == bank.programmed);
  }
}

TEST_CASE("SIO2 absent infrared transfer publishes bounded replies and completion IRQ",
  "[iop][sio2][sio2-active]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  REQUIRE(bus.read8(receiveAddress).status == IOPBusStatus::Unmapped);
  for (auto address : {0x1f80826cu, 0x1f808270u, 0x1f808274u})
  {
    REQUIRE(bus.read32(address).status == IOPBusStatus::Unmapped);
  }
  stageInfraredTransfer(system);
  SECTION("Observed infrared probe")
  {
  }
  SECTION("Second port uses the same absent endpoint")
  {
    stageInfraredTransfer(system, 0x001c0741);
  }
  SECTION("Non-selector bytes are opaque to the absent endpoint")
  {
    REQUIRE(bus.write32(descriptorBase, 0x001c0740).status ==
      IOPBusStatus::Completed);
    for (auto byte : {0x61, 0xff, 0x80, 0x7f, 0xaa, 0x55, 1})
    {
      REQUIRE(bus.write8(transmitAddress, static_cast<std::uint8_t>(byte)).status ==
        IOPBusStatus::Completed);
    }
  }
  REQUIRE(bus.write32(controlAddress, transferControl).status ==
    IOPBusStatus::Completed);
  REQUIRE(bus.read32(controlAddress).value == transferControl);
  REQUIRE(system.sio2Controller().transmitInputSize() == 0);
  REQUIRE(bus.read32(0x1f80826c).value == 0x1d100);
  REQUIRE(bus.read32(0x1f808270).value == 0xf);
  REQUIRE(bus.read32(0x1f808274).value == 0);
  REQUIRE(bus.read32(interruptAddress).value == 1);
  REQUIRE(system.iopInterruptController().status() == sio2IRQ);
  REQUIRE(bus.write32(interruptAddress, 0).status == IOPBusStatus::Completed);
  REQUIRE(bus.read32(interruptAddress).value == 1);
  REQUIRE(bus.write32(interruptAddress, 1).status == IOPBusStatus::Completed);
  REQUIRE(bus.read32(interruptAddress).value == 0);
  REQUIRE(system.iopInterruptController().status() == sio2IRQ);
  system.iopInterruptController().writeStatus(~sio2IRQ);
  for (std::size_t index = 0; index < infraredPacket.size(); ++index)
  {
    CAPTURE(index);
    const auto reply = bus.read8(receiveAddress);
    REQUIRE(reply.status == IOPBusStatus::Completed);
    REQUIRE(reply.value == 0xff);
  }
  REQUIRE(bus.read8(receiveAddress).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.read32(0x1f80826c).value == 0x1d100);
  REQUIRE(bus.write32(controlAddress, transferControl).status ==
    IOPBusStatus::Unmapped);
  REQUIRE(system.iopInterruptController().status() == 0);
  stageInfraredTransfer(system);
  REQUIRE(bus.writeMasked32(controlAddress, transferControl, 0xf).status ==
    IOPBusStatus::Completed);
  REQUIRE(bus.read32(interruptAddress).value == 1);
  REQUIRE(system.iopInterruptController().status() == sio2IRQ);
}

TEST_CASE("SIO2 active starts reject unsupported or incomplete profiles atomically",
  "[iop][sio2][sio2-active]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  stageInfraredTransfer(system);
  SECTION("Missing input")
  {
    REQUIRE(bus.write32(descriptorBase, 0x001c0740).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.write8(transmitAddress, 0x61).status == IOPBusStatus::Completed);
  }
  SECTION("Excess input")
  {
    REQUIRE(bus.write8(transmitAddress, 0).status == IOPBusStatus::Completed);
  }
  SECTION("Different descriptor")
  {
    for (auto descriptor : {0x001c0700u, 0x001c0742u, 0x001c0743u,
           0x001c0744u, 0x801c0741u})
    {
      stageInfraredTransfer(system, descriptor);
      const auto before = system.iopStateHash();
      REQUIRE(bus.write32(controlAddress, transferControl).status ==
        IOPBusStatus::Unmapped);
      REQUIRE(system.iopStateHash() == before);
    }
  }
  SECTION("Multiple descriptors")
  {
    REQUIRE(bus.write32(descriptorBase + 4, 0x001c0740).status ==
      IOPBusStatus::Completed);
  }
  SECTION("Different endpoint")
  {
    REQUIRE(bus.write32(descriptorBase, 0x001c0740).status ==
      IOPBusStatus::Completed);
    for (auto byte : {1, 0x42, 0, 0, 0, 0, 0})
    {
      REQUIRE(bus.write8(transmitAddress, static_cast<std::uint8_t>(byte)).status ==
        IOPBusStatus::Completed);
    }
  }
  SECTION("Unread replies")
  {
    REQUIRE(bus.write32(controlAddress, transferControl).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.write32(interruptAddress, 1).status == IOPBusStatus::Completed);
    stageInfraredTransfer(system);
  }
  SECTION("Unread replies with pending cause")
  {
    REQUIRE(bus.write32(controlAddress, transferControl).status ==
      IOPBusStatus::Completed);
    stageInfraredTransfer(system, 0x001c0741);
  }
  const auto before = system.iopStateHash();
  const auto irqBefore = system.iopInterruptController().status();
  REQUIRE(bus.write32(controlAddress, transferControl).status ==
    IOPBusStatus::Unmapped);
  REQUIRE(system.iopStateHash() == before);
  REQUIRE(system.iopInterruptController().status() == irqBefore);
}

TEST_CASE("SIO2 successive infrared transfers separate completion from new interrupt cause",
  "[iop][sio2][sio2-active][sio2-successive]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  for (auto descriptor : {0x001c0740u, 0x001c0741u, 0x001c0741u, 0x001c0740u})
  {
    CAPTURE(descriptor);
    REQUIRE(bus.write32(controlAddress, passiveControl).status ==
      IOPBusStatus::Completed);
    stageInfraredTransfer(system, descriptor);
    REQUIRE(bus.write32(controlAddress, transferControl).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.read32(interruptAddress).value == 1);
    REQUIRE(system.iopInterruptController().status() == sio2IRQ);
    for (std::size_t index = 0; index < infraredPacket.size(); ++index)
    {
      const auto reply = bus.read8(receiveAddress);
      REQUIRE(reply.status == IOPBusStatus::Completed);
      REQUIRE(reply.value == 0xff);
    }
    REQUIRE(bus.read8(receiveAddress).status == IOPBusStatus::Unmapped);
  }

  system.iopInterruptController().writeStatus(~sio2IRQ);
  REQUIRE(bus.write32(controlAddress, startupControl).status ==
    IOPBusStatus::Completed);
  stageInfraredTransfer(system, 0x001c0741);
  REQUIRE(bus.write32(controlAddress, transferControl).status ==
    IOPBusStatus::Completed);
  REQUIRE(bus.read32(interruptAddress).value == 1);
  REQUIRE(system.iopInterruptController().status() == 0);

  REQUIRE(bus.write32(interruptAddress, 1).status == IOPBusStatus::Completed);
  stageInfraredTransfer(system);
  const auto unread = system.iopStateHash();
  REQUIRE(bus.write32(controlAddress, transferControl).status ==
    IOPBusStatus::Unmapped);
  REQUIRE(system.iopStateHash() == unread);
  for (std::size_t index = 0; index < infraredPacket.size(); ++index)
  {
    REQUIRE(bus.read8(receiveAddress).status == IOPBusStatus::Completed);
  }
  REQUIRE(bus.write32(controlAddress, transferControl).status ==
    IOPBusStatus::Completed);
  REQUIRE(bus.read32(interruptAddress).value == 1);
  REQUIRE(system.iopInterruptController().status() == sio2IRQ);
}

TEST_CASE("SIO2 successive infrared guests complete without reasserting an acknowledged INTC",
  "[iop][sio2][sio2-active][sio2-successive]")
{
  for (auto address : {0x1f808200u, 0x9f808200u, 0xbf808200u})
  {
    CAPTURE(address);
    NekoSystem system;
    installSuccessiveInfraredProgram(system, address);
    system.startTrace();
    system.runMasterCycles(256);
    requireSuccessiveInfraredGuestResult(system);
    namespace Memory = NekoIOPTraceMemory;
    const auto route = address < 0xa0000000
      ? Memory::CacheRoute::Cached : Memory::CacheRoute::Uncached;
    IOPTraceTest::requireEvents(controlEvents(system, address + 0x68), {
      {25, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address + 0x68, passiveControl, 0, nekoPackIOPMemoryTraceMetadata({
          controlAddress, 4, Memory::AccessKind::DataStore,
          Memory::Outcome::Completed, route, true, 0xf})},
      {161, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address + 0x68, transferControl, 0, nekoPackIOPMemoryTraceMetadata({
          controlAddress, 4, Memory::AccessKind::DataStore,
          Memory::Outcome::Completed, route, true, 0xf})}
    });
    REQUIRE(IOPTraceTest::subsystemEvents(
      system, NekoTraceSubsystem::IOPInterruptController).empty());
  }
}

TEST_CASE("SIO2 successive infrared guests restore pending cause before and after START",
  "[iop][sio2][sio2-active][sio2-successive][save-state]")
{
  for (auto checkpointCycle : {160u, 200u})
  {
    CAPTURE(checkpointCycle);
    NekoSystem original;
    installSuccessiveInfraredProgram(original, 0xbf808200);
    original.runMasterCycles(checkpointCycle);
    const auto checkpoint = original.saveState();
    NekoSystem restored;
    NekoSystem plain;
    restored.loadState(checkpoint);
    plain.loadState(checkpoint);
    REQUIRE(restored.saveState() == checkpoint);
    REQUIRE(plain.saveState() == checkpoint);
    original.startTrace();
    restored.startTrace();
    for (auto *system : {&original, &restored, &plain})
    {
      system->runMasterCycles(256 - checkpointCycle);
      requireSuccessiveInfraredGuestResult(*system);
    }
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireMachine(plain, original);
    REQUIRE(plain.trace().empty());
  }
}

TEST_CASE("SIO2 active access rejects wrong widths purposes and missing IRQ wiring",
  "[iop][sio2][sio2-active]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  stageInfraredTransfer(system);
  bus.attachInterruptController(nullptr);
  const auto staged = system.sio2Controller().transmitInputSize();
  REQUIRE(bus.write32(controlAddress, transferControl).status ==
    IOPBusStatus::Unmapped);
  REQUIRE(system.sio2Controller().transmitInputSize() == staged);
  REQUIRE(system.sio2Controller().readControl() == 0);
  bus.attachInterruptController(&system.iopInterruptController());
  REQUIRE(bus.write32(controlAddress, transferControl).status ==
    IOPBusStatus::Completed);
  const auto before = system.iopStateHash();
  for (auto address : {receiveAddress, 0x1f80826cu, 0x1f808270u, 0x1f808274u,
         interruptAddress})
  {
    CAPTURE(address);
    if (address != receiveAddress)
    {
      REQUIRE(bus.read8(address).status == IOPBusStatus::Unmapped);
    }
    REQUIRE(bus.read16(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(address, 1).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write16(address, 1).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.readInstruction32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.readDMAC32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.writeDMAC32(address, 1).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.dmaWriteStatus(address) == IOPBusStatus::Unmapped);
    for (std::uint8_t mask = 1; mask < 0xf; ++mask)
    {
      REQUIRE(bus.writeMasked32(address, 1, mask).status == IOPBusStatus::Unmapped);
    }
    if (address != interruptAddress)
    {
      REQUIRE(bus.write32(address, 1).status == IOPBusStatus::Unmapped);
    }
    for (IOPAddress segment : {0x80000000u, 0xa0000000u})
    {
      REQUIRE(bus.read32(address | segment).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.read8(address | segment).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write32(address | segment, 1).status == IOPBusStatus::Unmapped);
    }
    std::uint8_t byte = 0x55;
    REQUIRE(bus.loadPhysical(address, &byte, 1) == IOPBusStatus::Unmapped);
    REQUIRE(bus.inspectPhysical(address, &byte, 1) == IOPBusStatus::Unmapped);
    REQUIRE(byte == 0x55);
    REQUIRE_THROWS_AS(system.eeBus().read32(address), std::out_of_range);
  }
  for (auto value : {0u, 1u, 0xb0u, 0xb3u, 0x3bdu, 0x800000b1u, 0xffffffffu})
  {
    REQUIRE(bus.write32(controlAddress, value).status == IOPBusStatus::Unmapped);
  }
  for (auto value : {2u, 3u, 0xffffffffu})
  {
    REQUIRE(bus.write32(interruptAddress, value).status == IOPBusStatus::Unmapped);
  }
  REQUIRE(bus.read32(receiveAddress).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.read16(receiveAddress + 2).status == IOPBusStatus::Unmapped);
  REQUIRE(system.iopStateHash() == before);
}

TEST_CASE("SIO2 completed transfer lifetime preserves results across passive programming",
  "[iop][sio2][sio2-active]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  stageInfraredTransfer(system);
  REQUIRE(bus.write32(controlAddress, transferControl).status ==
    IOPBusStatus::Completed);
  REQUIRE(bus.read8(receiveAddress).value == 0xff);
  REQUIRE(bus.read8(receiveAddress).value == 0xff);
  for (auto control : {passiveControl, startupControl, passiveControl})
  {
    REQUIRE(bus.write32(controlAddress, control).status == IOPBusStatus::Completed);
  }
  REQUIRE(bus.write32(portControlBase, 0x89abcdef).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(descriptorBase, 0xffffffff).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(descriptorBase + 4, 0x12345678).status == IOPBusStatus::Completed);
  REQUIRE(bus.write8(transmitAddress, 0x55).status == IOPBusStatus::Completed);
  bus.reset();
  system.iopCore().reset();
  system.runMasterCycles(80);
  REQUIRE(bus.read32(interruptAddress).value == 1);
  REQUIRE(bus.read32(0x1f80826c).value == 0x1d100);
  REQUIRE(bus.read32(portControlBase).value == 0x89abcdef);
  REQUIRE(bus.read32(descriptorBase + 4).value == 0x12345678);
  REQUIRE(system.sio2Controller().transmitInputSize() == 1);
  for (unsigned index = 0; index < 5; ++index)
  {
    const auto reply = bus.read8(receiveAddress);
    REQUIRE(reply.status == IOPBusStatus::Completed);
    REQUIRE(reply.value == 0xff);
  }
  REQUIRE(bus.read8(receiveAddress).status == IOPBusStatus::Unmapped);
  system.sio2Controller().reset();
  REQUIRE(bus.read32(interruptAddress).value == 0);
  REQUIRE(bus.read32(0x1f80826c).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.read8(receiveAddress).status == IOPBusStatus::Unmapped);
  REQUIRE(system.iopInterruptController().status() == sio2IRQ);
  stageInfraredTransfer(system);
  REQUIRE(bus.write32(controlAddress, transferControl).status ==
    IOPBusStatus::Completed);
  system.reset();
  REQUIRE(bus.read8(receiveAddress).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.read32(interruptAddress).value == 0);
  REQUIRE(bus.read32(controlAddress).value == 0);
  REQUIRE(system.iopInterruptController().status() == 0);
}

TEST_CASE("SIO2 infrared guests execute a delay-slot start with exact byte and IRQ traces",
  "[iop][sio2][sio2-active]")
{
  for (auto address : {0x1f808200u, 0x9f808200u, 0xbf808200u})
  {
    CAPTURE(address);
    NekoSystem system;
    installInfraredProgram(system, address);
    system.startTrace();
    system.runMasterCycles(320);
    requireInfraredGuestResult(system);
    namespace Memory = NekoIOPTraceMemory;
    const auto route = address < 0xa0000000
      ? Memory::CacheRoute::Cached : Memory::CacheRoute::Uncached;
    IOPTraceTest::requireEvents(controlEvents(system, address + 0x68), {{
      145, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
      address + 0x68, transferControl, 0, nekoPackIOPMemoryTraceMetadata({
        controlAddress, 4, Memory::AccessKind::DataStore,
        Memory::Outcome::Completed, route, true, 0xf})
    }});
    IOPTraceTest::Events expectedReceive;
    for (auto cycle : {153u, 177u, 249u, 257u, 265u, 273u, 281u})
    {
      expectedReceive.push_back({
        cycle, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address + 0x64, 0xff, 0, nekoPackIOPMemoryTraceMetadata({
          receiveAddress, 1, Memory::AccessKind::DataLoad,
          Memory::Outcome::Completed, route, true, 0})
      });
    }
    IOPTraceTest::requireEvents(
      controlEvents(system, address + 0x64), expectedReceive);
    const auto interrupts = IOPTraceTest::subsystemEvents(
      system, NekoTraceSubsystem::IOPInterruptController);
    REQUIRE(interrupts.size() == 1);
    REQUIRE(interrupts[0].masterCycle == 145);
    REQUIRE(interrupts[0].value0 == sio2IRQ);
  }
}

TEST_CASE("SIO2 infrared guests restore staged partial and acknowledged transfers",
  "[iop][sio2][sio2-active][save-state]")
{
  for (auto checkpointCycle : {144u, 152u, 160u, 248u, 288u})
  {
    CAPTURE(checkpointCycle);
    NekoSystem original;
    installInfraredProgram(original, 0xbf808200);
    original.runMasterCycles(checkpointCycle);
    const auto checkpoint = original.saveState();
    NekoSystem restored;
    NekoSystem plain;
    stageInfraredTransfer(restored);
    REQUIRE(restored.iopBus().write32(controlAddress, transferControl).status ==
      IOPBusStatus::Completed);
    restored.loadState(checkpoint);
    plain.loadState(checkpoint);
    REQUIRE(restored.saveState() == checkpoint);
    REQUIRE(plain.saveState() == checkpoint);
    original.startTrace();
    restored.startTrace();
    for (auto *system : {&original, &restored, &plain})
    {
      system->runMasterCycles(320 - checkpointCycle);
      requireInfraredGuestResult(*system);
    }
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireMachine(plain, original);
    REQUIRE(plain.trace().empty());
  }
}

TEST_CASE("SIO2 transmit staging retains bytes and rejects overflow without mutation",
  "[iop][sio2][sio2-tx]")
{
  NekoSystem system;
  const auto coldHash = system.iopStateHash();
  for (std::size_t index = 0; index < 1023; ++index)
  {
    CAPTURE(index);
    REQUIRE(system.iopBus().write8(transmitAddress,
      static_cast<std::uint8_t>(index)).status == IOPBusStatus::Completed);
  }
  REQUIRE(system.iopStateHash() != coldHash);
  const auto full = system.saveState();
  const auto hash = system.iopStateHash();
  REQUIRE(system.iopBus().write8(transmitAddress, 0x61).status ==
    IOPBusStatus::Unmapped);
  REQUIRE(system.iopStateHash() == hash);
  REQUIRE(system.saveState() == full);
  const auto fields = inspectSaveState(full);
  const auto *count = fields.find("system.iop.sio2.transmitInput.count");
  REQUIRE(count != nullptr);
  REQUIRE(count->kind == SaveStateFieldKind::U16);
  REQUIRE(count->scalarValue == 1023);
  for (std::size_t index = 0; index < 1023; ++index)
  {
    const auto *byte = fields.find("system.iop.sio2.transmitInput.bytes[" +
      std::to_string(index) + "].value");
    REQUIRE(byte != nullptr);
    REQUIRE(byte->scalarValue == static_cast<std::uint8_t>(index));
  }
  system.sio2Controller().reset();
  REQUIRE(system.iopStateHash() == coldHash);
  REQUIRE_THROWS_AS(system.sio2Controller().transmitInputByte(0), std::out_of_range);
}

TEST_CASE("SIO2 transmit staging admits only the exact CPU byte write",
  "[iop][sio2][sio2-tx]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  REQUIRE(bus.write8(transmitAddress, 0x61).status == IOPBusStatus::Completed);
  const auto before = system.iopStateHash();
  for (IOPAddress address = transmitAddress; address < transmitAddress + 8; ++address)
  {
    CAPTURE(address);
    REQUIRE(bus.read8(address).status == IOPBusStatus::Unmapped);
    if (address != transmitAddress)
    {
      REQUIRE(bus.write8(address, 0xff).status == IOPBusStatus::Unmapped);
    }
  }
  REQUIRE(bus.read16(transmitAddress).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.write16(transmitAddress, 0x61).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.read32(transmitAddress).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.write32(transmitAddress, 0x61).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.readInstruction32(transmitAddress).status == IOPBusStatus::Unmapped);
  for (std::uint8_t mask = 1; mask <= 0xf; ++mask)
  {
    REQUIRE(bus.writeMasked32(transmitAddress, 0x61, mask).status ==
      IOPBusStatus::Unmapped);
  }
  REQUIRE(bus.readDMAC32(transmitAddress).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.dmaWriteStatus(transmitAddress) == IOPBusStatus::Unmapped);
  REQUIRE(bus.writeDMAC32(transmitAddress, 0x61).status == IOPBusStatus::Unmapped);
  std::uint8_t byte = 0x55;
  REQUIRE(bus.loadPhysical(transmitAddress, &byte, 1) == IOPBusStatus::Unmapped);
  REQUIRE(bus.inspectPhysical(transmitAddress, &byte, 1) == IOPBusStatus::Unmapped);
  REQUIRE(byte == 0x55);
  for (IOPAddress alias : {0x9f808260u, 0xbf808260u})
  {
    REQUIRE(bus.write8(alias, 0x61).status == IOPBusStatus::Unmapped);
  }
  REQUIRE_THROWS_AS(system.eeBus().read32(transmitAddress), std::out_of_range);
  IOPBus unattached;
  REQUIRE(unattached.write8(transmitAddress, 0x61).status == IOPBusStatus::Unmapped);
  REQUIRE(system.iopStateHash() == before);
}

TEST_CASE("SIO2 staged bytes survive passive programming time and local resets",
  "[iop][sio2][sio2-tx]")
{
  NekoSystem system;
  auto &controller = system.sio2Controller();
  REQUIRE(controller.transmitInputSize() == 0);
  REQUIRE_THROWS_AS(controller.transmitInputByte(0), std::out_of_range);
  REQUIRE(system.iopBus().write8(transmitAddress, 0x61).status ==
    IOPBusStatus::Completed);
  REQUIRE(system.iopBus().write8(transmitAddress, 0).status ==
    IOPBusStatus::Completed);
  for (IOPWord control : {passiveControl, startupControl, passiveControl})
  {
    REQUIRE(controller.writeControl(control) == SIO2AccessStatus::Accepted);
  }
  controller.writePortControl(0, 0x01060014);
  controller.writePortControl(7, 0xffffffff);
  controller.writeCommandDescriptor(1, 0);
  controller.writeCommandDescriptor(15, 0x001c0740);
  REQUIRE_THROWS_AS(controller.writeCommandDescriptor(16, 0), std::out_of_range);
  REQUIRE(system.iopBus().writeMasked32(descriptorBase, 0, 1).status ==
    IOPBusStatus::Unmapped);
  REQUIRE(system.iopBus().write16(descriptorBase, 0).status ==
    IOPBusStatus::Unmapped);
  REQUIRE(controller.writeControl(1) == SIO2AccessStatus::Unsupported);
  system.runMasterCycles(80);
  system.iopBus().reset();
  system.iopCore().reset();
  REQUIRE(controller.transmitInputSize() == 2);
  REQUIRE(controller.transmitInputByte(0) == 0x61);
  REQUIRE(controller.transmitInputByte(1) == 0);
  REQUIRE_THROWS_AS(controller.transmitInputByte(2), std::out_of_range);
  REQUIRE_THROWS_AS(controller.transmitInputByte(static_cast<std::size_t>(-1)),
    std::out_of_range);
  system.reset();
  REQUIRE(controller.transmitInputSize() == 0);
  REQUIRE(system.iopBus().write8(transmitAddress, 0xff).status ==
    IOPBusStatus::Completed);
  REQUIRE(controller.transmitInputByte(0) == 0xff);
}

TEST_CASE("SIO2 transmit guests retain repeats delay-slot bytes and exact alias traces",
  "[iop][sio2][sio2-tx][trace]")
{
  for (IOPAddress address : {transmitAddress, 0x9f808260u, 0xbf808260u})
  {
    CAPTURE(address);
    NekoSystem system;
    installTransmitProgram(system, address);
    system.startTrace();
    system.runMasterCycles(96);
    requireTransmitResult(system);
    IOPTraceTest::requireEvents(controlEvents(system, address),
      expectedTransmitEvents(address));
    unsigned delayStores = 0;
    for (const auto &event : system.trace())
    {
      if (event.subsystem == NekoTraceSubsystem::IOP &&
          event.type == NekoTraceEventType::InstructionIssued &&
          event.value0 == 0xa000001c)
      {
        REQUIRE(event.masterCycle == 57);
        REQUIRE(event.value1 == 0xa0200000);
        REQUIRE(event.value3 == NekoIOPTraceInstruction::DELAY_SLOT);
        ++delayStores;
      }
    }
    REQUIRE(delayStores == 1);
  }
}

TEST_CASE("SIO2 staged transmit guests restore traced and plain pending writes",
  "[iop][sio2][sio2-tx][save-state][trace]")
{
  constexpr IOPAddress address = 0xbf808260;
  for (unsigned cut : {24u, 32u, 56u, 80u})
  {
    CAPTURE(cut);
    NekoSystem original;
    installTransmitProgram(original, address);
    original.runMasterCycles(cut);
    REQUIRE(original.iopCore().branchContinuation().active == (cut == 56));
    REQUIRE(original.sio2Controller().transmitInputSize() ==
      (cut == 24 ? 0 : cut == 32 ? 1 : cut == 56 ? 2 : 4));
    const auto checkpoint = original.saveState();
    NekoSystem restored;
    NekoSystem plain;
    REQUIRE(restored.iopBus().write8(transmitAddress, 0x55).status ==
      IOPBusStatus::Completed);
    REQUIRE(plain.iopBus().write8(transmitAddress, 0xaa).status ==
      IOPBusStatus::Completed);
    restored.loadState(checkpoint);
    plain.loadState(checkpoint);
    REQUIRE(restored.saveState() == checkpoint);
    REQUIRE(plain.saveState() == checkpoint);
    original.startTrace();
    restored.startTrace();
    original.runMasterCycles(96 - cut);
    restored.runMasterCycles(96 - cut);
    plain.runMasterCycles(96 - cut);
    requireTransmitResult(original);
    requireTransmitResult(restored);
    requireTransmitResult(plain);
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    REQUIRE_FALSE(original.trace().empty());
    REQUIRE(plain.trace().empty());
    REQUIRE_FALSE(plain.traceEnabled());
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireMachine(plain, original);
    IOPTraceTest::Events suffix;
    for (const auto &event : expectedTransmitEvents(address))
    {
      if (event.masterCycle > cut)
      {
        suffix.push_back(event);
      }
    }
    IOPTraceTest::requireEvents(controlEvents(original, address), suffix);
  }
}

TEST_CASE("SIO2 transmit overflow preserves the rejected guest delay-slot store",
  "[iop][sio2][sio2-tx][trace]")
{
  NekoSystem system;
  for (std::size_t index = 0; index < 1023; ++index)
  {
    REQUIRE(system.sio2Controller().writeTransmitByte(
      static_cast<std::uint8_t>(index)) == SIO2AccessStatus::Accepted);
  }
  installProgram(system, std::array<IOPWord, 5>{{
    0x3c01bf80, 0x34218260, 0x24020061, 0x10000002, 0xa0220000
  }});
  system.startTrace();
  system.runMasterCycles(40);
  const auto &core = system.iopCore();
  REQUIRE(core.retiredInstructions() == 4);
  REQUIRE(core.programCounter() == 0xa0000010);
  REQUIRE(core.stopReason() == IOPStopReason::StoreFailure);
  REQUIRE(core.exception().kind == IOPException::None);
  REQUIRE(core.branchContinuation().active);
  REQUIRE(core.storeFailure().width == IOPStoreWidth::Byte);
  REQUIRE(core.storeFailure().value == 0x61);
  REQUIRE(core.storeFailure().access.virtualAddress == 0xbf808260);
  REQUIRE(core.storeFailure().access.physicalAddress == transmitAddress);
  REQUIRE(core.storeFailure().access.outcome == IOPAccessOutcome::Unmapped);
  REQUIRE(system.sio2Controller().transmitInputSize() == 1023);
  for (std::size_t index = 0; index < 1023; ++index)
  {
    REQUIRE(system.sio2Controller().transmitInputByte(index) ==
      static_cast<std::uint8_t>(index));
  }
  namespace Memory = NekoIOPTraceMemory;
  IOPTraceTest::requireEvents(controlEvents(system, 0xbf808260), {
    {33, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
      0xbf808260, 0x61, 0, nekoPackIOPMemoryTraceMetadata({
        transmitAddress, 1, Memory::AccessKind::DataStore,
        Memory::Outcome::Unmapped, Memory::CacheRoute::Uncached, true, 1})}
  });
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
    if (address != transmitAddress)
    {
      REQUIRE(bus.write8(address, 0xbc).status == IOPBusStatus::Unmapped);
    }
    REQUIRE(bus.write16(address, programmedControl).status == IOPBusStatus::Unmapped);
    if (address != controlAddress &&
        (address < descriptorBase || address >= portControlBase + 32))
    {
      REQUIRE(bus.read32(address).status == (address == interruptAddress
        ? IOPBusStatus::Completed : IOPBusStatus::Unmapped));
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

TEST_CASE("SIO2 programming banks retain independent full-width words",
  "[iop][sio2][sio2-port][sio2-descriptor]")
{
  for (const auto &bank : programmingBanks)
  {
    CAPTURE(bank.base);
    NekoSystem system;
    auto &bus = system.iopBus();
    for (std::size_t index = 0; index < bank.count; ++index)
    {
      const auto address = bank.base + static_cast<IOPAddress>(index * 4);
      CAPTURE(index);
      REQUIRE(bus.read32(address).status == IOPBusStatus::Completed);
      REQUIRE(bus.read32(address).value == 0);
      REQUIRE(bus.write32(address, bank.values[index]).status == IOPBusStatus::Completed);
      for (std::size_t other = 0; other < bank.count; ++other)
      {
        REQUIRE(bus.read32(bank.base + static_cast<IOPAddress>(other * 4)).value ==
          (other <= index ? bank.values[other] : 0));
      }
    }
    for (std::size_t index = 0; index < bank.count; ++index)
    {
      const auto address = bank.base + static_cast<IOPAddress>(index * 4);
      CAPTURE(index);
      for (IOPWord value : {bank.programmed, 0x01060014u, 0xffffffffu, 0u})
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
}

TEST_CASE("SIO2 programming admission excludes partial narrow and nondata access",
  "[iop][sio2][sio2-port][sio2-descriptor]")
{
  for (const auto &bank : programmingBanks)
  {
    CAPTURE(bank.base);
    NekoSystem system;
    auto &bus = system.iopBus();
    IOPBus unattached;
    for (std::size_t index = 0; index < bank.count; ++index)
    {
      const auto address = bank.base + static_cast<IOPAddress>(index * 4);
      CAPTURE(index);
      REQUIRE(bus.write32(address, bank.values[index]).status == IOPBusStatus::Completed);
      for (std::uint8_t mask = 1; mask < 0xf; ++mask)
      {
        CAPTURE(mask);
        REQUIRE(bus.writeMasked32(address, ~bank.values[index], mask).status ==
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
      REQUIRE(bus.read32(address).value == bank.values[index]);
    }
    for (IOPAddress address : {descriptorBase - 4, portControlBase + 32,
           portControlBase + 36})
    {
      CAPTURE(address);
      REQUIRE(bus.read32(address).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write32(address, 1).status == IOPBusStatus::Unmapped);
    }
  }
}

TEST_CASE("SIO2 programming lifetime distinguishes passive control from owner reset",
  "[iop][sio2][sio2-port][sio2-descriptor]")
{
  for (const auto &bank : programmingBanks)
  {
    CAPTURE(bank.base);
    NekoSystem system;
    auto &bus = system.iopBus();
    REQUIRE_THROWS_AS(system.sio2Controller().readPortControl(8), std::out_of_range);
    REQUIRE_THROWS_AS(system.sio2Controller().writePortControl(8, 1), std::out_of_range);
    for (std::size_t index = 0; index < bank.count; ++index)
    {
      REQUIRE(bus.write32(bank.base + static_cast<IOPAddress>(index * 4),
        bank.values[index]).status == IOPBusStatus::Completed);
    }
    for (IOPWord control : {passiveControl, startupControl, passiveControl, 0u, 1u})
    {
      REQUIRE(bus.write32(controlAddress, control).status ==
        (control == 0 || control == 1 ? IOPBusStatus::Unmapped : IOPBusStatus::Completed));
      bus.reset();
      system.iopCore().reset();
      system.runMasterCycles(80);
      for (std::size_t index = 0; index < bank.count; ++index)
      {
        REQUIRE(bus.read32(bank.base + static_cast<IOPAddress>(index * 4)).value ==
          bank.values[index]);
      }
    }
    system.sio2Controller().reset();
    REQUIRE(bus.read32(controlAddress).value == 0);
    for (std::size_t index = 0; index < bank.count; ++index)
    {
      const auto address = bank.base + static_cast<IOPAddress>(index * 4);
      REQUIRE(bus.read32(address).value == 0);
      REQUIRE(bus.write32(address, bank.values[index]).status == IOPBusStatus::Completed);
    }
    system.reset();
    for (std::size_t index = 0; index < bank.count; ++index)
    {
      REQUIRE(system.iopBus().read32(bank.base +
        static_cast<IOPAddress>(index * 4)).value == 0);
    }
  }
}

TEST_CASE("SIO2 entry-zero programming preserves other passive images",
  "[iop][sio2][sio2-descriptor]")
{
  NekoSystem system;
  auto &controller = system.sio2Controller();
  REQUIRE_THROWS_AS(controller.readCommandDescriptor(16), std::out_of_range);
  REQUIRE_THROWS_AS(controller.writeCommandDescriptor(16, 1), std::out_of_range);
  REQUIRE(controller.writeControl(startupControl) == SIO2AccessStatus::Accepted);
  for (std::size_t index = 0; index < portValues.size(); ++index)
  {
    controller.writePortControl(index, portValues[index]);
  }
  for (std::size_t index = descriptorValues.size(); index > 0; --index)
  {
    REQUIRE(system.iopBus().write32(descriptorBase +
      static_cast<IOPAddress>((index - 1) * 4), descriptorValues[index - 1]).status ==
      IOPBusStatus::Completed);
  }
  for (IOPWord value : {0u, 0x001c0740u, 0xffffffffu})
  {
    REQUIRE(system.iopBus().write8(transmitAddress, 0x61).status ==
      IOPBusStatus::Completed);
    REQUIRE(controller.transmitInputSize() == 1);
    REQUIRE(system.iopBus().write32(descriptorBase, value).status ==
      IOPBusStatus::Completed);
    REQUIRE(controller.transmitInputSize() == 0);
    REQUIRE_THROWS_AS(controller.transmitInputByte(0), std::out_of_range);
    REQUIRE(controller.readCommandDescriptor(0) == value);
    REQUIRE(controller.readControl() == startupControl);
    for (std::size_t index = 0; index < portValues.size(); ++index)
    {
      REQUIRE(controller.readPortControl(index) == portValues[index]);
    }
    for (std::size_t index = 1; index < descriptorValues.size(); ++index)
    {
      REQUIRE(controller.readCommandDescriptor(index) == descriptorValues[index]);
    }
  }
}

TEST_CASE("SIO2 programming guests preserve read delays and exact alias traces",
  "[iop][sio2][sio2-port][sio2-descriptor][trace]")
{
  for (const auto &bank : programmingBanks)
  for (std::size_t index = 0; index < bank.count; ++index)
  for (IOPAddress segment : {0u, 0x80000000u, 0xa0000000u})
  {
    const auto address = segment | (bank.base + static_cast<IOPAddress>(index * 4));
    CAPTURE(index);
    CAPTURE(address);
    NekoSystem system;
    installProgrammingProgram(system, address, bank);
    system.startTrace();
    system.runMasterCycles(120);
    requireProgrammingResult(system, index, bank);
    IOPTraceTest::requireEvents(controlEvents(system, address),
      expectedProgrammingEvents(address, index, bank));
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

TEST_CASE("SIO2 programming guests restore before writes loads and branch-delay repeats",
  "[iop][sio2][sio2-port][sio2-descriptor][save-state][trace]")
{
  for (const auto &bank : programmingBanks)
  for (std::size_t index : {std::size_t{0}, bank.count - 1})
  for (unsigned cut : {64u, 72u, 80u, 96u})
  {
    const auto address = 0xa0000000u | (bank.base +
      static_cast<IOPAddress>(index * 4));
    CAPTURE(index);
    CAPTURE(cut);
    NekoSystem original;
    installProgrammingProgram(original, address, bank);
    original.runMasterCycles(cut);
    const auto &core = original.iopCore();
    REQUIRE(core.retiredInstructions() == cut / 8);
    REQUIRE(core.programCounter() == 0xa0000000 + cut / 2);
    REQUIRE(original.iopBus().read32(bank.base +
      static_cast<IOPAddress>(index * 4)).value ==
      (cut == 64 ? bank.values[index] : bank.programmed));
    REQUIRE(core.generalRegister(3) ==
      (cut < 96 ? bank.values[index] : bank.programmed));
    REQUIRE(core.branchContinuation().active == (cut == 96));
    REQUIRE(core.delayedResultContinuation().source == (cut == 80
      ? IOPDelayedResultSource::Load : IOPDelayedResultSource::None));
    if (cut == 80)
    {
      REQUIRE(core.delayedResultContinuation().destination == 3);
      REQUIRE(core.delayedResultContinuation().value == bank.programmed);
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
      for (std::size_t word = 0; word < bank.count; ++word)
      {
        REQUIRE(destination->iopBus().write32(bank.base +
          static_cast<IOPAddress>(word * 4), ~bank.values[word]).status ==
          IOPBusStatus::Completed);
      }
      destination->loadState(checkpoint);
      REQUIRE(destination->saveState() == checkpoint);
    }
    original.startTrace();
    restored.startTrace();
    original.runMasterCycles(120 - cut);
    restored.runMasterCycles(120 - cut);
    plain.runMasterCycles(120 - cut);
    requireProgrammingResult(original, index, bank);
    requireProgrammingResult(restored, index, bank);
    requireProgrammingResult(plain, index, bank);
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    REQUIRE_FALSE(original.trace().empty());
    REQUIRE(plain.trace().empty());
    REQUIRE_FALSE(plain.traceEnabled());
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireMachine(plain, original);
    IOPTraceTest::Events expected;
    for (const auto &event : expectedProgrammingEvents(address, index, bank))
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
      for (std::size_t index = 0; index < descriptorValues.size(); ++index)
      {
        const auto address = descriptorBase + static_cast<IOPAddress>(index * 4);
        REQUIRE(system.iopBus().write32(address, descriptorValues[index]).status ==
          IOPBusStatus::Completed);
        REQUIRE(system.iopBus().read32(address).value == descriptorValues[index]);
      }
      REQUIRE(system.iopBus().read32(controlAddress).value == retained);
      REQUIRE(system.iopBus().write8(transmitAddress, 0x61).status ==
        IOPBusStatus::Completed);
      REQUIRE(system.iopBus().write8(transmitAddress, 0).status ==
        IOPBusStatus::Completed);
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
