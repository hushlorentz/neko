#include <cstddef>
#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "ee_bus.hpp"
#include "ee_elf_loader.hpp"
#include "neko_system.hpp"

namespace
{
  constexpr std::size_t ELF_HEADER_SIZE = 52;
  constexpr std::size_t PROGRAM_HEADER_SIZE = 32;
  constexpr std::size_t PROGRAM_HEADER_OFFSET = ELF_HEADER_SIZE;

  void writeU16(
    std::vector<std::uint8_t> *image,
    std::size_t offset,
    std::uint16_t value)
  {
    (*image)[offset] = static_cast<std::uint8_t>(value);
    (*image)[offset + 1] =
      static_cast<std::uint8_t>(value >> 8);
  }

  void writeU32(
    std::vector<std::uint8_t> *image,
    std::size_t offset,
    std::uint32_t value)
  {
    for (std::size_t index = 0; index < 4; ++index)
    {
      (*image)[offset + index] =
        static_cast<std::uint8_t>(value >> (index * 8));
    }
  }

  void writeProgramHeader(
    std::vector<std::uint8_t> *image,
    std::size_t index,
    std::uint32_t type,
    std::uint32_t fileOffset,
    std::uint32_t virtualAddress,
    std::uint32_t fileSize,
    std::uint32_t memorySize,
    std::uint32_t flags,
    std::uint32_t alignment)
  {
    const std::size_t offset =
      PROGRAM_HEADER_OFFSET + index * PROGRAM_HEADER_SIZE;
    writeU32(image, offset, type);
    writeU32(image, offset + 4, fileOffset);
    writeU32(image, offset + 8, virtualAddress);
    writeU32(image, offset + 12, virtualAddress);
    writeU32(image, offset + 16, fileSize);
    writeU32(image, offset + 20, memorySize);
    writeU32(image, offset + 24, flags);
    writeU32(image, offset + 28, alignment);
  }

  std::vector<std::uint8_t> validELF()
  {
    std::vector<std::uint8_t> image(0x204, 0);
    image[0] = 0x7f;
    image[1] = 'E';
    image[2] = 'L';
    image[3] = 'F';
    image[4] = 1;
    image[5] = 1;
    image[6] = 1;
    writeU16(&image, 16, 2);
    writeU16(&image, 18, 8);
    writeU32(&image, 20, 1);
    writeU32(&image, 24, UINT32_C(0x80001000));
    writeU32(&image, 28, PROGRAM_HEADER_OFFSET);
    writeU16(&image, 40, ELF_HEADER_SIZE);
    writeU16(&image, 42, PROGRAM_HEADER_SIZE);
    writeU16(&image, 44, 2);
    writeProgramHeader(
      &image,
      0,
      1,
      0x100,
      UINT32_C(0x80001000),
      8,
      8,
      5,
      0x100);
    writeProgramHeader(
      &image,
      1,
      1,
      0x200,
      0x2000,
      4,
      12,
      6,
      0x100);
    writeU32(&image, 0x100, UINT32_C(0x3401002a));
    writeU32(&image, 0x104, UINT32_C(0x0000000c));
    image[0x200] = 0x11;
    image[0x201] = 0x22;
    image[0x202] = 0x33;
    image[0x203] = 0x44;
    return image;
  }

  std::vector<std::uint8_t> returningELF(
    std::uint16_t exitCode)
  {
    std::vector<std::uint8_t> image = validELF();
    writeU32(
      &image,
      PROGRAM_HEADER_OFFSET + 16,
      12);
    writeU32(
      &image,
      PROGRAM_HEADER_OFFSET + 20,
      12);
    writeU32(
      &image,
      0x100,
      (UINT32_C(0x09) << 26) |
        (UINT32_C(2) << 16) |
        exitCode);
    writeU32(&image, 0x104, UINT32_C(0x03e00008));
    writeU32(&image, 0x108, 0);
    return image;
  }
}

