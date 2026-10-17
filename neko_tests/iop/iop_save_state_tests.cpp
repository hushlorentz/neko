#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "catch.hpp"
#include "iop_core.hpp"
#include "iop_interrupt_controller.hpp"
#include "iop_timer_timing.hpp"
#include "neko_system.hpp"
#include "save_state_mutation.hpp"

namespace
{
  void loadInstruction(NekoSystem *system, IOPAddress address, IOPWord word)
  {
    REQUIRE(system->iopBus().write32(address, word).status ==
      IOPBusStatus::Completed);
  }

  std::shared_ptr<const BootROMImage> rom(std::uint8_t marker)
  {
    std::vector<std::uint8_t> bytes(BootROMImage::SIZE, 0);
    bytes[0] = marker;
    return BootROMImage::create(bytes);
  }

  void advanceTimers(NekoSystem *system, unsigned cycles)
  {
    for (unsigned cycle = 0; cycle < cycles; ++cycle)
    {
      const auto sample = system->iopTimerTiming().advance(
        cycle % 8 == 0, {cycle % 7 < 3, cycle % 11 < 5});
      const auto requests = system->iopTimers().advance(sample);
      for (std::size_t index = 0; index < requests.count; ++index)
      {
        system->iopInterruptController().assertSource(
          requests.sources[index]);
      }
    }
  }
}

TEST_CASE("IOP persistence owns dormant attached machine components")
{
  NekoSystem system;
  const auto initial = system.iopStateHash();
  REQUIRE(system.iopCore().hasAttachedBus());
  REQUIRE(system.iopCore().executionState() == IOPExecutionState::Halted);
  system.runMasterCycles(80);
  REQUIRE(system.iopCore().elapsedCycles() == 0);
  REQUIRE(system.iopTimers().counter(0).count == 0);
  REQUIRE(system.iopTimerTiming().pixelPhase() == 0);
  REQUIRE(system.iopStateHash() == initial);
  system.iopInterruptController().writeMask(0x10);
  system.iopInterruptController().writeControl(1);
  system.iopInterruptController().assertSource(IOPInterruptSource::Timer0);
  REQUIRE(system.iopBus().interruptControllerPending());
  REQUIRE(system.iopBus().read32(IOPBus::INTC_STAT).value == 0x10);
  system.iopBus().write32(0x1f801108, 37);
  REQUIRE(system.iopTimers().counter(0).target == 37);
}

TEST_CASE("IOP persistence continues branch and delayed load without copied pointers")
{
  NekoSystem original;
  loadInstruction(&original, 0, 0x8c010100); // lw r1, 0x100(r0)
  loadInstruction(&original, 4, 0x10000002); // beq r0, r0, 0x10
  loadInstruction(&original, 8, 0x8c020104); // lw r2, 0x104(r0)
  loadInstruction(&original, 0x10, 0x24430001); // addiu r3, r2, 1
  loadInstruction(&original, 0x14, 0);
  loadInstruction(&original, 0x100, 0x12345678);
  loadInstruction(&original, 0x104, 0x87654321);
  original.iopCore().startExecution(0x80000000);
  original.iopCore().stepInstruction();
  const auto loadState = original.saveState();
  NekoSystem restored;
  restored.loadState(loadState);
  REQUIRE(restored.iopCore().delayedResultContinuation().source ==
    IOPDelayedResultSource::Load);
  REQUIRE(restored.iopStateHash() == original.iopStateHash());
  for (unsigned step = 0; step < 4; ++step)
  {
    original.iopCore().stepInstruction();
    restored.iopCore().stepInstruction();
    REQUIRE(restored.iopStateHash() == original.iopStateHash());
    REQUIRE(restored.saveState() == original.saveState());
    if (step == 0)
    {
      REQUIRE(restored.iopCore().branchContinuation().active);
      const auto branchState = restored.saveState();
      restored.loadState(branchState);
      REQUIRE(restored.saveState() == branchState);
    }
  }
  REQUIRE(restored.iopCore().generalRegister(1) == 0x12345678);
  REQUIRE(restored.iopCore().generalRegister(2) == 0x87654321);
  REQUIRE(restored.iopCore().generalRegister(3) == 1);
  original.iopBus().write32(0x100, 0xaaaaaaaa);
  REQUIRE(restored.iopBus().read32(0x100).value == 0x12345678);
  restored.iopInterruptController().writeMask(1);
  restored.iopInterruptController().writeControl(1);
  restored.iopInterruptController().assertSource(IOPInterruptSource::VBlankStart);
  REQUIRE(restored.iopBus().interruptControllerPending());
  REQUIRE_FALSE(original.iopBus().interruptControllerPending());
}

