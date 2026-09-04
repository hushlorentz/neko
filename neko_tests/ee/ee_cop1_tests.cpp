#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "ee_core.hpp"
#include "ee_instruction.hpp"
#include "floating_point_ops.hpp"
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

  std::uint32_t cop1MemoryInstruction(
    std::uint8_t opcode,
    std::uint8_t base,
    std::uint8_t floatingPointRegister,
    std::uint16_t offset)
  {
    return
      (static_cast<std::uint32_t>(opcode) << 26) |
      (static_cast<std::uint32_t>(base) << 21) |
      (static_cast<std::uint32_t>(
        floatingPointRegister) << 16) |
      offset;
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

TEST_CASE("EE COP1 raw values expose EE and IEEE classifications")
{
  struct ClassificationVector
  {
    std::uint32_t bits;
    bool negative;
    std::uint8_t encodedExponent;
    std::int16_t unbiasedExponent;
    std::uint32_t mantissa;
    EEFloatClassification classification;
    IEEEFloatEncoding ieeeEncoding;
  };

  const ClassificationVector vectors[] = {
    {
      UINT32_C(0x00000000),
      false,
      0,
      -127,
      0,
      EEFloatClassification::Zero,
      IEEEFloatEncoding::Zero
    },
    {
      UINT32_C(0x80000000),
      true,
      0,
      -127,
      0,
      EEFloatClassification::Zero,
      IEEEFloatEncoding::Zero
    },
    {
      UINT32_C(0x807fffff),
      true,
      0,
      -127,
      UINT32_C(0x7fffff),
      EEFloatClassification::Zero,
      IEEEFloatEncoding::Subnormal
    },
    {
      UINT32_C(0x3fc12345),
      false,
      127,
      0,
      UINT32_C(0x412345),
      EEFloatClassification::Normal,
      IEEEFloatEncoding::Normal
    },
    {
      UINT32_C(0x7f800000),
      false,
      255,
      128,
      0,
      EEFloatClassification::ExtendedFinite,
      IEEEFloatEncoding::Infinity
    },
    {
      UINT32_C(0xffc00000),
      true,
      255,
      128,
      UINT32_C(0x400000),
      EEFloatClassification::ExtendedFinite,
      IEEEFloatEncoding::NaN
    }
  };

  for (const ClassificationVector &vector : vectors)
  {
    const EEFloatDecomposition value =
      decomposeEEFloat(vector.bits);

    REQUIRE(value.negative == vector.negative);
    REQUIRE(value.encodedExponent == vector.encodedExponent);
    REQUIRE(value.unbiasedExponent == vector.unbiasedExponent);
    REQUIRE(value.mantissa == vector.mantissa);
    REQUIRE(value.classification == vector.classification);
    REQUIRE(value.ieeeEncoding == vector.ieeeEncoding);
  }
}

TEST_CASE("EE COP1 memory transfer instructions decode canonically")
{
  REQUIRE(
    decodeEEInstruction(
      cop1MemoryInstruction(
        0x31,
        1,
        2,
        0x3456)).operation ==
    EEOperation::LoadWordToCOP1);
  REQUIRE(
    decodeEEInstruction(
      cop1MemoryInstruction(
        0x39,
        1,
        2,
        0x3456)).operation ==
    EEOperation::StoreWordFromCOP1);
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

TEST_CASE("EE LWC1 and SWC1 transfer raw words through memory")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setGeneralRegister(
    1,
    {
      UINT64_C(0x1234000000000104),
      UINT64_C(0xabcdef0123456789)
    });
  REQUIRE(
    system.eeBus().writeData32(
      0x100,
      UINT32_C(0x89abcdef)));

  system.eeBus().write32(
    0,
    cop1MemoryInstruction(0x31, 1, 3, 0xfffc));
  system.eeBus().write32(4, 0);
  core.startExecution(0);
  system.clockMasterCycle();
  REQUIRE(core.floatingPointRegister(3) == 0);
  system.clockMasterCycle();
  REQUIRE(
    core.floatingPointRegister(3) ==
    UINT32_C(0x89abcdef));

  core.setFloatingPointRegister(4, UINT32_C(0x76543210));
  runInstruction(
    &system,
    cop1MemoryInstruction(0x39, 1, 4, 0));

  std::uint32_t stored = 0;
  REQUIRE(system.eeBus().readData32(0x104, &stored));
  REQUIRE(stored == UINT32_C(0x76543210));
}

TEST_CASE("EE LWC1 stalls an immediate dependent FPR use")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setGeneralRegister(1, {0x100, 0});
  core.setFloatingPointRegister(3, UINT32_C(0x11111111));
  REQUIRE(
    system.eeBus().writeData32(
      0x100,
      UINT32_C(0x89abcdef)));
  system.eeBus().write32(
    0,
    cop1MemoryInstruction(0x31, 1, 3, 0));
  system.eeBus().write32(
    4,
    cop1TransferInstruction(0x00, 2, 3));
  core.startExecution(0);

  system.clockMasterCycle();
  REQUIRE(core.programCounter() == 4);
  REQUIRE(
    core.floatingPointRegister(3) ==
    UINT32_C(0x11111111));

  system.clockMasterCycle();
  REQUIRE(core.programCounter() == 4);
  REQUIRE(core.generalRegister(2) == EERegister128{});
  REQUIRE(
    core.floatingPointRegister(3) ==
    UINT32_C(0x89abcdef));

  system.clockMasterCycle();
  REQUIRE(core.programCounter() == 8);
  REQUIRE(
    core.generalRegister(2).low ==
    UINT64_C(0xffffffff89abcdef));
}

