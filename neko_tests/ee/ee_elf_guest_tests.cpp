#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "catch.hpp"
#include "elf_runner.hpp"
#include "neko_system.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_register_ids.hpp"

namespace
{
  std::string guestPath(const std::string &fileName)
  {
    return
      std::string(NEKO_EE_ELF_GUEST_DIR) + "/" + fileName;
  }

  std::vector<std::uint8_t> readGuest(
    const std::string &fileName)
  {
    const std::string path = guestPath(fileName);
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
      throw std::runtime_error(
        "Could not open EE ELF guest: " + path);
    }
    return std::vector<std::uint8_t>(
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>());
  }

  void uploadVectorCopyProgram(VPU *vpu)
  {
    vpu->writeMicroInstruction(
      0,
      VPU_MOVE_ENCODING |
        (UINT32_C(0xf) << 21) |
        (static_cast<std::uint32_t>(
          VPU_REGISTER_VF02) << 16) |
        (static_cast<std::uint32_t>(
          VPU_REGISTER_VF01) << 11),
      VPU_E_BIT | VPU_NOP);
    vpu->writeMicroInstruction(
      1,
      VPU_LOWER_NOP,
      VPU_NOP);
  }
}

TEST_CASE("PS2DEV scalar EE ELF guests complete successfully")
{
  struct GuestExpectation
  {
    const char *fileName;
    std::uint64_t instructions;
  };
  const GuestExpectation guests[] = {
    {"arithmetic.elf", 16},
    {"branches.elf", 12},
    {"memory.elf", 23},
    {"mmio.elf", 20},
    {"fifo.elf", 16},
    {"vif1_dma.elf", 34},
    {"cop2_transfer.elf", 19},
    {"cop2_control.elf", 23},
    {"vu_macro_arithmetic.elf", 35},
    {"vu_macro_families.elf", 107}
  };

  for (const GuestExpectation &guest : guests)
  {
    CAPTURE(guest.fileName);
    NekoSystem system;
    const EEGuestExecutionResult result =
      system.runELF(readGuest(guest.fileName), 512);

    REQUIRE(result.outcome == EEGuestOutcome::Completed);
    REQUIRE(result.exitCode == 0);
    REQUIRE(
      result.execution.instructions ==
      guest.instructions);
    REQUIRE_FALSE(result.execution.cycleLimitReached);
    REQUIRE(
      result.execution.programCounter ==
      EEGuestRuntime::RETURN_ADDRESS);
  }
}

TEST_CASE("PS2DEV COP1 semantic guest preserves raw results and FCR31")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("cop1_semantics.elf"), 256);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  REQUIRE_FALSE(result.execution.cycleLimitReached);
  const EECore &core = system.eeCore();
  REQUIRE(
    core.floatingPointRegister(4) ==
    UINT32_C(0x40700000));
  REQUIRE(
    core.floatingPointRegister(5) ==
    UINT32_C(0x3f400000));
  REQUIRE(
    core.floatingPointRegister(6) ==
    UINT32_C(0x40580000));
  REQUIRE(
    core.floatingPointRegister(7) ==
    UINT32_C(0x40800000));
  REQUIRE(
    core.floatingPointRegister(9) ==
    UINT32_C(0xbfc00000));
  REQUIRE(
    core.floatingPointRegister(10) ==
    UINT32_C(0x40100000));
  REQUIRE(
    core.floatingPointRegister(11) ==
    UINT32_C(0x3fc00000));
  REQUIRE(
    core.floatingPointRegister(12) ==
    UINT32_C(0xc0400000));
  REQUIRE(core.floatingPointRegister(14) == 2);
  REQUIRE(
    core.floatingPointRegister(15) ==
    UINT32_C(0x40000000));
  REQUIRE(
    core.floatingPointRegister(16) ==
    UINT32_C(0x7fffffff));
  REQUIRE(
    core.cop1ControlRegister(31) ==
    (EECOP1Control::STATUS_FIXED |
     EECOP1Control::CONDITION |
     EECOP1Control::CAUSE_INVALID |
     EECOP1Control::STICKY_INVALID));
  const std::uint32_t expectedReadbacks[] = {
    UINT32_C(0x40700000),
    UINT32_C(0x3f400000),
    UINT32_C(0x40580000),
    UINT32_C(0x40800000),
    UINT32_C(0xbfc00000),
    UINT32_C(0x40100000),
    UINT32_C(0x3fc00000),
    UINT32_C(0xc0400000),
    UINT32_C(0x00000002),
    UINT32_C(0x40000000),
    UINT32_C(0x7fffffff),
    EECOP1Control::STATUS_FIXED |
      EECOP1Control::CONDITION |
      EECOP1Control::CAUSE_INVALID |
      EECOP1Control::STICKY_INVALID
  };
  for (std::size_t index = 0;
       index < sizeof(expectedReadbacks) /
         sizeof(expectedReadbacks[0]);
       ++index)
  {
    REQUIRE(
      static_cast<std::uint32_t>(
        core.generalRegister(9 + index).low) ==
      expectedReadbacks[index]);
  }
}

