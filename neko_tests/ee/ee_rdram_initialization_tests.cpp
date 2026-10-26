#include <cstdint>
#include <stdexcept>

#include "catch.hpp"
#include "ee_rdram_initialization.hpp"

namespace
{
  void requireState(
    const EERDRAMInitialization &owner,
    const EERDRAMInitializationState &expected)
  {
    const auto actual = owner.state();
    REQUIRE(actual.command == expected.command);
    REQUIRE(actual.inputData == expected.inputData);
    REQUIRE(actual.enumeratedDevices == expected.enumeratedDevices);
    REQUIRE(owner.readStatus() == 0);
  }

  void requireData(EERDRAMInitialization &owner, std::uint32_t expected)
  {
    std::uint32_t value = UINT32_MAX;
    REQUIRE(owner.readData(&value));
    REQUIRE(value == expected);
    REQUIRE(owner.readStatus() == 0);
  }

  void requireUnreadable(EERDRAMInitialization &owner)
  {
    const auto before = owner.state();
    std::uint32_t value = 0xa5a5a5a5;
    REQUIRE_FALSE(owner.readData(&value));
    REQUIRE(value == 0xa5a5a5a5);
    requireState(owner, before);
  }

  void requireRejectedCommand(
    EERDRAMInitialization &owner, std::uint32_t command)
  {
    CAPTURE(command);
    const auto before = owner.state();
    REQUIRE_FALSE(owner.writeCommand(command));
    requireState(owner, before);
  }

  EERDRAMInitialization partiallyEnumerated()
  {
    EERDRAMInitialization owner;
    owner.writeData(0x12345678);
    REQUIRE(owner.writeCommand(0x80210000));
    requireData(owner, 0x1f);
    return owner;
  }
}

TEST_CASE("EE RDRAM initialization owner starts idle without a data response",
  "[ee][rdram-init-owner]")
{
  EERDRAMInitialization owner;
  requireState(owner, {});
  requireUnreadable(owner);
  REQUIRE_THROWS_AS(owner.readData(nullptr), std::invalid_argument);
  requireState(owner, {});
}

TEST_CASE("EE RDRAM initialization owner admits the exact first BIOS command",
  "[ee][rdram-init-owner]")
{
  auto owner = partiallyEnumerated();
  owner.writeData(0);
  REQUIRE(owner.writeCommand(0x800003a0));
  requireState(owner, {0x000003a0, 0, 1});
  requireUnreadable(owner);
  for (unsigned repeat = 0; repeat < 3; ++repeat)
  {
    REQUIRE(owner.writeCommand(0x800003a0));
    requireState(owner, {0x000003a0, 0, 1});
  }

  for (unsigned bit = 0; bit < 32; ++bit)
  {
    CAPTURE(bit);
    requireRejectedCommand(owner, 0x800003a0u ^ (UINT32_C(1) << bit));
    owner.writeData(UINT32_C(1) << bit);
    requireRejectedCommand(owner, 0x800003a0);
    owner.writeData(0);
  }
}

TEST_CASE("EE RDRAM initialization owner selects explicit retail read responses",
  "[ee][rdram-init-owner]")
{
  struct ReadCase
  {
    std::uint32_t command;
    std::uint32_t response;
  };
  for (const auto test : {
    ReadCase{0x80210000, 0x1f},
    ReadCase{0x80230000, 0x0d0d},
    ReadCase{0x80240000, 0x0090},
    ReadCase{0x80400000, 0}})
  {
    for (std::uint32_t device = 0; device < 32; ++device)
    {
      CAPTURE(test.command);
      CAPTURE(device);
      EERDRAMInitialization owner;
      owner.writeData(0xa5a5a5a5);
      REQUIRE(owner.writeCommand(test.command | device));
      requireState(owner, {
        (test.command & 0x7fffffffu) | device, 0xa5a5a5a5, 0});
      const auto expected =
        test.command == 0x80400000 ? device : test.response;
      requireData(owner, expected);
      REQUIRE(owner.state().enumeratedDevices ==
        (test.command == 0x80210000 ? 1 : 0));
      if (test.command != 0x80210000)
      {
        const auto before = owner.state();
        for (unsigned repeat = 0; repeat < 4; ++repeat)
        {
          requireData(owner, expected);
          requireState(owner, before);
        }
      }
    }
  }
}