TEST_CASE("PS2 ELF loadable segments initialize EE memory")
{
  NekoSystem system;
  for (std::uint32_t address = 0x2000;
       address < 0x200c;
       ++address)
  {
    REQUIRE(system.eeBus().writeData8(address, 0xff));
  }

  const EEELFLoadResult result =
    system.loadELF(validELF());

  REQUIRE(result.entryPoint == UINT32_C(0x80001000));
  REQUIRE(result.loadedSegments == 2);
  REQUIRE(result.fileBytes == 12);
  REQUIRE(result.zeroedBytes == 8);
  REQUIRE(
    system.eeCore().executionState() ==
    EEExecutionState::Halted);
  REQUIRE(
    system.eeCore().programCounter() ==
    UINT32_C(0x80001000));
  REQUIRE(
    system.eeBus().read32(0x1000) ==
    UINT32_C(0x3401002a));
  REQUIRE(
    system.eeBus().read32(UINT32_C(0xa0001004)) ==
    UINT32_C(0x0000000c));
  REQUIRE(system.eeBus().read32(0x2000) == UINT32_C(0x44332211));
  for (std::uint32_t address = 0x2004;
       address < 0x200c;
       ++address)
  {
    std::uint8_t value = 0xff;
    REQUIRE(system.eeBus().readData8(address, &value));
    REQUIRE(value == 0);
  }

  system.eeCore().setCOP0Register(EECOP0Register::Status, 0);
  system.eeCore().startExecution(result.entryPoint);
  const EEExecutionResult execution = system.runEE(4);
  REQUIRE(execution.instructions == 1);
  REQUIRE(
    execution.pendingException ==
    EEException::SystemCall);
  REQUIRE(system.eeCore().generalRegister(1).low == 42);
}

TEST_CASE("PS2 ELF headers are validated")
{
  SECTION("Magic")
  {
    std::vector<std::uint8_t> image = validELF();
    image[0] = 0;
    NekoSystem system;
    REQUIRE_THROWS_WITH(
      system.loadELF(image),
      "ELF magic is invalid.");
  }

  SECTION("Class")
  {
    std::vector<std::uint8_t> image = validELF();
    image[4] = 2;
    NekoSystem system;
    REQUIRE_THROWS_WITH(
      system.loadELF(image),
      "ELF is not a 32-bit image.");
  }

  SECTION("Byte order")
  {
    std::vector<std::uint8_t> image = validELF();
    image[5] = 2;
    NekoSystem system;
    REQUIRE_THROWS_WITH(
      system.loadELF(image),
      "ELF is not little-endian.");
  }

  SECTION("Executable type")
  {
    std::vector<std::uint8_t> image = validELF();
    writeU16(&image, 16, 1);
    NekoSystem system;
    REQUIRE_THROWS_WITH(
      system.loadELF(image),
      "ELF is not an executable image.");
  }

  SECTION("Machine")
  {
    std::vector<std::uint8_t> image = validELF();
    writeU16(&image, 18, 3);
    NekoSystem system;
    REQUIRE_THROWS_WITH(
      system.loadELF(image),
      "ELF does not target MIPS.");
  }

  SECTION("Program header table")
  {
    std::vector<std::uint8_t> image = validELF();
    writeU32(&image, 28, 0x200);
    NekoSystem system;
    REQUIRE_THROWS_WITH(
      system.loadELF(image),
      "ELF program-header table extends beyond the image.");
  }

  SECTION("Header size")
  {
    std::vector<std::uint8_t> image = validELF();
    writeU16(&image, 40, 51);
    NekoSystem system;
    REQUIRE_THROWS_WITH(
      system.loadELF(image),
      "ELF header size is invalid.");
  }

  SECTION("Program header size")
  {
    std::vector<std::uint8_t> image = validELF();
    writeU16(&image, 42, 31);
    NekoSystem system;
    REQUIRE_THROWS_WITH(
      system.loadELF(image),
      "ELF program-header size is invalid.");
  }

  SECTION("Processor flags remain compatible with EE toolchain output")
  {
    std::vector<std::uint8_t> image = validELF();
    writeU32(&image, 36, 1);
    NekoSystem system;
    REQUIRE_NOTHROW(system.loadELF(image));
  }
}

