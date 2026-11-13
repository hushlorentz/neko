#include <array>
#include <stdexcept>
#include <string>

#include "catch.hpp"
#include "iop_trace_test_utils.hpp"
#include "neko_system.hpp"
#include "save_state_diagnostics.hpp"
#include "save_state_mutation.hpp"
#include "spu2_input_test_utils.hpp"

struct SPU2VoiceTestAccess
{
  static auto &voice(SPU2Controller &spu, std::size_t index) { return spu.voices.at(index); }
  static bool start(SPU2Controller &spu, std::size_t index) { return spu.startVoice(index); }
  static auto step(SPU2Controller &spu, std::size_t index) { return spu.advanceVoice(index); }
  static void loop(SPU2Controller &spu, std::size_t index, std::uint32_t address)
  {
    spu.writeVoiceLoopAddress(index, address);
  }
  static auto endMask(const SPU2Controller &spu, std::size_t core)
  {
    return spu.voiceEndMasks.at(core);
  }
  static bool valid(const SPU2Controller &spu)
  {
    return spu.hasValidVoiceState() && spu.hasValidCommandState();
  }
  static void sampleEdge(SPU2Controller &spu, std::uint16_t position)
  {
    spu.inputClockPhase = SPU2Controller::INPUT_CLOCK_PERIOD - 1;
    spu.inputReadPosition = position;
  }
};

namespace
{
  void block(SPU2Controller &spu, std::uint32_t address,
             std::uint16_t header, std::uint16_t payload)
  {
    spu.writeSoundRAMHalfword(address, header);
    for (std::uint32_t word = 1; word < 8; ++word)
      spu.writeSoundRAMHalfword((address + word) & 0xfffff, payload);
  }

  void program(SPU2Controller &spu, std::size_t index,
               std::uint32_t address, std::uint16_t pitch)
  {
    REQUIRE(spu.writeVoiceStartAddress(index * 2, address >> 16));
    REQUIRE(spu.writeVoiceStartAddress(index * 2 + 1, address & 0xffff));
    spu.writeVoicePitchControl(index, pitch);
    spu.writeVoiceADSRControl(index * 2, 0x7f00);
  }

  void samples(SPU2Controller &spu, unsigned count)
  {
    SPU2InputTest::advanceSamples(spu, count);
    REQUIRE(SPU2VoiceTestAccess::valid(spu));
  }

  void constantVoice(SPU2Controller &spu, std::size_t index)
  {
    const auto address = static_cast<std::uint32_t>(0x4000 + index * 8);
    block(spu, address, 0, 0x1111);
    program(spu, index, address, 0);
    spu.writeVoiceADSRControl(index * 2 + 1, 0x1fc0);
    REQUIRE(spu.writeVoiceVolumeControl(index * 2, 0x3fff));
    REQUIRE(spu.writeVoiceVolumeControl(index * 2 + 1, 0x3fff));
    REQUIRE(SPU2VoiceTestAccess::start(spu, index));
    SPU2VoiceTestAccess::voice(spu, index).envelope =
      {SPU2Envelope::Phase::Sustain, 0x7fff, 0};
  }
}

TEST_CASE("SPU2 guest commands retain images and start all voices only at the next tick",
          "[iop][spu2][spu2-command]")
{
  NekoSystem system;
  auto &spu = system.spu2Controller();
  for (std::size_t index = 0; index < 48; ++index)
  {
    const auto address = static_cast<std::uint32_t>(0x4000 + index * 8);
    block(spu, address, 0x000c, 0x1111);
    program(spu, index, address, 0);
  }
  for (const auto address : {0x1f9001a0u, 0x1f9001a2u, 0x1f9005a0u, 0x1f9005a2u})
  {
    REQUIRE(system.iopBus().read16(address).value == 0);
    REQUIRE(system.iopBus().write16(address, 0xffff).status == IOPBusStatus::Completed);
    REQUIRE(system.iopBus().read16(address).value == 0xffff);
  }
  system.runMasterCycles(6136);
  for (std::size_t index = 0; index < 48; ++index)
  {
    REQUIRE(SPU2VoiceTestAccess::voice(spu, index).envelope.phase ==
      SPU2Envelope::Phase::Stopped);
    REQUIRE(SPU2VoiceTestAccess::voice(spu, index).nextAddress == 0);
  }
  system.runMasterCycles(1);
  for (std::size_t index = 0; index < 48; ++index)
  {
    const auto &voice = SPU2VoiceTestAccess::voice(spu, index);
    REQUIRE(voice.envelope.phase == SPU2Envelope::Phase::Attack);
    REQUIRE(voice.age == 1);
    REQUIRE(voice.nextAddress == 0x4002 + index * 8);
    REQUIRE(voice.fifoCount == 4);
  }
  for (const auto address : {0x1f9001a0u, 0x1f9001a2u, 0x1f9005a0u, 0x1f9005a2u})
    REQUIRE(system.iopBus().read16(address).value == 0xffff);
  REQUIRE(SPU2VoiceTestAccess::valid(spu));
}

TEST_CASE("SPU2 pending command halves replace independently and zero cancels only pending work",
          "[iop][spu2][spu2-command]")
{
  for (std::size_t core = 0; core < 2; ++core)
  {
    NekoSystem system;
    auto &spu = system.spu2Controller();
    auto &bus = system.iopBus();
    const auto kon = static_cast<IOPAddress>(0x1f9001a0 + core * 0x400);
    for (const auto voice : {0u, 1u, 16u, 23u})
      program(spu, core * 24 + voice, 0x4000, 0);
    REQUIRE(bus.write16(kon, 3).status == IOPBusStatus::Completed);
    REQUIRE(bus.write16(kon + 2, 0xff81).status == IOPBusStatus::Completed);
    REQUIRE(bus.write16(kon, 2).status == IOPBusStatus::Completed);
    REQUIRE(bus.write16(kon + 2, 0xff80).status == IOPBusStatus::Completed);
    samples(spu, 1);
    for (const auto voice : {0u, 1u, 16u, 23u})
      REQUIRE((SPU2VoiceTestAccess::voice(spu, core * 24 + voice).age == 1) ==
        (voice == 1 || voice == 23));
    REQUIRE(bus.read16(kon + 2).value == 0xff80);
    REQUIRE(bus.read16(kon + 0x1a0).value == 0xfffd);
    REQUIRE(bus.read16(kon + 0x1a2).value == 0x7f);
    REQUIRE(bus.write16(kon, 1).status == IOPBusStatus::Completed);
    REQUIRE(bus.write16(kon, 0).status == IOPBusStatus::Completed);
    REQUIRE(bus.write16(kon + 4, 3).status == IOPBusStatus::Completed);
    REQUIRE(bus.write16(kon + 4, 1).status == IOPBusStatus::Completed);
    REQUIRE(bus.write16(kon + 6, 0x80).status == IOPBusStatus::Completed);
    REQUIRE(bus.write16(kon + 6, 0xff00).status == IOPBusStatus::Completed);
    samples(spu, 1);
    REQUIRE(SPU2VoiceTestAccess::voice(spu, core * 24).age == 0);
    REQUIRE(SPU2VoiceTestAccess::voice(spu, core * 24 + 1).age == 2);
    REQUIRE(SPU2VoiceTestAccess::voice(spu, core * 24 + 1).envelope.phase ==
      SPU2Envelope::Phase::Attack);
    REQUIRE(SPU2VoiceTestAccess::voice(spu, core * 24 + 23).age == 2);
    REQUIRE(bus.read16(kon + 6).value == 0xff00);
  }
}

