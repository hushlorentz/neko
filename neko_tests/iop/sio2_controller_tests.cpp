#include <array>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "catch.hpp"
#include "iop_trace_test_utils.hpp"
#include "neko_system.hpp"
#include "save_state_diagnostics.hpp"
#include "save_state_mutation.hpp"
#include "startup_diagnostics.hpp"

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

  void stageCardProbe(NekoSystem &system, IOPWord descriptor = 0x00100472,
    IOPWord firstWord = 0x00001181, std::size_t words = 36,
    IOPWord paddingWord = 0)
  {
    auto &bus = system.iopBus();
    const auto port = descriptor & 3;
    REQUIRE(bus.write32(portControlBase + port * 8, 0xff020405).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.write32(portControlBase + port * 8 + 4, 0x0005ffff).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.write32(descriptorBase, descriptor).status == IOPBusStatus::Completed);
    for (std::size_t word = 0; word < 36; ++word)
      REQUIRE(bus.write32(static_cast<IOPAddress>(0x1000 + word * 4),
        word == 0 ? firstWord : paddingWord).status == IOPBusStatus::Completed);
    REQUIRE(bus.write32(0x1f801540, 0x1000).status == IOPBusStatus::Completed);
    REQUIRE(bus.write32(0x1f801544, 0x10024).status == IOPBusStatus::Completed);
    REQUIRE(bus.write32(0x1f801548, 0x01000201).status == IOPBusStatus::Completed);
    REQUIRE(bus.write32(0x1f801570, 8 << 16).status == IOPBusStatus::Completed);
    for (std::size_t word = 0; word < words; ++word)
      REQUIRE(system.iopDMAController().clockSIO2Input(bus,
        system.sio2Controller()).outcome ==
          (word == 35 ? IOPDMABeatOutcome::Completed : IOPDMABeatOutcome::Accepted));
  }

  void stageCPUCardRead(NekoSystem &system, IOPWord descriptor = 0x000c0342,
    std::array<std::uint8_t, 3> packet = {{0x81, 0x52, 0}})
  {
    auto &bus = system.iopBus();
    const auto port = descriptor & 3;
    REQUIRE(bus.write32(portControlBase + port * 8, 0xffc00505).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.write32(portControlBase + port * 8 + 4, 0x000201f4).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.write32(descriptorBase, descriptor).status == IOPBusStatus::Completed);
    for (auto byte : packet)
      REQUIRE(bus.write8(transmitAddress, byte).status == IOPBusStatus::Completed);
  }

  void stageCPUMultitapSupport(NekoSystem &system, IOPWord descriptor = 0x00180642,
    std::array<std::uint8_t, 6> packet = {{0x21, 0x12, 0, 0, 0, 0}})
  {
    auto &bus = system.iopBus();
    const auto port = descriptor & 3;
    REQUIRE(bus.write32(portControlBase + port * 8, 0xff020505).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.write32(portControlBase + port * 8 + 4, 0x00030064).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.write32(descriptorBase, descriptor).status == IOPBusStatus::Completed);
    for (auto byte : packet)
      REQUIRE(bus.write8(transmitAddress, byte).status == IOPBusStatus::Completed);
  }

  void requireRejectedCPUStart(NekoSystem &system, IOPWord control = 0x3bd)
  {
    const auto before = system.iopStateHash();
    const auto irq = system.iopInterruptController().status();
    REQUIRE(system.iopBus().write32(controlAddress, control).status ==
      IOPBusStatus::Unmapped);
    REQUIRE(system.iopStateHash() == before);
    REQUIRE(system.iopInterruptController().status() == irq);
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

TEST_CASE("SIO2 DMA absent-card profiles complete only on the next IOP pulse",
  "[iop][sio2][sio2-card]")
{
  for (const auto descriptor : {0x00100472u, 0x00100473u, 0x00140572u, 0x00140573u})
  {
    CAPTURE(descriptor);
    const auto replyBytes = (descriptor >> 18) & 0x1ff;
    const auto firstWord = replyBytes == 5 ? 0xf381u : 0x1181u;
    NekoSystem system;
    stageCardProbe(system, descriptor, firstWord);
    auto &bus = system.iopBus();
    system.runMasterCycles(2);
    REQUIRE(bus.write32(controlAddress, 0x3bd).status == IOPBusStatus::Completed);
    REQUIRE(system.sio2Controller().dmaResultPhase() == SIO2DMAResultPhase::Pending);
    REQUIRE(system.sio2Controller().dmaResultSize() == 0);
    REQUIRE_THROWS_AS(system.sio2Controller().dmaResultByte(0), std::out_of_range);
    REQUIRE(system.sio2Controller().transmitInputPhase() == SIO2IngressPhase::Empty);
    REQUIRE(bus.read32(0x1f80826c).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read32(interruptAddress).value == 0);
    REQUIRE(system.iopInterruptController().status() == 0);
    system.runMasterCycles(6);
    REQUIRE(bus.read32(0x1f80826c).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read32(interruptAddress).value == 0);
    system.runMasterCycles(1);
    REQUIRE(bus.read32(0x1f80826c).value == 0x1d100);
    REQUIRE(bus.read32(0x1f808270).value == 0xf);
    REQUIRE(bus.read32(0x1f808274).value == 0);
    REQUIRE(bus.read32(controlAddress).value == 0x3bd);
    REQUIRE(bus.read32(interruptAddress).value == 1);
    REQUIRE(system.sio2Controller().dmaResultPhase() == SIO2DMAResultPhase::Ready);
    REQUIRE(system.sio2Controller().dmaResultSize() == 144);
    for (std::size_t index = 0; index < 144; ++index)
      REQUIRE(system.sio2Controller().dmaResultByte(index) ==
        (index < replyBytes ? 0xff : 0));
    REQUIRE_THROWS_AS(system.sio2Controller().dmaResultByte(144), std::out_of_range);
    REQUIRE(system.iopInterruptController().status() == sio2IRQ);
    REQUIRE(bus.read8(receiveAddress).status == IOPBusStatus::Unmapped);
    REQUIRE_FALSE(system.iopDMAController().sio2OutputActive());
  }
}

TEST_CASE("SIO2 DMA card probe rejects unselected framing without consuming input",
  "[iop][sio2][sio2-card]")
{
  for (const auto descriptor : {0x00100472u, 0x00140572u})
  for (unsigned invalid = 0; invalid < 12; ++invalid)
  {
    CAPTURE(descriptor);
    CAPTURE(invalid);
    const auto firstWord = descriptor == 0x00140572 ? 0xf381u : 0x1181u;
    NekoSystem system;
    stageCardProbe(system, invalid == 0 ? descriptor & ~3u :
        invalid == 11 ? descriptor ^ 0x40000 : descriptor,
      invalid == 1 ? 0x00001281 : invalid == 2 ? firstWord | 0x01000000 :
        invalid == 9 ? firstWord ^ 1 : invalid == 10 ? firstWord ^ 0xe200 : firstWord,
      invalid == 3 ? 35 : 36, invalid == 4 ? 1 : 0);
    auto &bus = system.iopBus();
    if (invalid == 5)
      REQUIRE(bus.write32(descriptorBase + 4, 1).status == IOPBusStatus::Completed);
    if (invalid == 6)
      REQUIRE(bus.write32(portControlBase + 16, 0).status == IOPBusStatus::Completed);
    if (invalid == 7)
      REQUIRE(bus.write32(portControlBase + 20, 0).status == IOPBusStatus::Completed);
    const auto before = system.saveState();
    REQUIRE(bus.write32(controlAddress, invalid == 8 ? 0x3b1 : 0x3bd).status ==
      IOPBusStatus::Unmapped);
    REQUIRE(system.saveState() == before);
  }
}

TEST_CASE("SIO2 card START preserves staged bytes on bus admission failures",
  "[iop][sio2][sio2-card]")
{
  NekoSystem system;
  stageCardProbe(system);
  auto &bus = system.iopBus();
  const auto before = system.saveState();
  bus.attachInterruptController(nullptr);
  REQUIRE(bus.write32(controlAddress, 0x3bd).status == IOPBusStatus::Unmapped);
  bus.attachInterruptController(&system.iopInterruptController());
  REQUIRE(bus.write8(controlAddress, 0xbd).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.write16(controlAddress, 0x3bd).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.writeDMAC32(controlAddress, 0x3bd).status == IOPBusStatus::Unmapped);
  for (std::uint8_t mask = 1; mask < 0xf; ++mask)
    REQUIRE(bus.writeMasked32(controlAddress, 0x3bd, mask).status == IOPBusStatus::Unmapped);
  REQUIRE(system.saveState() == before);
  REQUIRE(bus.write32(controlAddress, 0x3bd).status == IOPBusStatus::Completed);
}

TEST_CASE("SIO2 card guest START publishes results on the following IOP instruction pulse",
  "[iop][sio2][sio2-card][trace]")
{
  NekoSystem system;
  IOPWord descriptor = 0x00100472;
  IOPWord firstWord = 0x1181;
  SECTION("Probe") {}
  SECTION("Absent f3")
  {
    descriptor = 0x00140572;
    firstWord = 0xf381;
  }
  stageCardProbe(system, descriptor, firstWord);
  installProgram(system, std::array<IOPWord, 5>{{
    0x3c01bf80, 0x34218268, 0x340203bd, 0xac220000, 0
  }});
  system.startTrace();
  system.runMasterCycles(25);
  REQUIRE(system.iopCore().retiredInstructions() == 4);
  REQUIRE(system.sio2Controller().dmaResultPhase() == SIO2DMAResultPhase::Pending);
  REQUIRE(system.iopInterruptController().status() == 0);
  system.runMasterCycles(7);
  REQUIRE(system.sio2Controller().dmaResultPhase() == SIO2DMAResultPhase::Pending);
  system.runMasterCycles(1);
  REQUIRE(system.sio2Controller().dmaResultPhase() == SIO2DMAResultPhase::Ready);
  REQUIRE(system.iopCore().retiredInstructions() == 5);
  const auto interrupts = IOPTraceTest::subsystemEvents(
    system, NekoTraceSubsystem::IOPInterruptController);
  REQUIRE(interrupts.size() == 1);
  REQUIRE(interrupts[0].masterCycle == 33);
  REQUIRE(interrupts[0].value0 == sio2IRQ);
}

TEST_CASE("SIO2 DMA card results reject replacement and remain independent of output arming",
  "[iop][sio2][sio2-card]")
{
  NekoSystem system;
  stageCardProbe(system);
  auto &bus = system.iopBus();
  REQUIRE(bus.write32(0x1f801550, 0x2000).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(0x1f801554, 0x10024).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(0x1f801558, 0x41000200).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(controlAddress, 0x3bd).status == IOPBusStatus::Completed);
  for (const bool ready : {false, true})
  {
    CAPTURE(ready);
    if (ready)
      system.runMasterCycles(1);
    const auto snapshot = inspectStartup(system);
    REQUIRE(snapshot.sio2.resultPhase ==
      (ready ? SIO2DMAResultPhase::Ready : SIO2DMAResultPhase::Pending));
    REQUIRE(snapshot.sio2.resultBytes == (ready ? 144 : 0));
    std::ostringstream json;
    writeStartupSummaryJsonLine(json, snapshot);
    REQUIRE(json.str().find(ready ?
      "\"dma_result_phase\":\"ready\",\"dma_result_bytes\":144,"
      "\"dma_logical_bytes\":4,\"dma_padding_bytes\":140" :
      "\"dma_result_phase\":\"pending\",\"dma_result_bytes\":0,"
      "\"dma_logical_bytes\":0,\"dma_padding_bytes\":0") != std::string::npos);
    REQUIRE(json.str().find(ready ? "\"state\":\"armed_results_ready\"" :
      "\"state\":\"armed_waiting_results\"") != std::string::npos);
    const auto before = system.saveState();
    for (const auto value : {0xcu, 0x3bcu, 0x3bdu, 0xb1u})
      REQUIRE(bus.write32(controlAddress, value).status == IOPBusStatus::Unmapped);
    for (std::size_t index = 0; index < 16; ++index)
      REQUIRE(bus.write32(descriptorBase + static_cast<IOPAddress>(index * 4), 0).status ==
        IOPBusStatus::Unmapped);
    for (std::size_t index = 0; index < 8; ++index)
      REQUIRE(bus.write32(portControlBase + static_cast<IOPAddress>(index * 4), 0).status ==
        IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(transmitAddress, 0x61).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(0x1f801548, 0x01000201).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read8(receiveAddress).status == IOPBusStatus::Unmapped);
    REQUIRE_THROWS_AS(system.sio2Controller().reset(), std::logic_error);
    REQUIRE(system.saveState() == before);
    REQUIRE(bus.write32(0x1f801558, 0x40000200).status == IOPBusStatus::Completed);
    REQUIRE(system.sio2Controller().dmaResultPhase() ==
      (ready ? SIO2DMAResultPhase::Ready : SIO2DMAResultPhase::Pending));
    REQUIRE(bus.write32(0x1f801558, 0x41000200).status == IOPBusStatus::Completed);
  }
  system.runMasterCycles(80);
  REQUIRE(system.sio2Controller().dmaResultSize() == 144);
  REQUIRE(bus.read32(0x1f801550).value == 0x2000);
  REQUIRE(bus.read32(0x1f801554).value == 0x10024);
  REQUIRE(bus.read32(0x1f801558).value == 0x41000200);
  REQUIRE(bus.read32(0x2000).value == 0);
  system.iopBus().reset();
  system.iopCore().reset();
  REQUIRE(system.sio2Controller().dmaResultSize() == 144);
  system.reset();
  REQUIRE(system.sio2Controller().dmaResultPhase() == SIO2DMAResultPhase::Empty);
  REQUIRE(system.sio2Controller().dmaResultSize() == 0);
}

TEST_CASE("SIO2 DMA card completion respects independently retained interrupt cause",
  "[iop][sio2][sio2-card][save-state]")
{
  for (const auto descriptor : {0x00100472u, 0x00140572u})
  for (const bool acknowledge : {false, true})
  {
    CAPTURE(descriptor);
    CAPTURE(acknowledge);
    NekoSystem system;
    stageInfraredTransfer(system);
    REQUIRE(system.iopBus().write32(controlAddress, transferControl).status ==
      IOPBusStatus::Completed);
    for (std::size_t index = 0; index < infraredPacket.size(); ++index)
      REQUIRE(system.iopBus().read8(receiveAddress).value == 0xff);
    system.iopInterruptController().writeStatus(~sio2IRQ);
    stageCardProbe(system, descriptor, descriptor == 0x00140572 ? 0xf381 : 0x1181);
    REQUIRE(system.iopBus().write32(controlAddress, 0x3bd).status ==
      IOPBusStatus::Completed);
    REQUIRE(system.iopBus().read32(0x1f80826c).status == IOPBusStatus::Unmapped);
    NekoSystem restored;
    restored.loadState(system.saveState());
    if (acknowledge)
      REQUIRE(restored.iopBus().write32(interruptAddress, 1).status ==
        IOPBusStatus::Completed);
    restored.runMasterCycles(1);
    REQUIRE(restored.iopInterruptController().status() == (acknowledge ? sio2IRQ : 0));
    REQUIRE(restored.iopBus().read32(interruptAddress).value == 1);
    restored.iopInterruptController().writeStatus(~sio2IRQ);
    restored.runMasterCycles(16);
    REQUIRE(restored.iopInterruptController().status() == 0);
  }
}

TEST_CASE("SIO2 DMA card phases restore traced and plain pulse continuations",
  "[iop][sio2][sio2-card][save-state][trace]")
{
  NekoSystem original;
  stageCardProbe(original);
  original.runMasterCycles(2);
  REQUIRE(original.iopBus().write32(controlAddress, 0x3bd).status ==
    IOPBusStatus::Completed);
  for (const bool ready : {false, true})
  {
    CAPTURE(ready);
    if (ready)
      original.runMasterCycles(7);
    const auto state = original.saveState();
    const auto originalHash = original.iopStateHash();
    NekoSystem restored;
    NekoSystem plain;
    restored.loadState(state);
    plain.loadState(state);
    REQUIRE(restored.saveState() == state);
    REQUIRE(restored.iopStateHash() == originalHash);
    REQUIRE(inspectSaveState(state).find("system.iop.sio2.dmaResultPhase")->scalarValue ==
      (ready ? 2 : 1));
    original.startTrace();
    restored.startTrace();
    original.runMasterCycles(ready ? 8 : 6);
    restored.runMasterCycles(ready ? 8 : 6);
    plain.runMasterCycles(ready ? 8 : 6);
    REQUIRE(restored.sio2Controller().dmaResultPhase() ==
      (ready ? SIO2DMAResultPhase::Ready : SIO2DMAResultPhase::Pending));
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireMachine(plain, original);
    REQUIRE(plain.trace().empty());
    original.stopTrace();
  }
}

TEST_CASE("SIO2 DMA card canonical admission rejects mismatched phases transactionally",
  "[iop][sio2][sio2-card][save-state]")
{
  NekoSystem source;
  stageCardProbe(source);
  REQUIRE(source.iopBus().write32(controlAddress, 0x3bd).status == IOPBusStatus::Completed);
  NekoSystem destination;
  stageInfraredTransfer(destination);
  const auto before = destination.saveState();
  const auto hash = destination.iopStateHash();
  for (const bool ready : {false, true})
  {
    if (ready)
      source.runMasterCycles(1);
    const auto state = source.saveState();
    for (const auto &mutation : std::vector<std::pair<std::string, std::uint64_t>>{
      {"container.version", 105},
      {"system.iop.sio2.dmaResultPhase", 0},
      {"system.iop.sio2.dmaResultPhase", 3},
      {"system.iop.sio2.control", 0x3bc},
      {"system.iop.sio2.transferCompleted", ready ? 0u : 1u},
      {"system.iop.sio2.receiveRemaining", 1},
      {"system.iop.sio2.transmitInput.count", 4},
      {"system.iop.sio2.commandDescriptors[0].value", 0x00100470},
      {"system.iop.sio2.commandDescriptors[1].value", 1},
      {"system.iop.sio2.portControls[4].value", 0},
      {"system.iop.dmaController.sio2Programming[0].channelControl", 0x01000201}})
    {
      CAPTURE(ready);
      CAPTURE(mutation.first);
      if (ready && mutation.first == "system.iop.sio2.dmaResultPhase" &&
          mutation.second == 0)
        continue; // An empty completed result is now the valid fully drained state.
      SaveStateMutation invalid(state);
      invalid.writeScalar(mutation.first, mutation.second);
      invalid.refreshChecksum();
      REQUIRE_THROWS_AS(inspectSaveState(invalid), std::invalid_argument);
      REQUIRE_THROWS_AS(destination.loadState(invalid), std::invalid_argument);
      REQUIRE(destination.iopStateHash() == hash);
    }
    REQUIRE(destination.saveState() == before);
  }
}

TEST_CASE("SIO2 CPU52 returns exactly three absent-card bytes at START",
  "[iop][sio2][sio2-cpu52]")
{
  for (const auto descriptor : {0x000c0342u, 0x000c0343u})
  {
    CAPTURE(descriptor);
    NekoSystem system;
    auto &bus = system.iopBus();
    stageCPUCardRead(system, descriptor);
    for (std::size_t index = 0; index < 8; ++index)
      if (index / 2 != (descriptor & 3))
        REQUIRE(bus.write32(portControlBase + static_cast<IOPAddress>(index * 4),
          0xabcdef01).status == IOPBusStatus::Completed);
    REQUIRE(bus.read32(0x1f80826c).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(controlAddress, 0x3bd).status == IOPBusStatus::Completed);
    REQUIRE(system.sio2Controller().transmitInputPhase() == SIO2IngressPhase::Empty);
    REQUIRE(system.sio2Controller().transmitInputSize() == 0);
    REQUIRE(bus.read32(controlAddress).value == 0x3bd);
    REQUIRE(bus.read32(0x1f80826c).value == 0x1d100);
    REQUIRE(bus.read32(0x1f808270).value == 0xf);
    REQUIRE(bus.read32(0x1f808274).value == 0);
    REQUIRE(bus.read32(interruptAddress).value == 1);
    REQUIRE(system.iopInterruptController().status() == sio2IRQ);
    for (std::size_t index = 0; index < 3; ++index)
    {
      const auto before = system.iopStateHash();
      REQUIRE(bus.write32(controlAddress, 0x3bd).status == IOPBusStatus::Unmapped);
      REQUIRE(system.iopStateHash() == before);
      const auto reply = bus.read8(receiveAddress);
      REQUIRE(reply.status == IOPBusStatus::Completed);
      REQUIRE(reply.value == 0xff);
    }
    REQUIRE(bus.read8(receiveAddress).status == IOPBusStatus::Unmapped);
    const auto drained = system.iopStateHash();
    REQUIRE(bus.write32(controlAddress, 0x3bd).status == IOPBusStatus::Completed);
    REQUIRE(system.iopStateHash() == drained);
    REQUIRE(system.sio2Controller().dmaResultPhase() == SIO2DMAResultPhase::Empty);
    REQUIRE(system.sio2Controller().dmaResultSize() == 0);
    system.iopInterruptController().writeStatus(~sio2IRQ);
    system.runMasterCycles(17);
    REQUIRE(system.iopInterruptController().status() == 0);
    REQUIRE(bus.read32(interruptAddress).value == 1);
  }
}

TEST_CASE("SIO2 CPU52 rejects mismatched and overlapping profiles atomically",
  "[iop][sio2][sio2-cpu52]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  stageCPUCardRead(system);
  IOPWord control = 0x3bd;
  SECTION("Selected first control differs")
  {
    REQUIRE(bus.write32(portControlBase + 16, 0xff020405).status ==
      IOPBusStatus::Completed);
  }
  SECTION("Selected second control differs")
  {
    REQUIRE(bus.write32(portControlBase + 20, 0x5ffff).status ==
      IOPBusStatus::Completed);
  }
  SECTION("Trailing descriptor")
  {
    REQUIRE(bus.write32(descriptorBase + 60, 1).status == IOPBusStatus::Completed);
  }
  SECTION("Different descriptor")
  {
    stageCPUCardRead(system, 0x000c0340);
  }
  SECTION("Wrong endpoint") { stageCPUCardRead(system, 0x000c0342, {{1, 0x52, 0}}); }
  SECTION("Wrong command") { stageCPUCardRead(system, 0x000c0342, {{0x81, 0x53, 0}}); }
  SECTION("Nonzero final byte") { stageCPUCardRead(system, 0x000c0342, {{0x81, 0x52, 1}}); }
  SECTION("Short input")
  {
    REQUIRE(bus.write32(descriptorBase, 0x000c0342).status == IOPBusStatus::Completed);
    REQUIRE(bus.write8(transmitAddress, 0x81).status == IOPBusStatus::Completed);
    REQUIRE(bus.write8(transmitAddress, 0x52).status == IOPBusStatus::Completed);
  }
  SECTION("Excess input")
  {
    REQUIRE(bus.write8(transmitAddress, 0).status == IOPBusStatus::Completed);
  }
  SECTION("Wrong control") { control = 0xb1; }
  SECTION("Unread CPU result")
  {
    REQUIRE(bus.write32(controlAddress, control).status == IOPBusStatus::Completed);
    stageCPUCardRead(system);
  }
  SECTION("DMA ingress with CPU programming")
  {
    REQUIRE(bus.write32(descriptorBase, 0).status == IOPBusStatus::Completed);
    stageCardProbe(system, 0x000c0342, 0x5281);
    REQUIRE(bus.write32(portControlBase + 16, 0xffc00505).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.write32(portControlBase + 20, 0x201f4).status ==
      IOPBusStatus::Completed);
  }
  const auto before = system.iopStateHash();
  const auto irq = system.iopInterruptController().status();
  REQUIRE(bus.write32(controlAddress, control).status == IOPBusStatus::Unmapped);
  REQUIRE(system.iopStateHash() == before);
  REQUIRE(system.iopInterruptController().status() == irq);
}

TEST_CASE("SIO2 CPU52 completion independently gates and rearms its interrupt cause",
  "[iop][sio2][sio2-cpu52]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  for (std::size_t transfer = 0; transfer < 3; ++transfer)
  {
    CAPTURE(transfer);
    stageCPUCardRead(system);
    system.iopInterruptController().writeStatus(~sio2IRQ);
    if (transfer == 2)
      REQUIRE(bus.write32(interruptAddress, 1).status == IOPBusStatus::Completed);
    REQUIRE(bus.write32(controlAddress, 0x3bd).status == IOPBusStatus::Completed);
    REQUIRE(system.iopInterruptController().status() == (transfer == 1 ? 0 : sio2IRQ));
    REQUIRE(bus.read32(interruptAddress).value == 1);
    for (std::size_t byte = 0; byte < 3; ++byte)
      REQUIRE(bus.read8(receiveAddress).status == IOPBusStatus::Completed);
  }
}

TEST_CASE("SIO2 CPU52 leaves armed output DMA waiting through CPU receive drain",
  "[iop][sio2][sio2-cpu52]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  stageCPUCardRead(system);
  REQUIRE(bus.write32(0x2000, 0x12345678).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(0x1f801550, 0x2000).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(0x1f801554, 0x10024).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(0x1f801558, 0x41000200).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(0x1f801570, 8 << 20).status == IOPBusStatus::Completed);
  const auto dmaIRQ = system.iopDMAController().interruptStatus(IOPDMABank::Secondary);
  REQUIRE(bus.write32(controlAddress, 0x3bd).status == IOPBusStatus::Completed);
  for (std::size_t byte = 0; byte < 4; ++byte)
  {
    REQUIRE(system.iopDMAController().clockSIO2Output(
      bus, system.sio2Controller()).outcome == IOPDMABeatOutcome::Wait);
    REQUIRE(system.sio2Controller().dmaResultSize() == 0);
    REQUIRE(bus.read8(receiveAddress).status ==
      (byte == 3 ? IOPBusStatus::Unmapped : IOPBusStatus::Completed));
    REQUIRE(bus.read32(0x1f801550).value == 0x2000);
    REQUIRE(bus.read32(0x1f801554).value == 0x10024);
    REQUIRE(bus.read32(0x1f801558).value == 0x41000200);
    REQUIRE(bus.read32(0x2000).value == 0x12345678);
    REQUIRE(system.iopDMAController().interruptStatus(IOPDMABank::Secondary) == dmaIRQ);
    REQUIRE(system.iopInterruptController().status() == sio2IRQ);
  }
}

TEST_CASE("SIO2 CPU52 guest START retires synchronously with exact IRQ timing",
  "[iop][sio2][sio2-cpu52][trace]")
{
  NekoSystem system;
  stageCPUCardRead(system);
  bool accepted = true;
  SECTION("Accepted request") {}
  SECTION("Rejected request")
  {
    stageCPUCardRead(system, 0x000c0342, {{0x81, 0x52, 1}});
    accepted = false;
  }
  installProgram(system, std::array<IOPWord, 5>{{
    0x3c01bf80, 0x34218268, 0x340203bd, 0xac220000, 0
  }});
  system.startTrace();
  system.runMasterCycles(25);
  REQUIRE(system.iopCore().retiredInstructions() == (accepted ? 4 : 3));
  REQUIRE(system.iopCore().programCounter() == (accepted ? 0xa0000010 : 0xa000000c));
  REQUIRE(system.sio2Controller().transmitInputSize() == (accepted ? 0 : 3));
  const auto interrupts = IOPTraceTest::subsystemEvents(
    system, NekoTraceSubsystem::IOPInterruptController);
  REQUIRE(interrupts.size() == (accepted ? 1 : 0));
  if (accepted)
  {
    REQUIRE(interrupts[0].masterCycle == 25);
    REQUIRE(interrupts[0].value0 == sio2IRQ);
  }
  namespace Memory = NekoIOPTraceMemory;
  IOPTraceTest::requireEvents(controlEvents(system, 0xbf808268), {{
    25, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
    0xbf808268, 0x3bd, 0, nekoPackIOPMemoryTraceMetadata({
      controlAddress, 4, Memory::AccessKind::DataStore,
      accepted ? Memory::Outcome::Completed : Memory::Outcome::Unmapped,
      Memory::CacheRoute::Uncached, true, 0xf})
  }});
}

TEST_CASE("SIO2 CPU52 restores staged partial acknowledged and reprogrammed continuations",
  "[iop][sio2][sio2-cpu52][save-state][trace]")
{
  for (const auto checkpointCycle : {0u, 25u, 33u, 49u})
  {
    CAPTURE(checkpointCycle);
    NekoSystem original;
    stageCPUCardRead(original);
    installProgram(original, std::array<IOPWord, 8>{{
      0x3c01bf80, 0x34218268, 0x340203bd, 0xac220000,
      0x9023fffc, 0x9024fffc, 0x9025fffc, 0
    }});
    original.runMasterCycles(checkpointCycle);
    if (checkpointCycle == 33)
    {
      REQUIRE(original.iopBus().write32(descriptorBase, 0).status ==
        IOPBusStatus::Completed);
      REQUIRE(original.iopBus().write32(portControlBase + 16, 0).status ==
        IOPBusStatus::Completed);
      REQUIRE(original.iopBus().write8(transmitAddress, 0x61).status ==
        IOPBusStatus::Completed);
      REQUIRE(original.iopBus().write32(interruptAddress, 1).status ==
        IOPBusStatus::Completed);
      original.iopInterruptController().writeStatus(~sio2IRQ);
    }
    const auto state = original.saveState();
    NekoSystem restored;
    NekoSystem plain;
    restored.loadState(state);
    plain.loadState(state);
    REQUIRE(restored.saveState() == state);
    original.startTrace();
    restored.startTrace();
    for (auto *system : {&original, &restored, &plain})
    {
      system->runMasterCycles(57 - checkpointCycle);
      REQUIRE(system->iopCore().generalRegister(3) == 0xff);
      REQUIRE(system->iopCore().generalRegister(4) == 0xff);
      REQUIRE(system->iopCore().generalRegister(5) == 0xff);
      REQUIRE(system->iopBus().read8(receiveAddress).status == IOPBusStatus::Unmapped);
    }
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireMachine(plain, original);
    REQUIRE(plain.trace().empty());
  }
}

TEST_CASE("SIO2 CPU52 canonical admission rejects inconsistent receive states atomically",
  "[iop][sio2][sio2-cpu52][save-state]")
{
  NekoSystem source;
  stageCPUCardRead(source);
  REQUIRE(source.iopBus().write32(controlAddress, 0x3bd).status == IOPBusStatus::Completed);
  const auto state = source.saveState();
  NekoSystem destination;
  stageInfraredTransfer(destination);
  const auto before = destination.iopStateHash();
  for (const auto &mutation : std::vector<std::pair<std::string, std::uint64_t>>{
    {"system.iop.sio2.receiveRemaining", 8},
    {"system.iop.sio2.receiveRemaining", 7},
    {"system.iop.sio2.transferCompleted", 0},
    {"system.iop.sio2.dmaResultPhase", 1},
    {"system.iop.sio2.dmaResultPhase", 2},
    {"system.iop.sio2.transmitInput.phase", 3}})
  {
    CAPTURE(mutation.first);
    CAPTURE(mutation.second);
    SaveStateMutation invalid(state);
    invalid.writeScalar(mutation.first, mutation.second);
    invalid.refreshChecksum();
    REQUIRE_THROWS_AS(inspectSaveState(invalid), std::invalid_argument);
    REQUIRE_THROWS_AS(destination.loadState(invalid), std::invalid_argument);
    REQUIRE(destination.iopStateHash() == before);
  }
}

TEST_CASE("SIO2 CPU52 preserves unread replies on passive programming and clears them on reset",
  "[iop][sio2][sio2-cpu52]")
{
  NekoSystem system;
  stageCPUCardRead(system);
  auto &bus = system.iopBus();
  REQUIRE(bus.write32(controlAddress, 0x3bd).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(controlAddress, 0xc).status == IOPBusStatus::Completed);
  REQUIRE(bus.read8(receiveAddress).value == 0xff);
  REQUIRE(bus.write32(descriptorBase, 0).status == IOPBusStatus::Completed);
  REQUIRE(bus.write8(transmitAddress, 0x61).status == IOPBusStatus::Completed);
  system.sio2Controller().reset();
  REQUIRE(bus.read8(receiveAddress).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.read32(0x1f80826c).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.read32(controlAddress).value == 0);
  REQUIRE(bus.read32(interruptAddress).value == 0);
  REQUIRE(system.sio2Controller().transmitInputSize() == 0);
  REQUIRE(system.iopInterruptController().status() == sio2IRQ);
}

TEST_CASE("SIO2 CPU2112 returns exactly six absent-multitap bytes at START",
  "[iop][sio2][sio2-cpu2112]")
{
  for (const auto descriptor : {0x00180642u, 0x00180643u})
  {
    CAPTURE(descriptor);
    NekoSystem system;
    auto &bus = system.iopBus();
    stageCPUMultitapSupport(system, descriptor);
    for (std::size_t index = 0; index < 8; ++index)
      if (index / 2 != (descriptor & 3))
        REQUIRE(bus.write32(portControlBase + static_cast<IOPAddress>(index * 4),
          0xabcdef01).status == IOPBusStatus::Completed);
    REQUIRE(bus.read32(0x1f80826c).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(controlAddress, 0x3bd).status == IOPBusStatus::Completed);
    REQUIRE(system.sio2Controller().transmitInputPhase() == SIO2IngressPhase::Empty);
    REQUIRE(system.sio2Controller().transmitInputSize() == 0);
    REQUIRE(bus.read32(controlAddress).value == 0x3bd);
    REQUIRE(bus.read32(0x1f80826c).value == 0x1d100);
    REQUIRE(bus.read32(0x1f808270).value == 0xf);
    REQUIRE(bus.read32(0x1f808274).value == 0);
    REQUIRE(bus.read32(interruptAddress).value == 1);
    for (std::size_t byte = 0; byte < 6; ++byte)
    {
      requireRejectedCPUStart(system);
      const auto reply = bus.read8(receiveAddress);
      REQUIRE(reply.status == IOPBusStatus::Completed);
      REQUIRE(reply.value == 0xff);
    }
    REQUIRE(bus.read8(receiveAddress).status == IOPBusStatus::Unmapped);
    system.iopInterruptController().writeStatus(~sio2IRQ);
    const auto drained = system.iopStateHash();
    REQUIRE(bus.write32(controlAddress, 0x3bd).status == IOPBusStatus::Completed);
    REQUIRE(system.iopStateHash() == drained);
    REQUIRE(bus.write32(portControlBase + (descriptor & 3) * 8 + 4, 0).status ==
      IOPBusStatus::Completed);
    requireRejectedCPUStart(system);
    system.iopInterruptController().writeStatus(~sio2IRQ);
    system.runMasterCycles(17);
    REQUIRE(system.iopInterruptController().status() == 0);
    REQUIRE(bus.read32(interruptAddress).value == 1);
  }
}

TEST_CASE("SIO2 CPU2112 rejects mismatched and overlapping profiles atomically",
  "[iop][sio2][sio2-cpu2112]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  stageCPUMultitapSupport(system);
  IOPWord control = 0x3bd;
  SECTION("Selected first control differs")
  {
    REQUIRE(bus.write32(portControlBase + 16, 0xff020405).status ==
      IOPBusStatus::Completed);
  }
  SECTION("Selected second control differs")
  {
    REQUIRE(bus.write32(portControlBase + 20, 0x201f4).status ==
      IOPBusStatus::Completed);
  }
  SECTION("Any trailing descriptor rejects")
  {
    for (std::size_t index = 1; index < 16; ++index)
    {
      REQUIRE(bus.write32(descriptorBase + static_cast<IOPAddress>(index * 4), 1).
        status == IOPBusStatus::Completed);
      requireRejectedCPUStart(system);
      REQUIRE(bus.write32(descriptorBase + static_cast<IOPAddress>(index * 4), 0).
        status == IOPBusStatus::Completed);
    }
    return;
  }
  SECTION("Other descriptors reject")
  {
    for (auto descriptor : {0x00180640u, 0x00180641u, 0x00180646u, 0x00140642u,
      0x001c0642u, 0x00180742u})
    {
      CAPTURE(descriptor);
      stageCPUMultitapSupport(system, descriptor);
      requireRejectedCPUStart(system);
    }
    return;
  }
  SECTION("Wrong endpoint") { stageCPUMultitapSupport(system, 0x00180642, {{0x81, 0x12}}); }
  SECTION("Other commands reject")
  {
    for (std::uint8_t command : {0x13, 0x21, 0x22, 0x11, 0x52, 0xf3, 0})
    {
      CAPTURE(command);
      stageCPUMultitapSupport(system, 0x00180642, {{0x21, command}});
      requireRejectedCPUStart(system);
    }
    return;
  }
  SECTION("Nonzero trailing bytes reject")
  {
    for (std::size_t index = 2; index < 6; ++index)
    {
      std::array<std::uint8_t, 6> packet = {{0x21, 0x12}};
      packet[index] = 1;
      stageCPUMultitapSupport(system, 0x00180642, packet);
      requireRejectedCPUStart(system);
    }
    return;
  }
  SECTION("Every short input rejects")
  {
    const std::array<std::uint8_t, 6> packet = {{0x21, 0x12}};
    REQUIRE(bus.write32(descriptorBase, 0x00180642).status == IOPBusStatus::Completed);
    for (auto byte : packet)
    {
      requireRejectedCPUStart(system);
      REQUIRE(bus.write8(transmitAddress, byte).status == IOPBusStatus::Completed);
    }
    return;
  }
  SECTION("Excess input")
  {
    REQUIRE(bus.write8(transmitAddress, 0).status == IOPBusStatus::Completed);
  }
  SECTION("Wrong control") { control = 0xb1; }
  SECTION("Unread CPU result")
  {
    REQUIRE(bus.write32(controlAddress, control).status == IOPBusStatus::Completed);
    stageCPUMultitapSupport(system);
  }
  SECTION("DMA ingress is not CPU ingress")
  {
    for (const auto words : {1u, 36u})
    {
      NekoSystem dma;
      stageCardProbe(dma, 0x00180642, 0x1221, words);
      REQUIRE(dma.iopBus().write32(portControlBase + 16, 0xff020505).status ==
        IOPBusStatus::Completed);
      REQUIRE(dma.iopBus().write32(portControlBase + 20, 0x30064).status ==
        IOPBusStatus::Completed);
      requireRejectedCPUStart(dma);
    }
    return;
  }
  requireRejectedCPUStart(system, control);
}

TEST_CASE("SIO2 CPU2112 completion independently gates and rearms its interrupt cause",
  "[iop][sio2][sio2-cpu2112]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  for (std::size_t transfer = 0; transfer < 3; ++transfer)
  {
    CAPTURE(transfer);
    stageCPUMultitapSupport(system);
    system.iopInterruptController().writeStatus(~sio2IRQ);
    if (transfer == 2)
      REQUIRE(bus.write32(interruptAddress, 1).status == IOPBusStatus::Completed);
    REQUIRE(bus.write32(controlAddress, 0x3bd).status == IOPBusStatus::Completed);
    REQUIRE(system.iopInterruptController().status() == (transfer == 1 ? 0 : sio2IRQ));
    REQUIRE(bus.read32(interruptAddress).value == 1);
    for (std::size_t byte = 0; byte < 6; ++byte)
      REQUIRE(bus.read8(receiveAddress).status == IOPBusStatus::Completed);
  }
  REQUIRE(bus.write32(interruptAddress, 1).status == IOPBusStatus::Completed);
  system.iopInterruptController().writeStatus(~sio2IRQ);
  const auto acknowledged = system.iopStateHash();
  REQUIRE(bus.write32(controlAddress, 0x3bd).status == IOPBusStatus::Completed);
  REQUIRE(system.iopStateHash() == acknowledged);
}

TEST_CASE("SIO2 CPU2112 keeps CPU replies and DMA results independent",
  "[iop][sio2][sio2-cpu2112]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  SECTION("Armed output DMA waits through CPU completion and drain")
  {
    stageCPUMultitapSupport(system);
    REQUIRE(bus.write32(0x2000, 0x12345678).status == IOPBusStatus::Completed);
    REQUIRE(bus.write32(0x1f801550, 0x2000).status == IOPBusStatus::Completed);
    REQUIRE(bus.write32(0x1f801554, 0x10024).status == IOPBusStatus::Completed);
    REQUIRE(bus.write32(0x1f801558, 0x41000200).status == IOPBusStatus::Completed);
    REQUIRE(bus.write32(0x1f801570, 8 << 20).status == IOPBusStatus::Completed);
    const auto dmaIRQ = system.iopDMAController().interruptStatus(IOPDMABank::Secondary);
    REQUIRE(bus.write32(controlAddress, 0x3bd).status == IOPBusStatus::Completed);
    for (std::size_t byte = 0; byte < 7; ++byte)
    {
      REQUIRE(system.iopDMAController().clockSIO2Output(
        bus, system.sio2Controller()).outcome == IOPDMABeatOutcome::Wait);
      REQUIRE(system.sio2Controller().dmaResultPhase() == SIO2DMAResultPhase::Empty);
      REQUIRE(system.sio2Controller().dmaResultSize() == 0);
      REQUIRE(system.iopDMAController().sio2OutputActive());
      REQUIRE(system.iopDMAController().sio2OutputRemainingWords() == 36);
      REQUIRE(bus.read8(receiveAddress).status ==
        (byte == 6 ? IOPBusStatus::Unmapped : IOPBusStatus::Completed));
      REQUIRE(bus.read32(0x1f801550).value == 0x2000);
      REQUIRE(bus.read32(0x1f801554).value == 0x10024);
      REQUIRE(bus.read32(0x1f801558).value == 0x41000200);
      REQUIRE(bus.read32(0x2000).value == 0x12345678);
      REQUIRE(system.iopDMAController().interruptStatus(IOPDMABank::Secondary) == dmaIRQ);
      REQUIRE(system.iopInterruptController().status() == sio2IRQ);
    }
  }
  SECTION("Pending and ready DMA results block CPU ingress and START")
  {
    stageCardProbe(system);
    REQUIRE(bus.write32(controlAddress, 0x3bd).status == IOPBusStatus::Completed);
    for (const bool ready : {false, true})
    {
      if (ready)
        system.runMasterCycles(1);
      const auto before = system.iopStateHash();
      REQUIRE(bus.write8(transmitAddress, 0x21).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write32(descriptorBase, 0x00180642).status == IOPBusStatus::Unmapped);
      REQUIRE(system.iopStateHash() == before);
      requireRejectedCPUStart(system);
    }
  }
}

TEST_CASE("SIO2 CPU2112 guest START retires synchronously with exact IRQ timing",
  "[iop][sio2][sio2-cpu2112][trace]")
{
  NekoSystem system;
  stageCPUMultitapSupport(system);
  bool accepted = true;
  SECTION("Accepted request") {}
  SECTION("Rejected request")
  {
    stageCPUMultitapSupport(system, 0x00180642, {{0x21, 0x12, 0, 0, 0, 1}});
    accepted = false;
  }
  installProgram(system, std::array<IOPWord, 5>{{
    0x3c01bf80, 0x34218268, 0x340203bd, 0xac220000, 0
  }});
  system.startTrace();
  system.runMasterCycles(25);
  REQUIRE(system.iopCore().retiredInstructions() == (accepted ? 4 : 3));
  REQUIRE(system.iopCore().programCounter() == (accepted ? 0xa0000010 : 0xa000000c));
  REQUIRE(system.sio2Controller().transmitInputSize() == (accepted ? 0 : 6));
  const auto interrupts = IOPTraceTest::subsystemEvents(
    system, NekoTraceSubsystem::IOPInterruptController);
  REQUIRE(interrupts.size() == (accepted ? 1 : 0));
  if (accepted)
  {
    REQUIRE(interrupts[0].masterCycle == 25);
    REQUIRE(interrupts[0].value0 == sio2IRQ);
  }
  namespace Memory = NekoIOPTraceMemory;
  IOPTraceTest::requireEvents(controlEvents(system, 0xbf808268), {{
    25, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
    0xbf808268, 0x3bd, 0, nekoPackIOPMemoryTraceMetadata({
      controlAddress, 4, Memory::AccessKind::DataStore,
      accepted ? Memory::Outcome::Completed : Memory::Outcome::Unmapped,
      Memory::CacheRoute::Uncached, true, 0xf})
  }});
}

TEST_CASE("SIO2 CPU2112 restores staged partial acknowledged and reprogrammed continuations",
  "[iop][sio2][sio2-cpu2112][save-state][trace]")
{
  for (const auto checkpointCycle : {0u, 25u, 33u, 41u, 73u})
  {
    CAPTURE(checkpointCycle);
    NekoSystem original;
    stageCPUMultitapSupport(original);
    installProgram(original, std::array<IOPWord, 11>{{
      0x3c01bf80, 0x34218268, 0x340203bd, 0xac220000,
      0x9023fffc, 0x9024fffc, 0x9025fffc, 0x9026fffc, 0x9027fffc, 0x9028fffc, 0
    }});
    original.runMasterCycles(checkpointCycle);
    if (checkpointCycle == 33)
    {
      REQUIRE(original.iopBus().write32(descriptorBase, 0).status ==
        IOPBusStatus::Completed);
      REQUIRE(original.iopBus().write32(portControlBase + 16, 0).status ==
        IOPBusStatus::Completed);
      REQUIRE(original.iopBus().write8(transmitAddress, 0x61).status ==
        IOPBusStatus::Completed);
      REQUIRE(original.iopBus().write32(interruptAddress, 1).status ==
        IOPBusStatus::Completed);
      original.iopInterruptController().writeStatus(~sio2IRQ);
    }
    const auto state = original.saveState();
    NekoSystem restored;
    NekoSystem plain;
    restored.loadState(state);
    plain.loadState(state);
    REQUIRE(restored.saveState() == state);
    original.startTrace();
    restored.startTrace();
    for (auto *system : {&original, &restored, &plain})
    {
      system->runMasterCycles(81 - checkpointCycle);
      for (std::size_t reg = 3; reg <= 8; ++reg)
        REQUIRE(system->iopCore().generalRegister(reg) == 0xff);
      REQUIRE(system->iopBus().read8(receiveAddress).status == IOPBusStatus::Unmapped);
    }
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireMachine(plain, original);
    REQUIRE(plain.trace().empty());
  }
}

TEST_CASE("SIO2 CPU2112 canonical admission rejects inconsistent receive states atomically",
  "[iop][sio2][sio2-cpu2112][save-state]")
{
  NekoSystem source;
  stageCPUMultitapSupport(source);
  REQUIRE(source.iopBus().write32(controlAddress, 0x3bd).status == IOPBusStatus::Completed);
  const auto state = source.saveState();
  NekoSystem destination;
  stageInfraredTransfer(destination);
  const auto before = destination.iopStateHash();
  for (const auto &mutation : std::vector<std::pair<std::string, std::uint64_t>>{
    {"system.iop.sio2.receiveRemaining", 7},
    {"system.iop.sio2.receiveRemaining", 8},
    {"system.iop.sio2.transferCompleted", 0},
    {"system.iop.sio2.dmaResultPhase", 1},
    {"system.iop.sio2.dmaResultPhase", 2},
    {"system.iop.sio2.transmitInput.phase", 3}})
  {
    CAPTURE(mutation.first);
    CAPTURE(mutation.second);
    SaveStateMutation invalid(state);
    invalid.writeScalar(mutation.first, mutation.second);
    invalid.refreshChecksum();
    REQUIRE_THROWS_AS(inspectSaveState(invalid), std::invalid_argument);
    REQUIRE_THROWS_AS(destination.loadState(invalid), std::invalid_argument);
    REQUIRE(destination.iopStateHash() == before);
  }
}

TEST_CASE("SIO2 CPU2112 preserves unread replies on passive programming and clears them on reset",
  "[iop][sio2][sio2-cpu2112]")
{
  NekoSystem system;
  stageCPUMultitapSupport(system);
  auto &bus = system.iopBus();
  REQUIRE(bus.write32(controlAddress, 0x3bd).status == IOPBusStatus::Completed);
  for (auto control : {0xcu, 0x3bcu})
  {
    REQUIRE(bus.write32(controlAddress, control).status == IOPBusStatus::Completed);
    REQUIRE(bus.read8(receiveAddress).value == 0xff);
    REQUIRE(bus.read32(interruptAddress).value == 1);
  }
  REQUIRE(bus.write32(descriptorBase, 0).status == IOPBusStatus::Completed);
  REQUIRE(bus.write8(transmitAddress, 0x61).status == IOPBusStatus::Completed);
  system.sio2Controller().reset();
  REQUIRE(bus.read8(receiveAddress).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.read32(0x1f80826c).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.read32(controlAddress).value == 0);
  REQUIRE(bus.read32(interruptAddress).value == 0);
  REQUIRE(system.sio2Controller().transmitInputSize() == 0);
  REQUIRE(system.iopInterruptController().status() == sio2IRQ);
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
