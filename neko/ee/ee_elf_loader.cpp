#include "ee_elf_loader.hpp"

#include <limits>
#include <stdexcept>

#include "ee_bus.hpp"

namespace
{
  constexpr std::size_t ELF32_HEADER_SIZE = 52;
  constexpr std::size_t ELF32_PROGRAM_HEADER_SIZE = 32;
  constexpr std::uint32_t ELF_VERSION_CURRENT = 1;
  constexpr std::uint16_t ELF_TYPE_EXECUTABLE = 2;
  constexpr std::uint16_t ELF_MACHINE_MIPS = 8;
  constexpr std::uint32_t ELF_PROGRAM_TYPE_LOAD = 1;
  constexpr std::uint32_t ELF_PERMISSION_EXECUTE = 1;
  constexpr std::uint32_t ELF_PERMISSION_WRITE = 2;
  constexpr std::uint32_t ELF_PERMISSION_READ = 4;
  constexpr std::uint32_t ELF_PERMISSION_MASK =
    ELF_PERMISSION_EXECUTE |
    ELF_PERMISSION_WRITE |
    ELF_PERMISSION_READ;

  struct LoadSegment
  {
    std::uint32_t fileOffset = 0;
    std::uint32_t virtualAddress = 0;
    std::uint32_t fileSize = 0;
    std::uint32_t memorySize = 0;
    std::uint32_t flags = 0;
  };

  void requireELF(bool condition, const char *message)
  {
    if (!condition)
    {
      throw std::invalid_argument(message);
    }
  }

  bool rangeFits(
    std::size_t offset,
    std::size_t size,
    std::size_t limit)
  {
    return offset <= limit && size <= limit - offset;
  }

  std::uint16_t readU16(
    const std::vector<std::uint8_t> &image,
    std::size_t offset)
  {
    requireELF(
      rangeFits(offset, 2, image.size()),
      "ELF field extends beyond the image.");
    return
      image[offset] |
      (static_cast<std::uint16_t>(image[offset + 1]) << 8);
  }

  std::uint32_t readU32(
    const std::vector<std::uint8_t> &image,
    std::size_t offset)
  {
    requireELF(
      rangeFits(offset, 4, image.size()),
      "ELF field extends beyond the image.");
    return
      image[offset] |
      (static_cast<std::uint32_t>(image[offset + 1]) << 8) |
      (static_cast<std::uint32_t>(image[offset + 2]) << 16) |
      (static_cast<std::uint32_t>(image[offset + 3]) << 24);
  }

  bool isPowerOfTwo(std::uint32_t value)
  {
    return value != 0 && (value & (value - 1)) == 0;
  }

  bool addressRangeFits(
    std::uint32_t address,
    std::uint32_t size)
  {
    return
      static_cast<std::uint64_t>(address) + size <=
      static_cast<std::uint64_t>(
        std::numeric_limits<std::uint32_t>::max()) + 1;
  }
}

