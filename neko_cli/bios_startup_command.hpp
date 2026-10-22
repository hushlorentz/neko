#pragma once

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

namespace neko_cli
{
  using BIOSFileLoader = std::function<
    std::vector<std::uint8_t>(const std::string &)>;

  int runBIOSStartupCommand(
    const std::vector<std::string> &arguments,
    const BIOSFileLoader &loadFile,
    std::ostream &output,
    std::ostream &error);

  int runBIOSStartupCommand(
    const std::vector<std::string> &arguments,
    std::ostream &output,
    std::ostream &error);
}
