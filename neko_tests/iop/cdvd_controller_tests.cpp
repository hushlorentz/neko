#include <array>
#include <cstdint>

#include "catch.hpp"
#include "cdvd_controller.hpp"

struct CDVDControllerTestAccess
{
  static void seedClock(CDVDController &controller, std::uint64_t ticks)
  {
    REQUIRE(ticks < controller.RTC_PERIOD_TICKS);
    controller.rtcTicks = ticks;
  }

  static std::uint64_t ticks(const CDVDController &controller)
  {
    return controller.rtcTicks;
  }

  static bool sameState(
    const CDVDController &actual, const CDVDController &expected)
  {
    return actual.rtcTicks == expected.rtcTicks &&
      actual.normalPending == expected.normalPending &&
      actual.deviceCause == expected.deviceCause &&
      actual.lastSubcommand == expected.lastSubcommand &&
      actual.subcommandPhase == expected.subcommandPhase &&
      actual.result == expected.result &&
      actual.resultCursor == expected.resultCursor;
  }

  static void requireEmptyStorage(const CDVDController &controller)
  {
    const std::array<std::uint8_t, 8> empty = {};
    REQUIRE(controller.result == empty);
    REQUIRE(controller.resultCursor == 0);
  }
};

namespace
{
  constexpr std::uint32_t BASE = 0x1f402000;
  constexpr std::uint64_t CENTURY_SECONDS = UINT64_C(3155760000);
  const std::array<std::uint8_t, 8> EPOCH = {{0, 0, 0, 0, 0, 1, 1, 0}};
  const std::array<std::uint8_t, 8> LAST_SECOND =
    {{0, 0x59, 0x59, 0x23, 0, 0x31, 0x12, 0x99}};

  std::uint8_t read(CDVDController &controller, std::uint32_t offset)
  {
    const auto result = controller.readByte(BASE + offset);
    REQUIRE(result.status == CDVDAccessStatus::Accepted);
    return result.value;
  }

  void accept(CDVDController &controller,
    std::uint32_t offset, std::uint8_t opcode)
  {
    REQUIRE(controller.writeByte(BASE + offset, opcode) ==
      CDVDAccessStatus::Accepted);
  }

  std::array<std::uint8_t, 8> drain(CDVDController &controller)
  {
    std::array<std::uint8_t, 8> bytes = {};
    for (auto &byte : bytes)
    {
      REQUIRE(read(controller, 0x17) == 0);
      byte = read(controller, 0x18);
    }
    REQUIRE(read(controller, 0x17) == 0x40);
    CDVDControllerTestAccess::requireEmptyStorage(controller);
    return bytes;
  }

  void requirePending(CDVDController &controller)
  {
    REQUIRE(read(controller, 0x05) == 0x80);
    REQUIRE(read(controller, 0x17) == 0xc0);
    REQUIRE(read(controller, 0x08) == 0);
    REQUIRE(read(controller, 0x18) == 0);
    CDVDControllerTestAccess::requireEmptyStorage(controller);
  }

  CDVDNormalCompletion masterStep(
    CDVDController &controller, std::uint64_t preIncrementCycle)
  {
    controller.advanceMasterTick();
    if (preIncrementCycle % PS2Clock::IOP_CLOCK_PERIOD ==
        PS2Clock::IOP_CLOCK_PHASE)
    {
      const auto normal = controller.completeNormalCommand();
      controller.completeSubcommand();
      return normal;
    }
    return {};
  }
}

TEST_CASE("CDVD owner resets to the exact no-media byte profile",
  "[iop][cdvd-owner]")
{
  CDVDController controller;
  for (const auto offset : {0x04u, 0x06u, 0x08u, 0x0au, 0x0fu, 0x16u, 0x18u})
  {
    CAPTURE(offset);
    REQUIRE(read(controller, offset) == 0);
  }
  REQUIRE(read(controller, 0x05) == 0x40);
  REQUIRE(read(controller, 0x17) == 0x40);
  accept(controller, 0x16, 8);
  REQUIRE(read(controller, 0x16) == 8);
  REQUIRE(read(controller, 0x17) == 0xc0);
  REQUIRE(controller.completeSubcommand());
  REQUIRE(drain(controller) == EPOCH);
  REQUIRE(read(controller, 0x16) == 8);
  REQUIRE_FALSE(controller.completeSubcommand());
  REQUIRE(read(controller, 0x08) == 0);
}

