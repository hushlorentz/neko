#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

#include "catch.hpp"
#include "ee_bus.hpp"
#include "gif_registers.hpp"
#include "neko_system.hpp"
#include "save_state_mutation.hpp"

namespace
{
  std::uint32_t immediateInstruction(
    std::uint8_t opcode,
    std::uint8_t source,
    std::uint8_t target,
    std::uint16_t immediate = 0)
  {
    return
      (static_cast<std::uint32_t>(opcode) << 26) |
      (static_cast<std::uint32_t>(source) << 21) |
      (static_cast<std::uint32_t>(target) << 16) |
      immediate;
  }

  GIFQuadword gifTag()
  {
    const std::uint64_t low =
      UINT64_C(1) |
      (UINT64_C(1) << 15) |
      (static_cast<std::uint64_t>(GIFDataFormat::Packed) << 58) |
      (UINT64_C(1) << 60);
    return GIFQuadword{{
      static_cast<std::uint32_t>(low),
      static_cast<std::uint32_t>(low >> 32),
      GIFRegisterDescriptor::AD,
      0
    }};
  }

  GIFQuadword adWrite(
    std::uint8_t address,
    std::uint64_t value)
  {
    return GIFQuadword{{
      static_cast<std::uint32_t>(value),
      static_cast<std::uint32_t>(value >> 32),
      address,
      0
    }};
  }

  EEExecutionResult guestLoad(
    NekoSystem &system, std::uint32_t address)
  {
    system.eeBus().write32(0, UINT32_C(0x8c220000));
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, {address | EEMemoryMap::KSEG1_BASE, 0});
    core.setGeneralRegister(2, {UINT32_MAX, UINT32_MAX});
    core.setCOP0Register(EECOP0Register::Status, 0);
    core.startExecution(EEMemoryMap::KSEG1_BASE);
    system.startTrace(100);
    return system.stepEEInstruction(16);
  }
}

TEST_CASE("EE BA0 retained registers admit only exact physical halfword accesses",
  "[ee][bus][ba0]")
{
  std::uint32_t address = 0;
  SECTION("offset04") { address = 0x1a000004; }
  SECTION("offset08") { address = 0x1a000008; }
  const std::uint32_t otherAddress =
    address == 0x1a000004 ? 0x1a000008 : 0x1a000004;
  NekoSystem system;
  auto &bus = system.eeBus();
  std::uint16_t half = 0xa5a5;
  REQUIRE(bus.readData16(address, &half));
  REQUIRE(half == 0);
  const auto resetHash = system.eeStateHash();
  for (const std::uint16_t written : {0u, 1u, 3u, 0x7fffu, 0x8000u, 0xffffu})
  {
    CAPTURE(written);
    REQUIRE(bus.writeData16(address, written));
    REQUIRE(bus.readData16(address, &half));
    REQUIRE(half == written);
    const auto hash = system.eeStateHash();
    REQUIRE((hash == resetHash) == (written == 0));
    REQUIRE(bus.writeData16(address, written));
    REQUIRE(system.eeStateHash() == hash);
    REQUIRE(bus.readData16(otherAddress, &half));
    REQUIRE(half == 0);
  }
  REQUIRE(bus.writeData16(otherAddress, 0x1234));
  const auto hash = system.eeStateHash();
  for (const auto unsupported : {0x1a000000u, 0x1a000002u,
         0x1a00000au, 0x1a00000cu, 0x1a00000eu, 0x1a000020u,
         address + 0x20, address + 0x100, address + 0x10000,
         address | 0x80000000u, address | 0xa0000000u})
  {
    CAPTURE(unsupported);
    half = 0xa5a5;
    REQUIRE_FALSE(bus.readData16(unsupported, &half));
    REQUIRE(half == 0xa5a5);
    REQUIRE_FALSE(bus.writeData16(unsupported,
      unsupported == 0x1a000006 ? 2 : 3));
  }
  for (const auto byteAddress : {address, address + 1})
  {
    std::uint8_t byte = 0xa5;
    REQUIRE_FALSE(bus.readData8(byteAddress, &byte));
    REQUIRE(byte == 0xa5);
    REQUIRE_FALSE(bus.writeData8(byteAddress, 0));
  }
  std::uint32_t word = 0xa5a5a5a5;
  std::uint64_t wide = UINT64_MAX;
  EEQuadword quad = {UINT64_MAX, UINT64_MAX};
  REQUIRE_FALSE(bus.readData32(address, &word));
  REQUIRE(word == 0xa5a5a5a5);
  REQUIRE_FALSE(bus.writeData32(address, 0));
  const auto wideAddress = address & ~std::uint32_t{7};
  REQUIRE_FALSE(bus.readData64(wideAddress, &wide));
  REQUIRE(wide == UINT64_MAX);
  REQUIRE_FALSE(bus.writeData64(wideAddress, 0));
  REQUIRE_FALSE(bus.readData128(0x1a000000, &quad));
  REQUIRE(quad.low == UINT64_MAX);
  REQUIRE(quad.high == UINT64_MAX);
  REQUIRE_FALSE(bus.writeData128(0x1a000000, {0, 0}));
  REQUIRE_FALSE(bus.readInstruction32(address, &word));
  REQUIRE_FALSE(bus.isCacheFillRange(address, 2));
  REQUIRE_FALSE(bus.readDMAC128(0x1a000000, &quad));
  REQUIRE_FALSE(bus.writeDMAC128(0x1a000000, {0, 0}));
  REQUIRE_THROWS_AS(bus.readData16(address, nullptr), std::invalid_argument);
  REQUIRE_THROWS_AS(bus.readData16(address + 1, &half), std::invalid_argument);
  REQUIRE_THROWS_AS(bus.writeData16(address + 1, 0), std::invalid_argument);
  REQUIRE(system.eeStateHash() == hash);
  REQUIRE(bus.readData16(address, &half));
  REQUIRE(half == 0xffff);
  REQUIRE(bus.readData16(otherAddress, &half));
  REQUIRE(half == 0x1234);
  system.reset();
  REQUIRE(system.eeBus().readData16(address, &half));
  REQUIRE(half == 0);
  REQUIRE(system.eeBus().readData16(otherAddress, &half));
  REQUIRE(half == 0);
  REQUIRE(system.eeStateHash() == resetHash);
}

TEST_CASE("EE BA0 status admits exact halfword acknowledgement and pure reads",
  "[ee][bus][ba0-init]")
{
  constexpr std::uint32_t ADDRESS = 0x1a000006;
  NekoSystem system;
  auto &bus = system.eeBus();
  REQUIRE(bus.writeData16(0x1a000004, 0x1234));
  REQUIRE(bus.writeData16(0x1a000008, 0xabcd));
  const auto eeHash = system.eeStateHash();
  const auto iopHash = system.iopStateHash();
  const auto sifHash = system.sifStateHash();
  REQUIRE(bus.writeData16(ADDRESS, 3));
  REQUIRE(bus.writeData16(ADDRESS, 3));
  for (const std::uint16_t value : {0u, 1u, 2u, 4u, 0x8003u, 0xffffu})
  {
    CAPTURE(value);
    REQUIRE_FALSE(bus.writeData16(ADDRESS, value));
  }
  std::uint16_t half = 0xa5a5;
  REQUIRE(bus.readData16(ADDRESS, &half));
  REQUIRE(half == 0);
  half = 0xa5a5;
  for (const auto address : {0x1a000000u, 0x1a000002u,
         0x1a00000au, 0x1a00000cu, 0x1a000106u, 0x9a000006u, 0xba000006u})
  {
    CAPTURE(address);
    REQUIRE_FALSE(bus.readData16(address, &half));
    REQUIRE(half == 0xa5a5);
    if (address != ADDRESS)
    {
      REQUIRE_FALSE(bus.writeData16(address, 3));
    }
  }
  for (const auto address : {ADDRESS, ADDRESS + 1})
  {
    std::uint8_t byte = 0xa5;
    REQUIRE_FALSE(bus.readData8(address, &byte));
    REQUIRE(byte == 0xa5);
    REQUIRE_FALSE(bus.writeData8(address, 3));
  }
  std::uint32_t word = 0xa5a5a5a5;
  std::uint64_t wide = UINT64_MAX;
  EEQuadword quad{UINT64_MAX, UINT64_MAX};
  REQUIRE_FALSE(bus.readData32(0x1a000004, &word));
  REQUIRE(word == 0xa5a5a5a5);
  REQUIRE_FALSE(bus.writeData32(0x1a000004, 0x00030000));
  REQUIRE_FALSE(bus.readData64(0x1a000000, &wide));
  REQUIRE(wide == UINT64_MAX);
  REQUIRE_FALSE(bus.writeData64(0x1a000000, UINT64_C(3) << 48));
  REQUIRE_FALSE(bus.readData128(0x1a000000, &quad));
  REQUIRE(quad.low == UINT64_MAX);
  REQUIRE(quad.high == UINT64_MAX);
  REQUIRE_FALSE(bus.writeData128(0x1a000000, {UINT64_C(3) << 48, 0}));
  REQUIRE_FALSE(bus.readInstruction32(0x1a000004, &word));
  REQUIRE_FALSE(bus.isCacheFillRange(ADDRESS, 2));
  REQUIRE_FALSE(bus.readDMAC128(0x1a000000, &quad));
  REQUIRE_FALSE(bus.writeDMAC128(0x1a000000, {UINT64_C(3) << 48, 0}));
  REQUIRE_THROWS_AS(bus.readData16(ADDRESS, nullptr), std::invalid_argument);
  REQUIRE_THROWS_AS(bus.readData16(ADDRESS + 1, &half), std::invalid_argument);
  REQUIRE_THROWS_AS(bus.writeData16(ADDRESS + 1, 3), std::invalid_argument);
  REQUIRE(system.iopBus().read16(ADDRESS).status == IOPBusStatus::Unmapped);
  REQUIRE(system.iopBus().write16(ADDRESS, 3).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.readData16(0x1a000004, &half));
  REQUIRE(half == 0x1234);
  REQUIRE(bus.readData16(0x1a000008, &half));
  REQUIRE(half == 0xabcd);
  REQUIRE(system.eeStateHash() == eeHash);
  REQUIRE(system.iopStateHash() == iopHash);
  REQUIRE(system.sifStateHash() == sifHash);
}

TEST_CASE("EE BA0 selector commands complete synchronously with pure status polling",
  "[ee][bus][ba0-command]")
{
  NekoSystem system;
  auto &bus = system.eeBus();
  const auto resetHash = system.eeStateHash();
  REQUIRE_FALSE(bus.writeData16(0x1a000000, 0x81));
  REQUIRE(system.eeStateHash() == resetHash);
  REQUIRE(bus.writeData16(0x1a000002, 0x42));
  REQUIRE(bus.writeData16(0x1a000004, 0xcafe));
  REQUIRE(bus.writeData16(0x1a000008, 0x1357));
  REQUIRE(bus.writeData16(0x1a000012, 0xabcd));
  const auto unselectedHash = system.eeStateHash();
  const auto iopHash = system.iopStateHash();
  const auto sifHash = system.sifStateHash();
  for (const std::uint16_t selector : {0u, 0x7eu, 0xffu})
  {
    CAPTURE(selector);
    REQUIRE(bus.writeData16(0x1a000010, selector));
    REQUIRE(bus.writeData16(0x1a000000, 0x81));
    const auto completedHash = system.eeStateHash();
    for (unsigned repeat = 0; repeat < 4; ++repeat)
    {
      std::uint16_t status = 0xffff;
      REQUIRE(bus.readData16(0x1a000006, &status));
      REQUIRE(status == 2);
      REQUIRE(system.eeStateHash() == completedHash);
    }
    REQUIRE(bus.writeData16(0x1a000000, 0x81));
    REQUIRE(system.eeStateHash() == completedHash);
    for (const std::uint16_t invalid : {0u, 1u, 2u, 4u, 0xffffu})
    {
      REQUIRE_FALSE(bus.writeData16(0x1a000006, invalid));
      REQUIRE(system.eeStateHash() == completedHash);
    }
    REQUIRE_FALSE(bus.writeData16(0x1a000002, 0x44));
    for (const std::uint16_t command : {0u, 1u, 0x80u, 0x82u, 0x181u, 0xffffu})
    {
      REQUIRE_FALSE(bus.writeData16(0x1a000000, command));
      REQUIRE(system.eeStateHash() == completedHash);
    }
    REQUIRE(bus.writeData16(0x1a000010, selector | 0x100));
    const auto invalidSelectorHash = system.eeStateHash();
    REQUIRE_FALSE(bus.writeData16(0x1a000000, 0x81));
    REQUIRE(system.eeStateHash() == invalidSelectorHash);
    REQUIRE(bus.writeData16(0x1a000010, selector));
    REQUIRE(system.eeStateHash() == completedHash);
    std::uint16_t value = 0xa5a5;
    REQUIRE_FALSE(bus.readData16(0x1a000000, &value));
    REQUIRE(value == 0xa5a5);
    REQUIRE(bus.readData16(0x1a000010, &value));
    REQUIRE(value == selector);
    REQUIRE(bus.readData16(0x1a000012, &value));
    REQUIRE(value == 0xabcd);
    REQUIRE(bus.writeData16(0x1a000006, 3));
    REQUIRE(bus.readData16(0x1a000006, &value));
    REQUIRE(value == 0);
    REQUIRE(system.eeStateHash() != completedHash);
    REQUIRE(system.eeStateHash() != unselectedHash);
  }
  for (const std::uint16_t selector : {0x100u, 0x17eu, 0x8000u, 0xffffu})
  {
    REQUIRE(bus.writeData16(0x1a000010, selector));
    const auto hash = system.eeStateHash();
    REQUIRE_FALSE(bus.writeData16(0x1a000000, 0x81));
    REQUIRE(system.eeStateHash() == hash);
  }
  for (const auto address : {0x1a000000u, 0x1a000006u})
  {
    const auto hash = system.eeStateHash();
    for (const auto unsupported : {address + 0x20, address + 0x100,
           address | 0x80000000u, address | 0xa0000000u})
    {
      std::uint16_t half = 0xa5a5;
      REQUIRE_FALSE(bus.readData16(unsupported, &half));
      REQUIRE(half == 0xa5a5);
      REQUIRE_FALSE(bus.writeData16(unsupported, 0x81));
    }
    std::uint8_t byte = 0xa5;
    REQUIRE_FALSE(bus.readData8(address, &byte));
    REQUIRE(byte == 0xa5);
    REQUIRE_FALSE(bus.writeData8(address, 0x81));
    std::uint32_t word = UINT32_MAX;
    REQUIRE_FALSE(bus.readData32(address & ~3u, &word));
    REQUIRE(word == UINT32_MAX);
    REQUIRE_FALSE(bus.writeData32(address & ~3u, 0x81));
    REQUIRE_FALSE(bus.readInstruction32(address & ~3u, &word));
    REQUIRE_FALSE(bus.isCacheFillRange(address, 2));
    REQUIRE_THROWS_AS(bus.writeData16(address + 1, 0x81), std::invalid_argument);
    REQUIRE(system.iopBus().read16(address).status == IOPBusStatus::Unmapped);
    REQUIRE(system.iopBus().write16(address, 0x81).status == IOPBusStatus::Unmapped);
    REQUIRE(system.eeStateHash() == hash);
  }
  std::uint16_t value = 0;
  REQUIRE(bus.readData16(0x1a000004, &value));
  REQUIRE(value == 0xcafe);
  REQUIRE(bus.readData16(0x1a000008, &value));
  REQUIRE(value == 0x1357);
  REQUIRE(system.iopStateHash() == iopHash);
  REQUIRE(system.sifStateHash() == sifHash);
  system.reset();
  REQUIRE(system.eeStateHash() == resetHash);
}

