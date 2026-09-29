#include <cstdint>

#include "catch.hpp"
#include "ee_test_utils.hpp"
#include "neko_system.hpp"

namespace
{
  std::uint32_t exceptionCode(const EECore &core)
  {
    return
      (core.cop0Register(EECOP0Register::Cause) &
        EECOP0Cause::EXCEPTION_CODE_MASK) >> 2;
  }

  std::uint32_t registerInstruction(
    std::uint8_t function,
    std::uint8_t source = 0,
    std::uint8_t target = 0,
    std::uint8_t destination = 0)
  {
    return
      (static_cast<std::uint32_t>(source) << 21) |
      (static_cast<std::uint32_t>(target) << 16) |
      (static_cast<std::uint32_t>(destination) << 11) |
      function;
  }

  std::uint32_t immediateInstruction(
    std::uint8_t opcode,
    std::uint8_t source,
    std::uint8_t target,
    std::uint16_t immediate)
  {
    return
      (static_cast<std::uint32_t>(opcode) << 26) |
      (static_cast<std::uint32_t>(source) << 21) |
      (static_cast<std::uint32_t>(target) << 16) |
      immediate;
  }
}

TEST_CASE("EE exceptions enter the general vector through COP0")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(EECOP0Register::Status, 0);
  mapLowKusegForTest(&core);
  core.setCOP0Register(
    EECOP0Register::Cause,
    UINT32_C(0xc000ff7c));
  core.setGeneralRegister(1, {0x7fffffff, 0});
  core.setGeneralRegister(2, {1, 0});
  system.eeBus().write32(
    0x100,
    registerInstruction(0x20, 1, 2, 3));
  core.startExecution(0x100);

  const EEExecutionResult entry = system.runEE(10);

  REQUIRE(entry.masterCycles == 1);
  REQUIRE_FALSE(entry.cycleLimitReached);
  REQUIRE(core.executionState() == EEExecutionState::Running);
  REQUIRE(core.stopReason() == EEStopReason::None);
  REQUIRE(core.programCounter() == EEExceptionVector::GENERAL);
  REQUIRE(
    (core.cop0Register(EECOP0Register::Status) &
      EECOP0Status::EXCEPTION_LEVEL) != 0);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0x100);
  REQUIRE(
    exceptionCode(core) ==
    EEExceptionCode::ARITHMETIC_OVERFLOW);
  REQUIRE(
    (core.cop0Register(EECOP0Register::Cause) &
      ~EECOP0Cause::EXCEPTION_CODE_MASK &
      ~EECOP0Cause::BRANCH_DELAY &
      ~EECOP0Cause::INTC_PENDING &
      ~EECOP0Cause::DMAC_PENDING) ==
    (UINT32_C(0xc000ff7c) &
      ~EECOP0Cause::EXCEPTION_CODE_MASK &
      ~EECOP0Cause::BRANCH_DELAY &
      ~EECOP0Cause::INTC_PENDING &
      ~EECOP0Cause::DMAC_PENDING));
  REQUIRE(core.pendingException() == EEException::ArithmeticOverflow);
  REQUIRE_FALSE(core.hasLastInstruction());

  system.eeBus().write32(EEExceptionVector::GENERAL, 0);
  const EEExecutionResult handler = system.stepEEInstruction(1);

  REQUIRE(handler.instructions == 1);
  REQUIRE(core.hasLastInstruction());
  REQUIRE(
    core.lastInstructionAddress() ==
    EEExceptionVector::GENERAL);
}

TEST_CASE("EE exception entry replaces coprocessor attribution")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(EECOP0Register::Status, 0);
  mapLowKusegForTest(&core);
  system.eeBus().write32(
    0,
    (UINT32_C(0x11) << 26) |
      (UINT32_C(1) << 16));
  core.startExecution(0);

  system.clockMasterCycle();

  REQUIRE(
    core.pendingException() ==
    EEException::CoprocessorUnusable);
  REQUIRE(
    (core.cop0Register(EECOP0Register::Cause) &
      EECOP0Cause::COPROCESSOR_ERROR_MASK) ==
    EECOP0Cause::COPROCESSOR_1);

  core.setCOP0Register(EECOP0Register::Status, 0);
  mapLowKusegForTest(&core);
  core.setProgramCounter(0x100);
  core.enterInterruptException();

  REQUIRE(core.pendingException() == EEException::Interrupt);
  REQUIRE(
    (core.cop0Register(EECOP0Register::Cause) &
      EECOP0Cause::COPROCESSOR_ERROR_MASK) == 0);
}

