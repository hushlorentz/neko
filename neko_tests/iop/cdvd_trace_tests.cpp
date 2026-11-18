#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

#include "catch.hpp"
#include "iop_trace_test_utils.hpp"
#include "neko_system.hpp"
#include "save_state_mutation.hpp"

struct CDVDObservationTestAccess
{
  static void enable(CDVDController &controller, NekoObservationOrder *order)
  {
    controller.enableObservations(order);
  }

  static void disable(CDVDController &controller)
  {
    controller.disableObservations();
  }

  static bool attached(const CDVDController &controller, NekoObservationOrder *order)
  {
    return controller.observationsEnabled && controller.observationOrder == order;
  }

  static bool detached(const CDVDController &controller)
  {
    return !controller.observationsEnabled && controller.observationOrder == nullptr &&
      controller.observationCount == 0;
  }

  static std::size_t count(const CDVDController &controller)
  {
    return controller.observationCount;
  }

  static std::size_t sequence(const CDVDController &controller, std::size_t index)
  {
    return controller.observations.at(index).sequence;
  }
};

namespace
{
  constexpr auto CDVD = static_cast<NekoTraceSubsystem>(15);
  constexpr auto ACCEPTED = static_cast<NekoTraceEventType>(23);
  constexpr auto COMPLETED = static_cast<NekoTraceEventType>(24);
  constexpr std::uint32_t BASE = 0x1f402000;
  constexpr std::uint64_t EPOCH = UINT64_C(0x0001010000000000);

  void write(NekoSystem &system, unsigned offset, std::uint8_t value)
  {
    REQUIRE(system.iopBus().write8(BASE + offset, value).status ==
      IOPBusStatus::Completed);
  }

  void prepareStore(NekoSystem &system, IOPWord instruction = 0xa0200004)
  {
    const std::array<IOPWord, 3> program = {{
      0x3c01bf40, 0x34212000, instruction
    }};
    for (std::size_t index = 0; index < program.size(); ++index)
    {
      REQUIRE(system.iopBus().write32(static_cast<IOPAddress>(index * 4),
        program[index]).status == IOPBusStatus::Completed);
    }
    system.iopCore().startExecution(0xa0000000);
    system.runMasterCycles(16);
    write(system, 4, 0);
    write(system, 0x16, 8);
  }

  class Observer final : public NekoTraceObserver
  {
    public:
      void observe(const NekoTraceEvent &event) noexcept override
      {
        if (count < events.size())
        {
          events[count++] = event;
        }
        else
        {
          overflow = true;
        }
      }

      std::array<NekoTraceEvent, 2048> events = {};
      std::size_t count = 0;
      bool overflow = false;
  };

  template<typename Program>
  void installProgram(NekoSystem &system, IOPAddress address,
    const Program &words)
  {
    for (const auto word : words)
    {
      REQUIRE(system.iopBus().write32(address, word).status ==
        IOPBusStatus::Completed);
      address += 4;
    }
  }

  std::array<IOPWord, 27> bootCertifyGuest(IOPWord upper)
  {
    return {{
      0x3c010000 | upper, 0x34212000,
      0x34020002, 0xa0220017, 0xa0200017,
      0x34020041, 0xa0220017, 0x34020043, 0xa0220017,
      0x3402001a, 0x10000002, 0xa0220016, 0xffffffff,
      0x90230017, 0, 0x30640080, 0x1480fffc, 0,
      0x90280018, 0, 0x90290017, 0, 0x902a0018, 0,
      0x902b0008, 0, 0x34140001
    }};
  }

  enum class TraceMode { Disabled, Retained, Prefix, Stream };

  void requireContinuationModes(const NekoSystem &reference, NekoSystem &source)
  {
    const auto cut = source.masterClockScheduler().currentCycle();
    const auto remaining = reference.masterClockScheduler().currentCycle() - cut;
    const auto checkpoint = source.saveState();
    std::vector<NekoTraceEvent> suffix;
    for (const auto &event : reference.trace())
    {
      if (event.masterCycle > cut)
      {
        suffix.push_back(event);
      }
    }
    REQUIRE(suffix.size() > 3);
    for (const auto mode : {TraceMode::Disabled, TraceMode::Retained,
         TraceMode::Prefix, TraceMode::Stream})
    {
      CAPTURE(mode);
      NekoSystem restored;
      restored.loadState(checkpoint);
      const auto observer = std::make_shared<Observer>();
      switch (mode)
      {
        case TraceMode::Disabled:
          break;
        case TraceMode::Retained:
          restored.startTrace();
          break;
        case TraceMode::Prefix:
          restored.startTrace(3, observer);
          break;
        case TraceMode::Stream:
          restored.startTrace(0, observer);
          break;
      }
      restored.runMasterCycles(remaining);
      IOPTraceTest::requireMachine(restored, reference);
      REQUIRE(CDVDObservationTestAccess::detached(restored.cdvdController()));
      if (mode == TraceMode::Retained)
      {
        IOPTraceTest::requireEvents(restored.trace(), suffix);
        REQUIRE_FALSE(restored.traceTruncated());
      }
      else if (mode == TraceMode::Prefix || mode == TraceMode::Stream)
      {
        REQUIRE_FALSE(observer->overflow);
        const std::vector<NekoTraceEvent> observed(
          observer->events.begin(), observer->events.begin() + observer->count);
        IOPTraceTest::requireEvents(observed, suffix);
        const auto retained = mode == TraceMode::Prefix ? 3u : 0u;
        IOPTraceTest::requireEvents(restored.trace(),
          {suffix.begin(), suffix.begin() + retained});
        REQUIRE(restored.traceTruncated());
      }
      else
      {
        REQUIRE(restored.trace().empty());
        REQUIRE_FALSE(restored.traceEnabled());
      }
    }
    source.runMasterCycles(remaining);
    IOPTraceTest::requireMachine(source, reference);
  }
}

TEST_CASE("CDVD BootCertify authored guest stages submits polls and drains through each segment",
  "[iop][cdvd-trace][cdvd-boot-certify][cdvd-boot-certify-conformance][save-state]")
{
  for (const IOPWord upper : {0x1f40u, 0x9f40u, 0xbf40u})
  {
    CAPTURE(upper);
    const auto program = bootCertifyGuest(upper);
    const auto prepare = [&program](NekoSystem &system) {
      installProgram(system, 0, program);
      system.iopCore().startExecution(0xa0000000);
    };
    NekoSystem reference;
    prepare(reference);
    const auto initial = reference.saveState();
    reference.startTrace();
    reference.runMasterCycles(201);
    REQUIRE(reference.iopCore().stopReason() == IOPStopReason::None);
    REQUIRE(reference.iopCore().retiredInstructions() == program.size() - 1);
    REQUIRE(reference.iopCore().generalRegister(3) == 0);
    REQUIRE(reference.iopCore().generalRegister(8) == 1);
    REQUIRE(reference.iopCore().generalRegister(9) == 0x40);
    REQUIRE(reference.iopCore().generalRegister(10) == 0);
    REQUIRE(reference.iopCore().generalRegister(11) == 0);
    REQUIRE(reference.iopCore().generalRegister(20) == 1);
    REQUIRE(reference.iopInterruptController().status() == 0);
    const auto commands = IOPTraceTest::subsystemEvents(reference, CDVD);
    REQUIRE(commands.size() == 2);
    for (unsigned index = 0; index < commands.size(); ++index)
    {
      REQUIRE(commands[index].type == (index == 0 ? ACCEPTED : COMPLETED));
      REQUIRE(commands[index].masterCycle == (index == 0 ? 89 : 97));
      const auto decoded = nekoDecodeCDVDTrace(commands[index]);
      REQUIRE(decoded.transport == NekoCDVDTrace::Transport::Subcommand);
      REQUIRE(decoded.opcode == 0x1a);
      REQUIRE(decoded.beforePhase == (index == 0 ?
        NekoCDVDTrace::Phase::Idle : NekoCDVDTrace::Phase::Pending));
      REQUIRE(decoded.afterPhase == (index == 0 ?
        NekoCDVDTrace::Phase::Pending : NekoCDVDTrace::Phase::Result));
      REQUIRE(decoded.beforeCause == 0);
      REQUIRE(decoded.afterCause == 0);
      REQUIRE(decoded.rtcTicks == commands[index].masterCycle);
      const std::array<std::uint8_t, 16> expected = index == 0 ?
        std::array<std::uint8_t, 16>{} : std::array<std::uint8_t, 16>{{1}};
      REQUIRE(decoded.result == expected);
    }
    const IOPAddress base = (upper << 16) | 0x2000;
    const std::array<std::uint64_t, 10> cycles = {{
      25, 33, 49, 65, 89, 97, 137, 153, 169, 185
    }};
    const std::array<std::uint8_t, 10> values = {{
      2, 0, 0x41, 0x43, 0x1a, 0, 1, 0x40, 0, 0
    }};
    const std::array<unsigned, 10> offsets = {{
      0x17, 0x17, 0x17, 0x17, 0x16, 0x17, 0x18, 0x17, 0x18, 8
    }};
    std::size_t access = 0;
    bool completed = false;
    for (const auto &event : reference.trace())
    {
      if (event.subsystem == CDVD && event.type == COMPLETED)
      {
        completed = true;
      }
      if (event.subsystem != NekoTraceSubsystem::IOP ||
          event.type != NekoTraceEventType::MemoryAccess ||
          (event.value0 != base + 8 &&
           (event.value0 < base + 0x16 || event.value0 > base + 0x18)))
      {
        continue;
      }
      REQUIRE(access < cycles.size());
      REQUIRE(event.masterCycle == cycles[access]);
      REQUIRE(event.value0 == base + offsets[access]);
      REQUIRE(event.value1 == values[access]);
      if (upper == 0xbf40)
      {
        namespace Memory = NekoIOPTraceMemory;
        REQUIRE(event.value3 == nekoPackIOPMemoryTraceMetadata({
          BASE + offsets[access], 1,
          access < 5 ? Memory::AccessKind::DataStore : Memory::AccessKind::DataLoad,
          Memory::Outcome::Completed, Memory::CacheRoute::Uncached, true,
          static_cast<std::uint8_t>(1u << (offsets[access] % 4))}));
      }
      if (access >= 5)
      {
        REQUIRE(completed);
      }
      ++access;
    }
    REQUIRE(access == cycles.size());
    NekoSystem coldPlain;
    prepare(coldPlain);
    coldPlain.runMasterCycles(201);
    IOPTraceTest::requireMachine(coldPlain, reference);
    if (upper != 0xbf40)
    {
      continue;
    }
    struct Cut
    {
      std::uint64_t cycle;
      std::uint8_t parameterCount;
      std::uint8_t phase;
    };
    const Cut cuts[] = {
      {25, 1, 0}, {49, 3, 0}, {81, 4, 0}, {89, 0, 1},
      {96, 0, 1}, {97, 0, 2}, {137, 0, 0}
    };
    for (const auto &cut : cuts)
    {
      CAPTURE(cut.cycle);
      NekoSystem source;
      source.loadState(initial);
      source.runMasterCycles(cut.cycle);
      const SaveStateMutation fields(source.saveState());
      REQUIRE(fields.readByte("system.iop.cdvd.subcommandParameterCount") ==
        cut.parameterCount);
      REQUIRE(fields.readByte("system.iop.cdvd.subcommandPhase") == cut.phase);
      if (cut.cycle == 97 || cut.cycle == 137)
      {
        REQUIRE(source.iopCore().delayedResultContinuation().source ==
          IOPDelayedResultSource::Load);
      }
      requireContinuationModes(reference, source);
    }
  }
}

