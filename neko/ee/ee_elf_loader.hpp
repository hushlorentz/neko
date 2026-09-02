#ifndef EE_ELF_LOADER_HPP
#define EE_ELF_LOADER_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

class EEBus;

struct EEELFLoadResult
{
  std::uint32_t entryPoint = 0;
  std::uint32_t loadedSegments = 0;
  std::uint64_t fileBytes = 0;
  std::uint64_t zeroedBytes = 0;
};

EEELFLoadResult loadEEELF(
  const std::vector<std::uint8_t> &image,
  EEBus *bus);

#endif
