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
    std::uint8_t rd)
  {
    return
      (static_cast<std::uint32_t>(rs) << 21) |
      (static_cast<std::uint32_t>(rt) << 16) |
      (static_cast<std::uint32_t>(rd) << 11) |
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

  void writeProgram(
    NekoSystem *system,
    const std::uint32_t *instructions,
    std::size_t count,
    std::uint32_t base = 0)
  {
    for (std::size_t index = 0; index < count; ++index)
    {
      system->eeBus().write32(
        base + static_cast<std::uint32_t>(index * 4),
        instructions[index]);
    }
  }

  void setLow(
    EECore *core,
    std::uint8_t index,
    std::uint64_t value)
  {
    core->setGeneralRegister(
      index,
      {value, UINT64_C(0xfeedfacecafebeef)});
  }
}

TEST_CASE("EE conditional branches execute architectural delay slots")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  setLow(&core, 1, 7);
  setLow(&core, 2, 7);
  const std::uint32_t program[] = {
    immediateInstruction(0x04, 1, 2, 2),
    immediateInstruction(0x0d, 0, 3, 1),
    immediateInstruction(0x0d, 0, 4, 2),
    immediateInstruction(0x0d, 0, 5, 3)
  };
  writeProgram(&system, program, 4);
  core.startExecution(0);

  system.clockMasterCycle();
  REQUIRE(core.programCounter() == 4);
  REQUIRE(core.generalRegister(3).low == 0);

  system.clockMasterCycle();
  REQUIRE(core.programCounter() == 12);
  REQUIRE(core.generalRegister(3).low == 1);
  REQUIRE(core.generalRegister(4).low == 0);

  system.clockMasterCycle();
  REQUIRE(core.generalRegister(5).low == 3);
}

TEST_CASE("EE fallthrough and likely annulment differ")
{
  SECTION("A normal untaken branch executes its delay slot")
  {
    NekoSystem system;
    setLow(&system.eeCore(), 1, 1);
    setLow(&system.eeCore(), 2, 2);
    const std::uint32_t program[] = {
      immediateInstruction(0x04, 1, 2, 2),
      immediateInstruction(0x0d, 0, 3, 1),
      immediateInstruction(0x0d, 0, 4, 2)
    };
    writeProgram(&system, program, 3);
    system.eeCore().startExecution(0);
    system.runMasterCycles(3);

    REQUIRE(system.eeCore().generalRegister(3).low == 1);
    REQUIRE(system.eeCore().generalRegister(4).low == 2);
  }

  SECTION("An untaken likely branch nullifies its delay slot")
  {
    NekoSystem system;
    setLow(&system.eeCore(), 1, 1);
    setLow(&system.eeCore(), 2, 2);
    const std::uint32_t program[] = {
      immediateInstruction(0x14, 1, 2, 2),
      immediateInstruction(0x0d, 0, 3, 1),
      immediateInstruction(0x0d, 0, 4, 2)
    };
    writeProgram(&system, program, 3);
    system.eeCore().startExecution(0);
    system.runMasterCycles(2);

    REQUIRE(system.eeCore().generalRegister(3).low == 0);
    REQUIRE(system.eeCore().generalRegister(4).low == 2);
    REQUIRE(system.eeCore().programCounter() == 12);
  }

  SECTION("A taken likely branch executes its delay slot")
  {
    NekoSystem system;
    setLow(&system.eeCore(), 1, 2);
    setLow(&system.eeCore(), 2, 2);
    const std::uint32_t program[] = {
      immediateInstruction(0x14, 1, 2, 2),
      immediateInstruction(0x0d, 0, 3, 1),
      0,
      immediateInstruction(0x0d, 0, 4, 2)
    };
    writeProgram(&system, program, 4);
    system.eeCore().startExecution(0);
    system.runMasterCycles(3);

    REQUIRE(system.eeCore().generalRegister(3).low == 1);
    REQUIRE(system.eeCore().generalRegister(4).low == 2);
  }
}

TEST_CASE("EE signed branch families use 64-bit conditions")
{
  struct Contract
  {
    std::uint8_t rt;
    std::uint64_t source;
    bool taken;
  };
  const Contract contracts[] = {
    {0x00, UINT64_MAX, true},
    {0x00, 0, false},
    {0x01, 0, true},
    {0x01, UINT64_MAX, false},
    {0x02, UINT64_MAX, true},
    {0x03, 0, true}
  };

  for (const Contract &contract : contracts)
  {
    NekoSystem system;
    setLow(&system.eeCore(), 1, contract.source);
    const std::uint32_t program[] = {
      immediateInstruction(0x01, 1, contract.rt, 2),
      immediateInstruction(0x0d, 0, 2, 1),
      immediateInstruction(0x0d, 0, 3, 2),
      immediateInstruction(0x0d, 0, 4, 3)
    };
    writeProgram(&system, program, 4);
    system.eeCore().startExecution(0);
    system.runMasterCycles(contract.taken ? 3 : 2);

    if (contract.rt >= 2 && !contract.taken)
    {
      REQUIRE(system.eeCore().generalRegister(2).low == 0);
    }
    else
    {
      REQUIRE(system.eeCore().generalRegister(2).low == 1);
    }
    REQUIRE(
      system.eeCore().programCounter() ==
      (contract.taken ? 16 : 8));
  }
}

