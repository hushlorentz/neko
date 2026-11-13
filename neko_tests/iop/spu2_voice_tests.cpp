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
  static bool valid(const SPU2Controller &spu) { return spu.hasValidVoiceState(); }
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
}

TEST_CASE("SPU2 waveform source reset state is canonical without admitting guest KON",
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
    REQUIRE(system.iopBus().write16(address, 0xffff).status != IOPBusStatus::Completed);
    REQUIRE(system.iopBus().read16(address).status != IOPBusStatus::Completed);
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
  REQUIRE(bus.write16(0x1f9001a4, 1).status != IOPBusStatus::Completed);
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