TEST_CASE("PS2DEV COP1 transfer and memory guest preserves raw words")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("cop1_transfer_memory.elf"), 128);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  REQUIRE(result.execution.instructions == 17);
  REQUIRE_FALSE(result.execution.cycleLimitReached);
  REQUIRE(
    result.execution.programCounter ==
    EEGuestRuntime::RETURN_ADDRESS);

  const EECore &core = system.eeCore();
  REQUIRE(
    core.floatingPointRegister(2) ==
    UINT32_C(0x89abcdef));
  REQUIRE(
    core.floatingPointRegister(3) ==
    UINT32_C(0x89abcdef));
  REQUIRE(
    core.floatingPointRegister(4) ==
    UINT32_C(0x7fc12345));
  REQUIRE(
    core.floatingPointRegister(5) ==
    UINT32_C(0x89abcdef));
  REQUIRE(
    core.floatingPointRegister(6) ==
    UINT32_C(0x7fc12345));
  REQUIRE(
    core.generalRegister(10).low ==
    UINT64_C(0xffffffff89abcdef));
  REQUIRE(
    core.generalRegister(11).low ==
    UINT64_C(0xffffffff89abcdef));
  REQUIRE(
    core.generalRegister(12).low ==
    UINT64_C(0x000000007fc12345));
  REQUIRE(
    core.cop1ControlRegister(31) ==
    EECOP1Control::STATUS_FIXED);

  const std::uint32_t payloadAddress =
    static_cast<std::uint32_t>(
      core.generalRegister(8).low);
  REQUIRE(
    system.eeBus().read32(payloadAddress + 8) ==
    UINT32_C(0x89abcdef));
  REQUIRE(
    system.eeBus().read32(payloadAddress + 12) ==
    UINT32_C(0x7fc12345));
}

TEST_CASE("PS2DEV COP1 control-state guest applies FCR31 fields")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("cop1_control_state.elf"), 128);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  REQUIRE(result.execution.instructions == 19);
  REQUIRE_FALSE(result.execution.cycleLimitReached);
  REQUIRE(
    result.execution.programCounter ==
    EEGuestRuntime::RETURN_ADDRESS);

  const EECore &core = system.eeCore();
  REQUIRE(
    core.generalRegister(9).low ==
    (EECOP1Control::STATUS_FIXED |
     EECOP1Control::CONDITION));
  REQUIRE(
    core.generalRegister(11).low ==
    (EECOP1Control::STATUS_FIXED |
     EECOP1Control::CAUSE_MASK));
  REQUIRE(
    core.generalRegister(13).low ==
    (EECOP1Control::STATUS_FIXED |
     EECOP1Control::STICKY_MASK));
  REQUIRE(
    core.generalRegister(15).low ==
    (EECOP1Control::STATUS_FIXED |
     EECOP1Control::STATUS_WRITABLE_MASK));
  REQUIRE(
    core.generalRegister(16).low ==
    EECOP1Control::STATUS_FIXED);
  REQUIRE(
    core.cop1ControlRegister(31) ==
    EECOP1Control::STATUS_FIXED);
}

TEST_CASE("PS2DEV COP1 comparison guest covers every branch path")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("cop1_comparison_branches.elf"), 256);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  REQUIRE(result.execution.instructions == 38);
  REQUIRE_FALSE(result.execution.cycleLimitReached);
  REQUIRE(
    result.execution.programCounter ==
    EEGuestRuntime::RETURN_ADDRESS);

  const EECore &core = system.eeCore();
  REQUIRE(core.generalRegister(16).low == UINT64_C(0x5f));
  REQUIRE(core.generalRegister(17).low == UINT64_C(0xaa));
  REQUIRE(
    core.generalRegister(18).low ==
    EECOP1Control::STATUS_FIXED);
  REQUIRE(
    core.cop1ControlRegister(31) ==
    EECOP1Control::STATUS_FIXED);
}

