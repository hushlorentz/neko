#include <cstdint>

#include "catch.hpp"
#include "ee_instruction.hpp"

namespace
{
  std::uint32_t registerInstruction(
    std::uint8_t function,
    std::uint8_t rs,
    std::uint8_t rt,
    std::uint8_t rd,
    std::uint8_t shiftAmount = 0)
  {
    return
      (static_cast<std::uint32_t>(rs) << 21) |
      (static_cast<std::uint32_t>(rt) << 16) |
      (static_cast<std::uint32_t>(rd) << 11) |
      (static_cast<std::uint32_t>(shiftAmount) << 6) |
      function;
  }

  std::uint32_t immediateInstruction(
    std::uint8_t opcode,
    std::uint8_t rs,
    std::uint8_t rt,
    std::uint16_t immediate)
  {
    return
      (static_cast<std::uint32_t>(opcode) << 26) |
      (static_cast<std::uint32_t>(rs) << 21) |
      (static_cast<std::uint32_t>(rt) << 16) |
      immediate;
  }
}

TEST_CASE("EE instruction field decoding")
{
  SECTION("The canonical zero instruction decodes as NOP")
  {
    const EEInstruction instruction = decodeEEInstruction(0);

    REQUIRE(instruction.operation == EEOperation::Nop);
    REQUIRE(instruction.raw == 0);
  }

  SECTION("Register instruction fields are preserved")
  {
    const std::uint32_t raw =
      registerInstruction(0x21, 1, 2, 3);
    const EEInstruction instruction =
      decodeEEInstruction(raw);

    REQUIRE(
      instruction.operation ==
      EEOperation::AddUnsignedWord);
    REQUIRE(instruction.raw == raw);
    REQUIRE(instruction.opcode == 0);
    REQUIRE(instruction.sourceRegister == 1);
    REQUIRE(instruction.targetRegister == 2);
    REQUIRE(instruction.destinationRegister == 3);
    REQUIRE(instruction.shiftAmount == 0);
    REQUIRE(instruction.function == 0x21);
  }

  SECTION("Immediate instruction fields remain raw")
  {
    const std::uint32_t raw =
      immediateInstruction(0x09, 4, 5, 0xfffc);
    const EEInstruction instruction =
      decodeEEInstruction(raw);

    REQUIRE(
      instruction.operation ==
      EEOperation::AddImmediateUnsignedWord);
    REQUIRE(instruction.sourceRegister == 4);
    REQUIRE(instruction.targetRegister == 5);
    REQUIRE(instruction.immediate == 0xfffc);
  }
}

TEST_CASE("EE base integer decoder tables")
{
  struct RegisterContract
  {
    std::uint8_t function;
    EEOperation operation;
    bool immediateShift;
  };
  const RegisterContract registerContracts[] = {
    {0x00, EEOperation::ShiftLeftLogicalWord, true},
    {0x02, EEOperation::ShiftRightLogicalWord, true},
    {0x03, EEOperation::ShiftRightArithmeticWord, true},
    {0x04, EEOperation::ShiftLeftLogicalVariableWord, false},
    {0x06, EEOperation::ShiftRightLogicalVariableWord, false},
    {0x07, EEOperation::ShiftRightArithmeticVariableWord, false},
    {0x14, EEOperation::ShiftLeftLogicalVariableDoubleword, false},
    {0x16, EEOperation::ShiftRightLogicalVariableDoubleword, false},
    {0x17, EEOperation::ShiftRightArithmeticVariableDoubleword, false},
    {0x20, EEOperation::AddWord, false},
    {0x21, EEOperation::AddUnsignedWord, false},
    {0x22, EEOperation::SubtractWord, false},
    {0x23, EEOperation::SubtractUnsignedWord, false},
    {0x24, EEOperation::And, false},
    {0x25, EEOperation::Or, false},
    {0x26, EEOperation::Xor, false},
    {0x27, EEOperation::Nor, false},
    {0x2a, EEOperation::SetLessThan, false},
    {0x2b, EEOperation::SetLessThanUnsigned, false},
    {0x2c, EEOperation::AddDoubleword, false},
    {0x2d, EEOperation::AddUnsignedDoubleword, false},
    {0x2e, EEOperation::SubtractDoubleword, false},
    {0x2f, EEOperation::SubtractUnsignedDoubleword, false},
    {0x38, EEOperation::ShiftLeftLogicalDoubleword, true},
    {0x3a, EEOperation::ShiftRightLogicalDoubleword, true},
    {0x3b, EEOperation::ShiftRightArithmeticDoubleword, true},
    {0x3c, EEOperation::ShiftLeftLogicalDoubleword32, true},
    {0x3e, EEOperation::ShiftRightLogicalDoubleword32, true},
    {0x3f, EEOperation::ShiftRightArithmeticDoubleword32, true}
  };

  for (const RegisterContract &contract : registerContracts)
  {
    const std::uint8_t rs = contract.immediateShift ? 0 : 1;
    const std::uint8_t shift = contract.immediateShift ? 7 : 0;
    const EEInstruction instruction = decodeEEInstruction(
      registerInstruction(
        contract.function,
        rs,
        2,
        3,
        shift));
    REQUIRE(instruction.operation == contract.operation);
  }

  struct ImmediateContract
  {
    std::uint8_t opcode;
    EEOperation operation;
  };
  const ImmediateContract immediateContracts[] = {
    {0x08, EEOperation::AddImmediateWord},
    {0x09, EEOperation::AddImmediateUnsignedWord},
    {0x0a, EEOperation::SetLessThanImmediate},
    {0x0b, EEOperation::SetLessThanImmediateUnsigned},
    {0x0c, EEOperation::AndImmediate},
    {0x0d, EEOperation::OrImmediate},
    {0x0e, EEOperation::XorImmediate},
    {0x0f, EEOperation::LoadUpperImmediate},
    {0x18, EEOperation::AddImmediateDoubleword},
    {0x19, EEOperation::AddImmediateUnsignedDoubleword}
  };

  for (const ImmediateContract &contract : immediateContracts)
  {
    const std::uint8_t rs = contract.opcode == 0x0f ? 0 : 1;
    const EEInstruction instruction = decodeEEInstruction(
      immediateInstruction(
        contract.opcode,
        rs,
        2,
        0x3456));
    REQUIRE(instruction.operation == contract.operation);
  }
}

