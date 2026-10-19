#include <cstdint>
#include <stdexcept>
#include <vector>

#include "catch.hpp"
#include "neko_system.hpp"

namespace
{
  void romInstruction(
    std::vector<std::uint8_t> &image,
    std::size_t offset,
    std::uint32_t instruction)
  {
    for (std::size_t byte = 0; byte < 4; ++byte)
    {
      image[offset + byte] =
        static_cast<std::uint8_t>(instruction >> (8 * byte));
    }
  }

  std::vector<std::uint8_t> countingROM()
  {
    std::vector<std::uint8_t> image(BootROMImage::SIZE, 0);
    for (std::size_t offset = 0; offset < 1024; offset += 4)
    {
      romInstruction(image, offset, 0x24420001); // addiu r2, r2, 1
    }
    return image;
  }

  void requireSameState(const NekoSystem &left, const NekoSystem &right)
  {
    const bool equal = left.saveState() == right.saveState();
    REQUIRE(equal);
  }
}

TEST_CASE("Dual CPU BIOS zero budget releases both reset vectors after cold reset",
  "[system][bios][dual-cpu]")
{
  NekoSystem system;
  system.installBootROM(std::vector<std::uint8_t>(BootROMImage::SIZE, 0xff));
  const auto oldROM = system.eeBus().bootROMIdentity();
  system.eeBus().write32(0x100, 0x12345678);
  REQUIRE(system.iopBus().write32(0x100, 0x87654321).status ==
    IOPBusStatus::Completed);
  REQUIRE(system.eeBus().writeData32(EEMemoryMap::SIF_BASE, 0x13572468));
  REQUIRE(system.sif().pushSIF1Quadword({{1, 2, 3, 4}}) ==
    SIFTransferOutcome::Completed);
  system.eeCore().setGeneralRegister(29, {0x1000, 0});
  system.runMasterCycles(3);
  system.startTrace();

  auto image = countingROM();
  const auto result = system.runDualCPUBIOS(image, 0);
  REQUIRE(result.masterCycles == 0);
  REQUIRE(result.cycleLimitReached);
  REQUIRE(result.ee.masterCycles == 0);
  REQUIRE(result.ee.eeCycles == 0);
  REQUIRE(result.ee.instructions == 0);
  REQUIRE(result.ee.cycleLimitReached);
  REQUIRE(result.ee.state == EEExecutionState::Running);
  REQUIRE(result.ee.stopReason == EEStopReason::None);
  REQUIRE(result.ee.programCounter == EEReset::VECTOR);
  REQUIRE(result.ee.pendingException == EEException::None);
  REQUIRE(result.iop.iopCycles == 0);
  REQUIRE(result.iop.instructions == 0);
  REQUIRE(result.iop.state == IOPExecutionState::Running);
  REQUIRE(result.iop.stopReason == IOPStopReason::None);
  REQUIRE(result.iop.programCounter == IOPReset::VECTOR);
  REQUIRE(result.iop.exception.kind == IOPException::None);
  REQUIRE(result.iop.storeFailure.width == IOPStoreWidth::None);
  REQUIRE(system.masterClockScheduler().currentCycle() == 0);
  REQUIRE(system.eeBus().bootROMIdentity() != oldROM);
  REQUIRE(system.eeBus().bootROMIdentity() == system.iopBus().romIdentity());
  REQUIRE_FALSE(system.traceEnabled());
  REQUIRE(system.trace().empty());

  NekoSystem expected;
  expected.installBootROM(image);
  expected.reset();
  expected.eeCore().startExecution(EEReset::VECTOR);
  expected.iopCore().startExecution(IOPReset::VECTOR);
  requireSameState(system, expected);
  image[0] = 0xff;
  REQUIRE(system.eeBus().read32(EEMemoryMap::BOOT_ROM_BASE) == 0x24420001);
  REQUIRE(system.iopCore().readData32(IOPReset::VECTOR).value == 0x24420001);
}

