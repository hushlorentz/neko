#include <cstdint>

#include "catch.hpp"
#include "ee_core.hpp"
#include "neko_system.hpp"

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

  void runInstruction(
    NekoSystem *system,
    std::uint32_t instruction)
  {
    system->eeBus().write32(0, instruction);
    system->eeCore().startExecution(0);
    system->clockMasterCycle();
  }

  void setRegister(
    EECore *core,
    std::uint8_t index,
    std::uint64_t low,
    std::uint64_t high =
      UINT64_C(0xfeedfacecafebeef))
  {
    core->setGeneralRegister(index, {low, high});
  }
}

TEST_CASE("EE immediate integer execution")
{
  NekoSystem system;
  EECore &core = system.eeCore();

  SECTION("LUI and logical immediates preserve the upper doubleword")
  {
    setRegister(&core, 1, UINT64_C(0x123456789abcdef0));
    runInstruction(
      &system,
      immediateInstruction(0x0f, 0, 1, 0x8000));

    REQUIRE(
      core.generalRegister(1).low ==
      UINT64_C(0xffffffff80000000));
    REQUIRE(
      core.generalRegister(1).high ==
      UINT64_C(0xfeedfacecafebeef));

    runInstruction(
      &system,
      immediateInstruction(0x0d, 1, 1, 0x1234));
    REQUIRE(
      core.generalRegister(1).low ==
      UINT64_C(0xffffffff80001234));

    runInstruction(
      &system,
      immediateInstruction(0x0c, 1, 1, 0x00ff));
    REQUIRE(core.generalRegister(1).low == 0x34);

    runInstruction(
      &system,
      immediateInstruction(0x0e, 1, 1, 0xffff));
    REQUIRE(core.generalRegister(1).low == 0xffcb);
  }

  SECTION("ADDIU sign extends its word result")
  {
    setRegister(&core, 1, 1);
    setRegister(&core, 2, 0);

    runInstruction(
      &system,
      immediateInstruction(0x09, 1, 2, 0xfffe));

    REQUIRE(
      core.generalRegister(2).low ==
      UINT64_C(0xffffffffffffffff));
    REQUIRE(
      core.generalRegister(2).high ==
      UINT64_C(0xfeedfacecafebeef));
  }

  SECTION("DADDIU performs 64-bit modulo arithmetic")
  {
    setRegister(&core, 1, UINT64_MAX);

    runInstruction(
      &system,
      immediateInstruction(0x19, 1, 2, 1));

    REQUIRE(core.generalRegister(2).low == 0);
  }

  SECTION("Signed and unsigned immediate comparisons use 64 bits")
  {
    setRegister(&core, 1, UINT64_MAX);

    runInstruction(
      &system,
      immediateInstruction(0x0a, 1, 2, 0));
    REQUIRE(core.generalRegister(2).low == 1);

    runInstruction(
      &system,
      immediateInstruction(0x0b, 1, 3, 0));
    REQUIRE(core.generalRegister(3).low == 0);

    runInstruction(
      &system,
      immediateInstruction(0x0b, 1, 4, 0xffff));
    REQUIRE(core.generalRegister(4).low == 0);
  }
}

TEST_CASE("EE register integer execution")
{
  NekoSystem system;
  EECore &core = system.eeCore();

  SECTION("Word arithmetic wraps and sign extends")
  {
    setRegister(&core, 1, 0x7fffffff);
    setRegister(&core, 2, 1);

    runInstruction(
      &system,
      registerInstruction(0x21, 1, 2, 3));

    REQUIRE(
      core.generalRegister(3).low ==
      UINT64_C(0xffffffff80000000));

    runInstruction(
      &system,
      registerInstruction(0x23, 2, 1, 4));
    REQUIRE(
      core.generalRegister(4).low ==
      UINT64_C(0xffffffff80000002));
  }

  SECTION("Doubleword arithmetic uses modulo 64-bit results")
  {
    setRegister(&core, 1, UINT64_MAX);
    setRegister(&core, 2, 2);

    runInstruction(
      &system,
      registerInstruction(0x2d, 1, 2, 3));
    REQUIRE(core.generalRegister(3).low == 1);

    runInstruction(
      &system,
      registerInstruction(0x2f, 3, 2, 4));
    REQUIRE(core.generalRegister(4).low == UINT64_MAX);
  }

  SECTION("Logic and comparisons operate on the low doubleword")
  {
    setRegister(&core, 1, UINT64_C(0xf0f0000000000001));
    setRegister(&core, 2, UINT64_C(0x0ff0000000000003));

    runInstruction(
      &system,
      registerInstruction(0x24, 1, 2, 3));
    REQUIRE(
      core.generalRegister(3).low ==
      UINT64_C(0x00f0000000000001));

    runInstruction(
      &system,
      registerInstruction(0x25, 1, 2, 4));
    REQUIRE(
      core.generalRegister(4).low ==
      UINT64_C(0xfff0000000000003));

    runInstruction(
      &system,
      registerInstruction(0x26, 1, 2, 5));
    REQUIRE(
      core.generalRegister(5).low ==
      UINT64_C(0xff00000000000002));

    runInstruction(
      &system,
      registerInstruction(0x27, 1, 2, 6));
    REQUIRE(
      core.generalRegister(6).low ==
      UINT64_C(0x000ffffffffffffc));

    setRegister(&core, 1, UINT64_MAX);
    setRegister(&core, 2, 0);
    runInstruction(
      &system,
      registerInstruction(0x2a, 1, 2, 7));
    runInstruction(
      &system,
      registerInstruction(0x2b, 1, 2, 8));
    REQUIRE(core.generalRegister(7).low == 1);
    REQUIRE(core.generalRegister(8).low == 0);
  }

  SECTION("Register zero remains immutable")
  {
    setRegister(&core, 1, 1);
    setRegister(&core, 2, 2);

    runInstruction(
      &system,
      registerInstruction(0x2d, 1, 2, 0));

    REQUIRE(core.generalRegister(0) == EERegister128{});
  }
}