TEST_CASE("EE bootstrap and interrupt vectors follow Status BEV")
{
  SECTION("A general exception uses the bootstrap vector")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::BOOTSTRAP_EXCEPTION_VECTOR);
    mapLowKusegForTest(&core);
    system.eeBus().write32(0, UINT32_C(0x0000000c));
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE(
      core.programCounter() ==
      EEExceptionVector::BOOTSTRAP_GENERAL);
  }

  SECTION("Interrupt entry selects the dedicated vector")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(EECOP0Register::Status, 0);
    mapLowKusegForTest(&core);
    core.setProgramCounter(0x80001000);

    core.enterInterruptException();

    REQUIRE(core.programCounter() == EEExceptionVector::INTERRUPT);
    REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0x80001000);
    REQUIRE(exceptionCode(core) == EEExceptionCode::INTERRUPT);

    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::BOOTSTRAP_EXCEPTION_VECTOR);
    mapLowKusegForTest(&core);
    core.setProgramCounter(0x80002000);
    core.enterInterruptException();

    REQUIRE(
      core.programCounter() ==
      EEExceptionVector::BOOTSTRAP_INTERRUPT);
  }
}

TEST_CASE("EE TLB exceptions select refill and general vectors")
{
  constexpr std::uint32_t instructionAddress =
    EEMemoryMap::KSEG0_BASE;
  constexpr std::uint32_t dataAddress = UINT32_C(0x00400000);

  SECTION("A first-level fetch no-match uses the normal refill vector")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::USER_MODE);
    core.startExecution(dataAddress);

    system.clockMasterCycle();

    REQUIRE(
      core.pendingException() ==
      EEException::TLBRefillLoadOrFetch);
    REQUIRE(core.programCounter() == EEExceptionVector::REFILL);
    REQUIRE(core.cop0Register(EECOP0Register::EPC) == dataAddress);
  }

  SECTION("A first-level store no-match uses the bootstrap refill vector")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::BOOTSTRAP_EXCEPTION_VECTOR);
    core.setGeneralRegister(1, {dataAddress, 0});
    system.eeBus().write32(
      0,
      immediateInstruction(0x2b, 1, 0, 0));
    core.startExecution(instructionAddress);

    system.clockMasterCycle();

    REQUIRE(core.pendingException() == EEException::TLBRefillStore);
    REQUIRE(
      core.programCounter() ==
      EEExceptionVector::BOOTSTRAP_REFILL);
    REQUIRE(
      core.cop0Register(EECOP0Register::EPC) ==
      instructionAddress);
  }

  SECTION("A first-level invalid match uses the normal general vector")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::USER_MODE);
    core.setTLBEntry(
      0,
      {
        EECOP0PageMask::SIZE_4_KIB,
        dataAddress,
        {},
        {}
      });
    core.startExecution(dataAddress);

    system.clockMasterCycle();

    REQUIRE(
      core.pendingException() ==
      EEException::TLBInvalidLoadOrFetch);
    REQUIRE(core.programCounter() == EEExceptionVector::GENERAL);
  }

  SECTION("A first-level modified match uses the bootstrap general vector")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::BOOTSTRAP_EXCEPTION_VECTOR);
    core.setTLBEntry(
      0,
      {
        EECOP0PageMask::SIZE_4_KIB,
        dataAddress,
        {UINT32_C(0x0001001a)},
        {}
      });
    core.setGeneralRegister(1, {dataAddress, 0});
    system.eeBus().write32(
      0,
      immediateInstruction(0x2b, 1, 0, 0));
    core.startExecution(instructionAddress);

    system.clockMasterCycle();

    REQUIRE(core.pendingException() == EEException::TLBModified);
    REQUIRE(
      core.programCounter() ==
      EEExceptionVector::BOOTSTRAP_GENERAL);
  }
}

TEST_CASE("Nested EE exceptions preserve EPC and use the general vector")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(
    EECOP0Register::Status,
    EECOP0Status::EXCEPTION_LEVEL);
  mapLowKusegForTest(&core);
  core.setCOP0Register(EECOP0Register::EPC, 0x80001234);
  core.setCOP0Register(
    EECOP0Register::Cause,
    EECOP0Cause::BRANCH_DELAY);
  core.setProgramCounter(0x80002000);

  core.enterInterruptException();

  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0x80001234);
  REQUIRE(
    (core.cop0Register(EECOP0Register::Cause) &
      EECOP0Cause::BRANCH_DELAY) != 0);
  REQUIRE(core.programCounter() == EEExceptionVector::GENERAL);
  REQUIRE(exceptionCode(core) == EEExceptionCode::INTERRUPT);
}