TEST_CASE("IOP persistence preserves timer requests phases and storage")
{
  NekoSystem original;
  original.iopTimers().writeTarget(0, 1);
  original.iopTimers().writeMode(0, IOPTimerMode::INTERRUPT_ON_TARGET);
  original.iopTimers().writeMode(2, IOPTimerMode::DIVIDE_BY_8);
  original.iopTimers().writeMode(4, IOPTimerMode::PRESCALER_MASK);
  original.iopTimers().writeMode(1, IOPTimerMode::GATE_ENABLE | 6);
  original.iopBus().write32(IOPBus::SCRATCHPAD_BASE + 1020, 0xdeadbeef);
  original.iopBus().write32(IOPBus::RAM_SIZE - 4, 0xcafebabe);
  advanceTimers(&original, 1);
  REQUIRE(original.iopTimers().counter(0).pulseReleasePending);
  const auto state = original.saveState();
  REQUIRE_NOTHROW(inspectSaveState(state));
  NekoSystem restored;
  restored.loadState(state);
  REQUIRE(restored.saveState() == state);
  REQUIRE(restored.iopStateHash() == original.iopStateHash());
  for (unsigned step = 0; step < 20; ++step)
  {
    advanceTimers(&original, 17);
    advanceTimers(&restored, 17);
    REQUIRE(restored.iopStateHash() == original.iopStateHash());
  }
  REQUIRE(restored.saveState() == original.saveState());
  REQUIRE(restored.iopBus().read32(IOPBus::SCRATCHPAD_BASE + 1020).value ==
    0xdeadbeef);
  restored.iopTimers().writeTarget(0, 99);
  restored.loadState(state);
  restored.iopBus().write32(0x1f801108, 23);
  REQUIRE(restored.iopTimers().counter(0).target == 23);
  REQUIRE(original.iopTimers().counter(0).target == 1);
}

TEST_CASE("IOP persistence ROM compatibility uses contents not host identity")
{
  NekoSystem original;
  REQUIRE(original.iopBus().installRom(rom(0x12)));
  REQUIRE(original.iopCore().readData32(0xbfc00000).outcome ==
    IOPAccessOutcome::Completed);
  const auto state = original.saveState();
  REQUIRE_NOTHROW(inspectSaveState(state));
  NekoSystem restored;
  REQUIRE(restored.iopBus().installRom(rom(0x12)));
  restored.loadState(state);
  REQUIRE(restored.saveState() == state);
  REQUIRE(restored.iopStateHash() == original.iopStateHash());
  REQUIRE(restored.iopCore().readData32(0xbfc00000).value == 0x12);
  REQUIRE(restored.saveState() == original.saveState());
  SECTION("different image")
  {
    REQUIRE(restored.iopBus().installRom(rom(0x13)));
  }
  SECTION("missing image")
  {
    NekoSystem absent;
    const auto before = absent.saveState();
    REQUIRE_THROWS_AS(absent.loadState(state), std::invalid_argument);
    REQUIRE(absent.saveState() == before);
    return;
  }
  const auto before = restored.saveState();
  REQUIRE_THROWS_AS(restored.loadState(state), std::invalid_argument);
  REQUIRE(restored.saveState() == before);
}