TEST_CASE("CDVD owner rejects unselected addresses and directions atomically",
  "[iop][cdvd-owner]")
{
  for (const unsigned stage : {0u, 1u, 2u})
  {
    CAPTURE(stage);
    CDVDController controller;
    CDVDControllerTestAccess::seedClock(controller, 123456789);
    if (stage != 0)
    {
      accept(controller, 0x04, 0);
      accept(controller, 0x16, 8);
    }
    if (stage == 2)
    {
      REQUIRE(controller.completeNormalCommand().completed);
      REQUIRE(controller.completeSubcommand());
      read(controller, 0x18);
      read(controller, 0x18);
      read(controller, 0x18);
    }
    const auto before = controller;
    for (std::uint32_t offset = 0; offset < 0x40; ++offset)
    {
      CAPTURE(offset);
      if (offset != 0x04 && offset != 0x05 && offset != 0x06 &&
          offset != 0x08 && offset != 0x0a && offset != 0x0f &&
          offset != 0x16 && offset != 0x17 && offset != 0x18)
      {
        const auto rejected = controller.readByte(BASE + offset);
        REQUIRE(rejected.status == CDVDAccessStatus::Unsupported);
        REQUIRE(CDVDControllerTestAccess::sameState(controller, before));
      }
      if (offset != 0x04 && offset != 0x08 && offset != 0x16)
      {
        for (unsigned value = 0; value < 256; ++value)
        {
          REQUIRE(controller.writeByte(BASE + offset,
            static_cast<std::uint8_t>(value)) == CDVDAccessStatus::Unsupported);
          REQUIRE(CDVDControllerTestAccess::sameState(controller, before));
        }
      }
    }
    for (const auto address : {0x1f401fffu, 0x1f403005u, 0xbf402005u,
           0x9f402018u, 0x1f402104u, 0u, UINT32_MAX})
    {
      CAPTURE(address);
      REQUIRE(controller.readByte(address).status == CDVDAccessStatus::Unsupported);
      REQUIRE(controller.writeByte(address, 0) == CDVDAccessStatus::Unsupported);
      REQUIRE(CDVDControllerTestAccess::sameState(controller, before));
    }
  }
}

TEST_CASE("CDVD owner admits only N-NOP and ReadClock in their available phases",
  "[iop][cdvd-owner]")
{
  for (unsigned stage = 0; stage < 10; ++stage)
  {
    CDVDController controller;
    if (stage != 0)
    {
      accept(controller, 0x04, 0);
      accept(controller, 0x16, 8);
      if (stage >= 2)
      {
        REQUIRE(controller.completeNormalCommand().completed);
        REQUIRE(controller.completeSubcommand());
        for (unsigned consumed = 0; consumed < stage - 2; ++consumed)
        {
          read(controller, 0x18);
        }
      }
    }
    for (const auto offset : {0x04u, 0x16u})
    {
      for (unsigned opcode = 0; opcode < 256; ++opcode)
      {
        CAPTURE(stage);
        CAPTURE(offset);
        CAPTURE(opcode);
        auto probe = controller;
        const bool available = offset == 0x04 ? stage != 1 : stage == 0;
        const bool supported = opcode == (offset == 0x04 ? 0u : 8u);
        const auto status = probe.writeByte(BASE + offset,
          static_cast<std::uint8_t>(opcode));
        if (available && supported)
        {
          REQUIRE(status == CDVDAccessStatus::Accepted);
          REQUIRE(read(probe, offset) == opcode);
          REQUIRE(read(probe, offset + 1) == (offset == 0x04 ? 0x80 : 0xc0));
        }
        else
        {
          REQUIRE(status == CDVDAccessStatus::Unsupported);
          REQUIRE(CDVDControllerTestAccess::sameState(probe, controller));
        }
      }
    }
  }
}