TEST_CASE("Nested EE TLB refill uses the general vector and newest fault state")
{
  constexpr std::uint32_t faultAddress = UINT32_C(0x12345abc);
  constexpr std::uint32_t preservedEPC = UINT32_C(0x80001234);
  constexpr std::uint32_t initialContext = UINT32_C(0xabd23450);
  constexpr std::uint32_t initialEntryHi = UINT32_C(0x89abc05a);
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(
    EECOP0Register::Status,
    EECOP0Status::EXCEPTION_LEVEL |
      EECOP0Status::BOOTSTRAP_EXCEPTION_VECTOR);
  core.setCOP0Register(EECOP0Register::EPC, preservedEPC);
  core.setCOP0Register(
    EECOP0Register::Cause,
    EECOP0Cause::BRANCH_DELAY);
  core.setCOP0Register(
    EECOP0Register::Context,
    initialContext);
  core.setCOP0Register(
    EECOP0Register::EntryHi,
    initialEntryHi);
  core.startExecution(faultAddress);

  system.clockMasterCycle();

  REQUIRE(
    core.pendingException() ==
    EEException::TLBRefillLoadOrFetch);
  REQUIRE(
    core.programCounter() ==
    EEExceptionVector::BOOTSTRAP_GENERAL);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == preservedEPC);
  REQUIRE(
    (core.cop0Register(EECOP0Register::Cause) &
      EECOP0Cause::BRANCH_DELAY) != 0);
  REQUIRE(
    exceptionCode(core) ==
    EEExceptionCode::TLB_LOAD_OR_FETCH);
  REQUIRE(
    core.cop0Register(EECOP0Register::BadVAddr) ==
    faultAddress);
  REQUIRE(
    core.cop0Register(EECOP0Register::Context) ==
    ((initialContext & EECOP0Context::PTE_BASE_MASK) |
     ((faultAddress >> 9) & EECOP0Context::BAD_VPN2_MASK)));
  REQUIRE(
    core.cop0Register(EECOP0Register::EntryHi) ==
    ((faultAddress & EECOP0EntryHi::VIRTUAL_PAGE_MASK) |
     (initialEntryHi & ~EECOP0EntryHi::VIRTUAL_PAGE_MASK)));
}

TEST_CASE("EE ERET returns through the active exception level")
{
  SECTION("Level-one return uses EPC and clears EXL")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    const std::uint32_t preserved =
      EECOP0Status::INTERRUPT_ENABLE |
      EECOP0Status::MASTER_INTERRUPT_ENABLE |
      EECOP0Status::BOOTSTRAP_EXCEPTION_VECTOR;
    core.setCOP0Register(
      EECOP0Register::Status,
      preserved | EECOP0Status::EXCEPTION_LEVEL);
    mapLowKusegForTest(&core);
    core.setCOP0Register(EECOP0Register::EPC, 0x100);
    core.setCOP0Register(
      EECOP0Register::ErrorEPC,
      UINT32_C(0x80002000));
    core.setCOP0Register(
      EECOP0Register::Cause,
      EECOP0Cause::BRANCH_DELAY |
        (EEExceptionCode::BREAKPOINT << 2));
    system.eeBus().write32(0, UINT32_C(0x42000018));
    core.startExecution(0);

    const EEExecutionResult result = system.stepEEInstruction(1);

    REQUIRE(result.instructions == 1);
    REQUIRE(core.programCounter() == 0x100);
    REQUIRE(
      core.cop0Register(EECOP0Register::Status) ==
      preserved);
    REQUIRE(
      core.cop0Register(EECOP0Register::Cause) ==
      (EECOP0Cause::BRANCH_DELAY |
        (EEExceptionCode::BREAKPOINT << 2)));
    REQUIRE(core.pendingException() == EEException::None);
    REQUIRE(core.lastInstruction().operation == EEOperation::ExceptionReturn);
  }

  SECTION("Level-two return uses ErrorEPC and clears only ERL")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::EXCEPTION_LEVEL |
        EECOP0Status::ERROR_LEVEL);
    mapLowKusegForTest(&core);
    core.setCOP0Register(EECOP0Register::EPC, 0x100);
    core.setCOP0Register(EECOP0Register::ErrorEPC, 0x200);
    system.eeBus().write32(0, UINT32_C(0x42000018));
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE(core.programCounter() == 0x200);
    REQUIRE(
      core.cop0Register(EECOP0Register::Status) ==
      EECOP0Status::EXCEPTION_LEVEL);
  }
}

