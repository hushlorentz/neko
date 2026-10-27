#pragma once

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

namespace neko_cli
{
  using SaveStateFileLoader = std::function<
    std::vector<std::uint8_t>(const std::string &)>;

  std::vector<std::uint8_t> loadSaveStateFile(const std::string &path);

  int runSaveStateCommand(
    const std::vector<std::string> &arguments,
    const SaveStateFileLoader &loadFile,
    std::ostream &output,
    std::ostream &error);

  int runSaveStateCommand(
    const std::vector<std::string> &arguments,
    std::ostream &output,
    std::ostream &error);
}
