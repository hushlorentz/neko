#include <cstdint>

#include "catch.hpp"
#include "ee_core.hpp"
#include "ee_instruction.hpp"
#include "neko_system.hpp"

namespace
{
  std::uint32_t cop1TransferInstruction(
    std::uint8_t source,
    std::uint8_t target,
    std::uint8_t floatingPointRegister)
  {
    return
      (UINT32_C(0x11) << 26) |
      (static_cast<std::uint32_t>(source) << 21) |
      (static_cast<std::uint32_t>(target) << 16) |
      (static_cast<std::uint32_t>(floatingPointRegister) << 11);
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

TEST_CASE("EE COP1 word transfer instructions decode canonically")
{
  REQUIRE(
    decodeEEInstruction(
      cop1TransferInstruction(0x00, 2, 3)).operation ==
    EEOperation::MoveWordFromCOP1);
  REQUIRE(
    decodeEEInstruction(
      cop1TransferInstruction(0x04, 2, 3)).operation ==
    EEOperation::MoveWordToCOP1);
  REQUIRE(
    decodeEEInstruction(
      cop1TransferInstruction(0x02, 2, 0)).operation ==
    EEOperation::MoveControlWordFromCOP1);
  REQUIRE(
    decodeEEInstruction(
      cop1TransferInstruction(0x06, 2, 31)).operation ==
    EEOperation::MoveControlWordToCOP1);

  REQUIRE_THROWS_WITH(
    decodeEEInstruction(
      cop1TransferInstruction(0x00, 2, 3) | 1),
    "Reserved EE instruction encoding.");
  REQUIRE_THROWS_WITH(
    decodeEEInstruction(
      cop1TransferInstruction(0x04, 2, 3) | 1),
    "Reserved EE instruction encoding.");
  REQUIRE_THROWS_WITH(
    decodeEEInstruction(
      cop1TransferInstruction(0x02, 2, 0) | 1),
    "Reserved EE instruction encoding.");
  REQUIRE_THROWS_WITH(
    decodeEEInstruction(
      cop1TransferInstruction(0x06, 2, 31) | 1),
    "Reserved EE instruction encoding.");
}

TEST_CASE("EE COP1 control transfers reject reserved FCRs")
{
  for (std::uint8_t controlRegister = 1;
       controlRegister < 31;
       ++controlRegister)
  {
    REQUIRE_THROWS_WITH(
      decodeEEInstruction(
        cop1TransferInstruction(
          0x02,
          2,
          controlRegister)),
      "Reserved EE instruction encoding.");
    REQUIRE_THROWS_WITH(
      decodeEEInstruction(
        cop1TransferInstruction(
          0x06,
          2,
          controlRegister)),
      "Reserved EE instruction encoding.");
  }
}

TEST_CASE("EE MFC1 sign-extends raw FPR words into GPRs")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setFloatingPointRegister(3, UINT32_C(0x89abcdef));
  core.setGeneralRegister(
    2,
    {
      UINT64_C(0x1111111122222222),
      UINT64_C(0x3333333344444444)
    });

  runInstruction(
    &system,
    cop1TransferInstruction(0x00, 2, 3));

  REQUIRE(
    core.generalRegister(2) ==
    EERegister128{
      UINT64_C(0xffffffff89abcdef),
      UINT64_C(0x3333333344444444)
    });
}

TEST_CASE("EE MTC1 writes the low GPR word into any FPR")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setGeneralRegister(
    2,
    {
      UINT64_C(0x1122334489abcdef),
      UINT64_C(0xfedcba9876543210)
    });

  runInstruction(
    &system,
    cop1TransferInstruction(0x04, 2, 0));

  REQUIRE(
    core.floatingPointRegister(0) ==
    UINT32_C(0x89abcdef));
}

