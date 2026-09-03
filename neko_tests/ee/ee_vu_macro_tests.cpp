#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "ee_core.hpp"
#include "neko_system.hpp"
#include "vpu_field_mask.hpp"
#include "vpu_flags.hpp"
#include "vpu_instruction.hpp"
#include "vpu_opcodes.hpp"

namespace
{
  std::uint32_t macroArithmeticInstruction(
    std::uint8_t destinationFields,
    std::uint8_t target,
    std::uint8_t source,
    std::uint8_t destination,
    std::uint8_t operation)
  {
    return
      UINT32_C(0x4a000000) |
      (static_cast<std::uint32_t>(
        vpuFieldMaskToEncoding(destinationFields)) << 21) |
      (static_cast<std::uint32_t>(target) << 16) |
      (static_cast<std::uint32_t>(source) << 11) |
      (static_cast<std::uint32_t>(destination) << 6) |
      operation;
  }

  std::uint32_t quadwordMoveFromCOP2(
    std::uint8_t target,
    std::uint8_t vectorRegister)
  {
    return
      UINT32_C(0x48200001) |
      (static_cast<std::uint32_t>(target) << 16) |
      (static_cast<std::uint32_t>(vectorRegister) << 11);
  }

  std::uint32_t controlMoveFromCOP2(
    std::uint8_t target,
    std::uint8_t controlRegister)
  {
    return
      UINT32_C(0x48400000) |
      (static_cast<std::uint32_t>(target) << 16) |
      (static_cast<std::uint32_t>(controlRegister) << 11);
  }

  std::uint32_t controlMoveToCOP2(
    std::uint8_t target,
    std::uint8_t controlRegister)
  {
    return
      UINT32_C(0x48c00000) |
      (static_cast<std::uint32_t>(target) << 16) |
      (static_cast<std::uint32_t>(controlRegister) << 11);
  }

  std::uint32_t quadwordMoveToCOP2(
    std::uint8_t target,
    std::uint8_t vectorRegister)
  {
    return
      UINT32_C(0x48a00000) |
      (static_cast<std::uint32_t>(target) << 16) |
      (static_cast<std::uint32_t>(vectorRegister) << 11);
  }

  std::uint32_t storeQuadwordFromCOP2(
    std::uint8_t base,
    std::uint8_t vectorRegister,
    std::uint16_t offset = 0)
  {
    return
      (UINT32_C(0x3e) << 26) |
      (static_cast<std::uint32_t>(base) << 21) |
      (static_cast<std::uint32_t>(vectorRegister) << 16) |
      offset;
  }

  std::uint32_t vectorCallInstruction(
    std::uint16_t instructionAddress)
  {
    return
      UINT32_C(0x4a000038) |
      (static_cast<std::uint32_t>(instructionAddress) << 6);
  }

  std::uint32_t macroUpperInstruction(
    std::uint16_t operation,
    bool hasDestinationRegister,
    bool scalar = false,
    std::uint8_t destinationFields =
      FP_REGISTER_ALL_FIELDS)
  {
    if (operation == VPU_NOP)
    {
      return UINT32_C(0x4a000000) | operation;
    }
    return
      UINT32_C(0x4a000000) |
      (static_cast<std::uint32_t>(
        vpuFieldMaskToEncoding(destinationFields)) << 21) |
      (static_cast<std::uint32_t>(scalar ? 0 : 2) << 16) |
      (UINT32_C(1) << 11) |
      (hasDestinationRegister ? UINT32_C(3) << 6 : 0) |
      operation;
  }

  std::uint32_t macroLowerInstruction(
    std::uint32_t lowerInstruction)
  {
    return
      UINT32_C(0x4a000000) |
      (lowerInstruction & UINT32_C(0x01ffffff));
  }

  void runMacroAndReadResult(
    NekoSystem *system,
    std::uint32_t instruction,
    std::uint8_t vectorRegister)
  {
    system->eeBus().write32(0, instruction);
    system->eeBus().write32(
      4,
      quadwordMoveFromCOP2(2, vectorRegister));
    system->eeCore().startExecution(0);

    const EEExecutionResult issued =
      system->stepEEInstruction(8);
    REQUIRE(issued.instructions == 1);
    REQUIRE(system->eeCore().programCounter() == 4);
    REQUIRE(system->vu0().clockActive());

    const EEExecutionResult read =
      system->stepEEInstruction(32);
    REQUIRE(read.instructions == 1);
    REQUIRE(system->eeCore().programCounter() == 8);
    REQUIRE_FALSE(system->vu0().clockActive());
  }
}

TEST_CASE("EE VU macro VADD and VSUB variants decode canonically")
{
  const std::uint8_t operations[] = {
    VPU_ADD,
    VPU_ADDi,
    VPU_ADDq,
    VPU_ADDx,
    VPU_ADDy,
    VPU_ADDz,
    VPU_ADDw,
    VPU_SUB,
    VPU_SUBi,
    VPU_SUBq,
    VPU_SUBx,
    VPU_SUBy,
    VPU_SUBz,
    VPU_SUBw
  };

  for (std::uint8_t operation : operations)
  {
    const bool scalar =
      operation == VPU_ADDi ||
      operation == VPU_ADDq ||
      operation == VPU_SUBi ||
      operation == VPU_SUBq;
    const EEInstruction decoded =
      decodeEEInstruction(
        macroArithmeticInstruction(
          FP_REGISTER_ALL_FIELDS,
          scalar ? 0 : 2,
          1,
          3,
          operation));
    REQUIRE(
      decoded.operation ==
      EEOperation::VectorMacroArithmetic);
  }

  REQUIRE_THROWS_WITH(
    decodeEEInstruction(
      macroArithmeticInstruction(
        FP_REGISTER_ALL_FIELDS,
        2,
        1,
        3,
        VPU_ADDi)),
    "Reserved EE instruction encoding.");
}

