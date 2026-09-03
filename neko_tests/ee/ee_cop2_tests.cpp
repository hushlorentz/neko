#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "ee_core.hpp"
#include "neko_system.hpp"
#include "vpu_opcodes.hpp"

namespace
{
  std::uint32_t cop2TransferInstruction(
    std::uint8_t source,
    std::uint8_t target,
    std::uint8_t vectorRegister,
    bool interlock = false)
  {
    return
      (UINT32_C(0x12) << 26) |
      (static_cast<std::uint32_t>(source) << 21) |
      (static_cast<std::uint32_t>(target) << 16) |
      (static_cast<std::uint32_t>(vectorRegister) << 11) |
      (interlock ? 1 : 0);
  }

  std::uint32_t cop2MemoryInstruction(
    std::uint8_t opcode,
    std::uint8_t base,
    std::uint8_t vectorRegister,
    std::uint16_t offset)
  {
    return
      (static_cast<std::uint32_t>(opcode) << 26) |
      (static_cast<std::uint32_t>(base) << 21) |
      (static_cast<std::uint32_t>(vectorRegister) << 16) |
      offset;
  }

  std::uint32_t cop2BranchInstruction(
    std::uint8_t condition,
    std::uint16_t offset)
  {
    return
      (UINT32_C(0x12) << 26) |
      (UINT32_C(0x08) << 21) |
      (static_cast<std::uint32_t>(condition) << 16) |
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

TEST_CASE("EE COP2 vector transfer instructions decode canonically")
{
  REQUIRE(
    decodeEEInstruction(
      cop2TransferInstruction(0x01, 2, 3)).operation ==
    EEOperation::QuadwordMoveFromCOP2);
  REQUIRE(
    decodeEEInstruction(
      cop2TransferInstruction(0x05, 2, 3, true)).operation ==
    EEOperation::QuadwordMoveToCOP2);
  REQUIRE(
    decodeEEInstruction(
      cop2MemoryInstruction(0x36, 1, 2, 0x3456)).operation ==
    EEOperation::LoadQuadwordToCOP2);
  REQUIRE(
    decodeEEInstruction(
      cop2MemoryInstruction(0x3e, 1, 2, 0x3456)).operation ==
    EEOperation::StoreQuadwordFromCOP2);
  REQUIRE_THROWS_WITH(
    decodeEEInstruction(
      cop2TransferInstruction(0x01, 2, 3) | 2),
    "Reserved EE instruction encoding.");
}

TEST_CASE("EE COP2 control and branch instructions decode canonically")
{
  REQUIRE(
    decodeEEInstruction(
      cop2TransferInstruction(0x02, 2, 3)).operation ==
    EEOperation::ControlMoveFromCOP2);
  REQUIRE(
    decodeEEInstruction(
      cop2TransferInstruction(0x06, 2, 3, true)).operation ==
    EEOperation::ControlMoveToCOP2);

  const EEOperation operations[] = {
    EEOperation::BranchCOP2False,
    EEOperation::BranchCOP2True,
    EEOperation::BranchCOP2FalseLikely,
    EEOperation::BranchCOP2TrueLikely
  };
  for (std::uint8_t condition = 0; condition < 4; ++condition)
  {
    REQUIRE(
      decodeEEInstruction(
        cop2BranchInstruction(condition, 0x1234)).operation ==
      operations[condition]);
  }

  REQUIRE_THROWS_WITH(
    decodeEEInstruction(cop2BranchInstruction(4, 0)),
    "Reserved EE instruction encoding.");
  REQUIRE_THROWS_WITH(
    decodeEEInstruction(
      cop2TransferInstruction(0x02, 2, 3) | 2),
    "Reserved EE instruction encoding.");
}

TEST_CASE("EE QMTC2 and QMFC2 transfer complete 128-bit values")
{
  NekoSystem system;
  const EERegister128 expected = {
    UINT64_C(0x7766554433221100),
    UINT64_C(0xffeeddccbbaa9988)
  };
  system.eeCore().setGeneralRegister(2, expected);

  runInstruction(
    &system,
    cop2TransferInstruction(0x05, 2, 3));

  const FPRegister *vector = system.vu0().fpRegisterValue(3);
  REQUIRE(vector->x.bits() == UINT32_C(0x33221100));
  REQUIRE(vector->y.bits() == UINT32_C(0x77665544));
  REQUIRE(vector->z.bits() == UINT32_C(0xbbaa9988));
  REQUIRE(vector->w.bits() == UINT32_C(0xffeeddcc));

  system.eeCore().setGeneralRegister(4, {});
  runInstruction(
    &system,
    cop2TransferInstruction(0x01, 4, 3));

  REQUIRE(system.eeCore().generalRegister(4) == expected);
}

TEST_CASE("EE COP2 transfers preserve constant registers")
{
  NekoSystem system;
  system.eeCore().setGeneralRegister(
    2,
    {UINT64_MAX, UINT64_MAX});

  runInstruction(
    &system,
    cop2TransferInstruction(0x05, 2, 0));

  const FPRegister *vf0 = system.vu0().fpRegisterValue(0);
  REQUIRE(vf0->x.bits() == 0);
  REQUIRE(vf0->y.bits() == 0);
  REQUIRE(vf0->z.bits() == 0);
  REQUIRE(vf0->w.bits() == UINT32_C(0x3f800000));

  runInstruction(
    &system,
    cop2TransferInstruction(0x01, 0, 0));
  REQUIRE(system.eeCore().generalRegister(0) == EERegister128{});
}

TEST_CASE("EE LQC2 and SQC2 bridge RAM and VU0 registers")
{
  NekoSystem system;
  const EEQuadword expected = {
    UINT64_C(0x7766554433221100),
    UINT64_C(0xffeeddccbbaa9988)
  };
  system.eeCore().setGeneralRegister(1, {0x110, 0});
  REQUIRE(system.eeBus().writeData128(0x100, expected));

  runInstruction(
    &system,
    cop2MemoryInstruction(0x36, 1, 5, 0xfff0));

  const FPRegister *vector = system.vu0().fpRegisterValue(5);
  REQUIRE(vector->x.bits() == UINT32_C(0x33221100));
  REQUIRE(vector->y.bits() == UINT32_C(0x77665544));
  REQUIRE(vector->z.bits() == UINT32_C(0xbbaa9988));
  REQUIRE(vector->w.bits() == UINT32_C(0xffeeddcc));

  system.eeCore().setGeneralRegister(1, {0x200, 0});
  runInstruction(
    &system,
    cop2MemoryInstruction(0x3e, 1, 5, 0));

  EEQuadword stored;
  REQUIRE(system.eeBus().readData128(0x200, &stored));
  REQUIRE(stored.low == expected.low);
  REQUIRE(stored.high == expected.high);
}

TEST_CASE("EE COP2 quadword memory accesses require alignment")
{
  SECTION("LQC2 preserves its destination on address error")
  {
    NekoSystem system;
    system.vu0().loadFPRegisterBits(3, 1, 2, 3, 4);
    system.eeCore().setGeneralRegister(1, {0x101, 0});

    runInstruction(
      &system,
      cop2MemoryInstruction(0x36, 1, 3, 0));

    REQUIRE(
      system.eeCore().pendingException() ==
      EEException::AddressErrorLoadOrFetch);
    REQUIRE(system.eeCore().exceptionAddress() == 0x101);
    REQUIRE(
      system.eeCore().cop0Register(EECOP0Register::BadVAddr) ==
      0x101);
    const FPRegister *vector = system.vu0().fpRegisterValue(3);
    REQUIRE(vector->x.bits() == 1);
    REQUIRE(vector->y.bits() == 2);
    REQUIRE(vector->z.bits() == 3);
    REQUIRE(vector->w.bits() == 4);
  }

  SECTION("SQC2 preserves memory on address error")
  {
    NekoSystem system;
    system.vu0().loadFPRegisterBits(3, 1, 2, 3, 4);
    system.eeCore().setGeneralRegister(1, {0x101, 0});

    runInstruction(
      &system,
      cop2MemoryInstruction(0x3e, 1, 3, 0));

    REQUIRE(
      system.eeCore().pendingException() ==
      EEException::AddressErrorStore);
    REQUIRE(system.eeCore().exceptionAddress() == 0x101);
    EEQuadword stored;
    REQUIRE(system.eeBus().readData128(0x100, &stored));
    REQUIRE(stored.low == 0);
    REQUIRE(stored.high == 0);
  }
}

TEST_CASE("Interlocked EE COP2 moves wait for active VU0 micro execution")
{
  NekoSystem system;
  system.eeCore().setGeneralRegister(
    2,
    {UINT64_C(0x1122334455667788), UINT64_MAX});
  system.vu0().startMicroMode();

  runInstruction(
    &system,
    cop2TransferInstruction(0x05, 2, 3, true));

  REQUIRE(system.eeCore().programCounter() == 0);
  REQUIRE_FALSE(system.eeCore().hasLastInstruction());
  REQUIRE(system.vu0().fpRegisterValue(3)->x.bits() == 0);

  system.vu0().forceBreak();
  system.clockMasterCycle();

  REQUIRE(system.eeCore().programCounter() == 4);
  REQUIRE(system.eeCore().hasLastInstruction());
  REQUIRE(
    system.vu0().fpRegisterValue(3)->x.bits() ==
    UINT32_C(0x55667788));
}

TEST_CASE("Non-interlocked EE COP2 moves proceed during VU0 micro execution")
{
  NekoSystem system;
  system.eeCore().setGeneralRegister(
    2,
    {
      UINT64_C(0x7766554433221100),
      UINT64_C(0xffeeddccbbaa9988)
    });
  system.vu0().startMicroMode();

  runInstruction(
    &system,
    cop2TransferInstruction(0x05, 2, 3));

  REQUIRE(system.eeCore().programCounter() == 4);
  REQUIRE(system.eeCore().hasLastInstruction());
  REQUIRE(
    system.vu0().fpRegisterValue(3)->x.bits() ==
    UINT32_C(0x33221100));
}

TEST_CASE("VU0 M bit releases interlocked COP2 writes during micro execution")
{
  NekoSystem system;
  system.vu0().writeMicroInstruction(
    0,
    VPU_LOWER_NOP,
    VPU_M_BIT | VPU_NOP);
  system.vu0().startMicroMode();
  system.eeCore().setGeneralRegister(
    2,
    {
      UINT64_C(0x7766554433221100),
      UINT64_C(0xffeeddccbbaa9988)
    });

  runInstruction(
    &system,
    cop2TransferInstruction(0x05, 2, 3, true));
  REQUIRE(system.eeCore().programCounter() == 0);

  system.clockMasterCycle();

  REQUIRE(system.eeCore().programCounter() == 4);
  REQUIRE(
    system.vu0().fpRegisterValue(3)->x.bits() ==
    UINT32_C(0x33221100));
}

TEST_CASE("EE COP2 transfer results survive save-state restoration")
{
  NekoSystem system;
  system.eeCore().setGeneralRegister(
    2,
    {
      UINT64_C(0x7766554433221100),
      UINT64_C(0xffeeddccbbaa9988)
    });
  runInstruction(
    &system,
    cop2TransferInstruction(0x05, 2, 3));
  const std::vector<std::uint8_t> saved = system.saveState();

  system.vu0().loadFPRegisterBits(3, 0, 0, 0, 0);
  system.loadState(saved);
  system.eeCore().setGeneralRegister(4, {});
  runInstruction(
    &system,
    cop2TransferInstruction(0x01, 4, 3));

  REQUIRE((
    system.eeCore().generalRegister(4) ==
    EERegister128{
      UINT64_C(0x7766554433221100),
      UINT64_C(0xffeeddccbbaa9988)
    }));
}

TEST_CASE("EE CTC2 and CFC2 transfer VU0 integer and special state")
{
  NekoSystem system;
  system.eeCore().setGeneralRegister(
    2,
    {UINT64_C(0xaaaaaaaa8000ffff), UINT64_MAX});

  runInstruction(
    &system,
    cop2TransferInstruction(0x06, 2, 3));
  REQUIRE(system.vu0().intRegisterValue(3) == UINT16_C(0xffff));

  system.eeCore().setGeneralRegister(4, {});
  runInstruction(
    &system,
    cop2TransferInstruction(0x02, 4, 3));
  REQUIRE(system.eeCore().generalRegister(4).low == UINT64_C(0xffff));

  runInstruction(
    &system,
    cop2TransferInstruction(0x06, 2, 21));
  system.eeCore().setGeneralRegister(4, {});
  runInstruction(
    &system,
    cop2TransferInstruction(0x02, 4, 21));
  REQUIRE(
    system.eeCore().generalRegister(4).low ==
    UINT64_C(0xffffffff8000ffff));
}

TEST_CASE("EE COP2 flag control registers enforce their write masks")
{
  NekoSystem system;
  system.eeCore().setGeneralRegister(2, {UINT32_C(0xffffffff), 0});

  runInstruction(
    &system,
    cop2TransferInstruction(0x06, 2, 16));
  REQUIRE(system.vu0().statusFlagsValue() == UINT16_C(0x0fc0));

  runInstruction(
    &system,
    cop2TransferInstruction(0x06, 2, 18));
  REQUIRE(
    system.vu0().clippingFlagsValue() ==
    UINT32_C(0x00ffffff));
}

TEST_CASE("EE CTC2 FBRST controls and reports both vector units")
{
  NekoSystem system;
  system.vu0().startMicroMode();
  system.vu1().startMicroMode();
  system.eeCore().setGeneralRegister(
    2,
    {UINT32_C(0x00000d0d), 0});

  runInstruction(
    &system,
    cop2TransferInstruction(0x06, 2, 28));

  REQUIRE(system.vu0().getState() == VPU_STATE_STOP);
  REQUIRE(system.vu1().getState() == VPU_STATE_STOP);
  REQUIRE(system.vu0().dBitEnabled());
  REQUIRE(system.vu0().tBitEnabled());
  REQUIRE(system.vu1().dBitEnabled());
  REQUIRE(system.vu1().tBitEnabled());

  system.eeCore().setGeneralRegister(3, {});
  runInstruction(
    &system,
    cop2TransferInstruction(0x02, 3, 28));
  REQUIRE(
    system.eeCore().generalRegister(3).low ==
    UINT64_C(0x00000c0c));

  runInstruction(
    &system,
    cop2TransferInstruction(0x02, 3, 29));
  REQUIRE(
    (system.eeCore().generalRegister(3).low &
     UINT64_C(0x00000808)) ==
    UINT64_C(0x00000808));
}

TEST_CASE("EE CTC2 CMSAR1 starts VU1 and ignores writes while busy")
{
  NekoSystem system;
  system.eeCore().setGeneralRegister(2, {8, 0});

  runInstruction(
    &system,
    cop2TransferInstruction(0x06, 2, 31));
  REQUIRE(system.vu1().clockActive());
  REQUIRE(system.vu1().programCounter() == 16);

  system.eeCore().setGeneralRegister(2, {0x20, 0});
  runInstruction(
    &system,
    cop2TransferInstruction(0x06, 2, 31));
  REQUIRE(system.vu1().clockActive());
  REQUIRE(system.vu1().programCounter() != 0x20);
}

TEST_CASE("EE COP2 branches use VU1 activity and likely annulment")
{
  struct Contract
  {
    std::uint8_t condition;
    bool vu1Running;
    bool taken;
    bool likely;
  };
  const Contract contracts[] = {
    {0, false, true, false},
    {0, true, false, false},
    {1, false, false, false},
    {1, true, true, false},
    {2, false, true, true},
    {2, true, false, true},
    {3, false, false, true},
    {3, true, true, true}
  };

  for (const Contract &contract : contracts)
  {
    NekoSystem system;
    if (contract.vu1Running)
    {
      system.vu1().startMicroMode();
    }
    const std::uint32_t program[] = {
      cop2BranchInstruction(contract.condition, 2),
      UINT32_C(0x34020001),
      UINT32_C(0x34030002),
      UINT32_C(0x34040003)
    };
    for (std::size_t index = 0; index < 4; ++index)
    {
      system.eeBus().write32(index * 4, program[index]);
    }
    system.eeCore().startExecution(0);
    system.runMasterCycles(contract.taken ? 3 : 2);

    REQUIRE(
      system.eeCore().generalRegister(2).low ==
      (contract.likely && !contract.taken ? 0 : 1));
    REQUIRE(
      system.eeCore().programCounter() ==
      (contract.taken
        ? 16
        : contract.likely
          ? 12
          : 8));
  }
}