TEST_CASE("EE BA0 selector phases have canonical transactional persistence",
  "[ee][bus][save-state][ba0-command]")
{
  unsigned phase = 0;
  SECTION("before command") {}
  SECTION("completed command") { phase = 1; }
  SECTION("acknowledged command") { phase = 2; }
  NekoSystem original;
  REQUIRE(original.eeBus().writeData16(0x1a000002, 0x42));
  REQUIRE(original.eeBus().writeData16(0x1a000010, 0x7e));
  SaveStateMutation expected(original.saveState());
  if (phase != 0)
  {
    REQUIRE(original.eeBus().writeData16(0x1a000000, 0x81));
  }
  if (phase == 2)
  {
    REQUIRE(original.eeBus().writeData16(0x1a000006, 3));
  }
  expected.writeScalar("system.eeBus.ba0SelectorValid", phase != 0);
  expected.writeScalar("system.eeBus.ba0Selector", phase != 0 ? 0x7e : 0);
  expected.writeScalar("system.eeBus.ba0Status06", phase == 1 ? 2 : 0);
  expected.refreshChecksum();
  const auto saved = original.saveState();
  REQUIRE(saved == expected.bytes());
  NekoSystem restored;
  restored.loadState(saved);
  REQUIRE(restored.eeStateHash() == original.eeStateHash());
  REQUIRE(restored.saveState() == saved);
  const auto snapshot = inspectSaveState(saved);
  for (const char *path : {"system.eeBus.ba0SelectorValid",
         "system.eeBus.ba0Selector", "system.eeBus.ba0Status06"})
  {
    REQUIRE(snapshot.find(path) != nullptr);
  }
  REQUIRE(snapshot.find("system.eeBus.ba0SelectorValid")->kind ==
    SaveStateFieldKind::Boolean);
  REQUIRE(snapshot.find("system.eeBus.ba0Selector")->kind == SaveStateFieldKind::U8);
  REQUIRE(snapshot.find("system.eeBus.ba0Status06")->kind == SaveStateFieldKind::U16);

  NekoSystem target;
  REQUIRE(target.eeBus().writeData16(0x1a000002, 0x42));
  REQUIRE(target.eeBus().writeData16(0x1a000010, 0xff));
  REQUIRE(target.eeBus().writeData16(0x1a000000, 0x81));
  const auto before = target.saveState();
  const auto hash = target.eeStateHash();
  for (unsigned corruption = 0; corruption < 7; ++corruption)
  {
    CAPTURE(corruption);
    SaveStateMutation invalid(saved);
    switch (corruption)
    {
      case 0: invalid.writeScalar("system.eeBus.ba0Status06", 1); break;
      case 1: invalid.writeScalar("system.eeBus.ba0Status06", 3); break;
      case 2: invalid.writeScalar("system.eeBus.ba0SelectorValid", 2); break;
      case 3:
        invalid.writeScalar("system.eeBus.ba0SelectorValid", 0);
        invalid.writeScalar("system.eeBus.ba0Selector", 1);
        break;
      case 4:
        invalid.writeScalar("system.eeBus.ba0SelectorValid", 0);
        invalid.writeScalar("system.eeBus.ba0Selector", 0);
        invalid.writeScalar("system.eeBus.ba0Status06", 2);
        break;
      case 5:
        invalid.writeScalar("system.eeBus.ba0SelectorValid", 1);
        invalid.writeScalar("system.eeBus.ba0Control02", 0);
        break;
      case 6: invalid.writeScalar("container.version", 61); break;
    }
    invalid.refreshChecksum();
    REQUIRE_THROWS_AS(target.loadState(invalid.bytes()), std::invalid_argument);
    REQUIRE(target.saveState() == before);
    REQUIRE(target.eeStateHash() == hash);
  }

  SaveStateMutation alternate(saved);
  alternate.writeScalar("system.eeBus.ba0SelectorValid", 1);
  alternate.writeScalar("system.eeBus.ba0Selector", 0);
  alternate.writeScalar("system.eeBus.ba0Status06", 0);
  alternate.refreshChecksum();
  target.loadState(alternate.bytes());
  const auto zeroSelectorHash = target.eeStateHash();
  alternate.writeScalar("system.eeBus.ba0Selector", 1);
  alternate.refreshChecksum();
  target.loadState(alternate.bytes());
  REQUIRE(target.eeStateHash() != zeroSelectorHash);
  alternate.writeScalar("system.eeBus.ba0Selector", 0);
  alternate.writeScalar("system.eeBus.ba0SelectorValid", 0);
  alternate.refreshChecksum();
  target.loadState(alternate.bytes());
  REQUIRE(target.eeStateHash() != zeroSelectorHash);
}

TEST_CASE("EE BA0 device reads use only latched supported selectors",
  "[ee][bus][ba0-read][ba0-read7f]")
{
  std::uint16_t selector = 0x7e;
  SECTION("nonzero compatibility response") {}
  SECTION("zero compatibility response") { selector = 0x7f; }
  const std::uint16_t response = selector == 0x7e ? 0x1c : 0;
  NekoSystem system;
  auto &bus = system.eeBus();
  const auto resetHash = system.eeStateHash();
  const auto iopHash = system.iopStateHash();
  const auto sifHash = system.sifStateHash();
  REQUIRE(bus.writeData16(0x1a000004, 0xcafe));
  REQUIRE(bus.writeData16(0x1a000008, 0x1357));
  for (std::uint32_t index = 0; index < 8; ++index)
  {
    REQUIRE(bus.writeData16(0x1a000010 + index * 2,
      static_cast<std::uint16_t>(0x8000 + index)));
  }
  REQUIRE(bus.writeData16(0x1a000002, 0x43));
  REQUIRE(bus.writeData16(0x1a000010, selector));
  const auto unselectedHash = system.eeStateHash();
  REQUIRE_FALSE(bus.writeData16(0x1a000000, 0x81));
  REQUIRE(system.eeStateHash() == unselectedHash);
  REQUIRE(bus.writeData16(0x1a000002, 0x42));
  REQUIRE(bus.writeData16(0x1a000000, 0x81));
  REQUIRE(bus.writeData16(0x1a000006, 3));
  REQUIRE(bus.writeData16(0x1a000010, 0xffff));
  const auto selectorHash = system.eeStateHash();
  REQUIRE(bus.writeData16(0x1a000002, 0x43));
  REQUIRE(system.eeStateHash() != selectorHash);
  std::uint16_t value = 0;
  REQUIRE(bus.readData16(0x1a000010, &value));
  REQUIRE(value == 0xffff);
  REQUIRE(bus.readData16(0x1a000006, &value));
  REQUIRE(value == 0);
  for (unsigned repeat = 0; repeat < 3; ++repeat)
  {
    REQUIRE(bus.writeData16(0x1a000000, 0x81));
    const auto completedHash = system.eeStateHash();
    for (unsigned poll = 0; poll < 4; ++poll)
    {
      REQUIRE(bus.readData16(0x1a000006, &value));
      REQUIRE(value == 2);
      REQUIRE(bus.readData16(0x1a000010, &value));
      REQUIRE(value == response);
      REQUIRE(system.eeStateHash() == completedHash);
    }
    REQUIRE(bus.writeData16(0x1a000000, 0x81));
    REQUIRE(system.eeStateHash() == completedHash);
    REQUIRE(bus.writeData16(0x1a000002, 0x43));
    REQUIRE(system.eeStateHash() == completedHash);
    for (const std::uint16_t command : {0u, 1u, 0x80u, 0x82u, 0x181u, 0xffffu})
    {
      REQUIRE_FALSE(bus.writeData16(0x1a000000, command));
      REQUIRE(system.eeStateHash() == completedHash);
    }
    REQUIRE(bus.writeData16(0x1a000006, 3));
    REQUIRE(bus.readData16(0x1a000006, &value));
    REQUIRE(value == 0);
    REQUIRE(bus.readData16(0x1a000010, &value));
    REQUIRE(value == response);
    REQUIRE(bus.writeData16(0x1a000002, 0x42));
    REQUIRE(bus.writeData16(0x1a000002, 0x43));
    REQUIRE(bus.readData16(0x1a000006, &value));
    REQUIRE(value == 0);
    REQUIRE(bus.readData16(0x1a000010, &value));
    REQUIRE(value == response);
    REQUIRE(bus.writeData16(0x1a000010, selector == 0x7e ? 0x7f : 0x7e));
  }
  for (std::uint32_t index = 1; index < 8; ++index)
  {
    REQUIRE(bus.readData16(0x1a000010 + index * 2, &value));
    REQUIRE(value == 0x8000 + index);
  }
  REQUIRE(bus.readData16(0x1a000004, &value));
  REQUIRE(value == 0xcafe);
  REQUIRE(bus.readData16(0x1a000008, &value));
  REQUIRE(value == 0x1357);
  REQUIRE(system.iopStateHash() == iopHash);
  REQUIRE(system.sifStateHash() == sifHash);
  system.reset();
  REQUIRE(system.eeStateHash() == resetHash);
}

TEST_CASE("EE BA0 unsupported device reads preserve pending completion and staging",
  "[ee][bus][ba0-read]")
{
  NekoSystem system;
  auto &bus = system.eeBus();
  for (const std::uint16_t selector : {0u, 0x7du, 0x80u, 0xffu})
  {
    CAPTURE(selector);
    REQUIRE(bus.writeData16(0x1a000002, 0x42));
    REQUIRE(bus.writeData16(0x1a000010, selector));
    REQUIRE(bus.writeData16(0x1a000000, 0x81));
    REQUIRE(bus.writeData16(0x1a000002, 0x43));
    REQUIRE(bus.writeData16(0x1a000010, 0x7e));
    for (unsigned acknowledged = 0; acknowledged < 2; ++acknowledged)
    {
      const auto hash = system.eeStateHash();
      REQUIRE_FALSE(bus.writeData16(0x1a000000, 0x81));
      REQUIRE(system.eeStateHash() == hash);
      std::uint16_t value = 0;
      REQUIRE(bus.readData16(0x1a000006, &value));
      REQUIRE(value == (acknowledged == 0 ? 2 : 0));
      REQUIRE(bus.readData16(0x1a000010, &value));
      REQUIRE(value == 0x7e);
      REQUIRE(bus.writeData16(0x1a000006, 3));
    }
  }
}

TEST_CASE("EE BA0 read phases retain canonical transactional state",
  "[ee][bus][save-state][ba0-read][ba0-read7f]")
{
  std::uint16_t selector = 0x7e;
  unsigned phase = 0;
  SECTION("unselected read mode") {}
  SECTION("selected before read command") { phase = 1; }
  SECTION("completed read command") { phase = 2; }
  SECTION("acknowledged read command") { phase = 3; }
  SECTION("overwritten completed result") { phase = 4; }
  SECTION("unselected zero read mode") { selector = 0x7f; }
  SECTION("selected before zero read") { selector = 0x7f; phase = 1; }
  SECTION("completed zero read") { selector = 0x7f; phase = 2; }
  SECTION("acknowledged zero read") { selector = 0x7f; phase = 3; }
  SECTION("overwritten completed zero result") { selector = 0x7f; phase = 4; }
  const std::uint16_t response = selector == 0x7e ? 0x1c : 0;
  NekoSystem original;
  REQUIRE(original.eeBus().writeData16(0x1a000002, 0x42));
  REQUIRE(original.eeBus().writeData16(0x1a000010, selector));
  if (phase != 0)
  {
    REQUIRE(original.eeBus().writeData16(0x1a000000, 0x81));
    REQUIRE(original.eeBus().writeData16(0x1a000006, 3));
  }
  REQUIRE(original.eeBus().writeData16(0x1a000002, 0x43));
  REQUIRE(original.eeBus().writeData16(0x1a000010, 0xffff));
  SaveStateMutation expected(original.saveState());
  if (phase >= 2)
  {
    REQUIRE(original.eeBus().writeData16(0x1a000000, 0x81));
    expected.writeByte("system.eeBus.ba0TransferStaging", 0,
      static_cast<std::uint8_t>(response));
    expected.writeByte("system.eeBus.ba0TransferStaging", 1, 0);
    expected.writeScalar("system.eeBus.ba0Status06", 2);
  }
  if (phase == 3)
  {
    REQUIRE(original.eeBus().writeData16(0x1a000006, 3));
    expected.writeScalar("system.eeBus.ba0Status06", 0);
  }
  if (phase == 4)
  {
    REQUIRE(original.eeBus().writeData16(0x1a000010, 0xffff));
    expected.writeByte("system.eeBus.ba0TransferStaging", 0, 0xff);
    expected.writeByte("system.eeBus.ba0TransferStaging", 1, 0xff);
  }
  expected.refreshChecksum();
  const auto saved = original.saveState();
  REQUIRE(saved == expected.bytes());
  NekoSystem restored;
  restored.loadState(saved);
  REQUIRE(restored.saveState() == saved);
  REQUIRE(restored.eeStateHash() == original.eeStateHash());
  const auto beforeReadHash = restored.eeStateHash();
  REQUIRE(restored.eeBus().writeData16(0x1a000000, 0x81) == (phase != 0));
  std::uint16_t value = 0;
  REQUIRE(restored.eeBus().readData16(0x1a000010, &value));
  REQUIRE(value == (phase == 0 ? 0xffff : response));
  REQUIRE(restored.eeBus().readData16(0x1a000006, &value));
  REQUIRE(value == (phase == 0 ? 0 : 2));
  if (phase == 0 || phase == 2)
  {
    REQUIRE(restored.eeStateHash() == beforeReadHash);
  }
  else
  {
    REQUIRE(restored.eeStateHash() != beforeReadHash);
  }

  const auto before = restored.saveState();
  const auto hash = restored.eeStateHash();
  for (unsigned corruption = 0; corruption < 5; ++corruption)
  {
    CAPTURE(corruption);
    SaveStateMutation invalid(saved);
    switch (corruption)
    {
      case 0: invalid.writeScalar("system.eeBus.ba0Control02", 0x44); break;
      case 1: invalid.writeScalar("system.eeBus.ba0Status06", 1); break;
      case 2:
        invalid.writeScalar("system.eeBus.ba0SelectorValid", 0);
        invalid.writeScalar("system.eeBus.ba0Selector", 0);
        invalid.writeScalar("system.eeBus.ba0Status06", 2);
        break;
      case 3:
        invalid.writeScalar("system.eeBus.ba0SelectorValid", 1);
        invalid.writeScalar("system.eeBus.ba0Control02", 0);
        break;
      case 4: invalid.writeScalar("container.version", 62); break;
    }
    invalid.refreshChecksum();
    REQUIRE_THROWS_AS(restored.loadState(invalid.bytes()), std::invalid_argument);
    REQUIRE(restored.saveState() == before);
    REQUIRE(restored.eeStateHash() == hash);
  }
  SaveStateMutation unknown(saved);
  unknown.writeScalar("system.eeBus.ba0SelectorValid", 1);
  unknown.writeScalar("system.eeBus.ba0Selector", 0x80);
  unknown.writeScalar("system.eeBus.ba0Status06", 2);
  unknown.refreshChecksum();
  restored.loadState(unknown.bytes());
  REQUIRE(restored.saveState() == unknown.bytes());
  const auto unknownHash = restored.eeStateHash();
  REQUIRE_FALSE(restored.eeBus().writeData16(0x1a000000, 0x81));
  REQUIRE(restored.eeStateHash() == unknownHash);
}

