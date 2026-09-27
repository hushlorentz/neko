#include <cstdint>

#include "catch.hpp"
#include "ee_core.hpp"
#include "ee_instruction.hpp"
#include "neko_system.hpp"

namespace
{
  std::uint32_t cop0TransferInstruction(
    std::uint8_t source,
    std::uint8_t target,
    EECOP0Register cop0Register)
  {
    return
      (UINT32_C(0x10) << 26) |
      (static_cast<std::uint32_t>(source) << 21) |
      (static_cast<std::uint32_t>(target) << 16) |
      (static_cast<std::uint32_t>(cop0Register) << 11);
  }

  std::uint32_t cop0OperationInstruction(
    std::uint8_t function)
  {
    return
      (UINT32_C(0x10) << 26) |
      (UINT32_C(0x10) << 21) |
      function;
  }

  void runInstruction(
    NekoSystem *system,
    std::uint32_t instruction)
  {
    system->eeBus().write32(0, instruction);
    system->eeCore().startExecution(0);
    system->clockMasterCycle();
  }
}

TEST_CASE("EE TLB management operations decode canonically")
{
  const struct
  {
    std::uint8_t function;
    EEOperation operation;
  } contracts[] = {
    {0x01, EEOperation::ReadIndexedTLBEntry},
    {0x02, EEOperation::WriteIndexedTLBEntry},
    {0x06, EEOperation::WriteRandomTLBEntry},
    {0x08, EEOperation::ProbeTLB}
  };

  for (const auto &contract : contracts)
  {
    REQUIRE(
      decodeEEInstruction(
        cop0OperationInstruction(contract.function)).operation ==
      contract.operation);
    REQUIRE_THROWS_WITH(
      decodeEEInstruction(
        cop0OperationInstruction(contract.function) |
        (UINT32_C(1) << 6)),
      "Reserved EE instruction encoding.");
  }
}

TEST_CASE("EE TLB indexed writes and reads use canonical entries")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  const std::uint32_t pageMask = EECOP0PageMask::SIZE_16_KIB;
  const std::uint32_t entryHi = UINT32_C(0x12347e5a);
  const std::uint32_t entryLo0 = UINT32_C(0x801234ff);
  const std::uint32_t entryLo1 = UINT32_C(0x001abcff);
  core.setCOP0Register(EECOP0Register::Index, 5);
  core.setCOP0Register(EECOP0Register::PageMask, pageMask);
  core.setCOP0Register(EECOP0Register::EntryHi, entryHi);
  core.setCOP0Register(EECOP0Register::EntryLo0, entryLo0);
  core.setCOP0Register(EECOP0Register::EntryLo1, entryLo1);

  runInstruction(
    &system,
    cop0OperationInstruction(0x02));

  const EETLBEntry &entry = core.tlbEntry(5);
  REQUIRE(entry.pageMask == pageMask);
  REQUIRE(
    entry.entryHi ==
    (entryHi & EECOP0EntryHi::IMPLEMENTED_MASK & ~pageMask));
  REQUIRE(
    entry.evenPage.value ==
    (entryLo0 &
     EECOP0EntryLo::ENTRY_LO_0_IMPLEMENTED_MASK &
     ~(pageMask >> 7)));
  REQUIRE(
    entry.oddPage.value ==
    (entryLo1 &
     EECOP0EntryLo::ENTRY_LO_1_IMPLEMENTED_MASK &
     ~(pageMask >> 7)));
  REQUIRE(entry.global());
  REQUIRE(entry.evenPage.scratchpad());
  REQUIRE_FALSE(entry.oddPage.scratchpad());

  core.setCOP0Register(EECOP0Register::PageMask, 0);
  core.setCOP0Register(EECOP0Register::EntryHi, 0);
  core.setCOP0Register(EECOP0Register::EntryLo0, 0);
  core.setCOP0Register(EECOP0Register::EntryLo1, 0);
  runInstruction(
    &system,
    cop0OperationInstruction(0x01));

  REQUIRE(
    core.cop0Register(EECOP0Register::PageMask) ==
    entry.pageMask);
  REQUIRE(
    core.cop0Register(EECOP0Register::EntryHi) ==
    entry.entryHi);
  REQUIRE(
    core.cop0Register(EECOP0Register::EntryLo0) ==
    entry.evenPage.value);
  REQUIRE(
    core.cop0Register(EECOP0Register::EntryLo1) ==
    entry.oddPage.value);
}

