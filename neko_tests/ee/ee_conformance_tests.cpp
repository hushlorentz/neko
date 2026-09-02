#include <cstddef>
#include <cstdint>

#include "catch.hpp"
#include "ee_bus.hpp"
#include "neko_system.hpp"

namespace
{
  std::uint32_t registerInstruction(
    std::uint8_t function,
    std::uint8_t source,
    std::uint8_t target,
    std::uint8_t destination,
    std::uint8_t shiftAmount = 0)
  {
    return
      (static_cast<std::uint32_t>(source) << 21) |
      (static_cast<std::uint32_t>(target) << 16) |
      (static_cast<std::uint32_t>(destination) << 11) |
      (static_cast<std::uint32_t>(shiftAmount) << 6) |
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

  void writeProgram(
    NekoSystem *system,
    std::uint32_t base,
    const std::uint32_t *instructions,
    std::size_t count)
  {
    for (std::size_t index = 0; index < count; ++index)
    {
      system->eeBus().write32(
        base + static_cast<std::uint32_t>(index * 4),
        instructions[index]);
    }
  }

  std::uint32_t exceptionCode(const EECore &core)
  {
    return
      (core.cop0Register(EECOP0Register::Cause) &
       EECOP0Cause::EXCEPTION_CODE_MASK) >> 2;
  }
}

TEST_CASE("EE arithmetic conformance program")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(EECOP0Register::Status, 0);
  const std::uint32_t program[] = {
    immediateInstruction(0x09, 0, 1, 7),
    immediateInstruction(0x09, 0, 2, 0xfffd),
    registerInstruction(0x21, 1, 2, 3),
    registerInstruction(0x23, 1, 2, 4),
    registerInstruction(0x2a, 2, 1, 5),
    registerInstruction(0x00, 0, 1, 6, 2),
    UINT32_C(0x0000000c)
  };
  writeProgram(&system, 0, program, 7);
  core.startExecution(0);

  const EEExecutionResult result = system.runEE(16);

  REQUIRE_FALSE(result.cycleLimitReached);
  REQUIRE(result.instructions == 6);
  REQUIRE(core.generalRegister(1).low == 7);
  REQUIRE(
    core.generalRegister(2).low ==
    UINT64_C(0xfffffffffffffffd));
  REQUIRE(core.generalRegister(3).low == 4);
  REQUIRE(core.generalRegister(4).low == 10);
  REQUIRE(core.generalRegister(5).low == 1);
  REQUIRE(core.generalRegister(6).low == 28);
  REQUIRE(core.pendingException() == EEException::SystemCall);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 24);
}

TEST_CASE("EE delay-slot conformance program")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(EECOP0Register::Status, 0);
  const std::uint32_t program[] = {
    immediateInstruction(0x04, 0, 0, 2),
    immediateInstruction(0x0d, 0, 2, 1),
    immediateInstruction(0x0d, 0, 3, 2),
    immediateInstruction(0x15, 0, 0, 1),
    immediateInstruction(0x0d, 0, 4, 4),
    immediateInstruction(0x0d, 0, 5, 5),
    UINT32_C(0x0000000c)
  };
  writeProgram(&system, 0, program, 7);
  core.startExecution(0);

  const EEExecutionResult result = system.runEE(16);

  REQUIRE_FALSE(result.cycleLimitReached);
  REQUIRE(result.instructions == 4);
  REQUIRE(core.generalRegister(2).low == 1);
  REQUIRE(core.generalRegister(3).low == 0);
  REQUIRE(core.generalRegister(4).low == 0);
  REQUIRE(core.generalRegister(5).low == 5);
  REQUIRE(core.pendingException() == EEException::SystemCall);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 24);
}