TEST_CASE("PS2DEV COP1 conversion and unary guest covers raw edge cases")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("cop1_conversion_unary.elf"), 256);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  REQUIRE(result.execution.instructions == 35);
  REQUIRE_FALSE(result.execution.cycleLimitReached);
  REQUIRE(
    result.execution.programCounter ==
    EEGuestRuntime::RETURN_ADDRESS);

  const EECore &core = system.eeCore();
  REQUIRE(core.floatingPointRegister(10) == 0);
  REQUIRE(
    core.floatingPointRegister(11) ==
    UINT32_C(0x80000000));
  REQUIRE(
    core.floatingPointRegister(12) ==
    UINT32_C(0x40600000));
  REQUIRE(
    core.floatingPointRegister(13) ==
    UINT32_C(0x40600000));
  REQUIRE(core.floatingPointRegister(14) == 0);
  REQUIRE(
    core.floatingPointRegister(15) ==
    UINT32_C(0xc0400000));
  REQUIRE(
    core.floatingPointRegister(16) ==
    UINT32_C(0x4effffff));
  REQUIRE(
    core.floatingPointRegister(17) ==
    UINT32_C(0x7fffffff));
  REQUIRE(
    core.floatingPointRegister(18) ==
    UINT32_C(0x80000000));
  REQUIRE(
    core.floatingPointRegister(19) ==
    UINT32_C(0xffffffff));

  const std::uint32_t unaryStatus =
    EECOP1Control::STATUS_FIXED |
    EECOP1Control::STICKY_OVERFLOW |
    EECOP1Control::STICKY_UNDERFLOW;
  REQUIRE(core.generalRegister(23).low == unaryStatus);
  REQUIRE(core.generalRegister(24).low == unaryStatus);
  REQUIRE(
    core.generalRegister(25).low ==
    (unaryStatus |
     EECOP1Control::CAUSE_OVERFLOW |
     EECOP1Control::CAUSE_UNDERFLOW));
  REQUIRE(
    core.generalRegister(20).low ==
    (EECOP1Control::STATUS_FIXED |
     EECOP1Control::CAUSE_INVALID |
     EECOP1Control::STICKY_INVALID));
  REQUIRE(
    core.generalRegister(21).low ==
    (EECOP1Control::STATUS_FIXED |
     EECOP1Control::CAUSE_INVALID |
     EECOP1Control::STICKY_INVALID));
  REQUIRE(
    core.generalRegister(22).low ==
    (EECOP1Control::STATUS_FIXED |
     EECOP1Control::STICKY_INVALID));
  REQUIRE(
    core.cop1ControlRegister(31) ==
    (EECOP1Control::STATUS_FIXED |
     EECOP1Control::STICKY_INVALID));
}

TEST_CASE("PS2DEV COP1 basic arithmetic guest covers flags and saturation")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("cop1_basic_arithmetic.elf"), 256);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  REQUIRE(result.execution.instructions == 37);
  REQUIRE_FALSE(result.execution.cycleLimitReached);
  REQUIRE(
    result.execution.programCounter ==
    EEGuestRuntime::RETURN_ADDRESS);

  const EECore &core = system.eeCore();
  REQUIRE(
    core.floatingPointRegister(20) ==
    UINT32_C(0x40700000));
  REQUIRE(
    core.floatingPointRegister(21) ==
    UINT32_C(0x40800000));
  REQUIRE(
    core.floatingPointRegister(22) ==
    UINT32_C(0xc0c00000));
  REQUIRE(
    core.floatingPointRegister(23) ==
    UINT32_C(0xbf800000));
  REQUIRE(
    core.floatingPointRegister(24) ==
    UINT32_C(0xc0000000));
  REQUIRE(
    core.floatingPointRegister(25) ==
    UINT32_C(0x7fffffff));
  REQUIRE(core.floatingPointRegister(26) == 0);
  REQUIRE(
    core.floatingPointRegister(27) ==
    UINT32_C(0x7fffffff));
  REQUIRE(core.floatingPointRegister(28) == 0);
  REQUIRE(core.floatingPointRegister(29) == 0);
  REQUIRE(
    core.floatingPointRegister(30) ==
    UINT32_C(0x80000000));

  const std::uint32_t overflowStatus =
    EECOP1Control::STATUS_FIXED |
    EECOP1Control::CAUSE_OVERFLOW |
    EECOP1Control::STICKY_OVERFLOW;
  const std::uint32_t stickyStatus =
    EECOP1Control::STATUS_FIXED |
    EECOP1Control::STICKY_OVERFLOW |
    EECOP1Control::STICKY_UNDERFLOW;
  REQUIRE(core.generalRegister(15).low == overflowStatus);
  REQUIRE(
    core.generalRegister(16).low ==
    (stickyStatus |
     EECOP1Control::CAUSE_UNDERFLOW));
  REQUIRE(
    core.generalRegister(17).low ==
    (stickyStatus |
     EECOP1Control::CAUSE_OVERFLOW));
  REQUIRE(
    core.generalRegister(18).low ==
    (stickyStatus |
     EECOP1Control::CAUSE_UNDERFLOW));
  REQUIRE(core.generalRegister(19).low == stickyStatus);
  REQUIRE(core.generalRegister(20).low == stickyStatus);
  REQUIRE(core.cop1ControlRegister(31) == stickyStatus);
}