TEST_CASE("EE TLB entries select pages using their page mask")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(EECOP0Register::Index, 3);
  core.setCOP0Register(
    EECOP0Register::PageMask,
    EECOP0PageMask::SIZE_16_KIB);
  core.setCOP0Register(
    EECOP0Register::EntryHi,
    UINT32_C(0x1234002a));
  core.setCOP0Register(
    EECOP0Register::EntryLo0,
    UINT32_C(0x80010007));
  core.setCOP0Register(
    EECOP0Register::EntryLo1,
    UINT32_C(0x00020007));
  runInstruction(
    &system,
    cop0OperationInstruction(0x02));

  const EETLBEntry &entry = core.tlbEntry(3);
  REQUIRE(entry.matches(UINT32_C(0x12340000), 0xff));
  REQUIRE(
    &entry.pageForAddress(UINT32_C(0x12340000)) ==
    &entry.evenPage);
  REQUIRE(
    &entry.pageForAddress(UINT32_C(0x12344000)) ==
    &entry.oddPage);
  REQUIRE(
    entry.pageForAddress(UINT32_C(0x12340000)).
      scratchpad());
  REQUIRE_FALSE(
    entry.pageForAddress(UINT32_C(0x12344000)).
      scratchpad());

  const struct
  {
    std::uint32_t pageMask;
    std::uint32_t oddPageBit;
  } pageSizes[] = {
    {EECOP0PageMask::SIZE_4_KIB, UINT32_C(0x00001000)},
    {EECOP0PageMask::SIZE_16_KIB, UINT32_C(0x00004000)},
    {EECOP0PageMask::SIZE_64_KIB, UINT32_C(0x00010000)},
    {EECOP0PageMask::SIZE_256_KIB, UINT32_C(0x00040000)},
    {EECOP0PageMask::SIZE_1_MIB, UINT32_C(0x00100000)},
    {EECOP0PageMask::SIZE_4_MIB, UINT32_C(0x00400000)},
    {EECOP0PageMask::SIZE_16_MIB, UINT32_C(0x01000000)}
  };
  for (const auto &pageSize : pageSizes)
  {
    const EETLBEntry sizedEntry = {
      pageSize.pageMask,
      0,
      {1},
      {2}
    };
    REQUIRE(&sizedEntry.pageForAddress(0) == &sizedEntry.evenPage);
    REQUIRE(
      &sizedEntry.pageForAddress(pageSize.oddPageBit) ==
      &sizedEntry.oddPage);
  }
}

TEST_CASE("EE TLB probes honor ASIDs and combined global state")
{
  NekoSystem system;
  EECore &core = system.eeCore();

  const auto writeEntry =
    [&system, &core](
      std::uint32_t index,
      std::uint32_t asid,
      bool global)
    {
      core.setCOP0Register(EECOP0Register::Index, index);
      core.setCOP0Register(EECOP0Register::PageMask, 0);
      core.setCOP0Register(
        EECOP0Register::EntryHi,
        UINT32_C(0x23456000) | asid);
      core.setCOP0Register(
        EECOP0Register::EntryLo0,
        UINT32_C(0x00010006) |
        static_cast<std::uint32_t>(global));
      core.setCOP0Register(
        EECOP0Register::EntryLo1,
        UINT32_C(0x00020006) |
        static_cast<std::uint32_t>(global));
      runInstruction(
        &system,
        cop0OperationInstruction(0x02));
    };

  writeEntry(7, 0x2a, false);
  core.setCOP0Register(
    EECOP0Register::EntryHi,
    UINT32_C(0x2345602b));
  runInstruction(
    &system,
    cop0OperationInstruction(0x08));
  REQUIRE(
    core.cop0Register(EECOP0Register::Index) ==
    EECOP0Index::PROBE_FAILURE);

  writeEntry(7, 0x2a, true);
  writeEntry(2, 0x3c, true);
  core.setCOP0Register(
    EECOP0Register::EntryHi,
    UINT32_C(0x234560ff));
  runInstruction(
    &system,
    cop0OperationInstruction(0x08));
  REQUIRE(core.cop0Register(EECOP0Register::Index) == 2);
}