TEST_CASE("EE BA0 guest selector and device reads preserve restored traced continuation",
  "[ee][bus][guest][trace][save-state][ba0-command][ba0-read][ba0-read7f]")
{
  std::uint16_t selector = 0x7e;
  unsigned checkpoint = 0;
  SECTION("before command") {}
  SECTION("after command") { checkpoint = 1; }
  SECTION("after acknowledgement") { checkpoint = 3; }
  SECTION("before device read command") { checkpoint = 6; }
  SECTION("after device read command") { checkpoint = 7; }
  SECTION("after device read acknowledgement") { checkpoint = 9; }
  SECTION("before zero selector command") { selector = 0x7f; }
  SECTION("after zero selector command") { selector = 0x7f; checkpoint = 1; }
  SECTION("after zero selector acknowledgement") { selector = 0x7f; checkpoint = 3; }
  SECTION("before zero read command") { selector = 0x7f; checkpoint = 6; }
  SECTION("after zero read command") { selector = 0x7f; checkpoint = 7; }
  SECTION("after zero read acknowledgement") { selector = 0x7f; checkpoint = 9; }
  const std::uint16_t response = selector == 0x7e ? 0x1c : 0;
  NekoSystem original;
  const std::array<std::uint32_t, 12> program = {
    0xa4620000, // SH r2, 0(r3)
    0x94640006, // LHU r4, 6(r3)
    0xa4650006, // SH r5, 6(r3)
    0x94660006, // LHU r6, 6(r3)
    0x94670010, // LHU r7, 16(r3)
    0xa4680002, // SH r8, 2(r3), control43
    0xa4620000, // SH r2, 0(r3), read command81
    0x94690006, // LHU r9, 6(r3)
    0xa4650006, // SH r5, 6(r3), acknowledgement
    0x946a0010, // LHU r10, 16(r3)
    0x946b0006, // LHU r11, 6(r3)
    0xa46c0000  // SH r12, 0(r3), unsupported command82
  };
  const std::array<std::uint16_t, 12> values = {
    0x81, 2, 3, 0, selector, 0x43, 0x81, 2, 3, response, 0, 0x82
  };
  for (std::size_t index = 0; index < program.size(); ++index)
  {
    original.eeBus().write32(static_cast<std::uint32_t>(index * 4), program[index]);
  }
  REQUIRE(original.eeBus().writeData16(0x1a000002, 0x42));
  REQUIRE(original.eeBus().writeData16(0x1a000010, selector));
  auto &core = original.eeCore();
  core.setCOP0Register(EECOP0Register::Status, 0);
  core.setGeneralRegister(2, {0x81, 0});
  core.setGeneralRegister(3, {0xba000000, 0});
  core.setGeneralRegister(5, {3, 0});
  core.setGeneralRegister(8, {0x43, 0});
  core.setGeneralRegister(12, {0x82, 0});
  core.startExecution(EEMemoryMap::KSEG1_BASE);
  for (unsigned index = 0; index < checkpoint; ++index)
  {
    REQUIRE(original.stepEEInstruction(16).pendingException == EEException::None);
  }
  const auto saved = original.saveState();
  NekoSystem restored;
  NekoSystem plain;
  restored.loadState(saved);
  plain.loadState(saved);
  original.startTrace(100);
  restored.startTrace(100);
  for (auto *machine : {&original, &restored, &plain})
  {
    for (unsigned index = checkpoint; index < program.size(); ++index)
    {
      const auto result = machine->stepEEInstruction(16);
      REQUIRE_FALSE(result.cycleLimitReached);
      REQUIRE(result.pendingException ==
        (index == 11 ? EEException::DataBusErrorStore : EEException::None));
    }
    REQUIRE(machine->eeCore().generalRegister(4).low == 2);
    REQUIRE(machine->eeCore().generalRegister(6).low == 0);
    REQUIRE(machine->eeCore().generalRegister(7).low == selector);
    REQUIRE(machine->eeCore().generalRegister(9).low == 2);
    REQUIRE(machine->eeCore().generalRegister(10).low == response);
    REQUIRE(machine->eeCore().generalRegister(11).low == 0);
    REQUIRE(machine->eeCore().cop0Register(EECOP0Register::EPC) ==
      EEMemoryMap::KSEG1_BASE + 44);
  }
  std::size_t accesses = 0;
  for (const auto &event : original.trace())
  {
    if (event.subsystem == NekoTraceSubsystem::EE &&
        event.type == NekoTraceEventType::MemoryAccess &&
        event.value0 >= 0xba000000 && event.value0 <= 0xba000010)
    {
      const auto index = checkpoint + accesses++;
      REQUIRE(event.value1 == values[index]);
      REQUIRE(event.value2 == 0);
      REQUIRE(event.value3 == nekoPackEEMemoryTraceMetadata({
        static_cast<std::uint32_t>(event.value0) & 0x1fffffff, 2,
        (program[index] >> 26) == 0x29
          ? NekoEETraceMemory::AccessKind::DataStore
          : NekoEETraceMemory::AccessKind::DataLoad,
        NekoEETraceMemory::TranslationOutcome::Translated,
        NekoEETraceMemory::CacheRoute::Uncached, 2, false, true,
        NekoEETraceMemory::CacheAccess::Bypassed,
        index == 11 ? NekoEETraceMemory::TransferOutcome::PhysicalBusError
                   : NekoEETraceMemory::TransferOutcome::Completed,
        index == 11 ? NekoEETraceMemory::FailurePhase::Direct
                   : NekoEETraceMemory::FailurePhase::None}));
    }
  }
  REQUIRE(accesses == program.size() - checkpoint);
  REQUIRE_FALSE(original.traceTruncated());
  REQUIRE(original.traceHash() == restored.traceHash());
  REQUIRE(original.eeStateHash() == restored.eeStateHash());
  REQUIRE(original.eeStateHash() == plain.eeStateHash());
  const auto ending = original.saveState();
  REQUIRE(restored.saveState() == ending);
  REQUIRE(plain.saveState() == ending);
}

TEST_CASE("EE BA0 B1 writes retain byte data without changing staging",
  "[ee][bus][ba0-write]")
{
  NekoSystem system;
  auto &bus = system.eeBus();
  const auto resetHash = system.eeStateHash();
  const auto iopHash = system.iopStateHash();
  const auto sifHash = system.sifStateHash();
  REQUIRE(bus.writeData16(0x1a000004, 0xcafe));
  REQUIRE(bus.writeData16(0x1a000008, 0x1357));
  REQUIRE(bus.writeData16(0x1a000002, 0x42));
  for (std::uint32_t index = 2; index < 8; ++index)
  {
    REQUIRE(bus.writeData16(0x1a000010 + index * 2, 0x8000 + index));
  }
  for (const std::uint16_t byte : {0u, 1u, 0x80u, 0xffu})
  {
    CAPTURE(byte);
    REQUIRE(bus.writeData16(0x1a000010, 0xb1));
    REQUIRE(bus.writeData16(0x1a000012, byte));
    SaveStateMutation expected(system.saveState());
    REQUIRE(bus.writeData16(0x1a000000, 0x82));
    expected.writeScalar("system.eeBus.ba0DeviceRegisterB1", byte);
    expected.writeScalar("system.eeBus.ba0SelectorValid", 1);
    expected.writeScalar("system.eeBus.ba0Selector", 0xb1);
    expected.writeScalar("system.eeBus.ba0Status06", 2);
    expected.refreshChecksum();
    REQUIRE(system.saveState() == expected.bytes());
    const auto completedHash = system.eeStateHash();
    for (unsigned repeat = 0; repeat < 3; ++repeat)
    {
      std::uint16_t status = 0xffff;
      REQUIRE(bus.readData16(0x1a000006, &status));
      REQUIRE(status == 2);
      REQUIRE(bus.writeData16(0x1a000000, 0x82));
      REQUIRE(system.eeStateHash() == completedHash);
    }
    REQUIRE(bus.writeData16(0x1a000006, 3));
    REQUIRE(bus.writeData16(0x1a000002, 0x43));
    const auto beforeRead = system.eeStateHash();
    REQUIRE_FALSE(bus.writeData16(0x1a000000, 0x81));
    REQUIRE(system.eeStateHash() == beforeRead);
    for (const std::uint16_t selector : {0x7eu, 0x7fu})
    {
      REQUIRE(bus.writeData16(0x1a000002, 0x42));
      REQUIRE(bus.writeData16(0x1a000010, selector));
      REQUIRE(bus.writeData16(0x1a000000, 0x81));
      REQUIRE(bus.writeData16(0x1a000002, 0x43));
      REQUIRE(bus.writeData16(0x1a000000, 0x81));
      std::uint16_t result = 0xffff;
      REQUIRE(bus.readData16(0x1a000010, &result));
      REQUIRE(result == (selector == 0x7e ? 0x1c : 0));
    }
    REQUIRE(bus.writeData16(0x1a000012, 0xffff));
    REQUIRE(SaveStateMutation(system.saveState()).readByte(
      "system.eeBus.ba0DeviceRegisterB1") == byte);
    REQUIRE(bus.writeData16(0x1a000002, 0x42));
  }
  REQUIRE(system.iopStateHash() == iopHash);
  REQUIRE(system.sifStateHash() == sifHash);
  system.reset();
  REQUIRE(system.eeStateHash() == resetHash);
}

TEST_CASE("EE BA0 malformed B1 writes preserve the complete pending owner",
  "[ee][bus][ba0-write]")
{
  NekoSystem system;
  auto &bus = system.eeBus();
  REQUIRE(bus.writeData16(0x1a000010, 0xb1));
  REQUIRE(bus.writeData16(0x1a000012, 0xa5));
  const auto unselectedHash = system.eeStateHash();
  REQUIRE_FALSE(bus.writeData16(0x1a000000, 0x82));
  REQUIRE(system.eeStateHash() == unselectedHash);
  REQUIRE(bus.writeData16(0x1a000002, 0x42));
  REQUIRE(bus.writeData16(0x1a000000, 0x82));
  for (unsigned acknowledged = 0; acknowledged < 2; ++acknowledged)
  {
    for (const std::uint16_t target : {0u, 0x7eu, 0x7fu, 0xb0u, 0xb2u,
           0x1b1u, 0xb100u, 0xffffu})
    {
      REQUIRE(bus.writeData16(0x1a000010, target));
      const auto hash = system.eeStateHash();
      REQUIRE_FALSE(bus.writeData16(0x1a000000, 0x82));
      REQUIRE(system.eeStateHash() == hash);
    }
    REQUIRE(bus.writeData16(0x1a000010, 0xb1));
    for (const std::uint16_t data : {0x100u, 0x1a5u, 0xff00u, 0xffffu})
    {
      REQUIRE(bus.writeData16(0x1a000012, data));
      const auto hash = system.eeStateHash();
      REQUIRE_FALSE(bus.writeData16(0x1a000000, 0x82));
      REQUIRE(system.eeStateHash() == hash);
    }
    REQUIRE(bus.writeData16(0x1a000012, 0xff));
    for (const std::uint16_t control : {0x42u, 0x43u})
    {
      REQUIRE(bus.writeData16(0x1a000002, control));
      const auto hash = system.eeStateHash();
      for (const std::uint16_t command : {0u, 1u, 0x80u, 0x83u, 0x182u, 0xffffu})
      {
        REQUIRE_FALSE(bus.writeData16(0x1a000000, command));
        REQUIRE(system.eeStateHash() == hash);
      }
      if (control == 0x43)
      {
        REQUIRE_FALSE(bus.writeData16(0x1a000000, 0x82));
        REQUIRE_FALSE(bus.writeData16(0x1a000000, 0x81));
        REQUIRE(system.eeStateHash() == hash);
      }
    }
    REQUIRE(bus.writeData16(0x1a000002, 0x42));
    const auto hash = system.eeStateHash();
    for (const auto address : {0x1a000020u, 0x1a000100u, 0x9a000000u, 0xba000000u})
    {
      REQUIRE_FALSE(bus.writeData16(address, 0x82));
    }
    REQUIRE_FALSE(bus.writeData8(0x1a000000, 0x82));
    REQUIRE_FALSE(bus.writeData32(0x1a000000, 0x82));
    REQUIRE_THROWS_AS(bus.writeData16(0x1a000001, 0x82), std::invalid_argument);
    REQUIRE(system.iopBus().write16(0x1a000000, 0x82).status == IOPBusStatus::Unmapped);
    REQUIRE(system.eeStateHash() == hash);
    REQUIRE(SaveStateMutation(system.saveState()).readByte(
      "system.eeBus.ba0DeviceRegisterB1") == 0xa5);
    std::uint16_t status = 0xffff;
    REQUIRE(bus.readData16(0x1a000006, &status));
    REQUIRE(status == (acknowledged == 0 ? 2 : 0));
    REQUIRE(bus.writeData16(0x1a000006, 3));
  }
}

TEST_CASE("EE BA0 B1 write phases have canonical transactional persistence",
  "[ee][bus][save-state][ba0-write]")
{
  unsigned phase = 0;
  SECTION("before write") {}
  SECTION("pending completion") { phase = 1; }
  SECTION("acknowledged") { phase = 2; }
  SECTION("reselected and overwritten staging") { phase = 3; }
  NekoSystem original;
  auto &bus = original.eeBus();
  REQUIRE(bus.writeData16(0x1a000002, 0x42));
  REQUIRE(bus.writeData16(0x1a000010, 0x7f));
  REQUIRE(bus.writeData16(0x1a000000, 0x81));
  REQUIRE(bus.writeData16(0x1a000006, 3));
  REQUIRE(bus.writeData16(0x1a000010, 0xb1));
  REQUIRE(bus.writeData16(0x1a000012, 0xa5));
  if (phase > 0)
  {
    REQUIRE(bus.writeData16(0x1a000000, 0x82));
  }
  if (phase >= 2)
  {
    REQUIRE(bus.writeData16(0x1a000006, 3));
  }
  if (phase == 3)
  {
    REQUIRE(bus.writeData16(0x1a000010, 0x7e));
    REQUIRE(bus.writeData16(0x1a000000, 0x81));
    REQUIRE(bus.writeData16(0x1a000002, 0x43));
    REQUIRE(bus.writeData16(0x1a000010, 0xffff));
    REQUIRE(bus.writeData16(0x1a000012, 0xffff));
  }
  const auto saved = original.saveState();
  REQUIRE(SaveStateMutation(saved).readByte("system.eeBus.ba0DeviceRegisterB1") ==
    (phase == 0 ? 0 : 0xa5));
  NekoSystem restored;
  restored.loadState(saved);
  REQUIRE(restored.saveState() == saved);
  REQUIRE(restored.eeStateHash() == original.eeStateHash());
  SaveStateMutation changed(saved);
  changed.writeScalar("system.eeBus.ba0DeviceRegisterB1", 0xff);
  changed.refreshChecksum();
  restored.loadState(changed.bytes());
  REQUIRE(restored.saveState() == changed.bytes());
  REQUIRE(restored.eeStateHash() != original.eeStateHash());
  const auto before = restored.saveState();
  const auto hash = restored.eeStateHash();
  for (unsigned corruption = 0; corruption < 4; ++corruption)
  {
    SaveStateMutation invalid(saved);
    switch (corruption)
    {
      case 0:
        invalid.writeScalar("system.eeBus.ba0SelectorValid", 0);
        invalid.writeScalar("system.eeBus.ba0Selector", 0);
        invalid.writeScalar("system.eeBus.ba0Status06", 0);
        invalid.writeScalar("system.eeBus.ba0DeviceRegisterB1", 1);
        break;
      case 1: invalid.writeScalar("system.eeBus.ba0Status06", 1); break;
      case 2: invalid.writeScalar("system.eeBus.ba0Control02", 0); break;
      case 3: invalid.writeScalar("container.version", 63); break;
    }
    invalid.refreshChecksum();
    REQUIRE_THROWS_AS(restored.loadState(invalid.bytes()), std::invalid_argument);
    REQUIRE(restored.saveState() == before);
    REQUIRE(restored.eeStateHash() == hash);
  }
  restored.loadState(saved);
  if (phase < 3)
  {
    REQUIRE(restored.eeBus().writeData16(0x1a000000, 0x82));
    REQUIRE(SaveStateMutation(restored.saveState()).readByte(
      "system.eeBus.ba0DeviceRegisterB1") == 0xa5);
  }
  else
  {
    REQUIRE(restored.eeBus().writeData16(0x1a000000, 0x81));
    REQUIRE(SaveStateMutation(restored.saveState()).readByte(
      "system.eeBus.ba0DeviceRegisterB1") == 0xa5);
  }
}