TEST_CASE("EE LWC1 permits independent work during writeback")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setGeneralRegister(1, {0x100, 0});
  REQUIRE(
    system.eeBus().writeData32(
      0x100,
      UINT32_C(0x12345678)));
  system.eeBus().write32(
    0,
    cop1MemoryInstruction(0x31, 1, 3, 0));
  system.eeBus().write32(
    4,
    (UINT32_C(0x0d) << 26) |
      (UINT32_C(4) << 16) |
      UINT32_C(0x55));
  system.eeBus().write32(
    8,
    cop1TransferInstruction(0x00, 2, 3));
  core.startExecution(0);

  system.runMasterCycles(2);

  REQUIRE(core.programCounter() == 8);
  REQUIRE(core.generalRegister(4).low == 0x55);
  REQUIRE(
    core.floatingPointRegister(3) ==
    UINT32_C(0x12345678));

  system.clockMasterCycle();
  REQUIRE(core.programCounter() == 12);
  REQUIRE(core.generalRegister(2).low == 0x12345678);
}

TEST_CASE("EE LWC1 interlocks younger writes to the same FPR")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setGeneralRegister(1, {0x100, 0});
  core.setGeneralRegister(
    2,
    {UINT32_C(0x76543210), 0});
  REQUIRE(
    system.eeBus().writeData32(
      0x100,
      UINT32_C(0x12345678)));
  system.eeBus().write32(
    0,
    cop1MemoryInstruction(0x31, 1, 3, 0));
  system.eeBus().write32(
    4,
    cop1TransferInstruction(0x04, 2, 3));
  core.startExecution(0);

  system.runMasterCycles(2);

  REQUIRE(core.programCounter() == 4);
  REQUIRE(
    core.floatingPointRegister(3) ==
    UINT32_C(0x12345678));

  system.clockMasterCycle();
  REQUIRE(core.programCounter() == 8);
  REQUIRE(
    core.floatingPointRegister(3) ==
    UINT32_C(0x76543210));
}

TEST_CASE("EE LWC1 interlocks an immediate SWC1 source")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setGeneralRegister(1, {0x100, 0});
  REQUIRE(
    system.eeBus().writeData32(
      0x100,
      UINT32_C(0x89abcdef)));
  system.eeBus().write32(
    0,
    cop1MemoryInstruction(0x31, 1, 3, 0));
  system.eeBus().write32(
    4,
    cop1MemoryInstruction(0x39, 1, 3, 4));
  core.startExecution(0);

  system.runMasterCycles(2);

  REQUIRE(core.programCounter() == 4);
  std::uint32_t stored = 0;
  REQUIRE(system.eeBus().readData32(0x104, &stored));
  REQUIRE(stored == 0);

  system.clockMasterCycle();

  REQUIRE(core.programCounter() == 8);
  REQUIRE(system.eeBus().readData32(0x104, &stored));
  REQUIRE(stored == UINT32_C(0x89abcdef));
}

