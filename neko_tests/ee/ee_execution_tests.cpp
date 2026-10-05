#include <cstdint>

#include "catch.hpp"
#include "neko_system.hpp"

namespace
{
  std::uint32_t immediateInstruction(
    std::uint8_t opcode,
    std::uint8_t source,
    std::uint8_t target,
    std::uint16_t immediate)
  {
    return
      (static_cast<std::uint32_t>(opcode) << 26) |
      (static_cast<std::uint32_t>(source) << 21) |
      (static_cast<std::uint32_t>(target) << 16) |
      immediate;
  }

  std::uint32_t registerInstruction(
    std::uint8_t function,
    std::uint8_t source,
    std::uint8_t target,
    std::uint8_t destination)
  {
    return
      (static_cast<std::uint32_t>(source) << 21) |
      (static_cast<std::uint32_t>(target) << 16) |
      (static_cast<std::uint32_t>(destination) << 11) |
      function;
  }
}

TEST_CASE("EE bounded execution reports cycle-limit progress")
{
  NekoSystem system;
  system.eeBus().write32(
    0,
    immediateInstruction(0x0d, 0, 1, 1));
  system.eeBus().write32(
    4,
    immediateInstruction(0x0d, 0, 2, 2));
  system.eeBus().write32(
    8,
    immediateInstruction(0x0d, 0, 3, 3));
  system.eeCore().startExecution(0);

  const EEExecutionResult result = system.runEE(2);

  REQUIRE(result.masterCycles == 2);
  REQUIRE(result.eeCycles == 2);
  REQUIRE(result.instructions == 3);
  REQUIRE(result.cycleLimitReached);
  REQUIRE(result.state == EEExecutionState::Running);
  REQUIRE(result.stopReason == EEStopReason::None);
  REQUIRE(result.programCounter == 12);
  REQUIRE(result.pendingException == EEException::None);
  REQUIRE(result.exceptionAddress == 0);
  REQUIRE(system.masterClockScheduler().currentCycle() == 2);
}

TEST_CASE("EE bounded execution reports architectural stops")
{
  NekoSystem system;
  system.eeBus().write32(0, 0);
  system.eeBus().write32(4, UINT32_C(0x40003800));
  system.eeCore().startExecution(0);

  const EEExecutionResult result = system.runEE(20);

  REQUIRE(result.masterCycles == 2);
  REQUIRE(result.eeCycles == 2);
  REQUIRE(result.instructions == 1);
  REQUIRE_FALSE(result.cycleLimitReached);
  REQUIRE(result.state == EEExecutionState::Halted);
  REQUIRE(result.stopReason == EEStopReason::UnsupportedInstruction);
  REQUIRE(result.programCounter == 4);
  REQUIRE(result.pendingException == EEException::None);
}

TEST_CASE("EE bounded execution reports pending exceptions")
{
  NekoSystem system;
  system.eeCore().startExecution(2);

  const EEExecutionResult result = system.runEE(5);

  REQUIRE(result.masterCycles == 1);
  REQUIRE(result.eeCycles == 1);
  REQUIRE(result.instructions == 0);
  REQUIRE_FALSE(result.cycleLimitReached);
  REQUIRE(result.state == EEExecutionState::Running);
  REQUIRE(result.stopReason == EEStopReason::None);
  REQUIRE(
    result.pendingException ==
    EEException::AddressErrorLoadOrFetch);
  REQUIRE(result.exceptionAddress == 2);
  REQUIRE(
    result.programCounter ==
    EEExceptionVector::BOOTSTRAP_GENERAL);
}

TEST_CASE("EE synchronization instructions execute scalarly")
{
  NekoSystem system;
  system.eeBus().write32(0, UINT32_C(0x0000000f));
  system.eeBus().write32(4, UINT32_C(0x0000040f));
  system.eeCore().startExecution(0);

  const EEExecutionResult loadStore =
    system.stepEEInstruction(1);
  REQUIRE(
    system.eeCore().lastInstruction().operation ==
    EEOperation::SynchronizeLoadStore);
  const EEExecutionResult pipeline =
    system.stepEEInstruction(1);

  REQUIRE(loadStore.instructions == 1);
  REQUIRE(loadStore.programCounter == 4);
  REQUIRE(
    system.eeCore().lastInstruction().operation ==
    EEOperation::SynchronizePipeline);
  REQUIRE(pipeline.instructions == 1);
  REQUIRE(pipeline.programCounter == 8);
}