TEST_CASE("EE BA0 guest B1 writes preserve restored traced continuation",
  "[ee][bus][guest][trace][save-state][ba0-write]")
{
  unsigned checkpoint = 2;
  SECTION("before write command") {}
  SECTION("after write command") { checkpoint = 3; }
  SECTION("after acknowledgement") { checkpoint = 5; }
  SECTION("after reselecting") { checkpoint = 8; }
  const std::array<std::uint32_t, 12> program = {
    0xa4680010, 0xa4690012, 0xa4620000, 0x94640006,
    0xa4650006, 0x94660006, 0xa46a0010, 0xa46b0000,
    0xa46c0002, 0xa46b0000, 0x94670010, 0xa4620000
  };
  const std::array<std::uint16_t, 12> values = {
    0xb1, 0xa5, 0x82, 2, 3, 0, 0x7e, 0x81, 0x43, 0x81, 0x1c, 0x82
  };
  NekoSystem original;
  for (std::size_t index = 0; index < program.size(); ++index)
  {
    original.eeBus().write32(static_cast<std::uint32_t>(index * 4), program[index]);
  }
  REQUIRE(original.eeBus().writeData16(0x1a000002, 0x42));
  REQUIRE(original.eeBus().writeData16(0x1a000010, 0x7f));
  REQUIRE(original.eeBus().writeData16(0x1a000000, 0x81));
  REQUIRE(original.eeBus().writeData16(0x1a000006, 3));
  auto &core = original.eeCore();
  core.setCOP0Register(EECOP0Register::Status, 0);
  for (const auto &entry : std::array<std::array<std::uint32_t, 2>, 9>{{
      {{2, 0x82}}, {{3, 0xba000000}}, {{5, 3}}, {{8, 0xb1}}, {{9, 0xa5}},
      {{10, 0x7e}}, {{11, 0x81}}, {{12, 0x43}}, {{4, 0}}
    }})
  {
    core.setGeneralRegister(entry[0], {entry[1], 0});
  }
  core.startExecution(EEMemoryMap::KSEG1_BASE);
  for (unsigned index = 0; index < checkpoint; ++index)
  {
    REQUIRE(original.stepEEInstruction(16).pendingException == EEException::None);
  }
  const auto saved = original.saveState();
  NekoSystem restored;
  NekoSystem plain;
  restored.loadState(saved);
  plain.loadState(saved);
  original.startTrace(100);
  restored.startTrace(100);
  for (auto *machine : {&original, &restored, &plain})
  {
    for (unsigned index = checkpoint; index < program.size(); ++index)
    {
      const auto result = machine->stepEEInstruction(16);
      REQUIRE_FALSE(result.cycleLimitReached);
      REQUIRE(result.pendingException ==
        (index == 11 ? EEException::DataBusErrorStore : EEException::None));
    }
    REQUIRE(machine->eeCore().generalRegister(4).low == 2);
    REQUIRE(machine->eeCore().generalRegister(6).low == 0);
    REQUIRE(machine->eeCore().generalRegister(7).low == 0x1c);
    REQUIRE(machine->eeCore().cop0Register(EECOP0Register::EPC) ==
      EEMemoryMap::KSEG1_BASE + 44);
    REQUIRE(SaveStateMutation(machine->saveState()).readByte(
      "system.eeBus.ba0DeviceRegisterB1") == 0xa5);
  }
  std::size_t accesses = 0;
  for (const auto &event : original.trace())
  {
    if (event.subsystem == NekoTraceSubsystem::EE &&
        event.type == NekoTraceEventType::MemoryAccess &&
        event.value0 >= 0xba000000 && event.value0 <= 0xba000012)
    {
      const auto index = checkpoint + accesses++;
      REQUIRE(event.value1 == values[index]);
      REQUIRE(event.value2 == 0);
      REQUIRE(event.value3 == nekoPackEEMemoryTraceMetadata({
        static_cast<std::uint32_t>(event.value0) & 0x1fffffff, 2,
        (program[index] >> 26) == 0x29
          ? NekoEETraceMemory::AccessKind::DataStore
          : NekoEETraceMemory::AccessKind::DataLoad,
        NekoEETraceMemory::TranslationOutcome::Translated,
        NekoEETraceMemory::CacheRoute::Uncached, 2, false, true,
        NekoEETraceMemory::CacheAccess::Bypassed,
        index == 11 ? NekoEETraceMemory::TransferOutcome::PhysicalBusError
                   : NekoEETraceMemory::TransferOutcome::Completed,
        index == 11 ? NekoEETraceMemory::FailurePhase::Direct
                   : NekoEETraceMemory::FailurePhase::None}));
    }
  }
  REQUIRE(accesses == program.size() - checkpoint);
  REQUIRE_FALSE(original.traceTruncated());
  REQUIRE(original.traceHash() == restored.traceHash());
  REQUIRE(original.eeStateHash() == restored.eeStateHash());
  REQUIRE(original.eeStateHash() == plain.eeStateHash());
  const auto ending = original.saveState();
  REQUIRE(restored.saveState() == ending);
  REQUIRE(plain.saveState() == ending);
}

TEST_CASE("EE BA0 control selection retains only supported transfer modes",
  "[ee][bus][ba0-control]")
{
  constexpr std::uint32_t ADDRESS = 0x1a000002;
  NekoSystem system;
  auto &bus = system.eeBus();
  const auto resetHash = system.eeStateHash();
  REQUIRE(bus.writeData16(0x1a000004, 0x1234));
  REQUIRE(bus.writeData16(0x1a000008, 0xabcd));
  const auto unselectedHash = system.eeStateHash();
  const auto iopHash = system.iopStateHash();
  const auto sifHash = system.sifStateHash();
  REQUIRE(bus.writeData16(ADDRESS, 0x42));
  const auto selectedHash = system.eeStateHash();
  REQUIRE(selectedHash != unselectedHash);
  REQUIRE(bus.writeData16(ADDRESS, 0x42));
  REQUIRE(bus.writeData16(0x1a000006, 3));
  for (unsigned bit = 1; bit < 16; ++bit)
  {
    CAPTURE(bit);
    REQUIRE_FALSE(bus.writeData16(ADDRESS,
      static_cast<std::uint16_t>(0x42u ^ (1u << bit))));
  }
  REQUIRE_FALSE(bus.writeData16(ADDRESS, 0));
  std::uint16_t half = 0xa5a5;
  for (const auto address : {ADDRESS, 0x1a000000u, 0x1a00000au,
         0x1a000102u, 0x9a000002u, 0xba000002u})
  {
    CAPTURE(address);
    REQUIRE_FALSE(bus.readData16(address, &half));
    REQUIRE(half == 0xa5a5);
    if (address != ADDRESS)
    {
      REQUIRE_FALSE(bus.writeData16(address, 0x42));
    }
  }
  REQUIRE_FALSE(bus.writeData16(0x1a000000, 0x80));
  for (const auto address : {ADDRESS, ADDRESS + 1})
  {
    std::uint8_t byte = 0xa5;
    REQUIRE_FALSE(bus.readData8(address, &byte));
    REQUIRE(byte == 0xa5);
    REQUIRE_FALSE(bus.writeData8(address, 0x42));
  }
  std::uint32_t word = 0xa5a5a5a5;
  std::uint64_t wide = UINT64_MAX;
  EEQuadword quad{UINT64_MAX, UINT64_MAX};
  REQUIRE_FALSE(bus.readData32(0x1a000000, &word));
  REQUIRE(word == 0xa5a5a5a5);
  REQUIRE_FALSE(bus.writeData32(0x1a000000, 0x00420000));
  REQUIRE_FALSE(bus.readData64(0x1a000000, &wide));
  REQUIRE(wide == UINT64_MAX);
  REQUIRE_FALSE(bus.writeData64(0x1a000000, UINT64_C(0x42) << 16));
  REQUIRE_FALSE(bus.readData128(0x1a000000, &quad));
  REQUIRE(quad.low == UINT64_MAX);
  REQUIRE(quad.high == UINT64_MAX);
  REQUIRE_FALSE(bus.writeData128(0x1a000000, {UINT64_C(0x42) << 16, 0}));
  REQUIRE_FALSE(bus.readInstruction32(0x1a000000, &word));
  REQUIRE_FALSE(bus.isCacheFillRange(ADDRESS, 2));
  REQUIRE_FALSE(bus.readDMAC128(0x1a000000, &quad));
  REQUIRE_FALSE(bus.writeDMAC128(0x1a000000, {UINT64_C(0x42) << 16, 0}));
  REQUIRE_THROWS_AS(bus.readData16(ADDRESS, nullptr), std::invalid_argument);
  REQUIRE_THROWS_AS(bus.writeData16(ADDRESS + 1, 0x42), std::invalid_argument);
  REQUIRE(system.iopBus().read16(ADDRESS).status == IOPBusStatus::Unmapped);
  REQUIRE(system.iopBus().write16(ADDRESS, 0x42).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.readData16(0x1a000004, &half));
  REQUIRE(half == 0x1234);
  REQUIRE(bus.readData16(0x1a000008, &half));
  REQUIRE(half == 0xabcd);
  REQUIRE(system.eeStateHash() == selectedHash);
  REQUIRE(system.iopStateHash() == iopHash);
  REQUIRE(system.sifStateHash() == sifHash);
  system.reset();
  REQUIRE(system.eeStateHash() == resetHash);
}

TEST_CASE("EE BA0 control selection participates in atomic canonical persistence",
  "[ee][bus][save-state][ba0-control]")
{
  constexpr const char *CONTROL = "system.eeBus.ba0Control02";
  NekoSystem original;
  SaveStateMutation expected(original.saveState());
  REQUIRE(original.eeBus().writeData16(0x1a000002, 0x42));
  REQUIRE(original.eeBus().writeData16(0x1a000004, 0xcafe));
  REQUIRE(original.eeBus().writeData16(0x1a000008, 0x1357));
  expected.writeScalar(CONTROL, 0x42);
  expected.writeScalar("system.eeBus.ba0Register04", 0xcafe);
  expected.writeScalar("system.eeBus.ba0Register08", 0x1357);
  expected.refreshChecksum();
  const auto saved = original.saveState();
  REQUIRE(saved == expected.bytes());
  NekoSystem restored;
  restored.loadState(saved);
  REQUIRE(restored.eeStateHash() == original.eeStateHash());
  REQUIRE(restored.eeBus().writeData16(0x1a000006, 3));
  REQUIRE(restored.eeBus().writeData16(0x1a000002, 0x42));
  REQUIRE(restored.saveState() == saved);

  NekoSystem target;
  REQUIRE(target.eeBus().writeData16(0x1a000004, 0x1234));
  REQUIRE(target.eeBus().writeData16(0x1a000008, 0x2468));
  const auto before = target.saveState();
  const auto hash = target.eeStateHash();
  struct Corruption
  {
    const char *path;
    std::uint16_t value;
  };
  for (const auto mutation : {
         Corruption{CONTROL, 1}, Corruption{CONTROL, 0x40},
         Corruption{CONTROL, 0x44}, Corruption{CONTROL, 0xffff},
         Corruption{"container.version", 59},
         Corruption{"system.eeRDRAMInitialization.enumeratedDevices", 3}})
  {
    CAPTURE(mutation.path);
    CAPTURE(mutation.value);
    SaveStateMutation invalid(saved);
    invalid.writeScalar(mutation.path, mutation.value);
    invalid.refreshChecksum();
    REQUIRE_THROWS_AS(target.loadState(invalid.bytes()), std::invalid_argument);
    REQUIRE(target.saveState() == before);
    REQUIRE(target.eeStateHash() == hash);
  }
  target.loadState(before);
  REQUIRE(target.saveState() == before);
}

TEST_CASE("EE BA0 halfword guest stores and loads preserve traced continuation",
  "[ee][bus][guest][trace][save-state][ba0][ba0-staging]")
{
  std::uint32_t address = 0x1a000008;
  std::uint16_t written = 3;
  SECTION("offset08") {}
  SECTION("offset04")
  {
    address = 0x1a000004;
    written = 1;
  }
  SECTION("offset04 signed load")
  {
    address = 0x1a000004;
    written = 0x8001;
  }
  SECTION("first staging halfword")
  {
    address = 0x1a000010;
    written = 0x7e;
  }
  SECTION("last staging halfword")
  {
    address = 0x1a00001e;
    written = 0x8001;
  }
  NekoSystem original;
  const std::uint32_t program[] = {
    0xa4620000, // SH r2, 0(r3)
    0x94640000, // LHU r4, 0(r3)
    0x84650000  // LH r5, 0(r3)
  };
  for (std::size_t index = 0; index < 3; ++index)
  {
    original.eeBus().write32(static_cast<std::uint32_t>(index * 4),
      program[index]);
  }
  original.eeCore().setCOP0Register(EECOP0Register::Status, 0);
  original.eeCore().setGeneralRegister(3,
    {address | EEMemoryMap::KSEG1_BASE, 0});
  original.eeCore().setGeneralRegister(2, {written, 0});
  original.eeCore().startExecution(EEMemoryMap::KSEG1_BASE);
  REQUIRE(original.eeBus().writeData16(EEMemoryMap::BA0_CONTROL_02, 0x42));
  const auto saved = original.saveState();
  NekoSystem restored;
  NekoSystem plain;
  restored.loadState(saved);
  plain.loadState(saved);
  original.startTrace(100);
  restored.startTrace(100);
  for (auto *machine : {&original, &restored, &plain})
  {
    for (unsigned instruction = 0; instruction < 3; ++instruction)
    {
      const auto result = machine->stepEEInstruction(16);
      REQUIRE(result.pendingException == EEException::None);
      REQUIRE_FALSE(result.cycleLimitReached);
    }
    REQUIRE(machine->eeCore().generalRegister(4).low == written);
    const std::uint64_t signedValue = written == 0x8001
      ? UINT64_C(0xffffffffffff8001) : written;
    REQUIRE(machine->eeCore().generalRegister(5).low == signedValue);
    std::uint16_t value = 0;
    REQUIRE(machine->eeBus().readData16(address, &value));
    REQUIRE(value == written);
  }
  REQUIRE(original.traceHash() == restored.traceHash());
  REQUIRE(original.trace().size() == restored.trace().size());
  std::size_t accesses = 0;
  for (const auto &event : original.trace())
  {
    if (event.subsystem == NekoTraceSubsystem::EE &&
        event.type == NekoTraceEventType::MemoryAccess &&
        event.value0 == (address | EEMemoryMap::KSEG1_BASE))
    {
      const bool store = accesses++ == 0;
      REQUIRE(event.value1 == written);
      REQUIRE(event.value2 == 0);
      REQUIRE(event.value3 == nekoPackEEMemoryTraceMetadata({
        address, 2, store ? NekoEETraceMemory::AccessKind::DataStore
                          : NekoEETraceMemory::AccessKind::DataLoad,
        NekoEETraceMemory::TranslationOutcome::Translated,
        NekoEETraceMemory::CacheRoute::Uncached, 2, false, true,
        NekoEETraceMemory::CacheAccess::Bypassed,
        NekoEETraceMemory::TransferOutcome::Completed,
        NekoEETraceMemory::FailurePhase::None}));
    }
  }
  REQUIRE(accesses == 3);
  REQUIRE_FALSE(original.traceTruncated());
  REQUIRE(original.eeStateHash() == restored.eeStateHash());
  REQUIRE(original.eeStateHash() == plain.eeStateHash());
  const auto ending = original.saveState();
  REQUIRE(restored.saveState() == ending);
  REQUIRE(plain.saveState() == ending);
}