TEST_CASE("EE ERET is undefined in a branch delay slot")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(
    EECOP0Register::Status,
    EECOP0Status::EXCEPTION_LEVEL);
  mapLowKusegForTest(&core);
  core.setCOP0Register(EECOP0Register::EPC, 0x100);
  system.eeBus().write32(
    0,
    immediateInstruction(0x04, 0, 0, 2));
  system.eeBus().write32(4, UINT32_C(0x42000018));
  core.startExecution(0);

  system.runMasterCycles(2);

  REQUIRE(core.executionState() == EEExecutionState::Halted);
  REQUIRE(core.stopReason() == EEStopReason::UndefinedOperation);
  REQUIRE(core.programCounter() == 4);
  REQUIRE(
    (core.cop0Register(EECOP0Register::Status) &
      EECOP0Status::EXCEPTION_LEVEL) != 0);
  REQUIRE(core.rejectedInstruction() == UINT32_C(0x42000018));
}

TEST_CASE("EE delay-slot exceptions identify the restartable branch")
{
  SECTION("A TLB refill uses the refill vector and restartable branch")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(EECOP0Register::Status, 0);
    mapLowKusegForTest(&core);
    core.setGeneralRegister(1, {UINT32_C(0x00400000), 0});
    system.eeBus().write32(
      0,
      immediateInstruction(0x04, 0, 0, 2));
    system.eeBus().write32(
      4,
      immediateInstruction(0x21, 1, 2, 0));
    core.startExecution(0);

    system.runMasterCycles(1);

    REQUIRE(
      core.pendingException() ==
      EEException::TLBRefillLoadOrFetch);
    REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
    REQUIRE(
      (core.cop0Register(EECOP0Register::Cause) &
        EECOP0Cause::BRANCH_DELAY) != 0);
    REQUIRE(core.programCounter() == EEExceptionVector::REFILL);
  }

  SECTION("A data fault sets BD and points EPC at the branch")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(EECOP0Register::Status, 0);
    mapLowKusegForTest(&core);
    core.setGeneralRegister(1, {0x101, 0});
    system.eeBus().write32(
      0,
      immediateInstruction(0x04, 0, 0, 2));
    system.eeBus().write32(
      4,
      immediateInstruction(0x21, 1, 2, 0));
    core.startExecution(0);

    system.runMasterCycles(1);

    REQUIRE(
      core.pendingException() ==
      EEException::AddressErrorLoadOrFetch);
    REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
    REQUIRE(
      (core.cop0Register(EECOP0Register::Cause) &
        EECOP0Cause::BRANCH_DELAY) != 0);
    REQUIRE(core.programCounter() == EEExceptionVector::GENERAL);

    NekoSystem restored;
    restored.loadState(system.saveState());
    REQUIRE(
      restored.eeCore().cop0Register(EECOP0Register::EPC) == 0);
    REQUIRE(
      (restored.eeCore().cop0Register(EECOP0Register::Cause) &
        EECOP0Cause::BRANCH_DELAY) != 0);

    system.eeBus().write32(EEExceptionVector::GENERAL, 0);
    system.clockMasterCycle();
    REQUIRE(
      core.programCounter() ==
      EEExceptionVector::GENERAL + 4);
  }

  SECTION("A taken branch-likely delay slot sets BD")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(EECOP0Register::Status, 0);
    mapLowKusegForTest(&core);
    system.eeBus().write32(
      0,
      immediateInstruction(0x14, 0, 0, 2));
    system.eeBus().write32(4, UINT32_C(0x0000000c));
    core.startExecution(0);

    system.runMasterCycles(1);

    REQUIRE(core.pendingException() == EEException::SystemCall);
    REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
    REQUIRE(
      (core.cop0Register(EECOP0Register::Cause) &
        EECOP0Cause::BRANCH_DELAY) != 0);
  }
}

TEST_CASE("First-level non-delay exceptions clear stale Cause BD")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(EECOP0Register::Status, 0);
  mapLowKusegForTest(&core);
  core.setCOP0Register(
    EECOP0Register::Cause,
    EECOP0Cause::BRANCH_DELAY);
  system.eeBus().write32(0x100, UINT32_C(0x0000000d));
  core.startExecution(0x100);

  system.clockMasterCycle();

  REQUIRE(core.pendingException() == EEException::Breakpoint);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0x100);
  REQUIRE(
    (core.cop0Register(EECOP0Register::Cause) &
      EECOP0Cause::BRANCH_DELAY) == 0);
}