TEST_CASE("IOP persistence rejects malformed candidate state atomically")
{
  const std::vector<std::pair<std::string, std::uint64_t>> mutations = {
    {"system.iop.core.state", 2},
    {"system.iop.core.status", 0xffffffff},
    {"system.iop.core.cause", 1},
    {"system.iop.core.directInterruptInputs", 1},
    {"system.iop.core.hiWriteHazardInstructions", 3},
    {"system.iop.core.delayedResult.destination", 32},
    {"system.iop.core.delayedResult.source", 3},
    {"system.iop.core.exception.kind", 11},
    {"system.iop.core.storeFailure.outcome", 5},
    {"system.iop.core.branch.active", 2},
    {"system.iop.core.cache.profile", 0},
    {"system.iop.core.cache.instructionLineWords", 1},
    {"system.iop.core.cache.instructionLines[0].physicalTag", 1},
    {"system.iop.core.cache.dataLines[0].valid", 2},
    {"system.iop.interruptController.status", 0x04000000},
    {"system.iop.interruptController.mask", 0x04000000},
    {"system.iop.interruptController.control", 2},
    {"system.iop.timers.counters[0].count", 0x10000},
    {"system.iop.timers.counters[1].mode", 0x8000},
    {"system.iop.timers.counters[2].prescalerRemainder", 1},
    {"system.iop.timers.counters[3].gateWaiting", 1},
    {"system.iop.timers.counters[4].pulseReleasePending", 1},
    {"system.iop.timing.pixelPhase", 8192},
    {"system.iop.bus.ram.size", 1}
  };
  NekoSystem system;
  system.iopBus().write32(0x100, 0x1234);
  const auto before = system.saveState();
  const auto hash = system.iopStateHash();
  for (const auto &mutation : mutations)
  {
    CAPTURE(mutation.first);
    SaveStateMutation invalid(before);
    invalid.writeScalar(mutation.first, mutation.second);
    invalid.refreshChecksum();
    REQUIRE_THROWS_AS(system.loadState(invalid), std::invalid_argument);
    REQUIRE(system.saveState() == before);
    REQUIRE(system.iopStateHash() == hash);
  }
}

TEST_CASE("IOP persistence reset preserves external ROM and clears mutable owners")
{
  NekoSystem system;
  const auto image = rom(0x42);
  REQUIRE(system.iopBus().installRom(image));
  system.iopCore().startExecution(0x80000000);
  system.iopCore().stepInstruction();
  advanceTimers(&system, 19);
  system.iopBus().write32(0x100, 1);
  system.reset();
  REQUIRE(system.iopBus().romIdentity() == image);
  REQUIRE(system.iopCore().executionState() == IOPExecutionState::Halted);
  REQUIRE(system.iopCore().elapsedCycles() == 0);
  REQUIRE(system.iopBus().read32(0x100).value == 0);
  REQUIRE(system.iopTimers().counter(0).count == 0);
  REQUIRE(system.iopTimerTiming().pixelPhase() == 0);
  REQUIRE(system.iopCore().readData32(0xbfc00000).value == 0x42);
}

