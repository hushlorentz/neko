#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "ee_core.hpp"
#include "neko_system.hpp"

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
