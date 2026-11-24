#include "catch.hpp"
#include "startup_diagnostics.hpp"

#include <locale>
#include <sstream>
#include <stdexcept>

namespace
{
  constexpr auto EE = NekoTraceSubsystem::EE;
  constexpr auto IOP = NekoTraceSubsystem::IOP;
  constexpr auto MEMORY = NekoTraceEventType::MemoryAccess;
  constexpr auto ISSUE = NekoTraceEventType::InstructionIssued;
  constexpr auto FAULT = NekoTraceEventType::FaultContext;
  constexpr auto EXCEPTION = NekoTraceEventType::ExceptionEntered;

  std::uint64_t failedEELoad()
  {
    NekoEETraceMemory::Metadata metadata;
    metadata.width = 4;
    metadata.accessKind = NekoEETraceMemory::AccessKind::DataLoad;
    metadata.translationOutcome =
      NekoEETraceMemory::TranslationOutcome::AddressErrorLoadOrFetch;
    return nekoPackEEMemoryTraceMetadata(metadata);
  }

  std::uint64_t failedIOPLoad()
  {
    NekoIOPTraceMemory::Metadata metadata;
    metadata.width = 4;
    metadata.accessKind = NekoIOPTraceMemory::AccessKind::DataLoad;
    metadata.outcome = NekoIOPTraceMemory::Outcome::Misaligned;
    return nekoPackIOPMemoryTraceMetadata(metadata);
  }
}

TEST_CASE("First startup failure retains the original EE fault rather than handler state",
  "[startup-failure]")
{
  const std::vector<NekoTraceEvent> events = {
    {1, EE, ISSUE, 0x1000, 0x8c020001, 0, 1},
    {1, EE, MEMORY, 1, 0, 0, failedEELoad()},
    {1, EE, FAULT, 0x1000, 0x8c020001, 0xffc,
      NekoEETraceFault::INSTRUCTION_AVAILABLE | NekoEETraceFault::DELAY_SLOT},
    {1, EE, EXCEPTION, static_cast<unsigned>(EEException::AddressErrorLoadOrFetch),
      1, 0x80000180, UINT64_C(1) << 31 | 16},
    {2, EE, MEMORY, 3, 0, 0, failedEELoad()},
    {2, EE, FAULT, 0x80000180, 0x8c030003, 0,
      NekoEETraceFault::INSTRUCTION_AVAILABLE},
    {2, EE, EXCEPTION, static_cast<unsigned>(EEException::AddressErrorLoadOrFetch),
      3, 0x80000180, UINT64_C(1) << 31 | 16}
  };
  const auto failure = firstStartupFailure(events);
  REQUIRE(failure.found);
  REQUIRE(failure.eventIndex == 1);
  REQUIRE(failure.event.value0 == 1);
  REQUIRE(failure.instruction.available);
  REQUIRE(failure.instruction.instructionAvailable);
  REQUIRE(failure.instruction.programCounter == 0x1000);
  REQUIRE(failure.instruction.instruction == 0x8c020001);
  REQUIRE(failure.instruction.delaySlot);
  REQUIRE(failure.instruction.delaySlotAvailable);
  REQUIRE(failure.instruction.branchAddressAvailable);
  REQUIRE(failure.instruction.branchAddress == 0xffc);
  REQUIRE(failure.exceptionAvailable);
  REQUIRE(failure.exception.value1 == 1);

  for (std::size_t size : {2, 3, 4})
  {
    CAPTURE(size);
    const auto prefix = firstStartupFailure({events.begin(), events.begin() + size});
    REQUIRE(prefix.found);
    REQUIRE(prefix.instruction.available == (size >= 3));
    REQUIRE(prefix.exceptionAvailable == (size >= 4));
  }
}

TEST_CASE("Failure probes retain architectural EE rejection context before exception entry",
  "[startup-failure][bios-scout]")
{
  NekoSystem system;
  system.eeBus().write32(0, 0x4c000000);
  system.eeCore().startExecution(0xa0000000);
  system.iopCore().startExecution(0xa0000000);
  system.startFailureProbe();
  system.runDualCPU(100);
  const auto failure = firstStartupFailure(system.trace());
  REQUIRE(failure.found);
  REQUIRE(failure.kind == StartupFailureKind::InstructionRejection);
  REQUIRE(failure.instruction.available);
  REQUIRE(failure.instruction.instruction == 0x4c000000);
  REQUIRE(failure.instruction.programCounter == 0xa0000000);
  REQUIRE(failure.exceptionAvailable);
}

TEST_CASE("Failure probes match full tracing and plain canonical state at complete cycles",
  "[startup-failure][bios-scout][persistence]")
{
  for (const bool iopFailure : {false, true})
  {
    CAPTURE(iopFailure);
    const auto prepare = [&](NekoSystem &system)
    {
      if (iopFailure)
      {
        REQUIRE(system.iopBus().write32(0, 0x10000001).status == IOPBusStatus::Completed);
        REQUIRE(system.iopBus().write32(4, 0x44000000).status == IOPBusStatus::Completed);
      }
      else
      {
        system.eeBus().write32(0, 0x8c020001);
      }
      system.eeCore().startExecution(0xa0000000);
      system.iopCore().startExecution(0xa0000000);
    };
    NekoSystem scout;
    prepare(scout);
    scout.startFailureProbe();
    const auto result = scout.runDualCPU(100);
    REQUIRE_FALSE(result.cycleLimitReached);
    REQUIRE_FALSE(scout.traceEnabled());
    const auto failure = firstStartupFailure(scout.trace());
    REQUIRE(failure.found);
    REQUIRE(failure.event.subsystem == (iopFailure ? IOP : EE));
    REQUIRE(failure.event.masterCycle == result.masterCycles);
    REQUIRE(failure.instruction.available);
    REQUIRE(failure.exceptionAvailable);
    if (iopFailure)
    {
      REQUIRE(failure.instruction.delaySlot);
      REQUIRE(failure.instruction.branchAddress == 0xa0000000);
    }
    for (const auto &event : scout.trace())
    {
      REQUIRE(event.type != NekoTraceEventType::StateSnapshot);
    }
    NekoSystem traced;
    prepare(traced);
    traced.startTrace(1000, {}, NekoTraceStopPolicy::FirstFailure);
    REQUIRE(traced.runDualCPU(100).masterCycles == result.masterCycles);
    const auto tracedFailure = firstStartupFailure(traced.trace());
    REQUIRE(tracedFailure.event.value0 == failure.event.value0);
    REQUIRE(tracedFailure.instruction.programCounter == failure.instruction.programCounter);
    NekoSystem plain;
    prepare(plain);
    plain.runDualCPU(result.masterCycles);
    REQUIRE(scout.saveState() == plain.saveState());
    REQUIRE(traced.saveState() == plain.saveState());
    // Diagnostic stop state must not survive checkpoint restoration.
    NekoSystem restored;
    restored.loadState(scout.saveState());
    REQUIRE(restored.runDualCPU(1).masterCycles == 1);
    scout.stopTrace();
    REQUIRE(scout.runDualCPU(1).masterCycles == 1);
    REQUIRE(scout.saveState() == restored.saveState());
  }
}

