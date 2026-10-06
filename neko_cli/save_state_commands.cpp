#include "save_state_commands.hpp"

#include "save_state_diagnostics.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>

namespace
{
  const char *USAGE =
    "Usage: neko save-state inspect <file> [--bytes <path>]...\n"
    "       neko save-state locate <file> <path> [--bytes]\n"
    "       neko save-state diff <left> <right> [--bytes <path>]...";

  SaveStateDiagnosticOptions
  parseRetainedPaths(
    const std::vector<std::string> &arguments,
    std::size_t firstOption)
  {
    SaveStateDiagnosticOptions options;
    for (std::size_t index = firstOption;
         index < arguments.size();
         index += 2)
    {
      if (arguments[index] != "--bytes" ||
          index + 1 >= arguments.size() ||
          arguments[index + 1].empty())
      {
        throw std::invalid_argument(USAGE);
      }
      options.retainedBytePaths.push_back(arguments[index + 1]);
    }
    return options;
  }

  int inspect(
    const std::vector<std::string> &arguments,
    const neko_cli::SaveStateFileLoader &loadFile,
    std::ostream &output)
  {
    if (arguments.size() < 2)
    {
      throw std::invalid_argument(USAGE);
    }
    const auto options = parseRetainedPaths(arguments, 2);
    const auto snapshot = inspectSaveState(
      loadFile(arguments[1]),
      options);
    for (const auto &field : snapshot.fields)
    {
      writeSaveStateField(output, field);
      output << '\n';
    }
    return 0;
  }

  int locate(
    const std::vector<std::string> &arguments,
    const neko_cli::SaveStateFileLoader &loadFile,
    std::ostream &output,
    std::ostream &error)
  {
    if (arguments.size() < 3 || arguments.size() > 4 ||
        (arguments.size() == 4 && arguments[3] != "--bytes"))
    {
      throw std::invalid_argument(USAGE);
    }

    SaveStateDiagnosticOptions options;
    if (arguments.size() == 4)
    {
      options.retainedBytePaths.push_back(arguments[2]);
    }
    const auto snapshot = inspectSaveState(
      loadFile(arguments[1]),
      options);
    const auto *field = snapshot.find(arguments[2]);
    if (field == nullptr)
    {
      error <<
        "Save-state field not found: " <<
        arguments[2] <<
        '\n';
      return 1;
    }
    writeSaveStateField(output, *field);
    output << '\n';
    return 0;
  }

  int diff(
    const std::vector<std::string> &arguments,
    const neko_cli::SaveStateFileLoader &loadFile,
    std::ostream &output)
  {
    if (arguments.size() < 3)
    {
      throw std::invalid_argument(USAGE);
    }
    const auto options = parseRetainedPaths(arguments, 3);
    const auto differences = diffSaveStates(
      inspectSaveState(
        loadFile(arguments[1]),
        options),
      inspectSaveState(
        loadFile(arguments[2]),
        options));
    if (differences.empty())
    {
      output << "No semantic differences.\n";
      return 0;
    }
    for (const auto &difference : differences)
    {
      writeSaveStateDifference(output, difference);
      output << '\n';
    }
    return 1;
  }

  std::vector<std::uint8_t> loadSaveStateFile(
    const std::string &path)
  {
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
      throw std::runtime_error(
        "Cannot read save state: " + path);
    }
    std::vector<std::uint8_t> bytes{
      std::istreambuf_iterator<char>(file),
      std::istreambuf_iterator<char>()};
    if (file.bad())
    {
      throw std::runtime_error(
        "Cannot read save state: " + path);
    }
    return bytes;
  }
}

int neko_cli::runSaveStateCommand(
  const std::vector<std::string> &arguments,
  const SaveStateFileLoader &loadFile,
  std::ostream &output,
  std::ostream &error)
{
  try
  {
    if (arguments.empty())
    {
      throw std::invalid_argument(USAGE);
    }
    if (arguments[0] == "inspect")
    {
      return inspect(arguments, loadFile, output);
    }
    if (arguments[0] == "locate")
    {
      return locate(arguments, loadFile, output, error);
    }
    if (arguments[0] == "diff")
    {
      return diff(arguments, loadFile, output);
    }
    throw std::invalid_argument(USAGE);
  }
  catch (const std::exception &exception)
  {
    error << exception.what() << '\n';
    return 2;
  }
}

int neko_cli::runSaveStateCommand(
  const std::vector<std::string> &arguments,
  std::ostream &output,
  std::ostream &error)
{
  return runSaveStateCommand(
    arguments,
    loadSaveStateFile,
    output,
    error);
}