TEST_CASE("PS2 ELF segment and entry contracts are validated")
{
  SECTION("File size cannot exceed memory size")
  {
    std::vector<std::uint8_t> image = validELF();
    writeU32(
      &image,
      PROGRAM_HEADER_OFFSET + 16,
      9);
    NekoSystem system;
    REQUIRE_THROWS_WITH(
      system.loadELF(image),
      "ELF segment file size exceeds its memory size.");
  }

  SECTION("File data must be present")
  {
    std::vector<std::uint8_t> image = validELF();
    writeU32(
      &image,
      PROGRAM_HEADER_OFFSET + 4,
      0x400);
    NekoSystem system;
    REQUIRE_THROWS_WITH(
      system.loadELF(image),
      "ELF segment data extends beyond the image.");
  }

  SECTION("Memory range must map to EE RAM")
  {
    std::vector<std::uint8_t> image = validELF();
    writeU32(
      &image,
      PROGRAM_HEADER_OFFSET + 8,
      EEMemoryMap::MAIN_MEMORY_SIZE);
    NekoSystem system;
    REQUIRE_THROWS_WITH(
      system.loadELF(image),
      "ELF segment is outside EE main memory.");
  }

  SECTION("Memory address cannot overflow")
  {
    std::vector<std::uint8_t> image = validELF();
    writeU32(
      &image,
      PROGRAM_HEADER_OFFSET + 8,
      UINT32_C(0xfffffff0));
    writeU32(
      &image,
      PROGRAM_HEADER_OFFSET + 20,
      0x20);
    writeU32(
      &image,
      PROGRAM_HEADER_OFFSET + 28,
      1);
    NekoSystem system;
    REQUIRE_THROWS_WITH(
      system.loadELF(image),
      "ELF segment address range overflows.");
  }

  SECTION("Aggregate initialization is bounded")
  {
    std::vector<std::uint8_t> image = validELF();
    writeU32(
      &image,
      PROGRAM_HEADER_OFFSET + 20,
      20 * 1024 * 1024);
    writeU32(
      &image,
      PROGRAM_HEADER_OFFSET + PROGRAM_HEADER_SIZE + 20,
      20 * 1024 * 1024);
    NekoSystem system;
    REQUIRE_THROWS_WITH(
      system.loadELF(image),
      "ELF segments require excessive initialization.");
  }

  SECTION("Permission bits are bounded")
  {
    std::vector<std::uint8_t> image = validELF();
    writeU32(
      &image,
      PROGRAM_HEADER_OFFSET + 24,
      0x0d);
    NekoSystem system;
    REQUIRE_THROWS_WITH(
      system.loadELF(image),
      "ELF segment permissions are invalid.");
  }

  SECTION("Alignment must be a power of two")
  {
    std::vector<std::uint8_t> image = validELF();
    writeU32(
      &image,
      PROGRAM_HEADER_OFFSET + 28,
      3);
    NekoSystem system;
    REQUIRE_THROWS_WITH(
      system.loadELF(image),
      "ELF segment alignment is invalid.");
  }

  SECTION("Address and file alignment must agree")
  {
    std::vector<std::uint8_t> image = validELF();
    writeU32(
      &image,
      PROGRAM_HEADER_OFFSET + 8,
      UINT32_C(0x80001010));
    NekoSystem system;
    REQUIRE_THROWS_WITH(
      system.loadELF(image),
      "ELF segment address and file alignment disagree.");
  }

  SECTION("Entry point must be executable")
  {
    std::vector<std::uint8_t> image = validELF();
    writeU32(
      &image,
      PROGRAM_HEADER_OFFSET + 24,
      4);
    NekoSystem system;
    REQUIRE_THROWS_WITH(
      system.loadELF(image),
      "ELF entry point is not in an executable segment.");
  }

  SECTION("Entry point must be instruction aligned")
  {
    std::vector<std::uint8_t> image = validELF();
    writeU32(&image, 24, UINT32_C(0x80001002));
    NekoSystem system;
    REQUIRE_THROWS_WITH(
      system.loadELF(image),
      "ELF entry point is not instruction-aligned.");
  }

  SECTION("A loadable segment is required")
  {
    std::vector<std::uint8_t> image = validELF();
    writeU32(&image, PROGRAM_HEADER_OFFSET, 0);
    writeU32(
      &image,
      PROGRAM_HEADER_OFFSET + PROGRAM_HEADER_SIZE,
      0);
    NekoSystem system;
    REQUIRE_THROWS_WITH(
      system.loadELF(image),
      "ELF contains no nonempty loadable segments.");
  }
}