TEST_CASE("EE decoder rejects invalid and deferred encodings")
{
  SECTION("Reserved primary opcodes are distinguished")
  {
    REQUIRE_THROWS_WITH(
      decodeEEInstruction(UINT32_C(0x4c000000)),
      "Reserved EE instruction encoding.");
  }

  SECTION("Architecturally unsupported primary opcodes are rejected")
  {
    REQUIRE_THROWS_WITH(
      decodeEEInstruction(UINT32_C(0xc0000000)),
      "Unsupported EE instruction encoding.");
  }

  SECTION("Deferred valid instruction families are rejected explicitly")
  {
    REQUIRE_THROWS_WITH(
      decodeEEInstruction(UINT32_C(0x10000000)),
      "Unsupported EE instruction encoding.");
    REQUIRE_THROWS_WITH(
      decodeEEInstruction(UINT32_C(0x40000000)),
      "Unsupported EE instruction encoding.");
  }

  SECTION("Reserved SPECIAL functions are distinguished")
  {
    REQUIRE_THROWS_WITH(
      decodeEEInstruction(registerInstruction(0x01, 0, 0, 0)),
      "Reserved EE instruction encoding.");
  }

  SECTION("Fixed zero fields are validated")
  {
    REQUIRE_THROWS_WITH(
      decodeEEInstruction(
        registerInstruction(0x00, 1, 2, 3, 4)),
      "Reserved EE instruction encoding.");
    REQUIRE_THROWS_WITH(
      decodeEEInstruction(
        registerInstruction(0x20, 1, 2, 3, 4)),
      "Reserved EE instruction encoding.");
    REQUIRE_THROWS_WITH(
      decodeEEInstruction(
        immediateInstruction(0x0f, 1, 2, 3)),
      "Reserved EE instruction encoding.");
  }
}

TEST_CASE("EE multiply divide and SA decoder tables")
{
  struct Contract
  {
    std::uint32_t instruction;
    EEOperation operation;
  };
  const Contract contracts[] = {
    {registerInstruction(0x10, 0, 0, 3), EEOperation::MoveFromHI},
    {registerInstruction(0x11, 1, 0, 0), EEOperation::MoveToHI},
    {registerInstruction(0x12, 0, 0, 3), EEOperation::MoveFromLO},
    {registerInstruction(0x13, 1, 0, 0), EEOperation::MoveToLO},
    {registerInstruction(0x18, 1, 2, 3), EEOperation::MultiplyWord},
    {registerInstruction(0x19, 1, 2, 3), EEOperation::MultiplyUnsignedWord},
    {registerInstruction(0x1a, 1, 2, 0), EEOperation::DivideWord},
    {registerInstruction(0x1b, 1, 2, 0), EEOperation::DivideUnsignedWord},
    {registerInstruction(0x28, 0, 0, 3), EEOperation::MoveFromShiftAmount},
    {registerInstruction(0x29, 1, 0, 0), EEOperation::MoveToShiftAmount},
    {UINT32_C(0x70000000) | registerInstruction(0x00, 1, 2, 3), EEOperation::MultiplyAddWord},
    {UINT32_C(0x70000000) | registerInstruction(0x01, 1, 2, 3), EEOperation::MultiplyAddUnsignedWord},
    {UINT32_C(0x70000000) | registerInstruction(0x10, 0, 0, 3), EEOperation::MoveFromHI1},
    {UINT32_C(0x70000000) | registerInstruction(0x11, 1, 0, 0), EEOperation::MoveToHI1},
    {UINT32_C(0x70000000) | registerInstruction(0x12, 0, 0, 3), EEOperation::MoveFromLO1},
    {UINT32_C(0x70000000) | registerInstruction(0x13, 1, 0, 0), EEOperation::MoveToLO1},
    {UINT32_C(0x70000000) | registerInstruction(0x18, 1, 2, 3), EEOperation::MultiplyWord1},
    {UINT32_C(0x70000000) | registerInstruction(0x19, 1, 2, 3), EEOperation::MultiplyUnsignedWord1},
    {UINT32_C(0x70000000) | registerInstruction(0x1a, 1, 2, 0), EEOperation::DivideWord1},
    {UINT32_C(0x70000000) | registerInstruction(0x1b, 1, 2, 0), EEOperation::DivideUnsignedWord1},
    {UINT32_C(0x70000000) | registerInstruction(0x20, 1, 2, 3), EEOperation::MultiplyAddWord1},
    {UINT32_C(0x70000000) | registerInstruction(0x21, 1, 2, 3), EEOperation::MultiplyAddUnsignedWord1},
    {immediateInstruction(0x01, 1, 0x18, 5), EEOperation::MoveByteCountToShiftAmount},
    {immediateInstruction(0x01, 1, 0x19, 5), EEOperation::MoveHalfwordCountToShiftAmount}
  };

  for (const Contract &contract : contracts)
  {
    REQUIRE(
      decodeEEInstruction(contract.instruction).operation ==
      contract.operation);
  }
}