TEST_CASE("EE BA0 retained halfwords participate in atomic canonical persistence",
  "[ee][bus][save-state][ba0]")
{
  std::uint32_t address = 0;
  const char *field = nullptr;
  SECTION("offset04")
  {
    address = 0x1a000004;
    field = "system.eeBus.ba0Register04";
  }
  SECTION("offset08")
  {
    address = 0x1a000008;
    field = "system.eeBus.ba0Register08";
  }
  const std::uint32_t otherAddress =
    address == 0x1a000004 ? 0x1a000008 : 0x1a000004;
  const char *otherField = address == 0x1a000004
    ? "system.eeBus.ba0Register08" : "system.eeBus.ba0Register04";
  NekoSystem original;
  SaveStateMutation expected(original.saveState());
  REQUIRE(original.eeBus().writeData16(address, 0xcafe));
  REQUIRE(original.eeBus().writeData16(otherAddress, 0x1357));
  expected.writeScalar(field, 0xcafe);
  expected.writeScalar(otherField, 0x1357);
  expected.refreshChecksum();
  const auto saved = original.saveState();
  REQUIRE(saved == expected.bytes());
  const auto hash = original.eeStateHash();
  NekoSystem restored;
  REQUIRE(restored.eeBus().writeData16(address, 3));
  restored.loadState(saved);
  REQUIRE(restored.saveState() == saved);
  REQUIRE(restored.eeStateHash() == hash);
  std::uint16_t half = 0;
  REQUIRE(restored.eeBus().readData16(address, &half));
  REQUIRE(half == 0xcafe);
  REQUIRE(restored.eeBus().readData16(otherAddress, &half));
  REQUIRE(half == 0x1357);

  REQUIRE(restored.eeBus().writeData16(address, 0x1234));
  REQUIRE(restored.eeBus().writeData16(otherAddress, 0x2468));
  const auto before = restored.saveState();
  const auto beforeHash = restored.eeStateHash();
  struct Corruption
  {
    const char *path;
    std::uint32_t value;
  };
  for (const auto mutation : {
         Corruption{"container.version", 59},
         Corruption{"system.eeRDRAMInitialization.enumeratedDevices", 3}})
  {
    CAPTURE(mutation.path);
    SaveStateMutation invalid(saved);
    invalid.writeScalar(mutation.path, mutation.value);
    invalid.refreshChecksum();
    REQUIRE_THROWS_AS(restored.loadState(invalid.bytes()), std::invalid_argument);
    REQUIRE(restored.saveState() == before);
    REQUIRE(restored.eeStateHash() == beforeHash);
    REQUIRE(restored.eeBus().readData16(address, &half));
    REQUIRE(half == 0x1234);
    REQUIRE(restored.eeBus().readData16(otherAddress, &half));
    REQUIRE(half == 0x2468);
  }
}

TEST_CASE("EE BA0 staging halfwords retain independent values without active effects",
  "[ee][bus][ba0-staging]")
{
  NekoSystem system;
  auto &bus = system.eeBus();
  const auto resetHash = system.eeStateHash();
  REQUIRE(bus.writeData16(0x1a000002, 0x42));
  REQUIRE(bus.writeData16(0x1a000004, 0xcafe));
  REQUIRE(bus.writeData16(0x1a000008, 0x1357));
  const auto iopHash = system.iopStateHash();
  const auto sifHash = system.sifStateHash();
  std::uint16_t half = 0xa5a5;
  for (std::uint32_t index = 0; index < 8; ++index)
  {
    const auto address = 0x1a000010 + index * 2;
    CAPTURE(address);
    REQUIRE(bus.readData16(address, &half));
    REQUIRE(half == 0);
    const auto before = system.eeStateHash();
    for (const std::uint16_t value : {0u, 0x7eu, 0x8001u, 0xffffu})
    {
      REQUIRE(bus.writeData16(address, value));
      REQUIRE(bus.readData16(address, &half));
      REQUIRE(half == value);
    }
    REQUIRE(bus.writeData16(address, static_cast<std::uint16_t>(0xa500 + index)));
    REQUIRE(system.eeStateHash() != before);
    std::uint8_t byte = 0xa5;
    REQUIRE_FALSE(bus.readData8(address, &byte));
    REQUIRE(byte == 0xa5);
    REQUIRE_FALSE(bus.writeData8(address, 0x7e));
    REQUIRE_FALSE(bus.writeData8(address + 1, 0));
    REQUIRE_THROWS_AS(bus.writeData16(address + 1, 0x7e), std::invalid_argument);
  }
  const auto hash = system.eeStateHash();
  REQUIRE(bus.writeData16(0x1a000006, 3));
  REQUIRE(bus.writeData16(0x1a000002, 0x42));
  for (const auto address : {0x1a000000u, 0x1a00000au, 0x1a00000cu,
         0x1a00000eu, 0x1a000020u, 0x1a000022u, 0x1a000110u,
         0x9a000010u, 0xba000010u})
  {
    CAPTURE(address);
    half = 0xa5a5;
    REQUIRE_FALSE(bus.readData16(address, &half));
    REQUIRE(half == 0xa5a5);
    REQUIRE_FALSE(bus.writeData16(address, 0x7e));
  }
  std::uint32_t word = UINT32_MAX;
  std::uint64_t wide = UINT64_MAX;
  EEQuadword quad{UINT64_MAX, UINT64_MAX};
  REQUIRE_FALSE(bus.readData32(0x1a000010, &word));
  REQUIRE(word == UINT32_MAX);
  REQUIRE_FALSE(bus.writeData32(0x1a000010, 0x1234007e));
  REQUIRE_FALSE(bus.readData64(0x1a000010, &wide));
  REQUIRE(wide == UINT64_MAX);
  REQUIRE_FALSE(bus.writeData64(0x1a000010, UINT64_MAX));
  REQUIRE_FALSE(bus.readData128(0x1a000010, &quad));
  REQUIRE(quad.low == UINT64_MAX);
  REQUIRE(quad.high == UINT64_MAX);
  REQUIRE_FALSE(bus.writeData128(0x1a000010, quad));
  REQUIRE_FALSE(bus.readInstruction32(0x1a000010, &word));
  REQUIRE_FALSE(bus.isCacheFillRange(0x1a000010, 16));
  REQUIRE_FALSE(bus.readDMAC128(0x1a000010, &quad));
  REQUIRE_FALSE(bus.writeDMAC128(0x1a000010, quad));
  REQUIRE(system.iopBus().read16(0x1a000010).status == IOPBusStatus::Unmapped);
  REQUIRE(system.iopBus().write16(0x1a000010, 0x7e).status == IOPBusStatus::Unmapped);
  for (std::uint32_t index = 0; index < 8; ++index)
  {
    REQUIRE(bus.readData16(0x1a000010 + index * 2, &half));
    REQUIRE(half == 0xa500 + index);
  }
  REQUIRE(bus.readData16(0x1a000004, &half));
  REQUIRE(half == 0xcafe);
  REQUIRE(bus.readData16(0x1a000008, &half));
  REQUIRE(half == 0x1357);
  REQUIRE(system.eeStateHash() == hash);
  REQUIRE(system.iopStateHash() == iopHash);
  REQUIRE(system.sifStateHash() == sifHash);
  system.reset();
  REQUIRE(system.eeStateHash() == resetHash);
  for (std::uint32_t index = 0; index < 8; ++index)
  {
    REQUIRE(system.eeBus().readData16(0x1a000010 + index * 2, &half));
    REQUIRE(half == 0);
  }
}

TEST_CASE("EE BA0 staging has canonical little-endian transactional persistence",
  "[ee][bus][save-state][ba0-staging]")
{
  constexpr const char *FIELD = "system.eeBus.ba0TransferStaging";
  NekoSystem original;
  SaveStateMutation expected(original.saveState());
  for (std::uint32_t index = 0; index < 8; ++index)
  {
    const auto value = static_cast<std::uint16_t>(0x807e + index * 0x101);
    REQUIRE(original.eeBus().writeData16(0x1a000010 + index * 2, value));
    expected.writeByte(FIELD, index * 2, static_cast<std::uint8_t>(value));
    expected.writeByte(FIELD, index * 2 + 1, static_cast<std::uint8_t>(value >> 8));
  }
  expected.refreshChecksum();
  const auto saved = original.saveState();
  REQUIRE(saved == expected.bytes());
  NekoSystem restored;
  restored.loadState(saved);
  REQUIRE(restored.saveState() == saved);
  REQUIRE(restored.eeStateHash() == original.eeStateHash());
  for (std::uint32_t index = 0; index < 8; ++index)
  {
    std::uint16_t value = 0;
    REQUIRE(restored.eeBus().readData16(0x1a000010 + index * 2, &value));
    REQUIRE(value == 0x807e + index * 0x101);
    REQUIRE(restored.eeBus().writeData16(0x1a000010 + index * 2, 0x1234));
  }
  const auto before = restored.saveState();
  const auto hash = restored.eeStateHash();
  for (const char *path : {"container.version", "system.eeBus.ba0Control02",
         "system.eeRDRAMInitialization.enumeratedDevices"})
  {
    CAPTURE(path);
    SaveStateMutation invalid(saved);
    invalid.writeScalar(path, std::string(path) == "container.version" ? 60 : 3);
    invalid.refreshChecksum();
    REQUIRE_THROWS_AS(restored.loadState(invalid.bytes()), std::invalid_argument);
    REQUIRE(restored.saveState() == before);
    REQUIRE(restored.eeStateHash() == hash);
  }
}

TEST_CASE("EE retail IOP boot probe is an exact stateless byte read",
  "[ee][bus][boot-probe][save-state]")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();
  const auto state = system.saveState();
  std::uint8_t byte = 0xa5;
  REQUIRE(bus.readData8(0x1f803204, &byte));
  REQUIRE(byte == 0);
  REQUIRE_THROWS_AS(bus.readData8(0x1f803204, nullptr), std::invalid_argument);
  REQUIRE_FALSE(bus.writeData8(0x1f803204, 0));
  for (std::uint32_t address : {0x1f803203u, 0x1f803205u, 0x1f803200u,
       0x1f803208u, 0x9f803204u, 0xbf803204u})
  {
    CAPTURE(address);
    byte = 0xa5;
    REQUIRE_FALSE(bus.readData8(address, &byte));
    REQUIRE(byte == 0xa5);
    REQUIRE_FALSE(bus.writeData8(address, 0));
  }
  std::uint16_t halfword = 0xa5a5;
  std::uint32_t word = 0xa5a5a5a5;
  std::uint64_t doubleword = UINT64_MAX;
  EEQuadword quadword{};
  REQUIRE_FALSE(bus.readData16(0x1f803204, &halfword));
  REQUIRE(halfword == 0xa5a5);
  REQUIRE_FALSE(bus.readData32(0x1f803204, &word));
  REQUIRE(word == 0xa5a5a5a5);
  REQUIRE_FALSE(bus.readData64(0x1f803200, &doubleword));
  REQUIRE(doubleword == UINT64_MAX);
  REQUIRE_FALSE(bus.readData128(0x1f803200, &quadword));
  REQUIRE_FALSE(bus.writeData16(0x1f803204, 0));
  REQUIRE_FALSE(bus.writeData32(0x1f803204, 0));
  REQUIRE_FALSE(bus.writeData64(0x1f803200, 0));
  REQUIRE_FALSE(bus.writeData128(0x1f803200, quadword));
  REQUIRE_FALSE(bus.readInstruction32(0x1f803204, &word));
  REQUIRE_FALSE(bus.isCacheFillRange(0x1f803204, 1));
  REQUIRE_FALSE(bus.readDMAC128(0x1f803200, &quadword));
  REQUIRE(system.saveState() == state);
}

TEST_CASE("EE absent DEV9 revision is an exact stateless byte read",
  "[ee][bus][dev9-revision]")
{
  constexpr std::uint32_t ADDRESS = 0x1f80146e;
  NekoSystem system;
  EEBus &bus = system.eeBus();
  const auto eeHash = system.eeStateHash();
  const auto iopHash = system.iopStateHash();
  const auto sifHash = system.sifStateHash();
  for (unsigned repeat = 0; repeat < 2; ++repeat)
  {
    std::uint8_t byte = 0xa5;
    REQUIRE(bus.readData8(ADDRESS, &byte));
    REQUIRE(byte == 0);
  }
  REQUIRE_THROWS_AS(bus.readData8(ADDRESS, nullptr), std::invalid_argument);
  for (std::uint32_t address : {ADDRESS, 0x1f80146du, 0x1f80146fu,
       0x1f801470u, 0x9f80146eu, 0xbf80146eu})
  {
    CAPTURE(address);
    if (address != ADDRESS)
    {
      std::uint8_t byte = 0xa5;
      REQUIRE_FALSE(bus.readData8(address, &byte));
      REQUIRE(byte == 0xa5);
    }
    REQUIRE_FALSE(bus.writeData8(address, 0));
    REQUIRE_FALSE(bus.writeData8(address, 0x32));
  }
  std::uint16_t halfword = 0xa5a5;
  std::uint32_t word = 0xa5a5a5a5;
  std::uint64_t doubleword = UINT64_MAX;
  EEQuadword quadword{UINT64_MAX, UINT64_MAX};
  REQUIRE_FALSE(bus.readData16(ADDRESS, &halfword));
  REQUIRE(halfword == 0xa5a5);
  REQUIRE_FALSE(bus.readData32(0x1f80146c, &word));
  REQUIRE(word == 0xa5a5a5a5);
  REQUIRE_FALSE(bus.readData64(0x1f801468, &doubleword));
  REQUIRE(doubleword == UINT64_MAX);
  REQUIRE_FALSE(bus.readData128(0x1f801460, &quadword));
  REQUIRE(quadword.low == UINT64_MAX);
  REQUIRE(quadword.high == UINT64_MAX);
  REQUIRE_FALSE(bus.writeData16(ADDRESS, 0));
  REQUIRE_FALSE(bus.writeData32(0x1f80146c, 0));
  REQUIRE_FALSE(bus.writeData64(0x1f801468, 0));
  REQUIRE_FALSE(bus.writeData128(0x1f801460, quadword));
  REQUIRE_FALSE(bus.readInstruction32(0x1f80146c, &word));
  REQUIRE_FALSE(bus.isCacheFillRange(ADDRESS, 1));
  REQUIRE_FALSE(bus.readDMAC128(0x1f801460, &quadword));
  REQUIRE_FALSE(bus.writeDMAC128(0x1f801460, quadword));
  REQUIRE(system.iopBus().read8(ADDRESS).status == IOPBusStatus::Unmapped);
  REQUIRE(system.iopBus().read16(ADDRESS).status == IOPBusStatus::Unmapped);
  REQUIRE(system.iopBus().write8(ADDRESS, 0).status == IOPBusStatus::Unmapped);
  REQUIRE(system.iopBus().readDMAC32(0x1f80146c).status == IOPBusStatus::Unmapped);
  REQUIRE(system.eeStateHash() == eeHash);
  REQUIRE(system.iopStateHash() == iopHash);
  REQUIRE(system.sifStateHash() == sifHash);
}