TEST_CASE("Startup failure capture exports decoded CDVD commands and unchanged raw events",
  "[startup-failure][failure-capture][cdvd]")
{
  StartupFailureCapture capture;
  capture.observe({1, NekoTraceSubsystem::CDVD, NekoTraceEventType::CommandAccepted,
    UINT64_C(0x01000800), 42, 0, 0});
  capture.observe({2, NekoTraceSubsystem::CDVD, NekoTraceEventType::CommandCompleted,
    UINT64_C(0x800002010801), 43, UINT64_C(0x070605048001ff00), 0});
  capture.observe({2, EE, ISSUE, 4096, 0, 0, 0});
  std::ostringstream output;
  writeStartupFailureCaptureJsonLine(output, capture);
  const auto json = output.str();
  REQUIRE(json.find(
    "\"cdvd\":{\"transport\":\"n\",\"opcode\":8,\"phase_before\":\"idle\","
    "\"phase_after\":\"pending\",\"cause_before\":0,\"cause_after\":0,"
    "\"rtc_ticks\":42,\"result\":[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]}") != std::string::npos);
  REQUIRE(json.find(
    "\"cdvd\":{\"transport\":\"s\",\"opcode\":8,\"phase_before\":\"pending\","
    "\"phase_after\":\"result\",\"cause_before\":0,\"cause_after\":128,"
    "\"rtc_ticks\":43,\"result\":[0,255,1,128,4,5,6,7,0,0,0,0,0,0,0,0]}") != std::string::npos);
  REQUIRE(json.find("\"values\":[" + std::to_string(UINT64_C(0x800002010801)) +
    ",43," + std::to_string(UINT64_C(0x070605048001ff00)) + ",0]") !=
    std::string::npos);
  REQUIRE(json.find(
    "{\"master_cycle\":2,\"subsystem\":10,\"event_type\":5,"
    "\"values\":[4096,0,0,0]}") != std::string::npos);
  REQUIRE_FALSE(firstStartupFailure(capture.events()).found);
}

TEST_CASE("Startup flight recorder freezes late failure context independently of its prefix",
  "[startup-failure][failure-capture]")
{
  StartupFailureCapture capture;
  for (std::size_t index = 0; index < StartupFailureCapture::CAPACITY + 37; ++index)
  {
    capture.observe({index + 1, EE, ISSUE, index * 4, 0, 0, 0});
  }
  const auto cycle = StartupFailureCapture::CAPACITY + 38;
  capture.observe({cycle, EE, MEMORY, 1, 0, 0, failedEELoad()});
  capture.observe({cycle, EE, FAULT, 0x1000, 0x8c020001, 0xffc,
    NekoEETraceFault::INSTRUCTION_AVAILABLE | NekoEETraceFault::DELAY_SLOT});
  capture.observe({cycle, EE, EXCEPTION,
    static_cast<unsigned>(EEException::AddressErrorLoadOrFetch),
    1, 0x80000180, UINT64_C(1) << 31 | 16});
  capture.observe({cycle, IOP, ISSUE, 0x2000, 0, 0, 0});
  const auto frozen = capture.events();
  const auto firstIndex = capture.firstEventIndex();
  for (std::uint64_t later = cycle + 1; later < cycle + 5000; ++later)
  {
    capture.observe({later, EE, MEMORY, 3, 0, 0, failedEELoad()});
  }
  REQUIRE(capture.events().size() == StartupFailureCapture::CAPACITY);
  REQUIRE(nekoTraceHash(capture.events()) == nekoTraceHash(frozen));
  REQUIRE(capture.firstEventIndex() == firstIndex);
  REQUIRE(firstIndex == 41);
  REQUIRE(capture.observedEvents() == StartupFailureCapture::CAPACITY + 5040);
  REQUIRE_FALSE(capture.failureCycleTruncated());
  const auto failure = firstStartupFailure(capture.events());
  REQUIRE(failure.found);
  REQUIRE(failure.eventIndex == StartupFailureCapture::CAPACITY - 4);
  REQUIRE(failure.event.masterCycle == cycle);
  REQUIRE(failure.instruction.programCounter == 0x1000);
  REQUIRE(failure.instruction.branchAddress == 0xffc);
  REQUIRE(failure.exceptionAvailable);
}

TEST_CASE("Startup flight recorder bounds oversized failure cycles without losing the trigger",
  "[startup-failure][failure-capture]")
{
  StartupFailureCapture capture;
  capture.observe({1, EE, MEMORY, 1, 0, 0, failedEELoad()});
  for (std::size_t index = 0; index < StartupFailureCapture::CAPACITY * 2; ++index)
  {
    capture.observe({1, EE, ISSUE, index * 4, 0, 0, 0});
  }
  REQUIRE(capture.events().size() == StartupFailureCapture::CAPACITY);
  REQUIRE(capture.failureCycleTruncated());
  REQUIRE(capture.firstEventIndex() == 0);
  REQUIRE(capture.observedEvents() == StartupFailureCapture::CAPACITY * 2 + 1);
  const auto failure = firstStartupFailure(capture.events());
  REQUIRE(failure.found);
  REQUIRE(failure.eventIndex == 0);
  REQUIRE(failure.event.value0 == 1);
}

