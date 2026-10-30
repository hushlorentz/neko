#include "save_state_commands.hpp"

#include "save_state_diagnostics.hpp"
#include "iop_cache.hpp"

#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace
{
  const char *USAGE =
    "Usage: neko save-state inspect <file> [--bytes <path>]...\n"
    "       neko save-state locate <file> <path> [--bytes]\n"
    "       neko save-state diff <left> <right> [--bytes <path>]...\n"
    "       neko save-state iop-memory <file> <physical-address> --words <1-256>\n"
    "       neko save-state ee-memory <file> <physical-address> --words <1-256>";

  std::uint32_t parseMemoryInteger(const std::string &text)
  {
    std::size_t first = 0;
    unsigned int base = 10;
    if (text.size() >= 2 && text[0] == '0' && text[1] == 'x')
    {
      first = 2;
      base = 16;
    }
    if (first == text.size())
    {
      throw std::invalid_argument("Expected a decimal or 0x-prefixed integer.");
    }
    std::uint32_t value = 0;
    for (std::size_t index = first; index < text.size(); ++index)
    {
      const char digit = text[index];
      unsigned int number;
      if (digit >= '0' && digit <= '9')
      {
        number = static_cast<unsigned int>(digit - '0');
      }
      else if (digit >= 'a' && digit <= 'f')
      {
        number = static_cast<unsigned int>(digit - 'a') + 10;
      }
      else if (digit >= 'A' && digit <= 'F')
      {
        number = static_cast<unsigned int>(digit - 'A') + 10;
      }
      else
      {
        throw std::invalid_argument("Expected a decimal or 0x-prefixed integer.");
      }
      if (number >= base ||
          value > (std::numeric_limits<std::uint32_t>::max() - number) / base)
      {
        throw std::invalid_argument("Memory argument exceeds a 32-bit integer.");
      }
      value = value * base + number;
    }
    return value;
  }

  struct MemoryWindow
  {
    std::uint32_t address;
    std::uint32_t count;
  };

  MemoryWindow parseMemoryWindow(const std::vector<std::string> &arguments,
    std::uint32_t ramSize, const char *cpu)
  {
    if (arguments.size() != 5 || arguments[1].empty() ||
        arguments[3] != "--words")
    {
      throw std::invalid_argument(USAGE);
    }
    const auto address = parseMemoryInteger(arguments[2]);
    const auto count = parseMemoryInteger(arguments[4]);
    if ((address & 3) != 0 || count == 0 || count > 256 ||
        address >= ramSize ||
        count > (ramSize - address) / sizeof(std::uint32_t))
    {
      throw std::invalid_argument(std::string(cpu) +
        " memory range must contain 1-256 aligned words inside physical RAM.");
    }
    return {address, count};
  }

  std::uint32_t readRAMWord(
    const std::vector<std::uint8_t> &bytes, std::uint32_t address)
  {
    std::uint32_t word = 0;
    for (unsigned int lane = 0; lane < 4; ++lane)
    {
      word |= std::uint32_t(bytes[address + lane]) << (8 * lane);
    }
    return word;
  }

  const SaveStateDiagnosticField &requiredField(
    const SaveStateDiagnosticSnapshot &snapshot,
    const std::string &path, SaveStateFieldKind kind)
  {
    const auto *field = snapshot.find(path);
    if (field == nullptr || field->kind != kind)
    {
      throw std::runtime_error("Missing or incompatible save-state field: " + path);
    }
    return *field;
  }

  void writeHexWord(std::ostream &output, std::uint32_t word)
  {
    output << "0x" << std::hex << std::setfill('0') << std::setw(8) << word
           << std::dec;
  }

  void writeCacheWord(std::ostream &output,
    const SaveStateDiagnosticSnapshot &snapshot,
    IOPCacheBank bank, IOPAddress address)
  {
    const auto location = IOPCache::location(bank, address);
    const std::string prefix = std::string("system.iop.core.cache.") +
      (bank == IOPCacheBank::Instruction ? "instructionLines[" : "dataLines[") +
      std::to_string(location.lineIndex) + "].";
    const auto tag = requiredField(
      snapshot, prefix + "physicalTag", SaveStateFieldKind::U32).scalarValue;
    const auto valid = requiredField(
      snapshot, prefix + "valid", SaveStateFieldKind::Boolean).scalarValue != 0;
    const auto word = requiredField(snapshot,
      prefix + "words[" + std::to_string(location.wordIndex) + "].value",
      SaveStateFieldKind::U32).scalarValue;
    output << "{index=" << location.lineIndex << " tag=";
    writeHexWord(output, static_cast<std::uint32_t>(tag));
    output << " valid=" << (valid ? "true" : "false")
           << " match=" << (valid && tag == location.physicalTag ? "true" : "false")
           << " word=";
    writeHexWord(output, static_cast<std::uint32_t>(word));
    output << '}';
  }

  int inspectIOPMemory(const std::vector<std::string> &arguments,
    const neko_cli::SaveStateFileLoader &loadFile, std::ostream &output)
  {
    const auto window = parseMemoryWindow(arguments, IOPBus::RAM_SIZE, "IOP");
    SaveStateDiagnosticOptions options;
    options.retainedBytePaths.push_back("system.iop.bus.ram.bytes");
    const auto snapshot = inspectSaveState(loadFile(arguments[1]), options);
    const auto &ram = requiredField(
      snapshot, "system.iop.bus.ram.bytes", SaveStateFieldKind::Bytes);
    if (!ram.bytes || ram.bytes->size() != IOPBus::RAM_SIZE ||
        requiredField(snapshot, "system.iop.core.cache.profile",
          SaveStateFieldKind::U32).scalarValue != 1)
    {
      throw std::runtime_error("Unsupported IOP memory/cache snapshot profile.");
    }
    // Buffer the bounded report so invalid snapshots never emit partial evidence.
    std::ostringstream report;
    for (std::uint32_t index = 0; index < window.count; ++index)
    {
      const auto current = window.address + index * 4;
      const auto word = readRAMWord(*ram.bytes, current);
      writeHexWord(report, current);
      report << " ram=";
      writeHexWord(report, word);
      report << " instruction=";
      writeCacheWord(report, snapshot, IOPCacheBank::Instruction, current);
      report << " data=";
      writeCacheWord(report, snapshot, IOPCacheBank::Data, current);
      report << '\n';
    }
    output << report.str();
    return 0;
  }

  int inspectEEMemory(const std::vector<std::string> &arguments,
    const neko_cli::SaveStateFileLoader &loadFile, std::ostream &output)
  {
    const auto window =
      parseMemoryWindow(arguments, EEMemoryMap::MAIN_MEMORY_SIZE, "EE");
    SaveStateDiagnosticOptions options;
    options.retainedBytePaths.push_back("system.eeBus.mainMemory.bytes");
    const auto snapshot = inspectSaveState(loadFile(arguments[1]), options);
    const auto &ram = requiredField(
      snapshot, "system.eeBus.mainMemory.bytes", SaveStateFieldKind::Bytes);
    if (!ram.bytes || ram.bytes->size() != EEMemoryMap::MAIN_MEMORY_SIZE)
    {
      throw std::runtime_error("Unsupported EE RAM snapshot size.");
    }
    std::ostringstream report;
    report <<
      "EE physical RAM backing only; not an instruction-cache or fetch view.\n";
    for (const auto &field : {
        std::make_pair("container.version", SaveStateFieldKind::U32),
        std::make_pair("container.checksum", SaveStateFieldKind::U64),
        std::make_pair("system.masterClock.masterCycle", SaveStateFieldKind::U64),
        std::make_pair("system.eeBus.romPresent", SaveStateFieldKind::Boolean),
        std::make_pair("system.eeBus.romFingerprint", SaveStateFieldKind::U64)})
    {
      writeSaveStateField(report, requiredField(snapshot, field.first, field.second));
      report << '\n';
    }
    for (std::uint32_t index = 0; index < window.count; ++index)
    {
      const auto current = window.address + index * 4;
      writeHexWord(report, current);
      report << " ram=";
      writeHexWord(report, readRAMWord(*ram.bytes, current));
      report << '\n';
    }
    output << report.str();
    return 0;
  }

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
}

std::vector<std::uint8_t> neko_cli::loadSaveStateFile(
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
    if (arguments[0] == "iop-memory")
    {
      return inspectIOPMemory(arguments, loadFile, output);
    }
    if (arguments[0] == "ee-memory")
    {
      return inspectEEMemory(arguments, loadFile, output);
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