TEST_CASE("EE TLB writes use indexed and random selectors")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(EECOP0Register::Index, 63);
  core.setCOP0Register(EECOP0Register::PageMask, 0);
  core.setCOP0Register(
    EECOP0Register::EntryHi,
    UINT32_C(0x11110001));
  core.setCOP0Register(
    EECOP0Register::EntryLo0,
    UINT32_C(0x00010006));
  core.setCOP0Register(
    EECOP0Register::EntryLo1,
    UINT32_C(0x00020006));
  runInstruction(
    &system,
    cop0OperationInstruction(0x02));
  REQUIRE(core.tlbEntry(47) == EETLBEntry{});

  core.setCOP0Register(EECOP0Register::Random, 11);
  runInstruction(
    &system,
    cop0OperationInstruction(0x06));
  REQUIRE(
    core.tlbEntry(11).entryHi ==
    UINT32_C(0x11110001));
  REQUIRE(core.cop0Register(EECOP0Register::Random) == 10);

  core.setCOP0Register(EECOP0Register::Index, 63);
  core.setCOP0Register(
    EECOP0Register::EntryHi,
    UINT32_C(0x22222002));
  runInstruction(
    &system,
    cop0OperationInstruction(0x01));
  REQUIRE(
    core.cop0Register(EECOP0Register::EntryHi) ==
    UINT32_C(0x22222002));
}

TEST_CASE("EE TLB operations require COP0 usability")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(
    EECOP0Register::Status,
    EECOP0Status::USER_MODE);
  core.setCOP0Register(EECOP0Register::Index, 4);
  core.setCOP0Register(
    EECOP0Register::EntryHi,
    UINT32_C(0x12345001));

  runInstruction(
    &system,
    cop0OperationInstruction(0x02));

  REQUIRE(
    core.pendingException() ==
    EEException::CoprocessorUnusable);
  REQUIRE(core.tlbEntry(4) == EETLBEntry{});
}

TEST_CASE("EE TLB entries reset deterministically and affect hashes")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  const std::uint64_t resetHash = core.stateHash();

  for (std::size_t index = 0;
       index < EEMemorySystem::TLB_ENTRY_COUNT;
       ++index)
  {
    REQUIRE(core.tlbEntry(index) == EETLBEntry{});
  }

  core.setTLBEntry(
    17,
    {
      EECOP0PageMask::SIZE_4_KIB,
      UINT32_C(0x1234402a),
      {UINT32_C(0x00010007)},
      {UINT32_C(0x00020007)}
    });
  REQUIRE(core.stateHash() != resetHash);

  core.reset();
  REQUIRE(core.stateHash() == resetHash);
}

TEST_CASE("EE COP0 register transfers decode canonically")
{
  REQUIRE(
    decodeEEInstruction(
      cop0TransferInstruction(
        0x00,
        2,
        EECOP0Register::EntryHi)).operation ==
    EEOperation::MoveWordFromCOP0);
  REQUIRE(
    decodeEEInstruction(
      cop0TransferInstruction(
        0x04,
        2,
        EECOP0Register::EntryHi)).operation ==
    EEOperation::MoveWordToCOP0);

  REQUIRE_THROWS_WITH(
    decodeEEInstruction(
      cop0TransferInstruction(
        0x00,
        2,
        EECOP0Register::EntryHi) | 1),
    "Reserved EE instruction encoding.");
  REQUIRE_THROWS_WITH(
    decodeEEInstruction(
      cop0TransferInstruction(
        0x04,
        2,
        EECOP0Register::EntryHi) | 1),
    "Reserved EE instruction encoding.");
}

TEST_CASE("EE MFC0 sign extends the COP0 low word")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(
    EECOP0Register::TagHi,
    UINT32_C(0x80000001));
  core.setGeneralRegister(
    2,
    {UINT64_C(0x1111111122222222),
     UINT64_C(0x3333333344444444)});

  runInstruction(
    &system,
    cop0TransferInstruction(
      0x00,
      2,
      EECOP0Register::TagHi));

  REQUIRE(
    core.generalRegister(2) ==
    EERegister128{
      UINT64_C(0xffffffff80000001),
      UINT64_C(0x3333333344444444)});
}

TEST_CASE("EE MTC0 consumes only the general-register low word")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setGeneralRegister(
    2,
    {UINT64_C(0xabcdef0112345678), UINT64_MAX});

  runInstruction(
    &system,
    cop0TransferInstruction(
      0x04,
      2,
      EECOP0Register::TagHi));

  REQUIRE(
    core.cop0Register(EECOP0Register::TagHi) ==
    UINT32_C(0x12345678));
}