TEST_CASE("CDVD BootCertify authored malformed requests preserve staging and rejected branches",
  "[iop][cdvd-trace][cdvd-boot-certify][cdvd-boot-certify-conformance][save-state]")
{
  const std::vector<std::vector<std::uint8_t>> requests = {
    {2, 0, 0x41}, {2, 0, 0x41, 0x44}, {2, 0, 0x41, 0x43, 0}
  };
  for (const auto &parameters : requests)
  {
    CAPTURE(parameters.size());
    std::vector<IOPWord> program = {0x3c01bf40, 0x34212000};
    for (const auto byte : parameters)
    {
      program.push_back(0x34020000 | byte);
      program.push_back(0xa0220017);
    }
    program.insert(program.end(), {
      0x3402001a, 0x10000001, 0xa0220016, 0x34140001
    });
    const auto prepare = [&program](NekoSystem &system) {
      installProgram(system, 0, program);
      system.iopCore().startExecution(0xa0000000);
    };
    const std::uint64_t failureCycle = 33 + 16 * parameters.size();
    NekoSystem reference;
    prepare(reference);
    const auto initial = reference.saveState();
    reference.startTrace();
    reference.runMasterCycles(failureCycle);
    REQUIRE(reference.iopCore().stopReason() == IOPStopReason::StoreFailure);
    REQUIRE(reference.iopCore().retiredInstructions() == program.size() - 2);
    REQUIRE(reference.iopCore().programCounter() ==
      0xa0000000 + (program.size() - 2) * 4);
    REQUIRE(reference.iopCore().storeFailure().access.physicalAddress == BASE + 0x16);
    REQUIRE(reference.iopCore().generalRegister(20) == 0);
    REQUIRE(reference.iopInterruptController().status() == 0);
    REQUIRE(IOPTraceTest::subsystemEvents(reference, CDVD).empty());
    const SaveStateMutation fields(reference.saveState());
    REQUIRE(fields.readByte("system.iop.cdvd.lastSubcommand") == 0);
    REQUIRE(fields.readByte("system.iop.cdvd.subcommandPhase") == 0);
    REQUIRE(fields.readByte("system.iop.cdvd.subcommandParameterCount") == parameters.size());
    for (std::size_t index = 0; index < 16; ++index)
    {
      REQUIRE(fields.readByte("system.iop.cdvd.subcommandParameters", index) ==
        (index < parameters.size() ? parameters[index] : 0));
      REQUIRE(fields.readByte("system.iop.cdvd.result", index) == 0);
    }
    NekoSystem coldPlain;
    prepare(coldPlain);
    coldPlain.runMasterCycles(failureCycle);
    IOPTraceTest::requireMachine(coldPlain, reference);
    for (const auto cut : {std::uint64_t{25}, failureCycle - 8})
    {
      CAPTURE(cut);
      NekoSystem source;
      source.loadState(initial);
      source.runMasterCycles(cut);
      requireContinuationModes(reference, source);
    }
  }
}

TEST_CASE("CDVD BootCertify unread result rejects guest staging across reset and restore",
  "[iop][cdvd-trace][cdvd-boot-certify][cdvd-boot-certify-conformance][save-state]")
{
  auto program = bootCertifyGuest(0xbf40);
  program[10] = 0xa0220016;
  program[11] = 0x34020002;
  program[12] = 0x10000001;
  program[13] = 0xa0220017;
  program[14] = 0x34140001;
  const auto prepare = [&program](NekoSystem &system) {
    installProgram(system, 0, program);
    system.iopCore().startExecution(0xa0000000);
  };
  NekoSystem reference;
  prepare(reference);
  const auto initial = reference.saveState();
  reference.startTrace();
  reference.runMasterCycles(105);
  REQUIRE(reference.iopCore().stopReason() == IOPStopReason::StoreFailure);
  REQUIRE(reference.iopCore().retiredInstructions() == 13);
  REQUIRE(reference.iopCore().programCounter() == 0xa0000034);
  REQUIRE(reference.iopCore().storeFailure().access.physicalAddress == BASE + 0x17);
  REQUIRE(reference.iopCore().generalRegister(20) == 0);
  REQUIRE(reference.iopInterruptController().status() == 0);
  const auto commands = IOPTraceTest::subsystemEvents(reference, CDVD);
  REQUIRE(commands.size() == 2);
  REQUIRE(commands[0].masterCycle == 81);
  REQUIRE(commands[1].masterCycle == 89);
  const SaveStateMutation fields(reference.saveState());
  REQUIRE(fields.readByte("system.iop.cdvd.subcommandPhase") == 2);
  REQUIRE(fields.readByte("system.iop.cdvd.result") == 1);
  REQUIRE(fields.readByte("system.iop.cdvd.resultCursor") == 0);
  REQUIRE(fields.readByte("system.iop.cdvd.subcommandParameterCount") == 0);
  NekoSystem coldPlain;
  prepare(coldPlain);
  coldPlain.runMasterCycles(105);
  IOPTraceTest::requireMachine(coldPlain, reference);
  NekoSystem fresh;
  for (const std::uint64_t cut : {25, 81, 89, 97})
  {
    CAPTURE(cut);
    NekoSystem source;
    source.loadState(initial);
    source.runMasterCycles(cut);
    const auto checkpoint = source.saveState();
    source.reset();
    IOPTraceTest::requireMachine(source, fresh);
    source.loadState(checkpoint);
    requireContinuationModes(reference, source);
  }
}

TEST_CASE("CDVD ForbidDVDP authored guest submits polls and drains through each segment",
  "[iop][cdvd-trace][cdvd-forbid-dvdp][cdvd-forbid-dvdp-conformance][save-state]")
{
  for (const IOPWord upper : {0x1f40u, 0x9f40u, 0xbf40u})
  {
    CAPTURE(upper);
    const std::array<IOPWord, 20> program = {{
      0x3c010000 | upper, 0x34212000, 0x34020015,
      0x10000002, 0xa0220016, 0xffffffff,
      0x90230017, 0, 0x30640080, 0x1480fffc, 0,
      0x90280018, 0, 0x90290017, 0, 0x902a0018, 0,
      0x902b0008, 0, 0x34140001
    }};
    const auto prepare = [&program](NekoSystem &system) {
      installProgram(system, 0, program);
      system.iopCore().startExecution(0xa0000000);
    };
    NekoSystem reference;
    prepare(reference);
    const auto initial = reference.saveState();
    reference.startTrace();
    reference.runMasterCycles(145);
    REQUIRE(reference.iopCore().stopReason() == IOPStopReason::None);
    REQUIRE(reference.iopCore().retiredInstructions() == program.size() - 1);
    REQUIRE(reference.iopCore().generalRegister(3) == 0);
    REQUIRE(reference.iopCore().generalRegister(8) == 5);
    REQUIRE(reference.iopCore().generalRegister(9) == 0x40);
    REQUIRE(reference.iopCore().generalRegister(10) == 0);
    REQUIRE(reference.iopCore().generalRegister(11) == 0);
    REQUIRE(reference.iopCore().generalRegister(20) == 1);
    REQUIRE(reference.iopInterruptController().status() == 0);
    const auto commands = IOPTraceTest::subsystemEvents(reference, CDVD);
    REQUIRE(commands.size() == 2);
    for (unsigned index = 0; index < commands.size(); ++index)
    {
      REQUIRE(commands[index].type == (index == 0 ? ACCEPTED : COMPLETED));
      REQUIRE(commands[index].masterCycle == (index == 0 ? 33 : 41));
      const auto decoded = nekoDecodeCDVDTrace(commands[index]);
      REQUIRE(decoded.transport == NekoCDVDTrace::Transport::Subcommand);
      REQUIRE(decoded.opcode == 0x15);
      REQUIRE(decoded.beforePhase == (index == 0 ?
        NekoCDVDTrace::Phase::Idle : NekoCDVDTrace::Phase::Pending));
      REQUIRE(decoded.afterPhase == (index == 0 ?
        NekoCDVDTrace::Phase::Pending : NekoCDVDTrace::Phase::Result));
      REQUIRE(decoded.beforeCause == 0);
      REQUIRE(decoded.afterCause == 0);
      REQUIRE(decoded.rtcTicks == commands[index].masterCycle);
      const std::array<std::uint8_t, 16> expected = index == 0 ?
        std::array<std::uint8_t, 16>{} : std::array<std::uint8_t, 16>{{5}};
      REQUIRE(decoded.result == expected);
    }
    const IOPAddress base = (upper << 16) | 0x2000;
    const std::array<std::uint64_t, 6> cycles = {{33, 41, 81, 97, 113, 129}};
    const std::array<std::uint8_t, 6> values = {{0x15, 0, 5, 0x40, 0, 0}};
    const std::array<unsigned, 6> offsets = {{0x16, 0x17, 0x18, 0x17, 0x18, 8}};
    std::size_t access = 0;
    bool completed = false;
    for (const auto &event : reference.trace())
    {
      if (event.subsystem == CDVD && event.type == COMPLETED)
      {
        completed = true;
      }
      if (event.subsystem != NekoTraceSubsystem::IOP ||
          event.type != NekoTraceEventType::MemoryAccess ||
          (event.value0 != base + 8 &&
           (event.value0 < base + 0x16 || event.value0 > base + 0x18)))
      {
        continue;
      }
      REQUIRE(access < cycles.size());
      REQUIRE(event.masterCycle == cycles[access]);
      REQUIRE(event.value0 == base + offsets[access]);
      REQUIRE(event.value1 == values[access]);
      if (upper == 0xbf40)
      {
        namespace Memory = NekoIOPTraceMemory;
        REQUIRE(event.value3 == nekoPackIOPMemoryTraceMetadata({
          BASE + offsets[access], 1,
          access == 0 ? Memory::AccessKind::DataStore : Memory::AccessKind::DataLoad,
          Memory::Outcome::Completed, Memory::CacheRoute::Uncached, true,
          static_cast<std::uint8_t>(1u << (offsets[access] % 4))}));
      }
      if (access != 0)
      {
        REQUIRE(completed);
      }
      ++access;
    }
    REQUIRE(access == cycles.size());
    NekoSystem coldPlain;
    prepare(coldPlain);
    coldPlain.runMasterCycles(145);
    REQUIRE(coldPlain.trace().empty());
    IOPTraceTest::requireMachine(coldPlain, reference);
    if (upper != 0xbf40)
    {
      continue;
    }
    struct Cut
    {
      std::uint64_t cycle;
      std::uint8_t phase;
    };
    const Cut cuts[] = {{25, 0}, {33, 1}, {40, 1}, {41, 2}, {81, 0}};
    for (const auto &cut : cuts)
    {
      CAPTURE(cut.cycle);
      NekoSystem source;
      source.loadState(initial);
      source.runMasterCycles(cut.cycle);
      const SaveStateMutation fields(source.saveState());
      REQUIRE(fields.readByte("system.iop.cdvd.subcommandParameterCount") == 0);
      REQUIRE(fields.readByte("system.iop.cdvd.subcommandPhase") == cut.phase);
      if (cut.cycle == 41 || cut.cycle == 81)
      {
        REQUIRE(source.iopCore().delayedResultContinuation().source ==
          IOPDelayedResultSource::Load);
      }
      requireContinuationModes(reference, source);
    }
  }
}

