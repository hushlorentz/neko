#include "bios_startup_command.hpp"

#include "startup_diagnostics.hpp"

#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace
{
  const char *USAGE =
    "Usage: neko bios-startup <file> --cycles <count>";

  std::uint64_t parseCycleBudget(const std::string &text)
  {
    const char *error =
      "Cycle ceiling must be an unsigned 64-bit decimal integer.";
    if (text.empty())
    {
      throw std::invalid_argument(error);
    }
    std::uint64_t value = 0;
    for (const char character : text)
    {
      if (character < '0' || character > '9')
      {
        throw std::invalid_argument(error);
      }
      const unsigned digit = static_cast<unsigned>(character - '0');
      if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10)
      {
        throw std::invalid_argument(error);
      }
      value = value * 10 + digit;
    }
    return value;
  }

  std::vector<std::uint8_t> loadBIOSFile(const std::string &path)
  {
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
      throw std::runtime_error("Cannot read BIOS image: " + path);
    }
    std::vector<std::uint8_t> bytes(BootROMImage::SIZE);
    file.read(
      reinterpret_cast<char *>(bytes.data()),
      static_cast<std::streamsize>(bytes.size()));
    if (file.bad())
    {
      throw std::runtime_error("Cannot read BIOS image: " + path);
    }
    if (file.gcount() != static_cast<std::streamsize>(bytes.size()) ||
        file.peek() != std::ifstream::traits_type::eof())
    {
      throw std::invalid_argument(
        "BIOS image must be exactly 4194304 bytes: " + path);
    }
    if (file.bad())
    {
      throw std::runtime_error("Cannot read BIOS image: " + path);
    }
    return bytes;
  }
}

int neko_cli::runBIOSStartupCommand(
  const std::vector<std::string> &arguments,
  const BIOSFileLoader &loadFile,
  std::ostream &output,
  std::ostream &error)
{
  try
  {
    if (arguments.size() != 3 || arguments[0].empty() ||
        arguments[1] != "--cycles")
    {
      throw std::invalid_argument(USAGE);
    }
    const auto budget = parseCycleBudget(arguments[2]);
    const auto image = loadFile(arguments[0]);
    NekoSystem system;
    const auto report =
      runDualCPUBIOSWithStartupSummary(system, image, budget);

    std::ostringstream record;
    record.imbue(std::locale::classic());
    record <<
      "{\"type\":\"bios_startup_input\",\"rom_bytes\":" << image.size() <<
      ",\"rom_fnv1a64\":\"" <<
      std::hex << std::setfill('0') << std::setw(16) <<
      system.eeBus().bootROMIdentity()->fingerprint() <<
      std::dec << "\",\"max_master_cycles\":" << budget <<
      ",\"start\":\"cold_dual_reset_vectors\"}\n";
    writeStartupRunSummaryJsonLine(record, report);
    output << record.str();
    output.flush();
    if (!output)
    {
      throw std::runtime_error("Cannot write BIOS startup report.");
    }
    return report.execution.cycleLimitReached ? 0 : 1;
  }
  catch (const std::exception &exception)
  {
    error << exception.what() << '\n';
    return 2;
  }
}

int neko_cli::runBIOSStartupCommand(
  const std::vector<std::string> &arguments,
  std::ostream &output,
  std::ostream &error)
{
  return runBIOSStartupCommand(arguments, loadBIOSFile, output, error);
}
