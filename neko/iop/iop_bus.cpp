#include "iop_bus.hpp"

#include <algorithm>
#include <cassert>
#include <utility>

namespace
{
  bool isAligned(IOPAddress address, std::size_t width)
  {
    return (address & (width - 1)) == 0;
  }
}

constexpr std::size_t IOPBus::RAM_SIZE;
constexpr std::size_t IOPBus::RAM_MIRROR_COUNT;
constexpr std::size_t IOPBus::SCRATCHPAD_SIZE;
constexpr std::size_t IOPBus::ROM_SIZE;
constexpr IOPAddress IOPBus::RAM_BASE;
constexpr IOPAddress IOPBus::SCRATCHPAD_BASE;
constexpr IOPAddress IOPBus::ROM_BASE;

IOPBus::IOPBus() : ram(RAM_SIZE, 0)
{
}

void IOPBus::reset()
{
  std::fill(ram.begin(), ram.end(), static_cast<std::uint8_t>(0));
  scratchpad.fill(0);
}

bool IOPBus::installRom(
  std::shared_ptr<const BootROMImage> image)
{
  if (image == nullptr || image->size() != ROM_SIZE)
  {
    return false;
  }

  rom = std::move(image);
  return true;
}

IOPBus::Location IOPBus::locate(IOPAddress address, std::size_t count) const
{
  const std::uint64_t first = address;
  const std::uint64_t last = first + (count == 0 ? 0 : count - 1);

  const std::uint64_t ramEnd =
    static_cast<std::uint64_t>(RAM_SIZE) * RAM_MIRROR_COUNT;
  if (first < ramEnd)
  {
    if (last / RAM_SIZE != first / RAM_SIZE)
    {
      return {};
    }
    return {Region::Ram, static_cast<std::size_t>(first % RAM_SIZE)};
  }

  if (first >= SCRATCHPAD_BASE &&
      last < static_cast<std::uint64_t>(SCRATCHPAD_BASE) + SCRATCHPAD_SIZE)
  {
    return {Region::Scratchpad,
      static_cast<std::size_t>(first - SCRATCHPAD_BASE)};
  }

  if (rom != nullptr && first >= ROM_BASE &&
      last < static_cast<std::uint64_t>(ROM_BASE) + ROM_SIZE)
  {
    return {Region::Rom, static_cast<std::size_t>(first - ROM_BASE)};
  }

  return {};
}

const std::uint8_t *IOPBus::readableBytes(const Location &location) const
{
  switch (location.region)
  {
    case Region::Ram:
      return ram.data() + location.offset;
    case Region::Scratchpad:
      return scratchpad.data() + location.offset;
    case Region::Rom:
      return rom->data() + location.offset;
    case Region::Unmapped:
      break;
  }
  return nullptr;
}

std::uint8_t *IOPBus::writableBytes(const Location &location)
{
  switch (location.region)
  {
    case Region::Ram:
      return ram.data() + location.offset;
    case Region::Scratchpad:
      return scratchpad.data() + location.offset;
    case Region::Rom:
    case Region::Unmapped:
      break;
  }
  return nullptr;
}

IOPBusReadResult IOPBus::readScalar(IOPAddress address, std::size_t width) const
{
  IOPBusReadResult result;
  if (!isAligned(address, width))
  {
    result.status = IOPBusStatus::Misaligned;
    return result;
  }

  const Location location = locate(address, width);
  if (location.region == Region::Unmapped)
  {
    return result;
  }

  const std::uint8_t *bytes = readableBytes(location);
  for (std::size_t index = 0; index < width; ++index)
  {
    result.value |= static_cast<IOPWord>(bytes[index]) << (8 * index);
  }
  result.status = IOPBusStatus::Completed;
  return result;
}

IOPBusWriteResult IOPBus::writeScalar(
  IOPAddress address,
  std::size_t width,
  IOPWord value)
{
  IOPBusWriteResult result;
  if (!isAligned(address, width))
  {
    result.status = IOPBusStatus::Misaligned;
    return result;
  }

  const Location location = locate(address, width);
  if (location.region == Region::Unmapped)
  {
    return result;
  }
  if (location.region == Region::Rom)
  {
    result.status = IOPBusStatus::ReadOnly;
    return result;
  }

  std::uint8_t *bytes = writableBytes(location);
  for (std::size_t index = 0; index < width; ++index)
  {
    bytes[index] = static_cast<std::uint8_t>(value >> (8 * index));
  }
  result.status = IOPBusStatus::Completed;
  return result;
}

IOPBusReadResult IOPBus::read8(IOPAddress address) const
{
  return readScalar(address, 1);
}

IOPBusReadResult IOPBus::read16(IOPAddress address) const
{
  return readScalar(address, 2);
}

IOPBusReadResult IOPBus::read32(IOPAddress address) const
{
  return readScalar(address, 4);
}

IOPBusWriteResult IOPBus::write8(IOPAddress address, std::uint8_t value)
{
  return writeScalar(address, 1, value);
}

IOPBusWriteResult IOPBus::write16(IOPAddress address, std::uint16_t value)
{
  return writeScalar(address, 2, value);
}

IOPBusWriteResult IOPBus::write32(IOPAddress address, IOPWord value)
{
  return writeScalar(address, 4, value);
}

IOPBusStatus IOPBus::loadPhysical(
  IOPAddress address,
  const std::uint8_t *bytes,
  std::size_t count)
{
  assert(count == 0 || bytes != nullptr);
  const Location location = locate(address, count);
  if (location.region == Region::Unmapped)
  {
    return IOPBusStatus::Unmapped;
  }
  if (location.region == Region::Rom)
  {
    return IOPBusStatus::ReadOnly;
  }

  std::copy(bytes, bytes + count, writableBytes(location));
  return IOPBusStatus::Completed;
}

IOPBusStatus IOPBus::inspectPhysical(
  IOPAddress address,
  std::uint8_t *bytes,
  std::size_t count) const
{
  assert(count == 0 || bytes != nullptr);
  const Location location = locate(address, count);
  if (location.region == Region::Unmapped)
  {
    return IOPBusStatus::Unmapped;
  }

  const std::uint8_t *source = readableBytes(location);
  std::copy(source, source + count, bytes);
  return IOPBusStatus::Completed;
}
