#include <cstdint>
#include <stdexcept>

#include "catch.hpp"
#include "neko_system.hpp"

namespace
{
  constexpr std::uint32_t timerBase(unsigned index)
  {
    return 0x10000000u + index * 0x800u;
  }
}

TEST_CASE("EE timers expose four strict word register banks",
  "[ee][ee-timer][bus]")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();
  for (unsigned index = 0; index < 4; ++index)
  {
    CAPTURE(index);
    const std::uint32_t base = timerBase(index);
    std::uint32_t value = UINT32_MAX;
    for (std::uint32_t offset : {0u, 0x10u, 0x20u})
    {
      REQUIRE(bus.readData32(base + offset, &value));
      REQUIRE(value == 0);
    }

    REQUIRE(bus.writeData32(base, 0xabcdef12));
    REQUIRE(bus.readData32(base, &value));
    REQUIRE(value == 0xef12);
    REQUIRE(bus.writeData32(base + 0x20, UINT32_MAX));
    REQUIRE(bus.readData32(base + 0x20, &value));
    REQUIRE(value == 0xffff);
    REQUIRE(bus.writeData32(base + 0x10, UINT32_MAX));
    REQUIRE(bus.readData32(base + 0x10, &value));
    REQUIRE(value == 0x3ff);
    if (index < 2)
    {
      REQUIRE(bus.writeData32(base + 0x30, 0x12345));
      REQUIRE(bus.readData32(base + 0x30, &value));
      REQUIRE(value == 0x2345);
    }
    else
    {
      REQUIRE_FALSE(bus.writeData32(base + 0x30, 0));
      REQUIRE_FALSE(bus.readData32(base + 0x30, &value));
    }
    for (std::uint32_t offset : {4u, 8u, 0x14u, 0x24u, 0x40u, 0x7fcu})
    {
      REQUIRE_FALSE(bus.writeData32(base + offset, 0));
      value = UINT32_MAX;
      REQUIRE_FALSE(bus.readData32(base + offset, &value));
      REQUIRE(value == UINT32_MAX);
    }
    std::uint8_t byte = 0xa5;
    std::uint16_t half = 0xa5a5;
    std::uint64_t wide = UINT64_MAX;
    EEQuadword quad{UINT64_MAX, UINT64_MAX};
    REQUIRE_FALSE(bus.readData8(base, &byte));
    REQUIRE_FALSE(bus.readData16(base, &half));
    REQUIRE_FALSE(bus.readData64(base, &wide));
    REQUIRE_FALSE(bus.readData128(base, &quad));
    REQUIRE(byte == 0xa5);
    REQUIRE(half == 0xa5a5);
    REQUIRE(wide == UINT64_MAX);
    REQUIRE_FALSE(bus.writeData8(base, 0));
    REQUIRE_FALSE(bus.writeData16(base, 0));
    REQUIRE_FALSE(bus.writeData64(base, 0));
    REQUIRE_FALSE(bus.writeData128(base, quad));
    REQUIRE_FALSE(bus.readInstruction32(base, &value));
    REQUIRE_FALSE(bus.readDMAC128(base, &quad));
    REQUIRE_FALSE(bus.writeDMAC128(base, quad));
    REQUIRE_FALSE(bus.isCacheFillRange(base, 4));
    REQUIRE_FALSE(bus.readData32(base | 0x80000000u, &value));
    REQUIRE_FALSE(bus.writeData32(base | 0xa0000000u, 0));
    REQUIRE(bus.wordMergeStorePolicy(base) == EEWordMergeStorePolicy::FullWordOnly);
    REQUIRE(bus.readData32(base, &value));
    REQUIRE(value == 0xef12);
  }
}