TEST_CASE("EE COP0 transfers honor general register zero")
{
  SECTION("MFC0 suppresses a write to general register zero")
  {
    NekoSystem system;
    system.eeCore().setCOP0Register(
      EECOP0Register::TagHi,
      UINT32_MAX);

    runInstruction(
      &system,
      cop0TransferInstruction(
        0x00,
        0,
        EECOP0Register::TagHi));

    REQUIRE(system.eeCore().generalRegister(0) == EERegister128{});
  }

  SECTION("MTC0 reads zero from general register zero")
  {
    NekoSystem system;
    system.eeCore().setCOP0Register(
      EECOP0Register::EntryHi,
      UINT32_MAX);

    runInstruction(
      &system,
      cop0TransferInstruction(
        0x04,
        0,
        EECOP0Register::EntryHi));

    REQUIRE(
      system.eeCore().cop0Register(EECOP0Register::EntryHi) ==
      0);
  }
}

TEST_CASE("EE MTC0 applies register-specific write policies")
{
  SECTION("Index preserves probe state")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::Index,
      EECOP0Index::PROBE_FAILURE);
    core.setGeneralRegister(2, {42, 0});

    runInstruction(
      &system,
      cop0TransferInstruction(
        0x04,
        2,
        EECOP0Register::Index));

    REQUIRE(
      core.cop0Register(EECOP0Register::Index) ==
      (EECOP0Index::PROBE_FAILURE | 42));
  }

  SECTION("Context preserves exception-owned BadVPN2")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::Context,
      UINT32_C(0x00543210));
    core.setGeneralRegister(2, {UINT32_MAX, 0});

    runInstruction(
      &system,
      cop0TransferInstruction(
        0x04,
        2,
        EECOP0Register::Context));

    REQUIRE(
      core.cop0Register(EECOP0Register::Context) ==
      UINT32_C(0xffd43210));
  }

  SECTION("Status preserves cache-hit state")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::CACHE_HIT);
    core.setGeneralRegister(2, {UINT32_MAX, 0});

    runInstruction(
      &system,
      cop0TransferInstruction(
        0x04,
        2,
        EECOP0Register::Status));

    REQUIRE(
      core.cop0Register(EECOP0Register::Status) ==
      EECOP0Status::IMPLEMENTED_MASK);
  }

  SECTION("Read-only registers ignore guest writes")
  {
    SECTION("Random follows only normal retirement sequencing")
    {
      NekoSystem system;
      EECore &core = system.eeCore();
      core.setCOP0Register(EECOP0Register::Random, 17);
      core.setGeneralRegister(2, {0, 0});
      runInstruction(
        &system,
        cop0TransferInstruction(
          0x04,
          2,
          EECOP0Register::Random));
      REQUIRE(core.cop0Register(EECOP0Register::Random) == 16);
    }

    SECTION("BadVAddr and Cause remain unchanged")
    {
      NekoSystem system;
      EECore &core = system.eeCore();
      core.setCOP0Register(
        EECOP0Register::BadVAddr,
        UINT32_C(0x12345678));
      core.setCOP0Register(
        EECOP0Register::Cause,
        UINT32_C(0x80008030));
      core.setGeneralRegister(2, {0, 0});

      runInstruction(
        &system,
        cop0TransferInstruction(
          0x04,
          2,
          EECOP0Register::BadVAddr));
      runInstruction(
        &system,
        cop0TransferInstruction(
          0x04,
          2,
          EECOP0Register::Cause));

      REQUIRE(
        core.cop0Register(EECOP0Register::BadVAddr) ==
        UINT32_C(0x12345678));
      REQUIRE(
        core.cop0Register(EECOP0Register::Cause) ==
        UINT32_C(0x80008030));
    }
  }
}

TEST_CASE("EE MTC0 Wired writes reset Random atomically")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(EECOP0Register::Wired, 3);
  core.setCOP0Register(EECOP0Register::Random, 17);
  core.setGeneralRegister(2, {7, 0});

  runInstruction(
    &system,
    cop0TransferInstruction(
      0x04,
      2,
      EECOP0Register::Wired));

  REQUIRE(core.cop0Register(EECOP0Register::Wired) == 7);
  REQUIRE(
    core.cop0Register(EECOP0Register::Random) ==
    EECOP0Random::RESET - 1);
}