TEST_CASE("EE jumps and links use the instruction after the delay slot")
{
  SECTION("JAL preserves the current 256 MB region")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(
      31,
      {0, UINT64_C(0xfeedfacecafebeef)});
    const std::uint32_t base = UINT32_C(0x80000000);
    system.eeBus().write32(
      base,
      UINT32_C(0x0c000004));
    system.eeBus().write32(
      base + 4,
      immediateInstruction(0x0d, 0, 1, 1));
    system.eeBus().write32(
      base + 16,
      immediateInstruction(0x0d, 0, 2, 2));
    core.startExecution(base);
    system.runMasterCycles(3);

    REQUIRE(core.generalRegister(1).low == 1);
    REQUIRE(core.generalRegister(2).low == 2);
    REQUIRE(core.generalRegister(31).low == base + 8);
    REQUIRE(
      core.generalRegister(31).high ==
      UINT64_C(0xfeedfacecafebeef));
  }

  SECTION("JALR captures its target before writing the link")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    setLow(&core, 1, 12);
    const std::uint32_t program[] = {
      registerInstruction(0x09, 1, 0, 5),
      immediateInstruction(0x0d, 0, 2, 1),
      0,
      immediateInstruction(0x0d, 0, 3, 2)
    };
    writeProgram(&system, program, 4);
    core.startExecution(0);
    system.runMasterCycles(3);

    REQUIRE(core.generalRegister(2).low == 1);
    REQUIRE(core.generalRegister(3).low == 2);
    REQUIRE(core.generalRegister(5).low == 8);
  }
}

TEST_CASE("EE branch link and target restrictions are deterministic")
{
  SECTION("REGIMM link writes register 31 even when not taken")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    setLow(&core, 1, 1);
    core.setGeneralRegister(
      31,
      {0, UINT64_C(0xfeedfacecafebeef)});
    const std::uint32_t program[] = {
      immediateInstruction(0x01, 1, 0x10, 2),
      immediateInstruction(0x0d, 0, 2, 1)
    };
    writeProgram(&system, program, 2);
    core.startExecution(0);
    system.clockMasterCycle();

    REQUIRE(core.generalRegister(31).low == 8);
    REQUIRE(
      core.generalRegister(31).high ==
      UINT64_C(0xfeedfacecafebeef));
    REQUIRE(core.programCounter() == 4);
  }

  SECTION("JALR rejects using the target as its link register")
  {
    NekoSystem system;
    setLow(&system.eeCore(), 1, 12);
    system.eeBus().write32(
      0,
      registerInstruction(0x09, 1, 0, 1));
    system.eeCore().startExecution(0);
    system.clockMasterCycle();

    REQUIRE(
      system.eeCore().stopReason() ==
      EEStopReason::UndefinedOperation);
    REQUIRE(system.eeCore().programCounter() == 0);
  }

  SECTION("A misaligned JR target faults when target fetch begins")
  {
    NekoSystem system;
    setLow(&system.eeCore(), 1, 3);
    const std::uint32_t program[] = {
      registerInstruction(0x08, 1, 0, 0),
      0
    };
    writeProgram(&system, program, 2);
    system.eeCore().startExecution(0);
    system.runMasterCycles(3);

    REQUIRE(system.eeCore().stopReason() == EEStopReason::None);
    REQUIRE(
      system.eeCore().pendingException() ==
      EEException::AddressErrorLoadOrFetch);
    REQUIRE(system.eeCore().exceptionAddress() == 3);
    REQUIRE(
      system.eeCore().programCounter() ==
      EEExceptionVector::BOOTSTRAP_GENERAL);
    REQUIRE(
      system.eeCore().cop0Register(EECOP0Register::EPC) == 3);
    REQUIRE(
      (system.eeCore().cop0Register(EECOP0Register::Cause) &
        EECOP0Cause::BRANCH_DELAY) == 0);
  }
}

TEST_CASE("EE rejects forbidden delay-slot instructions")
{
  SECTION("A branch cannot occupy another branch delay slot")
  {
    NekoSystem system;
    const std::uint32_t program[] = {
      UINT32_C(0x08000003),
      immediateInstruction(0x04, 0, 0, 1)
    };
    writeProgram(&system, program, 2);
    system.eeCore().startExecution(0);
    system.runMasterCycles(2);

    REQUIRE(
      system.eeCore().stopReason() ==
      EEStopReason::UndefinedOperation);
    REQUIRE(system.eeCore().programCounter() == 4);
  }

  SECTION("A taken branch-likely cannot put MTSA in its delay slot")
  {
    NekoSystem system;
    setLow(&system.eeCore(), 1, 4);
    const std::uint32_t program[] = {
      immediateInstruction(0x14, 0, 0, 1),
      registerInstruction(0x29, 1, 0, 0)
    };
    writeProgram(&system, program, 2);
    system.eeCore().startExecution(0);
    system.runMasterCycles(2);

    REQUIRE(
      system.eeCore().stopReason() ==
      EEStopReason::UndefinedOperation);
    REQUIRE(system.eeCore().programCounter() == 4);
  }
}

TEST_CASE("EE host halt resumes a pending branch delay slot")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  const std::uint32_t program[] = {
    UINT32_C(0x08000003),
    immediateInstruction(0x0d, 0, 1, 1),
    immediateInstruction(0x0d, 0, 2, 2),
    immediateInstruction(0x0d, 0, 3, 3)
  };
  writeProgram(&system, program, 4);
  core.startExecution(0);
  system.clockMasterCycle();

  core.haltExecution();
  core.startExecution(core.programCounter());
  system.runMasterCycles(2);

  REQUIRE(core.generalRegister(1).low == 1);
  REQUIRE(core.generalRegister(2).low == 0);
  REQUIRE(core.generalRegister(3).low == 3);
}