TEST_CASE("EE timers derive all bus divisors while both CPUs are halted",
  "[ee][ee-timer][clock]")
{
  NekoSystem system;
  for (unsigned index = 0; index < 4; ++index)
  {
    REQUIRE(system.eeBus().writeData32(timerBase(index) + 0x10,
      0x80 | (index == 3 ? 0 : index)));
  }
  REQUIRE(system.runMasterCycles(1024) == 1024);
  const std::uint32_t expected[] = {512, 32, 2, 512};
  for (unsigned index = 0; index < 4; ++index)
  {
    CAPTURE(index);
    REQUIRE(system.eeBus().read32(timerBase(index)) == expected[index]);
    REQUIRE(system.eeBus().writeData32(timerBase(index) + 0x10, 0));
  }
  system.runMasterCycles(1024);
  for (unsigned index = 0; index < 4; ++index)
  {
    REQUIRE(system.eeBus().read32(timerBase(index)) == expected[index]);
  }
  REQUIRE(system.eeCore().elapsedCycles() == 0);
  REQUIRE(system.iopCore().elapsedCycles() == 0);
}

TEST_CASE("EE timer register decoding rejects interior and absent registers",
  "[ee][ee-timer]")
{
  EETimers timers;
  for (std::uint32_t address : {0u, 0x0fffffffu, 0x10000001u,
       0x1000000cu, 0x10001030u, 0x10001830u, 0x10002000u, 0xb0000010u})
  {
    CAPTURE(address);
    REQUIRE_FALSE(EETimers::isRegisterAddress(address));
    REQUIRE_THROWS_AS(timers.readRegister(address), std::invalid_argument);
    REQUIRE_THROWS_AS(timers.writeRegister(address, 0), std::invalid_argument);
  }
  NekoSystem system;
  REQUIRE_THROWS_AS(system.eeBus().attachTimers(nullptr), std::invalid_argument);
  REQUIRE_THROWS_AS(system.eeBus().attachTimers(&timers), std::logic_error);
}

TEST_CASE("EE timer mode and count writes preserve global divider alignment",
  "[ee][ee-timer][clock]")
{
  for (unsigned clock = 0; clock < 3; ++clock)
  {
    const std::uint64_t periods[] = {2, 32, 512};
    const auto period = periods[clock];
    EETimers timers;
    timers.writeRegister(timerBase(0) + 0x10, 0x80 | clock);
    REQUIRE(timers.advance(period - 1, {}) == 0);
    REQUIRE(timers.readRegister(timerBase(0)) == 0);
    timers.writeRegister(timerBase(0), 17);
    REQUIRE(timers.advance(period, {}) == 0);
    REQUIRE(timers.readRegister(timerBase(0)) == 18);
    timers.writeRegister(timerBase(0) + 0x10, clock);
    timers.advance(2 * period, {});
    REQUIRE(timers.readRegister(timerBase(0)) == 18);
    timers.writeRegister(timerBase(0) + 0x10, 0x80 | clock);
    timers.advance(3 * period - 1, {});
    REQUIRE(timers.readRegister(timerBase(0)) == 18);
    timers.advance(3 * period, {});
    REQUIRE(timers.readRegister(timerBase(0)) == 19);
  }
}

TEST_CASE("EE timer gates honor both sources and all four modes",
  "[ee][ee-timer][gate]")
{
  for (unsigned index = 0; index < 4; ++index)
  {
    for (bool vertical : {false, true})
    {
      for (unsigned mode = 0; mode < 4; ++mode)
      {
        CAPTURE(index);
        CAPTURE(vertical);
        CAPTURE(mode);
        EETimers timers;
        const auto base = timerBase(index);
        timers.writeRegister(base, 7);
        timers.writeRegister(base + 0x10,
          0x84 | (vertical ? 8 : 0) | (mode << 4));
        timers.advance(0, {});
        REQUIRE(timers.readRegister(base) == 8);
        const EETimerGateLevels high{!vertical, vertical};
        timers.advance(2, high);
        REQUIRE(timers.readRegister(base) ==
          (mode == 0 ? 8 : mode == 2 ? 9 : 0));
        timers.advance(4, high);
        REQUIRE(timers.readRegister(base) ==
          (mode == 0 ? 8 : mode == 2 ? 10 : 1));
        timers.advance(6, {});
        REQUIRE(timers.readRegister(base) ==
          (mode == 0 ? 9 : mode == 1 ? 2 : 0));
      }
    }
  }
}