TEST_CASE("CDVD owner completes after acceptance on the next IOP pulse",
  "[iop][cdvd-owner]")
{
  CDVDController controller;
  REQUIRE_FALSE(masterStep(controller, 0).completed);
  accept(controller, 0x04, 0);
  accept(controller, 0x16, 8);
  for (unsigned cycle = 1; cycle < 8; ++cycle)
  {
    CAPTURE(cycle);
    requirePending(controller);
    const auto before = controller;
    for (unsigned repeat = 0; repeat < 8; ++repeat)
    {
      requirePending(controller);
    }
    REQUIRE(CDVDControllerTestAccess::sameState(controller, before));
    const auto completion = masterStep(controller, cycle);
    REQUIRE_FALSE(completion.completed);
    REQUIRE(completion.newCauses == 0);
  }
  requirePending(controller);
  const auto completion = masterStep(controller, 8);
  REQUIRE(completion.completed);
  REQUIRE(completion.newCauses == 1);
  REQUIRE(read(controller, 0x05) == 0x40);
  REQUIRE(read(controller, 0x08) == 1);
  REQUIRE(read(controller, 0x17) == 0);
  REQUIRE(drain(controller) == EPOCH);
  const auto before = controller;
  REQUIRE_FALSE(controller.completeNormalCommand().completed);
  REQUIRE(controller.completeNormalCommand().newCauses == 0);
  REQUIRE_FALSE(controller.completeSubcommand());
  REQUIRE(CDVDControllerTestAccess::sameState(controller, before));
}

TEST_CASE("CDVD owner N and S transports complete and drain independently",
  "[iop][cdvd-owner]")
{
  for (const bool normalFirst : {false, true})
  {
    CDVDController controller;
    accept(controller, 0x16, 8);
    accept(controller, 0x04, 0);
    if (normalFirst)
    {
      REQUIRE(controller.completeNormalCommand().newCauses == 1);
      REQUIRE(read(controller, 0x17) == 0xc0);
      REQUIRE(controller.completeSubcommand());
    }
    else
    {
      REQUIRE(controller.completeSubcommand());
      REQUIRE(read(controller, 0x05) == 0x80);
      REQUIRE(drain(controller) == EPOCH);
      accept(controller, 0x16, 8);
      REQUIRE(controller.completeNormalCommand().newCauses == 1);
      REQUIRE(read(controller, 0x17) == 0xc0);
      REQUIRE(controller.completeSubcommand());
    }
    REQUIRE(read(controller, 0x08) == 1);
    for (unsigned index = 0; index < 8; ++index)
    {
      CAPTURE(index);
      accept(controller, 0x04, 0);
      REQUIRE(controller.completeNormalCommand().newCauses == 0);
      REQUIRE(read(controller, 0x17) == 0);
      REQUIRE(read(controller, 0x18) == EPOCH[index]);
      REQUIRE(read(controller, 0x08) == 1);
    }
    REQUIRE(read(controller, 0x17) == 0x40);
    for (unsigned repeat = 0; repeat < 3; ++repeat)
    {
      REQUIRE(read(controller, 0x18) == 0);
    }
    accept(controller, 0x16, 8);
    REQUIRE(controller.completeSubcommand());
    REQUIRE(drain(controller) == EPOCH);
  }
}

TEST_CASE("CDVD owner reports only newly latched N completion causes",
  "[iop][cdvd-owner]")
{
  for (unsigned ack = 0; ack < 256; ++ack)
  {
    CAPTURE(ack);
    CDVDController controller;
    accept(controller, 0x04, 0);
    accept(controller, 0x08, 0xff);
    REQUIRE(read(controller, 0x05) == 0x80);
    REQUIRE(controller.completeNormalCommand().newCauses == 1);
    accept(controller, 0x08, static_cast<std::uint8_t>(ack));
    REQUIRE(read(controller, 0x08) == ((ack & 1) == 0 ? 1 : 0));
    accept(controller, 0x04, 0);
    const auto completion = controller.completeNormalCommand();
    REQUIRE(completion.completed);
    REQUIRE(completion.newCauses == ((ack & 1) == 0 ? 0 : 1));
    REQUIRE(read(controller, 0x08) == 1);
    REQUIRE(controller.completeNormalCommand().newCauses == 0);
    accept(controller, 0x08, 0xff);
    for (unsigned tick = 0; tick < 16; ++tick)
    {
      REQUIRE_FALSE(masterStep(controller, tick).completed);
    }
    REQUIRE(read(controller, 0x08) == 0);
    accept(controller, 0x16, 8);
    REQUIRE(controller.completeSubcommand());
    REQUIRE(read(controller, 0x08) == 0);
  }
}