TEST_CASE("EE pending COP1 loads survive host halt and save-state restore")
{
  NekoSystem original;
  EECore &originalCore = original.eeCore();
  originalCore.setGeneralRegister(1, {0x100, 0});
  REQUIRE(
    original.eeBus().writeData32(
      0x100,
      UINT32_C(0x89abcdef)));
  original.eeBus().write32(
    0,
    cop1MemoryInstruction(0x31, 1, 3, 0));
  original.eeBus().write32(
    4,
    cop1TransferInstruction(0x00, 2, 3));
  originalCore.startExecution(0);
  original.clockMasterCycle();
  originalCore.haltExecution();

  const std::vector<std::uint8_t> saved = original.saveState();
  NekoSystem restored;
  restored.loadState(saved);

  originalCore.startExecution(4);
  restored.eeCore().startExecution(4);
  original.clockMasterCycle();
  restored.clockMasterCycle();

  REQUIRE(originalCore.programCounter() == 4);
  REQUIRE(restored.eeCore().programCounter() == 4);
  REQUIRE(
    originalCore.floatingPointRegister(3) ==
    UINT32_C(0x89abcdef));
  REQUIRE(
    restored.eeCore().floatingPointRegister(3) ==
    UINT32_C(0x89abcdef));
  REQUIRE(originalCore.stateHash() == restored.eeCore().stateHash());

  original.clockMasterCycle();
  restored.clockMasterCycle();
  REQUIRE(
    originalCore.generalRegister(2) ==
    restored.eeCore().generalRegister(2));
  REQUIRE(originalCore.stateHash() == restored.eeCore().stateHash());
}

TEST_CASE("EE reset cancels pending COP1 loads")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setGeneralRegister(1, {0x100, 0});
  REQUIRE(
    system.eeBus().writeData32(
      0x100,
      UINT32_C(0x89abcdef)));

  runInstruction(
    &system,
    cop1MemoryInstruction(0x31, 1, 3, 0));
  REQUIRE(core.floatingPointRegister(3) == 0);

  core.reset();
  system.eeBus().write32(0, 0);
  core.startExecution(0);
  system.clockMasterCycle();

  REQUIRE(core.floatingPointRegister(3) == 0);
}

TEST_CASE("EE COP1 word memory accesses use RAM aliases and boundaries")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  const std::uint32_t boundary =
    EEMemoryMap::MAIN_MEMORY_SIZE - 4;
  REQUIRE(
    system.eeBus().writeData32(
      boundary,
      UINT32_C(0x89abcdef)));
  core.setGeneralRegister(
    1,
    {EEMemoryMap::KSEG0_BASE + boundary, 0});
  system.eeBus().write32(
    0,
    cop1MemoryInstruction(0x31, 1, 3, 0));
  system.eeBus().write32(4, 0);
  core.startExecution(0);
  system.runMasterCycles(2);

  REQUIRE(
    core.floatingPointRegister(3) ==
    UINT32_C(0x89abcdef));

  core.setFloatingPointRegister(4, UINT32_C(0x76543210));
  core.setGeneralRegister(
    1,
    {EEMemoryMap::KSEG1_BASE + boundary, 0});
  runInstruction(
    &system,
    cop1MemoryInstruction(0x39, 1, 4, 0));

  std::uint32_t stored = 0;
  REQUIRE(system.eeBus().readData32(boundary, &stored));
  REQUIRE(stored == UINT32_C(0x76543210));
}