TEST_CASE("IOP persistence retains invalid cache payloads tags validity and CM")
{
  NekoSystem original;
  SaveStateMutation seeded(original.saveState());
  seeded.writeScalar("system.iop.core.status",
    original.iopCore().status() | IOPCOP0Status::CACHE_ISOLATED |
      IOPCOP0Status::CACHE_PROBE_RESULT);
  for (const std::string bank : {"instructionLines", "dataLines"})
  {
    const bool instruction = bank == "instructionLines";
    const auto lines = instruction
      ? IOPCache::INSTRUCTION_LINE_COUNT : IOPCache::DATA_LINE_COUNT;
    const auto words = instruction
      ? IOPCache::INSTRUCTION_LINE_WORDS : IOPCache::DATA_LINE_WORDS;
    for (std::size_t line = 0; line < lines; ++line)
    {
      const auto path = "system.iop.core.cache." + bank +
        "[" + std::to_string(line) + "]";
      seeded.writeScalar(path + ".physicalTag", 0x40000000);
      seeded.writeScalar(path + ".valid", line % 2);
      for (std::size_t word = 0; word < words; ++word)
      {
        seeded.writeScalar(path + ".words[" + std::to_string(word) + "].value",
          1 + line * words + word);
      }
    }
  }
  seeded.refreshChecksum();
  original.loadState(seeded);
  REQUIRE(original.saveState() == seeded.bytes());
  const auto hash = original.iopStateHash();
  REQUIRE(original.iopStateHash() == hash);
  REQUIRE(original.saveState() == seeded.bytes());
  NekoSystem restored;
  restored.loadState(original.saveState());
  REQUIRE(restored.iopStateHash() == hash);
  for (std::size_t line = 0; line < IOPCache::INSTRUCTION_LINE_COUNT; ++line)
  {
    REQUIRE(restored.iopCore().cache().instructionLine(line).words ==
      original.iopCore().cache().instructionLine(line).words);
    REQUIRE(restored.iopCore().cache().instructionLine(line).physicalTag ==
      0x40000000);
    REQUIRE(restored.iopCore().cache().instructionLine(line).valid ==
      (line % 2 != 0));
    REQUIRE(restored.iopCore().cache().dataLine(line).words ==
      original.iopCore().cache().dataLine(line).words);
    REQUIRE(restored.iopCore().cache().dataLine(line).valid == (line % 2 != 0));
  }
  REQUIRE(restored.iopCore().readData32(0xa0000000).value == 1);
  REQUIRE((restored.iopCore().status() & IOPCOP0Status::CACHE_PROBE_RESULT) != 0);
  REQUIRE(restored.iopCore().cache().dataLine(0).words[0] == 1);
}

TEST_CASE("IOP persistence hashes every register cache field and device continuation")
{
  NekoSystem system;
  const auto before = system.saveState();
  const auto initialHash = system.iopStateHash();
  std::vector<std::pair<std::string, std::uint64_t>> mutations = {
    {"system.iop.core.hi", 1},
    {"system.iop.core.lo", 1},
    {"system.iop.core.hiWriteHazardInstructions", 1},
    {"system.iop.core.loWriteHazardInstructions", 1},
    {"system.iop.core.unreadMultiplyDivideHI", 1},
    {"system.iop.core.unreadMultiplyDivideLO", 1},
    {"system.iop.core.status", system.iopCore().status() |
      IOPCOP0Status::CACHE_PROBE_RESULT},
    {"system.iop.core.cause", IOPCOP0Cause::SOFTWARE_INTERRUPT_PENDING},
    {"system.iop.core.directInterruptInputs", 1 << 10},
    {"system.iop.core.cache.instructionLines[0].physicalTag", 0x1000},
    {"system.iop.core.cache.instructionLines[0].valid", 1},
    {"system.iop.core.cache.instructionLines[255].words[3].value", 1},
    {"system.iop.core.cache.dataLines[0].physicalTag", 0x400},
    {"system.iop.core.cache.dataLines[0].valid", 1},
    {"system.iop.core.cache.dataLines[255].words[0].value", 1},
    {"system.iop.interruptController.status", 1},
    {"system.iop.interruptController.mask", 1},
    {"system.iop.interruptController.control", 1},
    {"system.iop.timers.horizontalBlank", 1},
    {"system.iop.timers.verticalBlank", 1},
    {"system.iop.timers.counters[0].count", 1},
    {"system.iop.timers.counters[5].target", 1},
    {"system.iop.timing.pixelPhase", 8191}
  };
  for (unsigned reg = 1; reg < 32; ++reg)
  {
    mutations.push_back({
      "system.iop.core.generalRegisters[" + std::to_string(reg) + "].value", reg});
  }
  for (const auto &mutation : mutations)
  {
    CAPTURE(mutation.first);
    SaveStateMutation changed(before);
    changed.writeScalar(mutation.first, mutation.second);
    changed.refreshChecksum();
    system.loadState(changed);
    REQUIRE(system.iopStateHash() != initialHash);
    REQUIRE(system.saveState() == changed.bytes());
    system.loadState(before);
    REQUIRE(system.iopStateHash() == initialHash);
  }
  system.iopBus().write32(IOPBus::RAM_SIZE - 4, 1);
  REQUIRE(system.iopStateHash() != initialHash);
  system.loadState(before);
  system.iopBus().write32(IOPBus::SCRATCHPAD_BASE, 1);
  REQUIRE(system.iopStateHash() != initialHash);
}