TEST_CASE("EE MCH window endpoint supports word-backed access and guest loads",
  "[ee][bus][guest][trace][save-state][mch-window]")
{
  constexpr std::uint32_t ADDRESS = 0x1000f400;
  constexpr std::uint32_t END = 0x1000f500;
  constexpr std::uint32_t WRITTEN = 0x87654321;
  NekoSystem system;
  EEBus &bus = system.eeBus();
  const auto before = system.saveState();

  std::uint32_t word = UINT32_MAX;
  REQUIRE(bus.readData32(ADDRESS, &word));
  REQUIRE(word == 0);
  REQUIRE(bus.writeData32(ADDRESS, WRITTEN));
  REQUIRE(bus.readData32(ADDRESS, &word));
  REQUIRE(word == WRITTEN);
  REQUIRE(system.saveState() != before);

  std::uint8_t byte = 0xa5;
  std::uint16_t halfword = 0xa5a5;
  std::uint64_t doubleword = UINT64_MAX;
  EEQuadword quadword{};
  REQUIRE_FALSE(bus.readData8(ADDRESS, &byte));
  REQUIRE(bus.readData16(ADDRESS, &halfword));
  REQUIRE(halfword == 0x4321);
  REQUIRE_FALSE(bus.readData64(ADDRESS, &doubleword));
  REQUIRE_FALSE(bus.readData128(ADDRESS, &quadword));
  REQUIRE(bus.readData32(ADDRESS + 4, &word));
  REQUIRE(word == 0);
  word = UINT32_C(0x12345678);
  REQUIRE_FALSE(bus.readData32(END, &word));
  REQUIRE(bus.writeData32(END, WRITTEN));
  REQUIRE(word == UINT32_C(0x12345678));

  const auto result = guestLoad(system, ADDRESS);
  REQUIRE(result.pendingException == EEException::None);
  REQUIRE_FALSE(result.cycleLimitReached);
  REQUIRE(
    system.eeCore().generalRegister(2).low == UINT64_C(0xffffffff87654321));
  REQUIRE(system.eeCore().generalRegister(2).high == UINT32_MAX);

  std::size_t accesses = 0;
  for (const auto &event : system.trace())
  {
    if (event.subsystem == NekoTraceSubsystem::EE &&
        event.type == NekoTraceEventType::MemoryAccess &&
        event.value0 == (ADDRESS | EEMemoryMap::KSEG1_BASE))
    {
      ++accesses;
      REQUIRE(event.value1 == WRITTEN);
      REQUIRE(event.value3 == nekoPackEEMemoryTraceMetadata({
        ADDRESS, 4, NekoEETraceMemory::AccessKind::DataLoad,
        NekoEETraceMemory::TranslationOutcome::Translated,
        NekoEETraceMemory::CacheRoute::Uncached, 2, false, true,
        NekoEETraceMemory::CacheAccess::Bypassed,
        NekoEETraceMemory::TransferOutcome::Completed,
        NekoEETraceMemory::FailurePhase::None
      }));
    }
  }
  REQUIRE(accesses == 1);
  REQUIRE_FALSE(system.traceTruncated());
}

TEST_CASE("EE MCH halfword reads select backing lanes without protocol effects",
  "[ee][bus][mch-window][mch-halfword]")
{
  constexpr std::uint32_t BASE = EEMemoryMap::MCH_WINDOW_BASE;
  constexpr std::uint32_t END = EEMemoryMap::MCH_WINDOW_END;
  NekoSystem system;
  auto &bus = system.eeBus();
  REQUIRE(bus.writeData32(EEMemoryMap::MCH_RICM, 0x80210000));
  for (auto address = BASE; address < END; address += 4)
  {
    if (address == EEMemoryMap::MCH_RICM ||
        address == EEMemoryMap::MCH_DRD)
    {
      continue;
    }
    CAPTURE(address);
    for (const auto lane : {0u, 2u})
    {
      std::uint16_t value = 0xa5a5;
      REQUIRE(bus.readData16(address + lane, &value));
      REQUIRE(value == 0);
    }
    REQUIRE(bus.writeData32(address, 0xa135c46du ^ (address - BASE)));
  }
  const auto hash = system.eeStateHash();
  for (auto address = BASE; address < END; address += 4)
  {
    for (const auto lane : {0u, 2u})
    {
      CAPTURE(address);
      CAPTURE(lane);
      std::uint16_t value = 0xa5a5;
      const bool protocol = address == EEMemoryMap::MCH_RICM ||
        address == EEMemoryMap::MCH_DRD;
      REQUIRE(bus.readData16(address + lane, &value) == !protocol);
      const auto word = address == BASE + 0x10 ? 0u :
        0xa135c46du ^ (address - BASE);
      REQUIRE(value == (protocol ? 0xa5a5 :
        static_cast<std::uint16_t>(word >> (lane * 8))));
      REQUIRE_FALSE(bus.writeData16(address + lane, 0xffff));
    }
  }
  for (const auto address : {BASE - 2, END, END + 2,
         0x9000f480u, 0xb000f480u})
  {
    CAPTURE(address);
    std::uint16_t value = 0xa5a5;
    REQUIRE_FALSE(bus.readData16(address, &value));
    REQUIRE(value == 0xa5a5);
  }
  for (const auto address : {BASE + 1, BASE + 0x81, END - 1})
  {
    CAPTURE(address);
    std::uint16_t value = 0xa5a5;
    REQUIRE_THROWS_AS(bus.readData16(address, &value), std::invalid_argument);
    REQUIRE(value == 0xa5a5);
  }
  REQUIRE_THROWS_AS(bus.readData16(BASE, nullptr), std::invalid_argument);
  REQUIRE(system.eeStateHash() == hash);
  REQUIRE(bus.read32(EEMemoryMap::MCH_DRD) == 0x1f);
  REQUIRE(bus.read32(EEMemoryMap::MCH_DRD) == 0x1f);
  REQUIRE(bus.read32(EEMemoryMap::MCH_DRD) == 0);
}

TEST_CASE("GS PCRTC initialization writes preserve configured display timing",
  "[ee][gs][pcrtc-init][save-state]")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();
  GSDisplay &display = system.gsDisplay();
  display.clock();
  const auto state = system.saveState();
  for (std::uint8_t index : {0x01, 0x03, 0x04, 0x05, 0x06})
  {
    const std::uint32_t address = 0x12000000 + index * 16;
    CAPTURE(index);
    for (std::uint64_t value : {UINT64_C(0), UINT64_C(31147115780), UINT64_MAX})
    {
      CAPTURE(value);
      REQUIRE(bus.writeData64(address, value));
      REQUIRE_NOTHROW(display.writePrivilegedRegister(index, value));
    }
    std::uint8_t byte = 0xa5;
    std::uint16_t halfword = 0xa5a5;
    std::uint32_t word = 0xa5a5a5a5;
    std::uint64_t doubleword = UINT64_MAX;
    EEQuadword quadword{};
    REQUIRE_FALSE(bus.readData8(address, &byte));
    REQUIRE_FALSE(bus.readData16(address, &halfword));
    REQUIRE_FALSE(bus.readData32(address, &word));
    REQUIRE_FALSE(bus.readData64(address, &doubleword));
    REQUIRE(doubleword == UINT64_MAX);
    REQUIRE_FALSE(bus.readData128(address, &quadword));
    REQUIRE_FALSE(bus.writeData8(address, 0));
    REQUIRE_FALSE(bus.writeData16(address, 0));
    REQUIRE_FALSE(bus.writeData32(address, 0));
    REQUIRE_FALSE(bus.writeData64(address + 8, 0));
    REQUIRE_FALSE(bus.writeData64(address | 0xa0000000, 0));
    REQUIRE_FALSE(bus.writeData128(address, quadword));
    REQUIRE_THROWS_WITH(display.readPrivilegedRegister(index),
      "GS display privileged register is write-only.");
  }
  REQUIRE_FALSE(bus.writeData64(0x120000f0, 0));
  REQUIRE_THROWS_WITH(display.writePrivilegedRegister(0x0f, 0),
    "GS display privileged register is not implemented.");
  REQUIRE(system.saveState() == state);
}

TEST_CASE("EE SD reaches PCRTC initialization through kernel aliases",
  "[ee][gs][pcrtc-init]")
{
  for (std::uint32_t segment : {0x80000000u, 0xa0000000u})
  {
    for (std::uint32_t address : {0x12000010u, 0x12000030u, 0x12000040u,
         0x12000050u, 0x12000060u})
    {
      CAPTURE(segment);
      CAPTURE(address);
      NekoSystem system;
      EECore &core = system.eeCore();
      core.setGeneralRegister(1, {address | segment, 0});
      core.setGeneralRegister(2, {UINT64_C(31147115780), 0});
      system.eeBus().write32(0, immediateInstruction(0x3f, 1, 2));
      core.startExecution(0);
      system.clockMasterCycle();
      REQUIRE(core.pendingException() == EEException::None);
      REQUIRE(core.programCounter() == 4);
      REQUIRE(core.generalRegister(2).low == UINT64_C(31147115780));
    }
  }
}

TEST_CASE("EE DEV9 initialization admits only the observed halfword write",
  "[ee][bus][dev9-init][save-state]")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();
  const auto state = system.saveState();
  REQUIRE(bus.writeData16(0x1f801472, 1));
  REQUIRE(bus.writeData16(0x1f801472, 1));
  REQUIRE(bus.writeData16(0x1f801472, 0));
  REQUIRE_FALSE(bus.writeData16(0x1f801472, 2));
  REQUIRE(bus.writeData16(0x1f801470, 0));
  REQUIRE(bus.writeData16(0x1f801470, 0));
  for (unsigned bit = 0; bit < 16; ++bit)
  {
    CAPTURE(bit);
    if (bit != 0)
    {
      REQUIRE_FALSE(bus.writeData16(0x1f801472,
        static_cast<std::uint16_t>(1u ^ (1u << bit))));
    }
    REQUIRE_FALSE(bus.writeData16(0x1f801470,
      static_cast<std::uint16_t>(1u << bit)));
  }
  for (std::uint32_t address : {0x1f801470u, 0x1f801474u,
       0x9f801472u, 0xbf801472u})
  {
    CAPTURE(address);
    REQUIRE_FALSE(bus.writeData16(address, 1));
  }
  std::uint8_t byte = 0xa5;
  std::uint16_t halfword = 0xa5a5;
  std::uint32_t word = 0xa5a5a5a5;
  std::uint64_t doubleword = UINT64_MAX;
  EEQuadword quadword{};
  REQUIRE_FALSE(bus.readData8(0x1f801472, &byte));
  REQUIRE_FALSE(bus.readData16(0x1f801472, &halfword));
  REQUIRE_FALSE(bus.readData16(0x1f801470, &halfword));
  REQUIRE(halfword == 0xa5a5);
  REQUIRE_FALSE(bus.readData32(0x1f801470, &word));
  REQUIRE_FALSE(bus.readData64(0x1f801470, &doubleword));
  REQUIRE_FALSE(bus.readData128(0x1f801470, &quadword));
  REQUIRE_FALSE(bus.writeData8(0x1f801472, 1));
  REQUIRE_FALSE(bus.writeData32(0x1f801470, 1));
  REQUIRE_FALSE(bus.writeData64(0x1f801470, 1));
  REQUIRE_FALSE(bus.writeData128(0x1f801470, quadword));
  REQUIRE_THROWS_WITH(bus.writeData16(0x1f801473, 1),
    "EE halfword store must be naturally aligned.");
  REQUIRE(system.saveState() == state);
}

TEST_CASE("EE Main Memory Map Tests")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();

  bus.write32(0x00000100, UINT32_C(0x12345678));
  REQUIRE(bus.read32(0x20000100) == 0x12345678);
  REQUIRE(bus.read32(0x30000100) == 0x12345678);
  REQUIRE(bus.read32(0x80000100) == 0x12345678);
  REQUIRE(bus.read32(0xa0000100) == 0x12345678);

  std::uint8_t byte = 0;
  REQUIRE(bus.readData8(0x00000101, &byte));
  REQUIRE(byte == 0x56);
  REQUIRE(bus.writeData8(0x00000102, 0xab));
  REQUIRE(bus.readData8(0x00000102, &byte));
  REQUIRE(byte == 0xab);
  REQUIRE_FALSE(
    bus.readData8(EEMemoryMap::MAIN_MEMORY_SIZE, &byte));
  REQUIRE_FALSE(
    bus.writeData8(EEMemoryMap::MAIN_MEMORY_SIZE, 0));
  std::uint16_t halfword = 0;
  REQUIRE(bus.writeData16(0x00000104, 0x89ab));
  REQUIRE(bus.readData16(0x00000104, &halfword));
  REQUIRE(halfword == 0x89ab);
  REQUIRE_THROWS_WITH(
    bus.readData16(0x105, &halfword),
    "EE halfword load must be naturally aligned.");
  REQUIRE_FALSE(
    bus.readData16(EEMemoryMap::MAIN_MEMORY_SIZE, &halfword));
  std::uint32_t word = 0;
  REQUIRE(bus.writeData32(0x00000108, UINT32_C(0x89abcdef)));
  REQUIRE(bus.readData32(0x00000108, &word));
  REQUIRE(word == UINT32_C(0x89abcdef));
  REQUIRE_THROWS_WITH(
    bus.writeData32(0x10a, 0),
    "EE word store must be naturally aligned.");
  REQUIRE_FALSE(
    bus.writeData32(EEMemoryMap::MAIN_MEMORY_SIZE, 0));
  std::uint64_t doubleword = 0;
  REQUIRE(
    bus.writeData64(
      0x00000110,
      UINT64_C(0x0123456789abcdef)));
  REQUIRE(bus.readData64(0x00000110, &doubleword));
  REQUIRE(doubleword == UINT64_C(0x0123456789abcdef));
  REQUIRE_THROWS_WITH(
    bus.readData64(0x114, &doubleword),
    "EE doubleword load must be naturally aligned.");
  REQUIRE_FALSE(
    bus.readData64(
      EEMemoryMap::MAIN_MEMORY_SIZE,
      &doubleword));
  const EEQuadword storedQuadword = {
    UINT64_C(0x7766554433221100),
    UINT64_C(0xffeeddccbbaa9988)
  };
  REQUIRE(bus.writeData128(0x00000120, storedQuadword));
  EEQuadword loadedQuadword;
  REQUIRE(bus.readData128(0x00000120, &loadedQuadword));
  REQUIRE(loadedQuadword.low == storedQuadword.low);
  REQUIRE(loadedQuadword.high == storedQuadword.high);
  REQUIRE_THROWS_WITH(
    bus.writeData128(0x128, storedQuadword),
    "EE quadword store must be naturally aligned.");
  REQUIRE_FALSE(
    bus.readData128(
      EEMemoryMap::MAIN_MEMORY_SIZE,
      &loadedQuadword));

  const GIFQuadword quadword = {{
    UINT32_C(0x01020304),
    UINT32_C(0x11121314),
    UINT32_C(0x21222324),
    UINT32_C(0x31323334)
  }};
  REQUIRE(bus.writeQuadword(0x00000200, quadword));
  REQUIRE(bus.readQuadword(0x20000200) == quadword);

  REQUIRE_THROWS_WITH(
    bus.read32(2),
    "EE bus 32-bit access must be naturally aligned.");
  REQUIRE_THROWS_WITH(
    bus.read32(EEMemoryMap::MAIN_MEMORY_SIZE),
    "EE bus read from an unmapped address.");
}

