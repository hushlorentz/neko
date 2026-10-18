#include <algorithm>
#include <set>

#include "catch.hpp"
#include "iop_bus.hpp"
#include "iop_core.hpp"
#include "iop_semantic_fixtures.hpp"

namespace
{
  void requireExecution(
    IOPBus &bus, const IOPCore &core, const IOPProgramRunResult &result,
    const IOPSemanticFixtures::Fixture &fixture)
  {
    const auto &expected = fixture.expected;
    REQUIRE(result.entryPoint == fixture.config.entryPoint);
    REQUIRE(result.stackPointer == fixture.config.stackPointer);
    REQUIRE(result.returnAddress == fixture.config.returnAddress);
    REQUIRE(result.outcome == expected.outcome);
    REQUIRE(result.instructions == expected.instructions);
    REQUIRE(result.iopCycles == expected.cycles);
    REQUIRE(result.masterCycles == expected.masterCycles);
    REQUIRE_FALSE(result.cycleLimitReached);
    REQUIRE(result.state == IOPExecutionState::Halted);
    REQUIRE(result.stopReason == IOPStopReason::HostHalt);
    REQUIRE(result.programCounter == expected.programCounter);
    REQUIRE(result.exitCode == 0);
    REQUIRE(result.exception.kind == expected.exception.kind);
    REQUIRE(result.exception.badVirtualAddress == expected.exception.badVirtualAddress);
    REQUIRE(result.exception.inBranchDelaySlot == expected.exception.inBranchDelaySlot);
    REQUIRE(result.exception.coprocessor == expected.exception.coprocessor);
    REQUIRE(result.storeFailure.width == IOPStoreWidth::None);
    IOPSemanticFixtures::requireState(bus, core, fixture);
  }
}

TEST_CASE("Shared IOP semantic fixtures retain independent decode and layout expectations",
  "[iop][conformance][semantic][decode]")
{
  std::set<IOPOperation> operations;
  for (const auto &fixture : IOPSemanticFixtures::all())
  {
    INFO(fixture.name);
    REQUIRE_FALSE(fixture.code.empty());
    std::set<IOPAddress> codeAddresses;
    for (const auto &instruction : fixture.code)
    {
      CAPTURE(instruction.address);
      CAPTURE(instruction.word);
      REQUIRE(instruction.address % 4 == 0);
      REQUIRE(codeAddresses.insert(instruction.address).second);
      const auto decoded = decodeIOPInstruction(instruction.word);
      REQUIRE(decoded.disposition == instruction.disposition);
      REQUIRE(decoded.instruction.operation == instruction.operation);
      REQUIRE(decoded.instruction.raw == instruction.word);
      operations.insert(decoded.instruction.operation);
    }
    REQUIRE(codeAddresses.count(fixture.config.entryPoint) == 1);
    REQUIRE(fixture.expected.cycles >= fixture.expected.instructions);
    REQUIRE(fixture.expected.masterCycles == fixture.expected.cycles * 8);
    REQUIRE(fixture.expected.masterCycles <= fixture.config.maxMasterCycles);
    std::set<IOPAddress> dataAddresses;
    for (const auto &word : fixture.initialMemory)
    {
      CAPTURE(word.address);
      REQUIRE(word.address % 4 == 0);
      REQUIRE(dataAddresses.insert(word.address).second);
      REQUIRE(codeAddresses.count(word.address) == 0);
    }
    std::set<IOPAddress> changedAddresses;
    for (const auto &word : fixture.expected.changedMemory)
    {
      CAPTURE(word.address);
      REQUIRE(dataAddresses.count(word.address) == 1);
      REQUIRE(changedAddresses.insert(word.address).second);
    }
    std::set<std::size_t> registers;
    for (const auto &value : fixture.expected.registers)
    {
      CAPTURE(value.index);
      REQUIRE(value.index < static_cast<std::size_t>(IOPCore::GENERAL_REGISTER_COUNT));
      REQUIRE(registers.insert(value.index).second);
    }
  }
  for (const auto required : {
    IOPOperation::AddUnsigned, IOPOperation::AddImmediate,
    IOPOperation::ShiftRightArithmetic, IOPOperation::SetLessThanUnsigned,
    IOPOperation::Multiply, IOPOperation::Divide,
    IOPOperation::BranchEqual, IOPOperation::BranchNotEqual,
    IOPOperation::JumpAndLink, IOPOperation::JumpRegister,
    IOPOperation::LoadByte, IOPOperation::LoadByteUnsigned,
    IOPOperation::LoadHalfword, IOPOperation::LoadHalfwordUnsigned,
    IOPOperation::LoadWord, IOPOperation::StoreByte,
    IOPOperation::StoreHalfword, IOPOperation::StoreWord,
    IOPOperation::LoadWordLeft, IOPOperation::LoadWordRight,
    IOPOperation::StoreWordLeft, IOPOperation::StoreWordRight,
    IOPOperation::MoveFromCOP0, IOPOperation::MoveToCOP0,
    IOPOperation::ReturnFromException})
  {
    CAPTURE(static_cast<unsigned>(required));
    REQUIRE(operations.count(required) == 1);
  }
}

TEST_CASE("Shared IOP semantic fixtures execute with literal architectural signatures",
  "[iop][conformance][semantic][runner]")
{
  for (const auto &fixture : IOPSemanticFixtures::all())
  {
    INFO(fixture.name);
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    for (unsigned repetition = 0; repetition < 2; ++repetition)
    {
      CAPTURE(repetition);
      IOPSemanticFixtures::load(bus, fixture);
      const auto result = runIOPProgram(&core, fixture.config);
      requireExecution(bus, core, result, fixture);
    }
  }
}

TEST_CASE("Shared IOP control flow fixture reports an incorrectly selected branch",
  "[iop][conformance][semantic][runner]")
{
  for (const auto &original : IOPSemanticFixtures::all())
  {
    const auto branch = std::find_if(
      original.code.begin(), original.code.end(),
      [](const IOPSemanticFixtures::Instruction &instruction)
      {
        return instruction.operation == IOPOperation::JumpAndLink;
      });
    if (branch == original.code.end())
    {
      continue;
    }
    auto fixture = original;
    const auto guard = std::find_if(
      fixture.code.begin(), fixture.code.end(),
      [](const IOPSemanticFixtures::Instruction &instruction)
      {
        return instruction.operation == IOPOperation::BranchEqual;
      });
    REQUIRE(guard != fixture.code.end());
    REQUIRE(guard->word == 0x11280002);
    guard->word = 0x15280002; // bne r9, r8, 0x1020: fall into the failure path
    guard->operation = IOPOperation::BranchNotEqual;
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    IOPSemanticFixtures::load(bus, fixture);
    const auto result = runIOPProgram(&core, fixture.config);
    REQUIRE(result.outcome == IOPProgramOutcome::GuestReportedFailure);
    REQUIRE(result.exitCode == 1);
    REQUIRE(result.instructions == 22);
    REQUIRE(result.iopCycles == 22);
    REQUIRE(result.masterCycles == 176);
    REQUIRE(result.programCounter == fixture.config.returnAddress);
    REQUIRE(result.exception.kind == IOPException::None);
    return;
  }
  FAIL("Missing shared linked-call fixture.");
}
