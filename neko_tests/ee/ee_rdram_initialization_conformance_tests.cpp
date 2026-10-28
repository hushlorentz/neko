#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "neko_system.hpp"
#include "save_state_diagnostics.hpp"

namespace
{
  constexpr std::uint32_t COMMAND = 0x1000f430;
  constexpr std::uint32_t DATA = 0x1000f440;
  constexpr std::uint32_t GUEST_COMMAND = 0xb000f430;
  constexpr std::uint32_t GUEST_DATA = 0xb000f440;
  constexpr std::uint32_t ENTRY = 0xa0000000;
  constexpr std::uint32_t ENUMERATE = 0x80210000;
  constexpr std::uint32_t REINITIALIZE = 0x80210060;
  constexpr std::uint32_t SA4D = 0x804d0060;
  constexpr auto LOAD = NekoEETraceMemory::AccessKind::DataLoad;
  constexpr auto STORE = NekoEETraceMemory::AccessKind::DataStore;

  constexpr std::uint32_t PROTOCOL[] = {
    0x8c230000, // lw r3,0(r1)
    0x0460fffe, // bltz r3,poll_before
    0x254a0001, // addiu r10,r10,1
    0xac200010, // sw zero,16(r1)
    0xac220000, // sw r2,0(r1): initialize
    0x8c230000, // lw r3,0(r1)
    0x0460fffe, // bltz r3,poll_after
    0x254a0001, // addiu r10,r10,1
    0xac360000, // sw r22,0(r1): SA4d before enumeration
    0x8c230000, // lw r3,0(r1)
    0xac290010, // sw r9,16(r1): passive input after SA4d
    0x8c230000, // lw r3,0(r1)
    0xac200010, // sw zero,16(r1): prepare repeat submission
    0xac360000, // sw r22,0(r1)
    0xac240000, // sw r4,0(r1): enumerate
    0x8c2b0010, // lw r11,16(r1): first device
    0xac360000, // sw r22,0(r1): preserve partial enumeration
    0x8c230000, // lw r3,0(r1)
    0xac290010, // sw r9,16(r1)
    0xac260000, // sw r6,0(r1): config A
    0x8c2c0010, // lw r12,16(r1)
    0xac270000, // sw r7,0(r1): config B
    0x8c2d0010, // lw r13,16(r1)
    0xac280000, // sw r8,0(r1): device ID
    0x8c2e0010, // lw r14,16(r1)
    0xac200010, // sw zero,16(r1)
    0xac360000, // sw r22,0(r1): preserve cursor after configuration reads
    0xac240000, // sw r4,0(r1): resume enumeration
    0x8c2f0010, // lw r15,16(r1): second device
    0x8c300010, // lw r16,16(r1): exhausted
    0xac360000, // sw r22,0(r1): preserve exhausted enumeration
    0xac290010, // sw r9,16(r1): input bit7 set
    0x8c230000, // lw r3,0(r1)
    0xac250000, // sw r5,0(r1): preserve enumeration
    0xac200010, // sw zero,16(r1): passive input, not a replay
    0xac240000, // sw r4,0(r1)
    0x8c310010, // lw r17,16(r1): still exhausted
    0xac250000, // sw r5,0(r1): reset enumeration with bit7 clear
    0x8c230000, // lw r3,0(r1): status does not enumerate
    0xac290010, // sw r9,16(r1): cannot undo the accepted reset
    0xac240000, // sw r4,0(r1)
    0x8c320010, // lw r18,16(r1): first device again
    0x8c330010, // lw r19,16(r1): second device again
    0x8c340010, // lw r20,16(r1): exhausted again
    0xac200010, // sw zero,16(r1)
    0xac220000, // sw r2,0(r1): initialization preserves enumeration
    0xac360000, // sw r22,0(r1)
    0xac240000, // sw r4,0(r1)
    0x8c350010, // lw r21,16(r1): still exhausted
    0x1000ffff, // beq zero,zero,done
    0
  };
  constexpr std::uint32_t DONE = ENTRY + 49 * 4;

  void startEE(NekoSystem &system)
  {
    system.eeCore().setCOP0Register(EECOP0Register::Status, 0);
    system.eeCore().startExecution(ENTRY);
  }

