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
    "Usage: neko bios-startup <file> --cycles <count> "
    "[--trace-events <count> [--failure-context]]";

  std::uint64_t parseUnsignedInteger(
    const std::string &text, const char *error)
  {
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
    if ((arguments.size() != 3 && arguments.size() != 5 && arguments.size() != 6) ||
        arguments[0].empty() || arguments[1] != "--cycles" ||
        (arguments.size() >= 5 && arguments[3] != "--trace-events") ||
        (arguments.size() == 6 && arguments[5] != "--failure-context"))
    {
      throw std::invalid_argument(USAGE);
    }
    const auto budget = parseUnsignedInteger(
      arguments[2], "Cycle ceiling must be an unsigned 64-bit decimal integer.");
    const bool traceRequested = arguments.size() >= 5;
    const auto traceLimit = traceRequested ?
      parseUnsignedInteger(arguments[4],
        "Trace event ceiling must be an unsigned 64-bit decimal integer.") : 0;
    const auto image = loadFile(arguments[0]);
    NekoSystem system;
    system.runDualCPUBIOS(image, 0);
    const auto capture = arguments.size() == 6 ?
      std::make_shared<StartupFailureCapture>() : nullptr;
    if (traceRequested)
    {
      system.startTrace(traceLimit, capture);
    }
    const auto evidence = runDualCPUWithStartupEvidence(system, budget);
    const auto *runtimeError = evidence.outcome == StartupRunOutcome::RuntimeError ?
      &evidence.error : nullptr;
    if (runtimeError != nullptr)
    {
      error << runtimeError->message << '\n';
    }

    std::ostringstream record;
    record.imbue(std::locale::classic());
    record <<
      "{\"type\":\"bios_startup_input\",\"rom_bytes\":" << image.size() <<
      ",\"rom_fnv1a64\":\"" <<
      std::hex << std::setfill('0') << std::setw(16) <<
      system.eeBus().bootROMIdentity()->fingerprint() <<
      std::dec << "\",\"max_master_cycles\":" << budget <<
      ",\"start\":\"cold_dual_reset_vectors\"}\n";
    if (runtimeError != nullptr)
    {
      writeStartupRuntimeErrorJsonLine(record, *runtimeError);
    }
    else
    {
      writeStartupRunSummaryJsonLine(record, evidence.completed);
    }
    output << record.str();
    if (traceRequested)
    {
      writeStartupTraceJsonLines(output, system);
      if (capture)
      {
        writeStartupFailureCaptureJsonLine(output, *capture);
      }
      writeStartupFirstFailureJsonLine(output, system,
        runtimeError != nullptr ? StartupExecutionBoundary::IncompleteCycle :
          StartupExecutionBoundary::CompleteCycle, capture.get());
    }
    output.flush();
    if (!output)
    {
      throw std::runtime_error("Cannot write BIOS startup report.");
    }
    if (runtimeError != nullptr)
    {
      return 2;
    }
    return evidence.completed.execution.cycleLimitReached ? 0 : 1;
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