TEST_CASE("EE COP1 transfers preserve GPR zero")
{
  NekoSystem system;
  system.eeCore().setFloatingPointRegister(
    3,
    UINT32_C(0xffffffff));

  runInstruction(
    &system,
    cop1TransferInstruction(0x00, 0, 3));

  REQUIRE(
    system.eeCore().generalRegister(0) ==
    EERegister128{});
}

TEST_CASE("EE CFC1 exposes implemented control-register values")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP1ControlRegister(31, UINT32_MAX);
  core.setGeneralRegister(
    2,
    {
      UINT64_C(0xaaaaaaaaaaaaaaaa),
      UINT64_C(0xbbbbbbbbbbbbbbbb)
    });

  runInstruction(
    &system,
    cop1TransferInstruction(0x02, 2, 0));

  REQUIRE(
    core.generalRegister(2) ==
    EERegister128{
      EECOP1Control::IMPLEMENTATION_REVISION,
      UINT64_C(0xbbbbbbbbbbbbbbbb)
    });

  runInstruction(
    &system,
    cop1TransferInstruction(0x02, 2, 31));

  REQUIRE(
    core.generalRegister(2) ==
    EERegister128{
      EECOP1Control::STATUS_FIXED |
        EECOP1Control::STATUS_WRITABLE_MASK,
      UINT64_C(0xbbbbbbbbbbbbbbbb)
    });
}

TEST_CASE("EE CTC1 applies control-register architectural masks")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setGeneralRegister(
    2,
    {
      UINT64_C(0x11223344ffffffff),
      UINT64_C(0xfedcba9876543210)
    });

  runInstruction(
    &system,
    cop1TransferInstruction(0x06, 2, 0));
  REQUIRE(
    core.cop1ControlRegister(0) ==
    EECOP1Control::IMPLEMENTATION_REVISION);

  runInstruction(
    &system,
    cop1TransferInstruction(0x06, 2, 31));
  REQUIRE(
    core.cop1ControlRegister(31) ==
    (EECOP1Control::STATUS_FIXED |
     EECOP1Control::STATUS_WRITABLE_MASK));
}

TEST_CASE("EE COP1 transfers require Status CU1")
{
  const std::uint32_t instructions[] = {
    cop1TransferInstruction(0x00, 2, 3),
    cop1TransferInstruction(0x04, 2, 3),
    cop1TransferInstruction(0x02, 2, 31),
    cop1TransferInstruction(0x06, 2, 31)
  };

  for (const std::uint32_t instruction : instructions)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(EECOP0Register::Status, 0);
    core.setCOP0Register(
      EECOP0Register::Cause,
      UINT32_C(0xc0000000));
    core.setGeneralRegister(
      2,
      {
        UINT64_C(0x1111111189abcdef),
        UINT64_C(0x2222222233333333)
      });
    core.setFloatingPointRegister(3, UINT32_C(0x76543210));
    system.eeBus().write32(0, instruction);
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE(
      core.pendingException() ==
      EEException::CoprocessorUnusable);
    REQUIRE(
      ((core.cop0Register(EECOP0Register::Cause) &
        EECOP0Cause::EXCEPTION_CODE_MASK) >> 2) ==
      EEExceptionCode::COPROCESSOR_UNUSABLE);
    REQUIRE(
      (core.cop0Register(EECOP0Register::Cause) &
        EECOP0Cause::COPROCESSOR_ERROR_MASK) ==
      EECOP0Cause::COPROCESSOR_1);
    REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
    REQUIRE(
      core.programCounter() ==
      EEExceptionVector::GENERAL);
    REQUIRE(
      core.generalRegister(2) ==
      EERegister128{
        UINT64_C(0x1111111189abcdef),
        UINT64_C(0x2222222233333333)
      });
    REQUIRE(
      core.floatingPointRegister(3) ==
      UINT32_C(0x76543210));
    REQUIRE(
      core.cop1ControlRegister(31) ==
      EECOP1Control::STATUS_FIXED);
  }
}
