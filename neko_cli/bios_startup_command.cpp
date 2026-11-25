#include "bios_startup_command.hpp"

#include "save_state_commands.hpp"
#include "startup_diagnostics.hpp"
#include "gs_draw_diagnostics.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
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
    "[--trace-events <count> [--failure-context] [--trace-from-cycle <count>] [--gs-draws <1-4096>]] "
    "[--resume-checkpoint <file>] [--save-checkpoint <file>] "
    "[--progress-file <file> [--progress-every <cycles>]] "
    "[--scout | --stop-on-first-failure]";

  struct Options
  {
    std::uint64_t ceiling = 0;
    std::uint64_t traceLimit = 0;
    std::uint64_t traceStart = 0;
    bool traced = false;
    bool failureContext = false;
    bool traceStartSpecified = false;
    std::uint64_t gsDrawLimit = 0;
    bool scout = false;
    bool stopOnFirstFailure = false;
    std::string resumePath;
    std::string savePath;
    std::string progressPath;
    std::uint64_t progressEvery = 1000000;
    bool progressEverySpecified = false;
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
      if (name == "--scout" && !options.scout)
      {
        options.scout = true;
        continue;
      }
      if (name == "--stop-on-first-failure" && !options.stopOnFirstFailure)
      {
        options.stopOnFirstFailure = true;
        continue;
      }
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
      else if (name == "--gs-draws" && options.gsDrawLimit == 0)
      {
        options.gsDrawLimit = parseUnsignedInteger(value,
          "GS draw limit must be from 1 to 4096.");
        if (options.gsDrawLimit == 0 ||
            options.gsDrawLimit > StartupGSDrawCapture::MAX_DRAWS)
        {
          throw std::invalid_argument("GS draw limit must be from 1 to 4096.");
        }
      }
      else if (name == "--resume-checkpoint" && options.resumePath.empty())
      {
        options.resumePath = value;
      }
      else if (name == "--save-checkpoint" && options.savePath.empty())
      {
        options.savePath = value;
      }
      else if (name == "--progress-file" && options.progressPath.empty())
      {
        options.progressPath = value;
      }
      else if (name == "--progress-every" && !options.progressEverySpecified)
      {
        options.progressEverySpecified = true;
        options.progressEvery = parseUnsignedInteger(value,
          "Progress interval must be a positive unsigned 64-bit decimal integer.");
      }
      else
      {
        throw std::invalid_argument(USAGE);
      }
    }
    if (options.progressEvery == 0 ||
        (options.progressEverySpecified && options.progressPath.empty()) ||
        (options.scout && (options.traced || options.failureContext ||
          options.traceStartSpecified || options.stopOnFirstFailure)) ||
        (options.stopOnFirstFailure && !options.traced) ||
        (!options.traced && (options.failureContext || options.traceStartSpecified ||
          options.gsDrawLimit != 0)) ||
        (options.traceStartSpecified && options.traceStart > options.ceiling))
    {
      throw std::invalid_argument(USAGE);
    }
    return options;
  }

  class ProgressReport
  {
    public:
      ProgressReport(const Options &options, std::uint64_t start) :
        file(nullptr, &std::fclose), startCycle(start), ceiling(options.ceiling)
      {
        if (options.progressPath.empty())
        {
          return;
        }
        file.reset(std::fopen(options.progressPath.c_str(), "wbx"));
        if (!file)
        {
          throw std::runtime_error("Cannot create progress file: " + options.progressPath);
        }
        wallStart = std::chrono::steady_clock::now();
        cpuStart = cpuClock();
        publish("execution", start);
      }

      bool enabled() const { return file != nullptr; }

      void publish(const char *phase, std::uint64_t cycle)
      {
        if (!file)
        {
          return;
        }
        const double wall = std::chrono::duration<double>(
          std::chrono::steady_clock::now() - wallStart).count();
        const auto cpuNow = cpuClock();
        if (cpuNow < cpuStart)
        {
          throw std::runtime_error("Progress CPU clock wrapped.");
        }
        const double cpu = static_cast<double>(cpuNow - cpuStart) / CLOCKS_PER_SEC;
        const double timestamp = std::chrono::duration<double>(
          std::chrono::system_clock::now().time_since_epoch()).count();
        std::ostringstream row;
        row.imbue(std::locale::classic());
        row << std::setprecision(17) <<
          "{\"format\":\"neko-bios-progress-v1\",\"phase\":\"" << phase <<
          "\",\"master_cycle\":" << cycle <<
          ",\"start_master_cycle\":" << startCycle <<
          ",\"cycle_ceiling\":" << ceiling <<
          ",\"elapsed_wall_seconds\":" << wall <<
          ",\"elapsed_cpu_seconds\":" << cpu <<
          ",\"cycles_per_second\":" << (wall > 0 ? (cycle - startCycle) / wall : 0) <<
          ",\"sampled_at_unix_seconds\":" << timestamp << "}\n";
        const auto bytes = row.str();
        if (std::fwrite(bytes.data(), 1, bytes.size(), file.get()) != bytes.size() ||
            std::fflush(file.get()) != 0)
        {
          throw std::runtime_error("Cannot write progress file.");
        }
      }

      void close()
      {
        if (file && std::fclose(file.release()) != 0)
        {
          throw std::runtime_error("Cannot close progress file.");
        }
      }

    private:
      static std::clock_t cpuClock()
      {
        const auto value = std::clock();
        if (value == static_cast<std::clock_t>(-1))
        {
          throw std::runtime_error("Progress process CPU clock is unavailable.");
        }
        return value;
      }

      std::unique_ptr<std::FILE, decltype(&std::fclose)> file;
      std::chrono::steady_clock::time_point wallStart;
      std::clock_t cpuStart = 0;
      std::uint64_t startCycle;
      std::uint64_t ceiling;
  };

  void addExecutionCounts(
    DualCPUExecutionResult &run, const DualCPUExecutionResult &before)
  {
    run.masterCycles += before.masterCycles;
    run.ee.masterCycles += before.ee.masterCycles;
    run.ee.eeCycles += before.ee.eeCycles;
    run.ee.instructions += before.ee.instructions;
    run.iop.iopCycles += before.iop.iopCycles;
    run.iop.instructions += before.iop.instructions;
  }

  StartupRunEvidence runWithProgress(
    NekoSystem &system, std::uint64_t cycles,
    const Options &options, ProgressReport &progress)
  {
    if (!progress.enabled())
    {
      return runDualCPUWithStartupEvidence(system, cycles);
    }
    DualCPUExecutionResult accumulated{};
    StartupRunEvidence evidence;
    do
    {
      evidence = runDualCPUWithStartupEvidence(
        system, std::min(options.progressEvery, cycles - accumulated.masterCycles));
      if (evidence.outcome == StartupRunOutcome::RuntimeError)
      {
        return evidence;
      }
      addExecutionCounts(evidence.completed.execution, accumulated);
      accumulated = evidence.completed.execution;
      progress.publish("execution", system.masterClockScheduler().currentCycle());
    }
    while (accumulated.cycleLimitReached && accumulated.masterCycles < cycles);
    return evidence;
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
    const std::shared_ptr<StartupFailureCapture> &capture,
    const std::shared_ptr<StartupGSDrawCapture> &draws, ProgressReport &progress)
  {
    const auto start = system.masterClockScheduler().currentCycle();
    const auto traceStart = std::max(start, options.traceStart);
    const auto stopPolicy = options.stopOnFirstFailure ?
      NekoTraceStopPolicy::FirstFailure : NekoTraceStopPolicy::Continue;
    if (options.scout)
    {
      system.startFailureProbe();
      return runWithProgress(system, options.ceiling - start, options, progress);
    }
    if (!options.traced || traceStart == start)
    {
      if (options.traced)
      {
        system.startTrace(options.traceLimit, capture, stopPolicy);
        system.gs().setDrawObserver(draws);
      }
      return runWithProgress(system, options.ceiling - start, options, progress);
    }
    auto prefix = runWithProgress(system, traceStart - start, options, progress);
    system.startTrace(options.traceLimit, capture, stopPolicy);
    system.gs().setDrawObserver(draws);
    if (prefix.outcome == StartupRunOutcome::RuntimeError ||
        !prefix.completed.execution.cycleLimitReached)
    {
      return prefix;
    }
    auto suffix = runWithProgress(
      system, options.ceiling - traceStart, options, progress);
    if (suffix.outcome == StartupRunOutcome::Completed)
    {
      addExecutionCounts(suffix.completed.execution, prefix.completed.execution);
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
    const auto draws = options.gsDrawLimit != 0 ?
      std::make_shared<StartupGSDrawCapture>(
        system, static_cast<std::size_t>(options.gsDrawLimit)) : nullptr;
    ProgressReport progress(options, start);
    const auto evidence = runWithTrace(system, options, capture, draws, progress);
    const auto *runtimeError = evidence.outcome == StartupRunOutcome::RuntimeError ?
      &evidence.error : nullptr;
    if (runtimeError != nullptr)
    {
      error << runtimeError->message << '\n';
    }
    progress.publish(runtimeError != nullptr ? "runtime_error" : "report",
      system.masterClockScheduler().currentCycle());

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
    if (options.scout)
    {
      record << ",\"observation\":\"failure_probe\"";
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
    if (options.traced || options.scout)
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
    if (draws)
    {
      writeStartupGSDrawCaptureJsonLine(output, *draws);
    }
    output.flush();
    if (!output)
    {
      throw std::runtime_error("Cannot write BIOS startup report.");
    }
    if (runtimeError != nullptr)
    {
      progress.close();
      return 2;
    }
    if (!options.savePath.empty())
    {
      progress.publish("checkpoint", system.masterClockScheduler().currentCycle());
      saveCheckpoint(options.savePath, system.saveState());
    }
    progress.publish("finished", system.masterClockScheduler().currentCycle());
    progress.close();
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