TEST_CASE("Startup flight recorder reports lost pre-trigger failure-cycle context",
  "[startup-failure][failure-capture]")
{
  for (const std::size_t preceding : {StartupFailureCapture::CAPACITY - 1,
      StartupFailureCapture::CAPACITY + 1})
  {
    CAPTURE(preceding);
    StartupFailureCapture capture;
    for (std::size_t index = 0; index < preceding; ++index)
    {
      capture.observe({1, EE, ISSUE, index * 4, 0, 0, 0});
    }
    capture.observe({1, EE, MEMORY, 1, 0, 0, failedEELoad()});
    REQUIRE(capture.failureCycleTruncated() ==
      (preceding >= StartupFailureCapture::CAPACITY));
    capture.observe({1, EE, ISSUE, 0x1000, 0, 0, 0});
    REQUIRE(capture.failureCycleTruncated());
    REQUIRE(capture.events().size() == StartupFailureCapture::CAPACITY);
    REQUIRE(capture.firstEventIndex() ==
      preceding + 2 - StartupFailureCapture::CAPACITY);
    const auto failure = firstStartupFailure(capture.events());
    REQUIRE(failure.found);
    REQUIRE(failure.eventIndex == StartupFailureCapture::CAPACITY - 2);
    REQUIRE(failure.event.value0 == 1);
  }
}

TEST_CASE("Startup flight recorder shares all failure classes and ignores ordinary events",
  "[startup-failure][failure-capture]")
{
  const NekoTraceEvent failures[] = {
    {2, EE, MEMORY, 1, 0, 0, failedEELoad()},
    {2, IOP, MEMORY, 1, 0, 0, failedIOPLoad()},
    {2, EE, FAULT, 0x1000, 0x40803800, 0,
      NekoEETraceFault::HOST_REJECTION | NekoEETraceFault::INSTRUCTION_AVAILABLE |
        (static_cast<std::uint64_t>(EEStopReason::UnsupportedInstruction) <<
          NekoEETraceFault::STOP_REASON_SHIFT)},
    {2, IOP, EXCEPTION, 10, 0x1000, 0x80000080, 40},
    {2, NekoTraceSubsystem::SIF, NekoTraceEventType::DMABeat,
      static_cast<std::uint64_t>(NekoSIFDMATrace::Outcome::Rejected) <<
        NekoSIFDMATrace::OUTCOME_SHIFT, 0, 0, 0}
  };
  for (const auto &event : failures)
  {
    StartupFailureCapture capture;
    capture.observe({1, EE, ISSUE, 0x1000, 0, 0, 0});
    capture.observe(event);
    capture.observe({3, EE, ISSUE, 0x2000, 0, 0, 0});
    REQUIRE(capture.events().size() == 2);
    const auto failure = firstStartupFailure(capture.events());
    REQUIRE(failure.found);
    REQUIRE(failure.eventIndex == 1);
    REQUIRE(failure.event.masterCycle == 2);
  }
  StartupFailureCapture quiet;
  NekoEETraceMemory::Metadata metadata;
  metadata.transferOutcome = NekoEETraceMemory::TransferOutcome::DeviceNotReady;
  for (std::size_t index = 0; index < StartupFailureCapture::CAPACITY + 1; ++index)
  {
    quiet.observe({index + 1, EE, MEMORY, 0, 0, 0,
      nekoPackEEMemoryTraceMetadata(metadata)});
  }
  REQUIRE(quiet.events().size() == StartupFailureCapture::CAPACITY);
  REQUIRE(quiet.firstEventIndex() == 1);
  REQUIRE_FALSE(firstStartupFailure(quiet.events()).found);
  REQUIRE_FALSE(quiet.failureCycleTruncated());
}

TEST_CASE("Startup flight recorder is observational and trace lifecycle owns its attachment",
  "[startup-failure][failure-capture][save-state]")
{
  NekoSystem captured;
  NekoSystem prefix;
  NekoSystem plain;
  for (auto *system : {&captured, &prefix, &plain})
  {
    system->eeBus().write32(0x80, 0x8c020001);
    system->eeCore().setCOP0Register(EECOP0Register::Status, 0);
    system->eeCore().startExecution(0xa0000000);
  }
  auto capture = std::make_shared<StartupFailureCapture>();
  captured.startTrace(1, capture);
  prefix.startTrace(1);
  for (auto *system : {&captured, &prefix, &plain})
  {
    system->runMasterCycles(80);
  }
  REQUIRE_FALSE(firstStartupFailure(captured.trace()).found);
  REQUIRE(captured.traceTruncated());
  REQUIRE(captured.traceHash() == prefix.traceHash());
  REQUIRE(captured.saveState() == prefix.saveState());
  REQUIRE(captured.saveState() == plain.saveState());
  const auto failure = firstStartupFailure(capture->events());
  REQUIRE(failure.found);
  REQUIRE(failure.instruction.programCounter == 0xa0000080);
  REQUIRE(failure.instruction.instruction == 0x8c020001);
  REQUIRE(failure.exceptionAvailable);
  const auto count = capture->observedEvents();
  captured.stopTrace();
  captured.runMasterCycles(2);
  REQUIRE(capture->observedEvents() == count);
  std::weak_ptr<StartupFailureCapture> lifetime = capture;
  captured.startTrace(0, capture);
  capture.reset();
  REQUIRE_FALSE(lifetime.expired());
  captured.startTrace(0);
  REQUIRE(lifetime.expired());
  capture = std::make_shared<StartupFailureCapture>();
  lifetime = capture;
  captured.startTrace(0, capture);
  capture.reset();
  captured.reset();
  REQUIRE(lifetime.expired());
}

TEST_CASE("Startup attribution does not confuse waits or speculative fetches with a causal instruction",
  "[startup-failure]")
{
  NekoEETraceMemory::Metadata metadata;
  metadata.width = 4;
  metadata.translationOutcome = NekoEETraceMemory::TranslationOutcome::Translated;
  for (const auto outcome : {NekoEETraceMemory::TransferOutcome::Stalled,
      NekoEETraceMemory::TransferOutcome::DeviceNotReady,
      NekoEETraceMemory::TransferOutcome::NotAttempted})
  {
    metadata.transferOutcome = outcome;
    REQUIRE_FALSE(firstStartupFailure({
      {1, EE, MEMORY, 0x10000000, 0, 0, nekoPackEEMemoryTraceMetadata(metadata)}}).found);
  }
  metadata.transferOutcome = NekoEETraceMemory::TransferOutcome::PhysicalBusError;
  const auto failure = firstStartupFailure({
    {1, EE, ISSUE, 0x1000, 0, 0, 0},
    {1, EE, MEMORY, 0x80000000, 0, 0, nekoPackEEMemoryTraceMetadata(metadata)},
    {2, EE, FAULT, 0x80000000, 0, 0, 0},
    {2, EE, EXCEPTION, static_cast<unsigned>(EEException::InstructionBusError),
      0x80000000, 0x80000180, 24}});
  REQUIRE(failure.found);
  REQUIRE(failure.instruction.available);
  REQUIRE(failure.instruction.programCounter == 0x80000000);
  REQUIRE_FALSE(failure.instruction.instructionAvailable);
  REQUIRE_FALSE(failure.instruction.delaySlotAvailable);
  REQUIRE_FALSE(failure.exceptionAvailable);
}

