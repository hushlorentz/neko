#include <cstdint>

#include "catch.hpp"
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

TEST_CASE("EE bootstrap and interrupt vectors follow Status BEV")
{
  SECTION("A general exception uses the bootstrap vector")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::BOOTSTRAP_EXCEPTION_VECTOR);
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
    core.setProgramCounter(0x80001000);

    core.enterInterruptException();

    REQUIRE(core.programCounter() == EEExceptionVector::INTERRUPT);
    REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0x80001000);
    REQUIRE(exceptionCode(core) == EEExceptionCode::INTERRUPT);

    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::BOOTSTRAP_EXCEPTION_VECTOR);
    core.setProgramCounter(0x80002000);
    core.enterInterruptException();

    REQUIRE(
      core.programCounter() ==
      EEExceptionVector::BOOTSTRAP_INTERRUPT);
  }
}

TEST_CASE("Nested EE exceptions preserve EPC and use the general vector")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(
    EECOP0Register::Status,
    EECOP0Status::EXCEPTION_LEVEL);
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

TEST_CASE("EE delay-slot exceptions identify the restartable branch")
{
  SECTION("A data fault sets BD and points EPC at the branch")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(EECOP0Register::Status, 0);
    core.setGeneralRegister(1, {0x101, 0});
    system.eeBus().write32(
      0,
      immediateInstruction(0x04, 0, 0, 2));
    system.eeBus().write32(
      4,
      immediateInstruction(0x21, 1, 2, 0));
    core.startExecution(0);

    system.runMasterCycles(2);

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
    system.eeBus().write32(
      0,
      immediateInstruction(0x14, 0, 0, 2));
    system.eeBus().write32(4, UINT32_C(0x0000000c));
    core.startExecution(0);

    system.runMasterCycles(2);

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

TEST_CASE("EE bus errors use their architectural Cause codes")
{
  SECTION("An instruction bus error records IBE")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(EECOP0Register::Status, 0);
    core.startExecution(UINT32_C(0x02000000));

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
    core.setCOP0Register(
      EECOP0Register::BadVAddr,
      UINT32_C(0xdeadbeef));
    core.setGeneralRegister(1, {EEMemoryMap::MAIN_MEMORY_SIZE, 0});
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