TEST_CASE("EE direct-mapped kernel segments cover the system map")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();

  bus.write32(
    EEMemoryMap::KSEG0_BASE + 0x100,
    UINT32_C(0x12345678));
  REQUIRE(
    bus.read32(EEMemoryMap::KSEG1_BASE + 0x100) ==
    UINT32_C(0x12345678));

  REQUIRE(bus.writeData32(
    EEMemoryMap::INTC_MASK,
    EEInterruptSource::mask(EEInterruptSource::VIF0)));
  std::uint32_t interruptMask = 0;
  REQUIRE(bus.readData32(
    EEMemoryMap::INTC_MASK,
    &interruptMask));
  REQUIRE(
    interruptMask ==
    EEInterruptSource::mask(EEInterruptSource::VIF0));

  REQUIRE(bus.writeData64(
    EEMemoryMap::GS_BUSDIR,
    1));
  REQUIRE(system.gs().hostInterfaceReversed());
  std::uint64_t busDirection = 0;
  REQUIRE_FALSE(bus.readData64(
    EEMemoryMap::KSEG0_BASE + EEMemoryMap::GS_BUSDIR,
    &busDirection));
  REQUIRE(
    bus.read64(
      EEMemoryMap::KSEG0_BASE + EEMemoryMap::GS_BUSDIR) ==
    1);

  REQUIRE(bus.writeData128(
    EEMemoryMap::VIF0_FIFO,
    EEQuadword{}));
  REQUIRE(system.vif0().fifoQuadwordCount() == 1);
  REQUIRE(bus.writeQuadword(
    EEMemoryMap::KSEG1_BASE + EEMemoryMap::VIF0_FIFO,
    GIFQuadword{}));

  std::uint8_t byte = 0;
  REQUIRE_FALSE(
    bus.readData8(EEMemoryMap::KSEG2_BASE, &byte));
  REQUIRE_FALSE(
    bus.readData8(UINT32_C(0xe0000000), &byte));
  REQUIRE_THROWS_WITH(
    bus.read32(EEMemoryMap::KSEG2_BASE),
    "EE bus read from an unmapped address.");
}

TEST_CASE("EE Boot ROM is fixed-size and read-only")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();
  std::vector<std::uint8_t> image(
    EEMemoryMap::BOOT_ROM_SIZE,
    0);
  image[0] = 0x78;
  image[1] = 0x56;
  image[2] = 0x34;
  image[3] = 0x12;
  image[EEMemoryMap::BOOT_ROM_SIZE - 1] = 0xab;

  REQUIRE_THROWS_WITH(
    bus.installBootROM(
      std::vector<std::uint8_t>(
        EEMemoryMap::BOOT_ROM_SIZE - 1,
        0)),
    "EE Boot ROM image must be exactly 4 MiB.");

  bus.installBootROM(image);
  const auto identity = bus.bootROMIdentity();
  REQUIRE(identity != nullptr);
  REQUIRE(system.iopBus().romIdentity() == nullptr);
  REQUIRE_THROWS_AS(
    bus.installBootROM(std::shared_ptr<const BootROMImage>{}),
    std::invalid_argument);
  REQUIRE(bus.bootROMIdentity() == identity);

  std::uint32_t instruction = 0;
  REQUIRE(bus.readInstruction32(
    EEMemoryMap::BOOT_ROM_BASE,
    &instruction));
  REQUIRE(instruction == UINT32_C(0x12345678));
  std::uint16_t halfword = 0;
  REQUIRE(bus.readData16(EEMemoryMap::BOOT_ROM_BASE, &halfword));
  REQUIRE(halfword == 0x5678);
  REQUIRE(bus.readData16(EEMemoryMap::BOOT_ROM_END - 2, &halfword));
  REQUIRE(halfword == 0xab00);
  std::uint64_t doubleword = 0;
  REQUIRE(bus.readData64(EEMemoryMap::BOOT_ROM_BASE, &doubleword));
  REQUIRE(doubleword == UINT64_C(0x12345678));
  EEQuadword quadword{};
  REQUIRE(bus.readData128(EEMemoryMap::BOOT_ROM_BASE, &quadword));
  REQUIRE(quadword.low == UINT64_C(0x12345678));
  REQUIRE(quadword.high == 0);
  REQUIRE(bus.readData32(
    EEMemoryMap::BOOT_ROM_BASE,
    &instruction));
  REQUIRE(instruction == UINT32_C(0x12345678));
  std::uint8_t byte = 0;
  REQUIRE(bus.readData8(
    EEMemoryMap::BOOT_ROM_END - 1,
    &byte));
  REQUIRE(byte == 0xab);

  REQUIRE_FALSE(bus.writeData8(
    EEMemoryMap::BOOT_ROM_BASE,
    0xff));
  REQUIRE_FALSE(bus.writeData32(
    EEMemoryMap::BOOT_ROM_BASE,
    0));
  REQUIRE_THROWS_WITH(
    bus.write32(EEMemoryMap::BOOT_ROM_BASE, 0),
    "EE bus write to a read-only address.");
  REQUIRE(
    bus.read32(EEMemoryMap::KSEG1_BASE +
      EEMemoryMap::BOOT_ROM_BASE) ==
    UINT32_C(0x12345678));
  image[0] = 0;
  REQUIRE(bus.read32(EEMemoryMap::BOOT_ROM_BASE) == UINT32_C(0x12345678));
  system.reset();
  REQUIRE(system.eeBus().bootROMIdentity() == identity);
  REQUIRE(system.iopBus().romIdentity() == nullptr);
  REQUIRE(system.eeBus().read32(EEMemoryMap::BOOT_ROM_BASE) ==
    UINT32_C(0x12345678));
}

TEST_CASE("EE cache fill ranges require physical RAM or installed ROM backing",
  "[ee][cache-rom]")
{
  NekoSystem system;
  auto &bus = system.eeBus();
  REQUIRE(bus.isCacheFillRange(0, 64));
  REQUIRE(bus.isCacheFillRange(EEMemoryMap::MAIN_MEMORY_SIZE - 64, 64));
  REQUIRE_FALSE(bus.isCacheFillRange(EEMemoryMap::MAIN_MEMORY_SIZE - 63, 64));
  REQUIRE_FALSE(bus.isCacheFillRange(EEMemoryMap::BOOT_ROM_BASE, 64));
  REQUIRE_FALSE(bus.isCacheFillRange(0, 0));
  REQUIRE_FALSE(bus.isCacheFillRange(0, std::numeric_limits<std::size_t>::max()));
  REQUIRE_FALSE(bus.isCacheFillRange(EEMemoryMap::KSEG0_BASE, 64));
  REQUIRE_FALSE(bus.isCacheFillRange(UINT32_C(0x20000000), 64));
  REQUIRE_FALSE(bus.isCacheFillRange(EEMemoryMap::INTC_STAT, 64));
  bus.installBootROM(std::vector<std::uint8_t>(EEMemoryMap::BOOT_ROM_SIZE, 0));
  REQUIRE(bus.isCacheFillRange(EEMemoryMap::BOOT_ROM_BASE, 64));
  REQUIRE(bus.isCacheFillRange(EEMemoryMap::BOOT_ROM_END - 64, 64));
  REQUIRE_FALSE(bus.isCacheFillRange(EEMemoryMap::BOOT_ROM_BASE - 1, 64));
  REQUIRE_FALSE(bus.isCacheFillRange(EEMemoryMap::BOOT_ROM_END - 63, 64));
  REQUIRE_FALSE(bus.isCacheFillRange(EEMemoryMap::BOOT_ROM_END, 64));
  REQUIRE_FALSE(bus.isCacheFillRange(EEMemoryMap::BOOT_ROM_BASE,
    std::numeric_limits<std::size_t>::max()));
  REQUIRE_FALSE(bus.isCacheFillRange(UINT32_C(0xfffffffc), 64));
  REQUIRE_FALSE(bus.isCacheFillRange(UINT32_C(0x9fc00000), 64));
  REQUIRE_FALSE(bus.isCacheFillRange(UINT32_C(0xbfc00000), 64));
  REQUIRE_FALSE(bus.isCacheFillRange(EEMemoryMap::INTC_STAT, 64));
}

TEST_CASE("EE guest bus accesses require physical addresses")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();

  bus.write32(0x100, UINT32_C(0x12345678));

  std::uint32_t value = 0;
  REQUIRE_FALSE(bus.readInstruction32(0x80000100, &value));
  REQUIRE_FALSE(bus.readData32(0x20000100, &value));
  REQUIRE_FALSE(bus.readData32(0xa0000100, &value));
  REQUIRE_FALSE(
    bus.writeData32(
      EEMemoryMap::KSEG0_BASE + EEMemoryMap::INTC_MASK,
      EEInterruptSource::mask(EEInterruptSource::VIF0)));

  bus.write8(EEMemoryMap::KSEG0_BASE + 0x100, 0xab);
  std::uint8_t byte = 0;
  REQUIRE(bus.readData8(0x100, &byte));
  REQUIRE(byte == 0xab);
}

TEST_CASE("EE guest device accesses enforce register contracts")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();

  std::uint8_t byte = 0;
  std::uint16_t halfword = 0;
  std::uint32_t word = 0;
  std::uint64_t doubleword = 0;
  EEQuadword quadword;

  REQUIRE_FALSE(
    bus.readData8(EEMemoryMap::VIF0_STAT, &byte));
  REQUIRE_FALSE(
    bus.writeData8(EEMemoryMap::VIF0_FBRST, 0));
  REQUIRE_FALSE(
    bus.readData16(EEMemoryMap::INTC_STAT, &halfword));
  REQUIRE_FALSE(
    bus.writeData16(EEMemoryMap::INTC_MASK, 0));

  REQUIRE(bus.readData32(EEMemoryMap::VIF0_STAT, &word));
  REQUIRE_FALSE(
    bus.writeData32(EEMemoryMap::VIF0_STAT, 0));
  REQUIRE_FALSE(
    bus.readData32(EEMemoryMap::VIF0_FBRST, &word));
  REQUIRE(bus.writeData32(EEMemoryMap::VIF0_FBRST, 0));
  REQUIRE_FALSE(
    bus.writeData32(EEMemoryMap::VIF0_FBRST, 1));
  REQUIRE_FALSE(
    bus.writeData32(EEMemoryMap::GS_BUSDIR, 1));

  REQUIRE_FALSE(
    bus.readData64(EEMemoryMap::GS_BUSDIR, &doubleword));
  REQUIRE(bus.writeData64(EEMemoryMap::GS_BUSDIR, 1));
  REQUIRE(system.gs().hostInterfaceReversed());
  REQUIRE_FALSE(
    bus.readData64(EEMemoryMap::GS_PMODE, &doubleword));
  REQUIRE(bus.writeData64(EEMemoryMap::GS_PMODE, 0));
  REQUIRE_FALSE(
    bus.writeData64(
      EEMemoryMap::GS_DISPFB1,
      UINT64_C(1) << 15));
  REQUIRE_FALSE(
    bus.writeData64(EEMemoryMap::INTC_MASK, 0));

  REQUIRE_FALSE(
    bus.readData128(EEMemoryMap::GIF_STAT, &quadword));
  REQUIRE(
    bus.writeData128(EEMemoryMap::GIF_FIFO, quadword));

  REQUIRE_FALSE(
    bus.writeData32(EEMemoryMap::D_CTRL, 2));
  REQUIRE(system.dmacController().control() == 0);
  REQUIRE_FALSE(
    bus.writeData32(
      EEMemoryMap::D1_CHCR,
      GIFDMACChannelControl::START));
  REQUIRE(system.vif1DMAC().channelControl() == 0);
  REQUIRE(bus.writeData32(EEMemoryMap::D1_MADR, 0x1234));
  REQUIRE(bus.readData32(EEMemoryMap::D1_MADR, &word));
  REQUIRE(word == 0x1230);
  REQUIRE_THROWS_WITH(
    bus.write32(EEMemoryMap::D_CTRL, 2),
    "Unsupported D_CTRL fields or excluded SIF0-source/SIF1-drain pair.");
}

TEST_CASE("EE guest VIF FIFO stores are atomic and backpressured")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();
  const EEQuadword interruptedNops = {
    UINT64_C(0x0000000080000000),
    0
  };
  const EEQuadword nops = {};

  REQUIRE(
    bus.writeGuestData128(
      EEMemoryMap::VIF0_FIFO,
      interruptedNops) ==
    EEDataWriteResult::Completed);
  bus.advanceGuestFIFOs();
  REQUIRE(system.vif0().interruptPending());
  REQUIRE(system.vif0().wordsIngested() == 1);
  REQUIRE(system.vif0().fifoQuadwordCount() == 1);

  for (std::size_t index = 0; index < 7; ++index)
  {
    REQUIRE(
      bus.writeGuestData128(EEMemoryMap::VIF0_FIFO, nops) ==
      EEDataWriteResult::Completed);
  }
  REQUIRE(system.vif0().fifoQuadwordCount() == 8);
  REQUIRE(
    bus.writeGuestData128(EEMemoryMap::VIF0_FIFO, nops) ==
    EEDataWriteResult::Stalled);
  REQUIRE(system.vif0().fifoQuadwordCount() == 8);
  REQUIRE(system.vif0().wordsIngested() == 1);

  bus.write32(EEMemoryMap::VIF0_FBRST, 1u << 3);
  bus.advanceGuestFIFOs();
  REQUIRE(system.vif0().fifoQuadwordCount() == 0);
  REQUIRE(system.vif0().wordsIngested() == 32);
}

TEST_CASE("EE SQ retries a translated full FIFO without an exception")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();
  EECore &core = system.eeCore();
  const EEQuadword interruptedNops = {
    UINT64_C(0x0000000080000000),
    0
  };

  REQUIRE(
    bus.writeGuestData128(
      EEMemoryMap::VIF0_FIFO,
      interruptedNops) ==
    EEDataWriteResult::Completed);
  bus.advanceGuestFIFOs();
  for (std::size_t index = 0; index < 7; ++index)
  {
    REQUIRE(
      bus.writeGuestData128(EEMemoryMap::VIF0_FIFO, {}) ==
      EEDataWriteResult::Completed);
  }

  constexpr std::uint32_t virtualFIFO = UINT32_C(0x00400000);
  core.setGeneralRegister(1, {virtualFIFO, 0});
  core.setGeneralRegister(
    2,
    {
      UINT64_C(0x1111111122222222),
      UINT64_C(0x3333333344444444)
    });
  bus.write32(
    0,
    immediateInstruction(0x1f, 1, 2));
  core.setCOP0Register(EECOP0Register::Status, 0);
  core.setTLBEntry(
    0,
    {
      EECOP0PageMask::SIZE_4_KIB,
      virtualFIFO,
      {
        (EEMemoryMap::VIF0_FIFO >> 6) |
        UINT32_C(0x1f)
      },
      {UINT32_C(0x1f)}
    });
  core.startExecution(EEMemoryMap::KSEG0_BASE);

  system.clockMasterCycle();
  REQUIRE(core.programCounter() == EEMemoryMap::KSEG0_BASE);
  REQUIRE(core.pendingException() == EEException::None);
  REQUIRE(system.vif0().fifoQuadwordCount() == 8);

  bus.write32(EEMemoryMap::VIF0_FBRST, 1u << 3);
  system.clockMasterCycle();
  REQUIRE(core.programCounter() == EEMemoryMap::KSEG0_BASE + 4);
  REQUIRE(core.pendingException() == EEException::None);
  REQUIRE(system.vif0().fifoQuadwordCount() == 1);
}