TEST_CASE("Dual CPU BIOS uses one exact master budget and the existing clock phase",
  "[system][bios][dual-cpu]")
{
  const auto image = countingROM();
  for (const std::uint64_t budget : {0, 1, 7, 8, 9, 16, 17, 65})
  {
    CAPTURE(budget);
    NekoSystem system;
    const auto result = system.runDualCPUBIOS(image, budget);
    const std::uint64_t iopInstructions = (budget + 7) / 8;
    REQUIRE(result.masterCycles == budget);
    REQUIRE(result.cycleLimitReached);
    REQUIRE(result.ee.masterCycles == budget);
    REQUIRE(result.ee.eeCycles == budget);
    REQUIRE(result.ee.instructions == budget);
    REQUIRE(result.ee.cycleLimitReached);
    REQUIRE(result.ee.state == EEExecutionState::Running);
    REQUIRE(result.ee.stopReason == EEStopReason::None);
    REQUIRE(result.ee.pendingException == EEException::None);
    REQUIRE(result.ee.programCounter == EEReset::VECTOR + 4 * budget);
    REQUIRE(system.eeCore().generalRegister(2).low == budget);
    REQUIRE(result.iop.iopCycles == iopInstructions);
    REQUIRE(result.iop.instructions == iopInstructions);
    REQUIRE(result.iop.state == IOPExecutionState::Running);
    REQUIRE(result.iop.stopReason == IOPStopReason::None);
    REQUIRE(result.iop.exception.kind == IOPException::None);
    REQUIRE(result.iop.programCounter == IOPReset::VECTOR + 4 * iopInstructions);
    REQUIRE(system.iopCore().generalRegister(2) == iopInstructions);
    REQUIRE(system.masterClockScheduler().currentCycle() == budget);

    NekoSystem expected;
    expected.installBootROM(image);
    expected.reset();
    expected.eeCore().startExecution(EEReset::VECTOR);
    expected.iopCore().startExecution(IOPReset::VECTOR);
    expected.runMasterCycles(budget);
    requireSameState(system, expected);
  }
}

TEST_CASE("Dual CPU bounded continuation preserves phase and reports per-call progress",
  "[system][bios][dual-cpu]")
{
  const auto image = countingROM();
  NekoSystem system;
  system.runDualCPUBIOS(image, 17);
  const auto beforeZero = system.saveState();
  const auto zero = system.runDualCPU(0);
  REQUIRE(zero.masterCycles == 0);
  REQUIRE(zero.cycleLimitReached);
  REQUIRE(zero.ee.eeCycles == 0);
  REQUIRE(zero.ee.instructions == 0);
  REQUIRE(zero.iop.iopCycles == 0);
  REQUIRE(zero.iop.instructions == 0);
  const bool zeroUnchanged = system.saveState() == beforeZero;
  REQUIRE(zeroUnchanged);

  const auto continued = system.runDualCPU(16);
  REQUIRE(continued.masterCycles == 16);
  REQUIRE(continued.cycleLimitReached);
  REQUIRE(continued.ee.masterCycles == 16);
  REQUIRE(continued.ee.eeCycles == 16);
  REQUIRE(continued.ee.instructions == 16);
  REQUIRE(continued.iop.iopCycles == 2);
  REQUIRE(continued.iop.instructions == 2);
  REQUIRE(system.masterClockScheduler().currentCycle() == 33);
  REQUIRE(system.eeCore().generalRegister(2).low == 33);
  REQUIRE(system.iopCore().generalRegister(2) == 5);

  NekoSystem uninterrupted;
  uninterrupted.runDualCPUBIOS(image, 33);
  requireSameState(system, uninterrupted);
}

TEST_CASE("Dual CPU runner returns without advancing an already stopped pair",
  "[system][bios][dual-cpu]")
{
  NekoSystem system;
  system.runDualCPUBIOS(countingROM(), 1);
  SECTION("EE halted")
  {
    system.eeCore().haltExecution();
  }
  SECTION("IOP halted")
  {
    system.iopCore().haltExecution();
  }
  SECTION("both halted")
  {
    system.eeCore().haltExecution();
    system.iopCore().haltExecution();
  }
  const auto before = system.saveState();
  for (const std::uint64_t budget : {0, 100})
  {
    const auto result = system.runDualCPU(budget);
    REQUIRE(result.masterCycles == 0);
    REQUIRE_FALSE(result.cycleLimitReached);
    REQUIRE_FALSE(result.ee.cycleLimitReached);
    REQUIRE(result.ee.eeCycles == 0);
    REQUIRE(result.ee.instructions == 0);
    REQUIRE(result.iop.iopCycles == 0);
    REQUIRE(result.iop.instructions == 0);
    REQUIRE(result.ee.state == system.eeCore().executionState());
    REQUIRE(result.ee.stopReason == system.eeCore().stopReason());
    REQUIRE(result.iop.state == system.iopCore().executionState());
    REQUIRE(result.iop.stopReason == system.iopCore().stopReason());
    const bool unchanged = system.saveState() == before;
    REQUIRE(unchanged);
  }
}