TEST_CASE("EE address exceptions update BadVAddr")
{
  SECTION("A load address error records AdEL")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(EECOP0Register::Status, 0);
    mapLowKusegForTest(&core);
    core.setCOP0Register(
      EECOP0Register::BadVAddr,
      UINT32_C(0xdeadbeef));
    core.setGeneralRegister(1, {0x101, 0});
    system.eeBus().write32(
      0,
      (UINT32_C(0x21) << 26) |
      (UINT32_C(1) << 21) |
      (UINT32_C(2) << 16));
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE(
      core.pendingException() ==
      EEException::AddressErrorLoadOrFetch);
    REQUIRE(core.cop0Register(EECOP0Register::BadVAddr) == 0x101);
    REQUIRE(
      exceptionCode(core) ==
      EEExceptionCode::ADDRESS_ERROR_LOAD_OR_FETCH);
  }

  SECTION("A store address error records AdES")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(EECOP0Register::Status, 0);
    mapLowKusegForTest(&core);
    core.setGeneralRegister(1, {0x101, 0});
    system.eeBus().write32(
      0,
      (UINT32_C(0x29) << 26) |
      (UINT32_C(1) << 21) |
      (UINT32_C(2) << 16));
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE(core.pendingException() == EEException::AddressErrorStore);
    REQUIRE(core.cop0Register(EECOP0Register::BadVAddr) == 0x101);
    REQUIRE(
      exceptionCode(core) ==
      EEExceptionCode::ADDRESS_ERROR_STORE);
  }
}

TEST_CASE("EE TLB translation faults retain their architectural kind")
{
  constexpr std::uint32_t instructionAddress =
    EEMemoryMap::KSEG0_BASE;
  constexpr std::uint32_t dataAddress = UINT32_C(0x00400000);
  constexpr std::uint32_t initialContext = UINT32_C(0xabd23450);
  constexpr std::uint32_t initialEntryHi = UINT32_C(0x89abc05a);
  const auto requireException =
    [dataAddress, initialContext, initialEntryHi](
       const EECore &core,
       EEException expected,
       std::uint8_t expectedCode)
    {
      REQUIRE(core.pendingException() == expected);
      REQUIRE(exceptionCode(core) == expectedCode);
      REQUIRE(
        core.cop0Register(EECOP0Register::BadVAddr) ==
        dataAddress);
      REQUIRE(
        core.cop0Register(EECOP0Register::Context) ==
        ((initialContext & EECOP0Context::PTE_BASE_MASK) |
         ((dataAddress >> 9) &
          EECOP0Context::BAD_VPN2_MASK)));
      REQUIRE(
        core.cop0Register(EECOP0Register::EntryHi) ==
        ((dataAddress & EECOP0EntryHi::VIRTUAL_PAGE_MASK) |
         (initialEntryHi &
          ~EECOP0EntryHi::VIRTUAL_PAGE_MASK)));
    };

  SECTION("A fetch no-match records TLB refill load or fetch")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::USER_MODE);
    core.setCOP0Register(
      EECOP0Register::Context,
      initialContext);
    core.setCOP0Register(
      EECOP0Register::EntryHi,
      initialEntryHi);
    core.startExecution(dataAddress);

    system.clockMasterCycle();

    requireException(
      core,
      EEException::TLBRefillLoadOrFetch,
      EEExceptionCode::TLB_LOAD_OR_FETCH);
  }

  SECTION("A fetch invalid match records TLB invalid load or fetch")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::USER_MODE);
    core.setCOP0Register(
      EECOP0Register::Context,
      initialContext);
    core.setCOP0Register(
      EECOP0Register::EntryHi,
      initialEntryHi);
    core.setTLBEntry(
      0,
      {
        EECOP0PageMask::SIZE_4_KIB,
        dataAddress |
          (initialEntryHi & EECOP0EntryHi::ASID_MASK),
        {},
        {}
      });
    core.startExecution(dataAddress);

    system.clockMasterCycle();

    requireException(
      core,
      EEException::TLBInvalidLoadOrFetch,
      EEExceptionCode::TLB_LOAD_OR_FETCH);
  }

  SECTION("Refill and invalid remain distinct in hashes and save states")
  {
    NekoSystem refill;
    refill.eeCore().setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::USER_MODE);
    refill.eeCore().startExecution(dataAddress);
    refill.clockMasterCycle();

    NekoSystem invalid;
    invalid.eeCore().setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::USER_MODE);
    invalid.eeCore().setTLBEntry(
      0,
      {
        EECOP0PageMask::SIZE_4_KIB,
        dataAddress,
        {},
        {}
      });
    invalid.eeCore().startExecution(dataAddress);
    invalid.clockMasterCycle();
    invalid.eeCore().setTLBEntry(0, {});

    REQUIRE(
      exceptionCode(refill.eeCore()) ==
      exceptionCode(invalid.eeCore()));
    REQUIRE(
      refill.eeCore().stateHash() !=
      invalid.eeCore().stateHash());
    REQUIRE(refill.saveState() != invalid.saveState());

    NekoSystem restored;
    restored.loadState(invalid.saveState());
    REQUIRE(
      restored.eeCore().pendingException() ==
      EEException::TLBInvalidLoadOrFetch);
    REQUIRE(restored.saveState() == invalid.saveState());
  }

  const auto runStore =
    [instructionAddress, dataAddress, initialContext, initialEntryHi](
      const EETLBPage *page,
      EEException expected,
      std::uint8_t expectedCode)
    {
      NekoSystem system;
      EECore &core = system.eeCore();
      core.setCOP0Register(EECOP0Register::Status, 0);
      core.setCOP0Register(
        EECOP0Register::Context,
        initialContext);
      core.setCOP0Register(
        EECOP0Register::EntryHi,
        initialEntryHi);
      if (page != nullptr)
      {
        core.setTLBEntry(
          0,
          {
            EECOP0PageMask::SIZE_4_KIB,
            dataAddress |
              (initialEntryHi & EECOP0EntryHi::ASID_MASK),
            *page,
            {}
          });
      }
      core.setGeneralRegister(1, {dataAddress, 0});
      system.eeBus().write32(
        0,
        immediateInstruction(0x2b, 1, 0, 0));
      core.startExecution(instructionAddress);
      system.clockMasterCycle();
      INFO(
        "actual exception " <<
        static_cast<unsigned>(core.pendingException()) <<
        ", expected " <<
        static_cast<unsigned>(expected));
      REQUIRE(core.pendingException() == expected);
      REQUIRE(exceptionCode(core) == expectedCode);
      REQUIRE(
        core.cop0Register(EECOP0Register::BadVAddr) ==
        dataAddress);
      REQUIRE(
        core.cop0Register(EECOP0Register::Context) ==
        ((initialContext & EECOP0Context::PTE_BASE_MASK) |
         ((dataAddress >> 9) &
          EECOP0Context::BAD_VPN2_MASK)));
      REQUIRE(
        core.cop0Register(EECOP0Register::EntryHi) ==
        ((dataAddress & EECOP0EntryHi::VIRTUAL_PAGE_MASK) |
         (initialEntryHi &
          ~EECOP0EntryHi::VIRTUAL_PAGE_MASK)));
    };

  SECTION("A store no-match records TLB refill store")
  {
    runStore(
      nullptr,
      EEException::TLBRefillStore,
      EEExceptionCode::TLB_STORE);
  }

  SECTION("A store invalid match records TLB invalid store")
  {
    const EETLBPage invalid = {};
    runStore(
      &invalid,
      EEException::TLBInvalidStore,
      EEExceptionCode::TLB_STORE);
  }

  SECTION("A store read-only match records TLB modified")
  {
    const EETLBPage readOnly = {UINT32_C(0x0000001a)};
    runStore(
      &readOnly,
      EEException::TLBModified,
      EEExceptionCode::TLB_MODIFIED);
  }
}