TEST_CASE("SPU2 KOFF restarts release counters and overlapping KON retriggers afterward",
          "[iop][spu2][spu2-command]")
{
  SPU2Controller spu;
  constantVoice(spu, 0);
  samples(spu, 4);
  spu.writeVoiceADSRControl(1, 0x1f);
  auto &voice = SPU2VoiceTestAccess::voice(spu, 0);
  spu.writeKeyOff(0, 1);
  samples(spu, 2);
  REQUIRE(voice.envelope.phase == SPU2Envelope::Phase::Release);
  REQUIRE(voice.envelope.level == 0x7fff);
  REQUIRE(voice.envelope.counter == 2);
  spu.writeKeyOff(0, 1);
  samples(spu, 1);
  REQUIRE(voice.envelope.counter == 1);
  REQUIRE(voice.envelope.level == 0x7fff);
  REQUIRE(spu.writeKeyOn(0, 1));
  spu.writeKeyOff(0, 1);
  samples(spu, 1);
  REQUIRE(voice.envelope.phase == SPU2Envelope::Phase::Attack);
  REQUIRE(voice.envelope.level == 0);
  REQUIRE(voice.envelope.counter == 1);
  REQUIRE(voice.age == 1);
  REQUIRE(voice.fraction == 0);
  REQUIRE(voice.nextAddress == 0x4002);
  REQUIRE(voice.fifoCount == 4);
  samples(spu, 1);
  REQUIRE(voice.age == 2);
  REQUIRE(voice.envelope.counter == 2);
  REQUIRE(spu.readKeyOn(0) == 1);
  REQUIRE(spu.readKeyOff(0) == 1);
}

TEST_CASE("SPU2 KON admission and pending mode guards are atomic",
          "[iop][spu2][spu2-command]")
{
  NekoSystem system;
  auto &spu = system.spu2Controller();
  auto &bus = system.iopBus();
  REQUIRE(spu.writeVoiceModeMask(2, 2));
  const auto before = system.iopStateHash();
  REQUIRE(bus.write16(0x1f9001a0, 3).status == IOPBusStatus::Unmapped);
  REQUIRE(system.iopStateHash() == before);
  REQUIRE(bus.read16(0x1f9001a0).value == 0);
  samples(spu, 1);
  REQUIRE(SPU2VoiceTestAccess::voice(spu, 0).age == 0);
  REQUIRE(spu.writeVoiceModeMask(2, 0));
  REQUIRE(spu.writeVoiceModeMask(0, 1)); // Voice zero ignores PMON.
  REQUIRE(bus.write16(0x1f9001a0, 1).status == IOPBusStatus::Completed);
  const auto pending = system.iopStateHash();
  REQUIRE(bus.write16(0x1f9001c0, 1).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.write16(0x1f900184, 1).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.read16(0x1f900344).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.read16(0x1f900744).status == IOPBusStatus::Unmapped);
  REQUIRE(system.iopStateHash() == pending);
  REQUIRE(spu.writeKeyOn(0, 0));
  REQUIRE(spu.writeVoiceStartAddress(0, 1));
  REQUIRE(spu.writeVoiceModeMask(2, 1));
  REQUIRE(spu.writeVoiceModeMask(1, 0xff80));
  REQUIRE_FALSE(spu.writeKeyOn(1, 0xff80));
  REQUIRE(spu.readKeyOn(1) == 0);
  REQUIRE(spu.writeKeyOn(1, 0xff00));
  REQUIRE(spu.readKeyOn(1) == 0xff00);
  REQUIRE(SPU2VoiceTestAccess::valid(spu));
}

TEST_CASE("SPU2 live register halfwords qualify LSAX NAX ENVX and read-only ENDX",
          "[iop][spu2][spu2-command]")
{
  for (const auto index : {0u, 23u, 24u, 47u})
  {
    NekoSystem system;
    auto &spu = system.spu2Controller();
    auto &bus = system.iopBus();
    const auto core = index / 24;
    const auto voice = index % 24;
    const auto base = 0x1f9001c0 + core * 0x400 + voice * 12;
    const auto envx = 0x1f90000a + core * 0x400 + voice * 16;
    block(spu, 0xf1238, 0, 0x1111);
    program(spu, index, 0xf1238, 0);
    spu.writeVoiceADSRControl(index * 2, 0);
    REQUIRE(bus.write16(base + 4, 0xfff1).status == IOPBusStatus::Completed);
    REQUIRE(bus.write16(base + 6, 0x234f).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(base + 4).value == 1);
    REQUIRE(bus.read16(base + 6).value == 0x2348);
    REQUIRE(bus.read16(base + 8).value == 0);
    REQUIRE(spu.writeKeyOn(core * 2 + voice / 16, 1u << (voice % 16)));
    samples(spu, 1);
    for (unsigned age = 1; age < 4; ++age)
    {
      REQUIRE(bus.read16(base + 8).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.read16(base + 10).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write16(base + 4, 3).status == IOPBusStatus::Completed);
      REQUIRE(bus.write16(base + 6, 0xfff7).status == IOPBusStatus::Completed);
      REQUIRE(bus.read16(base + 4).value == 1);
      REQUIRE(bus.read16(base + 6).value == 0x2348);
      samples(spu, 1);
    }
    REQUIRE(bus.read16(base + 8).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(base + 8).value == 0xf);
    REQUIRE(bus.read16(base + 10).value == 0x123d);
    REQUIRE(bus.read16(envx).value == SPU2VoiceTestAccess::voice(spu, index).envelope.level);
    REQUIRE(bus.read16(envx).value > 0);
    REQUIRE(bus.write16(base + 4, 3).status == IOPBusStatus::Completed);
    REQUIRE(bus.write16(base + 6, 0xfff7).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(base + 4).value == 3);
    REQUIRE(bus.read16(base + 6).value == 0xfff0);
    REQUIRE(SPU2VoiceTestAccess::voice(spu, index).loopOverride);
    for (const auto address : {base + 8, base + 10, envx,
                              0x1f900340 + core * 0x400,
                              0x1f900342 + core * 0x400})
      REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Unmapped);
  }
}

TEST_CASE("SPU2 pending commands and images restore with canonical traced and plain continuation",
          "[iop][spu2][spu2-command][save-state]")
{
  NekoSystem source;
  auto &spu = source.spu2Controller();
  for (const auto index : {0u, 1u, 47u})
  {
    block(spu, 0x4000 + index * 8, 0x070c, 0x1111);
    program(spu, index, 0x4000 + index * 8, 0x1000);
  }
  source.runMasterCycles(6136);
  REQUIRE(spu.writeKeyOn(0, 3));
  REQUIRE(spu.writeKeyOn(3, 0xff80));
  spu.writeKeyOff(0, 3);
  spu.writeKeyOff(3, 0xff80);
  const auto saved = source.saveState();
  NekoSystem restored;
  NekoSystem plain;
  restored.loadState(saved);
  plain.loadState(saved);
  REQUIRE(restored.saveState() == saved);
  source.startTrace();
  restored.startTrace();
  source.runMasterCycles(50000);
  restored.runMasterCycles(50000);
  plain.runMasterCycles(50000);
  IOPTraceTest::requireEvents(source.trace(), restored.trace());
  REQUIRE(source.saveState() == restored.saveState());
  REQUIRE(source.saveState() == plain.saveState());
  REQUIRE(source.iopStateHash() == plain.iopStateHash());
  const auto commands = IOPTraceTest::subsystemEvents(source, NekoTraceSubsystem::SPU2);
  REQUIRE(commands.size() >= 7);
  REQUIRE(commands.front().type == NekoTraceEventType::CommandCompleted);
  NekoSystem destination;
  const auto before = destination.saveState();
  struct Invalid { const char *path; std::uint64_t value; };
  for (const auto invalid : {
    Invalid{"commands[0].pendingKeyOn", 1},
    Invalid{"commands[0].pendingKeyOn", 0x1000000},
    Invalid{"commands[0].keyOnLow", 1},
    Invalid{"commands[1].pendingKeyOff", 0x400000},
    Invalid{"voiceModeMasks[2].value", 2}})
  {
    INFO(invalid.path);
    SaveStateMutation mutation(saved);
    mutation.writeScalar(std::string("system.iop.spu2.") + invalid.path, invalid.value);
    mutation.refreshChecksum();
    REQUIRE_THROWS_AS(destination.loadState(mutation), std::invalid_argument);
    REQUIRE(destination.saveState() == before);
  }
  auto previous = saved;
  previous[8] = 117;
  REQUIRE_THROWS_AS(destination.loadState(previous), std::invalid_argument);
  REQUIRE(destination.saveState() == before);
}