  void startProtocol(NekoSystem &system)
  {
    for (unsigned index = 0; index < sizeof(PROTOCOL) / sizeof(PROTOCOL[0]); ++index)
    {
      system.eeBus().write32(index * 4, PROTOCOL[index]);
    }
    auto &core = system.eeCore();
    core.setGeneralRegister(1, {GUEST_COMMAND, 0});
    core.setGeneralRegister(2, {0x800003a0, 0});
    core.setGeneralRegister(4, {ENUMERATE, 0});
    core.setGeneralRegister(5, {REINITIALIZE, 0});
    core.setGeneralRegister(6, {0x80230000, 0});
    core.setGeneralRegister(7, {0x80240000, 0});
    core.setGeneralRegister(8, {0x8040001b, 0});
    core.setGeneralRegister(9, {0x80, 0});
    core.setGeneralRegister(22, {SA4D, 0});
    for (unsigned index = 11; index <= 21; ++index)
    {
      core.setGeneralRegister(index, {UINT64_MAX, 0});
    }
    startEE(system);
  }

  void advanceTo(NekoSystem &system, std::uint32_t pc)
  {
    for (unsigned cycle = 0; cycle < 256 &&
         system.eeCore().programCounter() != pc &&
         system.eeCore().pendingException() == EEException::None; ++cycle)
    {
      system.clockMasterCycle();
    }
    REQUIRE(system.eeCore().pendingException() == EEException::None);
    REQUIRE(system.eeCore().programCounter() == pc);
  }

  std::vector<NekoTraceEvent> accesses(const NekoSystem &system)
  {
    std::vector<NekoTraceEvent> result;
    for (const auto &event : system.trace())
    {
      if (event.subsystem == NekoTraceSubsystem::EE &&
          event.type == NekoTraceEventType::MemoryAccess &&
          event.value0 >= GUEST_COMMAND && event.value0 < GUEST_DATA + 16)
      {
        result.push_back(event);
      }
    }
    return result;
  }

  void requireProtocolResult(const NekoSystem &system)
  {
    const auto &core = system.eeCore();
    REQUIRE(core.pendingException() == EEException::None);
    REQUIRE((core.programCounter() == DONE || core.programCounter() == DONE + 4));
    REQUIRE(core.generalRegister(3).low == 0);
    REQUIRE(core.generalRegister(10).low == 2);
    constexpr std::uint32_t expected[] = {
      0x1f, 0x0d0d, 0x0090, 0x1b, 0x1f, 0, 0, 0x1f, 0x1f, 0, 0
    };
    for (unsigned index = 0; index < sizeof(expected) / sizeof(expected[0]); ++index)
    {
      CAPTURE(index);
      REQUIRE(core.generalRegister(11 + index).low == expected[index]);
    }
    REQUIRE(system.interruptController().status() == 0);
    REQUIRE(system.iopInterruptController().status() == 0);
  }

  void seedContinuation(NekoSystem &system)
  {
    REQUIRE(system.eeBus().writeData32(COMMAND, ENUMERATE));
    REQUIRE(system.eeBus().read32(DATA) == 0x1f);
    REQUIRE(system.eeBus().writeData32(DATA, 0x80));
  }

  void requirePreservedContinuation(
    NekoSystem &system, std::uint32_t reinitializedResponse)
  {
    REQUIRE(system.eeBus().read32(DATA) == 0x1f);
    REQUIRE(system.eeBus().read32(DATA) == 0);
    REQUIRE(system.eeBus().writeData32(COMMAND, REINITIALIZE));
    REQUIRE(system.eeBus().writeData32(COMMAND, ENUMERATE));
    REQUIRE(system.eeBus().read32(DATA) == reinitializedResponse);
  }

  void requireFault(
    const NekoSystem &system, const EEExecutionResult &result,
    std::uint32_t address, std::uint32_t instructionOffset, EEException exception)
  {
    REQUIRE(result.pendingException == exception);
    REQUIRE_FALSE(result.cycleLimitReached);
    REQUIRE(result.exceptionAddress == address);
    REQUIRE(system.eeCore().cop0Register(EECOP0Register::EPC) == ENTRY);
    REQUIRE((system.eeCore().cop0Register(EECOP0Register::Cause) & 0x7c) == 0x1c);
    REQUIRE((system.eeCore().cop0Register(EECOP0Register::Cause) >> 31) ==
      (instructionOffset == 4 ? 1u : 0u));
    REQUIRE_FALSE(system.traceTruncated());
  }
}