TEST_CASE("PS2DEV COP1 accumulator guest covers forwarding and flags")
{
  NekoSystem system;
  system.startTrace();
  const EEGuestExecutionResult result =
    system.runELF(readGuest("cop1_accumulator_compound.elf"), 256);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  REQUIRE(result.execution.instructions == 39);
  REQUIRE_FALSE(result.execution.cycleLimitReached);
  REQUIRE(
    result.execution.programCounter ==
    EEGuestRuntime::RETURN_ADDRESS);

  const EECore &core = system.eeCore();
  REQUIRE(
    core.floatingPointRegister(20) ==
    UINT32_C(0x40c00000));
  REQUIRE(
    core.floatingPointRegister(21) ==
    UINT32_C(0x3f800000));
  REQUIRE(
    core.floatingPointRegister(22) ==
    UINT32_C(0x41000000));
  REQUIRE(
    core.floatingPointRegister(23) ==
    UINT32_C(0x40800000));
  REQUIRE(
    core.floatingPointRegister(24) ==
    UINT32_C(0x40000000));
  REQUIRE(
    core.floatingPointRegister(25) ==
    UINT32_C(0x7fffffff));
  REQUIRE(
    core.floatingPointRegister(26) ==
    UINT32_C(0x40000000));
  REQUIRE(
    core.floatingPointAccumulator() ==
    UINT32_C(0x40000000));

  const std::uint32_t overflowStatus =
    EECOP1Control::STATUS_FIXED |
    EECOP1Control::CAUSE_OVERFLOW |
    EECOP1Control::STICKY_OVERFLOW;
  const std::uint32_t stickyStatus =
    EECOP1Control::STATUS_FIXED |
    EECOP1Control::STICKY_OVERFLOW |
    EECOP1Control::STICKY_UNDERFLOW;
  REQUIRE(core.generalRegister(15).low == overflowStatus);
  REQUIRE(
    core.generalRegister(16).low ==
    (stickyStatus |
     EECOP1Control::CAUSE_UNDERFLOW));
  REQUIRE(
    core.generalRegister(17).low ==
    (EECOP1Control::STATUS_FIXED |
     EECOP1Control::STICKY_UNDERFLOW));
  REQUIRE(
    core.generalRegister(18).low ==
    (stickyStatus |
     EECOP1Control::CAUSE_OVERFLOW));
  REQUIRE(core.generalRegister(19).low == stickyStatus);
  REQUIRE(core.cop1ControlRegister(31) == stickyStatus);

  const std::uint32_t chainAddresses[] = {
    result.load.entryPoint + 52,
    result.load.entryPoint + 56,
    result.load.entryPoint + 60,
    result.load.entryPoint + 64,
    result.load.entryPoint + 68,
    result.load.entryPoint + 72,
    result.load.entryPoint + 76,
    result.load.entryPoint + 80,
    result.load.entryPoint + 84,
    result.load.entryPoint + 88
  };
  const auto isChainAddress =
    [&chainAddresses](std::uint32_t address)
    {
      for (const std::uint32_t chainAddress :
           chainAddresses)
      {
        if (address == chainAddress)
        {
          return true;
        }
      }
      return false;
    };

  std::vector<NekoTraceEvent> admissions;
  std::vector<NekoTraceEvent> retirements;
  for (const NekoTraceEvent &event : system.trace())
  {
    const std::uint32_t address =
      static_cast<std::uint32_t>(event.value1);
    if (!isChainAddress(address))
    {
      continue;
    }
    if (event.type ==
          NekoTraceEventType::COP1StageTransition &&
        (event.value2 &
         NekoEETraceCOP1Stage::FROM_MASK) ==
          NekoEETraceCOP1Stage::NONE)
    {
      admissions.push_back(event);
    }
    else if (
      event.type == NekoTraceEventType::COP1Retired)
    {
      retirements.push_back(event);
    }
  }

  REQUIRE(admissions.size() == 10);
  REQUIRE(retirements.size() == 10);
  for (std::size_t index = 0; index < 10; ++index)
  {
    REQUIRE(
      static_cast<std::uint32_t>(
        admissions[index].value1) ==
      chainAddresses[index]);
    REQUIRE(
      static_cast<std::uint32_t>(
        retirements[index].value1) ==
      chainAddresses[index]);
  }
  const std::size_t forwardingConsumers[] = {
    1, 3, 5, 7, 8, 9
  };
  for (const std::size_t consumer :
       forwardingConsumers)
  {
    REQUIRE(
      admissions[consumer].masterCycle + 1 ==
      retirements[consumer - 1].masterCycle);
  }
}