TEST_CASE("Dual CPU runner finishes the master cycle before reporting an EE host stop",
  "[system][bios][dual-cpu]")
{
  NekoSystem system;
  system.runDualCPUBIOS(countingROM(), 0);
  system.eeBus().write32(0, 0x40003800); // unsupported EE COP0 register
  system.eeCore().startExecution(0xa0000000);
  const auto result = system.runDualCPU(1);
  REQUIRE(result.masterCycles == 1);
  REQUIRE_FALSE(result.cycleLimitReached);
  REQUIRE_FALSE(result.ee.cycleLimitReached);
  REQUIRE(result.ee.eeCycles == 1);
  REQUIRE(result.ee.instructions == 0);
  REQUIRE(result.ee.state == EEExecutionState::Halted);
  REQUIRE(result.ee.stopReason == EEStopReason::UnsupportedInstruction);
  REQUIRE(result.ee.programCounter == 0xa0000000);
  REQUIRE(result.ee.pendingException == EEException::None);
  REQUIRE(result.iop.iopCycles == 1);
  REQUIRE(result.iop.instructions == 1);
  REQUIRE(result.iop.state == IOPExecutionState::Running);
  REQUIRE(system.iopCore().generalRegister(2) == 1);
  REQUIRE(system.masterClockScheduler().currentCycle() == 1);
}

TEST_CASE("Dual CPU runner reports an IOP rejected store without halting the EE",
  "[system][bios][dual-cpu]")
{
  NekoSystem system;
  system.runDualCPUBIOS(countingROM(), 0);
  REQUIRE(system.iopBus().write32(0, 0x3c011000).status ==
    IOPBusStatus::Completed); // lui r1, 0x1000
  REQUIRE(system.iopBus().write32(4, 0xac200000).status ==
    IOPBusStatus::Completed); // sw r0, 0(r1)
  system.iopCore().startExecution(0xa0000000);
  const auto result = system.runDualCPU(9);
  REQUIRE(result.masterCycles == 9);
  REQUIRE_FALSE(result.cycleLimitReached);
  REQUIRE_FALSE(result.ee.cycleLimitReached);
  REQUIRE(result.ee.state == EEExecutionState::Running);
  REQUIRE(result.ee.eeCycles == 9);
  REQUIRE(result.ee.instructions == 9);
  REQUIRE(system.eeCore().generalRegister(2).low == 9);
  REQUIRE(result.iop.iopCycles == 2);
  REQUIRE(result.iop.instructions == 1);
  REQUIRE(result.iop.state == IOPExecutionState::Halted);
  REQUIRE(result.iop.stopReason == IOPStopReason::StoreFailure);
  REQUIRE(result.iop.programCounter == 0xa0000004);
  REQUIRE(result.iop.exception.kind == IOPException::None);
  REQUIRE(result.iop.storeFailure.width == IOPStoreWidth::Word);
  REQUIRE(result.iop.storeFailure.value == 0);
  REQUIRE(result.iop.storeFailure.access.outcome == IOPAccessOutcome::Unmapped);
  REQUIRE(result.iop.storeFailure.access.virtualAddress == 0x10000000);
  REQUIRE(result.iop.storeFailure.access.physicalAddress == 0x10000000);
}