TEST_CASE("SPU2 CPU command store on a sample pulse waits until the following tick",
          "[iop][spu2][spu2-command]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  auto &spu = system.spu2Controller();
  program(spu, 0, 0x4000, 0);
  REQUIRE(bus.write32(0, 0x3c011f90).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(4, 0x24020001).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(8, 0xa42201a0).status == IOPBusStatus::Completed);
  system.iopCore().startExecution(0xa0000000);
  system.runMasterCycles(9);
  REQUIRE(system.iopCore().generalRegister(1) == 0x1f900000);
  REQUIRE(system.iopCore().generalRegister(2) == 1);
  system.iopCore().haltExecution();
  system.runMasterCycles(6127);
  system.iopCore().startExecution(0xa0000008);
  system.startTrace();
  system.runMasterCycles(1);
  REQUIRE(bus.read16(0x1f9001a0).value == 1);
  REQUIRE(SPU2VoiceTestAccess::voice(spu, 0).age == 0);
  system.runMasterCycles(6143);
  REQUIRE(SPU2VoiceTestAccess::voice(spu, 0).age == 0);
  system.runMasterCycles(1);
  REQUIRE(SPU2VoiceTestAccess::voice(spu, 0).age == 1);
  const auto events = IOPTraceTest::subsystemEvents(system, NekoTraceSubsystem::SPU2);
  REQUIRE(events.size() == 2);
  REQUIRE(events[0].masterCycle == 12281);
  REQUIRE(events[0].type == NekoTraceEventType::CommandCompleted);
}

TEST_CASE("SPU2 simultaneous lifecycle observations preserve all voices input edges and failure probes",
          "[iop][spu2][spu2-command][save-state]")
{
  for (const bool retrigger : {false, true})
  {
    CAPTURE(retrigger);
    NekoSystem source;
    auto &spu = source.spu2Controller();
    SPU2InputTest::programProfile(spu);
    for (std::size_t core = 0; core < 2; ++core)
      REQUIRE(spu.writeAutoDMAWriteSelection(core, 1u << core));
    for (std::size_t index = 0; index < 48; ++index)
      constantVoice(spu, index);
    samples(spu, 4);
    for (std::size_t index = 0; index < 48; ++index)
    {
      if (!retrigger)
      {
        auto &voice = SPU2VoiceTestAccess::voice(spu, index);
        voice.envelope = {SPU2Envelope::Phase::Sustain, 1, 0};
        voice.nextAddress = 0x4007 + static_cast<std::uint32_t>(index) * 8;
        voice.loopAddress = voice.nextAddress & 0xffff8;
        voice.header = 0x0300;
        voice.headerValid = true;
        voice.fifo.fill(0);
        voice.fifoHead = 0;
        voice.fifoCount = 0;
        spu.writeVoiceADSRControl(index * 2 + 1, 0);
      }
    }
    for (std::size_t index = 0; index < 4; ++index)
    {
      spu.writeKeyOff(index, 0xffff);
      if (retrigger)
        REQUIRE(spu.writeKeyOn(index, 0xffff));
    }
    SPU2VoiceTestAccess::sampleEdge(spu, 255);
    REQUIRE(SPU2VoiceTestAccess::valid(spu));
    REQUIRE(source.iopBus().write32(0, 0xa7a001ae).status == IOPBusStatus::Completed);
    source.iopCore().prepareFreshExecution(0xa0000000, 0x1f900000, 0);
    source.iopCore().startExecution(0xa0000000);
    const auto saved = source.saveState();
    source.startTrace();
    source.runMasterCycles(1);
    REQUIRE(source.iopCore().stopReason() == IOPStopReason::StoreFailure);
    const auto events = IOPTraceTest::subsystemEvents(source, NekoTraceSubsystem::SPU2);
    REQUIRE(events.size() == (retrigger ? 102 : 148));
    std::array<unsigned, 5> reasons{};
    unsigned commands = 0;
    unsigned inputs = 0;
    for (const auto &event : events)
    {
      if (event.type == NekoTraceEventType::CommandCompleted)
      {
        const auto command = nekoDecodeSPUCommand(event);
        REQUIRE(command.mask == 0xffffff);
        REQUIRE(command.samplePosition == 255);
        ++commands;
      }
      else if ((event.value0 & NekoSPUTrace::VOICE_TRANSITION) != 0)
      {
        const auto transition = nekoDecodeSPUVoice(event);
        REQUIRE(transition.samplePosition == 255);
        ++reasons[static_cast<std::uint8_t>(transition.reason)];
      }
      else
      {
        const auto input = nekoDecodeSPUInput(event);
        REQUIRE(input.before.readHalf == 0);
        REQUIRE(input.after.readHalf == 1);
        ++inputs;
      }
    }
    REQUIRE(commands == (retrigger ? 4 : 2));
    REQUIRE(inputs == 2);
    REQUIRE(reasons[0] == (retrigger ? 48 : 0));
    REQUIRE(reasons[1] == 48);
    REQUIRE(reasons[2] == (retrigger ? 0 : 48));
    REQUIRE(reasons[3] == 0);
    REQUIRE(reasons[4] == (retrigger ? 0 : 48));
    REQUIRE(nekoDecodeSPUCommand(events[0]).kind == NekoSPUTrace::CommandKind::KeyOff);
    if (retrigger)
      REQUIRE(nekoDecodeSPUCommand(events[25]).kind == NekoSPUTrace::CommandKind::KeyOn);
    const auto firstFailure = [](const NekoSystem &system)
    {
      IOPTraceTest::Events failures;
      for (const auto &event : system.trace())
        if (nekoTraceFailureKind(event) != NekoTraceFailureKind::None)
        {
          failures.push_back(event);
          break;
        }
      return failures;
    };
    const auto expectedFailure = firstFailure(source);
    REQUIRE(expectedFailure.size() == 1);
    for (const bool probe : {false, true})
    {
      NekoSystem restored;
      restored.loadState(saved);
      if (probe)
        restored.startFailureProbe();
      restored.runMasterCycles(1);
      IOPTraceTest::requireMachine(restored, source);
      if (probe)
      {
        REQUIRE(IOPTraceTest::subsystemEvents(restored, NekoTraceSubsystem::SPU2).empty());
        IOPTraceTest::requireEvents(firstFailure(restored), expectedFailure);
      }
    }
  }
}