TEST_CASE("EE decodes the complete VU macro opcode table")
{
    const std::uint16_t threeRegisterOperations[] = {
      VPU_ADD, VPU_ADDi, VPU_ADDq, VPU_ADDx, VPU_ADDy, VPU_ADDz, VPU_ADDw,
      VPU_MADD, VPU_MADDi, VPU_MADDq, VPU_MADDx, VPU_MADDy, VPU_MADDz,
      VPU_MADDw, VPU_MAX, VPU_MAXi, VPU_MAXx, VPU_MAXy, VPU_MAXz, VPU_MAXw,
      VPU_MINI, VPU_MINIi, VPU_MINIx, VPU_MINIy, VPU_MINIz, VPU_MINIw,
      VPU_MSUB, VPU_MSUBi, VPU_MSUBq, VPU_MSUBx, VPU_MSUBy, VPU_MSUBz,
      VPU_MSUBw, VPU_MUL, VPU_MULi, VPU_MULq, VPU_MULx, VPU_MULy,
      VPU_MULz, VPU_MULw, VPU_OPMSUB, VPU_SUB, VPU_SUBi, VPU_SUBq,
      VPU_SUBx, VPU_SUBy, VPU_SUBz, VPU_SUBw
    };
    for (std::uint16_t operation : threeRegisterOperations)
    {
      const bool scalar =
        operation == VPU_ADDi ||
        operation == VPU_ADDq ||
        operation == VPU_MADDi ||
        operation == VPU_MADDq ||
        operation == VPU_MAXi ||
        operation == VPU_MINIi ||
        operation == VPU_MSUBi ||
        operation == VPU_MSUBq ||
        operation == VPU_MULi ||
        operation == VPU_MULq ||
        operation == VPU_SUBi ||
        operation == VPU_SUBq;
      const std::uint8_t fields =
        operation == VPU_OPMSUB
          ? FP_REGISTER_X_FIELD |
            FP_REGISTER_Y_FIELD |
            FP_REGISTER_Z_FIELD
          : FP_REGISTER_ALL_FIELDS;
      REQUIRE(
        decodeEEInstruction(
          macroUpperInstruction(
            operation,
            true,
            scalar,
            fields)).operation ==
        EEOperation::VectorMacroArithmetic);
    }

    const std::uint16_t twoRegisterOperations[] = {
      VPU_ABS, VPU_ADDA, VPU_ADDAi, VPU_ADDAq, VPU_ADDAx, VPU_ADDAy,
      VPU_ADDAz, VPU_ADDAw, VPU_CLIP, VPU_FTOI0, VPU_FTOI4, VPU_FTOI12,
      VPU_FTOI15, VPU_ITOF0, VPU_ITOF4, VPU_ITOF12, VPU_ITOF15,
      VPU_MADDA, VPU_MADDAi, VPU_MADDAq, VPU_MADDAx, VPU_MADDAy,
      VPU_MADDAz, VPU_MADDAw, VPU_MSUBA, VPU_MSUBAi, VPU_MSUBAq,
      VPU_MSUBAx, VPU_MSUBAy, VPU_MSUBAz, VPU_MSUBAw, VPU_MULA,
      VPU_MULAi, VPU_MULAq, VPU_MULAx, VPU_MULAy, VPU_MULAz,
      VPU_MULAw, VPU_NOP, VPU_OPMULA, VPU_SUBA, VPU_SUBAi, VPU_SUBAq,
      VPU_SUBAx, VPU_SUBAy, VPU_SUBAz, VPU_SUBAw
    };
    for (std::uint16_t operation : twoRegisterOperations)
    {
      const bool scalar =
        operation == VPU_ADDAi ||
        operation == VPU_ADDAq ||
        operation == VPU_MADDAi ||
        operation == VPU_MADDAq ||
        operation == VPU_MSUBAi ||
        operation == VPU_MSUBAq ||
        operation == VPU_MULAi ||
        operation == VPU_MULAq ||
        operation == VPU_SUBAi ||
        operation == VPU_SUBAq;
      const std::uint8_t fields =
        operation == VPU_CLIP ||
        operation == VPU_OPMULA
          ? FP_REGISTER_X_FIELD |
            FP_REGISTER_Y_FIELD |
            FP_REGISTER_Z_FIELD
          : FP_REGISTER_ALL_FIELDS;
      REQUIRE(
        decodeEEInstruction(
          macroUpperInstruction(
            operation,
            false,
            scalar,
            fields)).operation ==
        EEOperation::VectorMacroArithmetic);
    }

    const std::uint32_t lowerOperations[] = {
      VPU_IADD_ENCODING |
        (UINT32_C(2) << 16) |
        (UINT32_C(1) << 11) |
        (UINT32_C(3) << 6),
      VPU_IADDI_ENCODING |
        (UINT32_C(2) << 16) |
        (UINT32_C(1) << 11) |
        (UINT32_C(3) << 6),
      VPU_IAND_ENCODING |
        (UINT32_C(2) << 16) |
        (UINT32_C(1) << 11) |
        (UINT32_C(3) << 6),
      VPU_IOR_ENCODING |
        (UINT32_C(2) << 16) |
        (UINT32_C(1) << 11) |
        (UINT32_C(3) << 6),
      VPU_ISUB_ENCODING |
        (UINT32_C(2) << 16) |
        (UINT32_C(1) << 11) |
        (UINT32_C(3) << 6),
      VPU_ILWR_ENCODING | VPU_DEST_X_BIT |
        (UINT32_C(2) << 16) |
        (UINT32_C(1) << 11),
      VPU_ISWR_ENCODING | VPU_DEST_X_BIT |
        (UINT32_C(2) << 16) |
        (UINT32_C(1) << 11),
      VPU_LQD_ENCODING | VPU_DEST_ALL_FIELDS |
        (UINT32_C(2) << 16) |
        (UINT32_C(1) << 11),
      VPU_LQI_ENCODING | VPU_DEST_ALL_FIELDS |
        (UINT32_C(2) << 16) |
        (UINT32_C(1) << 11),
      VPU_MFIR_ENCODING | VPU_DEST_ALL_FIELDS |
        (UINT32_C(2) << 16) |
        (UINT32_C(1) << 11),
      VPU_MOVE_ENCODING | VPU_DEST_ALL_FIELDS |
        (UINT32_C(2) << 16) |
        (UINT32_C(1) << 11),
      VPU_MR32_ENCODING | VPU_DEST_ALL_FIELDS |
        (UINT32_C(2) << 16) |
        (UINT32_C(1) << 11),
      VPU_MTIR_ENCODING |
        (UINT32_C(2) << 16) |
        (UINT32_C(1) << 11),
      VPU_RGET_ENCODING | VPU_DEST_ALL_FIELDS |
        (UINT32_C(2) << 16),
      VPU_RINIT_ENCODING |
        (UINT32_C(1) << 11),
      VPU_RNEXT_ENCODING | VPU_DEST_ALL_FIELDS |
        (UINT32_C(2) << 16),
      VPU_RXOR_ENCODING |
        (UINT32_C(1) << 11),
      VPU_SQD_ENCODING | VPU_DEST_ALL_FIELDS |
        (UINT32_C(2) << 16) |
        (UINT32_C(1) << 11),
      VPU_SQI_ENCODING | VPU_DEST_ALL_FIELDS |
        (UINT32_C(2) << 16) |
        (UINT32_C(1) << 11),
      VPU_DIV_ENCODING |
        (UINT32_C(2) << 16) |
        (UINT32_C(1) << 11),
      VPU_RSQRT_ENCODING |
        (UINT32_C(2) << 16) |
        (UINT32_C(1) << 11),
      VPU_SQRT_ENCODING |
        (UINT32_C(2) << 16),
      VPU_WAITQ_ENCODING
    };
    for (std::uint32_t operation : lowerOperations)
    {
      REQUIRE(
        decodeEEInstruction(
          macroLowerInstruction(operation)).operation ==
        EEOperation::VectorMacroArithmetic);
    }

  REQUIRE_THROWS_WITH(
    decodeEEInstruction(UINT32_C(0x4a00003a)),
    "Reserved EE instruction encoding.");
  REQUIRE_THROWS_WITH(
    decodeEEInstruction(
      macroLowerInstruction(
        VPU_IADD_ENCODING |
        VPU_DEST_X_BIT |
        (UINT32_C(2) << 16) |
        (UINT32_C(1) << 11) |
        (UINT32_C(3) << 6))),
    "Reserved EE instruction encoding.");
  REQUIRE_THROWS_WITH(
    decodeEEInstruction(
      macroLowerInstruction(
        VPU_IADD_ENCODING |
        (UINT32_C(18) << 16) |
        (UINT32_C(1) << 11) |
        (UINT32_C(3) << 6))),
    "Reserved EE instruction encoding.");
  REQUIRE_THROWS_WITH(
    decodeEEInstruction(
      macroLowerInstruction(
        VPU_SQRT_ENCODING |
        (UINT32_C(2) << 16) |
        (UINT32_C(1) << 11))),
    "Reserved EE instruction encoding.");
  REQUIRE_THROWS_WITH(
    decodeEEInstruction(
      macroLowerInstruction(
        VPU_RGET_ENCODING |
        VPU_DEST_ALL_FIELDS |
        (UINT32_C(2) << 16) |
        (UINT32_C(1) << 11))),
    "Reserved EE instruction encoding.");
}