TEST_CASE("CDVD wake-up authored guest consumes ten bytes across restored trace modes",
  "[iop][cdvd-trace][cdvd-wake-up][cdvd-wake-up-conformance][save-state]")
{
  for (const IOPWord upper : {0x1f40u, 0x9f40u, 0xbf40u})
  {
    CAPTURE(upper);
    std::vector<IOPWord> program = {
      0x3c010000 | upper, 0x34212000, 0x34020022,
      0x10000002, 0xa0220016, 0xffffffff,
      0x90230017, 0, 0x30640080, 0x1480fffc, 0
    };
    for (unsigned index = 0; index < 10; ++index)
    {
      program.insert(program.end(), {0x90280018, 0, 0x90290017, 0});
    }
    program.insert(program.end(), {
      0x902a0018, 0, 0x902b0008, 0, 0x34140001
    });
    const auto prepare = [&program](NekoSystem &system) {
      installProgram(system, 0, program);
      system.iopCore().startExecution(0xa0000000);
    };
    NekoSystem reference;
    prepare(reference);
    const auto initial = reference.saveState();
    reference.startTrace();
    reference.runMasterCycles(433);
    REQUIRE(reference.iopCore().stopReason() == IOPStopReason::None);
    REQUIRE(reference.iopCore().retiredInstructions() == program.size() - 1);
    for (const unsigned reg : {3u, 8u, 10u, 11u})
    {
      REQUIRE(reference.iopCore().generalRegister(reg) == 0);
    }
    REQUIRE(reference.iopCore().generalRegister(9) == 0x40);
    REQUIRE(reference.iopCore().generalRegister(20) == 1);
    REQUIRE(reference.iopInterruptController().status() == 0);
    const auto commands = IOPTraceTest::subsystemEvents(reference, CDVD);
    REQUIRE(commands.size() == 2);
    for (unsigned index = 0; index < commands.size(); ++index)
    {
      REQUIRE(commands[index].type == (index == 0 ? ACCEPTED : COMPLETED));
      REQUIRE(commands[index].masterCycle == (index == 0 ? 33 : 41));
      const auto decoded = nekoDecodeCDVDTrace(commands[index]);
      REQUIRE(decoded.transport == NekoCDVDTrace::Transport::Subcommand);
      REQUIRE(decoded.opcode == 0x22);
      REQUIRE(decoded.beforePhase == (index == 0 ?
        NekoCDVDTrace::Phase::Idle : NekoCDVDTrace::Phase::Pending));
      REQUIRE(decoded.afterPhase == (index == 0 ?
        NekoCDVDTrace::Phase::Pending : NekoCDVDTrace::Phase::Result));
      REQUIRE(decoded.beforeCause == 0);
      REQUIRE(decoded.afterCause == 0);
      REQUIRE(decoded.rtcTicks == commands[index].masterCycle);
      const std::array<std::uint8_t, 16> empty = {};
      REQUIRE(decoded.result == empty);
    }
    struct Access
    {
      std::uint64_t cycle;
      unsigned offset;
      std::uint8_t value;
    };
    std::vector<Access> expected = {{33, 0x16, 0x22}, {41, 0x17, 0}};
    for (unsigned index = 0; index < 10; ++index)
    {
      expected.push_back({81 + 32 * index, 0x18, 0});
      expected.push_back({97 + 32 * index, 0x17,
        static_cast<std::uint8_t>(index == 9 ? 0x40 : 0)});
    }
    expected.push_back({401, 0x18, 0});
    expected.push_back({417, 8, 0});
    const IOPAddress base = (upper << 16) | 0x2000;
    std::size_t access = 0;
    bool completed = false;
    for (const auto &event : reference.trace())
    {
      if (event.subsystem == CDVD && event.type == COMPLETED)
      {
        completed = true;
      }
      if (event.subsystem != NekoTraceSubsystem::IOP ||
          event.type != NekoTraceEventType::MemoryAccess ||
          (event.value0 != base + 8 &&
           (event.value0 < base + 0x16 || event.value0 > base + 0x18)))
      {
        continue;
      }
      REQUIRE(access < expected.size());
      REQUIRE(event.masterCycle == expected[access].cycle);
      REQUIRE(event.value0 == base + expected[access].offset);
      REQUIRE(event.value1 == expected[access].value);
      if (upper == 0xbf40)
      {
        namespace Memory = NekoIOPTraceMemory;
        const auto offset = expected[access].offset;
        REQUIRE(event.value3 == nekoPackIOPMemoryTraceMetadata({
          BASE + offset, 1,
          access == 0 ? Memory::AccessKind::DataStore : Memory::AccessKind::DataLoad,
          Memory::Outcome::Completed, Memory::CacheRoute::Uncached, true,
          static_cast<std::uint8_t>(1u << (offset % 4))}));
      }
      if (access != 0)
      {
        REQUIRE(completed);
      }
      ++access;
    }
    REQUIRE(access == expected.size());
    NekoSystem coldPlain;
    prepare(coldPlain);
    coldPlain.runMasterCycles(433);
    REQUIRE(coldPlain.trace().empty());
    IOPTraceTest::requireMachine(coldPlain, reference);
    if (upper != 0xbf40)
    {
      continue;
    }
    struct Cut
    {
      std::uint64_t cycle;
      std::uint8_t phase;
      std::uint8_t cursor;
    };
    std::vector<Cut> cuts = {{25, 0, 0}, {33, 1, 0}, {40, 1, 0}, {41, 2, 0}};
    for (unsigned index = 0; index < 10; ++index)
    {
      cuts.push_back({81 + 32 * index,
        static_cast<std::uint8_t>(index == 9 ? 0 : 2),
        static_cast<std::uint8_t>(index == 9 ? 0 : index + 1)});
    }
    cuts.push_back({385, 0, 0});
    for (const auto &cut : cuts)
    {
      CAPTURE(cut.cycle);
      NekoSystem source;
      source.loadState(initial);
      source.runMasterCycles(cut.cycle);
      const SaveStateMutation fields(source.saveState());
      REQUIRE(fields.readByte("system.iop.cdvd.subcommandParameterCount") == 0);
      REQUIRE(fields.readByte("system.iop.cdvd.subcommandPhase") == cut.phase);
      REQUIRE(fields.readByte("system.iop.cdvd.resultCursor") == cut.cursor);
      for (unsigned index = 0; index < 16; ++index)
      {
        REQUIRE(fields.readByte("system.iop.cdvd.result", index) == 0);
      }
      if (cut.cycle >= 41)
      {
        REQUIRE(source.iopCore().delayedResultContinuation().source ==
          IOPDelayedResultSource::Load);
      }
      requireContinuationModes(reference, source);
    }
  }
}

TEST_CASE("CDVD OpenConfig authored guest stages its exact request and drains one opaque ACK",
  "[iop][cdvd-trace][cdvd-open-config][cdvd-open-config-conformance][save-state]")
{
  for (const IOPWord upper : {0x1f40u, 0x9f40u, 0xbf40u})
  {
    CAPTURE(upper);
    const std::array<IOPWord, 25> program = {{
      0x3c010000 | upper, 0x34212000,
      0xa0200017, 0x34020001, 0xa0220017, 0x34020002, 0xa0220017,
      0x34020040, 0x10000002, 0xa0220016, 0xffffffff,
      0x90230017, 0, 0x30640080, 0x1480fffc, 0,
      0x90280018, 0, 0x90290017, 0, 0x902a0018, 0,
      0x902b0008, 0, 0x34140001
    }};
    const auto prepare = [&program](NekoSystem &system) {
      installProgram(system, 0, program);
      system.iopCore().startExecution(0xa0000000);
    };
    NekoSystem reference;
    prepare(reference);
    const auto initial = reference.saveState();
    reference.startTrace();
    reference.runMasterCycles(185);
    REQUIRE(reference.iopCore().stopReason() == IOPStopReason::None);
    REQUIRE(reference.iopCore().retiredInstructions() == program.size() - 1);
    for (const unsigned reg : {3u, 8u, 10u, 11u})
    {
      REQUIRE(reference.iopCore().generalRegister(reg) == 0);
    }
    REQUIRE(reference.iopCore().generalRegister(9) == 0x40);
    REQUIRE(reference.iopCore().generalRegister(20) == 1);
    REQUIRE(reference.iopInterruptController().status() == 0);
    const auto commands = IOPTraceTest::subsystemEvents(reference, CDVD);
    REQUIRE(commands.size() == 2);
    for (unsigned index = 0; index < commands.size(); ++index)
    {
      REQUIRE(commands[index].type == (index == 0 ? ACCEPTED : COMPLETED));
      REQUIRE(commands[index].masterCycle == (index == 0 ? 73 : 81));
      const auto decoded = nekoDecodeCDVDTrace(commands[index]);
      REQUIRE(decoded.transport == NekoCDVDTrace::Transport::Subcommand);
      REQUIRE(decoded.opcode == 0x40);
      REQUIRE(decoded.beforePhase == (index == 0 ?
        NekoCDVDTrace::Phase::Idle : NekoCDVDTrace::Phase::Pending));
      REQUIRE(decoded.afterPhase == (index == 0 ?
        NekoCDVDTrace::Phase::Pending : NekoCDVDTrace::Phase::Result));
      REQUIRE(decoded.beforeCause == 0);
      REQUIRE(decoded.afterCause == 0);
      REQUIRE(decoded.rtcTicks == commands[index].masterCycle);
      const std::array<std::uint8_t, 16> empty = {};
      REQUIRE(decoded.result == empty);
    }
    const IOPAddress base = (upper << 16) | 0x2000;
    const std::array<std::uint64_t, 9> cycles = {{
      17, 33, 49, 73, 81, 121, 137, 153, 169
    }};
    const std::array<std::uint8_t, 9> values = {{
      0, 1, 2, 0x40, 0, 0, 0x40, 0, 0
    }};
    const std::array<unsigned, 9> offsets = {{
      0x17, 0x17, 0x17, 0x16, 0x17, 0x18, 0x17, 0x18, 8
    }};
    std::size_t access = 0;
    bool completed = false;
    for (const auto &event : reference.trace())
    {
      if (event.subsystem == CDVD && event.type == COMPLETED)
      {
        completed = true;
      }
      if (event.subsystem != NekoTraceSubsystem::IOP ||
          event.type != NekoTraceEventType::MemoryAccess ||
          (event.value0 != base + 8 &&
           (event.value0 < base + 0x16 || event.value0 > base + 0x18)))
      {
        continue;
      }
      REQUIRE(access < cycles.size());
      REQUIRE(event.masterCycle == cycles[access]);
      REQUIRE(event.value0 == base + offsets[access]);
      REQUIRE(event.value1 == values[access]);
      if (upper == 0xbf40)
      {
        namespace Memory = NekoIOPTraceMemory;
        REQUIRE(event.value3 == nekoPackIOPMemoryTraceMetadata({
          BASE + offsets[access], 1,
          access < 4 ? Memory::AccessKind::DataStore : Memory::AccessKind::DataLoad,
          Memory::Outcome::Completed, Memory::CacheRoute::Uncached, true,
          static_cast<std::uint8_t>(1u << (offsets[access] % 4))}));
      }
      if (access >= 4)
      {
        REQUIRE(completed);
      }
      ++access;
    }
    REQUIRE(access == cycles.size());
    NekoSystem coldPlain;
    prepare(coldPlain);
    coldPlain.runMasterCycles(185);
    REQUIRE(coldPlain.trace().empty());
    IOPTraceTest::requireMachine(coldPlain, reference);
    if (upper != 0xbf40)
    {
      continue;
    }
    struct Cut
    {
      std::uint64_t cycle;
      std::uint8_t parameterCount;
      std::uint8_t phase;
    };
    const Cut cuts[] = {
      {9, 0, 0}, {17, 1, 0}, {33, 2, 0}, {49, 3, 0}, {65, 3, 0},
      {73, 0, 1}, {80, 0, 1}, {81, 0, 2}, {121, 0, 0}, {137, 0, 0}
    };
    for (const auto &cut : cuts)
    {
      CAPTURE(cut.cycle);
      NekoSystem source;
      source.loadState(initial);
      source.runMasterCycles(cut.cycle);
      const SaveStateMutation fields(source.saveState());
      REQUIRE(fields.readByte("system.iop.cdvd.subcommandParameterCount") ==
        cut.parameterCount);
      REQUIRE(fields.readByte("system.iop.cdvd.subcommandPhase") == cut.phase);
      REQUIRE(fields.readByte("system.iop.cdvd.resultCursor") == 0);
      for (unsigned index = 0; index < 16; ++index)
      {
        REQUIRE(fields.readByte("system.iop.cdvd.subcommandParameters", index) ==
          (index < cut.parameterCount ? index : 0));
        REQUIRE(fields.readByte("system.iop.cdvd.result", index) == 0);
      }
      if (cut.cycle >= 81)
      {
        REQUIRE(source.iopCore().delayedResultContinuation().source ==
          IOPDelayedResultSource::Load);
      }
      requireContinuationModes(reference, source);
    }
  }
}