TEST_CASE("SPU2 live and command registers reject other widths and transports without mutation",
          "[iop][spu2][spu2-command]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  const auto before = system.iopStateHash();
  for (const auto address : {
    0x1f9001a0u, 0x1f9001a2u, 0x1f9001a4u, 0x1f9001a6u,
    0x1f9005a0u, 0x1f9005a2u, 0x1f9005a4u, 0x1f9005a6u,
    0x1f9001c4u, 0x1f9002d8u, 0x1f9005c4u, 0x1f9006d8u,
    0x1f9001c8u, 0x1f9002dcu, 0x1f9005c8u, 0x1f9006dcu,
    0x1f90000au, 0x1f90017au, 0x1f90040au, 0x1f90057au,
    0x1f900340u, 0x1f900342u, 0x1f900740u, 0x1f900742u})
  {
    CAPTURE(address);
    REQUIRE(bus.read16(address).status == IOPBusStatus::Completed);
    for (const auto byte : {address, address + 1})
    {
      REQUIRE(bus.read8(byte).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write8(byte, 0xff).status == IOPBusStatus::Unmapped);
    }
    const auto word = address & ~IOPAddress{3};
    REQUIRE(bus.read32(word).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(word, 0xffffffff).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.readInstruction32(word).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.readDMAC32(word).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.writeDMAC32(word, 0xffffffff).status == IOPBusStatus::Unmapped);
    for (std::uint8_t mask = 1; mask <= 0xf; ++mask)
      REQUIRE(bus.writeMasked32(word, 0xffffffff, mask).status == IOPBusStatus::Unmapped);
    std::array<std::uint8_t, 2> bytes{{0x12, 0x34}};
    REQUIRE(bus.loadPhysical(address, bytes.data(), bytes.size()) == IOPBusStatus::Unmapped);
    REQUIRE(bus.inspectPhysical(address, bytes.data(), bytes.size()) == IOPBusStatus::Unmapped);
    for (const auto segment : {0x80000000u, 0xa0000000u})
    {
      REQUIRE(bus.read16(address | segment).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write16(address | segment, 0xffff).status == IOPBusStatus::Unmapped);
    }
    std::uint16_t value = 0x4321;
    REQUIRE_FALSE(system.eeBus().readData16(address, &value));
    REQUIRE(value == 0x4321);
    REQUIRE_FALSE(system.eeBus().writeData16(address, 0xffff));
  }
  REQUIRE(system.iopStateHash() == before);
}

TEST_CASE("SPU2 lifecycle traces omit level ticks and distinguish release from source termination",
          "[iop][spu2][spu2-command]")
{
  NekoSystem system;
  auto &spu = system.spu2Controller();
  constantVoice(spu, 0);
  samples(spu, 4);
  auto &voice = SPU2VoiceTestAccess::voice(spu, 0);
  SECTION("Envelope phases and repeated release are sparse")
  {
    voice.envelope = {SPU2Envelope::Phase::Attack, 0, 0};
    spu.writeVoiceADSRControl(0, 0);
    system.startTrace();
    system.runMasterCycles(8 * 6144);
    auto events = IOPTraceTest::subsystemEvents(system, NekoTraceSubsystem::SPU2);
    REQUIRE(events.size() == 2);
    for (const auto &event : events)
      REQUIRE(nekoDecodeSPUVoice(event).reason == NekoSPUTrace::VoiceReason::EnvelopePhase);
    REQUIRE(nekoDecodeSPUVoice(events[0]).afterPhase == 2);
    REQUIRE(nekoDecodeSPUVoice(events[1]).afterPhase == 3);
    system.clearTrace();
    spu.writeVoiceADSRControl(1, 0x1f);
    spu.writeKeyOff(0, 1);
    system.runMasterCycles(2 * 6144);
    spu.writeKeyOff(0, 1);
    system.runMasterCycles(6144);
    events = IOPTraceTest::subsystemEvents(system, NekoTraceSubsystem::SPU2);
    REQUIRE(events.size() == 4);
    const auto release = nekoDecodeSPUVoice(events[3]);
    REQUIRE(release.reason == NekoSPUTrace::VoiceReason::KeyOff);
    REQUIRE(release.beforePhase == 4);
    REQUIRE(release.afterPhase == 4);
    REQUIRE(release.beforeLevel == release.afterLevel);
  }
  SECTION("A nonrepeat endpoint emits only the source stop")
  {
    voice.nextAddress = 0x4007;
    voice.header = 0x0100;
    voice.headerValid = true;
    voice.fifo.fill(0);
    voice.fifoHead = 0;
    voice.fifoCount = 0;
    system.startTrace();
    system.runMasterCycles(2 * 6144);
    const auto events = IOPTraceTest::subsystemEvents(system, NekoTraceSubsystem::SPU2);
    REQUIRE(events.size() == 1);
    const auto endpoint = nekoDecodeSPUVoice(events[0]);
    REQUIRE(endpoint.reason == NekoSPUTrace::VoiceReason::Endpoint);
    REQUIRE(endpoint.beforePhase == 3);
    REQUIRE(endpoint.afterPhase == 0);
    REQUIRE(endpoint.afterLevel == 0);
    REQUIRE(SPU2VoiceTestAccess::valid(spu));
  }
}

TEST_CASE("SPU2 waveform source reset state is canonical",
          "[iop][spu2][spu2-source][save-state]")
{
  NekoSystem system;
  const auto snapshot = inspectSaveState(system.saveState());
  for (std::size_t index = 0; index < 48; ++index)
  {
    const auto path = "system.iop.spu2.voices[" + std::to_string(index) + "].";
    const auto *phase = snapshot.find(path + "envelopePhase");
    REQUIRE(phase != nullptr);
    REQUIRE(phase->scalarValue == 0);
    REQUIRE(snapshot.find(path + "nextAddress")->scalarValue == 0);
    REQUIRE(snapshot.find(path + "fifoCount")->scalarValue == 0);
  }
  for (const auto address : {0x1f9001a0u, 0x1f9001a2u, 0x1f9005a0u, 0x1f9005a2u})
  {
    REQUIRE(system.iopBus().write16(address, 0).status == IOPBusStatus::Completed);
    REQUIRE(system.iopBus().read16(address).status == IOPBusStatus::Completed);
  }
  for (std::size_t core = 0; core < 2; ++core)
    REQUIRE(SPU2VoiceTestAccess::endMask(system.spu2Controller(), core) == 0xffffff);
}

TEST_CASE("SPU2 waveform sources use the common sample clock for all muted voices",
          "[iop][spu2][spu2-source]")
{
  NekoSystem system;
  auto &spu = system.spu2Controller();
  for (std::size_t index = 0; index < 48; ++index)
  {
    const auto address = static_cast<std::uint32_t>(0x4000 + index * 8);
    block(spu, address, 0x000c, 0x1111);
    program(spu, index, address, 0);
    REQUIRE(SPU2VoiceTestAccess::start(spu, index));
  }
  system.runMasterCycles(6136);
  for (std::size_t index = 0; index < 48; ++index)
  {
    const auto &voice = SPU2VoiceTestAccess::voice(spu, index);
    REQUIRE(voice.age == 0);
    REQUIRE(voice.nextAddress == 0x4001 + index * 8);
  }
  system.runMasterCycles(1);
  for (std::size_t index = 0; index < 48; ++index)
  {
    const auto &voice = SPU2VoiceTestAccess::voice(spu, index);
    REQUIRE(voice.age == 1);
    REQUIRE(voice.nextAddress == 0x4002 + index * 8);
    REQUIRE(voice.fifoCount == 4);
    REQUIRE(voice.envelope.level == 0);
    REQUIRE(voice.history1 == 1);
  }
  REQUIRE(SPU2VoiceTestAccess::endMask(spu, 0) == 0);
  REQUIRE(SPU2VoiceTestAccess::endMask(spu, 1) == 0);
  REQUIRE(SPU2VoiceTestAccess::valid(spu));
  system.reset();
  REQUIRE(SPU2VoiceTestAccess::valid(spu));
  REQUIRE(SPU2VoiceTestAccess::voice(spu, 47).nextAddress == 0);
  REQUIRE(SPU2VoiceTestAccess::endMask(spu, 1) == 0xffffff);
}