TEST_CASE("EE memory conformance program")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(EECOP0Register::Status, 0);
  core.setGeneralRegister(1, {0x100, 0});
  core.setGeneralRegister(
    2,
    {UINT64_C(0x1122334489abcdef), 0});
  core.setGeneralRegister(
    3,
    {0, UINT64_C(0xaaaaaaaaaaaaaaaa)});
  core.setGeneralRegister(
    4,
    {
      UINT64_C(0x0123456789abcdef),
      UINT64_C(0xfedcba9876543210)
    });
  core.setGeneralRegister(
    5,
    {0, UINT64_C(0xbbbbbbbbbbbbbbbb)});
  const std::uint32_t program[] = {
    immediateInstruction(0x2b, 1, 2, 0),
    immediateInstruction(0x23, 1, 3, 0),
    immediateInstruction(0x3f, 1, 2, 8),
    immediateInstruction(0x37, 1, 5, 8),
    immediateInstruction(0x1f, 1, 4, 16),
    immediateInstruction(0x1e, 1, 6, 16),
    UINT32_C(0x0000000c)
  };
  writeProgram(&system, 0, program, 7);
  core.startExecution(0);

  const EEExecutionResult result = system.runEE(16);

  REQUIRE_FALSE(result.cycleLimitReached);
  REQUIRE(result.instructions == 6);
  REQUIRE(
    core.generalRegister(3) ==
    EERegister128{
      UINT64_C(0xffffffff89abcdef),
      UINT64_C(0xaaaaaaaaaaaaaaaa)
    });
  REQUIRE(
    core.generalRegister(5) ==
    EERegister128{
      UINT64_C(0x1122334489abcdef),
      UINT64_C(0xbbbbbbbbbbbbbbbb)
    });
  REQUIRE(core.generalRegister(6) == core.generalRegister(4));
  std::uint32_t word = 0;
  REQUIRE(system.eeBus().readData32(0x100, &word));
  REQUIRE(word == UINT32_C(0x89abcdef));
  EEQuadword quadword;
  REQUIRE(system.eeBus().readData128(0x110, &quadword));
  REQUIRE(quadword.low == core.generalRegister(4).low);
  REQUIRE(quadword.high == core.generalRegister(4).high);
  REQUIRE(core.pendingException() == EEException::SystemCall);
}

TEST_CASE("EE exception conformance program")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(EECOP0Register::Status, 0);
  core.setGeneralRegister(1, {0x7fffffff, 0});
  core.setGeneralRegister(2, {1, 0});
  core.setGeneralRegister(
    3,
    {UINT64_C(0x123456789abcdef0), UINT64_C(0xfedcba9876543210)});
  const std::uint32_t program[] = {
    registerInstruction(0x20, 1, 2, 3),
    immediateInstruction(0x0d, 0, 4, 1)
  };
  const std::uint32_t handler[] = {
    immediateInstruction(0x0d, 0, 10, 0xace)
  };
  writeProgram(&system, 0, program, 2);
  writeProgram(
    &system,
    EEExceptionVector::GENERAL,
    handler,
    1);
  core.startExecution(0);

  const EEExecutionResult entry = system.runEE(4);

  REQUIRE(entry.instructions == 0);
  REQUIRE_FALSE(entry.cycleLimitReached);
  REQUIRE(
    core.pendingException() ==
    EEException::ArithmeticOverflow);
  REQUIRE(
    exceptionCode(core) ==
    EEExceptionCode::ARITHMETIC_OVERFLOW);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
  REQUIRE(core.programCounter() == EEExceptionVector::GENERAL);
  REQUIRE(
    core.generalRegister(3) ==
    EERegister128{
      UINT64_C(0x123456789abcdef0),
      UINT64_C(0xfedcba9876543210)
    });
  REQUIRE(core.generalRegister(4).low == 0);

  const EEExecutionResult handlerResult =
    system.stepEEInstruction(1);
  REQUIRE(handlerResult.instructions == 1);
  REQUIRE(core.generalRegister(10).low == 0xace);
}

TEST_CASE("EE interrupt return conformance program")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(
    EECOP0Register::Status,
    EECOP0Status::INTERRUPT_ENABLE |
      EECOP0Status::MASTER_INTERRUPT_ENABLE |
      EECOP0Status::INTC_MASK);
  const std::uint32_t program[] = {
    immediateInstruction(0x0d, 0, 1, 1),
    UINT32_C(0x0000000c)
  };
  const std::uint32_t handler[] = {
    immediateInstruction(0x0d, 0, 10, 0x55),
    UINT32_C(0x42000018)
  };
  writeProgram(&system, 0, program, 2);
  writeProgram(
    &system,
    EEExceptionVector::INTERRUPT,
    handler,
    2);
  system.interruptController().setSource(
    EEInterruptSource::VIF0,
    true);
  system.interruptController().toggleMask(
    EEInterruptSource::mask(EEInterruptSource::VIF0));
  core.startExecution(0);

  const EEExecutionResult entry =
    system.stepEEInstruction(1);

  REQUIRE(entry.instructions == 0);
  REQUIRE_FALSE(entry.cycleLimitReached);
  REQUIRE(core.pendingException() == EEException::Interrupt);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
  REQUIRE(core.programCounter() == EEExceptionVector::INTERRUPT);

  system.interruptController().acknowledge(
    EEInterruptSource::mask(EEInterruptSource::VIF0));
  REQUIRE(system.stepEEInstruction(1).instructions == 1);
  REQUIRE(core.generalRegister(10).low == 0x55);
  REQUIRE(system.stepEEInstruction(1).instructions == 1);
  REQUIRE(core.pendingException() == EEException::None);
  REQUIRE(core.programCounter() == 0);
  REQUIRE(system.stepEEInstruction(1).instructions == 1);
  REQUIRE(core.generalRegister(1).low == 1);
}