TEST_CASE("CDVD authored malformed compatibility requests preserve staging and rejected branches",
  "[iop][cdvd-trace][cdvd-forbid-dvdp][cdvd-forbid-dvdp-conformance][cdvd-wake-up][cdvd-wake-up-conformance][cdvd-open-config][cdvd-open-config-conformance][save-state]")
{
  struct Request
  {
    IOPWord opcode;
    std::vector<std::uint8_t> parameters;
  };
  const Request requests[] = {
    {0x15, {0}}, {0x15, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}},
    {0x22, {0}}, {0x22, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}},
    {0x40, {0, 1}}, {0x40, {0, 1, 3}}, {0x40, {0, 1, 2, 0}}
  };
  for (const auto &request : requests)
  {
    const auto &parameters = request.parameters;
    const auto count = parameters.size();
    CAPTURE(request.opcode);
    CAPTURE(count);
    std::vector<IOPWord> program = {0x3c01bf40, 0x34212000};
    for (const auto byte : parameters)
    {
      program.push_back(0x34020000 | byte);
      program.push_back(0xa0220017);
    }
    program.insert(program.end(), {
      0x34020000 | request.opcode, 0x10000001, 0xa0220016, 0x34140001
    });
    const auto prepare = [&program](NekoSystem &system) {
      installProgram(system, 0, program);
      system.iopCore().startExecution(0xa0000000);
    };
    const std::uint64_t failureCycle = 33 + 16 * parameters.size();
    NekoSystem reference;
    prepare(reference);
    const auto initial = reference.saveState();
    reference.startTrace();
    reference.runMasterCycles(failureCycle);
    REQUIRE(reference.iopCore().stopReason() == IOPStopReason::StoreFailure);
    REQUIRE(reference.iopCore().retiredInstructions() == program.size() - 2);
    REQUIRE(reference.iopCore().programCounter() ==
      0xa0000000 + (program.size() - 2) * 4);
    REQUIRE(reference.iopCore().storeFailure().access.physicalAddress == BASE + 0x16);
    REQUIRE(reference.iopCore().generalRegister(20) == 0);
    REQUIRE(reference.iopInterruptController().status() == 0);
    REQUIRE(IOPTraceTest::subsystemEvents(reference, CDVD).empty());
    const SaveStateMutation fields(reference.saveState());
    REQUIRE(fields.readByte("system.iop.cdvd.lastSubcommand") == 0);
    REQUIRE(fields.readByte("system.iop.cdvd.subcommandPhase") == 0);
    REQUIRE(fields.readByte("system.iop.cdvd.subcommandParameterCount") == count);
    for (std::size_t index = 0; index < 16; ++index)
    {
      REQUIRE(fields.readByte("system.iop.cdvd.subcommandParameters", index) ==
        (index < parameters.size() ? parameters[index] : 0));
      REQUIRE(fields.readByte("system.iop.cdvd.result", index) == 0);
    }
    NekoSystem coldPlain;
    prepare(coldPlain);
    coldPlain.runMasterCycles(failureCycle);
    IOPTraceTest::requireMachine(coldPlain, reference);
    for (const auto cut : {std::uint64_t{25}, failureCycle - 8})
    {
      CAPTURE(cut);
      NekoSystem source;
      source.loadState(initial);
      source.runMasterCycles(cut);
      requireContinuationModes(reference, source);
    }
  }
}

TEST_CASE("CDVD compatibility results reject guest writes across reset and restore",
  "[iop][cdvd-trace][cdvd-forbid-dvdp][cdvd-forbid-dvdp-conformance][cdvd-wake-up][cdvd-wake-up-conformance][cdvd-open-config][cdvd-open-config-conformance][save-state]")
{
  struct Request
  {
    IOPWord opcode;
    unsigned consumed;
    IOPWord offset;
    IOPWord rejectedOpcode = 0;
  };
  const Request requests[] = {
    {0x15, 0, 0x16}, {0x15, 0, 0x17},
    {0x22, 0, 0x16}, {0x22, 0, 0x17},
    {0x22, 9, 0x16}, {0x22, 9, 0x17},
    {0x40, 0, 0x16}, {0x40, 0, 0x17},
    {0x40, 1, 0x16, 0x41}, {0x40, 1, 0x16, 0x42}, {0x40, 1, 0x16, 0x43}
  };
  for (const auto &request : requests)
  {
    const auto offset = request.offset;
    CAPTURE(request.opcode);
    CAPTURE(request.consumed);
    CAPTURE(offset);
    CAPTURE(request.rejectedOpcode);
    std::vector<IOPWord> program = {0x3c01bf40, 0x34212000};
    if (request.opcode == 0x40)
    {
      program.insert(program.end(), {
        0xa0200017, 0x34020001, 0xa0220017, 0x34020002, 0xa0220017
      });
    }
    program.insert(program.end(), {0x34020000 | request.opcode, 0xa0220016});
    const std::uint64_t acceptedCycle = (program.size() - 1) * 8 + 1;
    for (unsigned index = 0; index < request.consumed; ++index)
    {
      program.insert(program.end(), {0x90280018, 0});
    }
    if (request.rejectedOpcode != 0)
    {
      program.push_back(0x34020000 | request.rejectedOpcode);
    }
    program.insert(program.end(), {
      0x10000001, 0xa0220000 | offset, 0x34140001
    });
    const std::uint64_t failureCycle = (program.size() - 2) * 8 + 1;
    const auto prepare = [&program](NekoSystem &system) {
      installProgram(system, 0, program);
      system.iopCore().startExecution(0xa0000000);
    };
    NekoSystem reference;
    prepare(reference);
    const auto initial = reference.saveState();
    reference.startTrace();
    reference.runMasterCycles(failureCycle);
    REQUIRE(reference.iopCore().stopReason() == IOPStopReason::StoreFailure);
    REQUIRE(reference.iopCore().retiredInstructions() == program.size() - 2);
    REQUIRE(reference.iopCore().programCounter() ==
      0xa0000000 + (program.size() - 2) * 4);
    REQUIRE(reference.iopCore().storeFailure().access.physicalAddress == BASE + offset);
    REQUIRE(reference.iopCore().generalRegister(20) == 0);
    REQUIRE(reference.iopInterruptController().status() == 0);
    const auto commands = IOPTraceTest::subsystemEvents(reference, CDVD);
    REQUIRE(commands.size() == 2);
    REQUIRE(commands[0].masterCycle == acceptedCycle);
    REQUIRE(commands[1].masterCycle == acceptedCycle + 8);
    const SaveStateMutation fields(reference.saveState());
    REQUIRE(fields.readByte("system.iop.cdvd.lastSubcommand") == request.opcode);
    const bool drained = request.opcode == 0x40 && request.consumed == 1;
    REQUIRE(fields.readByte("system.iop.cdvd.subcommandPhase") == (drained ? 0 : 2));
    REQUIRE(fields.readByte("system.iop.cdvd.resultCursor") ==
      (drained ? 0 : request.consumed));
    REQUIRE(fields.readByte("system.iop.cdvd.subcommandParameterCount") == 0);
    for (unsigned index = 0; index < 16; ++index)
    {
      REQUIRE(fields.readByte("system.iop.cdvd.subcommandParameters", index) == 0);
      REQUIRE(fields.readByte("system.iop.cdvd.result", index) ==
        (request.opcode == 0x15 && index == 0 ? 5 : 0));
    }
    NekoSystem coldPlain;
    prepare(coldPlain);
    coldPlain.runMasterCycles(failureCycle);
    IOPTraceTest::requireMachine(coldPlain, reference);
    NekoSystem fresh;
    fresh.runMasterCycles(9);
    std::vector<std::uint64_t> cuts = {
      acceptedCycle - 8, acceptedCycle, acceptedCycle + 8, failureCycle - 1
    };
    if (request.consumed > 1)
    {
      cuts.push_back(acceptedCycle - 8 + 16 * request.consumed);
    }
    for (const auto cut : cuts)
    {
      CAPTURE(cut);
      NekoSystem source;
      source.loadState(initial);
      source.runMasterCycles(cut);
      const auto checkpoint = source.saveState();
      source.reset();
      source.startTrace();
      source.runMasterCycles(9);
      IOPTraceTest::requireMachine(source, fresh);
      REQUIRE(IOPTraceTest::subsystemEvents(source, CDVD).empty());
      source.stopTrace();
      source.loadState(checkpoint);
      requireContinuationModes(reference, source);
    }
  }
}

