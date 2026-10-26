#include <cstddef>
#include <cstdint>

#include "catch.hpp"
#include "neko_system.hpp"

namespace
{
  constexpr std::uint32_t ENTRY = 0xa0001000;
  constexpr std::uint32_t SCRATCHPAD = 0x70000000;
  constexpr std::uint32_t TIMER_STATUS =
    EECOP0Status::INTERRUPT_ENABLE |
    EECOP0Status::MASTER_INTERRUPT_ENABLE |
    EECOP0Status::TIMER_MASK;

  constexpr std::uint32_t CALIBRATION[] = {
    0x3c017000, // lui r1,0x7000
    0x40024800, // mfc0 r2,Count
    0xac220000, // sw r2,0(r1)
    0x24030004, // addiu r3,zero,4
    0x40044800, // mfc0 r4,Count
    0x2463ffff, // addiu r3,r3,-1
    0x1460fffd, // bne r3,zero,sample
    0,
    0x8c250000, // lw r5,0(r1)
    0x00853023, // subu r6,r4,r5
    0x24070003, // addiu r7,zero,3
    0x00c70018, // mult r6,r7
    0x00004012, // mflo r8
    0x0107001b, // divu r8,r7
    0x00008812, // mflo r17
    0x0011883c, // dsll32 r17,r17,0
    0xfc310010, // sd r17,16(r1)
    0xdc320010, // ld r18,16(r1)
    0x0012b03e, // dsrl32 r22,r18,0
    0x52c00001, // beql r22,zero,success
    0x000001cd, // break 7: annulled for a positive calibration
    0x3417600d, // ori r23,zero,0x600d
    0x1000ffff, // beq zero,zero,done
    0
  };
  constexpr std::uint32_t CALIBRATION_DONE = ENTRY + 22 * 4;

  constexpr std::uint32_t TIMER[] = {
    0x40804800, // mtc0 zero,Count
    0x24010008, // addiu r1,zero,8
    0x40815800, // mtc0 r1,Compare
    0x40026800, // mfc0 r2,Cause
    0x30428000, // andi r2,r2,0x8000
    0x1040fffd, // beq r2,zero,poll_masked
    0,
    0x40804800, // mtc0 zero,Count: pending remains sticky
    0x40036800, // mfc0 r3,Cause
    0x24040040, // addiu r4,zero,64
    0x40845800, // mtc0 r4,Compare: acknowledge while masked
    0x40056800, // mfc0 r5,Cause
    0x3c060001, // lui r6,1
    0x34c68001, // ori r6,r6,0x8001
    0x40866000, // mtc0 r6,Status: IE, EIE and timer mask
    0x40076800, // mfc0 r7,Cause
    0x30e78000, // andi r7,r7,0x8000
    0x1160fffd, // beq r11,zero,wait_for_handler
    0,
    0x3417600d, // ori r23,zero,0x600d
    0x1000ffff, // beq zero,zero,done
    0
  };
  constexpr std::uint32_t TIMER_DONE = ENTRY + 20 * 4;

  constexpr std::uint32_t HANDLER[] = {
    0x40086800, // mfc0 r8,Cause
    0x40094800, // mfc0 r9,Count
    0x25290064, // addiu r9,r9,100
    0x40895800, // mtc0 r9,Compare: acknowledge and defer the next match
    0x400a6800, // mfc0 r10,Cause
    0x256b0001, // addiu r11,r11,1
    0x42000018  // eret
  };

  void writeProgram(
    NekoSystem &system, std::uint32_t address,
    const std::uint32_t *program, std::size_t words)
  {
    for (std::size_t index = 0; index < words; ++index)
    {
      system.eeBus().write32(
        address + static_cast<std::uint32_t>(index * 4), program[index]);
    }
  }

  void startCalibration(NekoSystem &system)
  {
    writeProgram(system, 0x1000, CALIBRATION,
      sizeof(CALIBRATION) / sizeof(CALIBRATION[0]));
    system.eeCore().setTLBEntry(
      0,
      {EECOP0PageMask::SIZE_16_KIB, SCRATCHPAD,
       {EECOP0EntryLo::SCRATCHPAD | UINT32_C(0x00000006)}, {0}});
    system.eeCore().setCOP0Register(EECOP0Register::Status, 0);
    system.eeCore().startExecution(ENTRY);
  }