TEST_CASE("EE RDRAM guest polling and command ordering exercise the bounded retail protocol",
  "[ee][guest][trace][rdram-init-conformance]")
{
  NekoSystem system;
  startProtocol(system);
  system.startTrace(8192);
  system.runMasterCycles(256);
  requireProtocolResult(system);
  REQUIRE_FALSE(system.traceTruncated());
  struct Access
  {
    std::uint32_t address;
    std::uint32_t value;
    NekoEETraceMemory::AccessKind kind;
  };
  constexpr Access expected[] = {
    {COMMAND, 0, LOAD}, {DATA, 0, STORE}, {COMMAND, 0x800003a0, STORE},
    {COMMAND, 0, LOAD}, {COMMAND, SA4D, STORE}, {COMMAND, 0, LOAD},
    {DATA, 0x80, STORE}, {COMMAND, 0, LOAD},
    {DATA, 0, STORE}, {COMMAND, SA4D, STORE},
    {COMMAND, ENUMERATE, STORE}, {DATA, 0x1f, LOAD},
    {COMMAND, SA4D, STORE}, {COMMAND, 0, LOAD}, {DATA, 0x80, STORE},
    {COMMAND, 0x80230000, STORE}, {DATA, 0x0d0d, LOAD},
    {COMMAND, 0x80240000, STORE}, {DATA, 0x0090, LOAD},
    {COMMAND, 0x8040001b, STORE}, {DATA, 0x1b, LOAD},
    {DATA, 0, STORE}, {COMMAND, SA4D, STORE},
    {COMMAND, ENUMERATE, STORE}, {DATA, 0x1f, LOAD}, {DATA, 0, LOAD},
    {COMMAND, SA4D, STORE}, {DATA, 0x80, STORE}, {COMMAND, 0, LOAD},
    {COMMAND, REINITIALIZE, STORE}, {DATA, 0, STORE},
    {COMMAND, ENUMERATE, STORE}, {DATA, 0, LOAD}, {COMMAND, REINITIALIZE, STORE},
    {COMMAND, 0, LOAD}, {DATA, 0x80, STORE}, {COMMAND, ENUMERATE, STORE},
    {DATA, 0x1f, LOAD}, {DATA, 0x1f, LOAD}, {DATA, 0, LOAD},
    {DATA, 0, STORE}, {COMMAND, 0x800003a0, STORE},
    {COMMAND, SA4D, STORE},
    {COMMAND, ENUMERATE, STORE}, {DATA, 0, LOAD}
  };
  const auto actual = accesses(system);
  REQUIRE(actual.size() == sizeof(expected) / sizeof(expected[0]));
  for (unsigned index = 0; index < actual.size(); ++index)
  {
    CAPTURE(index);
    REQUIRE(actual[index].value0 == (expected[index].address | 0xa0000000));
    REQUIRE(actual[index].value1 == expected[index].value);
    REQUIRE(actual[index].value2 == 0);
    REQUIRE(actual[index].value3 == nekoPackEEMemoryTraceMetadata({
      expected[index].address, 4, expected[index].kind,
      NekoEETraceMemory::TranslationOutcome::Translated,
      NekoEETraceMemory::CacheRoute::Uncached, 2, false, true,
      NekoEETraceMemory::CacheAccess::Bypassed,
      NekoEETraceMemory::TransferOutcome::Completed,
      NekoEETraceMemory::FailurePhase::None
    }));
    if (index != 0)
    {
      REQUIRE(actual[index].masterCycle > actual[index - 1].masterCycle);
    }
  }
}