TEST_CASE("CDVD wake-up guest completes before polling on the next pulse even while halted",
  "[iop][cdvd-trace][cdvd-wake-up]")
{
  for (const IOPWord upper : {0x1f40u, 0x9f40u, 0xbf40u})
  {
    for (const bool halted : {false, true})
    {
      CAPTURE(upper);
      CAPTURE(halted);
      NekoSystem system;
      const std::array<IOPWord, 7> words = {{
        0x3c010000 | upper, 0x34212000, 0x34020022, 0xa0220016,
        0x90230017, 0, 0x34040001
      }};
      installProgram(system, 0, words);
      system.iopCore().startExecution(0xa0000000);
      system.startTrace();
      system.runMasterCycles(25);
      REQUIRE(system.iopCore().retiredInstructions() == 4);
      REQUIRE(system.iopBus().read8(BASE + 0x17).value == 0xc0);
      system.runMasterCycles(7);
      REQUIRE(system.iopBus().read8(BASE + 0x17).value == 0xc0);
      if (halted)
      {
        system.iopCore().haltExecution();
      }
      system.clockMasterCycle();
      REQUIRE(system.iopCore().exception().kind == IOPException::None);
      REQUIRE(system.iopCore().retiredInstructions() == (halted ? 4 : 5));
      REQUIRE(system.iopBus().read8(BASE + 0x17).value == 0);
      REQUIRE(system.iopInterruptController().status() == 0);
      const auto commands = IOPTraceTest::subsystemEvents(system, CDVD);
      REQUIRE(commands.size() == 2);
      for (std::size_t index = 0; index < commands.size(); ++index)
      {
        const auto decoded = nekoDecodeCDVDTrace(commands[index]);
        REQUIRE(commands[index].masterCycle == (index == 0 ? 25 : 33));
        REQUIRE(commands[index].type == (index == 0 ? ACCEPTED : COMPLETED));
        REQUIRE(decoded.opcode == 0x22);
        const std::array<std::uint8_t, 16> empty = {};
        REQUIRE(decoded.result == empty);
      }
      bool completed = false;
      bool polled = false;
      for (const auto &event : system.trace())
      {
        if (event.subsystem == CDVD && event.type == COMPLETED)
        {
          completed = true;
        }
        if (event.subsystem == NekoTraceSubsystem::IOP &&
            event.type == NekoTraceEventType::MemoryAccess &&
            event.value0 == ((upper << 16) | 0x2017))
        {
          REQUIRE(completed);
          polled = true;
        }
      }
      REQUIRE(completed);
      REQUIRE(polled == !halted);
      for (unsigned index = 0; index < 10; ++index)
      {
        REQUIRE(system.iopBus().read8(BASE + 0x18).value == 0);
        REQUIRE(system.iopBus().read8(BASE + 0x17).value == (index == 9 ? 0x40 : 0));
      }
    }
  }
}

TEST_CASE("CDVD one-byte compatibility publication and draining preserve traced continuation",
  "[iop][cdvd-trace][cdvd-boot-certify][cdvd-forbid-dvdp][save-state]")
{
  for (const IOPWord opcode : {0x15u, 0x1au})
  {
    CAPTURE(opcode);
    const std::array<IOPWord, 9> words = {{
      0x3c01bf40, 0x34212000, 0x34020000 | opcode, 0xa0220016,
      0x90230017, 0x90240018, 0x90250017, 0, 0x34060001
    }};
    const auto prepare = [&words, opcode](NekoSystem &system) {
      if (opcode == 0x1a)
      {
        for (const auto byte : {2, 0, 0x41, 0x43})
        {
          write(system, 0x17, static_cast<std::uint8_t>(byte));
        }
      }
      installProgram(system, 0, words);
      system.iopCore().startExecution(0xa0000000);
    };
    NekoSystem reference;
    prepare(reference);
    reference.startTrace();
    reference.runMasterCycles(65);
    REQUIRE(reference.iopCore().exception().kind == IOPException::None);
    REQUIRE(reference.iopCore().retiredInstructions() == words.size());
    REQUIRE(reference.iopCore().generalRegister(3) == 0);
    const std::uint8_t response = opcode == 0x15 ? 5 : 1;
    REQUIRE(reference.iopCore().generalRegister(4) == response);
    REQUIRE(reference.iopCore().generalRegister(5) == 0x40);
    REQUIRE(reference.iopCore().generalRegister(6) == 1);
    REQUIRE(reference.iopInterruptController().status() == 0);
    const auto commands = IOPTraceTest::subsystemEvents(reference, CDVD);
    REQUIRE(commands.size() == 2);
    for (std::size_t index = 0; index < commands.size(); ++index)
    {
      const auto decoded = nekoDecodeCDVDTrace(commands[index]);
      REQUIRE(commands[index].masterCycle == (index == 0 ? 25 : 33));
      REQUIRE(commands[index].type == (index == 0 ? ACCEPTED : COMPLETED));
      REQUIRE(decoded.opcode == opcode);
      const std::array<std::uint8_t, 16> expected = index == 0 ?
        std::array<std::uint8_t, 16>{} : std::array<std::uint8_t, 16>{{response}};
      REQUIRE(decoded.result == expected);
    }
    for (const unsigned cut : {25u, 33u, 49u})
    {
      CAPTURE(cut);
      NekoSystem source;
      prepare(source);
      source.runMasterCycles(cut);
      requireContinuationModes(reference, source);
    }
  }
}

TEST_CASE("CDVD trace observes both completions and guest acceptance in mutation order",
  "[iop][cdvd-trace]")
{
  NekoSystem system;
  prepareStore(system);
  system.startTrace();
  system.clockMasterCycle();
  IOPTraceTest::requireEvents(IOPTraceTest::subsystemEvents(system, CDVD), {
    {17, CDVD, COMPLETED, UINT64_C(0x010000010000), 17, 0, 0},
    {17, CDVD, COMPLETED, UINT64_C(0x010102010801), 17, EPOCH, 0},
    {17, CDVD, ACCEPTED, UINT64_C(0x010101000000), 17, 0, 0}
  });
  std::size_t commandCount = 0;
  bool interruptSeen = false;
  bool storeSeen = false;
  for (const auto &event : system.trace())
  {
    if (event.subsystem == CDVD)
    {
      ++commandCount;
      if (commandCount == 2)
      {
        REQUIRE(interruptSeen);
      }
    }
    if (event.subsystem == NekoTraceSubsystem::IOPInterruptController)
    {
      REQUIRE(commandCount == 1);
      interruptSeen = true;
    }
    if (event.subsystem == NekoTraceSubsystem::IOP &&
        event.type == NekoTraceEventType::MemoryAccess &&
        event.value0 == 0xbf402004)
    {
      REQUIRE(commandCount == 3);
      storeSeen = true;
    }
  }
  REQUIRE(interruptSeen);
  REQUIRE(storeSeen);
}

TEST_CASE("EE CDVD passive guest traces precede completion and restore across IOP draining",
  "[ee][iop][cdvd-trace][ee-cdvd][cdvd-authored][save-state]")
{
  NekoSystem reference;
  const std::array<std::uint32_t, 9> eeWords = {{
    0x80220005, // lb r2, NREADY
    0x90230005, // lbu r3, NREADY
    0x90240017, // lbu r4, SREADY
    0x90250008, // lbu r5, device cause
    0x90260016, // lbu r6, SCMD
    0x9027000f, // lbu r7, no-disc type
    0x9028000a, // lbu r8, stopped status
    0x90290004, // lbu r9, NCMD
    0x902a0006  // lbu r10, error
  }};
  for (std::size_t index = 0; index < eeWords.size(); ++index)
  {
    reference.eeBus().write32(static_cast<std::uint32_t>(index * 4),
      eeWords[index]);
  }
  std::array<IOPWord, 10> iopWords = {{0x3c01bf40, 0x34212000}};
  for (IOPWord index = 0; index < 8; ++index)
  {
    iopWords[index + 2] = 0x90200018 | ((8 + index) << 16);
  }
  installProgram(reference, 0x1000, iopWords);
  auto &ee = reference.eeCore();
  ee.setGeneralRegister(1, {0xbf402000, 0});
  ee.setCOP0Register(EECOP0Register::Status, 0);
  ee.startExecution(EEMemoryMap::KSEG1_BASE);
  reference.iopCore().startExecution(0xa0001000);
  write(reference, 4, 0);
  write(reference, 0x16, 8);
  const auto initial = reference.saveState();
  reference.startTrace();

  reference.runMasterCycles(81);

  REQUIRE(ee.pendingException() == EEException::None);
  REQUIRE(ee.generalRegister(2).low == UINT64_C(0xffffffffffffff80));
  const std::array<std::uint8_t, 9> bytes = {{0x80, 0x40, 0, 1, 8, 0, 0, 0, 0}};
  const std::array<std::uint32_t, 9> offsets = {{5, 5, 0x17, 8, 0x16, 15, 10, 4, 6}};
  for (std::size_t index = 1; index < bytes.size(); ++index)
  {
    REQUIRE(ee.generalRegister(2 + index).low == bytes[index]);
  }
  const std::array<std::uint8_t, 8> rtc = {{0, 0, 0, 0, 0, 1, 1, 0}};
  for (std::size_t index = 0; index < rtc.size(); ++index)
  {
    REQUIRE(reference.iopCore().generalRegister(8 + index) == rtc[index]);
  }
  REQUIRE(reference.iopCore().stopReason() == IOPStopReason::None);
  REQUIRE(reference.iopBus().read8(BASE + 0x17).value == 0x40);
  REQUIRE(reference.iopBus().read8(BASE + 8).value == 1);
  REQUIRE(reference.iopInterruptController().status() == 4);

  std::vector<std::size_t> loads;
  std::vector<std::size_t> completions;
  const auto &events = reference.trace();
  for (std::size_t index = 0; index < events.size(); ++index)
  {
    const auto &event = events[index];
    if (event.subsystem == NekoTraceSubsystem::EE &&
        event.type == NekoTraceEventType::MemoryAccess &&
        event.value0 >= 0xbf402000 && event.value0 < 0xbf402020)
    {
      loads.push_back(index);
    }
    if (event.subsystem == CDVD)
    {
      REQUIRE(event.type == COMPLETED);
      REQUIRE(event.masterCycle == 1);
      completions.push_back(index);
    }
  }
  REQUIRE(loads.size() == bytes.size());
  REQUIRE(completions.size() == 2);
  REQUIRE(loads[0] < completions[0]);
  REQUIRE(completions[0] < completions[1]);
  REQUIRE(completions[1] < loads[1]);
  IOPTraceTest::requireEvents(IOPTraceTest::subsystemEvents(reference, CDVD), {
    {1, CDVD, COMPLETED, UINT64_C(0x010000010000), 1, 0, 0},
    {1, CDVD, COMPLETED, UINT64_C(0x010102010801), 1, EPOCH, 0}
  });
  for (std::size_t index = 0; index < loads.size(); ++index)
  {
    CAPTURE(index);
    const auto &event = events[loads[index]];
    using namespace NekoEETraceMemory;
    REQUIRE(event.value0 == 0xbf402000 + offsets[index]);
    REQUIRE(event.value1 == bytes[index]);
    REQUIRE(event.value2 == 0);
    REQUIRE(event.value3 == nekoPackEEMemoryTraceMetadata({
      BASE + offsets[index], 1, AccessKind::DataLoad,
      TranslationOutcome::Translated, CacheRoute::Uncached, 2, false, true,
      CacheAccess::Bypassed, TransferOutcome::Completed, FailurePhase::None}));
    bool issued = false;
    for (std::size_t earlier = 0; earlier < loads[index]; ++earlier)
    {
      const auto &instruction = events[earlier];
      if (instruction.masterCycle == event.masterCycle &&
          instruction.subsystem == NekoTraceSubsystem::EE &&
          instruction.type == NekoTraceEventType::InstructionIssued)
      {
        REQUIRE(instruction.value0 == EEMemoryMap::KSEG1_BASE + index * 4);
        REQUIRE(instruction.value1 == eeWords[index]);
        issued = true;
      }
    }
    REQUIRE(issued);
  }
  REQUIRE(events[loads[0]].masterCycle == 1);
  for (const std::uint64_t cut : {0, 17, 49, 73})
  {
    CAPTURE(cut);
    NekoSystem source;
    source.loadState(initial);
    source.runMasterCycles(cut);
    REQUIRE(source.iopBus().read8(BASE + 5).value == (cut == 0 ? 0x80 : 0x40));
    REQUIRE(source.iopBus().read8(BASE + 0x17).value ==
      (cut == 0 ? 0xc0 : cut == 73 ? 0x40 : 0));
    requireContinuationModes(reference, source);
  }
}