  void startTimer(NekoSystem &system)
  {
    writeProgram(system, 0x1000, TIMER, sizeof(TIMER) / sizeof(TIMER[0]));
    writeProgram(system, EEExceptionVector::INTERRUPT,
      HANDLER, sizeof(HANDLER) / sizeof(HANDLER[0]));
    system.eeCore().setCOP0Register(EECOP0Register::Status, 0);
    system.eeCore().startExecution(ENTRY);
  }

  void advanceTo(NekoSystem &system, std::uint32_t pc)
  {
    for (unsigned cycle = 0; cycle < 256 &&
         system.eeCore().programCounter() != pc &&
         system.eeCore().executionState() == EEExecutionState::Running; ++cycle)
    {
      system.clockMasterCycle();
    }
    CAPTURE(system.eeCore().pendingException());
    CAPTURE(system.eeCore().cop0Register(EECOP0Register::EPC));
    CAPTURE(system.eeCore().cop0Register(EECOP0Register::Cause));
    REQUIRE(system.eeCore().executionState() == EEExecutionState::Running);
    REQUIRE(system.eeCore().programCounter() == pc);
  }

  void requireCalibration(const NekoSystem &system)
  {
    const auto &core = system.eeCore();
    const std::uint64_t sample = core.generalRegister(2).low;
    const std::uint64_t lastSample = core.generalRegister(4).low;
    REQUIRE(core.pendingException() == EEException::None);
    REQUIRE(core.programCounter() == CALIBRATION_DONE);
    REQUIRE(sample > 0);
    REQUIRE(lastSample > sample);
    REQUIRE(core.generalRegister(3).low == 0);
    REQUIRE(core.generalRegister(5).low == sample);
    REQUIRE(core.generalRegister(6).low == lastSample - sample);
    REQUIRE(core.generalRegister(8).low == (lastSample - sample) * 3);
    REQUIRE(core.generalRegister(17).low == (lastSample - sample) << 32);
    REQUIRE(core.generalRegister(18).low == core.generalRegister(17).low);
    REQUIRE(core.generalRegister(22).low == lastSample - sample);
    REQUIRE(core.generalRegister(23).low == 0x600d);
    REQUIRE(core.cop0Register(EECOP0Register::Count) == core.elapsedCycles());
  }

  void requireTimer(const NekoSystem &system)
  {
    const auto &core = system.eeCore();
    REQUIRE(core.pendingException() == EEException::None);
    REQUIRE(core.programCounter() == TIMER_DONE);
    REQUIRE(core.generalRegister(2).low == EECOP0Cause::TIMER_PENDING);
    REQUIRE((core.generalRegister(3).low & EECOP0Cause::TIMER_PENDING) != 0);
    REQUIRE((core.generalRegister(5).low & EECOP0Cause::TIMER_PENDING) == 0);
    REQUIRE((core.generalRegister(8).low & EECOP0Cause::TIMER_PENDING) != 0);
    REQUIRE((core.generalRegister(10).low & EECOP0Cause::TIMER_PENDING) == 0);
    REQUIRE(core.generalRegister(11).low == 1);
    REQUIRE(core.generalRegister(23).low == 0x600d);
    REQUIRE(core.generalRegister(6).low == TIMER_STATUS);
    REQUIRE(core.cop0Register(EECOP0Register::Status) == TIMER_STATUS);
    REQUIRE(core.cop0Register(EECOP0Register::Compare) == core.generalRegister(9).low);
    REQUIRE(core.cop0Register(EECOP0Register::Count) <
      core.cop0Register(EECOP0Register::Compare));
    REQUIRE((core.cop0Register(EECOP0Register::Cause) &
      (EECOP0Cause::TIMER_PENDING | EECOP0Cause::BRANCH_DELAY)) == 0);
    REQUIRE(core.cop0Register(EECOP0Register::EPC) >= ENTRY + 15 * 4);
    REQUIRE(core.cop0Register(EECOP0Register::EPC) <= ENTRY + 19 * 4);
  }