TEST_CASE("EE TLB exception state follows lifecycle boundaries")
{
  constexpr std::uint32_t instructionAddress =
    EEMemoryMap::KSEG0_BASE + UINT32_C(0x1000);
  constexpr std::uint32_t dataAddress = UINT32_C(0x00400100);
  constexpr std::uint32_t loadInstruction =
    (UINT32_C(0x23) << 26) |
    (UINT32_C(1) << 21) |
    (UINT32_C(2) << 16);
  const auto enterRefill =
    [instructionAddress, dataAddress, loadInstruction](
      NekoSystem *system)
    {
      EECore &core = system->eeCore();
      core.setCOP0Register(EECOP0Register::Status, 0);
      core.setGeneralRegister(1, {dataAddress, 0});
      core.setGeneralRegister(
        2,
        {UINT32_C(0xdeadbeef), UINT64_MAX});
      system->eeBus().write32(
        instructionAddress,
        loadInstruction);
      core.startExecution(instructionAddress);
      system->clockMasterCycle();

      REQUIRE(
        core.pendingException() ==
        EEException::TLBRefillLoadOrFetch);
      REQUIRE(core.exceptionAddress() == dataAddress);
      REQUIRE(core.programCounter() == EEExceptionVector::REFILL);
      REQUIRE(
        core.cop0Register(EECOP0Register::EPC) ==
        instructionAddress);
      REQUIRE(
        (core.cop0Register(EECOP0Register::Status) &
          EECOP0Status::EXCEPTION_LEVEL) != 0);
      REQUIRE(core.rejectedInstruction() == loadInstruction);
    };

  SECTION("An exact host resume preserves entered exception ownership")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    enterRefill(&system);
    const std::uint32_t cause =
      core.cop0Register(EECOP0Register::Cause);
    const std::uint32_t status =
      core.cop0Register(EECOP0Register::Status);

    core.haltExecution();
    const std::uint64_t haltedHash = core.stateHash();
    system.runMasterCycles(4);

    REQUIRE(core.stateHash() == haltedHash);

    core.startExecution(core.programCounter());

    REQUIRE(
      core.pendingException() ==
      EEException::TLBRefillLoadOrFetch);
    REQUIRE(core.exceptionAddress() == dataAddress);
    REQUIRE(
      core.cop0Register(EECOP0Register::Cause) == cause);
    REQUIRE(
      core.cop0Register(EECOP0Register::Status) == status);
    REQUIRE(
      core.cop0Register(EECOP0Register::EPC) ==
      instructionAddress);
    REQUIRE(core.rejectedInstruction() == loadInstruction);
  }

  SECTION("External PC redirection does not dismiss an entered exception")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    enterRefill(&system);
    const std::uint32_t cause =
      core.cop0Register(EECOP0Register::Cause);
    const std::uint32_t status =
      core.cop0Register(EECOP0Register::Status);

    core.haltExecution();
    core.setProgramCounter(EEExceptionVector::GENERAL);
    core.startExecution(core.programCounter());

    REQUIRE(
      core.pendingException() ==
      EEException::TLBRefillLoadOrFetch);
    REQUIRE(core.exceptionAddress() == dataAddress);
    REQUIRE(
      core.cop0Register(EECOP0Register::Cause) == cause);
    REQUIRE(
      core.cop0Register(EECOP0Register::Status) == status);
    REQUIRE(
      core.cop0Register(EECOP0Register::EPC) ==
      instructionAddress);
    REQUIRE(core.rejectedInstruction() == loadInstruction);
  }

  SECTION("A different-address host restart dismisses report state")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    enterRefill(&system);
    core.haltExecution();

    core.startExecution(EEMemoryMap::KSEG0_BASE + UINT32_C(0x2000));

    REQUIRE(core.pendingException() == EEException::None);
    REQUIRE(core.exceptionAddress() == 0);
    REQUIRE(core.rejectedInstruction() == 0);
    REQUIRE(
      core.programCounter() ==
      EEMemoryMap::KSEG0_BASE + UINT32_C(0x2000));
    REQUIRE(
      (core.cop0Register(EECOP0Register::Status) &
        EECOP0Status::EXCEPTION_LEVEL) != 0);
  }

  SECTION("ERET clears exception ownership before interrupt delivery")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    enterRefill(&system);
    system.eeBus().write32(
      EEExceptionVector::REFILL,
      UINT32_C(0x42000018));
    core.setCOP0Register(
      EECOP0Register::Status,
      core.cop0Register(EECOP0Register::Status) |
        EECOP0Status::INTERRUPT_ENABLE |
        EECOP0Status::MASTER_INTERRUPT_ENABLE |
        EECOP0Status::INTC_MASK);
    system.interruptController().setSource(
      EEInterruptSource::VIF0,
      true);
    system.interruptController().toggleMask(
      EEInterruptSource::mask(EEInterruptSource::VIF0));

    REQUIRE(system.stepEEInstruction(1).instructions == 1);
    REQUIRE(core.pendingException() == EEException::None);
    REQUIRE(core.exceptionAddress() == 0);
    REQUIRE(core.programCounter() == instructionAddress);
    REQUIRE(core.rejectedInstruction() == loadInstruction);
    REQUIRE(
      (core.cop0Register(EECOP0Register::Status) &
        EECOP0Status::EXCEPTION_LEVEL) == 0);

    system.clockMasterCycle();

    REQUIRE(core.pendingException() == EEException::Interrupt);
    REQUIRE(core.programCounter() == EEExceptionVector::INTERRUPT);
    REQUIRE(
      core.cop0Register(EECOP0Register::EPC) ==
      instructionAddress);
    REQUIRE(core.generalRegister(2).low == UINT32_C(0xdeadbeef));
  }

  SECTION("A restored exception retries identically after ERET")
  {
    NekoSystem original;
    EECore &originalCore = original.eeCore();
    enterRefill(&original);
    const std::vector<std::uint8_t> checkpoint =
      original.saveState();

    NekoSystem restored;
    restored.loadState(checkpoint);

    REQUIRE(restored.saveState() == checkpoint);
    REQUIRE(
      restored.eeCore().stateHash() ==
      originalCore.stateHash());

    const auto installHandlerAndMapping =
      [dataAddress](NekoSystem *system)
      {
        system->eeBus().write32(
          EEExceptionVector::REFILL,
          UINT32_C(0x42000018));
        REQUIRE(
          system->eeBus().writeData32(
            UINT32_C(0x2100),
            UINT32_C(0x89abcdef)));
        system->eeCore().setTLBEntry(
          0,
          {
            EECOP0PageMask::SIZE_4_KIB,
            dataAddress & EECOP0EntryHi::VIRTUAL_PAGE_MASK,
            {UINT32_C(0x0000009f)},
            {UINT32_C(0x0000009f)}
          });
      };
    installHandlerAndMapping(&original);
    installHandlerAndMapping(&restored);

    REQUIRE(original.stepEEInstruction(1).instructions == 1);
    REQUIRE(restored.stepEEInstruction(1).instructions == 1);
    REQUIRE(originalCore.pendingException() == EEException::None);
    REQUIRE(
      restored.eeCore().pendingException() ==
      EEException::None);

    REQUIRE(original.stepEEInstruction(1).instructions == 1);
    REQUIRE(restored.stepEEInstruction(1).instructions == 1);
    REQUIRE(
      originalCore.generalRegister(2).low ==
      UINT32_C(0xffffffff89abcdef));
    REQUIRE(
      restored.eeCore().generalRegister(2) ==
      originalCore.generalRegister(2));
    REQUIRE(
      restored.eeCore().stateHash() ==
      originalCore.stateHash());
    REQUIRE(restored.saveState() == original.saveState());
  }

  SECTION("Reset removes exception state byte-stably")
  {
    NekoSystem system;
    enterRefill(&system);
    REQUIRE(
      system.eeCore().pendingException() != EEException::None);

    system.reset();
    NekoSystem fresh;

    REQUIRE(system.eeCore().pendingException() == EEException::None);
    REQUIRE(system.eeCore().exceptionAddress() == 0);
    REQUIRE(
      system.eeCore().stateHash() ==
      fresh.eeCore().stateHash());
    REQUIRE(system.saveState() == fresh.saveState());
  }
}

