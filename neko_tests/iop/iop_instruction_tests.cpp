#include <array>
#include <cstdint>

#include "catch.hpp"
#include "iop_instruction.hpp"

namespace
{
  constexpr std::uint32_t registerInstruction(
    std::uint8_t function,
    std::uint8_t rs = 0,
    std::uint8_t rt = 0,
    std::uint8_t rd = 0,
    std::uint8_t shiftAmount = 0)
  {
    return
      (static_cast<std::uint32_t>(rs) << 21) |
      (static_cast<std::uint32_t>(rt) << 16) |
      (static_cast<std::uint32_t>(rd) << 11) |
      (static_cast<std::uint32_t>(shiftAmount) << 6) |
      function;
  }

  constexpr std::uint32_t immediateInstruction(
    std::uint8_t opcode,
    std::uint8_t rs = 0,
    std::uint8_t rt = 0,
    std::uint16_t immediate = 0)
  {
    return
      (static_cast<std::uint32_t>(opcode) << 26) |
      (static_cast<std::uint32_t>(rs) << 21) |
      (static_cast<std::uint32_t>(rt) << 16) |
      immediate;
  }

  constexpr std::uint32_t cop0Instruction(
    std::uint8_t rs,
    std::uint8_t rt = 0,
    std::uint8_t rd = 0,
    std::uint16_t low = 0)
  {
    return immediateInstruction(0x10, rs, rt, 0) |
      (static_cast<std::uint32_t>(rd) << 11) |
      low;
  }

  struct ExpectedDecode
  {
    IOPDecodeDisposition disposition;
    IOPOperation operation;
  };

  constexpr ExpectedDecode reserved()
  {
    return {
      IOPDecodeDisposition::Reserved,
      IOPOperation::None
    };
  }

  constexpr ExpectedDecode supported(IOPOperation operation)
  {
    return {
      IOPDecodeDisposition::Supported,
      operation
    };
  }

  constexpr ExpectedDecode deferred(IOPOperation operation)
  {
    return {
      IOPDecodeDisposition::ValidButDeferred,
      operation
    };
  }
}

TEST_CASE("IOP instruction decoding preserves fixed-width fields",
  "[iop][decode]")
{
  const std::uint32_t raw =
    registerInstruction(0x21, 1, 2, 3, 4);
  const IOPDecodeResult result = decodeIOPInstruction(raw);

  REQUIRE(result.instruction.raw == raw);
  REQUIRE(result.instruction.opcode == 0);
  REQUIRE(result.instruction.sourceRegister == 1);
  REQUIRE(result.instruction.targetRegister == 2);
  REQUIRE(result.instruction.destinationRegister == 3);
  REQUIRE(result.instruction.shiftAmount == 4);
  REQUIRE(result.instruction.function == 0x21);
  REQUIRE(result.instruction.immediate == 0x1921);
  REQUIRE(result.instruction.target == (raw & UINT32_C(0x03ffffff)));
}