TEST_CASE("EE timer gate resets dominate pulses without generating events",
  "[ee][ee-timer][gate]")
{
  for (bool enabled : {false, true})
  {
    EETimers timers;
    timers.writeRegister(timerBase(0), 0xffff);
    timers.writeRegister(timerBase(0) + 0x10, 0x314 | (enabled ? 0x80 : 0));
    REQUIRE(timers.advance(0, {true, false}) == 0);
    REQUIRE(timers.readRegister(timerBase(0)) == 0);
    REQUIRE((timers.readRegister(timerBase(0) + 0x10) & 0xc00) == 0);
  }
}

TEST_CASE("EE timers count HBLANK rising edges without replaying disabled edges",
  "[ee][ee-timer][clock]")
{
  EETimers timers;
  timers.advance(0, {true, false});
  for (unsigned index = 0; index < 4; ++index)
  {
    timers.writeRegister(timerBase(index) + 0x10, 0x83);
  }
  timers.advance(1, {true, false});
  timers.advance(2, {});
  timers.advance(3, {true, false});
  timers.advance(4, {true, false});
  timers.advance(5, {});
  timers.advance(6, {true, false});
  for (unsigned index = 0; index < 4; ++index)
  {
    REQUIRE(timers.readRegister(timerBase(index)) == 2);
  }
}

TEST_CASE("EE timer HBLANK clock supports VBLANK gating but stops with HBLANK gate",
  "[ee][ee-timer][gate]")
{
  EETimers timers;
  timers.writeRegister(timerBase(0) + 0x10, 0x8f);
  timers.advance(0, {true, false});
  timers.advance(1, {false, true});
  timers.advance(2, {true, true});
  timers.advance(3, {});
  timers.advance(4, {true, false});
  REQUIRE(timers.readRegister(timerBase(0)) == 2);
  for (unsigned mode = 0; mode < 4; ++mode)
  {
    timers.writeRegister(timerBase(1), 42);
    timers.writeRegister(timerBase(1) + 0x10, 0x387 | (mode << 4));
    REQUIRE(timers.advance(5 + mode * 2, {}) == 0);
    REQUIRE(timers.advance(6 + mode * 2, {true, true}) == 0);
    REQUIRE(timers.readRegister(timerBase(1)) == 42);
  }
}

TEST_CASE("EE timer compare flags rearm only through W1C mode writes",
  "[ee][ee-timer][interrupt]")
{
  for (unsigned index = 0; index < 4; ++index)
  {
    CAPTURE(index);
    EETimers timers;
    const auto base = timerBase(index);
    timers.writeRegister(base + 0x20, 2);
    timers.writeRegister(base + 0x10, 0x1c0);
    REQUIRE(timers.advance(0, {}) == 0);
    REQUIRE(timers.advance(2, {}) == (1u << index));
    REQUIRE(timers.readRegister(base) == 0);
    REQUIRE(timers.readRegister(base + 0x10) == 0x5c0);
    timers.writeRegister(base + 0x10, 0x1c0);
    REQUIRE(timers.advance(4, {}) == 0);
    REQUIRE(timers.advance(6, {}) == 0);
    timers.writeRegister(base + 0x10, 0x5c0);
    REQUIRE(timers.readRegister(base + 0x10) == 0x1c0);
    REQUIRE(timers.advance(8, {}) == 0);
    REQUIRE(timers.advance(10, {}) == (1u << index));
  }
}