TEST_CASE("SPU2 waveform refills are incremental with one latched header per block",
          "[iop][spu2][spu2-source]")
{
  SPU2Controller spu;
  block(spu, 0x4000, 0x000c, 0x1111);
  program(spu, 0, 0x4000, 0);
  REQUIRE(SPU2VoiceTestAccess::start(spu, 0));
  samples(spu, 1);
  auto &voice = SPU2VoiceTestAccess::voice(spu, 0);
  REQUIRE(voice.fifoCount == 4);
  REQUIRE(voice.fifo[0] == 1);
  spu.writeSoundRAMHalfword(0x4000, 0x0100);
  spu.writeSoundRAMHalfword(0x4002, 0x2222);
  samples(spu, 1);
  REQUIRE(voice.header == 0x000c);
  REQUIRE(voice.fifo[0] == 1);
  REQUIRE(voice.fifo[4] == 2);
  REQUIRE(voice.nextAddress == 0x4003);
  samples(spu, 2);
  REQUIRE(voice.fifoCount == 16);
  REQUIRE(voice.nextAddress == 0x4005);
  samples(spu, 20);
  REQUIRE(voice.fifoCount == 16);
  REQUIRE(voice.nextAddress == 0x4005);
  spu.writeVoicePitchControl(0, 0xffff);
  samples(spu, 1);
  REQUIRE(voice.nextAddress == 0x4005);
  REQUIRE(voice.fifoCount == 13);
  REQUIRE(voice.fraction == 0xfff);
  samples(spu, 1);
  REQUIRE(voice.nextAddress == 0x4005);
  REQUIRE(voice.fifoCount == 9);
  samples(spu, 1);
  REQUIRE(voice.nextAddress == 0x4006);
  REQUIRE(voice.fifoCount == 9);
  REQUIRE(voice.envelope.phase != SPU2Envelope::Phase::Stopped);
}

TEST_CASE("SPU2 waveform renders before pitch consumption using this tick envelope",
          "[iop][spu2][spu2-source]")
{
  SPU2Controller spu;
  block(spu, 0x4000, 0, 0x8f17);
  program(spu, 0, 0x4000, 0xffff);
  spu.writeVoiceADSRControl(0, 0);
  REQUIRE(SPU2VoiceTestAccess::start(spu, 0));
  REQUIRE(SPU2VoiceTestAccess::step(spu, 0) == 2829);
  const auto &voice = SPU2VoiceTestAccess::voice(spu, 0);
  REQUIRE(voice.fifoCount == 1);
  REQUIRE(voice.fifo[voice.fifoHead] == -32768);
  REQUIRE(voice.fraction == 0xfff);
  REQUIRE(voice.envelope.level == 14336);
  REQUIRE(SPU2VoiceTestAccess::valid(spu));
}

TEST_CASE("SPU2 waveform endpoints act at final refill and header byte positions matter",
          "[iop][spu2][spu2-source]")
{
  for (const auto header : {0x0001u, 0x0100u, 0x0300u})
  {
    INFO(header);
    SPU2Controller spu;
    block(spu, 0x4000, header, 0x1111);
    program(spu, 0, 0x4000, 0x3fff);
    SPU2VoiceTestAccess::loop(spu, 0, 0x5000);
    REQUIRE(SPU2VoiceTestAccess::start(spu, 0));
    samples(spu, 6);
    REQUIRE((SPU2VoiceTestAccess::endMask(spu, 0) & 1) == 0);
    auto &voice = SPU2VoiceTestAccess::voice(spu, 0);
    REQUIRE(voice.nextAddress == 0x4007);
    samples(spu, 1);
    REQUIRE_FALSE(voice.headerValid);
    REQUIRE(voice.header == 0);
    if (header == 0x0001)
    {
      REQUIRE(voice.nextAddress == 0x4009);
      REQUIRE((SPU2VoiceTestAccess::endMask(spu, 0) & 1) == 0);
      REQUIRE(voice.envelope.phase == SPU2Envelope::Phase::Attack);
    }
    else
    {
      REQUIRE(voice.nextAddress == 0x5001);
      REQUIRE((SPU2VoiceTestAccess::endMask(spu, 0) & 1) == 1);
      REQUIRE(voice.envelope.phase == (header == 0x0100 ?
        SPU2Envelope::Phase::Stopped : SPU2Envelope::Phase::Attack));
      REQUIRE(voice.fifoCount == (header == 0x0100 ? 0 : 1));
    }
  }
}

TEST_CASE("SPU2 waveform loop overrides qualify at four ticks and clear on retrigger",
          "[iop][spu2][spu2-source]")
{
  SPU2Controller spu;
  block(spu, 0x4000, 0x040c, 0x1111);
  block(spu, 0x4008, 0x040c, 0x2222);
  program(spu, 0, 0x4000, 0x3fff);
  SPU2VoiceTestAccess::loop(spu, 0, 0x5000);
  REQUIRE(SPU2VoiceTestAccess::start(spu, 0));
  auto &voice = SPU2VoiceTestAccess::voice(spu, 0);
  REQUIRE(voice.loopAddress == 0x5000);
  REQUIRE_FALSE(voice.loopOverride);
  samples(spu, 3);
  REQUIRE(voice.loopAddress == 0x4000);
  SPU2VoiceTestAccess::loop(spu, 0, 0x54321);
  REQUIRE(voice.loopAddress == 0x4000);
  REQUIRE_FALSE(voice.loopOverride);
  samples(spu, 1);
  SPU2VoiceTestAccess::loop(spu, 0, 0x54321);
  REQUIRE(voice.loopAddress == 0x54320);
  REQUIRE(voice.loopOverride);
  samples(spu, 4);
  REQUIRE(voice.nextAddress == 0x400a);
  REQUIRE(voice.loopAddress == 0x54320);
  REQUIRE(SPU2VoiceTestAccess::start(spu, 0));
  REQUIRE(voice.loopAddress == 0x54320);
  REQUIRE_FALSE(voice.loopOverride);
  REQUIRE(voice.fifoCount == 0);
  REQUIRE(voice.history1 == 0);
  REQUIRE(voice.fraction == 0);
  samples(spu, 1);
  REQUIRE(voice.loopAddress == 0x4000);
}

TEST_CASE("SPU2 waveform headers refetch on self loops and addresses wrap sound RAM",
          "[iop][spu2][spu2-source]")
{
  SPU2Controller spu;
  block(spu, 0xffff8, 0x070c, 0x1111);
  program(spu, 47, 0xffff8, 0x3fff);
  REQUIRE(SPU2VoiceTestAccess::start(spu, 47));
  samples(spu, 1);
  spu.writeSoundRAMHalfword(0xffff8, 0x000c);
  samples(spu, 6);
  auto &voice = SPU2VoiceTestAccess::voice(spu, 47);
  REQUIRE(voice.nextAddress == 0xffff9);
  REQUIRE_FALSE(voice.headerValid);
  samples(spu, 1);
  REQUIRE(voice.header == 0x000c);
  samples(spu, 6);
  REQUIRE(voice.nextAddress == 1);
  block(spu, 0, 0x040c, 0x2222);
  samples(spu, 1);
  REQUIRE(voice.nextAddress == 2);
  REQUIRE(voice.loopAddress == 0);
  REQUIRE(voice.history1 == 2);
  REQUIRE(SPU2VoiceTestAccess::endMask(spu, 1) == 0xffffff);
}

