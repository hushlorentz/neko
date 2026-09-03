#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "ee_core.hpp"
#include "neko_system.hpp"
#include "vpu_field_mask.hpp"
#include "vpu_flags.hpp"
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
  REQUIRE(system.stepEEInstruction(8).instructions == 1);
  REQUIRE(system.stepEEInstruction(32).instructions == 1);

  const FPRegister *result = system.vu0().fpRegisterValue(4);
  REQUIRE(result->x == 10);
  REQUIRE(result->y == 20);
  REQUIRE(result->z == 30);
  REQUIRE(result->w == 40);
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