TEST_CASE("EE timer overflow and zero compare share a request with distinct flags",
  "[ee][ee-timer][interrupt]")
{
  EETimers timers;
  const auto base = timerBase(0);
  timers.writeRegister(base, 0xffff);
  timers.writeRegister(base + 0x10, 0x380);
  REQUIRE(timers.advance(0, {}) == 1);
  REQUIRE(timers.readRegister(base) == 0);
  REQUIRE(timers.readRegister(base + 0x10) == 0xf80);
  timers.writeRegister(base + 0x10, 0x780);
  REQUIRE(timers.readRegister(base + 0x10) == 0xb80);
  timers.writeRegister(base, 0xffff);
  REQUIRE(timers.advance(2, {}) == 1);
  REQUIRE(timers.readRegister(base + 0x10) == 0xf80);
  timers.writeRegister(base + 0x10, 0xb80);
  REQUIRE(timers.readRegister(base + 0x10) == 0x780);
  timers.writeRegister(base, 0xffff);
  REQUIRE(timers.advance(4, {}) == 1);
  REQUIRE(timers.readRegister(base + 0x10) == 0xf80);
}

TEST_CASE("EE timer compare uses exact increments not register writes or overshoot",
  "[ee][ee-timer]")
{
  EETimers timers;
  const auto base = timerBase(0);
  timers.writeRegister(base, 8);
  timers.writeRegister(base + 0x20, 4);
  timers.writeRegister(base + 0x10, 0x1c0);
  REQUIRE(timers.advance(0, {}) == 0);
  REQUIRE(timers.readRegister(base) == 9);
  timers.writeRegister(base, 4);
  REQUIRE(timers.readRegister(base + 0x10) == 0x1c0);
  REQUIRE(timers.advance(2, {}) == 0);
  REQUIRE(timers.readRegister(base) == 5);
  timers.writeRegister(base + 0x20, 6);
  REQUIRE(timers.readRegister(base + 0x10) == 0x1c0);
  REQUIRE(timers.advance(4, {}) == 1);
  REQUIRE(timers.readRegister(base) == 0);
}

TEST_CASE("EE timer zero return remains active with interrupts disabled",
  "[ee][ee-timer]")
{
  EETimers timers;
  const auto base = timerBase(0);
  timers.writeRegister(base + 0x20, 1);
  timers.writeRegister(base + 0x10, 0xc0);
  REQUIRE(timers.advance(0, {}) == 0);
  REQUIRE(timers.readRegister(base) == 0);
  REQUIRE(timers.readRegister(base + 0x10) == 0xc0);
  timers.writeRegister(base, 0xffff);
  REQUIRE(timers.advance(2, {}) == 0);
  REQUIRE(timers.readRegister(base + 0x10) == 0xc0);
}

TEST_CASE("EE timer HBLANK counting follows configured display edges",
  "[ee][ee-timer][gs]")
{
  NekoSystem system;
  system.gsDisplay().configureTiming({4, 8, {2, 4, 1}});
  for (unsigned index = 0; index < 4; ++index)
  {
    REQUIRE(system.eeBus().writeData32(timerBase(index) + 0x10, 0x83));
  }
  system.runMasterCycles(1);
  REQUIRE(system.eeBus().read32(timerBase(0)) == 0);
  system.runMasterCycles(1);
  REQUIRE(system.gsDisplay().inHorizontalBlank());
  REQUIRE(system.eeBus().read32(timerBase(0)) == 1);
  system.runMasterCycles(13);
  for (unsigned index = 0; index < 4; ++index)
  {
    REQUIRE(system.eeBus().read32(timerBase(index)) == 4);
  }
}

