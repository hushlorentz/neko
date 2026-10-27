#include "bios_startup_command.hpp"

#include "save_state_commands.hpp"
#include "startup_diagnostics.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace
{
  const char *USAGE =
    "Usage: neko bios-startup <file> --cycles <count> "
    "[--trace-events <count> [--failure-context] [--trace-from-cycle <count>]] "
    "[--resume-checkpoint <file>] [--save-checkpoint <file>]";

  struct Options
  {
    std::uint64_t ceiling = 0;
    std::uint64_t traceLimit = 0;
    std::uint64_t traceStart = 0;
    bool traced = false;
    bool failureContext = false;
    bool traceStartSpecified = false;
    std::string resumePath;
    std::string savePath;
  };

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

  Options parseOptions(const std::vector<std::string> &arguments)
  {
    if (arguments.size() < 3 || arguments[0].empty() ||
        arguments[1] != "--cycles")
    {
      throw std::invalid_argument(USAGE);
    }
    Options options;
    options.ceiling = parseUnsignedInteger(arguments[2],
      "Cycle ceiling must be an unsigned 64-bit decimal integer.");
    for (std::size_t index = 3; index < arguments.size(); ++index)
    {
      const auto &name = arguments[index];
      if (name == "--failure-context" && !options.failureContext)
      {
        options.failureContext = true;
        continue;
      }
      if (index + 1 >= arguments.size() || arguments[index + 1].empty())
      {
        throw std::invalid_argument(USAGE);
      }
      const auto &value = arguments[++index];
      if (name == "--trace-events" && !options.traced)
      {
        options.traced = true;
        options.traceLimit = parseUnsignedInteger(value,
          "Trace event ceiling must be an unsigned 64-bit decimal integer.");
      }
      else if (name == "--trace-from-cycle" && !options.traceStartSpecified)
      {
        options.traceStartSpecified = true;
        options.traceStart = parseUnsignedInteger(value,
          "Trace start must be an unsigned 64-bit decimal integer.");
      }
      else if (name == "--resume-checkpoint" && options.resumePath.empty())
      {
        options.resumePath = value;
      }
      else if (name == "--save-checkpoint" && options.savePath.empty())
      {
        options.savePath = value;
      }
      else
      {
        throw std::invalid_argument(USAGE);
      }
    }
    if ((!options.traced && (options.failureContext || options.traceStartSpecified)) ||
        (options.traceStartSpecified && options.traceStart > options.ceiling))
    {
      throw std::invalid_argument(USAGE);
    }
    return options;
  }

  void saveCheckpointFile(
    const std::string &path, const std::vector<std::uint8_t> &bytes)
  {
    std::unique_ptr<std::FILE, decltype(&std::fclose)> file(
      std::fopen(path.c_str(), "wbx"), &std::fclose);
    if (!file)
    {
      throw std::runtime_error(
        "Cannot create checkpoint (destination must not exist): " + path);
    }
    if (std::fwrite(bytes.data(), 1, bytes.size(), file.get()) != bytes.size())
    {
      throw std::runtime_error("Cannot write checkpoint: " + path);
    }
    if (std::fclose(file.release()) != 0)
    {
      throw std::runtime_error("Cannot close checkpoint: " + path);
    }
  }

  StartupRunEvidence runWithTrace(
    NekoSystem &system, const Options &options,
    const std::shared_ptr<StartupFailureCapture> &capture)
  {
    const auto start = system.masterClockScheduler().currentCycle();
    const auto traceStart = std::max(start, options.traceStart);
    if (!options.traced || traceStart == start)
    {
      if (options.traced)
      {
        system.startTrace(options.traceLimit, capture);
      }
      return runDualCPUWithStartupEvidence(system, options.ceiling - start);
    }
    auto prefix = runDualCPUWithStartupEvidence(system, traceStart - start);
    system.startTrace(options.traceLimit, capture);
    if (prefix.outcome == StartupRunOutcome::RuntimeError ||
        !prefix.completed.execution.cycleLimitReached)
    {
      return prefix;
    }
    auto suffix = runDualCPUWithStartupEvidence(
      system, options.ceiling - traceStart);
    if (suffix.outcome == StartupRunOutcome::Completed)
    {
      auto &run = suffix.completed.execution;
      const auto &before = prefix.completed.execution;
      run.masterCycles += before.masterCycles;
      run.ee.masterCycles += before.ee.masterCycles;
      run.ee.eeCycles += before.ee.eeCycles;
      run.ee.instructions += before.ee.instructions;
      run.iop.iopCycles += before.iop.iopCycles;
      run.iop.instructions += before.iop.instructions;
    }
    return suffix;
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
  const BIOSFileLoader &loadCheckpoint,
  const BIOSCheckpointWriter &saveCheckpoint,
  std::ostream &output,
  std::ostream &error)
{
  try
  {
    const auto options = parseOptions(arguments);
    const auto budget = options.ceiling;
    const auto image = loadFile(arguments[0]);
    NekoSystem system;
    system.runDualCPUBIOS(image, 0);
    if (!options.resumePath.empty())
    {
      system.loadState(loadCheckpoint(options.resumePath));
    }
    const auto start = system.masterClockScheduler().currentCycle();
    if (start > budget ||
        (options.traceStartSpecified && options.traceStart < start))
    {
      throw std::invalid_argument(
        "Checkpoint/trace start is outside the absolute cycle ceiling.");
    }
    const auto capture = options.failureContext ?
      std::make_shared<StartupFailureCapture>() : nullptr;
    const auto evidence = runWithTrace(system, options, capture);
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
      ",\"start\":\"" <<
      (options.resumePath.empty() ? "cold_dual_reset_vectors" : "checkpoint") << "\"";
    if (!options.resumePath.empty())
    {
      record << ",\"start_master_cycle\":" << start;
    }
    if (options.traceStartSpecified)
    {
      record << ",\"trace_start_cycle\":" << std::max(start, options.traceStart);
    }
    record << "}\n";
    if (runtimeError != nullptr)
    {
      writeStartupRuntimeErrorJsonLine(record, *runtimeError);
    }
    else
    {
      writeStartupRunSummaryJsonLine(record, evidence.completed);
    }
    output << record.str();
    if (options.traced)
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
    if (!options.savePath.empty())
    {
      saveCheckpoint(options.savePath, system.saveState());
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
  const BIOSFileLoader &loadFile,
  std::ostream &output,
  std::ostream &error)
{
  return runBIOSStartupCommand(arguments, loadFile, loadSaveStateFile,
    saveCheckpointFile, output, error);
}

int neko_cli::runBIOSStartupCommand(
  const std::vector<std::string> &arguments,
  std::ostream &output,
  std::ostream &error)
{
  return runBIOSStartupCommand(arguments, loadBIOSFile, output, error);
}