TEST_CASE("EE VU macro arithmetic reuses vector masks and broadcasts")
{
  SECTION("VADD writes only selected destination fields")
  {
    NekoSystem system;
    system.vu0().loadFPRegister(1, 1, 2, 3, 4);
    system.vu0().loadFPRegister(2, 10, 20, 30, 40);
    system.vu0().loadFPRegister(3, 100, 200, 300, 400);

    runMacroAndReadResult(
      &system,
      macroArithmeticInstruction(
        FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD,
        2,
        1,
        3,
        VPU_ADD),
      3);

    const FPRegister *result = system.vu0().fpRegisterValue(3);
    REQUIRE(result->x == 11);
    REQUIRE(result->y == 22);
    REQUIRE(result->z == 300);
    REQUIRE(result->w == 400);
  }

  SECTION("VSUBw broadcasts the selected target field")
  {
    NekoSystem system;
    system.vu0().loadFPRegister(1, 10, 20, 30, 40);
    system.vu0().loadFPRegister(2, 1, 2, 3, 4);

    runMacroAndReadResult(
      &system,
      macroArithmeticInstruction(
        FP_REGISTER_ALL_FIELDS,
        2,
        1,
        3,
        VPU_SUBw),
      3);

    const FPRegister *result = system.vu0().fpRegisterValue(3);
    REQUIRE(result->x == 6);
    REQUIRE(result->y == 16);
    REQUIRE(result->z == 26);
    REQUIRE(result->w == 36);
  }
}

TEST_CASE("EE VU macro arithmetic reads I and Q scalar registers")
{
  SECTION("VADDi")
  {
    NekoSystem system;
    system.vu0().loadFPRegister(1, 1, 2, 3, 4);
    system.vu0().loadIRegister(5);

    runMacroAndReadResult(
      &system,
      macroArithmeticInstruction(
        FP_REGISTER_ALL_FIELDS,
        0,
        1,
        3,
        VPU_ADDi),
      3);

    const FPRegister *result = system.vu0().fpRegisterValue(3);
    REQUIRE(result->x == 6);
    REQUIRE(result->y == 7);
    REQUIRE(result->z == 8);
    REQUIRE(result->w == 9);
  }

  SECTION("VSUBq")
  {
    NekoSystem system;
    system.vu0().loadFPRegister(1, 10, 20, 30, 40);
    system.vu0().loadQRegister(3);

    runMacroAndReadResult(
      &system,
      macroArithmeticInstruction(
        FP_REGISTER_ALL_FIELDS,
        0,
        1,
        3,
        VPU_SUBq),
      3);

    const FPRegister *result = system.vu0().fpRegisterValue(3);
    REQUIRE(result->x == 7);
    REQUIRE(result->y == 17);
    REQUIRE(result->z == 27);
    REQUIRE(result->w == 37);
  }
}