TEST_CASE("EE COP1 word memory alignment faults are precise")
{
  SECTION("LWC1 preserves its destination")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, {0x102, 0});
    core.setFloatingPointRegister(2, UINT32_C(0x12345678));

    runInstruction(
      &system,
      cop1MemoryInstruction(0x31, 1, 2, 0));

    REQUIRE(
      core.pendingException() ==
      EEException::AddressErrorLoadOrFetch);
    REQUIRE(core.exceptionAddress() == 0x102);
    REQUIRE(
      core.floatingPointRegister(2) ==
      UINT32_C(0x12345678));
  }

  SECTION("SWC1 performs no partial write")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, {0x102, 0});
    core.setFloatingPointRegister(2, UINT32_C(0xaabbccdd));
    REQUIRE(
      system.eeBus().writeData32(
        0x100,
        UINT32_C(0x11223344)));

    runInstruction(
      &system,
      cop1MemoryInstruction(0x39, 1, 2, 0));

    REQUIRE(
      core.pendingException() ==
      EEException::AddressErrorStore);
    REQUIRE(core.exceptionAddress() == 0x102);
    std::uint32_t stored = 0;
    REQUIRE(system.eeBus().readData32(0x100, &stored));
    REQUIRE(stored == UINT32_C(0x11223344));
  }
}

TEST_CASE("EE COP1 alignment faults identify branch delay slots")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(EECOP0Register::Status, 0);
  core.setGeneralRegister(1, {0x102, 0});
  system.eeBus().write32(
    0,
    (UINT32_C(0x04) << 26) |
      UINT32_C(1));
  system.eeBus().write32(
    4,
    cop1MemoryInstruction(0x31, 1, 2, 0));
  core.startExecution(0);

  system.runMasterCycles(2);

  REQUIRE(
    core.pendingException() ==
    EEException::CoprocessorUnusable);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
  REQUIRE(
    (core.cop0Register(EECOP0Register::Cause) &
      EECOP0Cause::BRANCH_DELAY) != 0);

  core.setCOP0Register(
    EECOP0Register::Status,
    EECOP0Status::COP1_USABLE);
  core.clearPendingException();
  core.setProgramCounter(0);
  core.startExecution(0);
  system.runMasterCycles(2);

  REQUIRE(
    core.pendingException() ==
    EEException::AddressErrorLoadOrFetch);
  REQUIRE(core.exceptionAddress() == 0x102);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
  REQUIRE(
    (core.cop0Register(EECOP0Register::Cause) &
      EECOP0Cause::BRANCH_DELAY) != 0);
}

TEST_CASE("EE COP1 word memory bus faults preserve architectural state")
{
  SECTION("LWC1 reports a load bus error")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(
      1,
      {EEMemoryMap::MAIN_MEMORY_SIZE, 0});
    core.setFloatingPointRegister(2, UINT32_C(0x12345678));

    runInstruction(
      &system,
      cop1MemoryInstruction(0x31, 1, 2, 0));

    REQUIRE(
      core.pendingException() ==
      EEException::DataBusErrorLoad);
    REQUIRE(
      core.exceptionAddress() ==
      EEMemoryMap::MAIN_MEMORY_SIZE);
    REQUIRE(
      core.floatingPointRegister(2) ==
      UINT32_C(0x12345678));
  }

  SECTION("SWC1 reports a store bus error")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(
      1,
      {EEMemoryMap::MAIN_MEMORY_SIZE, 0});
    core.setFloatingPointRegister(2, UINT32_C(0x89abcdef));

    runInstruction(
      &system,
      cop1MemoryInstruction(0x39, 1, 2, 0));

    REQUIRE(
      core.pendingException() ==
      EEException::DataBusErrorStore);
    REQUIRE(
      core.exceptionAddress() ==
      EEMemoryMap::MAIN_MEMORY_SIZE);
  }
}

TEST_CASE("EE COP1 transfers require Status CU1")
{
  const std::uint32_t instructions[] = {
    cop1TransferInstruction(0x00, 2, 3),
    cop1TransferInstruction(0x04, 2, 3),
    cop1TransferInstruction(0x02, 2, 31),
    cop1TransferInstruction(0x06, 2, 31),
    cop1MemoryInstruction(0x31, 1, 3, 2),
    cop1MemoryInstruction(0x39, 1, 3, 2)
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
    REQUIRE(
      system.eeBus().writeData32(
        0,
        UINT32_C(0x11223344)));
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
    std::uint32_t stored = 0;
    REQUIRE(system.eeBus().readData32(0, &stored));
    REQUIRE(stored == instruction);
  }
}