TEST_CASE("CDVD trace snapshots RTC on completion rather than command acceptance",
  "[iop][cdvd-trace]")
{
  NekoSystem system;
  SaveStateMutation seeded(system.saveState());
  seeded.writeScalar("system.iop.cdvd.rtcTicks", PS2Clock::MASTER_HZ - 26);
  seeded.refreshChecksum();
  system.loadState(seeded);
  const std::array<IOPWord, 4> program = {{
    0x3c01bf40, 0x34212000, 0x34020008, 0xa0220016
  }};
  for (std::size_t index = 0; index < program.size(); ++index)
  {
    REQUIRE(system.iopBus().write32(static_cast<IOPAddress>(index * 4),
      program[index]).status == IOPBusStatus::Completed);
  }
  system.iopCore().startExecution(0xa0000000);
  system.startTrace();
  system.runMasterCycles(33);
  IOPTraceTest::requireEvents(IOPTraceTest::subsystemEvents(system, CDVD), {
    {25, CDVD, ACCEPTED, 0x01000801, PS2Clock::MASTER_HZ - 1, 0, 0},
    {33, CDVD, COMPLETED, 0x02010801, PS2Clock::MASTER_HZ + 7, EPOCH | 0x100, 0}
  });
}

TEST_CASE("CDVD completes within the actual ten-transition IOP interrupt cycle",
  "[iop][cdvd-trace]")
{
  NekoSystem system;
  const std::array<IOPWord, 3> program = {{
    0x3c01bf80, 0x34211078, 0x8c220000 // lw r2, I_CTRL
  }};
  for (std::size_t index = 0; index < program.size(); ++index)
  {
    REQUIRE(system.iopBus().write32(static_cast<IOPAddress>(index * 4),
      program[index]).status == IOPBusStatus::Completed);
  }
  system.iopCore().startExecution(0xa0000000);
  system.runMasterCycles(16);
  for (std::size_t index = 0; index < 6; ++index)
  {
    system.iopTimers().writeTarget(index, 0);
    system.iopTimers().writeMode(index, 0x30);
    system.iopTimers().writeCount(index, index < 3 ? 0xffff : 0xffffffff);
  }
  REQUIRE(system.sif().pushSIF1Quadword({{0x80001000, 0, 0, 0}}) ==
    SIFTransferOutcome::Completed);
  REQUIRE(system.iopSIF1DMAC().writeChannelControl(0x01000400) ==
    IOPDMAWriteOutcome::Completed);
  system.iopDMAController().writePriority(IOPDMABank::Secondary, 0x8000);
  system.iopInterruptController().writeMask(0x1c07e);
  system.iopInterruptController().writeControl(1);
  write(system, 4, 0);
  write(system, 0x16, 8);
  system.eeBus().write32(0, 0xac220000);
  system.eeCore().setGeneralRegister(1, {0xb000f240, 0});
  system.eeCore().setGeneralRegister(2, {0x40100, 0});
  system.eeCore().startExecution(0xa0000000);
  system.startTrace();
  system.clockMasterCycle();
  const auto events = IOPTraceTest::subsystemEvents(
    system, NekoTraceSubsystem::IOPInterruptController);
  const std::array<std::uint32_t, 10> expected = {{
    0x2, 0x12, 0x32, 0x72, 0x4072, 0xc072, 0x1c072, 0x1c076, 0x1c07e, 0x1c07e
  }};
  REQUIRE(events.size() == expected.size());
  for (std::size_t index = 0; index < events.size(); ++index)
  {
    CAPTURE(index);
    REQUIRE(events[index].masterCycle == 17);
    REQUIRE(events[index].value0 == expected[index]);
    REQUIRE(events[index].value1 == 0x1c07e);
    REQUIRE(events[index].value2 == (index == 9 ? 0 : 1));
  }
  REQUIRE(IOPTraceTest::subsystemEvents(system, CDVD).size() == 2);
  REQUIRE(system.iopCore().retiredInstructions() == 3);
  REQUIRE(system.iopInterruptController().control() == 0);
}

TEST_CASE("CDVD tracing preserves machines and streams past a full retained prefix",
  "[iop][cdvd-trace][save-state]")
{
  NekoSystem plain;
  prepareStore(plain);
  const auto initial = plain.saveState();
  NekoSystem full;
  NekoSystem limited;
  full.loadState(initial);
  limited.loadState(initial);
  const auto observer = std::make_shared<Observer>();
  full.startTrace();
  limited.startTrace(1, observer);
  plain.runMasterCycles(9);
  full.runMasterCycles(9);
  limited.runMasterCycles(9);
  IOPTraceTest::requireMachine(full, plain);
  IOPTraceTest::requireMachine(limited, plain);
  REQUIRE(plain.trace().empty());
  REQUIRE(limited.trace().size() == 1);
  REQUIRE(limited.traceTruncated());
  REQUIRE_FALSE(observer->overflow);
  const std::vector<NekoTraceEvent> observed(
    observer->events.begin(), observer->events.begin() + observer->count);
  IOPTraceTest::requireEvents(observed, full.trace());
  const auto commands = IOPTraceTest::subsystemEvents(full, CDVD);
  REQUIRE(commands.size() == 4);
  REQUIRE(commands.back().masterCycle == 25);
  REQUIRE(commands.back().type == COMPLETED);
  REQUIRE(commands.back().value0 == UINT64_C(0x010100010000));
  for (const auto *system : {&plain, &full, &limited})
  {
    REQUIRE(CDVDObservationTestAccess::detached(system->cdvdController()));
  }
}

TEST_CASE("CDVD polling result reads acknowledgements and rejection add no command events",
  "[iop][cdvd-trace]")
{
  for (const IOPWord instruction :
       {0x90220017u, 0x90220018u, 0xa0200008u, 0xa0200016u})
  {
    CAPTURE(instruction);
    NekoSystem system;
    prepareStore(system, instruction);
    system.startTrace();
    system.clockMasterCycle();
    const auto commands = IOPTraceTest::subsystemEvents(system, CDVD);
    REQUIRE(commands.size() == 2);
    REQUIRE(commands[0].type == COMPLETED);
    REQUIRE(commands[1].type == COMPLETED);
    REQUIRE(CDVDObservationTestAccess::detached(system.cdvdController()));
  }
}

TEST_CASE("CDVD parameter guest continuation is canonical across trace modes",
  "[iop][cdvd-trace][cdvd-parameters][save-state]")
{
  const std::array<IOPWord, 8> program = {{
    0x3c01bf40, 0x34212000, 0x240200a5, 0xa0220017,
    0xa0200017, 0x90230017, 0x24020003, 0xa0220016
  }};
  NekoSystem reference;
  NekoSystem source;
  installProgram(reference, 0, program);
  installProgram(source, 0, program);
  reference.iopCore().startExecution(0xa0000000);
  source.iopCore().startExecution(0xa0000000);
  reference.startTrace();
  reference.runMasterCycles(57);
  source.runMasterCycles(25);
  REQUIRE(source.iopCore().retiredInstructions() == 4);
  REQUIRE(reference.iopCore().retiredInstructions() == 7);
  REQUIRE(reference.iopCore().stopReason() == IOPStopReason::StoreFailure);
  REQUIRE(IOPTraceTest::subsystemEvents(reference, CDVD).empty());
  REQUIRE(reference.iopCore().generalRegister(3) == 0x40);
  REQUIRE(reference.iopInterruptController().status() == 0);
  const SaveStateMutation fields(reference.saveState());
  REQUIRE(fields.readByte("system.iop.cdvd.subcommandParameterCount") == 2);
  REQUIRE(fields.readByte("system.iop.cdvd.subcommandParameters", 0) == 0xa5);
  REQUIRE(fields.readByte("system.iop.cdvd.subcommandParameters", 1) == 0);
  namespace Memory = NekoIOPTraceMemory;
  const std::array<std::uint64_t, 4> cycles = {{25, 33, 41, 57}};
  const std::array<std::uint8_t, 4> values = {{0xa5, 0, 0x40, 3}};
  std::size_t access = 0;
  for (const auto &event : reference.trace())
  {
    if (event.subsystem != NekoTraceSubsystem::IOP ||
        event.type != NekoTraceEventType::MemoryAccess ||
        (event.value0 != 0xbf402017 && event.value0 != 0xbf402016))
    {
      continue;
    }
    REQUIRE(access < cycles.size());
    REQUIRE(event.masterCycle == cycles[access]);
    REQUIRE(event.value0 == (access == 3 ? 0xbf402016u : 0xbf402017u));
    REQUIRE(event.value1 == values[access]);
    REQUIRE(event.value2 == 0);
    REQUIRE(event.value3 == nekoPackIOPMemoryTraceMetadata({
      access == 3 ? BASE + 0x16 : BASE + 0x17, 1,
      access == 2 ? Memory::AccessKind::DataLoad : Memory::AccessKind::DataStore,
      access == 3 ? Memory::Outcome::Unmapped : Memory::Outcome::Completed,
      Memory::CacheRoute::Uncached, true,
      static_cast<std::uint8_t>(access == 3 ? 4 : 8)}));
    ++access;
  }
  REQUIRE(access == cycles.size());
  requireContinuationModes(reference, source);
}