TEST_CASE("EE integer shift execution")
{
  NekoSystem system;
  EECore &core = system.eeCore();

  SECTION("Word shifts sign extend their 32-bit result")
  {
    setRegister(&core, 1, UINT64_C(0xffffffff80000001));

    runInstruction(
      &system,
      registerInstruction(0x00, 0, 1, 2, 1));
    REQUIRE(core.generalRegister(2).low == 2);

    runInstruction(
      &system,
      registerInstruction(0x02, 0, 1, 3, 1));
    REQUIRE(core.generalRegister(3).low == 0x40000000);

    runInstruction(
      &system,
      registerInstruction(0x03, 0, 1, 4, 1));
    REQUIRE(
      core.generalRegister(4).low ==
      UINT64_C(0xffffffffc0000000));
  }

  SECTION("Variable shifts mask their shift counts")
  {
    setRegister(&core, 1, 33);
    setRegister(&core, 2, 1);

    runInstruction(
      &system,
      registerInstruction(0x04, 1, 2, 3));
    REQUIRE(core.generalRegister(3).low == 2);

    setRegister(&core, 1, 65);
    runInstruction(
      &system,
      registerInstruction(0x14, 1, 2, 4));
    REQUIRE(core.generalRegister(4).low == 2);
  }

  SECTION("Doubleword shifts preserve the upper doubleword")
  {
    setRegister(
      &core,
      1,
      UINT64_C(0x8000000000000001));
    setRegister(&core, 2, 0);

    runInstruction(
      &system,
      registerInstruction(0x3a, 0, 1, 2, 1));
    REQUIRE(
      core.generalRegister(2).low ==
      UINT64_C(0x4000000000000000));
    REQUIRE(
      core.generalRegister(2).high ==
      UINT64_C(0xfeedfacecafebeef));

    runInstruction(
      &system,
      registerInstruction(0x3b, 0, 1, 2, 1));
    REQUIRE(
      core.generalRegister(2).low ==
      UINT64_C(0xc000000000000000));

    runInstruction(
      &system,
      registerInstruction(0x3c, 0, 1, 2, 1));
    REQUIRE(
      core.generalRegister(2).low ==
      UINT64_C(0x0000000200000000));
  }
}

TEST_CASE("EE trapping and undefined integer operations")
{
  NekoSystem system;
  EECore &core = system.eeCore();

  SECTION("Word overflow preserves the destination and stops")
  {
    setRegister(&core, 1, 0x7fffffff);
    setRegister(&core, 2, 1);
    setRegister(&core, 3, 0x1234, 0x5678);

    runInstruction(
      &system,
      registerInstruction(0x20, 1, 2, 3));

    REQUIRE(
      core.generalRegister(3) ==
      EERegister128{0x1234, 0x5678});
    REQUIRE_FALSE(core.clockActive());
    REQUIRE(
      core.stopReason() ==
      EEStopReason::ExecutionException);
    REQUIRE(
      core.pendingException() ==
      EEException::ArithmeticOverflow);
    REQUIRE(core.exceptionAddress() == 0);
    REQUIRE(core.programCounter() == 0);
    REQUIRE_FALSE(core.hasLastInstruction());
  }

  SECTION("Doubleword subtraction overflow is detected")
  {
    setRegister(
      &core,
      1,
      UINT64_C(0x8000000000000000));
    setRegister(&core, 2, 1);
    setRegister(&core, 3, 0x1234);

    runInstruction(
      &system,
      registerInstruction(0x2e, 1, 2, 3));

    REQUIRE(core.generalRegister(3).low == 0x1234);
    REQUIRE(
      core.pendingException() ==
      EEException::ArithmeticOverflow);
  }

  SECTION("Word operations reject non-sign-extended operands")
  {
    setRegister(&core, 1, UINT64_C(0x0000000080000000));
    setRegister(&core, 2, 0);
    setRegister(&core, 3, 0x1234);

    runInstruction(
      &system,
      registerInstruction(0x21, 1, 2, 3));

    REQUIRE(core.generalRegister(3).low == 0x1234);
    REQUIRE_FALSE(core.exceptionPending());
    REQUIRE(
      core.stopReason() ==
      EEStopReason::UndefinedOperation);
    REQUIRE(core.programCounter() == 0);
  }
}