TEST_CASE("Invalid PS2 ELF loads are transactional")
{
  NekoSystem system;
  system.eeBus().write32(0x1000, UINT32_C(0xfeedface));
  system.eeCore().setProgramCounter(UINT32_C(0x12345678));
  std::vector<std::uint8_t> image = validELF();
  writeU32(
    &image,
    PROGRAM_HEADER_OFFSET + PROGRAM_HEADER_SIZE + 8,
    EEMemoryMap::MAIN_MEMORY_SIZE);

  REQUIRE_THROWS(system.loadELF(image));
  REQUIRE(
    system.eeBus().read32(0x1000) ==
    UINT32_C(0xfeedface));
  REQUIRE(
    system.eeCore().programCounter() ==
    UINT32_C(0x12345678));
}

TEST_CASE("PS2 ELF loading requires a halted EE")
{
  NekoSystem system;
  system.eeBus().write32(0x1000, UINT32_C(0xfeedface));
  system.eeBus().write32(0, 0);
  system.eeCore().startExecution(0);

  REQUIRE_THROWS_WITH(
    system.loadELF(validELF()),
    "An ELF cannot be loaded while the EE is running.");
  REQUIRE(
    system.eeBus().read32(0x1000) ==
    UINT32_C(0xfeedface));
  REQUIRE(system.eeCore().programCounter() == 0);
}

TEST_CASE("PS2 ELF loading clears stale execution state")
{
  SECTION("Pending branch")
  {
    NekoSystem system;
    system.eeBus().write32(
      0,
      (UINT32_C(0x04) << 26) | 2);
    system.eeBus().write32(4, 0);
    system.eeCore().startExecution(0);
    system.clockMasterCycle();
    system.eeCore().haltExecution();

    const EEELFLoadResult loaded =
      system.loadELF(validELF());
    system.eeCore().setCOP0Register(EECOP0Register::Status, 0);
    system.eeCore().startExecution(loaded.entryPoint);

    REQUIRE(system.stepEEInstruction(1).instructions == 1);
    REQUIRE(
      system.eeCore().programCounter() ==
      loaded.entryPoint + 4);
    REQUIRE(system.eeCore().generalRegister(1).low == 42);
  }

  SECTION("Pending multiply")
  {
    NekoSystem system;
    system.eeCore().setGeneralRegister(1, {6, 0});
    system.eeCore().setGeneralRegister(2, {7, 0});
    system.eeBus().write32(
      0,
      (UINT32_C(1) << 21) |
        (UINT32_C(2) << 16) |
        (UINT32_C(4) << 11) |
        UINT32_C(0x18));
    system.eeCore().startExecution(0);
    system.clockMasterCycle();
    system.eeCore().haltExecution();

    const EEELFLoadResult loaded =
      system.loadELF(validELF());
    system.eeCore().setCOP0Register(EECOP0Register::Status, 0);
    system.eeCore().startExecution(loaded.entryPoint);

    REQUIRE(system.stepEEInstruction(1).instructions == 1);
    REQUIRE(system.eeCore().generalRegister(1).low == 42);
    REQUIRE(system.eeCore().generalRegister(4).low == 0);
  }
}