TEST_CASE("IOP startup attribution keeps attempt and exception context in collection order",
  "[startup-failure]")
{
  const std::vector<NekoTraceEvent> events = {
    {9, IOP, ISSUE, 0xa0000004, 0x8c220001, 0, 1},
    {9, IOP, MEMORY, 0xa0000101, 0, 0, failedIOPLoad()},
    {9, IOP, EXCEPTION, 4, 0xa0000000, 0xbfc00180, UINT64_C(1) << 31 | 16},
    {9, EE, MEMORY, 1, 0, 0, failedEELoad()}
  };
  const auto failure = firstStartupFailure(events);
  REQUIRE(failure.found);
  REQUIRE(failure.eventIndex == 1);
  REQUIRE(failure.event.subsystem == IOP);
  REQUIRE(failure.instruction.available);
  REQUIRE(failure.instruction.instruction == 0x8c220001);
  REQUIRE(failure.instruction.delaySlot);
  REQUIRE(failure.instruction.branchAddress == 0xa0000000);
  REQUIRE(failure.exception.value0 == 4);
  const auto unavailable = firstStartupFailure({
    events[0], {17, IOP, MEMORY, 0xa0000101, 0, 0, failedIOPLoad()}});
  REQUIRE(unavailable.found);
  REQUIRE_FALSE(unavailable.instruction.available);
}

TEST_CASE("Startup instruction rejections are distinct from deliberate guest traps",
  "[startup-failure]")
{
  for (const auto cpu : {EE, IOP})
  {
    const bool ee = cpu == EE;
    REQUIRE_FALSE(firstStartupFailure({
      {1, cpu, EXCEPTION,
        ee ? static_cast<unsigned>(EEException::SystemCall) : 8u,
        0x1000, 0x80000180, 32}}).found);
    const auto failure = firstStartupFailure({
      {1, cpu, ISSUE, 0x1000, 0xffffffff, 0, 0},
      {1, cpu, EXCEPTION,
        ee ? static_cast<unsigned>(EEException::ReservedInstruction) : 10u,
        0x1000, 0x80000180, 40}});
    REQUIRE(failure.found);
    REQUIRE(failure.kind == StartupFailureKind::InstructionRejection);
    REQUIRE(failure.exceptionAvailable);
    REQUIRE(failure.instruction.available == !ee);
  }
}

TEST_CASE("IOP rejected stores keep physical routing data without inventing a guest exception",
  "[startup-failure]")
{
  NekoSystem system;
  REQUIRE(system.iopBus().write32(0, 0x3c011000).status == IOPBusStatus::Completed);
  REQUIRE(system.iopBus().write32(4, 0xac200000).status == IOPBusStatus::Completed);
  system.iopCore().startExecution(0xa0000000);
  system.startTrace();
  system.runMasterCycles(9);
  const auto failure = firstStartupFailure(system.trace());
  REQUIRE(failure.found);
  REQUIRE(failure.kind == StartupFailureKind::MemoryAccess);
  REQUIRE(failure.event.subsystem == IOP);
  REQUIRE(failure.instruction.available);
  REQUIRE(failure.instruction.instructionAvailable);
  REQUIRE(failure.instruction.programCounter == 0xa0000004);
  REQUIRE(failure.instruction.instruction == 0xac200000);
  REQUIRE_FALSE(failure.exceptionAvailable);
  std::ostringstream output;
  writeStartupFirstFailureJsonLine(output, system);
  REQUIRE(output.str().find("\"virtual_address\":268435456,"
    "\"physical_address\":268435456,\"width\":4,\"write\":true") != std::string::npos);
  REQUIRE(output.str().find("\"outcome\":\"unmapped\",\"cache_route\":\"cached\","
    "\"byte_enable\":15") != std::string::npos);
}

TEST_CASE("IOP rejected delay-slot instructions retain the captured exception branch PC",
  "[startup-failure]")
{
  NekoSystem system;
  REQUIRE(system.iopBus().write32(0, 0x10000001).status == IOPBusStatus::Completed);
  REQUIRE(system.iopBus().write32(4, 0x44000000).status == IOPBusStatus::Completed);
  system.iopCore().startExecution(0xa0000000);
  system.startTrace();
  system.runMasterCycles(9);
  const auto failure = firstStartupFailure(system.trace());
  REQUIRE(failure.found);
  REQUIRE(failure.kind == StartupFailureKind::InstructionRejection);
  REQUIRE(failure.exceptionAvailable);
  REQUIRE(failure.instruction.available);
  REQUIRE(failure.instruction.instructionAvailable);
  REQUIRE(failure.instruction.programCounter == 0xa0000004);
  REQUIRE(failure.instruction.instruction == 0x44000000);
  REQUIRE(failure.instruction.delaySlotAvailable);
  REQUIRE(failure.instruction.delaySlot);
  REQUIRE(failure.instruction.branchAddressAvailable);
  REQUIRE(failure.instruction.branchAddress == 0xa0000000);
}

TEST_CASE("Failed IOP fetch and deferred instruction contexts remain distinct",
  "[startup-failure]")
{
  NekoIOPTraceMemory::Metadata metadata;
  metadata.width = 4;
  metadata.outcome = NekoIOPTraceMemory::Outcome::Misaligned;
  const auto fetch = firstStartupFailure({
    {1, IOP, MEMORY, 0xa0000002, 0, 0, nekoPackIOPMemoryTraceMetadata(metadata)},
    {1, IOP, EXCEPTION, 4, 0xa0000000, 0xbfc00180, UINT64_C(1) << 31 | 16}});
  REQUIRE(fetch.found);
  REQUIRE(fetch.instruction.available);
  REQUIRE(fetch.instruction.programCounter == 0xa0000002);
  REQUIRE_FALSE(fetch.instruction.instructionAvailable);
  REQUIRE(fetch.instruction.delaySlotAvailable);
  REQUIRE(fetch.instruction.delaySlot);
  REQUIRE(fetch.instruction.branchAddressAvailable);
  REQUIRE(fetch.instruction.branchAddress == 0xa0000000);
  const auto rejected = firstStartupFailure({
    {1, IOP, ISSUE, 0xa0000004, 0x4a000001, 0, 0},
    {1, IOP, NekoTraceEventType::StateChanged, 1, 0, 2, 0xa0000004}});
  REQUIRE(rejected.found);
  REQUIRE(rejected.kind == StartupFailureKind::InstructionRejection);
  REQUIRE(rejected.instruction.instructionAvailable);
  REQUIRE(rejected.instruction.instruction == 0x4a000001);
  REQUIRE_FALSE(rejected.exceptionAvailable);
}