TEST_CASE("PS2DEV COP1 divider guest overlaps initiation and visibility")
{
  NekoSystem system;
  system.startTrace();
  const EEGuestExecutionResult result =
    system.runELF(readGuest("cop1_dividers.elf"), 256);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  REQUIRE(result.execution.instructions == 17);
  REQUIRE_FALSE(result.execution.cycleLimitReached);
  REQUIRE(
    result.execution.programCounter ==
    EEGuestRuntime::RETURN_ADDRESS);

  const EECore &core = system.eeCore();
  for (const std::size_t registerIndex : {20, 21, 22})
  {
    REQUIRE(
      core.floatingPointRegister(registerIndex) ==
      UINT32_C(0x40400000));
  }
  for (const std::size_t registerIndex : {15, 16, 17})
  {
    REQUIRE(
      core.generalRegister(registerIndex).low ==
      UINT64_C(0x0000000040400000));
  }
  REQUIRE(
    core.generalRegister(18).low ==
    EECOP1Control::STATUS_FIXED);
  REQUIRE(
    core.cop1ControlRegister(31) ==
    EECOP1Control::STATUS_FIXED);

  const std::uint32_t dividerAddresses[] = {
    result.load.entryPoint + 28,
    result.load.entryPoint + 32,
    result.load.entryPoint + 36
  };
  const auto isDividerAddress =
    [&dividerAddresses](std::uint32_t address)
    {
      for (const std::uint32_t dividerAddress :
           dividerAddresses)
      {
        if (address == dividerAddress)
        {
          return true;
        }
      }
      return false;
    };

  std::vector<NekoTraceEvent> admissions;
  std::vector<NekoTraceEvent> retirements;
  for (const NekoTraceEvent &event : system.trace())
  {
    const std::uint32_t address =
      static_cast<std::uint32_t>(event.value1);
    if (!isDividerAddress(address))
    {
      continue;
    }
    if (event.type ==
          NekoTraceEventType::COP1StageTransition &&
        (event.value2 &
         NekoEETraceCOP1Stage::FROM_MASK) ==
          NekoEETraceCOP1Stage::NONE)
    {
      admissions.push_back(event);
    }
    else if (
      event.type == NekoTraceEventType::COP1Retired)
    {
      retirements.push_back(event);
    }
  }

  REQUIRE(admissions.size() == 3);
  REQUIRE(retirements.size() == 3);
  const std::uint64_t latencies[] = {14, 8, 8};
  for (std::size_t index = 0; index < 3; ++index)
  {
    REQUIRE(
      static_cast<std::uint32_t>(
        admissions[index].value1) ==
      dividerAddresses[index]);
    REQUIRE(
      admissions[index].value2 ==
      (NekoEETraceCOP1Stage::NONE |
       (NekoEETraceCOP1Stage::R <<
        NekoEETraceCOP1Stage::TO_SHIFT) |
       (latencies[index] <<
        NekoEETraceCOP1Stage::
          REMAINING_CYCLES_SHIFT)));
    REQUIRE(
      retirements[index].masterCycle ==
      admissions[index].masterCycle +
        latencies[index]);
  }
  REQUIRE(
    admissions[1].masterCycle ==
    admissions[0].masterCycle + 13);
  REQUIRE(
    retirements[0].masterCycle ==
    admissions[1].masterCycle + 1);
  REQUIRE(
    admissions[2].masterCycle ==
    admissions[1].masterCycle + 7);
  REQUIRE(
    retirements[1].masterCycle ==
    admissions[2].masterCycle + 1);
}