TEST_CASE("EE Random advances through the Wired replacement range")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(EECOP0Register::Wired, 5);
  core.setCOP0Register(EECOP0Register::Random, 6);

  runInstruction(&system, 0);
  REQUIRE(core.cop0Register(EECOP0Register::Random) == 5);
  runInstruction(&system, 0);
  REQUIRE(
    core.cop0Register(EECOP0Register::Random) ==
    EECOP0Random::RESET);
  runInstruction(&system, 0);
  REQUIRE(
    core.cop0Register(EECOP0Register::Random) ==
    EECOP0Random::RESET - 1);
}

TEST_CASE("EE MTC0 rejects unsupported values without mutation")
{
  SECTION("Wired and Random remain unchanged")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(EECOP0Register::Wired, 3);
    core.setCOP0Register(EECOP0Register::Random, 17);
    core.setGeneralRegister(2, {48, 0});
    const std::uint32_t instruction =
      cop0TransferInstruction(
        0x04,
        2,
        EECOP0Register::Wired);

    runInstruction(&system, instruction);

    REQUIRE(core.cop0Register(EECOP0Register::Wired) == 3);
    REQUIRE(core.cop0Register(EECOP0Register::Random) == 17);
    REQUIRE(core.executionState() == EEExecutionState::Halted);
    REQUIRE(core.stopReason() == EEStopReason::UndefinedOperation);
    REQUIRE(core.rejectedInstruction() == instruction);
  }

  SECTION("PageMask remains unchanged after reserved-bit removal")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::PageMask,
      EECOP0PageMask::SIZE_16_KIB);
    core.setGeneralRegister(
      2,
      {UINT32_C(0x80002000), 0});
    const std::uint32_t instruction =
      cop0TransferInstruction(
        0x04,
        2,
        EECOP0Register::PageMask);

    runInstruction(&system, instruction);

    REQUIRE(
      core.cop0Register(EECOP0Register::PageMask) ==
      EECOP0PageMask::SIZE_16_KIB);
    REQUIRE(core.executionState() == EEExecutionState::Halted);
    REQUIRE(core.stopReason() == EEStopReason::UndefinedOperation);
    REQUIRE(core.rejectedInstruction() == instruction);
  }
}

TEST_CASE("EE COP0 transfers require kernel mode or CU0")
{
  SECTION("User mode without CU0 raises Coprocessor Unusable")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::USER_MODE);
    core.setCOP0Register(
      EECOP0Register::TagHi,
      UINT32_C(0x12345678));
    core.setGeneralRegister(2, {UINT64_MAX, UINT64_MAX});

    runInstruction(
      &system,
      cop0TransferInstruction(
        0x00,
        2,
        EECOP0Register::TagHi));

    REQUIRE(
      core.pendingException() ==
      EEException::CoprocessorUnusable);
    REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
    REQUIRE(
      (core.cop0Register(EECOP0Register::Cause) &
       EECOP0Cause::COPROCESSOR_ERROR_MASK) == 0);
    REQUIRE(
      core.generalRegister(2) ==
      EERegister128{UINT64_MAX, UINT64_MAX});
  }

  SECTION("CU0 permits a user-mode transfer")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::USER_MODE |
      EECOP0Status::COP0_USABLE);
    core.setCOP0Register(
      EECOP0Register::TagHi,
      UINT32_C(0x12345678));

    runInstruction(
      &system,
      cop0TransferInstruction(
        0x00,
        2,
        EECOP0Register::TagHi));

    REQUIRE_FALSE(core.exceptionPending());
    REQUIRE(
      core.generalRegister(2).low ==
      UINT64_C(0x0000000012345678));
  }
}