TEST_CASE("EE fault context preserves delayed nested provenance without COP0 reconstruction",
  "[startup-failure]")
{
  NekoSystem system;
  auto &core = system.eeCore();
  core.setCOP0Register(EECOP0Register::Status,
    EECOP0Status::COP1_USABLE | EECOP0Status::EXCEPTION_LEVEL);
  core.setCOP0Register(EECOP0Register::EPC, 0x80001234);
  core.setCOP0Register(EECOP0Register::Cause, 0);
  core.setGeneralRegister(1, {0xa0000102, 0});
  system.eeBus().write32(0, 0x10000001); // beq zero, zero, +1
  system.eeBus().write32(4, 0xc4220000); // lwc1 f2, 0(at)
  core.startExecution(0xa0000000);
  system.startTrace();
  system.clockMasterCycle();
  core.setProgramCounter(0xa0000100);
  system.runMasterCycles(3);
  REQUIRE(core.pendingException() == EEException::AddressErrorLoadOrFetch);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0x80001234);
  REQUIRE((core.cop0Register(EECOP0Register::Cause) & EECOP0Cause::BRANCH_DELAY) == 0);
  const auto failure = firstStartupFailure(system.trace());
  REQUIRE(failure.found);
  REQUIRE(failure.instruction.available);
  REQUIRE(failure.instruction.programCounter == 0xa0000004);
  REQUIRE(failure.instruction.instruction == 0xc4220000);
  REQUIRE(failure.instruction.delaySlot);
  REQUIRE(failure.instruction.branchAddress == 0xa0000000);
}

TEST_CASE("EE startup host rejection and failed fetch expose only available instruction bits",
  "[startup-failure]")
{
  for (const bool fetch : {false, true})
  {
    CAPTURE(fetch);
    NekoSystem system;
    system.eeBus().write32(0, 0x40803800);
    system.eeCore().startExecution(fetch ? 0xa0000002 : 0xa0000000);
    system.startTrace();
    system.clockMasterCycle();
    const auto failure = firstStartupFailure(system.trace());
    REQUIRE(failure.found);
    REQUIRE(failure.instruction.available);
    REQUIRE(failure.instruction.programCounter == (fetch ? 0xa0000002 : 0xa0000000));
    REQUIRE(failure.instruction.instructionAvailable == !fetch);
    REQUIRE(failure.kind == (fetch ? StartupFailureKind::MemoryAccess :
      StartupFailureKind::InstructionRejection));
    std::ostringstream output;
    writeStartupFirstFailureJsonLine(output, system);
    REQUIRE(output.str().find(fetch ? "\"bits\":null" :
      "\"rejection_reason\":\"unsupported_instruction\"") != std::string::npos);
  }
}

TEST_CASE("Startup runtime evidence is an incomplete prefix without a fabricated runner result",
  "[startup-failure]")
{
  for (const bool traced : {false, true})
  {
    CAPTURE(traced);
    NekoSystem system;
    system.runDualCPUBIOS(std::vector<std::uint8_t>(BootROMImage::SIZE), 0);
    REQUIRE(system.sif().pushSIF1Quadword({{0x80002001, 4, 0, 0}}) ==
      SIFTransferOutcome::Completed);
    REQUIRE(system.iopSIF1DMAC().writeChannelControl(0x01000400) ==
      IOPDMAWriteOutcome::Completed);
    system.iopDMAController().writePriority(IOPDMABank::Secondary, 0x8000);
    if (traced)
    {
      system.startTrace();
    }
    const auto evidence = runDualCPUWithStartupEvidence(system, 17);
    REQUIRE(evidence.outcome == StartupRunOutcome::RuntimeError);
    const auto &error = evidence.error;
    REQUIRE(error.message.find("IOP SIF1 DMA rejected") != std::string::npos);
    REQUIRE(error.startup.masterCycle == 1);
    REQUIRE(error.startup.ee.cycles == 1);
    REQUIRE(error.startup.iop.retiredInstructions == 0);
    REQUIRE(error.startup.sif.sif1Words == 4);
    REQUIRE(error.startup.iopDMA.sif1.nextBeat.outcome == IOPDMABeatOutcome::Rejected);
    const auto failure = firstStartupFailure(system.trace());
    REQUIRE(failure.found == traced);
    if (failure.found)
    {
      REQUIRE(failure.kind == StartupFailureKind::DMARejection);
      REQUIRE_FALSE(failure.instruction.available);
      REQUIRE_FALSE(failure.exceptionAvailable);
    }
    std::ostringstream output;
    writeStartupRuntimeErrorJsonLine(output, error);
    writeStartupFirstFailureJsonLine(output, system, StartupExecutionBoundary::IncompleteCycle);
    REQUIRE(output.str().find("\"run\":null") != std::string::npos);
    REQUIRE(output.str().find("\"execution_boundary\":\"incomplete_cycle\"") !=
      std::string::npos);
    REQUIRE((output.str().find("\"cpu\":null") != std::string::npos) == traced);
  }
}

TEST_CASE("Startup runtime messages are escaped and formatting stays local",
  "[startup-failure]")
{
  StartupRuntimeError error;
  error.message = "failure \"quoted\"\\path\n\t\r";
  error.message.push_back('\0');
  error.message += "\xc3\xa9";
  std::ostringstream output;
  output << std::hex << std::boolalpha;
  const auto flags = output.flags();
  const auto locale = output.getloc();
  writeStartupRuntimeErrorJsonLine(output, error);
  REQUIRE(output.str().find("failure \\\"quoted\\\"\\\\path\\u000a\\u0009\\u000d\\u0000") !=
    std::string::npos);
  REQUIRE(output.str().find("\xc3\xa9") != std::string::npos);
  REQUIRE(output.flags() == flags);
  REQUIRE(output.getloc() == locale);
}