TEST_CASE("EE timer INTC acknowledgements do not rearm retained timer flags",
  "[ee][ee-timer][interrupt]")
{
  NekoSystem system;
  constexpr std::uint32_t mask = 0x1e00;
  REQUIRE(EEInterruptSource::TIMER0 == 9);
  REQUIRE(EEInterruptSource::TIMER1 == 10);
  REQUIRE(EEInterruptSource::TIMER2 == 11);
  REQUIRE(EEInterruptSource::TIMER3 == 12);
  for (unsigned index = 0; index < 4; ++index)
  {
    REQUIRE(system.eeBus().writeData32(timerBase(index) + 0x20, 1));
    REQUIRE(system.eeBus().writeData32(timerBase(index) + 0x10, 0x1c0));
  }
  system.runMasterCycles(1);
  REQUIRE((system.interruptController().status() & mask) == mask);
  REQUIRE_FALSE(system.interruptController().interruptPending());
  system.interruptController().toggleMask(mask);
  REQUIRE(system.interruptController().interruptPending());
  REQUIRE(system.eeBus().writeData32(EEMemoryMap::INTC_STAT, mask));
  system.runMasterCycles(2);
  REQUIRE((system.interruptController().status() & mask) == 0);
  REQUIRE(system.eeBus().writeData32(timerBase(2) + 0x10, 0x5c0));
  system.runMasterCycles(2);
  REQUIRE((system.interruptController().status() & mask) == 0x800);
  REQUIRE(system.eeBus().read32(timerBase(2) + 0x10) == 0x5c0);
}

TEST_CASE("EE timer holds capture every actual SBUS request without clocking",
  "[ee][ee-timer][sif]")
{
  NekoSystem system;
  for (unsigned repeat = 1; repeat <= 3; ++repeat)
  {
    REQUIRE(system.eeBus().writeData32(timerBase(0), repeat * 11));
    REQUIRE(system.eeBus().writeData32(timerBase(1), repeat * 13));
    REQUIRE(system.iopBus().write32(IOPBus::SIF_DOORBELL, 2).status ==
      IOPBusStatus::Completed);
    REQUIRE(system.eeBus().read32(timerBase(0) + 0x30) == repeat * 11);
    REQUIRE(system.eeBus().read32(timerBase(1) + 0x30) == repeat * 13);
    REQUIRE(system.masterClockScheduler().currentCycle() == 0);
  }
  REQUIRE(system.eeBus().writeData32(timerBase(0), 99));
  system.interruptController().setSource(EEInterruptSource::SBUS, true);
  REQUIRE(system.eeBus().read32(timerBase(0) + 0x30) == 33);
  REQUIRE(system.iopBus().write32(IOPBus::SIF_DOORBELL, 0).status ==
    IOPBusStatus::Completed);
  REQUIRE(system.eeBus().read32(timerBase(0) + 0x30) == 33);
}

TEST_CASE("EE timer words reject split and narrow access at every register",
  "[ee][ee-timer][bus]")
{
  NekoSystem system;
  auto &bus = system.eeBus();
  for (unsigned index = 0; index < 4; ++index)
  {
    for (std::uint32_t offset : {0u, 0x10u, 0x20u, 0x30u})
    {
      if (offset == 0x30 && index >= 2)
      {
        continue;
      }
      const auto address = timerBase(index) + offset;
      CAPTURE(address);
      REQUIRE(bus.writeData32(address, 0x1234));
      const auto original = bus.read32(address);
      for (unsigned byte = 0; byte < 4; ++byte)
      {
        std::uint8_t value = 0xa5;
        REQUIRE_FALSE(bus.readData8(address + byte, &value));
        REQUIRE(value == 0xa5);
        REQUIRE_FALSE(bus.writeData8(address + byte, 0));
      }
      for (unsigned half = 0; half < 4; half += 2)
      {
        std::uint16_t value = 0xa5a5;
        REQUIRE_FALSE(bus.readData16(address + half, &value));
        REQUIRE(value == 0xa5a5);
        REQUIRE_FALSE(bus.writeData16(address + half, 0));
      }
      std::uint64_t wide = UINT64_MAX;
      EEQuadword quad{UINT64_MAX, UINT64_MAX};
      REQUIRE_FALSE(bus.readData64(address, &wide));
      REQUIRE(wide == UINT64_MAX);
      REQUIRE_FALSE(bus.writeData64(address, 0));
      REQUIRE_FALSE(bus.readData128(address, &quad));
      REQUIRE_FALSE(bus.writeData128(address, quad));
      REQUIRE(bus.wordMergeStorePolicy(address) == EEWordMergeStorePolicy::FullWordOnly);
      REQUIRE(bus.read32(address) == original);
    }
  }
}