EEELFLoadResult loadEEELF(
  const std::vector<std::uint8_t> &image,
  EEBus *bus)
{
  requireELF(bus != nullptr, "ELF loading requires an EE bus.");
  requireELF(
    image.size() >= ELF32_HEADER_SIZE,
    "ELF image is smaller than its header.");
  requireELF(
    image[0] == 0x7f &&
      image[1] == 'E' &&
      image[2] == 'L' &&
      image[3] == 'F',
    "ELF magic is invalid.");
  requireELF(image[4] == 1, "ELF is not a 32-bit image.");
  requireELF(image[5] == 1, "ELF is not little-endian.");
  requireELF(image[6] == 1, "ELF identification version is invalid.");
  requireELF(
    readU16(image, 16) == ELF_TYPE_EXECUTABLE,
    "ELF is not an executable image.");
  requireELF(
    readU16(image, 18) == ELF_MACHINE_MIPS,
    "ELF does not target MIPS.");
  requireELF(
    readU32(image, 20) == ELF_VERSION_CURRENT,
    "ELF version is invalid.");
  requireELF(
    readU16(image, 40) == ELF32_HEADER_SIZE,
    "ELF header size is invalid.");
  requireELF(
    readU16(image, 42) == ELF32_PROGRAM_HEADER_SIZE,
    "ELF program-header size is invalid.");

  // The SCEI EE documentation does not define an e_flags filter.
  // Validate it against a real PS2DEV fixture before restricting it.
  static_cast<void>(readU32(image, 36));
  const std::uint32_t entryPoint = readU32(image, 24);
  const std::uint32_t programHeaderOffset = readU32(image, 28);
  const std::uint16_t programHeaderCount = readU16(image, 44);
  requireELF(
    rangeFits(
      programHeaderOffset,
      static_cast<std::size_t>(programHeaderCount) *
        ELF32_PROGRAM_HEADER_SIZE,
      image.size()),
    "ELF program-header table extends beyond the image.");

  std::vector<LoadSegment> segments;
  segments.reserve(programHeaderCount);
  bool entryPointExecutable = false;
  std::uint64_t totalInitializationBytes = 0;
  EEELFLoadResult result;
  result.entryPoint = entryPoint;

  for (std::uint16_t index = 0;
       index < programHeaderCount;
       ++index)
  {
    const std::size_t offset =
      programHeaderOffset +
      static_cast<std::size_t>(index) *
        ELF32_PROGRAM_HEADER_SIZE;
    if (readU32(image, offset) != ELF_PROGRAM_TYPE_LOAD)
    {
      continue;
    }

    LoadSegment segment;
    segment.fileOffset = readU32(image, offset + 4);
    segment.virtualAddress = readU32(image, offset + 8);
    segment.fileSize = readU32(image, offset + 16);
    segment.memorySize = readU32(image, offset + 20);
    segment.flags = readU32(image, offset + 24);
    const std::uint32_t alignment =
      readU32(image, offset + 28);

    requireELF(
      segment.fileSize <= segment.memorySize,
      "ELF segment file size exceeds its memory size.");
    requireELF(
      rangeFits(
        segment.fileOffset,
        segment.fileSize,
        image.size()),
      "ELF segment data extends beyond the image.");
    requireELF(
      addressRangeFits(
        segment.virtualAddress,
        segment.memorySize),
      "ELF segment address range overflows.");
    requireELF(
      (segment.flags & ~ELF_PERMISSION_MASK) == 0,
      "ELF segment permissions are invalid.");
    requireELF(
      alignment == 0 ||
        alignment == 1 ||
        isPowerOfTwo(alignment),
      "ELF segment alignment is invalid.");
    requireELF(
      alignment <= 1 ||
        (segment.virtualAddress & (alignment - 1)) ==
        (segment.fileOffset & (alignment - 1)),
      "ELF segment address and file alignment disagree.");
    requireELF(
      segment.memorySize == 0 ||
        bus->isMainMemoryRange(
          segment.virtualAddress,
          segment.memorySize),
      "ELF segment is outside EE main memory.");

    if (segment.memorySize != 0)
    {
      totalInitializationBytes += segment.memorySize;
      requireELF(
        totalInitializationBytes <= EEMemoryMap::MAIN_MEMORY_SIZE,
        "ELF segments require excessive initialization.");
      ++result.loadedSegments;
      result.fileBytes += segment.fileSize;
      result.zeroedBytes +=
        segment.memorySize - segment.fileSize;
      segments.push_back(segment);
      if ((segment.flags & ELF_PERMISSION_EXECUTE) != 0 &&
          entryPoint >= segment.virtualAddress &&
          static_cast<std::uint64_t>(entryPoint) <
            static_cast<std::uint64_t>(
              segment.virtualAddress) +
            segment.memorySize)
      {
        entryPointExecutable = true;
      }
    }
  }

  requireELF(
    !segments.empty(),
    "ELF contains no nonempty loadable segments.");
  requireELF(
    (entryPoint & 3) == 0,
    "ELF entry point is not instruction-aligned.");
  requireELF(
    entryPointExecutable,
    "ELF entry point is not in an executable segment.");

  for (const LoadSegment &segment : segments)
  {
    for (std::uint32_t index = 0;
         index < segment.fileSize;
         ++index)
    {
      const bool written = bus->writeData8(
        segment.virtualAddress + index,
        image[
          static_cast<std::size_t>(segment.fileOffset) +
          index]);
      if (!written)
      {
        throw std::logic_error(
          "Validated ELF segment could not be written.");
      }
    }
    for (std::uint32_t index = segment.fileSize;
         index < segment.memorySize;
         ++index)
    {
      const bool written = bus->writeData8(
        segment.virtualAddress + index,
        0);
      if (!written)
      {
        throw std::logic_error(
          "Validated ELF BSS could not be written.");
      }
    }
  }

  return result;
}