TEST_CASE("EE SYNC preserves non-cached ordering without cache snooping")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  constexpr std::uint32_t cachedAddress = UINT32_C(0x80000100);
  constexpr std::uint32_t uncachedAddress = UINT32_C(0xa0000100);
  constexpr std::uint32_t acceleratedAddress =
    UINT32_C(0x30000100);
  constexpr std::uint32_t initial = UINT32_C(0x11111111);
  constexpr std::uint32_t cached = UINT32_C(0x22222222);
  constexpr std::uint32_t uncached = UINT32_C(0x33333333);
  constexpr std::uint32_t accelerated = UINT32_C(0x44444444);

  core.setCOP0Register(
    EECOP0Register::Status,
    0);
  core.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::DATA_CACHE_ENABLE);
  core.setTLBEntry(
    0,
    {
      EECOP0PageMask::SIZE_4_KIB,
      UINT32_C(0x30000000),
      {UINT32_C(0x0000003f)},
      {UINT32_C(0x0000003f)}
    });
  core.setGeneralRegister(1, {cachedAddress, 0});
  core.setGeneralRegister(2, {uncachedAddress, 0});
  core.setGeneralRegister(3, {acceleratedAddress, 0});
  core.setGeneralRegister(4, {cached, 0});
  core.setGeneralRegister(5, {uncached, 0});
  core.setGeneralRegister(6, {accelerated, 0});
  REQUIRE(
    system.eeMemorySystem()
      .translateDataAddress(
        acceleratedAddress,
        EEDataAccessDirection::Load,
        {EEPrivilegeMode::Kernel, false, false})
      .cacheRoute ==
    EECacheRoute::UncachedAccelerated);
  system.eeBus().write32(0x100, initial);
  system.eeBus().write32(
    0,
    immediateInstruction(0x2b, 1, 4, 0));
  system.eeBus().write32(4, UINT32_C(0x0000000f));
  system.eeBus().write32(
    8,
    immediateInstruction(0x23, 3, 7, 0));
  system.eeBus().write32(
    12,
    immediateInstruction(0x2b, 2, 5, 0));
  system.eeBus().write32(16, UINT32_C(0x0000000f));
  system.eeBus().write32(
    20,
    immediateInstruction(0x23, 1, 8, 0));
  system.eeBus().write32(
    24,
    immediateInstruction(0x2b, 3, 6, 0));
  system.eeBus().write32(28, UINT32_C(0x0000000f));
  system.eeBus().write32(
    32,
    immediateInstruction(0x23, 2, 9, 0));
  core.startExecution(EEMemoryMap::KSEG0_BASE);

  REQUIRE(system.stepEEInstruction(1).instructions == 1);
  REQUIRE(system.eeBus().read32(0x100) == initial);
  REQUIRE(system.stepEEInstruction(1).instructions == 1);
  REQUIRE(system.stepEEInstruction(1).instructions == 1);
  REQUIRE(core.generalRegister(7).low == initial);

  REQUIRE(system.stepEEInstruction(1).instructions == 1);
  REQUIRE(system.eeBus().read32(0x100) == uncached);
  REQUIRE(system.stepEEInstruction(1).instructions == 1);
  REQUIRE(system.stepEEInstruction(1).instructions == 1);
  REQUIRE(core.generalRegister(8).low == cached);

  REQUIRE(system.stepEEInstruction(1).instructions == 1);
  REQUIRE(system.eeBus().read32(0x100) == accelerated);
  REQUIRE(system.stepEEInstruction(1).instructions == 1);
  REQUIRE(system.stepEEInstruction(1).instructions == 1);
  REQUIRE(core.generalRegister(9).low == accelerated);

  const EECacheLine &line =
    system.eeMemorySystem().dataCacheLine(4, 0);
  REQUIRE(line.valid);
  REQUIRE(line.dirty);
  REQUIRE(line.data[0] == 0x22);
  REQUIRE(line.data[1] == 0x22);
  REQUIRE(line.data[2] == 0x22);
  REQUIRE(line.data[3] == 0x22);
}

TEST_CASE("EE SYNC.P does not wait for integer multiply completion")
{
  NekoSystem system;
  system.eeCore().setGeneralRegister(1, {3, 0});
  system.eeCore().setGeneralRegister(2, {4, 0});
  system.eeBus().write32(
    0,
    registerInstruction(0x18, 1, 2, 3));
  system.eeBus().write32(4, UINT32_C(0x0000040f));
  system.eeCore().startExecution(0);

  system.clockMasterCycle();
  const EEExecutionResult synchronization =
    system.stepEEInstruction(1);

  REQUIRE(synchronization.instructions == 1);
  REQUIRE(synchronization.programCounter == 8);
  REQUIRE(
    system.eeCore().lastInstruction().operation ==
    EEOperation::SynchronizePipeline);
  REQUIRE(
    system.eeCore()
      .lastIssueSelection()
      .instructionCount == 2);
}