TEST_CASE("EE COP0 registers have deterministic reset state")
{
  NekoSystem system;
  const EECore &core = system.eeCore();

  REQUIRE(core.cop0Register(EECOP0Register::Index) == 0);
  REQUIRE(
    core.cop0Register(EECOP0Register::Random) ==
    EECOP0Random::RESET);
  REQUIRE(core.cop0Register(EECOP0Register::EntryLo0) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::EntryLo1) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::Context) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::PageMask) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::Wired) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::EntryHi) == 0);
  REQUIRE(
    core.cop0Register(EECOP0Register::Status) ==
    EECOP0Status::RESET);
  REQUIRE(core.cop0Register(EECOP0Register::Cause) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
  REQUIRE(
    core.cop0Register(EECOP0Register::Config) ==
    EECOP0Config::RESET);
  REQUIRE(core.cop0Register(EECOP0Register::TagLo) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::TagHi) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::ErrorEPC) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::BadVAddr) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::Count) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::Compare) == 0);
  REQUIRE(core.programCounter() == EEReset::VECTOR);
  REQUIRE(core.executionState() == EEExecutionState::Halted);
}

TEST_CASE("EE COP0 register state can be inspected and restored")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  const struct
  {
    EECOP0Register registerIndex;
    std::uint32_t value;
  } contracts[] = {
    {EECOP0Register::BadVAddr, UINT32_C(0x81234567)},
    {EECOP0Register::Count, UINT32_C(0x12345678)},
    {EECOP0Register::Compare, UINT32_C(0x87654321)},
    {EECOP0Register::Status, UINT32_C(0xf0c79c1f)},
    {EECOP0Register::Cause, UINT32_C(0x80008030)},
    {EECOP0Register::EPC, UINT32_C(0x80001000)},
    {EECOP0Register::ErrorEPC, UINT32_C(0xbfc00000)}
  };

  for (const auto &contract : contracts)
  {
    core.setCOP0Register(
      contract.registerIndex,
      contract.value);
    REQUIRE(
      core.cop0Register(contract.registerIndex) ==
      contract.value);
  }

  core.reset();

  REQUIRE(
    core.cop0Register(EECOP0Register::Status) ==
    EECOP0Status::RESET);
  REQUIRE(core.cop0Register(EECOP0Register::Cause) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::ErrorEPC) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::BadVAddr) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::Count) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::Compare) == 0);
  REQUIRE(core.programCounter() == EEReset::VECTOR);
}

TEST_CASE("EE COP0 translation and cache registers canonicalize state")
{
  NekoSystem system;
  EECore &core = system.eeCore();

  core.setCOP0Register(EECOP0Register::Index, UINT32_MAX);
  core.setCOP0Register(EECOP0Register::Random, 17);
  core.setCOP0Register(EECOP0Register::EntryLo0, UINT32_MAX);
  core.setCOP0Register(EECOP0Register::EntryLo1, UINT32_MAX);
  core.setCOP0Register(EECOP0Register::Context, UINT32_MAX);
  core.setCOP0Register(
    EECOP0Register::PageMask,
    UINT32_C(0x80000000) | EECOP0PageMask::SIZE_16_KIB);
  core.setCOP0Register(EECOP0Register::Wired, 47);
  core.setCOP0Register(EECOP0Register::EntryHi, UINT32_MAX);
  core.setCOP0Register(EECOP0Register::Config, UINT32_MAX);
  core.setCOP0Register(EECOP0Register::TagLo, UINT32_MAX);
  core.setCOP0Register(EECOP0Register::TagHi, UINT32_MAX);

  REQUIRE(
    core.cop0Register(EECOP0Register::Index) ==
    EECOP0Index::IMPLEMENTED_MASK);
  REQUIRE(core.cop0Register(EECOP0Register::Random) == 17);
  REQUIRE(
    core.cop0Register(EECOP0Register::EntryLo0) ==
    EECOP0EntryLo::ENTRY_LO_0_IMPLEMENTED_MASK);
  REQUIRE(
    core.cop0Register(EECOP0Register::EntryLo1) ==
    EECOP0EntryLo::ENTRY_LO_1_IMPLEMENTED_MASK);
  REQUIRE(
    core.cop0Register(EECOP0Register::Context) ==
    EECOP0Context::IMPLEMENTED_MASK);
  REQUIRE(
    core.cop0Register(EECOP0Register::PageMask) ==
    EECOP0PageMask::SIZE_16_KIB);
  REQUIRE(core.cop0Register(EECOP0Register::Wired) == 47);
  REQUIRE(
    core.cop0Register(EECOP0Register::EntryHi) ==
    EECOP0EntryHi::IMPLEMENTED_MASK);
  REQUIRE(
    core.cop0Register(EECOP0Register::Config) ==
    (EECOP0Config::WRITABLE_MASK | EECOP0Config::FIXED));
  REQUIRE(
    core.cop0Register(EECOP0Register::TagLo) ==
    EECOP0TagLo::IMPLEMENTED_MASK);
  REQUIRE(
    core.cop0Register(EECOP0Register::TagHi) ==
    UINT32_MAX);

  REQUIRE_THROWS_WITH(
    core.setCOP0Register(EECOP0Register::Random, 48),
    "EE COP0 Random index is outside the 48-entry TLB.");
  REQUIRE_THROWS_WITH(
    core.setCOP0Register(EECOP0Register::Wired, 48),
    "EE COP0 Wired index is outside the 48-entry TLB.");
  REQUIRE_THROWS_WITH(
    core.setCOP0Register(
      EECOP0Register::PageMask,
      UINT32_C(0x00002000)),
    "EE COP0 PageMask encoding is unsupported.");
}