  void requireRestoredTraceModes(
    NekoSystem &original, std::uint32_t done,
    void (*requireResult)(const NekoSystem &))
  {
    const auto saved = original.saveState();
    NekoSystem restored;
    NekoSystem plain;
    NekoSystem capped;
    for (auto *machine : {&restored, &plain, &capped})
    {
      machine->loadState(saved);
      REQUIRE(machine->saveState() == saved);
      REQUIRE(machine->eeStateHash() == original.eeStateHash());
    }
    original.startTrace(8192);
    restored.startTrace(8192);
    capped.startTrace(0);
    for (auto *machine : {&original, &restored, &plain, &capped})
    {
      advanceTo(*machine, done);
      machine->runMasterCycles(4);
      requireResult(*machine);
    }
    const auto ending = original.saveState();
    for (const auto *machine : {&restored, &plain, &capped})
    {
      REQUIRE(machine->saveState() == ending);
      REQUIRE(machine->eeStateHash() == original.eeStateHash());
      REQUIRE(machine->iopStateHash() == original.iopStateHash());
      REQUIRE(machine->sifStateHash() == original.sifStateHash());
    }
    REQUIRE(original.traceHash() == restored.traceHash());
    REQUIRE(original.trace().size() == restored.trace().size());
    REQUIRE_FALSE(original.trace().empty());
    for (std::size_t index = 0; index < original.trace().size(); ++index)
    {
      CAPTURE(index);
      const auto &expected = original.trace()[index];
      const auto &actual = restored.trace()[index];
      REQUIRE(actual.masterCycle == expected.masterCycle);
      REQUIRE(actual.subsystem == expected.subsystem);
      REQUIRE(actual.type == expected.type);
      REQUIRE(actual.value0 == expected.value0);
      REQUIRE(actual.value1 == expected.value1);
      REQUIRE(actual.value2 == expected.value2);
      REQUIRE(actual.value3 == expected.value3);
    }
    REQUIRE_FALSE(original.traceTruncated());
    REQUIRE_FALSE(restored.traceTruncated());
    REQUIRE_FALSE(plain.traceEnabled());
    REQUIRE(plain.trace().empty());
    REQUIRE(capped.traceEnabled());
    REQUIRE(capped.trace().empty());
    REQUIRE(capped.traceTruncated());
  }
}

TEST_CASE("EE Count calibration guest preserves a positive parameter through its guard",
  "[ee][guest][trace][cp0-timer-conformance]")
{
  NekoSystem system;
  startCalibration(system);
  system.startTrace(8192);
  advanceTo(system, CALIBRATION_DONE);
  requireCalibration(system);
  std::uint32_t savedSample = 0;
  std::uint64_t savedParameter = 0;
  REQUIRE(system.eeMemorySystem().readScratchpad32(0, &savedSample));
  REQUIRE(system.eeMemorySystem().readScratchpad64(16, &savedParameter));
  REQUIRE(savedSample == system.eeCore().generalRegister(2).low);
  REQUIRE(savedParameter == system.eeCore().generalRegister(17).low);
  unsigned samples = 0;
  unsigned guards = 0;
  std::uint64_t firstSample = 0;
  std::uint64_t lastSample = 0;
  for (const auto &event : system.trace())
  {
    if (event.subsystem != NekoTraceSubsystem::EE)
    {
      continue;
    }
    REQUIRE(event.type != NekoTraceEventType::ExceptionEntered);
    if (event.type == NekoTraceEventType::InstructionIssued)
    {
      REQUIRE(event.value0 != ENTRY + 20 * 4);
      if (event.value0 == ENTRY + 19 * 4)
      {
        ++guards;
        REQUIRE(event.value1 == CALIBRATION[19]);
      }
      if (event.value0 == ENTRY + 4)
      {
        firstSample = event.masterCycle;
      }
      if (event.value0 == ENTRY + 4 * 4)
      {
        ++samples;
        lastSample = event.masterCycle;
      }
    }
  }
  REQUIRE(samples == 4);
  REQUIRE(guards == 1);
  REQUIRE(system.eeCore().generalRegister(2).low == firstSample);
  REQUIRE(system.eeCore().generalRegister(4).low == lastSample);
  REQUIRE_FALSE(system.traceTruncated());
}