TEST_CASE("EE RDRAM guest continuations restore identically across trace modes",
  "[ee][guest][trace][save-state][rdram-init-conformance]")
{
  struct Checkpoint
  {
    unsigned instruction;
    std::uint32_t command;
    std::uint32_t input;
    std::uint8_t cursor;
  };
  for (const auto point : {
         Checkpoint{0, 0, 0, 0},
         Checkpoint{5, 0x3a0, 0, 0},
         Checkpoint{9, 0x004d0060, 0, 0},
         Checkpoint{11, 0x004d0060, 0x80, 0},
         Checkpoint{16, 0x00210000, 0, 1},
         Checkpoint{17, 0x004d0060, 0, 1},
         Checkpoint{19, 0x004d0060, 0x80, 1},
         Checkpoint{31, 0x004d0060, 0, 2},
         Checkpoint{33, 0x004d0060, 0x80, 2},
         Checkpoint{34, 0x00210060, 0x80, 2},
         Checkpoint{35, 0x00210060, 0, 2},
         Checkpoint{40, 0x00210060, 0x80, 0},
         Checkpoint{46, 0x3a0, 0, 2}})
  {
    CAPTURE(point.instruction);
    NekoSystem original;
    startProtocol(original);
    advanceTo(original, ENTRY + point.instruction * 4);
    const auto saved = original.saveState();
    const auto fields = inspectSaveState(saved);
    const auto *command = fields.find("system.eeRDRAMInitialization.command");
    const auto *input = fields.find("system.eeRDRAMInitialization.inputData");
    const auto *cursor = fields.find("system.eeRDRAMInitialization.enumeratedDevices");
    REQUIRE(command != nullptr);
    REQUIRE(input != nullptr);
    REQUIRE(cursor != nullptr);
    REQUIRE(command->scalarValue == point.command);
    REQUIRE(input->scalarValue == point.input);
    REQUIRE(cursor->scalarValue == point.cursor);
    NekoSystem restored;
    NekoSystem plain;
    NekoSystem capped;
    for (auto *machine : {&restored, &plain, &capped})
    {
      machine->loadState(saved);
      REQUIRE(machine->eeStateHash() == original.eeStateHash());
    }
    original.startTrace(8192);
    restored.startTrace(8192);
    capped.startTrace(0);
    for (auto *machine : {&original, &restored, &plain, &capped})
    {
      machine->runMasterCycles(256);
      requireProtocolResult(*machine);
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
    for (unsigned index = 0; index < original.trace().size(); ++index)
    {
      CAPTURE(index);
      const auto &actual = restored.trace()[index];
      const auto &expected = original.trace()[index];
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

TEST_CASE("EE RDRAM guest non-word accesses fault without consuming protocol state",
  "[ee][guest][trace][rdram-init-conformance]")
{
  struct Access
  {
    std::uint8_t opcode;
    std::uint8_t width;
    NekoEETraceMemory::AccessKind kind;
  };
  for (const auto access : {
         Access{0x20, 1, LOAD}, Access{0x24, 1, LOAD},
         Access{0x21, 2, LOAD}, Access{0x25, 2, LOAD},
         Access{0x37, 8, LOAD}, Access{0x1e, 16, LOAD},
         Access{0x28, 1, STORE}, Access{0x29, 2, STORE},
         Access{0x3f, 8, STORE}, Access{0x1f, 16, STORE}})
  {
    for (const auto base : {GUEST_COMMAND, GUEST_DATA})
    {
      for (unsigned lane = 0; lane < (access.width < 4 ? 4u : 1u);
           lane += access.width)
      {
        for (const auto offset : {0u, 4u})
        {
          CAPTURE(access.opcode);
          CAPTURE(base);
          CAPTURE(lane);
          CAPTURE(offset);
          NekoSystem system;
          seedContinuation(system);
          const std::uint32_t instruction =
            (std::uint32_t(access.opcode) << 26) | (1u << 21) | (2u << 16);
          system.eeBus().write32(0, offset == 0 ? instruction : 0x10000003);
          system.eeBus().write32(4, offset == 0 ? 0 : instruction);
          system.eeCore().setGeneralRegister(1, {base + lane, 0});
          system.eeCore().setGeneralRegister(2, {0x12345678, 0xfeedface});
          startEE(system);
          system.startTrace(100);
          const auto result = system.runEE(16);
          requireFault(system, result, base + lane, offset,
            access.kind == LOAD ? EEException::DataBusErrorLoad
                                : EEException::DataBusErrorStore);
          REQUIRE(system.eeCore().generalRegister(2).low == 0x12345678);
          REQUIRE(system.eeCore().generalRegister(2).high == 0xfeedface);
          const auto memory = accesses(system);
          REQUIRE(memory.size() == 1);
          REQUIRE(memory[0].value3 == nekoPackEEMemoryTraceMetadata({
            (base + lane) & 0x1fffffff, access.width, access.kind,
            NekoEETraceMemory::TranslationOutcome::Translated,
            NekoEETraceMemory::CacheRoute::Uncached, 2, false, true,
            NekoEETraceMemory::CacheAccess::Bypassed,
            NekoEETraceMemory::TransferOutcome::PhysicalBusError,
            NekoEETraceMemory::FailurePhase::Direct
          }));
          requirePreservedContinuation(system, 0);
        }
      }
    }
  }
}

TEST_CASE("EE RDRAM guest rejected commands preserve enumeration and delay-slot provenance",
  "[ee][guest][trace][rdram-init-conformance]")
{
  struct Submission
  {
    std::uint32_t value;
    std::uint32_t input;
  };
  for (const auto submission : {
         Submission{0, 0x80}, Submission{0x00210000, 0x80},
         Submission{0x800003a0, 0x80},
         Submission{SA4D, 0x80}, Submission{SA4D, UINT32_MAX},
         Submission{0x004d0060, 0},
         Submission{0x804d0061, 0}, Submission{0x804d0040, 0},
         Submission{0x804d0020, 0}, Submission{0x804d0460, 0},
         Submission{0x904d0060, 0}})
  {
    for (const auto offset : {0u, 4u})
    {
      CAPTURE(submission.value);
      CAPTURE(submission.input);
      CAPTURE(offset);
      NekoSystem system;
      seedContinuation(system);
      REQUIRE(system.eeBus().writeData32(DATA, submission.input));
      system.eeBus().write32(0, offset == 0 ? 0xac220000 : 0x10000003);
      system.eeBus().write32(4, offset == 0 ? 0 : 0xac220000);
      system.eeCore().setGeneralRegister(1, {GUEST_COMMAND, 0});
      system.eeCore().setGeneralRegister(2, {submission.value, 0});
      startEE(system);
      system.startTrace(100);
      const auto result = system.runEE(16);
      requireFault(system, result, GUEST_COMMAND, offset, EEException::DataBusErrorStore);
      REQUIRE(system.eeCore().generalRegister(2).low == submission.value);
      REQUIRE(system.eeCore().generalRegister(2).high == 0);
      const auto memory = accesses(system);
      REQUIRE(memory.size() == 1);
      REQUIRE(memory[0].value1 == submission.value);
      REQUIRE(memory[0].value3 == nekoPackEEMemoryTraceMetadata({
        COMMAND, 4, STORE, NekoEETraceMemory::TranslationOutcome::Translated,
        NekoEETraceMemory::CacheRoute::Uncached, 2, false, true,
        NekoEETraceMemory::CacheAccess::Bypassed,
        NekoEETraceMemory::TransferOutcome::PhysicalBusError,
        NekoEETraceMemory::FailurePhase::Direct
      }));
      requirePreservedContinuation(system,
        (submission.input & 0x80) == 0 ? 0x1f : 0);
    }
  }
}

TEST_CASE("EE RDRAM guest data reads without a response fault before register writeback",
  "[ee][guest][trace][rdram-init-conformance]")
{
  struct Selection
  {
    std::uint32_t command;
    std::uint32_t input;
  };
  for (const auto selection : {
         Selection{0, 0}, Selection{0x800003a0, 0},
         Selection{REINITIALIZE, 0x80},
         Selection{SA4D, 0}, Selection{SA4D, 0x80},
         Selection{SA4D, UINT32_MAX}})
  {
    for (const auto offset : {0u, 4u})
    {
      CAPTURE(selection.command);
      CAPTURE(selection.input);
      CAPTURE(offset);
      NekoSystem system;
      if (selection.command != 0)
      {
        seedContinuation(system);
        if (selection.command != REINITIALIZE)
        {
          REQUIRE(system.eeBus().writeData32(DATA, 0));
        }
        REQUIRE(system.eeBus().writeData32(COMMAND, selection.command));
        REQUIRE(system.eeBus().writeData32(DATA, selection.input));
      }
      system.eeBus().write32(0, offset == 0 ? 0x8c220000 : 0x10000003);
      system.eeBus().write32(4, offset == 0 ? 0 : 0x8c220000);
      system.eeCore().setGeneralRegister(1, {GUEST_DATA, 0});
      system.eeCore().setGeneralRegister(2, {0x12345678, 0xfeedface});
      startEE(system);
      system.startTrace(100);
      const auto result = system.runEE(16);
      requireFault(system, result, GUEST_DATA, offset, EEException::DataBusErrorLoad);
      REQUIRE(system.eeCore().generalRegister(2).low == 0x12345678);
      REQUIRE(system.eeCore().generalRegister(2).high == 0xfeedface);
      const auto memory = accesses(system);
      REQUIRE(memory.size() == 1);
      REQUIRE(memory[0].value1 == 0);
      REQUIRE(memory[0].value3 == nekoPackEEMemoryTraceMetadata({
        DATA, 4, LOAD, NekoEETraceMemory::TranslationOutcome::Translated,
        NekoEETraceMemory::CacheRoute::Uncached, 2, false, true,
        NekoEETraceMemory::CacheAccess::Bypassed,
        NekoEETraceMemory::TransferOutcome::PhysicalBusError,
        NekoEETraceMemory::FailurePhase::Direct
      }));
      REQUIRE(system.eeBus().writeData32(COMMAND, ENUMERATE));
      REQUIRE(system.eeBus().read32(DATA) == 0x1f);
      REQUIRE(system.eeBus().read32(DATA) ==
        (selection.command == 0 ? 0x1fu : 0u));
      REQUIRE(system.eeBus().writeData32(COMMAND, REINITIALIZE));
      REQUIRE(system.eeBus().writeData32(COMMAND, ENUMERATE));
      REQUIRE(system.eeBus().read32(DATA) ==
        ((selection.input & 0x80) == 0 ? 0x1fu : 0u));
    }
  }
}

TEST_CASE("EE RDRAM guest delay-slot word transfers complete once without replay",
  "[ee][guest][trace][rdram-init-conformance]")
{
  struct Access
  {
    std::uint32_t address;
    NekoEETraceMemory::AccessKind kind;
    std::uint32_t source;
    std::uint32_t target;
    unsigned remainingDevices;
    std::uint32_t reinitializedResponse;
  };
  for (const auto access : {
         Access{GUEST_COMMAND, STORE, ENUMERATE, ENUMERATE, 1, 0},
         Access{GUEST_COMMAND, STORE, SA4D, SA4D, 1, 0x1f},
         Access{GUEST_DATA, STORE, 0, 0, 1, 0x1f},
         Access{GUEST_DATA, LOAD, 0x12345678, 0x1f, 0, 0}})
  {
    CAPTURE(access.address);
    CAPTURE(access.kind);
    NekoSystem system;
    seedContinuation(system);
    if (access.kind == STORE)
    {
      REQUIRE(system.eeBus().writeData32(COMMAND, REINITIALIZE));
    }
    if (access.source == SA4D)
    {
      REQUIRE(system.eeBus().writeData32(DATA, 0));
    }
    const auto opcode = access.kind == STORE ? 0x2bu : 0x23u;
    system.eeBus().write32(0, 0x10000003);
    system.eeBus().write32(4, (opcode << 26) | (1u << 21) | (2u << 16));
    system.eeCore().setGeneralRegister(1, {access.address, 0});
    system.eeCore().setGeneralRegister(2, {access.source, 0xfeedface});
    startEE(system);
    system.startTrace(100);
    advanceTo(system, ENTRY + 16);
    REQUIRE(system.eeCore().generalRegister(2).low == access.target);
    REQUIRE(system.eeCore().generalRegister(2).high == 0xfeedface);
    REQUIRE_FALSE(system.traceTruncated());
    const auto memory = accesses(system);
    REQUIRE(memory.size() == 1);
    REQUIRE(memory[0].value1 == (access.kind == STORE ? access.source : access.target));
    REQUIRE(memory[0].value2 == 0);
    REQUIRE(memory[0].value3 == nekoPackEEMemoryTraceMetadata({
      access.address & 0x1fffffff, 4, access.kind,
      NekoEETraceMemory::TranslationOutcome::Translated,
      NekoEETraceMemory::CacheRoute::Uncached, 2, false, true,
      NekoEETraceMemory::CacheAccess::Bypassed,
      NekoEETraceMemory::TransferOutcome::Completed,
      NekoEETraceMemory::FailurePhase::None
    }));
    REQUIRE(system.eeBus().writeData32(COMMAND, ENUMERATE));
    for (unsigned device = 0; device < access.remainingDevices; ++device)
    {
      REQUIRE(system.eeBus().read32(DATA) == 0x1f);
    }
    REQUIRE(system.eeBus().read32(DATA) == 0);
    REQUIRE(system.eeBus().writeData32(COMMAND, REINITIALIZE));
    REQUIRE(system.eeBus().writeData32(COMMAND, ENUMERATE));
    REQUIRE(system.eeBus().read32(DATA) == access.reinitializedResponse);
  }
}
