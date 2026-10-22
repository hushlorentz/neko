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
    system.eeBus().write32(0, 0x40003800);
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