TEST_CASE("EE COP0 translation and cache registers affect state hashes")
{
  const struct
  {
    EECOP0Register registerIndex;
    std::uint32_t value;
  } contracts[] = {
    {EECOP0Register::Index, 1},
    {EECOP0Register::Random, 46},
    {EECOP0Register::EntryLo0, 1},
    {EECOP0Register::EntryLo1, 1},
    {EECOP0Register::Context, UINT32_C(0x10)},
    {EECOP0Register::PageMask, EECOP0PageMask::SIZE_16_KIB},
    {EECOP0Register::Wired, 1},
    {EECOP0Register::EntryHi, 1},
    {EECOP0Register::Config, EECOP0Config::DATA_CACHE_ENABLE},
    {EECOP0Register::TagLo, EECOP0TagLo::VALID},
    {EECOP0Register::TagHi, 1}
  };

  for (const auto &contract : contracts)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    const std::uint64_t resetHash = core.stateHash();

    core.setCOP0Register(
      contract.registerIndex,
      contract.value);

    REQUIRE(core.stateHash() != resetHash);
    core.reset();
    REQUIRE(core.stateHash() == resetHash);
  }
}

TEST_CASE("Neko system reset returns the EE to architectural Reset entry")
{
  NekoSystem system;
  system.eeCore().setProgramCounter(0x80001000);
  system.eeCore().setCOP0Register(EECOP0Register::Status, 0);
  system.eeCore().setCOP0Register(
    EECOP0Register::ErrorEPC,
    UINT32_MAX);
  system.eeCore().startExecution(0);
  system.clockMasterCycle();

  system.reset();

  REQUIRE(system.eeCore().programCounter() == EEReset::VECTOR);
  REQUIRE(
    system.eeCore().cop0Register(EECOP0Register::Status) ==
    EECOP0Status::RESET);
  REQUIRE(
    system.eeCore().cop0Register(EECOP0Register::ErrorEPC) == 0);
  REQUIRE(
    system.eeCore().executionState() ==
    EEExecutionState::Halted);
  REQUIRE(system.eeCore().stopReason() == EEStopReason::None);
  REQUIRE(system.eeCore().elapsedCycles() == 0);
}

TEST_CASE("EE Reset fetch faults enter the bootstrap exception vector")
{
  NekoSystem system;
  system.eeCore().startExecution(
    system.eeCore().programCounter());

  system.clockMasterCycle();

  REQUIRE(
    system.eeCore().programCounter() ==
    EEExceptionVector::BOOTSTRAP_GENERAL);
  REQUIRE(
    system.eeCore().executionState() ==
    EEExecutionState::Running);
  REQUIRE(system.eeCore().stopReason() == EEStopReason::None);
  REQUIRE(
    system.eeCore().pendingException() ==
    EEException::InstructionBusError);
  REQUIRE(
    system.eeCore().exceptionAddress() ==
    EEReset::VECTOR);
  REQUIRE(
    system.eeCore().cop0Register(EECOP0Register::EPC) ==
    EEReset::VECTOR);
  REQUIRE_FALSE(system.eeCore().hasLastInstruction());
}

TEST_CASE("EE COP0 rejects unimplemented register identifiers")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  const EECOP0Register unsupported =
    static_cast<EECOP0Register>(7);

  REQUIRE_THROWS_WITH(
    core.cop0Register(unsupported),
    "EE COP0 register is not implemented.");
  REQUIRE_THROWS_WITH(
    core.setCOP0Register(unsupported, 0),
    "EE COP0 register is not implemented.");
}
