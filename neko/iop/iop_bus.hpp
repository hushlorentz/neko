#ifndef IOP_BUS_HPP
#define IOP_BUS_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "boot_rom.hpp"
#include "iop_types.hpp"

enum class IOPBusStatus : std::uint8_t
{
  Completed,
  Misaligned,
  ReadOnly,
  Unmapped
};

struct IOPBusReadResult
{
  IOPBusStatus status = IOPBusStatus::Unmapped;
  IOPWord value = 0;
};

struct IOPBusWriteResult
{
  IOPBusStatus status = IOPBusStatus::Unmapped;
};

// Physical-address boundary for the IOP. Guest accessors and host
// load/inspect helpers are separate; neither selects CPU exceptions.
class IOPBus final
{
  public:
    static constexpr std::size_t RAM_SIZE = 2 * 1024 * 1024;
    static constexpr std::size_t RAM_MIRROR_COUNT = 4;
    static constexpr std::size_t SCRATCHPAD_SIZE = 1024;
    static constexpr std::size_t ROM_SIZE = BootROMImage::SIZE;
    static constexpr IOPAddress RAM_BASE = 0x00000000;
    static constexpr IOPAddress SCRATCHPAD_BASE = 0x1f800000;
    static constexpr IOPAddress ROM_BASE = 0x1fc00000;

    IOPBus();

    // Clears RAM and scratchpad; the installed ROM view is configuration.
    void reset();

    // The shared image owns an immutable copy of its source bytes.
    bool installRom(
      std::shared_ptr<const BootROMImage> image);

    IOPBusReadResult read8(IOPAddress address) const;
    IOPBusReadResult read16(IOPAddress address) const;
    IOPBusReadResult read32(IOPAddress address) const;
    IOPBusWriteResult write8(IOPAddress address, std::uint8_t value);
    IOPBusWriteResult write16(IOPAddress address, std::uint16_t value);
    IOPBusWriteResult write32(IOPAddress address, IOPWord value);

    // Host-only physical access. A range must lie wholly inside one RAM
    // alias, the scratchpad, or (inspection only) the installed ROM.
    IOPBusStatus loadPhysical(
      IOPAddress address,
      const std::uint8_t *bytes,
      std::size_t count);
    IOPBusStatus inspectPhysical(
      IOPAddress address,
      std::uint8_t *bytes,
      std::size_t count) const;

  private:
    enum class Region : std::uint8_t
    {
      Unmapped,
      Ram,
      Scratchpad,
      Rom
    };

    struct Location
    {
      Region region = Region::Unmapped;
      std::size_t offset = 0;
    };

    Location locate(IOPAddress address, std::size_t count) const;
    const std::uint8_t *readableBytes(const Location &location) const;
    std::uint8_t *writableBytes(const Location &location);
    IOPBusReadResult readScalar(
      IOPAddress address,
      std::size_t width) const;
    IOPBusWriteResult writeScalar(
      IOPAddress address,
      std::size_t width,
      IOPWord value);

    std::vector<std::uint8_t> ram;
    std::array<std::uint8_t, SCRATCHPAD_SIZE> scratchpad = {};
    std::shared_ptr<const BootROMImage> rom;
};

#endif