TEST_CASE("IOP primary opcode table is completely classified",
  "[iop][decode]")
{
  constexpr std::array<ExpectedDecode, 64> expected = {{
    supported(IOPOperation::None),
    supported(IOPOperation::None),
    supported(IOPOperation::Jump),
    supported(IOPOperation::JumpAndLink),
    supported(IOPOperation::BranchEqual),
    supported(IOPOperation::BranchNotEqual),
    supported(IOPOperation::BranchLessThanOrEqualZero),
    supported(IOPOperation::BranchGreaterThanZero),
    supported(IOPOperation::AddImmediate),
    supported(IOPOperation::AddImmediateUnsigned),
    supported(IOPOperation::SetLessThanImmediate),
    supported(IOPOperation::SetLessThanImmediateUnsigned),
    supported(IOPOperation::AndImmediate),
    supported(IOPOperation::OrImmediate),
    supported(IOPOperation::XorImmediate),
    supported(IOPOperation::LoadUpperImmediate),
    supported(IOPOperation::None),
    deferred(IOPOperation::CoprocessorOperation),
    deferred(IOPOperation::CoprocessorOperation),
    reserved(),
    reserved(), reserved(), reserved(), reserved(),
    reserved(), reserved(), reserved(), reserved(),
    reserved(), reserved(), reserved(), reserved(),
    supported(IOPOperation::LoadByte),
    supported(IOPOperation::LoadHalfword),
    supported(IOPOperation::LoadWordLeft),
    supported(IOPOperation::LoadWord),
    supported(IOPOperation::LoadByteUnsigned),
    supported(IOPOperation::LoadHalfwordUnsigned),
    supported(IOPOperation::LoadWordRight),
    reserved(),
    supported(IOPOperation::StoreByte),
    supported(IOPOperation::StoreHalfword),
    supported(IOPOperation::StoreWordLeft),
    supported(IOPOperation::StoreWord),
    reserved(), reserved(),
    supported(IOPOperation::StoreWordRight),
    reserved(),
    reserved(),
    deferred(IOPOperation::LoadWordToCoprocessor),
    deferred(IOPOperation::LoadWordToCoprocessor),
    reserved(),
    reserved(), reserved(), reserved(), reserved(),
    reserved(),
    deferred(IOPOperation::StoreWordFromCoprocessor),
    deferred(IOPOperation::StoreWordFromCoprocessor),
    reserved(),
    reserved(), reserved(), reserved(), reserved()
  }};

  for (std::uint8_t opcode = 0; opcode < expected.size(); ++opcode)
  {
    const IOPDecodeResult result =
      decodeIOPInstruction(immediateInstruction(opcode));
    INFO("primary opcode " << static_cast<unsigned>(opcode));
    REQUIRE(result.disposition == expected[opcode].disposition);
    if (expected[opcode].operation != IOPOperation::None)
    {
      REQUIRE(
        result.instruction.operation ==
        expected[opcode].operation);
    }
  }
}

TEST_CASE("IOP SPECIAL function table is completely classified",
  "[iop][decode]")
{
  constexpr std::array<ExpectedDecode, 64> expected = {{
    supported(IOPOperation::ShiftLeftLogical),
    reserved(),
    supported(IOPOperation::ShiftRightLogical),
    supported(IOPOperation::ShiftRightArithmetic),
    supported(IOPOperation::ShiftLeftLogicalVariable),
    reserved(),
    supported(IOPOperation::ShiftRightLogicalVariable),
    supported(IOPOperation::ShiftRightArithmeticVariable),
    supported(IOPOperation::JumpRegister),
    supported(IOPOperation::JumpAndLinkRegister),
    reserved(), reserved(),
    supported(IOPOperation::SystemCall),
    supported(IOPOperation::Breakpoint),
    reserved(), reserved(),
    supported(IOPOperation::MoveFromHI),
    supported(IOPOperation::MoveToHI),
    supported(IOPOperation::MoveFromLO),
    supported(IOPOperation::MoveToLO),
    reserved(), reserved(), reserved(), reserved(),
    supported(IOPOperation::Multiply),
    supported(IOPOperation::MultiplyUnsigned),
    supported(IOPOperation::Divide),
    supported(IOPOperation::DivideUnsigned),
    reserved(), reserved(), reserved(), reserved(),
    supported(IOPOperation::Add),
    supported(IOPOperation::AddUnsigned),
    supported(IOPOperation::Subtract),
    supported(IOPOperation::SubtractUnsigned),
    supported(IOPOperation::And),
    supported(IOPOperation::Or),
    supported(IOPOperation::Xor),
    supported(IOPOperation::Nor),
    reserved(), reserved(),
    supported(IOPOperation::SetLessThan),
    supported(IOPOperation::SetLessThanUnsigned),
    reserved(), reserved(), reserved(), reserved(),
    reserved(), reserved(), reserved(), reserved(),
    reserved(), reserved(), reserved(), reserved(),
    reserved(), reserved(), reserved(), reserved(),
    reserved(), reserved(), reserved(), reserved()
  }};

  for (std::uint8_t function = 0;
       function < expected.size();
       ++function)
  {
    const std::uint32_t raw = function == 0
      ? registerInstruction(function, 0, 1, 1, 1)
      : registerInstruction(function);
    const IOPDecodeResult result =
      decodeIOPInstruction(raw);
    INFO("SPECIAL function " << static_cast<unsigned>(function));
    REQUIRE(result.disposition == expected[function].disposition);
    if (expected[function].operation != IOPOperation::None)
    {
      REQUIRE(
        result.instruction.operation ==
        expected[function].operation);
    }
  }
}

