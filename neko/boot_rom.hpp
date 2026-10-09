#ifndef BOOT_ROM_HPP
#define BOOT_ROM_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

class BootROMImage final
{
  public:
    static constexpr std::size_t SIZE = 4 * 1024 * 1024;

    static std::shared_ptr<const BootROMImage> create(
      const std::vector<std::uint8_t> &source)
    {
      if (source.size() != SIZE)
      {
        return nullptr;
      }
      return std::shared_ptr<const BootROMImage>(
        new BootROMImage(source));
    }

    std::size_t size() const
    {
      return bytes.size();
    }

    const std::uint8_t *data() const
    {
      return bytes.data();
    }

    std::uint8_t operator[](std::size_t index) const
    {
      return bytes[index];
    }

  private:
    explicit BootROMImage(
      const std::vector<std::uint8_t> &source) :
      bytes(source)
    {
    }

    const std::vector<std::uint8_t> bytes;
};

#endif