TEST_CASE("EE bus errors use their architectural Cause codes")
{
  SECTION("An instruction bus error records IBE")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(EECOP0Register::Status, 0);
    mapLowKusegForTest(&core);
    core.startExecution(
      EEMemoryMap::KSEG0_BASE +
        EEMemoryMap::MAIN_MEMORY_SIZE);

    system.clockMasterCycle();

    REQUIRE(core.pendingException() == EEException::InstructionBusError);
    REQUIRE(
      exceptionCode(core) ==
      EEExceptionCode::INSTRUCTION_BUS_ERROR);
  }

  SECTION("A data bus error records DBE without changing BadVAddr")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(EECOP0Register::Status, 0);
    mapLowKusegForTest(&core);
    core.setCOP0Register(
      EECOP0Register::BadVAddr,
      UINT32_C(0xdeadbeef));
    core.setGeneralRegister(
      1,
      {
        EEMemoryMap::KSEG0_BASE +
          EEMemoryMap::MAIN_MEMORY_SIZE,
        0
      });
    system.eeBus().write32(
      0,
      (UINT32_C(0x23) << 26) |
      (UINT32_C(1) << 21) |
      (UINT32_C(2) << 16));
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE(core.pendingException() == EEException::DataBusErrorLoad);
    REQUIRE(exceptionCode(core) == EEExceptionCode::DATA_BUS_ERROR);
    REQUIRE(
      core.cop0Register(EECOP0Register::BadVAddr) ==
      UINT32_C(0xdeadbeef));
  }
}