TEST_CASE("SPU2 waveform live controls cannot fabricate idle or unsupported execution",
          "[iop][spu2][spu2-source]")
{
  NekoSystem system;
  auto &spu = system.spu2Controller();
  auto &bus = system.iopBus();
  for (std::size_t index = 0; index < 2; ++index)
  {
    program(spu, index, 0x4000, 0);
    REQUIRE(SPU2VoiceTestAccess::start(spu, index));
  }
  REQUIRE(bus.write16(0x1f9001c0, 1).status != IOPBusStatus::Completed);
  REQUIRE(spu.readVoiceStartAddress(0) == 0);
  REQUIRE(bus.read16(0x1f900344).status != IOPBusStatus::Completed);
  REQUIRE(bus.read16(0x1f900744).status != IOPBusStatus::Completed);
  REQUIRE(bus.write16(0x1f9001a4, 1).status == IOPBusStatus::Completed);
  REQUIRE(bus.write16(0x1f9001a4, 0).status == IOPBusStatus::Completed);
  REQUIRE(spu.writeVoiceModeMask(0, 1));
  REQUIRE_FALSE(spu.writeVoiceModeMask(0, 3));
  REQUIRE(spu.readVoiceModeMask(0) == 1);
  REQUIRE_FALSE(spu.writeVoiceModeMask(2, 1));
  REQUIRE(spu.readVoiceModeMask(2) == 0);
  REQUIRE(spu.writeVoiceModeMask(2, 4));
  REQUIRE_FALSE(SPU2VoiceTestAccess::start(spu, 2));
  REQUIRE(SPU2VoiceTestAccess::valid(spu));

  auto &voice = SPU2VoiceTestAccess::voice(spu, 0);
  voice.envelope = {SPU2Envelope::Phase::Attack, 100, 0x7fff};
  spu.writeVoiceADSRControl(0, 0x7f00);
  REQUIRE(voice.envelope.counter == 0);
  REQUIRE(voice.envelope.level == 100);
  samples(spu, 1);
  REQUIRE(voice.envelope.counter == 1);
  REQUIRE(voice.envelope.level == 100);
  SPU2Envelope::keyOff(voice.envelope);
  spu.writeVoiceADSRControl(1, 0);
  samples(spu, 1);
  REQUIRE(voice.envelope.phase == SPU2Envelope::Phase::Stopped);
  REQUIRE(voice.fifoCount == 0);
  REQUIRE_FALSE(voice.headerValid);
}

TEST_CASE("SPU2 waveform canonical state restores buffered history and fractional execution",
          "[iop][spu2][spu2-source][save-state]")
{
  NekoSystem source;
  auto &spu = source.spu2Controller();
  for (const auto index : {0u, 23u, 24u, 47u})
  {
    const auto address = 0x4000 + index * 8;
    block(spu, address, 0x071c, 0x8f17);
    program(spu, index, address, 0x0fff);
    REQUIRE(SPU2VoiceTestAccess::start(spu, index));
  }
  source.runMasterCycles(23000);
  const auto saved = source.saveState();
  NekoSystem restored;
  NekoSystem plain;
  restored.loadState(saved);
  plain.loadState(saved);
  REQUIRE(restored.saveState() == saved);
  source.startTrace();
  restored.startTrace();
  source.runMasterCycles(160000);
  restored.runMasterCycles(160000);
  plain.runMasterCycles(160000);
  IOPTraceTest::requireEvents(source.trace(), restored.trace());
  REQUIRE(source.iopStateHash() == restored.iopStateHash());
  REQUIRE(source.iopStateHash() == plain.iopStateHash());
  REQUIRE(source.saveState() == restored.saveState());
  REQUIRE(source.saveState() == plain.saveState());
  REQUIRE(SPU2VoiceTestAccess::valid(source.spu2Controller()));
  restored.spu2Controller().writeSoundRAMHalfword(0x4001, 0x7777);
  REQUIRE(source.spu2Controller().readSoundRAMHalfword(0x4001) == 0x8f17);
}

TEST_CASE("SPU2 waveform malformed continuation rejects transactionally",
          "[iop][spu2][spu2-source][save-state]")
{
  NekoSystem source;
  auto &spu = source.spu2Controller();
  block(spu, 0x4000, 0x000c, 0x1111);
  program(spu, 0, 0x4000, 0);
  REQUIRE(SPU2VoiceTestAccess::start(spu, 0));
  samples(spu, 1);
  const auto saved = source.saveState();
  NekoSystem destination;
  destination.spu2Controller().writeSoundRAMHalfword(0x7000, 0xbeef);
  const auto before = destination.saveState();
  const auto beforeHash = destination.iopStateHash();
  struct Invalid
  {
    const char *path;
    std::uint64_t value;
  };
  for (const auto invalid : {
    Invalid{"voices[0].envelopePhase", 5},
    Invalid{"voices[0].envelopePhase", 0},
    Invalid{"voices[0].envelopeLevel", 0x8000},
    Invalid{"voices[0].envelopeCounter", 0x8000},
    Invalid{"voices[0].age", 5},
    Invalid{"voices[0].age", 0},
    Invalid{"voices[0].fraction", 0x1000},
    Invalid{"voices[0].nextAddress", 0x100000},
    Invalid{"voices[0].nextAddress", 0x4000},
    Invalid{"voices[0].nextAddress", 0x4001},
    Invalid{"voices[0].nextAddress", 0x4003},
    Invalid{"voices[0].loopAddress", 0x4001},
    Invalid{"voices[0].loopAddress", 0x100000},
    Invalid{"voices[0].headerValid", 0},
    Invalid{"voices[0].fifoHead", 32},
    Invalid{"voices[0].fifoCount", 17},
    Invalid{"voices[0].fifo[31].value", 1},
    Invalid{"voices[1].envelopeLevel", 1},
    Invalid{"voiceEndMasks[0].value", 0x1000000},
    Invalid{"voiceModeMasks[2].value", 1}})
  {
    INFO(invalid.path);
    SaveStateMutation mutation(saved);
    mutation.writeScalar(std::string("system.iop.spu2.") + invalid.path, invalid.value);
    mutation.refreshChecksum();
    REQUIRE_THROWS_AS(destination.loadState(mutation), std::invalid_argument);
    REQUIRE(destination.iopStateHash() == beforeHash);
    REQUIRE(destination.saveState() == before);
  }
}

TEST_CASE("SPU2 waveform missing mid-block header rejects after startup age saturates",
          "[iop][spu2][spu2-source][save-state]")
{
  NekoSystem source;
  auto &spu = source.spu2Controller();
  block(spu, 0x4000, 0, 0x1111);
  program(spu, 0, 0x4000, 0);
  REQUIRE(SPU2VoiceTestAccess::start(spu, 0));
  samples(spu, 4);
  const auto &voice = SPU2VoiceTestAccess::voice(spu, 0);
  REQUIRE(voice.age == 4);
  REQUIRE(voice.nextAddress == 0x4005);
  REQUIRE(voice.header == 0);
  REQUIRE(voice.headerValid);
  spu.writeSoundRAMHalfword(0x4000, 0x0100);
  SaveStateMutation invalid(source.saveState());
  invalid.writeScalar("system.iop.spu2.voices[0].headerValid", 0);
  invalid.refreshChecksum();
  NekoSystem destination;
  const auto before = destination.saveState();
  REQUIRE_THROWS_AS(destination.loadState(invalid), std::invalid_argument);
  REQUIRE(destination.saveState() == before);
}