TEST_CASE("IOP REGIMM selector table is completely classified",
  "[iop][decode]")
{
  constexpr std::array<ExpectedDecode, 32> expected = {{
    supported(IOPOperation::BranchLessThanZero),
    supported(IOPOperation::BranchGreaterThanOrEqualZero),
    reserved(), reserved(), reserved(), reserved(), reserved(), reserved(),
    reserved(), reserved(), reserved(), reserved(), reserved(), reserved(),
    reserved(), reserved(),
    supported(IOPOperation::BranchLessThanZeroAndLink),
    supported(IOPOperation::BranchGreaterThanOrEqualZeroAndLink),
    reserved(), reserved(), reserved(), reserved(), reserved(), reserved(),
    reserved(), reserved(), reserved(), reserved(), reserved(), reserved(),
    reserved(), reserved()
  }};

  for (std::uint8_t selector = 0;
       selector < expected.size();
       ++selector)
  {
    const IOPDecodeResult result = decodeIOPInstruction(
      immediateInstruction(0x01, 3, selector, 0x1234));
    INFO("REGIMM selector " << static_cast<unsigned>(selector));
    REQUIRE(result.disposition == expected[selector].disposition);
    if (expected[selector].operation != IOPOperation::None)
    {
      REQUIRE(
        result.instruction.operation ==
        expected[selector].operation);
    }
  }
}

TEST_CASE("IOP COP0 selector and function tables are completely classified",
  "[iop][decode]")
{
  for (std::uint8_t selector = 0; selector < 32; ++selector)
  {
    const IOPDecodeResult result =
      decodeIOPInstruction(cop0Instruction(selector));
    INFO("COP0 selector " << static_cast<unsigned>(selector));
    if (selector == 0)
    {
      REQUIRE(
        result.disposition ==
        IOPDecodeDisposition::Supported);
      REQUIRE(
        result.instruction.operation ==
        IOPOperation::MoveFromCOP0);
    }
    else if (selector == 4)
    {
      REQUIRE(
        result.disposition ==
        IOPDecodeDisposition::Supported);
      REQUIRE(
        result.instruction.operation ==
        IOPOperation::MoveToCOP0);
    }
    else
    {
      REQUIRE(
        result.disposition ==
        IOPDecodeDisposition::Reserved);
    }
  }

  for (std::uint8_t function = 0; function < 64; ++function)
  {
    const IOPDecodeResult result = decodeIOPInstruction(
      cop0Instruction(0x10, 0, 0, function));
    INFO("COP0 function " << static_cast<unsigned>(function));
    if (function == 0x10)
    {
      REQUIRE(
        result.disposition ==
        IOPDecodeDisposition::Supported);
      REQUIRE(
        result.instruction.operation ==
        IOPOperation::ReturnFromException);
    }
    else
    {
      REQUIRE(
        result.disposition ==
        IOPDecodeDisposition::Reserved);
    }
  }
}