TEST_CASE("PS2DEV COP1 mixed guest is concurrent and deterministic")
{
  const std::vector<std::uint8_t> guest =
    readGuest("cop1_mixed_concurrent.elf");
  NekoSystem first;
  NekoSystem second;
  first.startTrace();
  second.startTrace();

  const EEGuestExecutionResult firstResult =
    first.runELF(guest, 512);
  const EEGuestExecutionResult secondResult =
    second.runELF(guest, 512);

  REQUIRE(firstResult.outcome == EEGuestOutcome::Completed);
  REQUIRE(firstResult.exitCode == 0);
  REQUIRE(firstResult.execution.instructions == 27);
  REQUIRE_FALSE(firstResult.execution.cycleLimitReached);
  REQUIRE(
    firstResult.execution.programCounter ==
    EEGuestRuntime::RETURN_ADDRESS);
  REQUIRE(
    firstResult.execution.masterCycles ==
    secondResult.execution.masterCycles);
  REQUIRE(
    firstResult.execution.eeCycles ==
    secondResult.execution.eeCycles);
  REQUIRE(
    firstResult.execution.instructions ==
    secondResult.execution.instructions);
  REQUIRE(firstResult.outcome == secondResult.outcome);
  REQUIRE(firstResult.exitCode == secondResult.exitCode);
  REQUIRE(
    first.eeCore().stateHash() ==
    second.eeCore().stateHash());
  REQUIRE(first.traceHash() == second.traceHash());
  REQUIRE(first.saveState() == second.saveState());

  const EECore &core = first.eeCore();
  REQUIRE(
    core.floatingPointRegister(4) ==
    UINT32_C(0x89abcdef));
  REQUIRE(
    core.floatingPointRegister(10) ==
    UINT32_C(0x40700000));
  REQUIRE(
    core.floatingPointRegister(11) ==
    UINT32_C(0x40a80000));
  REQUIRE(
    core.floatingPointRegister(12) ==
    UINT32_C(0xc0400000));
  REQUIRE(
    core.floatingPointRegister(13) ==
    UINT32_C(0xbfc00000));
  REQUIRE(
    core.floatingPointRegister(14) ==
    UINT32_C(0x40a80000));
  REQUIRE(
    core.floatingPointRegister(15) ==
    UINT32_C(0xc0100000));
  REQUIRE(
    core.generalRegister(10).low ==
    UINT64_C(0xffffffff89abcdef));
  REQUIRE(
    core.generalRegister(11).low ==
    UINT64_C(0x0000000040700000));
  REQUIRE(
    core.generalRegister(12).low ==
    UINT64_C(0x0000000040a80000));
  REQUIRE(
    core.generalRegister(13).low ==
    UINT64_C(0xffffffffc0400000));
  REQUIRE(
    core.generalRegister(14).low ==
    UINT64_C(0xffffffffbfc00000));
  REQUIRE(
    core.generalRegister(15).low ==
    UINT64_C(0x0000000040a80000));
  REQUIRE(
    core.generalRegister(16).low ==
    UINT64_C(0xffffffffc0100000));
  REQUIRE(
    core.generalRegister(17).low ==
    EECOP1Control::STATUS_FIXED);
  REQUIRE(
    core.cop1ControlRegister(31) ==
    EECOP1Control::STATUS_FIXED);

  const std::uint32_t payloadAddress =
    static_cast<std::uint32_t>(
      core.generalRegister(8).low);
  REQUIRE(
    first.eeBus().read32(payloadAddress + 16) ==
    UINT32_C(0x40a80000));
  REQUIRE(
    first.eeBus().read32(payloadAddress + 20) ==
    UINT32_C(0x40a80000));
  REQUIRE(
    second.eeBus().read32(payloadAddress + 16) ==
    UINT32_C(0x40a80000));
  REQUIRE(
    second.eeBus().read32(payloadAddress + 20) ==
    UINT32_C(0x40a80000));

  const std::uint32_t pairAddresses[] = {
    firstResult.load.entryPoint + 28,
    firstResult.load.entryPoint + 32,
    firstResult.load.entryPoint + 36,
    firstResult.load.entryPoint + 40,
    firstResult.load.entryPoint + 44,
    firstResult.load.entryPoint + 48,
    firstResult.load.entryPoint + 52,
    firstResult.load.entryPoint + 56,
    firstResult.load.entryPoint + 60,
    firstResult.load.entryPoint + 64
  };
  std::vector<NekoTraceEvent> admissions;
  for (const NekoTraceEvent &event : first.trace())
  {
    if (event.type !=
          NekoTraceEventType::COP1StageTransition ||
        (event.value2 &
         NekoEETraceCOP1Stage::FROM_MASK) !=
          NekoEETraceCOP1Stage::NONE)
    {
      continue;
    }
    const std::uint32_t address =
      static_cast<std::uint32_t>(event.value1);
    for (const std::uint32_t pairAddress :
         pairAddresses)
    {
      if (address == pairAddress)
      {
        admissions.push_back(event);
        break;
      }
    }
  }

  REQUIRE(admissions.size() == 10);
  for (std::size_t index = 0; index < 10; ++index)
  {
    REQUIRE(
      static_cast<std::uint32_t>(
        admissions[index].value1) ==
      pairAddresses[index]);
  }
  for (std::size_t index = 0; index < 10; index += 2)
  {
    REQUIRE(
      admissions[index].masterCycle ==
      admissions[index + 1].masterCycle);
  }

  struct MoveInterlock
  {
    std::uint32_t address;
    std::size_t admissionIndex;
    EEOperation blocker;
  };
  const MoveInterlock moveInterlocks[] = {
    {
      pairAddresses[1],
      1,
      EEOperation::AddSingleCOP1
    },
    {
      pairAddresses[2],
      2,
      EEOperation::AddSingleCOP1
    },
    {
      pairAddresses[4],
      4,
      EEOperation::ConvertWordToSingleCOP1
    },
    {
      pairAddresses[7],
      7,
      EEOperation::AddSingleCOP1
    },
    {
      pairAddresses[8],
      8,
      EEOperation::MultiplySingleCOP1
    }
  };
  for (const MoveInterlock &interlock : moveInterlocks)
  {
    std::size_t interlockCount = 0;
    for (const NekoTraceEvent &event : first.trace())
    {
      if (event.type ==
            NekoTraceEventType::COP1ResourceInterlock &&
          event.value0 == interlock.address &&
          event.masterCycle >
            admissions[interlock.admissionIndex]
              .masterCycle &&
          event.value2 ==
            static_cast<std::uint8_t>(
              interlock.blocker))
      {
        REQUIRE(
          event.masterCycle ==
          admissions[interlock.admissionIndex]
              .masterCycle + 1);
        ++interlockCount;
      }
    }
    REQUIRE(interlockCount == 1);
  }

  std::uint64_t firstAddRetirement = 0;
  for (const NekoTraceEvent &event : first.trace())
  {
    if (event.type == NekoTraceEventType::COP1Retired &&
        static_cast<std::uint32_t>(event.value1) ==
          pairAddresses[0])
    {
      firstAddRetirement = event.masterCycle;
    }
  }
  REQUIRE(firstAddRetirement != 0);
  REQUIRE(
    admissions[2].masterCycle ==
    firstAddRetirement);
}