TEST_CASE("IOP persistence retains deferred ROM invalidation across restore")
{
  NekoSystem original;
  REQUIRE(original.iopBus().installRom(rom(0x12)));
  REQUIRE(original.iopCore().readData32(0x9fc00000).value == 0x12);
  REQUIRE(original.iopCore().cache().dataLine(0).valid);
  const auto synchronized = original.iopStateHash();
  // A new identity still invalidates caches when the bytes happen to match.
  REQUIRE(original.iopBus().installRom(rom(0x12)));
  REQUIRE(original.iopStateHash() != synchronized);
  const auto state = original.saveState();
  NekoSystem restored;
  REQUIRE(restored.iopBus().installRom(rom(0x12)));
  restored.loadState(state);
  REQUIRE(restored.saveState() == state);
  REQUIRE(restored.iopCore().cache().dataLine(0).valid);
  REQUIRE(original.iopCore().readData32(0xa0000000).value == 0);
  REQUIRE(restored.iopCore().readData32(0xa0000000).value == 0);
  REQUIRE_FALSE(restored.iopCore().cache().dataLine(0).valid);
  REQUIRE(restored.iopStateHash() == original.iopStateHash());
}

TEST_CASE("IOP persistence preserves multiply hazards COP0 delay and exception entry")
{
  NekoSystem original;
  const std::vector<IOPWord> program = {
    0x24010007, 0x24020003, 0x00220018, 0x00001810,
    0, 0, 0x00002012, 0x40056000, 0, 0x0000000c
  };
  for (std::size_t index = 0; index < program.size(); ++index)
  {
    loadInstruction(&original, static_cast<IOPAddress>(index * 4), program[index]);
  }
  original.iopCore().startExecution(0x80000000);
  for (std::size_t step = 0; step < program.size(); ++step)
  {
    NekoSystem restored;
    const auto state = original.saveState();
    restored.loadState(state);
    REQUIRE(restored.saveState() == state);
    original.iopCore().stepInstruction();
    restored.iopCore().stepInstruction();
    REQUIRE(restored.iopStateHash() == original.iopStateHash());
    REQUIRE(restored.saveState() == original.saveState());
  }
  REQUIRE(original.iopCore().exception().kind == IOPException::SystemCall);
  NekoSystem restored;
  restored.loadState(original.saveState());
  REQUIRE(restored.iopCore().exception().kind == IOPException::SystemCall);
  REQUIRE(restored.iopStateHash() == original.iopStateHash());
}

TEST_CASE("IOP persistence retains rejected store and its unretired delayed result")
{
  NekoSystem original;
  loadInstruction(&original, 0, 0x3c011000); // lui r1, 0x1000
  loadInstruction(&original, 4, 0x10000003); // beq r0, r0, 0x14
  loadInstruction(&original, 8, 0x8c020100); // lw r2, 0x100(r0)
  loadInstruction(&original, 0x14, 0xac200000); // sw r0, 0(r1)
  loadInstruction(&original, 0x100, 0x12345678);
  original.iopCore().startExecution(0x80000000);
  for (unsigned step = 0; step < 4; ++step)
  {
    original.iopCore().stepInstruction();
  }
  REQUIRE(original.iopCore().stopReason() == IOPStopReason::StoreFailure);
  REQUIRE(original.iopCore().delayedResultContinuation().source ==
    IOPDelayedResultSource::Load);
  const auto state = original.saveState();
  NekoSystem restored;
  restored.loadState(state);
  REQUIRE(restored.iopStateHash() == original.iopStateHash());
  REQUIRE(restored.saveState() == state);
  REQUIRE(restored.iopCore().storeFailure().width == IOPStoreWidth::Word);
  REQUIRE(restored.iopCore().storeFailure().access.physicalAddress == 0x10000000);
  REQUIRE_THROWS_AS(restored.iopCore().resumeExecution(), std::logic_error);
}