TEST_CASE("CDVD RTC snapshots calendar boundaries after the elapsed master tick",
  "[iop][cdvd-owner][cdvd-rtc]")
{
  struct Boundary
  {
    std::uint64_t seconds;
    std::array<std::uint8_t, 8> expected;
  };
  const Boundary boundaries[] = {
    {1, {{0, 1, 0, 0, 0, 1, 1, 0}}},
    {59, {{0, 0x59, 0, 0, 0, 1, 1, 0}}},
    {60, {{0, 0, 1, 0, 0, 1, 1, 0}}},
    {3600, {{0, 0, 0, 1, 0, 1, 1, 0}}},
    {86400, {{0, 0, 0, 0, 0, 2, 1, 0}}},
    {31 * 86400, {{0, 0, 0, 0, 0, 1, 2, 0}}},
    {59 * 86400, {{0, 0, 0, 0, 0, 0x29, 2, 0}}},
    {60 * 86400, {{0, 0, 0, 0, 0, 1, 3, 0}}},
    {366 * 86400, {{0, 0, 0, 0, 0, 1, 1, 1}}},
    {424 * 86400, {{0, 0, 0, 0, 0, 0x28, 2, 1}}},
    {425 * 86400, {{0, 0, 0, 0, 0, 1, 3, 1}}},
    {456 * 86400, {{0, 0, 0, 0, 0, 1, 4, 1}}},
    {486 * 86400, {{0, 0, 0, 0, 0, 1, 5, 1}}},
    {517 * 86400, {{0, 0, 0, 0, 0, 1, 6, 1}}},
    {547 * 86400, {{0, 0, 0, 0, 0, 1, 7, 1}}},
    {578 * 86400, {{0, 0, 0, 0, 0, 1, 8, 1}}},
    {609 * 86400, {{0, 0, 0, 0, 0, 1, 9, 1}}},
    {639 * 86400, {{0, 0, 0, 0, 0, 1, 0x10, 1}}},
    {670 * 86400, {{0, 0, 0, 0, 0, 1, 0x11, 1}}},
    {700 * 86400, {{0, 0, 0, 0, 0, 1, 0x12, 1}}},
    {731 * 86400, {{0, 0, 0, 0, 0, 1, 1, 2}}},
    {1461 * 86400, {{0, 0, 0, 0, 0, 1, 1, 4}}},
    {1520 * 86400, {{0, 0, 0, 0, 0, 0x29, 2, 4}}},
    {1521 * 86400, {{0, 0, 0, 0, 0, 1, 3, 4}}},
    {UINT64_C(35123) * 86400, {{0, 0, 0, 0, 0, 0x29, 2, 0x96}}},
    {UINT64_C(36160) * 86400, {{0, 0, 0, 0, 0, 1, 1, 0x99}}},
    {CENTURY_SECONDS - 1, LAST_SECOND}
  };
  for (const auto &boundary : boundaries)
  {
    CAPTURE(boundary.seconds);
    CDVDController controller;
    CDVDControllerTestAccess::seedClock(controller,
      boundary.seconds * PS2Clock::MASTER_HZ - 1);
    accept(controller, 0x16, 8);
    controller.advanceMasterTick();
    REQUIRE(controller.completeSubcommand());
    REQUIRE(drain(controller) == boundary.expected);
  }
  CDVDController controller;
  CDVDControllerTestAccess::seedClock(controller,
    CENTURY_SECONDS * PS2Clock::MASTER_HZ - 1);
  accept(controller, 0x16, 8);
  controller.advanceMasterTick();
  REQUIRE(CDVDControllerTestAccess::ticks(controller) == 0);
  REQUIRE(controller.completeSubcommand());
  REQUIRE(drain(controller) == EPOCH);
  controller.advanceMasterTick();
  REQUIRE(CDVDControllerTestAccess::ticks(controller) == 1);
}

