#include "vpu_integration_test_utils.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>

namespace vpu_integration
{
  std::vector<std::uint8_t> readBinary(const std::string &fileName)
  {
    std::string path =
      std::string(NEKO_TEST_FIXTURE_DIR) + "/" + fileName;
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
      throw std::runtime_error("Could not open integration fixture: " + path);
    }

    return std::vector<std::uint8_t>(
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>());
  }

  void appendWord(
    std::vector<std::uint8_t> *bytes,
    std::uint32_t value)
  {
    bytes->push_back(value & 0xff);
    bytes->push_back((value >> 8) & 0xff);
    bytes->push_back((value >> 16) & 0xff);
    bytes->push_back((value >> 24) & 0xff);
  }

  void appendQword(
    std::vector<std::uint8_t> *bytes,
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t z,
    std::uint32_t w)
  {
    appendWord(bytes, x);
    appendWord(bytes, y);
    appendWord(bytes, z);
    appendWord(bytes, w);
  }
}