TEST_CASE("EE RDRAM SA4d exact command preserves every enumeration continuation",
  "[ee][rdram-init-owner][rdram-sa4d]")
{
  for (std::uint8_t cursor = 0; cursor <= 2; ++cursor)
  {
    CAPTURE(cursor);
    EERDRAMInitialization owner;
    REQUIRE(owner.writeCommand(0x80210000));
    for (unsigned read = 0; read < cursor; ++read)
    {
      requireData(owner, 0x1f);
    }
    for (unsigned repeat = 0; repeat < 3; ++repeat)
    {
      REQUIRE(owner.writeCommand(0x804d0060));
      requireState(owner, {0x004d0060, 0, cursor});
      requireUnreadable(owner);
    }
    for (unsigned bit = 0; bit < 32; ++bit)
    {
      const auto input = UINT32_C(1) << bit;
      owner.writeData(input);
      requireState(owner, {0x004d0060, input, cursor});
      requireUnreadable(owner);
      requireRejectedCommand(owner, 0x804d0060);
    }
    owner.writeData(UINT32_MAX);
    requireRejectedCommand(owner, 0x804d0060);
    owner.writeData(0);
    REQUIRE(owner.writeCommand(0x804d0060));
    REQUIRE(owner.writeCommand(0x80210000));
    requireData(owner, cursor < 2 ? 0x1f : 0);
    owner.writeData(0);
    REQUIRE(owner.writeCommand(0x80210040));
    REQUIRE(owner.writeCommand(0x80210000));
    requireData(owner, 0x1f);
    REQUIRE(owner.writeCommand(0x804d0060));
    owner.writeData(UINT32_MAX);
    owner.reset();
    requireState(owner, {});
    requireUnreadable(owner);
  }
}

TEST_CASE("EE RDRAM SA4d rejects alternate encodings without changing selection",
  "[ee][rdram-init-owner][rdram-sa4d]")
{
  auto owner = partiallyEnumerated();
  owner.writeData(0);
  for (unsigned bit = 0; bit < 32; ++bit)
  {
    requireRejectedCommand(owner, 0x804d0060u ^ (UINT32_C(1) << bit));
  }
  for (std::uint32_t operation = 0; operation < 16; ++operation)
  {
    for (std::uint32_t selectors = 0; selectors < 64; ++selectors)
    {
      const auto command = 0x804d0000u | (operation << 6) | selectors;
      if (command != 0x804d0060)
      {
        requireRejectedCommand(owner, command);
      }
    }
  }
  requireData(owner, 0x1f);
  REQUIRE(owner.writeCommand(0x804d0060));
  for (unsigned bit = 0; bit < 32; ++bit)
  {
    requireRejectedCommand(owner, 0x804d0060u ^ (UINT32_C(1) << bit));
  }
  requireState(owner, {0x004d0060, 0, 2});
  requireUnreadable(owner);
}

TEST_CASE("EE RDRAM initialization owner enumerates two devices without wrapping",
  "[ee][rdram-init-owner]")
{
  auto owner = partiallyEnumerated();
  for (unsigned repeat = 0; repeat < 4; ++repeat)
  {
    REQUIRE(owner.readStatus() == 0);
  }
  REQUIRE(owner.state().enumeratedDevices == 1);
  owner.writeData(UINT32_MAX);
  REQUIRE(owner.writeCommand(0x80230000));
  requireData(owner, 0x0d0d);
  REQUIRE(owner.state().enumeratedDevices == 1);
  REQUIRE(owner.writeCommand(0x8021001f));
  requireData(owner, 0x1f);
  requireState(owner, {0x0021001f, UINT32_MAX, 2});
  for (unsigned repeat = 0; repeat < 1024; ++repeat)
  {
    REQUIRE(owner.writeCommand(0x80210000 | (repeat & 0x1f)));
    requireData(owner, 0);
    REQUIRE(owner.state().enumeratedDevices == 2);
  }
}

TEST_CASE("EE RDRAM initialization owner reinitializes only with input bit seven clear",
  "[ee][rdram-init-owner]")
{
  for (const auto input : {
    0u, 1u, 0x7fu, 0x80u, 0xffu, 0x80000000u, 0xffffff7fu, UINT32_MAX})
  {
    for (std::uint32_t selectors = 0; selectors < 64; ++selectors)
    {
      CAPTURE(input);
      CAPTURE(selectors);
      auto owner = partiallyEnumerated();
      requireData(owner, 0x1f);
      owner.writeData(input);
      const std::uint32_t command = 0x80210040 | selectors;
      REQUIRE(owner.writeCommand(command));
      const std::uint8_t expectedCount = (input & 0x80) == 0 ? 0 : 2;
      requireState(owner, {command & 0x7fffffffu, input, expectedCount});
      requireUnreadable(owner);
      REQUIRE(owner.writeCommand(0x80210000));
      requireData(owner, expectedCount == 0 ? 0x1f : 0);
    }
  }
}

