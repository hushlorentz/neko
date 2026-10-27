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
  using BIOSCheckpointWriter = std::function<void(
    const std::string &, const std::vector<std::uint8_t> &)>;

  int runBIOSStartupCommand(
    const std::vector<std::string> &arguments,
    const BIOSFileLoader &loadFile,
    const BIOSFileLoader &loadCheckpoint,
    const BIOSCheckpointWriter &saveCheckpoint,
    std::ostream &output,
    std::ostream &error);

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