TEST_CASE("Dual CPU runner lets both guest exception handlers execute",
  "[system][bios][dual-cpu]")
{
  std::vector<std::uint8_t> image(BootROMImage::SIZE, 0);
  romInstruction(image, 0, 0x0000000c); // syscall
  romInstruction(image, 0x380, 0x24030033); // EE handler: addiu r3, r0, 0x33
  romInstruction(image, 0x180, 0x24030044); // IOP handler: addiu r3, r0, 0x44
  NekoSystem system;
  const auto entry = system.runDualCPUBIOS(image, 1);
  REQUIRE(entry.masterCycles == 1);
  REQUIRE(entry.cycleLimitReached);
  REQUIRE(entry.ee.pendingException == EEException::SystemCall);
  REQUIRE(entry.ee.programCounter == EEExceptionVector::BOOTSTRAP_GENERAL);
  REQUIRE(entry.iop.exception.kind == IOPException::SystemCall);
  REQUIRE(entry.iop.programCounter == IOPExceptionVector::BOOT_GENERAL);
  REQUIRE(entry.iop.instructions == 0);
  const auto beforeZero = system.saveState();
  const auto zero = system.runDualCPU(0);
  REQUIRE(zero.ee.pendingException == EEException::SystemCall);
  REQUIRE(zero.iop.exception.kind == IOPException::SystemCall);
  const bool unchanged = system.saveState() == beforeZero;
  REQUIRE(unchanged);

  const auto handled = system.runDualCPU(8);
  REQUIRE(handled.masterCycles == 8);
  REQUIRE(handled.cycleLimitReached);
  REQUIRE(handled.ee.state == EEExecutionState::Running);
  REQUIRE(handled.iop.state == IOPExecutionState::Running);
  REQUIRE(handled.iop.iopCycles == 1);
  REQUIRE(handled.iop.instructions == 1);
  REQUIRE(handled.iop.exception.kind == IOPException::None);
  REQUIRE(system.eeCore().generalRegister(3).low == 0x33);
  REQUIRE(system.iopCore().generalRegister(3) == 0x44);
  REQUIRE(system.eeCore().cop0Register(EECOP0Register::EPC) == EEReset::VECTOR);
  REQUIRE(system.iopCore().exceptionProgramCounter() == IOPReset::VECTOR);
}

TEST_CASE("Dual CPU BIOS rejects invalid admission before any machine mutation",
  "[system][bios][dual-cpu]")
{
  NekoSystem system;
  const auto image = countingROM();
  system.installBootROM(image);
  const auto identity = system.eeBus().bootROMIdentity();
  system.eeBus().write32(0x100, 0x12345678);
  REQUIRE(system.iopBus().write32(0x100, 0x87654321).status ==
    IOPBusStatus::Completed);
  REQUIRE(system.eeBus().writeData32(EEMemoryMap::SIF_BASE, 0xabcdef01));
  system.startTrace();
  system.runMasterCycles(3);
  std::vector<std::uint8_t> invalid;
  SECTION("empty ROM") {}
  SECTION("short ROM") { invalid.resize(BootROMImage::SIZE - 1); }
  SECTION("oversized ROM") { invalid.resize(BootROMImage::SIZE + 1); }
  SECTION("EE running") { system.eeCore().startExecution(EEReset::VECTOR); }
  SECTION("IOP running") { system.iopCore().startExecution(IOPReset::VECTOR); }
  const auto before = system.saveState();
  const auto traceHash = system.traceHash();
  const auto traceSize = system.trace().size();
  if (system.eeCore().executionState() == EEExecutionState::Running ||
      system.iopCore().executionState() == IOPExecutionState::Running)
  {
    REQUIRE_THROWS_AS(system.runDualCPUBIOS(image, 1), std::logic_error);
  }
  else
  {
    REQUIRE_THROWS_AS(system.runDualCPUBIOS(invalid, 1), std::invalid_argument);
  }
  const bool unchanged = system.saveState() == before;
  REQUIRE(unchanged);
  REQUIRE(system.eeBus().bootROMIdentity() == identity);
  REQUIRE(system.iopBus().romIdentity() == identity);
  REQUIRE(system.traceEnabled());
  REQUIRE(system.traceHash() == traceHash);
  REQUIRE(system.trace().size() == traceSize);
}

TEST_CASE("Dual CPU runner continues an installed ROM restore without restarting",
  "[system][bios][dual-cpu]")
{
  const auto image = countingROM();
  NekoSystem original;
  original.runDualCPUBIOS(image, 9);
  NekoSystem restored;
  restored.installBootROM(image);
  restored.loadState(original.saveState());
  const auto uninterrupted = original.runDualCPU(16);
  const auto continued = restored.runDualCPU(16);
  REQUIRE(continued.masterCycles == 16);
  REQUIRE(continued.cycleLimitReached);
  REQUIRE(continued.ee.eeCycles == uninterrupted.ee.eeCycles);
  REQUIRE(continued.ee.instructions == uninterrupted.ee.instructions);
  REQUIRE(continued.ee.programCounter == uninterrupted.ee.programCounter);
  REQUIRE(continued.iop.iopCycles == 2);
  REQUIRE(continued.iop.instructions == uninterrupted.iop.instructions);
  REQUIRE(continued.iop.programCounter == uninterrupted.iop.programCounter);
  requireSameState(original, restored);
}