TEST_CASE("SPU2 capture rings write stopped zeros from reset and wrap together",
          "[iop][spu2][spu2-mix]")
{
  SPU2Controller spu;
  const std::array<std::uint32_t, 14> bases = {{
    0x400, 0x600, 0x800, 0xa00, 0xc00, 0xe00,
    0x1000, 0x1200, 0x1400, 0x1600, 0x1800, 0x1a00, 0x1c00, 0x1e00
  }};
  for (const auto base : bases)
  {
    spu.writeSoundRAMHalfword(base, 0xaaaa);
    spu.writeSoundRAMHalfword(base + 511, 0xbbbb);
  }
  samples(spu, 511);
  for (const auto base : bases)
  {
    REQUIRE(spu.readSoundRAMHalfword(base) == 0);
    REQUIRE(spu.readSoundRAMHalfword(base + 511) == 0xbbbb);
  }
  samples(spu, 1);
  for (const auto base : bases)
  {
    REQUIRE(spu.readSoundRAMHalfword(base + 511) == 0);
    spu.writeSoundRAMHalfword(base, 0xcccc);
  }
  samples(spu, 1);
  for (const auto base : bases)
    REQUIRE(spu.readSoundRAMHalfword(base) == 0);
}

TEST_CASE("SPU2 voice captures precede gain routing and final output mute",
          "[iop][spu2][spu2-mix]")
{
  SPU2Controller spu;
  constantVoice(spu, 1);
  REQUIRE(spu.writeVoiceVolumeControl(3, 0x4000));
  for (const auto lane : {0u, 2u, 4u, 6u})
    spu.writeVoiceMix(lane, 2);
  spu.writeMainMix(0, 0xfc0f);
  REQUIRE(spu.writeMasterVolumeControl(0, 0x3fff));
  REQUIRE(spu.writeMasterVolumeControl(1, 0x3fff));
  unsigned position = 0;
  for (const auto attribute : {0x4000u, 0x4001u, 0xc001u, 0x8001u, 0u, 0x4000u})
  {
    REQUIRE(spu.writeCoreAttribute(0, attribute));
    const auto tick = SPU2InputTest::advanceSamples(spu, 1);
    REQUIRE(tick.sampleTick);
    REQUIRE(spu.readSoundRAMHalfword(0x400 + position) == 4076);
    REQUIRE(spu.readSoundRAMHalfword(0x1000 + position) == 4075);
    REQUIRE(spu.readSoundRAMHalfword(0x1200 + position) ==
      static_cast<std::uint16_t>(-4076));
    REQUIRE(spu.readSoundRAMHalfword(0x1400 + position) == 4075);
    REQUIRE(spu.readSoundRAMHalfword(0x1600 + position) ==
      static_cast<std::uint16_t>(-4076));
    REQUIRE(tick.outputs[0].left == ((attribute & 0x4000) ? 4074 : 0));
    REQUIRE(tick.outputs[0].right == ((attribute & 0x4000) ? -4076 : 0));
    REQUIRE(spu.readSoundRAMHalfword(0x800 + position) ==
      static_cast<std::uint16_t>(tick.outputs[0].left));
    REQUIRE(spu.readSoundRAMHalfword(0xa00 + position) ==
      static_cast<std::uint16_t>(tick.outputs[0].right));
    ++position;
  }
  REQUIRE(SPU2VoiceTestAccess::voice(spu, 1).age == 4);
  REQUIRE(SPU2VoiceTestAccess::valid(spu));
  REQUIRE_FALSE(spu.writeVoiceVolumeControl(2, 0x8000));
  REQUIRE(spu.readVoiceVolumeControl(2) == 0x3fff);
  REQUIRE_FALSE(spu.writeMasterVolumeControl(0, 0x8000));
  REQUIRE_FALSE(spu.writeCoreAttribute(0, 0x4040));
  REQUIRE(spu.readCoreAttribute(0) == 0x4000);
}

TEST_CASE("SPU2 mix lanes sum wide before clipping and decode both voice mask halves",
          "[iop][spu2][spu2-mix]")
{
  SPU2Controller spu;
  for (std::size_t voice = 0; voice < 24; ++voice)
  {
    constantVoice(spu, voice);
    if (voice >= 12)
      REQUIRE(spu.writeVoiceVolumeControl(voice * 2, 0x4000));
    REQUIRE(spu.writeVoiceVolumeControl(voice * 2 + 1, 0x4000));
  }
  spu.writeVoiceMix(0, 0xffff);
  spu.writeVoiceMix(1, 0xffff);
  spu.writeVoiceMix(2, 0x0fff);
  spu.writeVoiceMix(4, 0xffff);
  spu.writeVoiceMix(5, 0xffff);
  spu.writeVoiceMix(6, 0xffff);
  spu.writeVoiceMix(7, 0xffff);
  REQUIRE(spu.writeCoreAttribute(0, 0x4000));
  spu.writeMainMix(0, 0xc00);
  REQUIRE(spu.writeMasterVolumeControl(0, 0x3fff));
  REQUIRE(spu.writeMasterVolumeControl(1, 0x3fff));
  const auto tick = SPU2InputTest::advanceSamples(spu, 1);
  REQUIRE(spu.readSoundRAMHalfword(0x1000) == static_cast<std::uint16_t>(-12));
  REQUIRE(spu.readSoundRAMHalfword(0x1200) == 0x8000);
  REQUIRE(spu.readSoundRAMHalfword(0x1400) == 0x7fff);
  REQUIRE(spu.readSoundRAMHalfword(0x1600) == 0x8000);
  REQUIRE(tick.outputs[0].left == -12);
  REQUIRE(tick.outputs[0].right == -32766);
  REQUIRE(SPU2VoiceTestAccess::valid(spu));
}

TEST_CASE("SPU2 PCM and external gains are signed and core linkage uses current shared RAM",
          "[iop][spu2][spu2-mix]")
{
  SPU2Controller spu;
  SPU2InputTest::programProfile(spu);
  REQUIRE(spu.writeAutoDMAWriteSelection(0, 1));
  spu.writeSoundRAMHalfword(0x2000, 16000);
  spu.writeSoundRAMHalfword(0x2200, 0x8000);
  spu.writeEffectInputVolume(4, 0x4000);
  spu.writeEffectInputVolume(5, 0x8000);
  spu.writeEffectInputVolume(2, 0xffff);
  spu.writeEffectInputVolume(3, 0xffff);
  spu.writeEffectInputVolume(8, 0x4000);
  spu.writeEffectInputVolume(9, 0x4000);
  spu.writeMainMix(0, 0x00ff);
  spu.writeMainMix(1, 0x000c);
  for (std::size_t index = 0; index < 4; ++index)
    REQUIRE(spu.writeMasterVolumeControl(index, 0x3fff));
  spu.writeSoundRAMHalfword(0x800, 0xdead);
  spu.writeSoundRAMHalfword(0xa00, 0xbeef);
  const auto tick = SPU2InputTest::advanceSamples(spu, 1);
  REQUIRE(tick.samples[0].consumed);
  REQUIRE(tick.samples[0].left == 16000);
  REQUIRE(tick.samples[0].right == 0x8000);
  REQUIRE_FALSE(tick.samples[1].consumed);
  REQUIRE(tick.outputs[0].left == 7999);
  REQUIRE(tick.outputs[0].right == 32765);
  REQUIRE(tick.outputs[1].left == 3998);
  REQUIRE(tick.outputs[1].right == 16381);
  REQUIRE(spu.readSoundRAMHalfword(0x800) == 7999);
  REQUIRE(spu.readSoundRAMHalfword(0xa00) == 32765);
  REQUIRE(spu.readSoundRAMHalfword(0x1000) == 0);
  REQUIRE(spu.readSoundRAMHalfword(0x1800) == 0);
  REQUIRE_FALSE(spu.writeCoreAttribute(0, 0x8001));
  REQUIRE(spu.readCoreAttribute(0) == 0xc001);
}