TEST_CASE("EE timer guest polls acknowledges interrupts and returns without host assistance",
  "[ee][guest][trace][cp0-timer-conformance]")
{
  NekoSystem system;
  startTimer(system);
  system.startTrace(8192);
  advanceTo(system, TIMER_DONE);
  requireTimer(system);
  unsigned interrupts = 0;
  unsigned exceptions = 0;
  for (const auto &event : system.trace())
  {
    if (event.subsystem != NekoTraceSubsystem::EE)
    {
      continue;
    }
    if (event.type == NekoTraceEventType::InterruptDelivered)
    {
      ++interrupts;
      REQUIRE(event.value0 == system.eeCore().cop0Register(EECOP0Register::EPC));
      REQUIRE(event.value1 ==
        (TIMER_STATUS | EECOP0Status::EXCEPTION_LEVEL));
      REQUIRE((event.value2 & EECOP0Cause::TIMER_PENDING) != 0);
      REQUIRE(event.value3 == EEExceptionVector::INTERRUPT);
    }
    if (event.type == NekoTraceEventType::ExceptionEntered)
    {
      ++exceptions;
      REQUIRE(event.value0 == static_cast<std::uint8_t>(EEException::Interrupt));
      REQUIRE(event.value1 == system.eeCore().cop0Register(EECOP0Register::EPC));
      REQUIRE(event.value2 == EEExceptionVector::INTERRUPT);
      REQUIRE((event.value3 & EECOP0Cause::TIMER_PENDING) != 0);
    }
  }
  REQUIRE(interrupts == 1);
  REQUIRE(exceptions == 1);
  REQUIRE_FALSE(system.traceTruncated());
}

TEST_CASE("EE calibration guest restores its sampled and unpacked parameter across trace modes",
  "[ee][guest][trace][save-state][cp0-timer-conformance]")
{
  for (unsigned instruction : {0u, 8u, 19u})
  {
    CAPTURE(instruction);
    NekoSystem original;
    startCalibration(original);
    advanceTo(original, ENTRY + instruction * 4);
    if (instruction != 0)
    {
      REQUIRE(original.eeCore().generalRegister(2).low > 0);
      std::uint32_t savedSample = 0;
      REQUIRE(original.eeMemorySystem().readScratchpad32(0, &savedSample));
      REQUIRE(savedSample == original.eeCore().generalRegister(2).low);
    }
    if (instruction == 19)
    {
      REQUIRE(original.eeCore().generalRegister(22).low > 0);
    }
    requireRestoredTraceModes(original, CALIBRATION_DONE, requireCalibration);
  }
}

TEST_CASE("EE timer guest restores pending match handler acknowledgment and ERET across trace modes",
  "[ee][guest][trace][save-state][cp0-timer-conformance]")
{
  for (unsigned checkpoint = 0; checkpoint < 5; ++checkpoint)
  {
    CAPTURE(checkpoint);
    NekoSystem original;
    startTimer(original);
    if (checkpoint == 0)
    {
      REQUIRE(original.eeCore().cop0Register(EECOP0Register::Count) == 0);
      REQUIRE(original.eeCore().cop0Register(EECOP0Register::Compare) == 0);
      REQUIRE(original.eeCore().cop0Register(EECOP0Register::Cause) == 0);
    }
    else if (checkpoint == 1)
    {
      advanceTo(original, ENTRY + 10 * 4);
      REQUIRE((original.eeCore().cop0Register(EECOP0Register::Cause) &
        EECOP0Cause::TIMER_PENDING) != 0);
      REQUIRE(original.eeCore().cop0Register(EECOP0Register::Count) < 8);
    }
    else if (checkpoint == 2)
    {
      advanceTo(original, ENTRY + 15 * 4);
      for (unsigned cycle = 0; cycle < 128 &&
           original.eeCore().cop0Register(EECOP0Register::Count) != 63; ++cycle)
      {
        original.clockMasterCycle();
      }
      REQUIRE(original.eeCore().cop0Register(EECOP0Register::Count) == 63);
      REQUIRE(original.eeCore().cop0Register(EECOP0Register::Compare) == 64);
      REQUIRE((original.eeCore().cop0Register(EECOP0Register::Cause) &
        EECOP0Cause::TIMER_PENDING) == 0);
    }
    else if (checkpoint == 3)
    {
      advanceTo(original, EEExceptionVector::INTERRUPT + 3 * 4);
      REQUIRE((original.eeCore().cop0Register(EECOP0Register::Cause) &
        EECOP0Cause::TIMER_PENDING) != 0);
      REQUIRE((original.eeCore().cop0Register(EECOP0Register::Status) &
        EECOP0Status::EXCEPTION_LEVEL) != 0);
      REQUIRE(original.eeCore().generalRegister(11).low == 0);
    }
    else
    {
      advanceTo(original, ENTRY + 19 * 4);
      REQUIRE(original.eeCore().generalRegister(11).low == 1);
      REQUIRE(original.eeCore().cop0Register(EECOP0Register::Status) == TIMER_STATUS);
      REQUIRE((original.eeCore().cop0Register(EECOP0Register::Cause) &
        EECOP0Cause::TIMER_PENDING) == 0);
    }
    requireRestoredTraceModes(original, TIMER_DONE, requireTimer);
  }
}