TEST_CASE("EE pairs fall back before a younger SQ FIFO stall")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();
  EECore &core = system.eeCore();
  const EEQuadword interruptedNops = {
    UINT64_C(0x0000000080000000),
    0
  };

  REQUIRE(
    bus.writeGuestData128(
      EEMemoryMap::VIF0_FIFO,
      interruptedNops) ==
    EEDataWriteResult::Completed);
  bus.advanceGuestFIFOs();
  for (std::size_t index = 0; index < 7; ++index)
  {
    REQUIRE(
      bus.writeGuestData128(EEMemoryMap::VIF0_FIFO, {}) ==
      EEDataWriteResult::Completed);
  }

  core.setGeneralRegister(1, {EEMemoryMap::VIF0_FIFO, 0});
  core.setGeneralRegister(
    2,
    {
      UINT64_C(0x1111111122222222),
      UINT64_C(0x3333333344444444)
    });
  bus.write32(0, immediateInstruction(0x09, 0, 3, 1));
  bus.write32(4, immediateInstruction(0x1f, 1, 2));
  core.startExecution(0);

  system.clockMasterCycle();
  REQUIRE(core.generalRegister(3).low == 1);
  REQUIRE(core.acceptanceRecordsThisCycle().size() == 1);
  REQUIRE(core.programCounter() == 4);
  REQUIRE(system.vif0().fifoQuadwordCount() == 8);

  system.clockMasterCycle();
  REQUIRE(core.generalRegister(3).low == 1);
  REQUIRE(core.acceptanceRecordsThisCycle().size() == 0);
  REQUIRE(core.programCounter() == 4);
  REQUIRE(core.pendingException() == EEException::None);

  bus.write32(EEMemoryMap::VIF0_FBRST, 1u << 3);
  system.clockMasterCycle();
  REQUIRE(core.generalRegister(3).low == 1);
  REQUIRE(core.acceptanceRecordsThisCycle().size() == 1);
  REQUIRE(core.programCounter() == 8);
  REQUIRE(system.vif0().fifoQuadwordCount() == 1);
}

TEST_CASE("EE guest GIF FIFO retains refused PATH3 transfers")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();
  const GIFQuadword blockingTag = gifTag();
  const EEQuadword guestTag = {
    UINT64_C(1) << 15,
    0
  };

  REQUIRE(system.gifPathArbiter().transferQuadword(
    GIFPath::Path2,
    blockingTag).accepted);
  for (std::size_t index = 0; index < 16; ++index)
  {
    REQUIRE(
      bus.writeGuestData128(
        EEMemoryMap::GIF_FIFO,
        guestTag) ==
      EEDataWriteResult::Completed);
  }
  REQUIRE(
    bus.writeGuestData128(
      EEMemoryMap::GIF_FIFO,
      guestTag) ==
    EEDataWriteResult::Stalled);

  bus.advanceGuestFIFOs();
  REQUIRE(system.gifPath3().guestFIFOQuadwordCount() == 16);
  REQUIRE(system.gifPath3().transferredQuadwordCount() == 0);
  REQUIRE(
    ((bus.read32(EEMemoryMap::GIF_STAT) &
      GIFStatus::FQC_MASK) >>
      GIFStatus::FQC_SHIFT) == 16);

  REQUIRE(system.gifPathArbiter().transferQuadword(
    GIFPath::Path2,
    adWrite(GSRegisterAddress::PRIM, 0)).accepted);
  bus.advanceGuestFIFOs();
  REQUIRE(system.gifPath3().guestFIFOQuadwordCount() == 0);
  REQUIRE(system.gifPath3().transferredQuadwordCount() == 16);
}

TEST_CASE("EE guest instructions reject invalid device access directions")
{
  SECTION("Write-only GS register cannot be loaded")
  {
    NekoSystem system;
    system.eeCore().setGeneralRegister(
      1,
      {EEMemoryMap::GS_BUSDIR, 0});
    system.eeBus().write32(
      0,
      immediateInstruction(0x37, 1, 2));
    system.eeCore().startExecution(0);

    const EEExecutionResult result = system.runEE(1);

    REQUIRE(
      result.pendingException ==
      EEException::DataBusErrorLoad);
    REQUIRE(
      result.exceptionAddress ==
      EEMemoryMap::GS_BUSDIR);
  }

  SECTION("64-bit GS register rejects word stores")
  {
    NekoSystem system;
    system.eeCore().setGeneralRegister(
      1,
      {EEMemoryMap::GS_BUSDIR, 0});
    system.eeCore().setGeneralRegister(2, {1, 0});
    system.eeBus().write32(
      0,
      immediateInstruction(0x2b, 1, 2));
    system.eeCore().startExecution(0);

    const EEExecutionResult result = system.runEE(1);

    REQUIRE(
      result.pendingException ==
      EEException::DataBusErrorStore);
    REQUIRE_FALSE(system.gs().hostInterfaceReversed());
  }

  SECTION("32-bit EE register rejects doubleword stores")
  {
    NekoSystem system;
    system.eeCore().setGeneralRegister(
      1,
      {EEMemoryMap::INTC_MASK, 0});
    system.eeCore().setGeneralRegister(
      2,
      {EEInterruptSource::mask(EEInterruptSource::VIF0), 0});
    system.eeBus().write32(
      0,
      immediateInstruction(0x3f, 1, 2));
    system.eeCore().startExecution(0);

    const EEExecutionResult result = system.runEE(1);

    REQUIRE(
      result.pendingException ==
      EEException::DataBusErrorStore);
    REQUIRE(system.interruptController().mask() == 0);
  }

  SECTION("Invalid register values become store bus errors")
  {
    NekoSystem system;
    system.eeCore().setGeneralRegister(
      1,
      {EEMemoryMap::D_CTRL, 0});
    system.eeCore().setGeneralRegister(2, {2, 0});
    system.eeBus().write32(
      0,
      immediateInstruction(0x2b, 1, 2));
    system.eeCore().startExecution(0);

    const EEExecutionResult result = system.runEE(1);

    REQUIRE(
      result.pendingException ==
      EEException::DataBusErrorStore);
    REQUIRE(system.dmacController().control() == 0);
  }
}

TEST_CASE("EE GIF reset admits only complete word controls atomically",
  "[gif][reset][ee][bus]")
{
  NekoSystem system;
  auto &bus = system.eeBus();
  bus.write32(EEMemoryMap::GIF_MODE, GIFMode::M3R | GIFMode::IMT);
  system.vif1().processCode(UINT32_C(0x06008000));
  REQUIRE(system.gifPath3().submitGuestQuadword(gifTag()));
  const auto hash = system.eeStateHash();
  REQUIRE(bus.writeData32(EEMemoryMap::GIF_BASE, 0));
  REQUIRE(system.eeStateHash() == hash);
  for (const auto value : {2u, 8u, 9u, 0x10u, UINT32_MAX})
  {
    CAPTURE(value);
    REQUIRE_FALSE(bus.writeData32(EEMemoryMap::GIF_BASE, value));
    REQUIRE_THROWS_AS(bus.write32(EEMemoryMap::GIF_BASE, value),
      std::invalid_argument);
    REQUIRE(system.eeStateHash() == hash);
  }
  for (unsigned offset = 0; offset < 4; ++offset)
  {
    REQUIRE_FALSE(bus.writeData8(EEMemoryMap::GIF_BASE + offset, 1));
  }
  REQUIRE_FALSE(bus.writeData16(EEMemoryMap::GIF_BASE, 1));
  REQUIRE_FALSE(bus.writeData16(EEMemoryMap::GIF_BASE + 2, 1));
  REQUIRE_FALSE(bus.writeData64(EEMemoryMap::GIF_BASE, 1));
  REQUIRE_FALSE(bus.writeData128(EEMemoryMap::GIF_BASE, {1, 0}));
  REQUIRE_FALSE(bus.writeDMAC128(EEMemoryMap::GIF_BASE, {1, 0}));
  for (const auto offset : {4u, 8u, 12u})
  {
    REQUIRE_FALSE(bus.writeData32(EEMemoryMap::GIF_BASE + offset, 1));
  }
  std::uint32_t word = 0xa5a5a5a5;
  REQUIRE_FALSE(bus.readData32(EEMemoryMap::GIF_BASE, &word));
  REQUIRE(word == 0xa5a5a5a5);
  REQUIRE_FALSE(bus.readInstruction32(EEMemoryMap::GIF_BASE, &word));
  REQUIRE_FALSE(bus.isCacheFillRange(EEMemoryMap::GIF_BASE, 4));
  EEQuadword quad = {UINT64_MAX, UINT64_MAX};
  REQUIRE_FALSE(bus.readDMAC128(EEMemoryMap::GIF_BASE, &quad));
  REQUIRE(system.eeStateHash() == hash);
  REQUIRE(bus.writeData32(EEMemoryMap::GIF_BASE, 1));
  REQUIRE(bus.read32(EEMemoryMap::GIF_STAT) == 0);
}

TEST_CASE("EE GIF reset guest stores preserve traced and restored continuation",
  "[gif][reset][ee][bus][save-state]")
{
  std::uint32_t opcode = UINT32_C(0xac440000);
  SECTION("SW") {}
  SECTION("full SWL") { opcode = UINT32_C(0xa8440003); }
  SECTION("full SWR") { opcode = UINT32_C(0xb8440000); }
  NekoSystem original;
  original.eeBus().write32(0, opcode);
  original.eeBus().write32(EEMemoryMap::GIF_MODE, GIFMode::M3R | GIFMode::IMT);
  original.vif1().processCode(UINT32_C(0x06008000));
  REQUIRE(original.gifPath3().submitGuestQuadword(gifTag()));
  original.eeCore().setCOP0Register(EECOP0Register::Status, 1u << 2);
  original.eeCore().setGeneralRegister(2, {EEMemoryMap::GIF_BASE, 0});
  original.eeCore().setGeneralRegister(4, {1, 0});
  original.eeCore().startExecution(EEMemoryMap::KSEG1_BASE);
  const auto saved = original.saveState();
  NekoSystem restored;
  NekoSystem plain;
  restored.loadState(saved);
  plain.loadState(saved);
  original.startTrace(100);
  restored.startTrace(100);
  for (auto *machine : {&original, &restored, &plain})
  {
    const auto result = machine->stepEEInstruction(16);
    REQUIRE(result.pendingException == EEException::None);
    REQUIRE_FALSE(result.cycleLimitReached);
    REQUIRE(machine->gifRegisters().readStatus() == 0);
    REQUIRE(!machine->vif1().path3Masked());
  }
  std::size_t stores = 0;
  for (const auto &event : original.trace())
  {
    if (event.subsystem == NekoTraceSubsystem::EE &&
        event.type == NekoTraceEventType::MemoryAccess &&
        event.value0 == EEMemoryMap::GIF_BASE)
    {
      ++stores;
      REQUIRE(event.value1 == 1);
      REQUIRE(event.value3 == nekoPackEEMemoryTraceMetadata({
        EEMemoryMap::GIF_BASE, 4, NekoEETraceMemory::AccessKind::DataStore,
        NekoEETraceMemory::TranslationOutcome::Translated,
        NekoEETraceMemory::CacheRoute::Uncached, 2, false, true,
        NekoEETraceMemory::CacheAccess::Bypassed,
        NekoEETraceMemory::TransferOutcome::Completed,
        NekoEETraceMemory::FailurePhase::None}));
    }
  }
  REQUIRE(stores == 1);
  REQUIRE(original.traceHash() == restored.traceHash());
  const auto ending = original.saveState();
  REQUIRE(restored.saveState() == ending);
  REQUIRE(plain.saveState() == ending);
  restored.loadState(ending);
  REQUIRE(restored.saveState() == ending);
}

TEST_CASE("EE GIF reset rejects partial and unsupported guest stores",
  "[gif][reset][ee][bus]")
{
  std::uint32_t opcode = UINT32_C(0xa8440001);
  std::uint32_t value = 1;
  SECTION("partial SWL") {}
  SECTION("partial SWR") { opcode = UINT32_C(0xb8440001); }
  SECTION("PSE") { opcode = UINT32_C(0xac440000); value = 9; }
  NekoSystem system;
  system.eeBus().write32(0, opcode);
  system.eeBus().write32(EEMemoryMap::GIF_MODE, GIFMode::M3R | GIFMode::IMT);
  system.eeCore().setCOP0Register(EECOP0Register::Status, 1u << 2);
  system.eeCore().setGeneralRegister(2, {EEMemoryMap::GIF_BASE, 0});
  system.eeCore().setGeneralRegister(4, {value, 0});
  system.eeCore().startExecution(EEMemoryMap::KSEG1_BASE);
  const auto result = system.stepEEInstruction(16);
  REQUIRE(result.pendingException == EEException::DataBusErrorStore);
  REQUIRE(system.gifRegisters().readStatus() == (GIFStatus::M3R | GIFStatus::IMT));
}

TEST_CASE("EE GIF Memory Map Tests")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();

  bus.write32(EEMemoryMap::GIF_MODE, GIFMode::IMT);
  REQUIRE(
    (bus.read32(EEMemoryMap::GIF_STAT) & GIFStatus::IMT) != 0);

  REQUIRE(bus.writeQuadword(EEMemoryMap::GIF_FIFO, gifTag()));
  REQUIRE(bus.writeQuadword(
    EEMemoryMap::GIF_FIFO,
    adWrite(GSRegisterAddress::PRIM, 6)));
  REQUIRE(
    system.gs().primitive().type ==
    GSPrimitiveType::Sprite);
}

TEST_CASE("EE VIF and Interrupt Coordination Tests")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();
  const GIFQuadword interruptedNops = {{
    0,
    0,
    0,
    UINT32_C(0x80000000)
  }};

  REQUIRE(bus.writeQuadword(
    EEMemoryMap::VIF1_FIFO,
    interruptedNops));
  REQUIRE(
    (bus.read32(EEMemoryMap::VIF1_STAT) &
     EEVIFStatus::INTERRUPT) != 0);
  REQUIRE(bus.read32(EEMemoryMap::VIF1_CODE) == 0x80000000);

  system.clockMasterCycle();
  REQUIRE(
    (bus.read32(EEMemoryMap::INTC_STAT) &
     EEInterruptSource::mask(EEInterruptSource::VIF1)) != 0);
  REQUIRE_FALSE(system.interruptPending());

  bus.write32(
    EEMemoryMap::INTC_MASK,
    EEInterruptSource::mask(EEInterruptSource::VIF1));
  REQUIRE(system.interruptPending());

  bus.write32(EEMemoryMap::VIF1_FBRST, 1u << 3);
  system.clockMasterCycle();
  REQUIRE(
    (bus.read32(EEMemoryMap::VIF1_STAT) &
     EEVIFStatus::INTERRUPT) == 0);
  REQUIRE(system.interruptPending());

  bus.write32(
    EEMemoryMap::INTC_STAT,
    EEInterruptSource::mask(EEInterruptSource::VIF1));
  REQUIRE_FALSE(system.interruptPending());
}

TEST_CASE("EE GS Privileged Memory Map Tests")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();

  bus.write64(EEMemoryMap::GS_BUSDIR, 1);

  REQUIRE(system.gs().hostInterfaceReversed());
  REQUIRE(bus.read64(EEMemoryMap::GS_BUSDIR) == 1);
}