TEST_CASE("EE instruction stepping follows repeated branch addresses")
{
  NekoSystem system;
  system.eeBus().write32(
    0,
    immediateInstruction(0x04, 0, 0, 0xffff));
  system.eeBus().write32(4, 0);
  system.eeCore().startExecution(0);

  const EEExecutionResult firstGroup =
    system.stepEEInstruction(1);
  const EEExecutionResult repeatedBranch =
    system.stepEEInstruction(1);

  REQUIRE(firstGroup.instructions == 2);
  REQUIRE(firstGroup.programCounter == 0);
  REQUIRE_FALSE(firstGroup.cycleLimitReached);
  REQUIRE(repeatedBranch.instructions == 2);
  REQUIRE(repeatedBranch.programCounter == 0);
  REQUIRE_FALSE(repeatedBranch.cycleLimitReached);
}

TEST_CASE("EE instruction stepping waits through execution latency")
{
  NekoSystem system;
  system.eeCore().setGeneralRegister(1, {3, 0});
  system.eeCore().setGeneralRegister(2, {4, 0});
  system.eeBus().write32(
    0,
    registerInstruction(0x18, 1, 2, 3));
  system.eeBus().write32(
    4,
    immediateInstruction(0x0d, 0, 4, 1));
  system.eeBus().write32(
    8,
    registerInstruction(0x12, 0, 0, 5));
  system.eeCore().startExecution(0);

  const EEExecutionResult pair =
    system.stepEEInstruction(1);
  const EEExecutionResult following =
    system.stepEEInstruction(10);

  REQUIRE(pair.masterCycles == 1);
  REQUIRE(pair.instructions == 2);
  REQUIRE(system.eeCore().generalRegister(4).low == 1);
  REQUIRE(following.masterCycles == 4);
  REQUIRE(following.eeCycles == 4);
  REQUIRE(following.instructions == 1);
  REQUIRE_FALSE(following.cycleLimitReached);
  REQUIRE(following.programCounter == 12);
  REQUIRE(system.eeCore().generalRegister(5).low == 12);
}

TEST_CASE("EE instruction stepping can stop at its cycle bound")
{
  NekoSystem system;
  system.eeCore().setGeneralRegister(1, {3, 0});
  system.eeCore().setGeneralRegister(2, {4, 0});
  system.eeBus().write32(
    0,
    registerInstruction(0x18, 1, 2, 3));
  system.eeBus().write32(
    4,
    registerInstruction(0x12, 0, 0, 4));
  system.eeCore().startExecution(0);
  REQUIRE(system.stepEEInstruction(1).instructions == 1);

  const EEExecutionResult result =
    system.stepEEInstruction(2);

  REQUIRE(result.masterCycles == 2);
  REQUIRE(result.eeCycles == 2);
  REQUIRE(result.instructions == 0);
  REQUIRE(result.cycleLimitReached);
  REQUIRE(result.state == EEExecutionState::Running);
  REQUIRE(result.programCounter == 4);
}

TEST_CASE("EE bounded execution handles a zero cycle budget")
{
  NekoSystem system;
  system.eeBus().write32(0, 0);
  system.eeCore().startExecution(0);

  const EEExecutionResult result = system.runEE(0);

  REQUIRE(result.masterCycles == 0);
  REQUIRE(result.eeCycles == 0);
  REQUIRE(result.instructions == 0);
  REQUIRE(result.cycleLimitReached);
  REQUIRE(result.state == EEExecutionState::Running);
  REQUIRE(result.programCounter == 0);
}

TEST_CASE("EE execution control reports an already halted core")
{
  NekoSystem system;

  const EEExecutionResult step = system.stepEEInstruction(1);
  const EEExecutionResult run = system.runEE(1);

  REQUIRE(step.masterCycles == 0);
  REQUIRE(step.eeCycles == 0);
  REQUIRE(step.instructions == 0);
  REQUIRE_FALSE(step.cycleLimitReached);
  REQUIRE(step.state == EEExecutionState::Halted);
  REQUIRE(step.stopReason == EEStopReason::None);
  REQUIRE(step.programCounter == EEReset::VECTOR);
  REQUIRE(run.masterCycles == 0);
  REQUIRE(run.eeCycles == 0);
  REQUIRE(run.instructions == 0);
  REQUIRE_FALSE(run.cycleLimitReached);
  REQUIRE(run.state == EEExecutionState::Halted);
}