TEST_CASE("EE reserved, syscall, and breakpoint instructions enter exceptions")
{
  const struct
  {
    std::uint32_t instruction;
    EEException exception;
    std::uint8_t code;
  } contracts[] = {
    {UINT32_C(0x4c000000),
     EEException::ReservedInstruction,
     EEExceptionCode::RESERVED_INSTRUCTION},
    {UINT32_C(0x42000019),
     EEException::ReservedInstruction,
     EEExceptionCode::RESERVED_INSTRUCTION},
    {UINT32_C(0x0123454c),
     EEException::SystemCall,
     EEExceptionCode::SYSTEM_CALL},
    {UINT32_C(0x0123454d),
     EEException::Breakpoint,
     EEExceptionCode::BREAKPOINT}
  };

  for (const auto &contract : contracts)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(EECOP0Register::Status, 0);
    mapLowKusegForTest(&core);
    system.eeBus().write32(0, contract.instruction);
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE(core.pendingException() == contract.exception);
    REQUIRE(exceptionCode(core) == contract.code);
    REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
    REQUIRE(core.programCounter() == EEExceptionVector::GENERAL);
    REQUIRE(core.rejectedInstruction() == contract.instruction);
    REQUIRE(core.executionState() == EEExecutionState::Running);
  }
}