TEST_CASE("CDVD version guest completes before polling and continues across trace modes",
  "[iop][cdvd-trace][cdvd-version][save-state]")
{
  const std::array<IOPWord, 22> program = {{
    0x3c01bf40, 0x34212000, 0xa0200017, 0x24020003, 0xa0220016,
    0x90230017, 0, 0x90280018, 0, 0x90290018, 0, 0x902a0018, 0,
    0x902b0018, 0, 0x90300017, 0, 0x90310018, 0, 0x90320008, 0,
    0x34140001
  }};
  NekoSystem reference;
  installProgram(reference, 0, program);
  reference.iopCore().startExecution(0xa0000000);
  const auto initial = reference.saveState();
  reference.startTrace();
  reference.runMasterCycles(169);
  REQUIRE(reference.iopCore().retiredInstructions() == program.size());
  REQUIRE(reference.iopCore().stopReason() == IOPStopReason::None);
  const std::array<std::uint8_t, 4> version = {{3, 6, 2, 0}};
  for (unsigned index = 0; index < version.size(); ++index)
  {
    REQUIRE(reference.iopCore().generalRegister(8 + index) == version[index]);
  }
  REQUIRE(reference.iopCore().generalRegister(3) == 0);
  REQUIRE(reference.iopCore().generalRegister(16) == 0x40);
  REQUIRE(reference.iopCore().generalRegister(17) == 0);
  REQUIRE(reference.iopCore().generalRegister(18) == 0);
  REQUIRE(reference.iopCore().generalRegister(20) == 1);
  REQUIRE(reference.iopInterruptController().status() == 0);
  const auto commands = IOPTraceTest::subsystemEvents(reference, CDVD);
  REQUIRE(commands.size() == 2);
  for (unsigned index = 0; index < commands.size(); ++index)
  {
    REQUIRE(commands[index].type == (index == 0 ? ACCEPTED : COMPLETED));
    REQUIRE(commands[index].masterCycle == (index == 0 ? 33 : 41));
    const auto decoded = nekoDecodeCDVDTrace(commands[index]);
    REQUIRE(decoded.transport == NekoCDVDTrace::Transport::Subcommand);
    REQUIRE(decoded.opcode == 3);
    REQUIRE(decoded.beforePhase == (index == 0 ?
      NekoCDVDTrace::Phase::Idle : NekoCDVDTrace::Phase::Pending));
    REQUIRE(decoded.afterPhase == (index == 0 ?
      NekoCDVDTrace::Phase::Pending : NekoCDVDTrace::Phase::Result));
    REQUIRE(decoded.beforeCause == 0);
    REQUIRE(decoded.afterCause == 0);
    REQUIRE(decoded.rtcTicks == commands[index].masterCycle);
    const std::array<std::uint8_t, 16> expected = index == 0 ?
      std::array<std::uint8_t, 16>{} : std::array<std::uint8_t, 16>{{3, 6, 2, 0}};
    REQUIRE(decoded.result == expected);
  }
  namespace Memory = NekoIOPTraceMemory;
  const std::array<std::uint64_t, 9> cycles = {{
    17, 33, 41, 57, 73, 89, 105, 121, 137
  }};
  const std::array<std::uint8_t, 9> values = {{0, 3, 0, 3, 6, 2, 0, 0x40, 0}};
  const std::array<unsigned, 9> offsets = {{
    0x17, 0x16, 0x17, 0x18, 0x18, 0x18, 0x18, 0x17, 0x18
  }};
  std::size_t access = 0;
  bool queryCompleted = false;
  for (const auto &event : reference.trace())
  {
    if (event.subsystem == CDVD && event.type == COMPLETED)
    {
      queryCompleted = true;
    }
    if (event.subsystem != NekoTraceSubsystem::IOP ||
        event.type != NekoTraceEventType::MemoryAccess ||
        event.value0 < 0xbf402016 || event.value0 > 0xbf402018)
    {
      continue;
    }
    REQUIRE(access < cycles.size());
    REQUIRE(event.masterCycle == cycles[access]);
    REQUIRE(event.value0 == 0xbf402000u + offsets[access]);
    REQUIRE(event.value1 == values[access]);
    REQUIRE(event.value3 == nekoPackIOPMemoryTraceMetadata({
      BASE + offsets[access], 1,
      access < 2 ? Memory::AccessKind::DataStore : Memory::AccessKind::DataLoad,
      Memory::Outcome::Completed, Memory::CacheRoute::Uncached, true,
      static_cast<std::uint8_t>(1u << (offsets[access] % 4))}));
    if (event.masterCycle == 41)
    {
      REQUIRE(queryCompleted);
    }
    ++access;
  }
  REQUIRE(access == cycles.size());
  for (const std::uint64_t cut : {17, 33, 40, 41, 57, 73, 89, 105})
  {
    CAPTURE(cut);
    NekoSystem source;
    source.loadState(initial);
    source.runMasterCycles(cut);
    if (cut == 57 || cut == 73 || cut == 89 || cut == 105)
    {
      REQUIRE(source.iopCore().delayedResultContinuation().source ==
        IOPDelayedResultSource::Load);
    }
    requireContinuationModes(reference, source);
  }
}

TEST_CASE("CDVD region guest drains fifteen bytes and continues across trace modes",
  "[iop][cdvd-trace][cdvd-region][save-state]")
{
  std::array<IOPWord, 43> program = {{
    0x3c01bf40, 0x34212000, 0x34020036, 0xa0220016, 0x90230017, 0
  }};
  for (unsigned index = 0; index < 15; ++index)
  {
    program[6 + index * 2] = 0x90200018 | ((8 + index) << 16);
  }
  program[36] = 0x90370017;
  program[38] = 0x90380018;
  program[40] = 0x90390008;
  program[42] = 0x341a0001;
  NekoSystem reference;
  installProgram(reference, 0, program);
  reference.iopCore().startExecution(0xa0000000);
  const auto initial = reference.saveState();
  reference.startTrace();
  reference.runMasterCycles(337);
  REQUIRE(reference.iopCore().retiredInstructions() == program.size());
  REQUIRE(reference.iopCore().stopReason() == IOPStopReason::None);
  const std::array<std::uint8_t, 16> expected = {{
    0, 8, 0, 0x41, 0x41, 0x65, 0x6e, 0x67, 0x41, 0x55, 0, 0, 0, 0, 0
  }};
  for (unsigned index = 0; index < 15; ++index)
  {
    REQUIRE(reference.iopCore().generalRegister(8 + index) == expected[index]);
  }
  REQUIRE(reference.iopCore().generalRegister(3) == 0);
  REQUIRE(reference.iopCore().generalRegister(23) == 0x40);
  REQUIRE(reference.iopCore().generalRegister(24) == 0);
  REQUIRE(reference.iopCore().generalRegister(25) == 0);
  REQUIRE(reference.iopCore().generalRegister(26) == 1);
  REQUIRE(reference.iopInterruptController().status() == 0);
  const auto commands = IOPTraceTest::subsystemEvents(reference, CDVD);
  REQUIRE(commands.size() == 2);
  for (unsigned index = 0; index < commands.size(); ++index)
  {
    REQUIRE(commands[index].type == (index == 0 ? ACCEPTED : COMPLETED));
    REQUIRE(commands[index].masterCycle == (index == 0 ? 25 : 33));
    const auto decoded = nekoDecodeCDVDTrace(commands[index]);
    REQUIRE(decoded.transport == NekoCDVDTrace::Transport::Subcommand);
    REQUIRE(decoded.opcode == 0x36);
    REQUIRE(decoded.beforePhase == (index == 0 ?
      NekoCDVDTrace::Phase::Idle : NekoCDVDTrace::Phase::Pending));
    REQUIRE(decoded.afterPhase == (index == 0 ?
      NekoCDVDTrace::Phase::Pending : NekoCDVDTrace::Phase::Result));
    REQUIRE(decoded.beforeCause == 0);
    REQUIRE(decoded.afterCause == 0);
    REQUIRE(decoded.rtcTicks == commands[index].masterCycle);
    REQUIRE(decoded.result == (index == 0 ? std::array<std::uint8_t, 16>{} : expected));
    REQUIRE(commands[index].value3 == (index == 0 ? 0 : 0x5541));
  }
  bool completed = false;
  unsigned drained = 0;
  for (const auto &event : reference.trace())
  {
    if (event.subsystem == CDVD && event.type == COMPLETED)
    {
      completed = true;
    }
    if (event.subsystem == NekoTraceSubsystem::IOP &&
        event.type == NekoTraceEventType::MemoryAccess &&
        event.value0 == 0xbf402017 && event.masterCycle == 33)
    {
      REQUIRE(completed);
      REQUIRE(event.value1 == 0);
    }
    if (event.subsystem == NekoTraceSubsystem::IOP &&
        event.type == NekoTraceEventType::MemoryAccess &&
        event.value0 == 0xbf402018)
    {
      REQUIRE(completed);
      REQUIRE(drained < expected.size());
      REQUIRE(event.value1 == expected[drained++]);
    }
  }
  REQUIRE(drained == 16);
  for (const std::uint64_t cut : {25, 32, 33, 49, 161, 177, 273, 289})
  {
    CAPTURE(cut);
    NekoSystem source;
    source.loadState(initial);
    source.runMasterCycles(cut);
    requireContinuationModes(reference, source);
  }
}

TEST_CASE("CDVD command observations disarm after a later DMA exception",
  "[iop][cdvd-trace][save-state]")
{
  NekoSystem system;
  write(system, 4, 0);
  write(system, 0x16, 8);
  REQUIRE(system.sif().pushSIF1Quadword({{0x80002001, 4, 0, 0}}) ==
    SIFTransferOutcome::Completed);
  REQUIRE(system.iopSIF1DMAC().writeChannelControl(0x01000400) ==
    IOPDMAWriteOutcome::Completed);
  system.iopDMAController().writePriority(IOPDMABank::Secondary, 0x8000);
  NekoSystem plain;
  plain.loadState(system.saveState());
  system.startTrace();
  REQUIRE_THROWS_AS(system.clockMasterCycle(), IOPDMATransferError);
  REQUIRE_THROWS_AS(plain.clockMasterCycle(), IOPDMATransferError);
  IOPTraceTest::requireMachine(system, plain);
  REQUIRE(IOPTraceTest::subsystemEvents(system, CDVD).size() == 2);
  REQUIRE(CDVDObservationTestAccess::detached(system.cdvdController()));
  const auto retained = system.trace().size();
  write(system, 4, 0);
  REQUIRE(CDVDObservationTestAccess::detached(system.cdvdController()));
  REQUIRE(system.trace().size() == retained);
  system.iopDMAController().writePriority(IOPDMABank::Secondary, 0);
  system.startTrace();
  system.runMasterCycles(8);
  const auto commands = IOPTraceTest::subsystemEvents(system, CDVD);
  REQUIRE(commands.size() == 1);
  REQUIRE(commands[0].type == COMPLETED);
  REQUIRE(commands[0].masterCycle == 9);
  REQUIRE(CDVDObservationTestAccess::detached(system.cdvdController()));
}