TEST_CASE("PS2DEV EE ELF guest controls and polls vector units through COP2")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("cop2_control.elf"), 96);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(system.vu0().intRegisterValue(3) == UINT16_C(0x7f01));
  REQUIRE(system.eeCore().generalRegister(11).low == 1);
  REQUIRE(system.eeCore().generalRegister(12).low == 1);
  REQUIRE(system.vu1().getState() == VPU_STATE_STOP);
  REQUIRE(system.vu1().stoppedByForceBreak());
}

TEST_CASE("PS2DEV EE ELF guest transfers vectors through VU0 COP2")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("cop2_transfer.elf"), 64);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  const FPRegister *vf1 = system.vu0().fpRegisterValue(1);
  const FPRegister *vf2 = system.vu0().fpRegisterValue(2);
  REQUIRE(vf1->x.bits() == UINT32_C(0x33221100));
  REQUIRE(vf1->y.bits() == UINT32_C(0x77665544));
  REQUIRE(vf1->z.bits() == UINT32_C(0xbbaa9988));
  REQUIRE(vf1->w.bits() == UINT32_C(0xffeeddcc));
  REQUIRE(vf2->x.bits() == vf1->x.bits());
  REQUIRE(vf2->y.bits() == vf1->y.bits());
  REQUIRE(vf2->z.bits() == vf1->z.bits());
  REQUIRE(vf2->w.bits() == vf1->w.bits());
}

TEST_CASE("PS2DEV EE ELF guest calls VU0 microprograms through COP2")
{
  NekoSystem system;
  uploadVectorCopyProgram(&system.vu0());

  const EEGuestExecutionResult result =
    system.runELF(readGuest("vcallms.elf"), 128);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  REQUIRE(result.execution.instructions == 36);
  const FPRegister *vf1 = system.vu0().fpRegisterValue(1);
  const FPRegister *vf2 = system.vu0().fpRegisterValue(2);
  REQUIRE(vf2->x.bits() == vf1->x.bits());
  REQUIRE(vf2->y.bits() == vf1->y.bits());
  REQUIRE(vf2->z.bits() == vf1->z.bits());
  REQUIRE(vf2->w.bits() == vf1->w.bits());
  REQUIRE_FALSE(system.vu0().clockActive());
}

TEST_CASE("PS2DEV EE ELF guest executes VU0 macro arithmetic")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(
      readGuest("vu_macro_arithmetic.elf"),
      128);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  const FPRegister *dependent =
    system.vu0().fpRegisterValue(4);
  REQUIRE(dependent->x == 1);
  REQUIRE(dependent->y == 2);
  REQUIRE(dependent->z == 3);
  REQUIRE(dependent->w == 4);
  const FPRegister *broadcast =
    system.vu0().fpRegisterValue(5);
  REQUIRE(broadcast->x == 6);
  REQUIRE(broadcast->y == 16);
  REQUIRE(broadcast->z == 26);
  REQUIRE(broadcast->w == 36);
}

TEST_CASE("PS2DEV EE ELF guest executes remaining VU0 macro families")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(
      readGuest("vu_macro_families.elf"),
      512);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  REQUIRE(result.execution.instructions == 107);
  REQUIRE(system.vu0().intRegisterValue(3) == 12);
  const FPRegister *moved =
    system.vu0().fpRegisterValue(8);
  REQUIRE(moved->x == 20);
  REQUIRE(moved->y == 3);
  REQUIRE(moved->z == 24);
  REQUIRE(moved->w == 10);
}