TEST_CASE("EE timer words support LW and SW through both kernel aliases",
  "[ee][ee-timer][memory]")
{
  for (unsigned index = 0; index < 4; ++index)
  {
    for (std::uint32_t alias : {0x80000000u, 0xa0000000u})
    {
      NekoSystem system;
      auto &core = system.eeCore();
      system.eeBus().write32(0, 0xac220000);
      system.eeBus().write32(4, 0x8c230000);
      core.setGeneralRegister(1, {timerBase(index) | alias, 0});
      core.setGeneralRegister(2, {0x1234, UINT64_MAX});
      core.setGeneralRegister(3, {0, UINT64_MAX});
      core.startExecution(0x80000000);
      system.clockMasterCycle();
      REQUIRE(core.pendingException() == EEException::None);
      REQUIRE(system.eeBus().read32(timerBase(index)) == 0x1234);
      system.clockMasterCycle();
      REQUIRE(core.pendingException() == EEException::None);
      REQUIRE(core.generalRegister(3).low == 0x1234);
      REQUIRE(core.generalRegister(3).high == UINT64_MAX);
    }
  }
}

TEST_CASE("EE timer merge stores admit full words and reject partial masks",
  "[ee][ee-timer][memory]")
{
  for (unsigned index = 0; index < 4; ++index)
  {
    for (std::uint32_t opcode : {0xa8220000u, 0xb8220000u})
    {
      for (unsigned offset = 0; offset < 4; ++offset)
      {
        CAPTURE(index);
        CAPTURE(opcode);
        CAPTURE(offset);
        NekoSystem system;
        auto &core = system.eeCore();
        system.eeBus().write32(0, opcode | offset);
        REQUIRE(system.eeBus().writeData32(timerBase(index), 66));
        core.setGeneralRegister(1, {timerBase(index) | 0xa0000000u, 0});
        core.setGeneralRegister(2, {0x1234, UINT64_MAX});
        core.startExecution(0x80000000);
        system.clockMasterCycle();
        const bool full = opcode == 0xa8220000u ? offset == 3 : offset == 0;
        REQUIRE(core.pendingException() ==
          (full ? EEException::None : EEException::DataBusErrorStore));
        REQUIRE(system.eeBus().read32(timerBase(index)) == (full ? 0x1234 : 66));
        REQUIRE(core.generalRegister(2).low == 0x1234);
        REQUIRE(core.generalRegister(2).high == UINT64_MAX);
      }
    }
  }
}

TEST_CASE("EE timer IRQ reaches the CPU at the next instruction boundary",
  "[ee][ee-timer][interrupt]")
{
  NekoSystem system;
  auto &core = system.eeCore();
  core.setCOP0Register(EECOP0Register::Status,
    EECOP0Status::INTERRUPT_ENABLE |
    EECOP0Status::MASTER_INTERRUPT_ENABLE |
    EECOP0Status::INTC_MASK);
  system.eeBus().write32(0, 0);
  system.eeBus().write32(4, 0);
  REQUIRE(system.eeBus().writeData32(timerBase(0) + 0x20, 1));
  REQUIRE(system.eeBus().writeData32(timerBase(0) + 0x10, 0x1c0));
  system.interruptController().toggleMask(EEInterruptSource::mask(EEInterruptSource::TIMER0));
  core.startExecution(0x80000000);
  system.clockMasterCycle();
  REQUIRE(core.pendingException() == EEException::None);
  REQUIRE(core.programCounter() == 0x80000004);
  REQUIRE((core.cop0Register(EECOP0Register::Cause) & EECOP0Cause::INTC_PENDING) != 0);
  system.clockMasterCycle();
  REQUIRE(core.pendingException() == EEException::Interrupt);
  REQUIRE(core.programCounter() == EEExceptionVector::INTERRUPT);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0x80000004);
}