TEST_CASE("Startup runtime errors retain the attempted GS transfer parameters",
  "[startup-failure][startup-runtime-gs]")
{
  NekoSystem system;
  system.runDualCPUBIOS(std::vector<std::uint8_t>(BootROMImage::SIZE), 0);
  system.gs().writeRegister(GSRegisterAddress::BITBLTBUF,
    (UINT64_C(0x3120) << 32) | (UINT64_C(1) << 48) | (UINT64_C(0x13) << 56));
  system.gs().writeRegister(GSRegisterAddress::TRXPOS,
    (UINT64_C(17) << 32) | (UINT64_C(9) << 48));
  system.gs().writeRegister(GSRegisterAddress::TRXREG,
    UINT64_C(64) | (UINT64_C(32) << 32));
  REQUIRE(system.eeBus().writeQuadword(0x1000,
    {{0x8001, 0x10000000, GIFRegisterDescriptor::AD, 0}}));
  REQUIRE(system.eeBus().writeQuadword(0x1010,
    {{0, 0, GSRegisterAddress::TRXDIR, 0}}));
  system.eeBus().write32(EEMemoryMap::D_CTRL, DMACControl::DMA_ENABLE);
  system.eeBus().write32(EEMemoryMap::D2_MADR, 0x1000);
  system.eeBus().write32(EEMemoryMap::D2_QWC, 2);
  system.eeBus().write32(EEMemoryMap::D2_CHCR, GIFDMACChannelControl::START);
  const auto evidence = runDualCPUWithStartupEvidence(system, 16);
  REQUIRE(evidence.outcome == StartupRunOutcome::RuntimeError);
  REQUIRE(evidence.error.message ==
    "GS host-to-local transfer requires PSMCT32, PSMCT16 or PSMT4.");
  std::ostringstream output;
  writeStartupRuntimeErrorJsonLine(output, evidence.error);
  REQUIRE(output.str().find(
    "\"gs_transfer\":{\"source_base_pointer\":0,\"source_buffer_width\":0,"
    "\"source_pixel_storage_mode\":0,\"destination_base_pointer\":12576,"
    "\"destination_buffer_width\":1,\"destination_pixel_storage_mode\":19,"
    "\"source_x\":0,\"source_y\":0,\"destination_x\":17,\"destination_y\":9,"
    "\"width\":64,\"height\":32,\"transferred_pixels\":0,\"direction\":0,"
    "\"active\":false}") != std::string::npos);
  REQUIRE(output.str().find("\"execution_boundary\":\"incomplete_cycle\"") !=
    std::string::npos);
}

TEST_CASE("Startup runtime errors retain the selected GS drawing context",
  "[startup-failure][startup-runtime-gs]")
{
  for (const std::uint8_t context : {0, 1})
  for (const bool traced : {false, true})
  {
    CAPTURE(context);
    CAPTURE(traced);
    NekoSystem system;
    system.runDualCPUBIOS(std::vector<std::uint8_t>(BootROMImage::SIZE), 0);
    auto &gs = system.gs();
    const std::uint64_t prim = 6 | (context << 9) | (1 << 6);
    const std::uint64_t test = UINT64_C(0x8000000000035a95);
    const std::uint64_t frame = UINT64_C(0x8000000000010001);
    const std::uint64_t zbuf = UINT64_C(0x8000000100000002);
    const std::uint64_t alpha = UINT64_C(0x8000008000000044);
    gs.writeRegister(GSRegisterAddress::PRIM, prim);
    gs.writeRegister(GSRegisterAddress::PRMODECONT, UINT64_C(0x8000000000000001));
    gs.writeRegister(GSRegisterAddress::PRMODE, UINT64_C(0x8000000000000000));
    gs.writeRegister(context == 0 ? GSRegisterAddress::TEST_1 : GSRegisterAddress::TEST_2, test);
    gs.writeRegister(context == 0 ? GSRegisterAddress::FRAME_1 : GSRegisterAddress::FRAME_2, frame);
    gs.writeRegister(context == 0 ? GSRegisterAddress::ZBUF_1 : GSRegisterAddress::ZBUF_2, zbuf);
    gs.writeRegister(context == 0 ? GSRegisterAddress::ALPHA_1 : GSRegisterAddress::ALPHA_2, alpha);
    gs.writeRegister(context == 0 ? GSRegisterAddress::FBA_1 : GSRegisterAddress::FBA_2,
      UINT64_C(0x8000000000000001));
    gs.writeRegister(GSRegisterAddress::PABE, UINT64_C(0x8000000000000001));
    struct ProfileRegister
    {
      const char *name;
      std::uint8_t address;
      std::uint64_t value;
    };
    const ProfileRegister profile[] = {
      {"tex0", context == 0 ? GSRegisterAddress::TEX0_1 : GSRegisterAddress::TEX0_2,
        UINT64_C(0x8000000000001234) | (UINT64_C(3) << 14) |
        (UINT64_C(20) << 20) | (UINT64_C(5) << 26) |
        (UINT64_C(6) << 30) | (UINT64_C(1) << 34) | (UINT64_C(2) << 35)},
      {"tex1", context == 0 ? GSRegisterAddress::TEX1_1 : GSRegisterAddress::TEX1_2,
        UINT64_C(0x80000000000000f4)},
      {"tex2", context == 0 ? GSRegisterAddress::TEX2_1 : GSRegisterAddress::TEX2_2,
        UINT64_C(0x8000000000100000)},
      {"clamp", context == 0 ? GSRegisterAddress::CLAMP_1 : GSRegisterAddress::CLAMP_2,
        UINT64_C(0x800000000000000e) | (UINT64_C(1) << 4) |
        (UINT64_C(100) << 14) | (UINT64_C(3) << 24) | (UINT64_C(200) << 34)},
      {"texa", GSRegisterAddress::TEXA, UINT64_C(0x8000008000000080)},
      {"texclut", GSRegisterAddress::TEXCLUT, UINT64_C(0x8000000000000041)},
      {"miptbp1", context == 0 ? GSRegisterAddress::MIPTBP1_1 : GSRegisterAddress::MIPTBP1_2,
        UINT64_C(0x8000000000000042)},
      {"miptbp2", context == 0 ? GSRegisterAddress::MIPTBP2_1 : GSRegisterAddress::MIPTBP2_2,
        UINT64_C(0x8000000000000043)},
      {"colclamp", GSRegisterAddress::COLCLAMP, UINT64_C(0x8000000000000001)},
      {"rgbaq", GSRegisterAddress::RGBAQ, UINT64_C(0x8000000080402010)},
      {"st", GSRegisterAddress::ST, UINT64_C(0x800000003f800000)},
      {"uv", GSRegisterAddress::UV, UINT64_C(0x8000000000200010)},
      {"xyoffset", context == 0 ? GSRegisterAddress::XYOFFSET_1 : GSRegisterAddress::XYOFFSET_2,
        UINT64_C(0x8000000100000002)},
      {"scissor", context == 0 ? GSRegisterAddress::SCISSOR_1 : GSRegisterAddress::SCISSOR_2,
        UINT64_C(0x8000000100020003)}
    };
    for (const auto &setting : profile)
    {
      gs.writeRegister(setting.address, setting.value);
    }
    gs.writeRegister(GSRegisterAddress::XYZ2, 0);
    REQUIRE(system.eeBus().writeQuadword(0x1000,
      {{0x8001, 0x10000000, GIFRegisterDescriptor::AD, 0}}));
    REQUIRE(system.eeBus().writeQuadword(0x1010,
      {{0x00100010, 0, GSRegisterAddress::XYZ2, 0}}));
    system.eeBus().write32(EEMemoryMap::D_CTRL, DMACControl::DMA_ENABLE);
    system.eeBus().write32(EEMemoryMap::D2_MADR, 0x1000);
    system.eeBus().write32(EEMemoryMap::D2_QWC, 2);
    system.eeBus().write32(EEMemoryMap::D2_CHCR, GIFDMACChannelControl::START);
    if (traced)
    {
      system.startTrace();
    }
    const auto evidence = runDualCPUWithStartupEvidence(system, 16);
    REQUIRE(evidence.outcome == StartupRunOutcome::RuntimeError);
    REQUIRE(evidence.error.message == "GS sprite uses unsupported pixel tests.");
    gs.writeRegister(GSRegisterAddress::PRIM, 0);
    gs.writeRegister(context == 0 ? GSRegisterAddress::TEST_1 : GSRegisterAddress::TEST_2, 0);
    for (const auto &setting : profile)
    {
      gs.writeRegister(setting.address, 0);
    }
    std::ostringstream output;
    writeStartupRuntimeErrorJsonLine(output, evidence.error);
    REQUIRE(output.str().find(
      "\"gs_drawing\":{\"primitive\":6,\"context\":" + std::to_string(context) +
      ",\"prim\":" + std::to_string(prim) +
      ",\"prmodecont\":9223372036854775809,\"prmode\":9223372036854775808,"
      "\"test\":" + std::to_string(test) +
      ",\"frame\":" + std::to_string(frame) +
      ",\"zbuf\":" + std::to_string(zbuf) +
      ",\"alpha\":" + std::to_string(alpha) +
      ",\"fba\":9223372036854775809,\"pabe\":9223372036854775809") !=
      std::string::npos);
    const auto profileStart = output.str().find("\"profile\":{");
    REQUIRE(profileStart != std::string::npos);
    const auto retainedProfile = output.str().substr(profileStart);
    for (const auto &setting : profile)
    {
      CAPTURE(setting.name);
      const auto expectedValue = std::string(setting.name) == "tex0"
        ? (setting.value & ~(UINT64_C(0x3f) << 20)) | (UINT64_C(1) << 20)
        : setting.value;
      REQUIRE(retainedProfile.find("\"" + std::string(setting.name) + "\":" +
        std::to_string(expectedValue)) != std::string::npos);
    }
    REQUIRE(retainedProfile.find(
      "\"decoded_texture\":{\"base_pointer\":4660,\"buffer_width\":3,"
      "\"pixel_storage_mode\":1,\"width_exponent\":5,\"height_exponent\":6,"
      "\"rgba\":true,\"function\":2,\"maximum_mip_level\":5,"
      "\"magnification_linear\":true,\"minification_filter\":3}") != std::string::npos);
    REQUIRE(retainedProfile.find(
      "\"decoded_clamp\":{\"horizontal\":2,\"vertical\":3,\"minimum_u\":1,"
      "\"maximum_u\":100,\"minimum_v\":3,\"maximum_v\":200}") != std::string::npos);
  }
}