TEST_CASE("Every decoded base integer operation executes")
{
  const std::uint32_t instructions[] = {
    0,
    registerInstruction(0x00, 0, 2, 3, 1),
    registerInstruction(0x02, 0, 2, 3, 1),
    registerInstruction(0x03, 0, 2, 3, 1),
    registerInstruction(0x04, 1, 2, 3),
    registerInstruction(0x06, 1, 2, 3),
    registerInstruction(0x07, 1, 2, 3),
    registerInstruction(0x14, 1, 2, 3),
    registerInstruction(0x16, 1, 2, 3),
    registerInstruction(0x17, 1, 2, 3),
    registerInstruction(0x20, 1, 2, 3),
    registerInstruction(0x21, 1, 2, 3),
    registerInstruction(0x22, 1, 2, 3),
    registerInstruction(0x23, 1, 2, 3),
    registerInstruction(0x24, 1, 2, 3),
    registerInstruction(0x25, 1, 2, 3),
    registerInstruction(0x26, 1, 2, 3),
    registerInstruction(0x27, 1, 2, 3),
    registerInstruction(0x2a, 1, 2, 3),
    registerInstruction(0x2b, 1, 2, 3),
    registerInstruction(0x2c, 1, 2, 3),
    registerInstruction(0x2d, 1, 2, 3),
    registerInstruction(0x2e, 1, 2, 3),
    registerInstruction(0x2f, 1, 2, 3),
    registerInstruction(0x38, 0, 2, 3, 1),
    registerInstruction(0x3a, 0, 2, 3, 1),
    registerInstruction(0x3b, 0, 2, 3, 1),
    registerInstruction(0x3c, 0, 2, 3, 1),
    registerInstruction(0x3e, 0, 2, 3, 1),
    registerInstruction(0x3f, 0, 2, 3, 1),
    immediateInstruction(0x08, 1, 3, 1),
    immediateInstruction(0x09, 1, 3, 1),
    immediateInstruction(0x0a, 1, 3, 1),
    immediateInstruction(0x0b, 1, 3, 1),
    immediateInstruction(0x0c, 1, 3, 1),
    immediateInstruction(0x0d, 1, 3, 1),
    immediateInstruction(0x0e, 1, 3, 1),
    immediateInstruction(0x0f, 0, 3, 1),
    immediateInstruction(0x18, 1, 3, 1),
    immediateInstruction(0x19, 1, 3, 1)
  };

  for (std::uint32_t instruction : instructions)
  {
    NekoSystem system;
    setRegister(&system.eeCore(), 1, 8);
    setRegister(&system.eeCore(), 2, 2);

    runInstruction(&system, instruction);

    REQUIRE(system.eeCore().clockActive());
    REQUIRE(system.eeCore().programCounter() == 4);
    REQUIRE(system.eeCore().hasLastInstruction());
  }
}

TEST_CASE("A failing EE instruction preserves the last retired instruction")
{
  NekoSystem system;
  setRegister(&system.eeCore(), 1, 0x7fffffff);
  setRegister(&system.eeCore(), 2, 1);
  system.eeBus().write32(0, 0);
  system.eeBus().write32(
    4,
    registerInstruction(0x20, 1, 2, 3));
  system.eeCore().startExecution(0);

  system.runMasterCycles(2);

  REQUIRE(system.eeCore().hasLastInstruction());
  REQUIRE(system.eeCore().lastInstructionAddress() == 0);
  REQUIRE(
    system.eeCore().lastInstruction().operation ==
    EEOperation::Nop);
  REQUIRE(
    system.eeCore().stopReason() ==
    EEStopReason::ExecutionException);
  REQUIRE(
    system.eeCore().rejectedInstruction() ==
    registerInstruction(0x20, 1, 2, 3));
}