TEST_CASE("SPU2 final dry mix clips before master gain and wet paths stay effects-disabled",
          "[iop][spu2][spu2-mix]")
{
  SPU2Controller spu;
  SPU2InputTest::programProfile(spu);
  for (std::size_t voice = 0; voice < 24; ++voice)
    constantVoice(spu, voice);
  spu.writeVoiceMix(0, 0xffff);
  spu.writeVoiceMix(1, 0x00ff);
  spu.writeVoiceMix(2, 0xffff);
  spu.writeVoiceMix(3, 0x00ff);
  spu.writeMainMix(0, 0x0880);
  REQUIRE(spu.writeMasterVolumeControl(0, 0x2000));
  REQUIRE(spu.writeAutoDMAWriteSelection(0, 1));
  spu.writeEffectInputVolume(4, 0x7fff);
  spu.writeSoundRAMHalfword(0x2000, 16000);
  auto tick = SPU2InputTest::advanceSamples(spu, 1);
  REQUIRE(spu.readSoundRAMHalfword(0x1000) == 0x7fff);
  REQUIRE(spu.readSoundRAMHalfword(0x1400) == 0x7fff);
  REQUIRE(tick.outputs[0].left == 16383);

  spu.writeMainMix(0, 0x0333);
  spu.writeEffectInputVolume(0, 0x7fff);
  spu.writeEffectInputVolume(1, 0x8000);
  tick = SPU2InputTest::advanceSamples(spu, 1);
  REQUIRE(tick.outputs[0].left == 0);
  REQUIRE(tick.outputs[0].right == 0);
  REQUIRE(spu.readSoundRAMHalfword(0x1401) == 0x7fff);
}

TEST_CASE("SPU2 voice capture aliases are observed by later refills but not buffered samples",
          "[iop][spu2][spu2-mix]")
{
  for (std::size_t core = 0; core < 2; ++core)
    for (const auto captureVoice : {1u, 3u})
    {
      SPU2Controller spu;
      const auto address = static_cast<std::uint32_t>(
        0x400 + core * 0x800 + (captureVoice == 3 ? 0x200 : 0));
      block(spu, address, 0x000c, 0x1111);
      const auto earlier = core * 24 + captureVoice - 1;
      const auto later = core * 24 + captureVoice + 1;
      for (const auto index : {earlier, later})
      {
        program(spu, index, address, 0);
        REQUIRE(SPU2VoiceTestAccess::start(spu, index));
      }
      samples(spu, 1);
      REQUIRE(SPU2VoiceTestAccess::voice(spu, earlier).history1 == 1);
      REQUIRE(SPU2VoiceTestAccess::voice(spu, later).history1 == 4096);
      REQUIRE(spu.readSoundRAMHalfword(address) == 0);
      samples(spu, 1);
      REQUIRE(SPU2VoiceTestAccess::voice(spu, earlier).fifo[0] == 1);
      REQUIRE(SPU2VoiceTestAccess::voice(spu, later).fifo[0] == 4096);
    }
}

TEST_CASE("SPU2 core mix and link captures precede core1 source fetches",
          "[iop][spu2][spu2-mix]")
{
  for (const auto address : {0x800u, 0xa00u, 0x1000u, 0x1200u, 0x1400u, 0x1600u})
  {
    SPU2Controller spu;
    block(spu, address, 0x000c, 0x1111);
    for (const auto index : {23u, 24u})
    {
      program(spu, index, address, 0);
      REQUIRE(SPU2VoiceTestAccess::start(spu, index));
    }
    samples(spu, 1);
    REQUIRE(SPU2VoiceTestAccess::voice(spu, 23).history1 == 1);
    REQUIRE(SPU2VoiceTestAccess::voice(spu, 24).history1 == 4096);
    REQUIRE(spu.readSoundRAMHalfword(address) == 0);
  }
}

TEST_CASE("SPU2 waveform and capture tick precedes a coincident accepted PCM DMA beat",
          "[iop][spu2][spu2-mix]")
{
  NekoSystem system;
  auto &spu = system.spu2Controller();
  auto &bus = system.iopBus();
  SPU2InputTest::programProfile(spu);
  REQUIRE(spu.writeAutoDMAWriteSelection(0, 1));
  block(spu, 0x2100, 0x000c, 0x1111);
  program(spu, 0, 0x2100, 0);
  system.runMasterCycles(6136);
  REQUIRE(SPU2VoiceTestAccess::start(spu, 0));
  REQUIRE(bus.write32(0x1000, 0x22220000).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(0x1f8010c0, 0x1000).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(0x1f8010c4, 0x00100010).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(0x1f8010c8, 0x01000201).status == IOPBusStatus::Completed);
  system.iopDMAController().writePriority(IOPDMABank::Primary, 0x80000);
  system.startTrace();
  system.runMasterCycles(1);
  REQUIRE(SPU2VoiceTestAccess::voice(spu, 0).history1 == 1);
  REQUIRE(SPU2VoiceTestAccess::voice(spu, 0).fifo[0] == 1);
  REQUIRE(spu.readSoundRAMHalfword(0x2100) == 0);
  REQUIRE(spu.readSoundRAMHalfword(0x2101) == 0x2222);
  const auto events = IOPTraceTest::subsystemEvents(
    system, static_cast<NekoTraceSubsystem>(16));
  REQUIRE(events.size() == 1);
  REQUIRE(events[0].masterCycle == 6137);
  REQUIRE(SPU2VoiceTestAccess::valid(spu));
}

TEST_CASE("SPU2 mix captures and final samples restore across ring wrap without output caches",
          "[iop][spu2][spu2-mix][save-state]")
{
  NekoSystem source;
  auto &spu = source.spu2Controller();
  constantVoice(spu, 1);
  constantVoice(spu, 47);
  for (std::size_t core = 0; core < 2; ++core)
  {
    REQUIRE(spu.writeCoreAttribute(core, 0x4000));
    for (std::size_t lane = 0; lane < 4; ++lane)
      spu.writeVoiceMix(core * 8 + lane * 2 + (core == 1 ? 1 : 0),
        core == 0 ? 2 : 0x80);
    spu.writeMainMix(core, core == 0 ? 0xc00 : 0xc0c);
    REQUIRE(spu.writeMasterVolumeControl(core * 2, 0x3fff));
    REQUIRE(spu.writeMasterVolumeControl(core * 2 + 1, 0x3fff));
  }
  spu.writeEffectInputVolume(8, 0x7fff);
  spu.writeEffectInputVolume(9, 0x8000);
  samples(spu, 511);
  const auto saved = source.saveState();
  NekoSystem restored;
  restored.loadState(saved);
  REQUIRE(restored.saveState() == saved);
  for (unsigned tick = 0; tick < 3; ++tick)
  {
    const auto cold = SPU2InputTest::advanceSamples(spu, 1);
    const auto loaded = SPU2InputTest::advanceSamples(restored.spu2Controller(), 1);
    for (std::size_t core = 0; core < 2; ++core)
    {
      REQUIRE(cold.outputs[core].left == loaded.outputs[core].left);
      REQUIRE(cold.outputs[core].right == loaded.outputs[core].right);
      REQUIRE(cold.outputs[core].left != 0);
    }
    REQUIRE(restored.iopStateHash() == source.iopStateHash());
    REQUIRE(restored.saveState() == source.saveState());
  }
  source.startTrace();
  source.runMasterCycles(6200);
  restored.runMasterCycles(6200);
  REQUIRE(restored.saveState() == source.saveState());
  auto previous = saved;
  previous[8] = 116;
  const auto before = restored.saveState();
  REQUIRE_THROWS_AS(inspectSaveState(previous), std::invalid_argument);
  REQUIRE_THROWS_AS(restored.loadState(previous), std::invalid_argument);
  REQUIRE(restored.saveState() == before);
}