TEST_CASE("PS2DEV EE ELF guest configures VIF1 DMA")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("vif1_dma.elf"), 128);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(system.vif1().wordsIngested() == 4);
  REQUIRE(system.vif1().fifoQuadwordCount() == 0);
  REQUIRE(system.vif1DMAC().transferredQuadwordCount() == 1);
  REQUIRE(
    (system.vif1DMAC().channelControl() &
     GIFDMACChannelControl::START) == 0);
  REQUIRE(
    (system.gifDMAC().globalStatus() &
     GIFDMACStatus::CHANNEL_1) != 0);
  REQUIRE(
    (system.gifDMAC().globalStatus() &
     GIFDMACStatus::CHANNEL_1_MASK) != 0);
  REQUIRE(system.gifDMAC().interruptPending());
}

TEST_CASE("PS2DEV EE ELF guest renders a rotating VU1 triangle")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("rotation_vu1.elf"), 4096);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  REQUIRE(result.execution.instructions == 1361);
  REQUIRE(system.vu1().getState() == VPU_STATE_READY);
  REQUIRE_FALSE(system.gifPath1().path1TransferActive());
  REQUIRE(system.gifPath1().transferredQuadwordCount() == 12);
  REQUIRE(system.vif1DMAC().transferredQuadwordCount() == 38);
  REQUIRE(system.vif1().wordsIngested() == 152);
  REQUIRE(system.gs().triangleCount() == 1);
  const GIFQuadword first =
    system.vu1().readDataQuadword(12);
  const GIFQuadword second =
    system.vu1().readDataQuadword(14);
  const GIFQuadword third =
    system.vu1().readDataQuadword(16);
  REQUIRE(first[0] == UINT32_C(0x00008000));
  REQUIRE(first[1] == UINT32_C(0x00007caf));
  REQUIRE(first[2] == UINT32_C(0x00000c00));
  REQUIRE(second[0] == UINT32_C(0x000071db));
  REQUIRE(second[1] == UINT32_C(0x00008ad4));
  REQUIRE(second[2] == UINT32_C(0x00000c00));
  REQUIRE(third[0] == UINT32_C(0x00007783));
  REQUIRE(third[1] == UINT32_C(0x0000907c));
  REQUIRE(third[2] == UINT32_C(0x00000c00));
  REQUIRE(system.gs().pixelWriteCount() == 20566);
  REQUIRE(
    system.gs().framebufferHash(0, 640, 448) ==
    UINT64_C(0xdf0bce57b91fbbd3));
}

TEST_CASE("PS2DEV EE ELF guest renders the point and sprite scene")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("point_sprite.elf"), 40000);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  REQUIRE(system.gifPath3().guestFIFOQuadwordCount() == 0);
  REQUIRE(system.gifPath3().transferredQuadwordCount() == 226);
  REQUIRE(system.gs().pointCount() == 96);
  REQUIRE(system.gs().lineCount() == 0);
  REQUIRE(system.gs().spriteCount() == 7);
  REQUIRE(system.gs().triangleCount() == 0);
  REQUIRE(
    system.gs().framebufferHash(0, 640, 448) ==
    UINT64_C(0xc1adcf6554c82b99));
}

TEST_CASE("PS2DEV EE ELF guest writes VIF and GIF FIFOs")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("fifo.elf"), 128);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(system.vif0().wordsIngested() == 4);
  REQUIRE(system.vif1().wordsIngested() == 4);
  REQUIRE(system.vif0().fifoQuadwordCount() == 0);
  REQUIRE(system.vif1().fifoQuadwordCount() == 0);
  REQUIRE(system.gifPath3().guestFIFOQuadwordCount() == 0);
  REQUIRE(system.gifPath3().transferredQuadwordCount() == 1);
}

TEST_CASE("PS2DEV EE ELF guest drives mapped device registers")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("mmio.elf"), 128);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(
    system.interruptController().mask() ==
    EEInterruptSource::mask(EEInterruptSource::VIF0));
  REQUIRE(system.gifDMAC().globalControl() == 1);
  REQUIRE(system.gs().hostInterfaceReversed());
}

TEST_CASE("PS2DEV EE ELF guest runs through frontend support")
{
  const neko_frontend::ELFRunReport report =
    neko_frontend::runELFFile(
      guestPath("arithmetic.elf"),
      128);

  REQUIRE(report.result.outcome == EEGuestOutcome::Completed);
  REQUIRE(report.result.exitCode == 0);
  REQUIRE(report.hostExitCode == 0);
  REQUIRE(
    report.diagnostic.find("outcome=completed") !=
    std::string::npos);
}