TEST_CASE("CDVD transient journal is bounded and excluded from persistence",
  "[iop][cdvd-trace][save-state]")
{
  NekoSystem system;
  NekoObservationOrder order;
  order.next = 9;
  const auto initial = system.saveState();
  CDVDObservationTestAccess::enable(system.cdvdController(), &order);
  REQUIRE(system.saveState() == initial);
  write(system, 4, 0);
  REQUIRE(CDVDObservationTestAccess::count(system.cdvdController()) == 1);
  REQUIRE(CDVDObservationTestAccess::sequence(system.cdvdController(), 0) == 9);
  const auto pending = system.saveState();
  SaveStateMutation invalid(pending);
  invalid.writeScalar("system.iop.cdvd.normalPending", 2);
  invalid.refreshChecksum();
  REQUIRE_THROWS_AS(system.loadState(invalid), std::invalid_argument);
  REQUIRE(system.saveState() == pending);
  REQUIRE(CDVDObservationTestAccess::attached(system.cdvdController(), &order));
  REQUIRE(CDVDObservationTestAccess::count(system.cdvdController()) == 1);
  system.loadState(pending);
  REQUIRE(CDVDObservationTestAccess::attached(system.cdvdController(), &order));
  REQUIRE(CDVDObservationTestAccess::count(system.cdvdController()) == 0);
  REQUIRE(system.cdvdController().completeNormalCommand().completed);
  REQUIRE(CDVDObservationTestAccess::sequence(system.cdvdController(), 0) == 10);
  write(system, 4, 0);
  REQUIRE(system.cdvdController().completeNormalCommand().completed);
  REQUIRE(CDVDObservationTestAccess::count(system.cdvdController()) == 3);
  REQUIRE_THROWS_AS(system.iopBus().write8(BASE + 0x16, 8), std::logic_error);
  CDVDObservationTestAccess::disable(system.cdvdController());
  REQUIRE(CDVDObservationTestAccess::detached(system.cdvdController()));
  REQUIRE(system.cdvdController().completeSubcommand());
  REQUIRE(CDVDObservationTestAccess::detached(system.cdvdController()));
}

TEST_CASE("CDVD trace codec round trips sixteen result bytes and ignores reserved metadata",
  "[cdvd-trace][trace]")
{
  using namespace NekoCDVDTrace;
  Command command;
  command.transport = Transport::Subcommand;
  command.opcode = 8;
  command.beforePhase = Phase::Pending;
  command.afterPhase = Phase::Result;
  command.beforeCause = 1;
  command.afterCause = 1;
  command.rtcTicks = UINT64_MAX;
  command.result = {{
    0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
    0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88
  }};
  const auto metadata = nekoPackCDVDTraceMetadata(command);
  const auto result = nekoPackCDVDTraceResult(command.result);
  REQUIRE(metadata == UINT64_C(0x010102010801));
  REQUIRE(result[0] == UINT64_C(0xf0debc9a78563412));
  REQUIRE(result[1] == UINT64_C(0x8899aabbccddeeff));
  NekoTraceEvent event = {
    1, CDVD, COMPLETED, metadata | UINT64_C(0xffff000000000000),
    command.rtcTicks, result[0], result[1]
  };
  const auto decoded = nekoDecodeCDVDTrace(event);
  REQUIRE(decoded.transport == command.transport);
  REQUIRE(decoded.opcode == command.opcode);
  REQUIRE(decoded.beforePhase == command.beforePhase);
  REQUIRE(decoded.afterPhase == command.afterPhase);
  REQUIRE(decoded.beforeCause == command.beforeCause);
  REQUIRE(decoded.afterCause == command.afterCause);
  REQUIRE(decoded.rtcTicks == command.rtcTicks);
  REQUIRE(decoded.result == command.result);
  event.subsystem = NekoTraceSubsystem::IOP;
  REQUIRE_THROWS_AS(nekoDecodeCDVDTrace(event), std::invalid_argument);
  event.subsystem = CDVD;
  event.type = NekoTraceEventType::Progress;
  REQUIRE_THROWS_AS(nekoDecodeCDVDTrace(event), std::invalid_argument);
}

TEST_CASE("CDVD authored RTC draining and IRQ rearming continue across every trace mode",
  "[iop][cdvd-trace][cdvd-authored][save-state]")
{
  NekoSystem reference;
  std::array<IOPWord, 40> words = {{
    0x3c01bf40, // lui r1, CDVD upper
    0x34212000, // ori r1, r1, CDVD lower
    0x34020008, // ori r2, r0, ReadClock
    0xa0220016, // sb r2, SCMD
    0xa0200004, // sb r0, NCMD
    0x90230017, // lbu r3, SREADY
    0          // load delay
  }};
  for (IOPWord index = 0; index < 8; ++index)
  {
    words[7 + index * 2] = 0x90200018 | ((8 + index) << 16);
  }
  words[23] = 0x90300017; // lbu r16, SREADY
  words[25] = 0x90310008; // lbu r17, device cause
  words[27] = 0x34040001; // ori r4, r0, bit0
  words[28] = 0xa0240008; // sb r4, device cause W1C
  words[29] = 0x3c05bf80; // lui r5, INTC upper
  words[30] = 0x34a51070; // ori r5, r5, INTC lower
  words[31] = 0x2406fffb; // addiu r6, r0, ~source2
  words[32] = 0xaca60000; // sw r6, I_STAT W0C
  words[33] = 0xa0200004; // sb r0, NCMD again
  words[35] = 0x90320008; // lbu r18, device cause
  words[37] = 0x90330005; // lbu r19, NREADY
  words[39] = 0x34140001; // ori r20, r0, done
  installProgram(reference, 0x1000, words);
  SaveStateMutation seeded(reference.saveState());
  seeded.writeScalar("system.iop.cdvd.rtcTicks", PS2Clock::MASTER_HZ - 30);
  seeded.refreshChecksum();
  reference.loadState(seeded);
  reference.iopCore().startExecution(0xa0001000);
  const auto initial = reference.saveState();
  reference.startTrace();
  reference.runMasterCycles(313);
  REQUIRE(reference.iopCore().retiredInstructions() == 40);
  REQUIRE(reference.iopCore().stopReason() == IOPStopReason::None);
  const std::array<std::uint8_t, 8> expected = {{0, 1, 0, 0, 0, 1, 1, 0}};
  for (std::size_t index = 0; index < expected.size(); ++index)
  {
    REQUIRE(reference.iopCore().generalRegister(8 + index) == expected[index]);
  }
  REQUIRE(reference.iopCore().generalRegister(3) == 0);
  REQUIRE(reference.iopCore().generalRegister(16) == 0x40);
  REQUIRE(reference.iopCore().generalRegister(17) == 1);
  REQUIRE(reference.iopCore().generalRegister(18) == 1);
  REQUIRE(reference.iopCore().generalRegister(19) == 0x40);
  REQUIRE(reference.iopCore().generalRegister(20) == 1);
  REQUIRE(reference.iopInterruptController().status() == 4);
  const auto commands = IOPTraceTest::subsystemEvents(reference, CDVD);
  REQUIRE(commands.size() == 6);
  const std::array<std::uint64_t, 6> cycles = {{25, 33, 33, 41, 265, 273}};
  for (std::size_t index = 0; index < commands.size(); ++index)
  {
    REQUIRE(commands[index].masterCycle == cycles[index]);
  }
  REQUIRE(commands[1].type == COMPLETED);
  REQUIRE(commands[1].value2 == (EPOCH | 0x100));
  REQUIRE(commands[2].type == ACCEPTED);
  for (const std::uint64_t cut : {25, 32, 33, 57, 73, 185, 225, 257, 265})
  {
    CAPTURE(cut);
    NekoSystem source;
    source.loadState(initial);
    source.runMasterCycles(cut);
    if (cut == 57 || cut == 73 || cut == 185)
    {
      REQUIRE(source.iopCore().delayedResultContinuation().source ==
        IOPDelayedResultSource::Load);
    }
    if (cut == 225)
    {
      REQUIRE(source.iopBus().read8(BASE + 8).value == 0);
      REQUIRE(source.iopInterruptController().status() == 4);
    }
    if (cut == 257)
    {
      REQUIRE(source.iopInterruptController().status() == 0);
    }
    requireContinuationModes(reference, source);
  }
}

TEST_CASE("CDVD authored IRQ handlers acknowledge both latches return and service again",
  "[iop][cdvd-trace][cdvd-authored][save-state]")
{
  for (const auto firstAck : {0xa0270008u, 0xacb50000u})
  {
    CAPTURE(firstAck);
    NekoSystem reference;
    const std::array<IOPWord, 14> words = {{
      0x3c01bf40, // lui r1, CDVD upper
      0x34212000, // ori r1, r1, CDVD lower
      0x3c05bf80, // lui r5, INTC upper
      0x34a51070, // ori r5, r5, INTC lower
      0x34060004, // ori r6, r0, source2
      0xaca60004, // sw r6, I_MASK
      0x34070001, // ori r7, r0, enabled
      0xaca70008, // sw r7, I_CTRL
      0x34060401, // ori r6, r0, IEc | IM2
      0x40866000, // mtc0 r6, Status
      0xa0200004, // sb r0, NCMD
      0x34080001, // ori r8, r0, first return
      0xa0200004, // sb r0, NCMD again
      0x34090001  // ori r9, r0, second return
    }};
    const std::array<IOPWord, 8> handler = {{
      0x26940001, // addiu r20, r20, service count
      0x2415fffb, // addiu r21, r0, ~source2
      firstAck,
      firstAck == 0xa0270008u ? 0xacb50000u : 0xa0270008u,
      0x401a7000, // mfc0 r26, EPC
      0,          // load delay
      0x03400008, // jr r26
      0x42000010  // rfe in branch delay slot
    }};
    installProgram(reference, 0x1000, words);
    installProgram(reference, 0x80, handler);
    reference.iopCore().startExecution(0xa0001000);
    const auto initial = reference.saveState();
    reference.startTrace();
    reference.runMasterCycles(249);
    REQUIRE(reference.iopCore().stopReason() == IOPStopReason::None);
    REQUIRE(reference.iopCore().retiredInstructions() == 30);
    REQUIRE(reference.iopCore().programCounter() == 0xa0001038);
    REQUIRE(reference.iopCore().generalRegister(8) == 1);
    REQUIRE(reference.iopCore().generalRegister(9) == 1);
    REQUIRE(reference.iopCore().generalRegister(20) == 2);
    REQUIRE(reference.iopCore().generalRegister(26) == 0xa0001034);
    REQUIRE((reference.iopCore().status() & 1) == 1);
    REQUIRE(reference.iopBus().read8(BASE + 8).value == 0);
    REQUIRE(reference.iopInterruptController().status() == 0);
    const auto commands = IOPTraceTest::subsystemEvents(reference, CDVD);
    REQUIRE(commands.size() == 4);
    const std::array<std::uint64_t, 4> cycles = {{81, 89, 169, 177}};
    for (std::size_t index = 0; index < commands.size(); ++index)
    {
      REQUIRE(commands[index].masterCycle == cycles[index]);
      REQUIRE(commands[index].type == (index % 2 == 0 ? ACCEPTED : COMPLETED));
    }
    unsigned delivered = 0;
    for (const auto &event : reference.trace())
    {
      if (event.subsystem == NekoTraceSubsystem::IOP &&
          event.type == NekoTraceEventType::InterruptDelivered)
      {
        ++delivered;
        REQUIRE(event.masterCycle == (delivered == 1 ? 89 : 177));
      }
    }
    REQUIRE(delivered == 2);
    for (const std::uint64_t cut : {81, 89, 113, 129, 145, 153, 169, 177})
    {
      CAPTURE(cut);
      NekoSystem source;
      source.loadState(initial);
      source.runMasterCycles(cut);
      if (cut == 113)
      {
        REQUIRE(source.iopBus().read8(BASE + 8).value ==
          (firstAck == 0xa0270008u ? 0 : 1));
        REQUIRE(source.iopInterruptController().status() ==
          (firstAck == 0xa0270008u ? 4 : 0));
      }
      requireContinuationModes(reference, source);
    }
  }
}