TEST_CASE("EE VU macro mode reuses upper arithmetic families")
  {
    NekoSystem system;
    system.vu0().loadFPRegister(1, 2, 3, 4, 5);
    system.vu0().loadFPRegister(2, 10, 20, 30, 40);
    system.eeBus().write32(
      0,
      macroUpperInstruction(VPU_MULA, false));
    system.eeBus().write32(
      4,
      macroUpperInstruction(VPU_MADD, true));
    system.eeBus().write32(
      8,
      quadwordMoveFromCOP2(2, 3));
    system.eeCore().startExecution(0);

    REQUIRE(system.stepEEInstruction(16).instructions == 1);
    REQUIRE(system.stepEEInstruction(16).instructions == 1);
    REQUIRE(system.stepEEInstruction(64).instructions == 1);

    const FPRegister *result =
      system.vu0().fpRegisterValue(3);
    REQUIRE(result->x == 40);
    REQUIRE(result->y == 120);
    REQUIRE(result->z == 240);
    REQUIRE(result->w == 400);
  }

  TEST_CASE("EE VU macro mode reuses conversion movement and random families")
  {
    SECTION("VFTOI and VMOVE use their existing vector pipelines")
    {
      NekoSystem system;
      system.vu0().loadFPRegister(1, 2, -3, 4, -5);
      system.eeBus().write32(
        0,
        macroUpperInstruction(VPU_FTOI0, false));
      system.eeBus().write32(
        4,
        macroLowerInstruction(
          VPU_MOVE_ENCODING |
          VPU_DEST_ALL_FIELDS |
          (UINT32_C(3) << 16) |
          (UINT32_C(2) << 11)));
      system.eeBus().write32(
        8,
        quadwordMoveFromCOP2(2, 3));
      system.eeCore().startExecution(0);

      REQUIRE(system.stepEEInstruction(16).instructions == 1);
      REQUIRE(system.stepEEInstruction(32).instructions == 1);
      REQUIRE(system.stepEEInstruction(64).instructions == 1);
      const FPRegister *result =
        system.vu0().fpRegisterValue(3);
      REQUIRE(result->x.bits() == 2);
      REQUIRE(result->y.bits() == UINT32_C(0xfffffffd));
      REQUIRE(result->z.bits() == 4);
      REQUIRE(result->w.bits() == UINT32_C(0xfffffffb));
    }

    SECTION("VRINIT and VRGET share the random unit")
    {
      NekoSystem system;
      system.vu0().loadFPRegisterBits(
        1,
        UINT32_C(0x3f800123),
        0,
        0,
        0);
      system.eeBus().write32(
        0,
        macroLowerInstruction(
          VPU_RINIT_ENCODING |
          (UINT32_C(1) << 11)));
      system.eeBus().write32(
        4,
        macroLowerInstruction(
          VPU_RGET_ENCODING |
          VPU_DEST_ALL_FIELDS |
          (UINT32_C(2) << 16)));
      system.eeBus().write32(
        8,
        quadwordMoveFromCOP2(2, 2));
      system.eeCore().startExecution(0);

      REQUIRE(system.stepEEInstruction(16).instructions == 1);
      REQUIRE(system.stepEEInstruction(16).instructions == 1);
      REQUIRE(system.stepEEInstruction(64).instructions == 1);
      const FPRegister *result =
        system.vu0().fpRegisterValue(2);
      REQUIRE(result->x.bits() == UINT32_C(0x3f800123));
      REQUIRE(result->y.bits() == UINT32_C(0x3f800123));
      REQUIRE(result->z.bits() == UINT32_C(0x3f800123));
      REQUIRE(result->w.bits() == UINT32_C(0x3f800123));
    }
  }

  TEST_CASE("EE VU macro mode executes integer and memory families")
  {
    SECTION("VI results interlock with CFC2")
    {
      NekoSystem system;
      system.vu0().loadIntRegister(1, 7);
      system.vu0().loadIntRegister(2, 5);
      system.eeBus().write32(
        0,
        macroLowerInstruction(
          VPU_IADD_ENCODING |
          (UINT32_C(2) << 16) |
          (UINT32_C(1) << 11) |
          (UINT32_C(3) << 6)));
      system.eeBus().write32(
        4,
        controlMoveFromCOP2(2, 3));
      system.eeCore().startExecution(0);

      REQUIRE(system.stepEEInstruction(8).instructions == 1);
      const EEExecutionResult read =
        system.stepEEInstruction(32);
      REQUIRE(read.instructions == 1);
      REQUIRE(system.eeCore().generalRegister(2).low == 12);
    }

    SECTION("VLQI loads VU memory and increments its address register")
    {
      NekoSystem system;
      system.vu0().loadIntRegister(1, 1);
      system.vu0().writeDataQuadword(
        1,
        {{1, 2, 3, 4}});
      system.eeBus().write32(
        0,
        macroLowerInstruction(
          VPU_LQI_ENCODING |
          VPU_DEST_ALL_FIELDS |
          (UINT32_C(3) << 16) |
          (UINT32_C(1) << 11)));
      system.eeBus().write32(
        4,
        quadwordMoveFromCOP2(2, 3));
      system.eeBus().write32(
        8,
        controlMoveFromCOP2(3, 1));
      system.eeCore().startExecution(0);

      REQUIRE(system.stepEEInstruction(8).instructions == 1);
      REQUIRE(system.vu0().macroModeActive());
      REQUIRE(
        system.vu0().macroRegisterWritePending(
          3,
          FP_REGISTER_ALL_FIELDS));
      REQUIRE(system.stepEEInstruction(32).instructions == 1);
      REQUIRE(system.stepEEInstruction(16).instructions == 1);
      REQUIRE(system.eeCore().generalRegister(2).low == UINT64_C(0x0000000200000001));
      REQUIRE(system.eeCore().generalRegister(2).high == UINT64_C(0x0000000400000003));
      REQUIRE(system.eeCore().generalRegister(3).low == 2);
    }
  }

  TEST_CASE("EE VU macro VWAITQ synchronizes divider results")
  {
    NekoSystem system;
    system.vu0().loadFPRegister(1, 8, 0, 0, 0);
    system.vu0().loadFPRegister(2, 2, 0, 0, 0);
    system.eeBus().write32(
      0,
      macroLowerInstruction(
        VPU_DIV_ENCODING |
        (UINT32_C(2) << 16) |
        (UINT32_C(1) << 11)));
    system.eeBus().write32(
      4,
      macroLowerInstruction(VPU_WAITQ_ENCODING));
    system.eeBus().write32(
      8,
      macroUpperInstruction(VPU_ADDq, true, true));
    system.eeBus().write32(
      12,
      quadwordMoveFromCOP2(2, 3));
    system.eeCore().startExecution(0);

    REQUIRE(system.stepEEInstruction(8).instructions == 1);
    REQUIRE(system.stepEEInstruction(16).instructions == 1);
    const EEExecutionResult added =
      system.stepEEInstruction(64);
    REQUIRE(added.instructions == 1);
    REQUIRE(added.masterCycles > 1);
    REQUIRE(system.stepEEInstruction(64).instructions == 1);
    REQUIRE(system.vu0().qRegisterBits() == UINT32_C(0x40800000));

    const FPRegister *result =
      system.vu0().fpRegisterValue(3);
    REQUIRE(result->x == 12);
    REQUIRE(result->y == 4);
    REQUIRE(result->z == 4);
    REQUIRE(result->w == 4);
  }