TEST_CASE("IOP decoder rejects nonzero fields required to be zero",
  "[iop][decode]")
{
  const std::uint32_t rs = UINT32_C(1) << 21;
  const std::uint32_t rt = UINT32_C(1) << 16;
  const std::uint32_t rd = UINT32_C(1) << 11;
  const std::uint32_t shift = UINT32_C(1) << 6;
  const std::uint32_t low = 1;

  for (std::uint8_t function : {
         UINT8_C(0x00), UINT8_C(0x02), UINT8_C(0x03)})
  {
    REQUIRE(
      decodeIOPInstruction(
        registerInstruction(function) | rs).disposition ==
      IOPDecodeDisposition::Reserved);
  }

  for (std::uint8_t function : {
         UINT8_C(0x04), UINT8_C(0x06), UINT8_C(0x07),
         UINT8_C(0x20), UINT8_C(0x21), UINT8_C(0x22),
         UINT8_C(0x23), UINT8_C(0x24), UINT8_C(0x25),
         UINT8_C(0x26), UINT8_C(0x27), UINT8_C(0x2a),
         UINT8_C(0x2b)})
  {
    REQUIRE(
      decodeIOPInstruction(
        registerInstruction(function) | shift).disposition ==
      IOPDecodeDisposition::Reserved);
  }

  REQUIRE(
    decodeIOPInstruction(registerInstruction(0x08) | rt).disposition ==
    IOPDecodeDisposition::Reserved);
  REQUIRE(
    decodeIOPInstruction(registerInstruction(0x08) | rd).disposition ==
    IOPDecodeDisposition::Reserved);
  REQUIRE(
    decodeIOPInstruction(registerInstruction(0x08) | shift).disposition ==
    IOPDecodeDisposition::Reserved);
  REQUIRE(
    decodeIOPInstruction(registerInstruction(0x09) | rt).disposition ==
    IOPDecodeDisposition::Reserved);
  REQUIRE(
    decodeIOPInstruction(registerInstruction(0x09) | shift).disposition ==
    IOPDecodeDisposition::Reserved);

  for (std::uint8_t function : {
         UINT8_C(0x10), UINT8_C(0x12)})
  {
    REQUIRE(
      decodeIOPInstruction(
        registerInstruction(function) | rs).disposition ==
      IOPDecodeDisposition::Reserved);
    REQUIRE(
      decodeIOPInstruction(
        registerInstruction(function) | rt).disposition ==
      IOPDecodeDisposition::Reserved);
    REQUIRE(
      decodeIOPInstruction(
        registerInstruction(function) | shift).disposition ==
      IOPDecodeDisposition::Reserved);
  }

  for (std::uint8_t function : {
         UINT8_C(0x11), UINT8_C(0x13)})
  {
    REQUIRE(
      decodeIOPInstruction(
        registerInstruction(function) | rt).disposition ==
      IOPDecodeDisposition::Reserved);
    REQUIRE(
      decodeIOPInstruction(
        registerInstruction(function) | rd).disposition ==
      IOPDecodeDisposition::Reserved);
    REQUIRE(
      decodeIOPInstruction(
        registerInstruction(function) | shift).disposition ==
      IOPDecodeDisposition::Reserved);
  }

  for (std::uint8_t function : {
         UINT8_C(0x18), UINT8_C(0x19),
         UINT8_C(0x1a), UINT8_C(0x1b)})
  {
    REQUIRE(
      decodeIOPInstruction(
        registerInstruction(function) | rd).disposition ==
      IOPDecodeDisposition::Reserved);
    REQUIRE(
      decodeIOPInstruction(
        registerInstruction(function) | shift).disposition ==
      IOPDecodeDisposition::Reserved);
  }

  REQUIRE(
    decodeIOPInstruction(
      immediateInstruction(0x06, 1, 1)).disposition ==
    IOPDecodeDisposition::Reserved);
  REQUIRE(
    decodeIOPInstruction(
      immediateInstruction(0x07, 1, 1)).disposition ==
    IOPDecodeDisposition::Reserved);
  REQUIRE(
    decodeIOPInstruction(
      immediateInstruction(0x0f, 1, 1)).disposition ==
    IOPDecodeDisposition::Reserved);

  REQUIRE(
    decodeIOPInstruction(
      cop0Instruction(0, 1, 2, low)).disposition ==
    IOPDecodeDisposition::Reserved);
  REQUIRE(
    decodeIOPInstruction(
      cop0Instruction(4, 1, 2, low)).disposition ==
    IOPDecodeDisposition::Reserved);
  REQUIRE(
    decodeIOPInstruction(
      cop0Instruction(0x10, 0, 0, 0x10) | rt).disposition ==
    IOPDecodeDisposition::Reserved);
  REQUIRE(
    decodeIOPInstruction(
      cop0Instruction(0x10, 0, 0, 0x10) | rd).disposition ==
    IOPDecodeDisposition::Reserved);
  REQUIRE(
    decodeIOPInstruction(
      cop0Instruction(0x10, 0, 0, 0x10) | shift).disposition ==
    IOPDecodeDisposition::Reserved);
}

TEST_CASE("IOP canonical zero instruction decodes as NOP",
  "[iop][decode]")
{
  const IOPDecodeResult result = decodeIOPInstruction(0);

  REQUIRE(result.disposition == IOPDecodeDisposition::Supported);
  REQUIRE(result.instruction.operation == IOPOperation::Nop);
}