TEST_CASE("Startup failure writers reject invalid boundaries without partial output",
  "[startup-failure]")
{
  REQUIRE(static_cast<unsigned>(NekoTraceEventType::FaultContext) == 21);
  NekoSystem system;
  std::ostringstream output;
  REQUIRE_THROWS_AS(writeStartupFirstFailureJsonLine(
    output, system, static_cast<StartupExecutionBoundary>(99)), std::invalid_argument);
  REQUIRE(output.str().empty());
}

TEST_CASE("Startup attribution respects every prefix of a dual CPU fault timeline",
  "[startup-failure][diagnostic-conformance]")
{
  std::vector<std::uint8_t> image(BootROMImage::SIZE, 0);
  for (const unsigned address : {0u, 0x180u, 0x380u})
  {
    const std::uint32_t instruction =
      address == 0 ? 0x8c020001 : 0x8c030003;
    for (unsigned byte = 0; byte < 4; ++byte)
    {
      image[address + byte] =
        static_cast<std::uint8_t>(instruction >> (byte * 8));
    }
  }
  NekoSystem system;
  system.runDualCPUBIOS(image, 0);
  system.startTrace();
  const auto evidence = runDualCPUWithStartupEvidence(system, 33);
  REQUIRE(evidence.outcome == StartupRunOutcome::Completed);
  REQUIRE(evidence.completed.execution.masterCycles == 33);
  REQUIRE(evidence.completed.execution.cycleLimitReached);
  const auto &events = system.trace();
  auto memoryIndex = events.size();
  auto contextIndex = events.size();
  auto exceptionIndex = events.size();
  auto peerIndex = events.size();
  auto handlerIndex = events.size();
  for (std::size_t index = 0; index < events.size(); ++index)
  {
    const auto &event = events[index];
    if (event.masterCycle == 1)
    {
      if (event.subsystem == EE)
      {
        if (event.type == MEMORY && event.value0 == 1)
        {
          memoryIndex = index;
        }
        else if (event.type == FAULT)
        {
          contextIndex = index;
        }
        else if (event.type == EXCEPTION)
        {
          exceptionIndex = index;
        }
      }
      else if (event.subsystem == IOP && event.type == MEMORY && event.value0 == 1)
      {
        peerIndex = index;
      }
    }
    else if (event.type == MEMORY && event.value0 == 3)
    {
      handlerIndex = index;
    }
  }
  REQUIRE(memoryIndex < contextIndex);
  REQUIRE(contextIndex < exceptionIndex);
  REQUIRE(exceptionIndex < peerIndex);
  REQUIRE(peerIndex < events.size());
  REQUIRE(handlerIndex < events.size());

  for (std::size_t size = 0; size <= events.size(); ++size)
  {
    CAPTURE(size);
    const auto failure = firstStartupFailure({events.begin(), events.begin() + size});
    REQUIRE(failure.found == (size > memoryIndex));
    if (!failure.found)
    {
      continue;
    }
    REQUIRE(failure.kind == StartupFailureKind::MemoryAccess);
    REQUIRE(failure.eventIndex == memoryIndex);
    REQUIRE(failure.event.masterCycle == 1);
    REQUIRE(failure.event.subsystem == EE);
    REQUIRE(failure.event.value0 == 1);
    REQUIRE(failure.instruction.available == (size > contextIndex));
    REQUIRE(failure.exceptionAvailable == (size > exceptionIndex));
    if (failure.instruction.available)
    {
      REQUIRE(failure.instruction.programCounter == 0xbfc00000);
      REQUIRE(failure.instruction.instructionAvailable);
      REQUIRE(failure.instruction.instruction == 0x8c020001);
      REQUIRE(failure.instruction.delaySlotAvailable);
      REQUIRE_FALSE(failure.instruction.delaySlot);
      REQUIRE_FALSE(failure.instruction.branchAddressAvailable);
    }
    if (failure.exceptionAvailable)
    {
      REQUIRE(failure.exception.value1 == 1);
      REQUIRE(failure.exception.value2 == 0xbfc00380);
    }
  }
}