TEST_CASE("EE VU macro divider instructions share one resource")
{
  NekoSystem system;
  system.vu0().loadFPRegister(1, 8, 0, 0, 0);
  system.vu0().loadFPRegister(2, 2, 0, 0, 0);
  system.eeBus().write32(
    0,
    macroLowerInstruction(
      VPU_DIV_ENCODING |
      (UINT32_C(2) << 16) |
      (UINT32_C(1) << 11)));
  system.eeBus().write32(
    4,
    macroLowerInstruction(
      VPU_SQRT_ENCODING |
      (UINT32_C(2) << 16)));
  system.eeBus().write32(
    8,
    macroLowerInstruction(VPU_WAITQ_ENCODING));
  system.eeCore().startExecution(0);

  REQUIRE(system.stepEEInstruction(8).instructions == 1);
  const EEExecutionResult squareRoot =
    system.stepEEInstruction(64);
  REQUIRE(squareRoot.instructions == 1);
  const EEExecutionResult waited =
    system.stepEEInstruction(64);
  REQUIRE(waited.instructions == 1);
  REQUIRE(waited.masterCycles > 2);
  system.runMasterCycles(64);
  REQUIRE(
    system.vu0().qRegisterBits() ==
    UINT32_C(0x3fb504f3));
}

  TEST_CASE("EE writes do not create macro WAR hazards")
  {
    SECTION("QMTC2 preserves an already-issued VF source")
    {
      NekoSystem system;
      system.vu0().loadFPRegister(1, 1, 2, 3, 4);
      system.vu0().loadFPRegister(2, 10, 20, 30, 40);
      system.eeCore().setGeneralRegister(
        4,
        EERegister128{
          UINT64_C(0x42c8000042c80000),
          UINT64_C(0x42c8000042c80000)});
      system.eeBus().write32(
        0,
        macroArithmeticInstruction(
          FP_REGISTER_ALL_FIELDS,
          2,
          1,
          3,
          VPU_ADD));
      system.eeBus().write32(
        4,
        quadwordMoveToCOP2(4, 1));
      system.eeBus().write32(
        8,
        quadwordMoveFromCOP2(2, 3));
      system.eeCore().startExecution(0);

      REQUIRE(system.stepEEInstruction(8).instructions == 1);
      const EEExecutionResult written =
        system.stepEEInstruction(32);
      REQUIRE(written.instructions == 1);
      REQUIRE(written.masterCycles == 1);
      REQUIRE(system.stepEEInstruction(32).instructions == 1);
      const FPRegister *result =
        system.vu0().fpRegisterValue(3);
      REQUIRE(result->x == 11);
      REQUIRE(result->y == 22);
      REQUIRE(result->z == 33);
      REQUIRE(result->w == 44);
    }

    SECTION("CTC2 does not interlock on the I register")
    {
      NekoSystem system;
      system.vu0().loadFPRegister(1, 1, 2, 3, 4);
      system.vu0().loadIRegister(5);
      system.eeCore().setGeneralRegister(
        4,
        EERegister128{UINT64_C(0x41200000), 0});
      system.eeBus().write32(
        0,
        macroUpperInstruction(VPU_ADDi, true, true));
      system.eeBus().write32(
        4,
        controlMoveToCOP2(4, 21));
      system.eeBus().write32(
        8,
        quadwordMoveFromCOP2(2, 3));
      system.eeCore().startExecution(0);

      REQUIRE(system.stepEEInstruction(8).instructions == 1);
      const EEExecutionResult written =
        system.stepEEInstruction(32);
      REQUIRE(written.instructions == 1);
      REQUIRE(written.masterCycles == 1);
      REQUIRE(system.stepEEInstruction(32).instructions == 1);
      const FPRegister *result =
        system.vu0().fpRegisterValue(3);
      REQUIRE(result->x == 11);
      REQUIRE(result->y == 12);
      REQUIRE(result->z == 13);
      REQUIRE(result->w == 14);
      REQUIRE(system.vu0().iRegisterBits() == UINT32_C(0x41200000));
    }
  }

TEST_CASE("EE to VU transfers stall the next macro issue once")
{
  SECTION("A transfer immediately before a macro adds one issue cycle")
  {
    NekoSystem system;
    system.vu0().loadFPRegister(1, 1, 2, 3, 4);
    system.vu0().loadFPRegister(2, 10, 20, 30, 40);
    system.eeCore().setGeneralRegister(
      4,
      EERegister128{
        UINT64_C(0x42c8000042c80000),
        UINT64_C(0x42c8000042c80000)});
    system.eeBus().write32(
      0,
      quadwordMoveToCOP2(4, 8));
    system.eeBus().write32(
      4,
      macroArithmeticInstruction(
        FP_REGISTER_ALL_FIELDS,
        2,
        1,
        3,
        VPU_ADD));
    system.eeCore().startExecution(0);

    REQUIRE(system.stepEEInstruction(8).instructions == 1);
    const EEExecutionResult macro =
      system.stepEEInstruction(8);
    REQUIRE(macro.instructions == 1);
    REQUIRE(macro.masterCycles == 2);
  }

  SECTION("A transfer during micro mode does not leak into macro resume")
  {
    const auto resumeCycles =
      [](bool transfer)
      {
        NekoSystem system;
        system.vu0().loadFPRegister(1, 1, 2, 3, 4);
        system.vu0().loadFPRegister(2, 10, 20, 30, 40);
        system.vu0().writeMicroInstruction(
          0,
          VPU_LOWER_NOP,
          VPU_E_BIT | VPU_NOP);
        system.vu0().writeMicroInstruction(
          1,
          VPU_LOWER_NOP,
          VPU_NOP);
        system.vu0().startMicroMode();
        system.eeCore().setGeneralRegister(
          4,
          EERegister128{
            UINT64_C(0x42c8000042c80000),
            UINT64_C(0x42c8000042c80000)});
        system.eeBus().write32(
          0,
          transfer ? quadwordMoveToCOP2(4, 8) : 0);
        system.eeBus().write32(
          4,
          macroArithmeticInstruction(
            FP_REGISTER_ALL_FIELDS,
            2,
            1,
            3,
            VPU_ADD));
        system.eeCore().startExecution(0);

        REQUIRE(
          system.stepEEInstruction(8).instructions ==
          1);
        const EEExecutionResult macro =
          system.stepEEInstruction(32);
        REQUIRE(macro.instructions == 1);
        return macro.masterCycles;
      };

    REQUIRE(resumeCycles(true) == resumeCycles(false));
  }
}

TEST_CASE("COP2 transfers honor cross-file macro hazards")
{
  SECTION("CFC2 waits for a same-numbered VF result")
  {
    NekoSystem system;
    system.vu0().loadFPRegister(1, 1, 2, 3, 4);
    system.vu0().loadFPRegister(2, 10, 20, 30, 40);
    system.vu0().loadIntRegister(3, 7);
    system.eeBus().write32(
      0,
      macroArithmeticInstruction(
        FP_REGISTER_ALL_FIELDS,
        2,
        1,
        3,
        VPU_ADD));
    system.eeBus().write32(
      4,
      controlMoveFromCOP2(2, 3));
    system.eeCore().startExecution(0);

    REQUIRE(system.stepEEInstruction(8).instructions == 1);
    const EEExecutionResult read =
      system.stepEEInstruction(32);
    REQUIRE(read.instructions == 1);
    REQUIRE(read.masterCycles > 1);
    REQUIRE(system.eeCore().generalRegister(2).low == 7);
  }

  SECTION("QMFC2 waits for a same-numbered VI result")
  {
    NekoSystem system;
    system.vu0().loadFPRegister(3, 1, 2, 3, 4);
    system.vu0().loadIntRegister(1, 7);
    system.vu0().loadIntRegister(2, 5);
    system.eeBus().write32(
      0,
      macroLowerInstruction(
        VPU_IADD_ENCODING |
        (UINT32_C(2) << 16) |
        (UINT32_C(1) << 11) |
        (UINT32_C(3) << 6)));
    system.eeBus().write32(
      4,
      quadwordMoveFromCOP2(2, 3));
    system.eeCore().startExecution(0);

    REQUIRE(system.stepEEInstruction(8).instructions == 1);
    const EEExecutionResult read =
      system.stepEEInstruction(32);
    REQUIRE(read.instructions == 1);
    REQUIRE(read.masterCycles > 1);
    REQUIRE(
      system.eeCore().generalRegister(2).low ==
      UINT64_C(0x400000003f800000));
  }
}

