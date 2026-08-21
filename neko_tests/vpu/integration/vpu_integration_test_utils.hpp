#ifndef VPU_INTEGRATION_TEST_UTILS_H
#define VPU_INTEGRATION_TEST_UTILS_H

#include <cstdint>
#include <string>
#include <vector>

namespace vpu_integration
{
  std::vector<std::uint8_t> readBinary(const std::string &fileName);

  void appendWord(
    std::vector<std::uint8_t> *bytes,
    std::uint32_t value);

  void appendQword(
    std::vector<std::uint8_t> *bytes,
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t z,
    std::uint32_t w);
}

#endif