TEST_CASE("Startup runtime diagnostics preserve CPU faults before same cycle DMA rejection",
  "[startup-failure][diagnostic-conformance][persistence]")
{
  const auto prepare = [](NekoSystem &system)
  {
    system.eeBus().write32(0, 0x8c020001);
    system.eeCore().startExecution(0xa0000000);
    system.iopCore().startExecution(0xa0000000);
    REQUIRE(system.sif().pushSIF1Quadword({{0x80002001, 4, 0, 0}}) ==
      SIFTransferOutcome::Completed);
    REQUIRE(system.iopSIF1DMAC().writeChannelControl(0x01000400) ==
      IOPDMAWriteOutcome::Completed);
    system.iopDMAController().writePriority(IOPDMABank::Secondary, 0x8000);
  };
  NekoSystem reference;
  prepare(reference);
  REQUIRE_THROWS_AS(reference.runDualCPU(17), IOPDMATransferError);
  const auto failedState = reference.saveState();
  REQUIRE(reference.iopSIF1DMAC().writeChannelControl(0x00000400) ==
    IOPDMAWriteOutcome::Completed);
  reference.runDualCPU(17);
  const auto continuedState = reference.saveState();

  for (const int limit : {-1, 0, 1, 1000})
  {
    CAPTURE(limit);
    NekoSystem system;
    prepare(system);
    if (limit >= 0)
    {
      system.startTrace(static_cast<std::uint64_t>(limit));
    }
    const auto evidence = runDualCPUWithStartupEvidence(system, 17);
    REQUIRE(evidence.outcome == StartupRunOutcome::RuntimeError);
    REQUIRE(evidence.error.startup.masterCycle == 1);
    REQUIRE(evidence.error.startup.ee.cycles == 1);
    REQUIRE(evidence.error.startup.iop.retiredInstructions == 0);
    REQUIRE(evidence.error.startup.sif.sif1Words == 4);
    REQUIRE(evidence.error.startup.iopDMA.sif1.nextBeat.outcome ==
      IOPDMABeatOutcome::Rejected);
    const auto historySize = system.trace().size();
    const auto historyHash = system.traceHash();
    const auto truncated = system.traceTruncated();
    const auto eventLimit = system.traceEventLimit();
    const auto failure = firstStartupFailure(system.trace());
    REQUIRE(failure.found == (limit == 1000));
    if (failure.found)
    {
      REQUIRE(failure.kind == StartupFailureKind::MemoryAccess);
      REQUIRE(failure.event.subsystem == EE);
      REQUIRE(failure.event.masterCycle == 1);
      REQUIRE(failure.instruction.programCounter == 0xa0000000);
      REQUIRE(failure.instruction.instruction == 0x8c020001);
      REQUIRE(failure.exceptionAvailable);
      auto rejectedIndex = system.trace().size();
      for (std::size_t index = 0; index < system.trace().size(); ++index)
      {
        const auto &event = system.trace()[index];
        if (event.subsystem == NekoTraceSubsystem::SIF &&
            event.type == NekoTraceEventType::DMABeat &&
            ((event.value0 >> NekoSIFDMATrace::OUTCOME_SHIFT) & 255) ==
              static_cast<unsigned>(NekoSIFDMATrace::Outcome::Rejected))
        {
          rejectedIndex = index;
        }
      }
      REQUIRE(failure.eventIndex < rejectedIndex);
      REQUIRE(rejectedIndex < system.trace().size());
      REQUIRE(system.trace()[rejectedIndex].masterCycle == 1);
    }
    std::string report;
    for (unsigned repeat = 0; repeat < 2; ++repeat)
    {
      std::ostringstream output;
      writeStartupRuntimeErrorJsonLine(output, evidence.error);
      writeStartupTraceJsonLines(output, system);
      writeStartupFirstFailureJsonLine(
        output, system, StartupExecutionBoundary::IncompleteCycle);
      if (repeat == 0)
      {
        report = output.str();
      }
      else
      {
        REQUIRE(output.str() == report);
      }
    }
    REQUIRE(report.find("\"run\":null") != std::string::npos);
    REQUIRE(report.find("\"execution_boundary\":\"incomplete_cycle\"") !=
      std::string::npos);
    REQUIRE(system.traceEnabled() == (limit >= 0));
    REQUIRE(system.traceEventLimit() == eventLimit);
    REQUIRE(system.traceTruncated() == truncated);
    REQUIRE(system.trace().size() == historySize);
    REQUIRE(system.traceHash() == historyHash);
    const bool unchanged = system.saveState() == failedState;
    REQUIRE(unchanged);
    REQUIRE(system.iopSIF1DMAC().writeChannelControl(0x00000400) ==
      IOPDMAWriteOutcome::Completed);
    system.runDualCPU(17);
    const bool sameContinuation = system.saveState() == continuedState;
    REQUIRE(sameContinuation);
  }
}