TEST_CASE("EE VU macro arithmetic waits for micro mode and rejects Stop")
{
  SECTION("A running microprogram stalls macro issue")
  {
    NekoSystem system;
    system.vu0().writeMicroInstruction(
      0,
      VPU_LOWER_NOP,
      VPU_E_BIT | VPU_NOP);
    system.vu0().writeMicroInstruction(
      1,
      VPU_LOWER_NOP,
      VPU_NOP);
    system.vu0().startMicroMode();
    system.eeBus().write32(
      0,
      macroArithmeticInstruction(
        FP_REGISTER_ALL_FIELDS,
        2,
        1,
        3,
        VPU_ADD));
    system.eeCore().startExecution(0);

    const EEExecutionResult result =
      system.stepEEInstruction(32);

    REQUIRE(result.instructions == 1);
    REQUIRE(result.masterCycles > 1);
    REQUIRE(system.eeCore().programCounter() == 4);
    REQUIRE(system.vu0().clockActive());
  }

  SECTION("Stop-state macro execution is deterministic undefined behavior")
  {
    NekoSystem system;
    system.vu0().forceBreak();
    system.eeBus().write32(
      0,
      macroArithmeticInstruction(
        FP_REGISTER_ALL_FIELDS,
        2,
        1,
        3,
        VPU_ADD));
    system.eeCore().startExecution(0);
    system.clockMasterCycle();

    REQUIRE(
      system.eeCore().stopReason() ==
      EEStopReason::UndefinedOperation);
    REQUIRE(system.eeCore().programCounter() == 0);
  }
}

TEST_CASE("EE VU macro arithmetic preserves pipeline dependencies")
{
  NekoSystem system;
  system.vu0().loadFPRegister(1, 10, 20, 30, 40);
  system.vu0().loadFPRegister(2, 1, 2, 3, 4);
  system.eeBus().write32(
    0,
    macroArithmeticInstruction(
      FP_REGISTER_ALL_FIELDS,
      2,
      1,
      3,
      VPU_ADD));
  system.eeBus().write32(
    4,
    macroArithmeticInstruction(
      FP_REGISTER_ALL_FIELDS,
      2,
      3,
      4,
      VPU_SUB));
  system.eeBus().write32(8, quadwordMoveFromCOP2(2, 4));
  system.eeCore().startExecution(0);

  REQUIRE(system.stepEEInstruction(8).instructions == 1);
  REQUIRE(system.stepEEInstruction(16).instructions == 1);
  REQUIRE(system.stepEEInstruction(32).instructions == 1);

  const FPRegister *result = system.vu0().fpRegisterValue(4);
  REQUIRE(result->x == 10);
  REQUIRE(result->y == 20);
  REQUIRE(result->z == 30);
  REQUIRE(result->w == 40);
}

TEST_CASE("VU macro hazards use whole register numbers")
{
  SECTION("Different VF fields still conflict")
  {
    const auto secondIssueCycles =
      [](std::uint8_t sourceRegister)
      {
        NekoSystem system;
        system.vu0().loadFPRegister(1, 1, 2, 3, 4);
        system.vu0().loadFPRegister(2, 10, 20, 30, 40);
        system.vu0().loadFPRegister(3, 100, 200, 300, 400);
        system.vu0().loadFPRegister(5, 100, 200, 300, 400);
        system.eeBus().write32(
          0,
          macroArithmeticInstruction(
            FP_REGISTER_X_FIELD,
            2,
            1,
            3,
            VPU_ADD));
        system.eeBus().write32(
          4,
          macroArithmeticInstruction(
            FP_REGISTER_Y_FIELD,
            2,
            sourceRegister,
            4,
            VPU_ADD));
        system.eeCore().startExecution(0);
        REQUIRE(
          system.stepEEInstruction(8).instructions ==
          1);
        const EEExecutionResult second =
          system.stepEEInstruction(32);
        REQUIRE(second.instructions == 1);
        return second.masterCycles;
      };

    const std::uint64_t conflicting =
      secondIssueCycles(3);
    const std::uint64_t independent =
      secondIssueCycles(5);
    REQUIRE(conflicting > independent);
  }

  SECTION("VF and VI registers with the same number conflict")
  {
    const auto integerIssueCycles =
      [](std::uint8_t sourceRegister)
      {
        NekoSystem system;
        system.vu0().loadFPRegister(1, 1, 2, 3, 4);
        system.vu0().loadFPRegister(2, 10, 20, 30, 40);
        system.vu0().loadIntRegister(3, 7);
        system.vu0().loadIntRegister(4, 7);
        system.vu0().loadIntRegister(5, 5);
        system.eeBus().write32(
          0,
          macroArithmeticInstruction(
            FP_REGISTER_ALL_FIELDS,
            2,
            1,
            3,
            VPU_ADD));
        system.eeBus().write32(
          4,
          macroLowerInstruction(
            VPU_IADD_ENCODING |
            (UINT32_C(5) << 16) |
            (static_cast<std::uint32_t>(
              sourceRegister) << 11) |
            (UINT32_C(6) << 6)));
        system.eeCore().startExecution(0);
        REQUIRE(
          system.stepEEInstruction(8).instructions ==
          1);
        const EEExecutionResult second =
          system.stepEEInstruction(32);
        REQUIRE(second.instructions == 1);
        return second.masterCycles;
      };

    const std::uint64_t conflicting =
      integerIssueCycles(3);
    const std::uint64_t independent =
      integerIssueCycles(4);
    REQUIRE(conflicting > independent);
  }
}