TEST_CASE("EE RDRAM initialization owner retains input without replaying commands",
  "[ee][rdram-init-owner]")
{
  auto owner = partiallyEnumerated();
  owner.writeData(0x80);
  REQUIRE(owner.writeCommand(0x80210060));
  for (unsigned bit = 0; bit < 32; ++bit)
  {
    const auto input = UINT32_C(1) << bit;
    owner.writeData(input);
    requireState(owner, {0x00210060, input, 1});
    requireUnreadable(owner);
  }
  owner.writeData(0);
  requireState(owner, {0x00210060, 0, 1});
  REQUIRE(owner.writeCommand(0x80210000));
  owner.writeData(UINT32_MAX);
  requireData(owner, 0x1f);
  requireState(owner, {0x00210000, UINT32_MAX, 2});
}

TEST_CASE("EE RDRAM initialization owner rejects unknown operations and subaddresses",
  "[ee][rdram-init-owner]")
{
  auto owner = partiallyEnumerated();
  for (std::uint32_t subaddress = 0; subaddress < 0x1000; ++subaddress)
  {
    if (subaddress != 0x21 && subaddress != 0x23 &&
        subaddress != 0x24 && subaddress != 0x40)
    {
      requireRejectedCommand(owner, 0x80000000u | (subaddress << 16));
    }
    if (subaddress != 0x21)
    {
      requireRejectedCommand(owner, 0x80000040u | (subaddress << 16));
    }
  }
  for (std::uint32_t operation = 2; operation < 16; ++operation)
  {
    for (const auto subaddress : {0u, 0x21u, 0x23u, 0x24u, 0x40u})
    {
      requireRejectedCommand(owner,
        0x80000000u | (subaddress << 16) | (operation << 6));
    }
  }
  requireRejectedCommand(owner, UINT32_MAX);
  requireData(owner, 0x1f);
  REQUIRE(owner.state().enumeratedDevices == 2);
}

TEST_CASE("EE RDRAM initialization owner rejects reserved fields and missing start",
  "[ee][rdram-init-owner]")
{
  auto owner = partiallyEnumerated();
  for (const auto command : {
    0x80210000u, 0x80230000u, 0x80240000u, 0x80400000u,
    0x80210040u, 0x8021007fu})
  {
    requireRejectedCommand(owner, command & 0x7fffffffu);
    for (const auto bit : {10u, 11u, 12u, 13u, 14u, 15u, 28u, 29u, 30u})
    {
      requireRejectedCommand(owner, command | (UINT32_C(1) << bit));
    }
    if ((command & 0x3c0) == 0)
    {
      requireRejectedCommand(owner, command | 0x20);
    }
  }
  requireData(owner, 0x1f);
}

TEST_CASE("EE RDRAM initialization owner null outputs do not consume enumeration",
  "[ee][rdram-init-owner]")
{
  auto owner = partiallyEnumerated();
  const auto before = owner.state();
  REQUIRE_THROWS_AS(owner.readData(nullptr), std::invalid_argument);
  requireState(owner, before);
  requireData(owner, 0x1f);
}

TEST_CASE("EE RDRAM initialization owner reset clears every continuation",
  "[ee][rdram-init-owner]")
{
  for (const auto command : {
    0x80210000u, 0x80230001u, 0x8024001fu, 0x8040000fu,
    0x80210040u, 0x8021007fu, 0x800003a0u})
  {
    CAPTURE(command);
    auto owner = partiallyEnumerated();
    owner.writeData(command == 0x800003a0 ? 0 : UINT32_MAX);
    REQUIRE(owner.writeCommand(command));
    owner.reset();
    requireState(owner, {});
    requireUnreadable(owner);
    owner.reset();
    requireState(owner, {});
    REQUIRE(owner.writeCommand(0x800003a0));
    requireUnreadable(owner);
    REQUIRE(owner.writeCommand(0x80210000));
    requireData(owner, 0x1f);
    requireData(owner, 0x1f);
    requireData(owner, 0);
  }
}
