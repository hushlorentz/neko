#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>

#include "catch.hpp"
#include "ee_bus.hpp"
#include "gif_registers.hpp"
#include "neko_system.hpp"

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