TEST_CASE("EE VU macro arithmetic applies issue backpressure")
{
  NekoSystem system;
  system.vu0().loadFPRegister(1, 1, 2, 3, 4);
  system.vu0().loadFPRegister(2, 10, 20, 30, 40);
  constexpr std::uint32_t INSTRUCTION_COUNT = 20;
  for (std::uint32_t index = 0;
       index < INSTRUCTION_COUNT;
       ++index)
  {
    system.eeBus().write32(
      index * 4,
      macroArithmeticInstruction(
        FP_REGISTER_ALL_FIELDS,
        2,
        1,
        static_cast<std::uint8_t>(3 + index),
        VPU_ADD));
  }
  system.eeBus().write32(
    INSTRUCTION_COUNT * 4,
    quadwordMoveFromCOP2(2, 22));
  system.eeCore().startExecution(0);

  for (std::uint32_t index = 0;
       index <= INSTRUCTION_COUNT;
       ++index)
  {
    REQUIRE(
      system.stepEEInstruction(64).instructions ==
      1);
  }

  const FPRegister *result =
    system.vu0().fpRegisterValue(22);
  REQUIRE(result->x == 11);
  REQUIRE(result->y == 22);
  REQUIRE(result->z == 33);
  REQUIRE(result->w == 44);
}

TEST_CASE("EE COP2 transfers wait for pending macro writes")
{
  NekoSystem system;
  system.vu0().loadFPRegister(1, 1, 2, 3, 4);
  system.vu0().loadFPRegister(2, 10, 20, 30, 40);
  system.eeCore().setGeneralRegister(
    1,
    EERegister128{UINT64_C(0x100), 0});
  system.eeBus().write32(
    0,
    macroArithmeticInstruction(
      FP_REGISTER_ALL_FIELDS,
      2,
      1,
      3,
      VPU_ADD));
  system.eeBus().write32(
    4,
    storeQuadwordFromCOP2(1, 3));
  system.eeCore().startExecution(0);

  REQUIRE(system.stepEEInstruction(8).instructions == 1);
  const EEExecutionResult stored =
    system.stepEEInstruction(32);
  REQUIRE(stored.instructions == 1);
  REQUIRE(stored.masterCycles > 1);
  REQUIRE(system.eeBus().read32(0x100) == UINT32_C(0x41300000));
  REQUIRE(system.eeBus().read32(0x104) == UINT32_C(0x41b00000));
  REQUIRE(system.eeBus().read32(0x108) == UINT32_C(0x42040000));
  REQUIRE(system.eeBus().read32(0x10c) == UINT32_C(0x42300000));
}

TEST_CASE("EE VU macro arithmetic flags use only calculated fields")
{
  SECTION("Unselected lanes clear current MAC flags")
  {
    NekoSystem system;
    system.vu0().loadFPRegister(1, -1, 0, 0, 0);
    system.vu0().loadFPRegister(2, 0, 0, 0, 0);
    system.vu0().loadFPRegister(3, 0, 0, 0, 0);

    runMacroAndReadResult(
      &system,
      macroArithmeticInstruction(
        FP_REGISTER_X_FIELD,
        2,
        1,
        3,
        VPU_ADD),
      3);

    REQUIRE(
      system.vu0().macFlagsValue() ==
      VPU_FLAG_SX);
  }

  SECTION("VF00 suppresses the write but not flag generation")
  {
    NekoSystem system;
    system.vu0().loadFPRegister(1, -1, 0, 0, 0);
    system.vu0().loadFPRegister(2, 0, 0, 0, 0);
    system.eeBus().write32(
      0,
      macroArithmeticInstruction(
        FP_REGISTER_X_FIELD,
        2,
        1,
        0,
        VPU_ADD));
    system.eeCore().startExecution(0);
    REQUIRE(system.stepEEInstruction(8).instructions == 1);
    for (std::uint8_t cycle = 0;
         cycle < 16 && system.vu0().clockActive();
         ++cycle)
    {
      system.clockMasterCycle();
    }
    REQUIRE_FALSE(system.vu0().clockActive());

    REQUIRE(
      system.vu0().macFlagsValue() ==
      VPU_FLAG_SX);
    const FPRegister *vf00 =
      system.vu0().fpRegisterValue(0);
    REQUIRE(vf00->x == 0);
    REQUIRE(vf00->y == 0);
    REQUIRE(vf00->z == 0);
    REQUIRE(vf00->w == 1);
  }
}

TEST_CASE("VU status does not report macro pipelines as micro execution")
{
  NekoSystem system;
  system.eeBus().write32(
    0,
    macroArithmeticInstruction(
      FP_REGISTER_ALL_FIELDS,
      2,
      1,
      3,
      VPU_ADD));
  system.eeBus().write32(
    4,
    controlMoveFromCOP2(2, 29));
  system.eeCore().startExecution(0);

  REQUIRE(system.stepEEInstruction(8).instructions == 1);
  REQUIRE(system.vu0().macroModeActive());
  REQUIRE(system.stepEEInstruction(8).instructions == 1);
  REQUIRE(
    (system.eeCore().generalRegister(2).low & 1) ==
    0);
}

TEST_CASE("VCALLMS overlaps a preceding VU macro pipeline")
{
  NekoSystem system;
  system.vu0().loadFPRegister(1, 1, 2, 3, 4);
  system.vu0().loadFPRegister(2, 10, 20, 30, 40);
  system.vu0().writeMicroInstruction(
    0,
    VPU_LOWER_NOP,
    VPU_E_BIT |
    (macroArithmeticInstruction(
      FP_REGISTER_ALL_FIELDS,
      2,
      3,
      4,
      VPU_SUB) &
     UINT32_C(0x01ffffff)));
  system.vu0().writeMicroInstruction(
    1,
    VPU_LOWER_NOP,
    VPU_NOP);
  system.eeBus().write32(
    0,
    macroArithmeticInstruction(
      FP_REGISTER_ALL_FIELDS,
      2,
      1,
      3,
      VPU_ADD));
  system.eeBus().write32(4, vectorCallInstruction(0));
  system.eeBus().write32(
    8,
    quadwordMoveFromCOP2(2, 4));
  system.eeCore().startExecution(0);

  REQUIRE(system.stepEEInstruction(8).instructions == 1);
  const EEExecutionResult called =
    system.stepEEInstruction(8);
  REQUIRE(called.instructions == 1);
  REQUIRE(called.masterCycles < 8);
  REQUIRE(system.stepEEInstruction(64).instructions == 1);

  const FPRegister *result =
    system.vu0().fpRegisterValue(4);
  REQUIRE(result->x == 1);
  REQUIRE(result->y == 2);
  REQUIRE(result->z == 3);
  REQUIRE(result->w == 4);
}