TEST_CASE("CDVD RTC advances while idle and retains fractional seconds",
  "[iop][cdvd-owner][cdvd-rtc]")
{
  CDVDController controller;
  CDVDControllerTestAccess::seedClock(controller, PS2Clock::MASTER_HZ - 2);
  controller.advanceMasterTick();
  REQUIRE(CDVDControllerTestAccess::ticks(controller) == PS2Clock::MASTER_HZ - 1);
  accept(controller, 0x16, 8);
  REQUIRE(controller.completeSubcommand());
  REQUIRE(drain(controller) == EPOCH);
  REQUIRE(CDVDControllerTestAccess::ticks(controller) == PS2Clock::MASTER_HZ - 1);
  controller.advanceMasterTick();
  REQUIRE(CDVDControllerTestAccess::ticks(controller) == PS2Clock::MASTER_HZ);
  accept(controller, 0x16, 8);
  REQUIRE(controller.completeSubcommand());
  const std::array<std::uint8_t, 8> oneSecond = {{0, 1, 0, 0, 0, 1, 1, 0}};
  REQUIRE(drain(controller) == oneSecond);
}

TEST_CASE("CDVD RTC keeps a partially drained snapshot across century wrap",
  "[iop][cdvd-owner][cdvd-rtc]")
{
  CDVDController controller;
  CDVDControllerTestAccess::seedClock(controller,
    CENTURY_SECONDS * PS2Clock::MASTER_HZ - 2);
  accept(controller, 0x16, 8);
  controller.advanceMasterTick();
  REQUIRE(controller.completeSubcommand());
  for (unsigned index = 0; index < 8; ++index)
  {
    CAPTURE(index);
    controller.advanceMasterTick();
    REQUIRE(read(controller, 0x17) == 0);
    const auto before = controller;
    REQUIRE_FALSE(controller.completeSubcommand());
    REQUIRE(controller.writeByte(BASE + 0x16, 8) == CDVDAccessStatus::Unsupported);
    REQUIRE(CDVDControllerTestAccess::sameState(controller, before));
    REQUIRE(read(controller, 0x18) == LAST_SECOND[index]);
  }
  REQUIRE(read(controller, 0x17) == 0x40);
  accept(controller, 0x16, 8);
  REQUIRE(controller.completeSubcommand());
  REQUIRE(drain(controller) == EPOCH);
}

TEST_CASE("CDVD reset discards pending and partially drained work and RTC phase",
  "[iop][cdvd-owner]")
{
  const CDVDController fresh;
  for (unsigned stage = 0; stage < 11; ++stage)
  {
    CAPTURE(stage);
    CDVDController controller;
    CDVDControllerTestAccess::seedClock(controller,
      (CENTURY_SECONDS - 1) * PS2Clock::MASTER_HZ + 123);
    accept(controller, 0x04, 0);
    accept(controller, 0x16, 8);
    if (stage >= 1)
    {
      REQUIRE(controller.completeNormalCommand().completed);
    }
    if (stage >= 2)
    {
      REQUIRE(controller.completeSubcommand());
      for (unsigned consumed = 0; consumed < stage - 2; ++consumed)
      {
        read(controller, 0x18);
      }
    }
    controller.reset();
    controller.reset();
    REQUIRE(CDVDControllerTestAccess::sameState(controller, fresh));
    REQUIRE_FALSE(controller.completeNormalCommand().completed);
    REQUIRE_FALSE(controller.completeSubcommand());
    REQUIRE(read(controller, 0x18) == 0);
    accept(controller, 0x16, 8);
    REQUIRE(controller.completeSubcommand());
    REQUIRE(drain(controller) == EPOCH);
  }
}