TEST_CASE("PS2 ELF loading establishes bare-metal EE state")
{
  NekoSystem system;
  system.eeCore().setGeneralRegister(
    1,
    {UINT64_MAX, UINT64_MAX});
  system.eeCore().setGeneralRegister(
    29,
    {UINT64_MAX, UINT64_MAX});
  system.eeCore().setGeneralRegister(
    31,
    {UINT64_MAX, UINT64_MAX});
  system.eeCore().setHI(UINT64_MAX);
  system.eeCore().setLO(UINT64_MAX);
  system.eeCore().setHI1(UINT64_MAX);
  system.eeCore().setLO1(UINT64_MAX);
  system.eeCore().setShiftAmount(UINT32_MAX);
  system.eeCore().setCOP0Register(EECOP0Register::Status, 0);
  system.eeCore().startExecution(0);
  system.clockMasterCycle();
  system.eeCore().haltExecution();

  const EEELFLoadResult loaded = system.loadELF(validELF());

  REQUIRE(
    system.eeCore().executionState() ==
    EEExecutionState::Halted);
  REQUIRE(system.eeCore().stopReason() == EEStopReason::None);
  REQUIRE(system.eeCore().programCounter() == loaded.entryPoint);
  REQUIRE(system.eeCore().generalRegister(1) == EERegister128{});
  REQUIRE(
    system.eeCore().generalRegister(29) ==
    EERegister128{EEGuestRuntime::STACK_POINTER, 0});
  REQUIRE(
    system.eeCore().generalRegister(31) ==
    EERegister128{EEGuestRuntime::RETURN_ADDRESS, 0});
  REQUIRE(system.eeCore().hi() == 0);
  REQUIRE(system.eeCore().lo() == 0);
  REQUIRE(system.eeCore().hi1() == 0);
  REQUIRE(system.eeCore().lo1() == 0);
  REQUIRE(system.eeCore().shiftAmount() == 0);
  REQUIRE(
    system.eeCore().cop0Register(EECOP0Register::Status) ==
    EECOP0Status::RESET);
  REQUIRE(system.eeCore().elapsedCycles() == 0);
}

TEST_CASE("PS2 ELF guests report bounded host outcomes")
{
  SECTION("Successful return")
  {
    NekoSystem system;
    const EEGuestExecutionResult result =
      system.runELF(returningELF(0), 3);

    REQUIRE(result.outcome == EEGuestOutcome::Completed);
    REQUIRE(result.exitCode == 0);
    REQUIRE(result.execution.instructions == 3);
    REQUIRE_FALSE(result.execution.cycleLimitReached);
    REQUIRE(
      result.execution.state ==
      EEExecutionState::Halted);
    REQUIRE(
      result.execution.programCounter ==
      EEGuestRuntime::RETURN_ADDRESS);
  }

  SECTION("Guest-reported failure")
  {
    NekoSystem system;
    const EEGuestExecutionResult result =
      system.runELF(returningELF(7), 8);

    REQUIRE(
      result.outcome ==
      EEGuestOutcome::GuestReportedFailure);
    REQUIRE(result.exitCode == 7);
  }

  SECTION("Cycle limit")
  {
    std::vector<std::uint8_t> image = validELF();
    writeU32(&image, 0x100, UINT32_C(0x1000ffff));
    writeU32(&image, 0x104, 0);
    NekoSystem system;
    const EEGuestExecutionResult result =
      system.runELF(image, 4);

    REQUIRE(
      result.outcome ==
      EEGuestOutcome::CycleLimitReached);
    REQUIRE(result.execution.cycleLimitReached);
    REQUIRE(
      result.execution.state ==
      EEExecutionState::Running);
  }

  SECTION("Exception")
  {
    NekoSystem system;
    const EEGuestExecutionResult result =
      system.runELF(validELF(), 8);

    REQUIRE(result.outcome == EEGuestOutcome::Exception);
    REQUIRE(
      result.execution.pendingException ==
      EEException::SystemCall);
  }
}