TEST_CASE("VU macro issue overlaps a terminating micro pipeline")
{
  NekoSystem system;
  system.vu0().loadFPRegister(1, 1, 2, 3, 4);
  system.vu0().loadFPRegister(2, 10, 20, 30, 40);
  system.vu0().writeMicroInstruction(
    0,
    VPU_LOWER_NOP,
    VPU_E_BIT |
    (macroArithmeticInstruction(
      FP_REGISTER_ALL_FIELDS,
      2,
      1,
      3,
      VPU_ADD) &
     UINT32_C(0x01ffffff)));
  system.vu0().writeMicroInstruction(
    1,
    VPU_LOWER_NOP,
    VPU_NOP);
  system.vu0().startMicroMode();
  system.eeBus().write32(
    0,
    macroArithmeticInstruction(
      FP_REGISTER_ALL_FIELDS,
      2,
      3,
      4,
      VPU_ADD));
  system.eeBus().write32(
    4,
    quadwordMoveFromCOP2(2, 4));
  system.eeCore().startExecution(0);

  const EEExecutionResult issued =
    system.stepEEInstruction(32);
  REQUIRE(issued.instructions == 1);
  REQUIRE(system.vu0().macroModeActive());
  REQUIRE(system.stepEEInstruction(64).instructions == 1);

  const FPRegister *result =
    system.vu0().fpRegisterValue(4);
  REQUIRE(result->x == 21);
  REQUIRE(result->y == 42);
  REQUIRE(result->z == 63);
  REQUIRE(result->w == 84);
}

TEST_CASE("VU lower-style macro issue sees final micro writeback")
{
  NekoSystem system;
  system.vu0().loadFPRegister(1, 1, 2, 3, 4);
  system.vu0().loadFPRegister(2, 10, 20, 30, 40);
  system.vu0().writeMicroInstruction(
    0,
    VPU_LOWER_NOP,
    VPU_E_BIT |
    (macroArithmeticInstruction(
      FP_REGISTER_ALL_FIELDS,
      2,
      1,
      3,
      VPU_ADD) &
     UINT32_C(0x01ffffff)));
  system.vu0().writeMicroInstruction(
    1,
    VPU_LOWER_NOP,
    VPU_NOP);
  system.vu0().startMicroMode();
  system.eeBus().write32(
    0,
    macroLowerInstruction(
      VPU_MOVE_ENCODING |
      VPU_DEST_ALL_FIELDS |
      (UINT32_C(4) << 16) |
      (UINT32_C(3) << 11)));
  system.eeBus().write32(
    4,
    quadwordMoveFromCOP2(2, 4));
  system.eeCore().startExecution(0);

  REQUIRE(system.stepEEInstruction(32).instructions == 1);
  REQUIRE(system.vu0().macroModeActive());
  REQUIRE(system.stepEEInstruction(64).instructions == 1);
  const FPRegister *result =
    system.vu0().fpRegisterValue(4);
  REQUIRE(result->x == 11);
  REQUIRE(result->y == 22);
  REQUIRE(result->z == 33);
  REQUIRE(result->w == 44);
}

TEST_CASE("Active EE VU macro arithmetic survives save-state restoration")
{
  NekoSystem system;
  system.vu0().loadFPRegister(1, 1, 2, 3, 4);
  system.vu0().loadFPRegister(2, 10, 20, 30, 40);
  system.eeBus().write32(
    0,
    macroArithmeticInstruction(
      FP_REGISTER_ALL_FIELDS,
      2,
      1,
      3,
      VPU_ADD));
  system.eeBus().write32(4, quadwordMoveFromCOP2(2, 3));
  system.eeCore().startExecution(0);
  REQUIRE(system.stepEEInstruction(8).instructions == 1);
  const std::vector<std::uint8_t> saved = system.saveState();

  REQUIRE(system.stepEEInstruction(32).instructions == 1);
  system.vu0().loadFPRegister(3, 0, 0, 0, 0);
  system.loadState(saved);
  REQUIRE(system.stepEEInstruction(32).instructions == 1);

  const FPRegister *result = system.vu0().fpRegisterValue(3);
  REQUIRE(result->x == 11);
  REQUIRE(result->y == 22);
  REQUIRE(result->z == 33);
  REQUIRE(result->w == 44);
}

TEST_CASE("Macro source snapshots survive save-state restoration")
{
  NekoSystem system;
  system.vu0().loadFPRegister(1, 1, 2, 3, 4);
  system.vu0().loadFPRegister(2, 10, 20, 30, 40);
  system.eeCore().setGeneralRegister(
    4,
    EERegister128{
      UINT64_C(0x42c8000042c80000),
      UINT64_C(0x42c8000042c80000)});
  system.eeBus().write32(
    0,
    macroArithmeticInstruction(
      FP_REGISTER_ALL_FIELDS,
      2,
      1,
      3,
      VPU_ADD));
  system.eeBus().write32(
    4,
    quadwordMoveToCOP2(4, 1));
  system.eeBus().write32(
    8,
    quadwordMoveFromCOP2(2, 3));
  system.eeCore().startExecution(0);

  REQUIRE(system.stepEEInstruction(8).instructions == 1);
  REQUIRE(system.stepEEInstruction(8).instructions == 1);
  const std::vector<std::uint8_t> saved =
    system.saveState();
  REQUIRE(system.stepEEInstruction(32).instructions == 1);

  system.loadState(saved);
  REQUIRE(system.stepEEInstruction(32).instructions == 1);
  const FPRegister *result =
    system.vu0().fpRegisterValue(3);
  REQUIRE(result->x == 11);
  REQUIRE(result->y == 22);
  REQUIRE(result->z == 33);
  REQUIRE(result->w == 44);
  REQUIRE(system.vu0().fpRegisterValue(1)->x == 100);
}

TEST_CASE("Accepted lower-style VU macro issue survives save-state restoration")
{
  NekoSystem system;
  system.vu0().loadIntRegister(1, 7);
  system.vu0().loadIntRegister(2, 5);
  system.eeBus().write32(
    0,
    macroLowerInstruction(
      VPU_IADD_ENCODING |
      (UINT32_C(2) << 16) |
      (UINT32_C(1) << 11) |
      (UINT32_C(3) << 6)));
  system.eeBus().write32(
    4,
    controlMoveFromCOP2(2, 3));
  system.eeCore().startExecution(0);
  REQUIRE(system.stepEEInstruction(8).instructions == 1);
  const std::vector<std::uint8_t> saved =
    system.saveState();

  REQUIRE(system.stepEEInstruction(32).instructions == 1);
  REQUIRE(system.eeCore().generalRegister(2).low == 12);
  system.loadState(saved);
  REQUIRE(system.stepEEInstruction(32).instructions == 1);
  REQUIRE(system.eeCore().generalRegister(2).low == 12);
}
