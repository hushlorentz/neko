#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "catch.hpp"
#include "iop_dma_controller.hpp"
#include "iop_sif1_dma_channel.hpp"
#include "iop_trace_test_utils.hpp"
#include "save_state_mutation.hpp"
#include "spu2_input_test_utils.hpp"
#include "startup_diagnostics.hpp"

struct SPU2ObservationTestAccess
{
  static bool detached(const SPU2Controller &spu)
  {
    return !spu.observationsEnabled && spu.observationOrder == nullptr &&
      spu.observationCount == 0;
  }

  static void enable(SPU2Controller &spu, NekoObservationOrder *order)
  {
    spu.enableObservations(order);
  }

  static bool attached(const SPU2Controller &spu, NekoObservationOrder *order)
  {
    return spu.observationsEnabled && spu.observationOrder == order;
  }

  static std::size_t count(const SPU2Controller &spu)
  {
    return spu.observationCount;
  }

  static void disable(SPU2Controller &spu)
  {
    spu.disableObservations();
  }
};

namespace
{
  constexpr IOPAddress DPCR = 0x1f8010f0;
  constexpr IOPAddress DICR = 0x1f8010f4;
  constexpr IOPAddress DPCR2 = 0x1f801570;
  constexpr IOPAddress DICR2 = 0x1f801574;
  constexpr IOPAddress AUXILIARY = 0x1f801578;
  constexpr IOPAddress DPCR3 = 0x1f8015f0;
  constexpr IOPAddress SPU_MADR[] = {0x1f8010c0, 0x1f801500};
  constexpr IOPAddress SPU_BCR[] = {0x1f8010c4, 0x1f801504};
  constexpr IOPAddress SPU_CHCR[] = {0x1f8010c8, 0x1f801508};
  constexpr IOPAddress SIO2_MADR[] = {0x1f801540, 0x1f801550};
  constexpr IOPAddress SIO2_BCR[] = {0x1f801544, 0x1f801554};
  constexpr IOPAddress SIO2_CHCR[] = {0x1f801548, 0x1f801558};
  constexpr IOPWord SIO2_OUTPUT_STOPPED_CONTROLS[] = {
    0, 0x200, 0x201, 0x40000000, 0x40000200, 0x40000201
  };
  constexpr IOPAddress CHANNEL_INITIALIZATION_REGISTERS[] = {
    0x1f801080, 0x1f801084, 0x1f801088,
    0x1f801090, 0x1f801094, 0x1f801098,
    0x1f8010a0, 0x1f8010a4, 0x1f8010a8,
    0x1f8010b0, 0x1f8010b4, 0x1f8010b8,
    0x1f8010d0, 0x1f8010d4, 0x1f8010d8,
    0x1f8010e0, 0x1f8010e4, 0x1f8010e8,
    0x1f801510, 0x1f801514, 0x1f801518
  };
  constexpr IOPAddress SUPPLEMENTAL_INITIALIZATION_REGISTERS[] = {
    0x1f8010cc, 0x1f801560, 0x1f801564, 0x1f801568
  };
  const std::vector<IOPAddress> INITIALIZATION_REGISTERS = []()
  {
    std::vector<IOPAddress> addresses(
      std::begin(CHANNEL_INITIALIZATION_REGISTERS),
      std::end(CHANNEL_INITIALIZATION_REGISTERS));
    addresses.insert(addresses.end(), std::begin(SUPPLEMENTAL_INITIALIZATION_REGISTERS),
      std::end(SUPPLEMENTAL_INITIALIZATION_REGISTERS));
    return addresses;
  }();
  constexpr IOPAddress MADR = 0x1f801530;
  constexpr IOPAddress BCR = 0x1f801534;
  constexpr IOPAddress CHCR = 0x1f801538;
  constexpr IOPWord START = 0x01000400;
  constexpr IOPWord FLAG = 0x08000000;
  constexpr IOPWord FORCE = 0x8000;
  constexpr IOPWord SUMMARY = 0x80000000;
  constexpr IOPWord DMAC_CAUSE = 8;
  constexpr IOPDMABank PRIMARY = IOPDMABank::Primary;
  constexpr IOPDMABank SECONDARY = IOPDMABank::Secondary;

  void requireState(const NekoSystem &system, const std::vector<std::uint8_t> &state)
  {
    const bool unchanged = system.saveState() == state;
    REQUIRE(unchanged);
  }

  void programSIO2Input(NekoSystem &system, IOPAddress address = 0x1000)
  {
    REQUIRE(system.iopBus().write32(SIO2_MADR[0], address).status ==
      IOPBusStatus::Completed);
    REQUIRE(system.iopBus().write32(SIO2_BCR[0], 0x10024).status ==
      IOPBusStatus::Completed);
  }

  void startSIO2Input(NekoSystem &system)
  {
    REQUIRE(system.iopBus().write32(SIO2_CHCR[0], 0x01000201).status ==
      IOPBusStatus::Completed);
  }

  TEST_CASE("IOP SIO2 output DMA arms and cancels independently of every input phase",
    "[iop][dma][sio2-output-dma][save-state]")
  {
    for (unsigned phase = 0; phase < 5; ++phase)
    {
      CAPTURE(phase);
      NekoSystem system;
      auto &bus = system.iopBus();
      auto &dma = system.iopDMAController();
      auto &sio2 = system.sio2Controller();
      if (phase == 1)
        REQUIRE(sio2.writeTransmitByte(0x81) == SIO2AccessStatus::Accepted);
      if (phase == 2 || phase == 3)
      {
        programSIO2Input(system);
        startSIO2Input(system);
        dma.writePriority(SECONDARY, 8 << 16);
        for (unsigned word = 0; word < (phase == 2 ? 1u : 36u); ++word)
          dma.clockSIO2Input(bus, sio2);
      }
      if (phase == 4)
      {
        REQUIRE(sio2.writeCommandDescriptor(0, 0x001c0740) == SIO2AccessStatus::Accepted);
        for (const auto byte : {0x61, 0, 0, 0, 0, 0, 0})
          REQUIRE(sio2.writeTransmitByte(static_cast<std::uint8_t>(byte)) ==
            SIO2AccessStatus::Accepted);
        REQUIRE(sio2.startTransfer(0xb1) == SIO2TransferResult::CompletedWithInterrupt);
      }
      REQUIRE(bus.write32(SIO2_MADR[1], 0x2000).status == IOPBusStatus::Completed);
      REQUIRE(bus.write32(SIO2_BCR[1], 0x10024).status == IOPBusStatus::Completed);
      SaveStateMutation expected(system.saveState());
      expected.writeScalar("system.iop.dmaController.sio2Programming[1].channelControl",
        0x41000200);
      expected.refreshChecksum();
      REQUIRE(bus.write32(SIO2_CHCR[1], 0x41000200).status == IOPBusStatus::Completed);
      REQUIRE(bus.write32(SIO2_CHCR[1], 0x41000200).status == IOPBusStatus::Completed);
      requireState(system, expected.bytes());
      const auto armed = system.saveState();
      for (const auto stop : SIO2_OUTPUT_STOPPED_CONTROLS)
      {
        system.loadState(armed);
        expected.writeScalar("system.iop.dmaController.sio2Programming[1].channelControl",
          stop);
        expected.refreshChecksum();
        REQUIRE(bus.write32(SIO2_CHCR[1], stop).status == IOPBusStatus::Completed);
        requireState(system, expected.bytes());
      }
      system.loadState(armed);
      if (phase == 2)
      {
        REQUIRE(dma.clockSIO2Input(bus, sio2).outcome == IOPDMABeatOutcome::Accepted);
        REQUIRE(sio2.transmitInputSize() == 8);
        REQUIRE(bus.write32(SIO2_CHCR[0], 0).status == IOPBusStatus::Completed);
        REQUIRE(bus.read32(SIO2_CHCR[1]).value == 0x41000200);
      }
      if (phase == 4)
        REQUIRE(sio2.readReceiveByte().status == SIO2AccessStatus::Accepted);
    }
  }

  TEST_CASE("IOP SIO2 output DMA keeps pending requests inert across clocks and resets",
    "[iop][dma][sio2-output-dma][trace]")
  {
    for (const bool enabled : {false, true})
    {
      NekoSystem system;
      auto &bus = system.iopBus();
      auto &dma = system.iopDMAController();
      for (IOPAddress word = 0; word < 36; ++word)
        REQUIRE(bus.write32(0x2000 + word * 4, 0x91827300 + word).status ==
          IOPBusStatus::Completed);
      REQUIRE(bus.write32(SIO2_MADR[1], 0x2000).status == IOPBusStatus::Completed);
      REQUIRE(bus.write32(SIO2_BCR[1], 0x10024).status == IOPBusStatus::Completed);
      dma.writePriority(SECONDARY, enabled ? 0x800000 : 0);
      dma.writeInterrupt(PRIMARY, 0x800000);
      dma.writeInterrupt(SECONDARY, 0x200000);
      REQUIRE(bus.write32(SIO2_CHCR[1], 0x41000200).status == IOPBusStatus::Completed);
      const auto cursor = dma.tieBreakChannel();
      const auto priorityOrder = dma.grantOrder();
      NekoSystem plain;
      plain.loadState(system.saveState());
      system.startTrace();
      system.runMasterCycles(800);
      plain.runMasterCycles(800);
      REQUIRE(system.saveState() == plain.saveState());
      REQUIRE(dma.tieBreakChannel() == cursor);
      REQUIRE(dma.grantOrder() == priorityOrder);
      REQUIRE(dma.interruptStatus(SECONDARY) == 0x200000);
      REQUIRE(!dma.interruptPending());
      REQUIRE(system.iopInterruptController().status() == 0);
      REQUIRE(system.sio2Controller().readInterruptStatus() == 0);
      REQUIRE(system.sio2Controller().readReceiveByte().status == SIO2AccessStatus::Unsupported);
      for (IOPAddress word = 0; word < 36; ++word)
        REQUIRE(bus.read32(0x2000 + word * 4).value == 0x91827300 + word);
      REQUIRE(bus.read32(SIO2_MADR[1]).value == 0x2000);
      REQUIRE(bus.read32(SIO2_BCR[1]).value == 0x10024);
      bus.reset();
      system.iopCore().reset();
      system.sio2Controller().reset();
      REQUIRE(bus.read32(SIO2_CHCR[1]).value == 0x41000200);
      REQUIRE(inspectStartup(system).iopDMA.sio2Output.enabled == enabled);
      std::ostringstream json;
      writeStartupSummaryJsonLine(json, inspectStartup(system));
      REQUIRE(json.str().find("\"sio2_output\":{\"channel\":12,\"control\":1090519552") !=
        std::string::npos);
      REQUIRE(json.str().find("\"state\":\"armed_waiting_results\"") != std::string::npos);
      REQUIRE(json.str().find("\"remaining_words\":36,\"active\":true,\"state\"") !=
        std::string::npos);
      system.reset();
      REQUIRE(bus.read32(SIO2_CHCR[1]).value == 0);
      std::ostringstream resetJson;
      writeStartupSummaryJsonLine(resetJson, inspectStartup(system));
      REQUIRE(resetJson.str().find("\"state\":\"inactive\"") != std::string::npos);
    }
  }

  TEST_CASE("IOP SIO2 output DMA validates finite arming and locks active programming",
    "[iop][dma][sio2-output-dma]")
  {
    NekoSystem system;
    auto &bus = system.iopBus();
    for (const auto bcr : {0u, 0x24u, 0x10023u, 0x20024u, 0xffffffffu})
    {
      REQUIRE(bus.write32(SIO2_BCR[1], bcr).status == IOPBusStatus::Completed);
      const auto before = system.saveState();
      REQUIRE(bus.write32(SIO2_CHCR[1], 0x41000200).status == IOPBusStatus::Unmapped);
      requireState(system, before);
    }
    REQUIRE(bus.write32(SIO2_BCR[1], 0x10024).status == IOPBusStatus::Completed);
    const auto stopped = system.saveState();
    IOPBus dmaOnly;
    dmaOnly.attachDMA(&system.iopDMAController(), &system.iopSIF1DMAC());
    REQUIRE(dmaOnly.write32(SIO2_CHCR[1], 0x41000200).status == IOPBusStatus::Unmapped);
    for (const auto control : {0x01000200u, 0x41000201u, 0x41000000u, 0xc1000200u})
      REQUIRE(bus.write32(SIO2_CHCR[1], control).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(SIO2_CHCR[1], 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write16(SIO2_CHCR[1], 0x200).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.writeMasked32(SIO2_CHCR[1], 0x41000200, 7).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.readInstruction32(SIO2_CHCR[1]).status == IOPBusStatus::Unmapped);
    requireState(system, stopped);
    for (const IOPAddress segment : {0u, 0x80000000u, 0xa0000000u})
    {
      system.loadState(stopped);
      REQUIRE(system.iopCore().writeData32(SIO2_CHCR[1] | segment, 0x41000200).outcome ==
        IOPAccessOutcome::Completed);
      const auto armed = system.saveState();
      for (const auto address : {SIO2_MADR[1], SIO2_BCR[1]})
      {
        REQUIRE(bus.write32(address, 0).status == IOPBusStatus::Unmapped);
        REQUIRE(bus.writeMasked32(address, 0, 15).status == IOPBusStatus::Unmapped);
        REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Unmapped);
        REQUIRE(bus.write16(address + 2, 0).status == IOPBusStatus::Unmapped);
      }
      REQUIRE(dmaOnly.write32(SIO2_CHCR[1], 0).status == IOPBusStatus::Unmapped);
      requireState(system, armed);
    }
  }

  TEST_CASE("IOP SIO2 output DMA rejects malformed armed continuations transactionally",
    "[iop][dma][sio2-output-dma][save-state]")
  {
    NekoSystem source;
    REQUIRE(source.iopBus().write32(SIO2_BCR[1], 0x10024).status == IOPBusStatus::Completed);
    REQUIRE(source.iopBus().write32(SIO2_CHCR[1], 0x41000200).status == IOPBusStatus::Completed);
    const auto armed = source.saveState();
    NekoSystem destination;
    destination.loadState(armed);
    requireState(destination, armed);
    for (const auto &mutation : std::vector<std::pair<std::string, std::uint64_t>>{
      {"system.iop.dmaController.sio2Programming[1].channelControl", 0x01000200},
      {"system.iop.dmaController.sio2Programming[1].channelControl", 0x41000201},
      {"system.iop.dmaController.sio2Programming[1].blockControl", 0x20024},
      {"system.iop.dmaController.sio2Programming[1].memoryAddress", 1},
      {"system.iop.dmaController.sio2Programming[1].memoryAddress", 0x1000000},
      {"system.iop.dmaController.nextChannel", 12}})
    {
      CAPTURE(mutation.first);
      SaveStateMutation invalid(armed);
      invalid.writeScalar(mutation.first, mutation.second);
      invalid.refreshChecksum();
      REQUIRE_THROWS_AS(inspectSaveState(invalid), std::invalid_argument);
      REQUIRE_THROWS_AS(destination.loadState(invalid), std::invalid_argument);
      requireState(destination, armed);
    }
  }

  TEST_CASE("IOP SIO2 output DMA guest arming restores branch load and traced continuations",
    "[iop][dma][sio2-output-dma][save-state][trace]")
  {
    for (const unsigned cut : {40u, 56u})
    {
      NekoSystem original;
      REQUIRE(original.iopBus().write32(SIO2_BCR[1], 0x10024).status == IOPBusStatus::Completed);
      const std::array<IOPWord, 13> program = {{
        0x3c01bf80, 0x34211558, 0x3c024100, 0x34420200,
        0x10000002, 0xac220000, 0xffffffff, 0x8c230000, 0,
        0x8c240000, 0, 0x1000fffd, 0
      }};
      for (std::size_t index = 0; index < program.size(); ++index)
        REQUIRE(original.iopBus().write32(static_cast<IOPAddress>(index * 4),
          program[index]).status == IOPBusStatus::Completed);
      original.iopCore().startExecution(0xa0000000);
      original.runMasterCycles(cut);
      REQUIRE(original.iopCore().branchContinuation().active == (cut == 40));
      REQUIRE(original.iopCore().delayedResultContinuation().source ==
        (cut == 56 ? IOPDelayedResultSource::Load : IOPDelayedResultSource::None));
      const auto checkpoint = original.saveState();
      NekoSystem restored;
      NekoSystem plain;
      restored.loadState(checkpoint);
      plain.loadState(checkpoint);
      original.startTrace();
      restored.startTrace();
      original.runMasterCycles(400 - cut);
      restored.runMasterCycles(400 - cut);
      plain.runMasterCycles(400 - cut);
      REQUIRE(original.iopCore().executionState() == IOPExecutionState::Running);
      REQUIRE(original.iopCore().generalRegister(3) == 0x41000200);
      REQUIRE(original.iopCore().generalRegister(4) == 0x41000200);
      REQUIRE(original.iopBus().read32(SIO2_CHCR[1]).value == 0x41000200);
      IOPTraceTest::requireEvents(original.trace(), restored.trace());
      REQUIRE(original.saveState() == restored.saveState());
      REQUIRE(original.saveState() == plain.saveState());
      REQUIRE(plain.trace().empty());
    }
  }

  TEST_CASE("IOP SIO2 input DMA stages one opaque block through real grants",
    "[iop][dma][sio2-input-dma]")
  {
    NekoSystem system;
    auto &bus = system.iopBus();
    for (IOPWord word = 0; word < 36; ++word)
      REQUIRE(bus.write32(0x1000 + word * 4, 0x91827300 + word).status ==
        IOPBusStatus::Completed);
    REQUIRE(bus.write32(SIO2_MADR[0], 0x1000).status == IOPBusStatus::Completed);
    REQUIRE(bus.write32(SIO2_BCR[0], 0x10024).status == IOPBusStatus::Completed);
    REQUIRE(bus.write32(SIO2_CHCR[0], 0x01000201).status == IOPBusStatus::Completed);
    REQUIRE(system.sio2Controller().transmitInputSize() == 0);
    system.runMasterCycles(16);
    REQUIRE(system.sio2Controller().transmitInputSize() == 0);
    REQUIRE(bus.write32(DPCR2, 8 << 16).status == IOPBusStatus::Completed);
    REQUIRE(bus.write32(DICR2, 1 << 20).status == IOPBusStatus::Completed);
    system.runMasterCycles(35 * 8);
    REQUIRE(system.sio2Controller().transmitInputSize() == 140);
    REQUIRE(bus.write32(0x108c, 0x11223344).status == IOPBusStatus::Completed);
    system.runMasterCycles(8);
    REQUIRE(bus.read32(SIO2_MADR[0]).value == 0x1090);
    REQUIRE(bus.read32(SIO2_BCR[0]).value == 0x24);
    REQUIRE(bus.read32(SIO2_CHCR[0]).value == 0x201);
    REQUIRE(system.sio2Controller().transmitInputSize() == 144);
    for (std::size_t byte = 0; byte < 144; ++byte)
      REQUIRE(system.sio2Controller().transmitInputByte(byte) ==
        static_cast<std::uint8_t>((byte / 4 == 35 ? 0x11223344 :
          0x91827300 + byte / 4) >> (8 * (byte % 4))));
    REQUIRE((system.iopDMAController().interruptStatus(SECONDARY) & 0x10000000) != 0);
    REQUIRE((system.iopInterruptController().status() & DMAC_CAUSE) != 0);
    REQUIRE(bus.write32(DICR2, 0x10100000).status == IOPBusStatus::Completed);
    REQUIRE(!system.iopDMAController().interruptPending());
    REQUIRE((system.iopInterruptController().status() & DMAC_CAUSE) != 0);
    system.iopInterruptController().writeStatus(~DMAC_CAUSE);
    REQUIRE((system.iopInterruptController().status() & DMAC_CAUSE) == 0);
    REQUIRE(system.sio2Controller().readInterruptStatus() == 0);
    REQUIRE(system.sio2Controller().readControl() == 0);
    REQUIRE(system.sio2Controller().readTransferStatus(0).status ==
      SIO2AccessStatus::Unsupported);
    REQUIRE(system.sio2Controller().readReceiveByte().status ==
      SIO2AccessStatus::Unsupported);
  }

  TEST_CASE("IOP SIO2 input DMA excludes competing ingress and cancels partial blocks",
    "[iop][dma][sio2-input-dma]")
  {
    for (const IOPWord stop : {0u, 0x200u, 0x201u})
    {
      NekoSystem system;
      auto &bus = system.iopBus();
      auto &sio2 = system.sio2Controller();
      auto &dma = system.iopDMAController();
      programSIO2Input(system);
      startSIO2Input(system);
      const auto initialHash = system.iopStateHash();
      startSIO2Input(system);
      REQUIRE(system.iopStateHash() == initialHash);
      REQUIRE(sio2.transmitInputPhase() == SIO2IngressPhase::DMAOpen);
      REQUIRE(bus.write8(0x1f808260, 0x61).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write32(0x1f808200, 0x1234).status == IOPBusStatus::Unmapped);
      REQUIRE(sio2.readCommandDescriptor(0) == 0);
      REQUIRE(bus.write32(0x1f808268, 0xb1).status == IOPBusStatus::Unmapped);
      REQUIRE_THROWS_AS(sio2.reset(), std::logic_error);
      REQUIRE(system.iopStateHash() == initialHash);
      dma.writePriority(SECONDARY, 8 << 16);
      for (unsigned word = 0; word < 3; ++word)
        REQUIRE(dma.clockSIO2Input(bus, sio2).outcome == IOPDMABeatOutcome::Accepted);
      REQUIRE(bus.read32(SIO2_MADR[0]).value == 0x100c);
      REQUIRE(bus.read32(SIO2_BCR[0]).value == 0x10024);
      const auto partial = system.saveState();
      for (const auto address : {SIO2_MADR[0], SIO2_BCR[0]})
      {
        REQUIRE(bus.write32(address, 0).status == IOPBusStatus::Unmapped);
        REQUIRE(bus.writeMasked32(address, 0, 15).status == IOPBusStatus::Unmapped);
        REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Unmapped);
        REQUIRE(bus.write16(address + 2, 0).status == IOPBusStatus::Unmapped);
      }
      startSIO2Input(system);
      REQUIRE_THROWS_AS(sio2.reset(), std::logic_error);
      requireState(system, partial);
      REQUIRE(bus.write32(0x1f808204, 0x12345678).status == IOPBusStatus::Completed);
      REQUIRE(bus.write32(0x1f808240, 0xff020405).status == IOPBusStatus::Completed);
      REQUIRE(bus.write32(0x1f808268, 0x3bc).status == IOPBusStatus::Completed);
      REQUIRE(sio2.transmitInputSize() == 12);
      REQUIRE(bus.write32(SIO2_CHCR[0], stop).status == IOPBusStatus::Completed);
      REQUIRE(!dma.sio2InputActive());
      REQUIRE(dma.sio2InputRemainingWords() == 0);
      REQUIRE(sio2.transmitInputPhase() == SIO2IngressPhase::Empty);
      REQUIRE(sio2.transmitInputSize() == 0);
      REQUIRE(bus.read32(SIO2_MADR[0]).value == 0x100c);
      REQUIRE(bus.read32(SIO2_BCR[0]).value == 0x10024);
      REQUIRE(bus.read32(SIO2_CHCR[0]).value == stop);
      REQUIRE(dma.interruptStatus(SECONDARY) == 0);
      REQUIRE(sio2.readControl() == 0x3bc);
      startSIO2Input(system);
      REQUIRE(sio2.transmitInputSize() == 0);
      bus.reset();
      system.iopCore().reset();
      REQUIRE(dma.sio2InputActive());
      system.reset();
      REQUIRE(!system.iopDMAController().sio2InputActive());
      REQUIRE(system.sio2Controller().transmitInputPhase() == SIO2IngressPhase::Empty);
    }
  }

  TEST_CASE("IOP SIO2 input DMA sealed frames require an explicit clear boundary",
    "[iop][dma][sio2-input-dma]")
  {
    NekoSystem system;
    auto &bus = system.iopBus();
    auto &sio2 = system.sio2Controller();
    auto &dma = system.iopDMAController();
    // Keep an acknowledged serial transaction's status and cause across DMA transport.
    REQUIRE(bus.write32(0x1f808200, 0x001c0740).status == IOPBusStatus::Completed);
    for (const auto byte : {0x61, 0, 0, 0, 0, 0, 0})
      REQUIRE(bus.write8(0x1f808260, byte).status == IOPBusStatus::Completed);
    REQUIRE(bus.write32(0x1f808268, 0xb1).status == IOPBusStatus::Completed);
    programSIO2Input(system);
    REQUIRE(bus.write32(SIO2_CHCR[0], 0x01000201).status == IOPBusStatus::Unmapped);
    for (unsigned byte = 0; byte < 7; ++byte)
      REQUIRE(bus.read8(0x1f808264).value == 0xff);
    startSIO2Input(system);
    dma.writePriority(SECONDARY, 8 << 16);
    const auto irqBefore = system.iopInterruptController().status();
    for (unsigned word = 0; word < 36; ++word)
    {
      REQUIRE(dma.clockSIO2Input(bus, sio2).outcome ==
        (word == 35 ? IOPDMABeatOutcome::Completed : IOPDMABeatOutcome::Accepted));
      REQUIRE(bus.read32(SIO2_MADR[0]).value == 0x1000 + (word + 1) * 4);
      REQUIRE(bus.read32(SIO2_BCR[0]).value == (word == 35 ? 0x24 : 0x10024));
    }
    REQUIRE(sio2.transmitInputPhase() == SIO2IngressPhase::DMASealed);
    REQUIRE(dma.interruptStatus(SECONDARY) == 0); // Completion was not masked in.
    REQUIRE(system.iopInterruptController().status() == irqBefore);
    REQUIRE(sio2.readControl() == 0xb1);
    REQUIRE(sio2.readInterruptStatus() == 1);
    REQUIRE(sio2.readTransferStatus(0).value == 0x1d100);
    REQUIRE(sio2.readReceiveByte().status == SIO2AccessStatus::Unsupported);
    REQUIRE(bus.write8(0x1f808260, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(0x1f808268, 0xb1).status == IOPBusStatus::Unmapped);
    for (IOPWord stop : {0u, 0x200u, 0x201u})
    {
      REQUIRE(bus.write32(SIO2_CHCR[0], stop).status == IOPBusStatus::Completed);
      REQUIRE(sio2.transmitInputSize() == 144);
    }
    programSIO2Input(system, 0x2000);
    REQUIRE(bus.write32(SIO2_CHCR[0], 0x01000201).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(SIO2_CHCR[1], 0x01000201).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(0x1f808200, 0xdeadbeef).status == IOPBusStatus::Completed);
    REQUIRE(sio2.transmitInputPhase() == SIO2IngressPhase::Empty);
    startSIO2Input(system);
  }

  TEST_CASE("IOP SIO2 input DMA rejects invalid profiles and reads only granted RAM words",
    "[iop][dma][sio2-input-dma]")
  {
    NekoSystem system;
    auto &bus = system.iopBus();
    auto &dma = system.iopDMAController();
    auto &sio2 = system.sio2Controller();
    for (IOPWord bcr : {0u, 0x24u, 0x10023u, 0x20024u, 0x10025u, 0xffffffffu})
    {
      REQUIRE(bus.write32(SIO2_BCR[0], bcr).status == IOPBusStatus::Completed);
      const auto hash = system.iopStateHash();
      REQUIRE(bus.write32(SIO2_CHCR[0], 0x01000201).status == IOPBusStatus::Unmapped);
      REQUIRE(system.iopStateHash() == hash);
    }
    programSIO2Input(system);
    REQUIRE(sio2.writeTransmitByte(0) == SIO2AccessStatus::Accepted);
    REQUIRE(bus.write32(SIO2_CHCR[0], 0x01000201).status == IOPBusStatus::Unmapped);
    REQUIRE(sio2.writeCommandDescriptor(0, 0) == SIO2AccessStatus::Accepted);
    SIO2Controller foreign;
    REQUIRE_THROWS_AS(dma.writeSIO2ChannelControl(
      IOPSIO2DMAChannel::Input, 0x01000201, bus, foreign), std::invalid_argument);
    // The first word is readable; the next address is outside the existing RAM aperture.
    programSIO2Input(system, 0x7ffffc);
    REQUIRE(bus.write32(0x1ffffc, 0x78563412).status == IOPBusStatus::Completed);
    startSIO2Input(system);
    dma.writePriority(SECONDARY, 8 << 16);
    REQUIRE(dma.clockSIO2Input(bus, sio2).outcome == IOPDMABeatOutcome::Accepted);
    REQUIRE(sio2.transmitInputByte(0) == 0x12);
    REQUIRE(sio2.transmitInputByte(3) == 0x78);
    REQUIRE(dma.sio2MemoryAddress(IOPSIO2DMAChannel::Input) == 0x800000);
    const auto accepted = system.saveState();
    const auto cursor = dma.tieBreakChannel();
    const auto rejected = dma.clockSIO2Input(bus, sio2);
    REQUIRE(rejected.outcome == IOPDMABeatOutcome::Rejected);
    REQUIRE(rejected.channel == IOPDMAChannel::SIO2Input);
    REQUIRE(rejected.failure == IOPDMAFailure::SourceRead);
    REQUIRE(rejected.address == 0x800000);
    REQUIRE(rejected.status == IOPBusStatus::Unmapped);
    REQUIRE(dma.tieBreakChannel() == cursor);
    requireState(system, accepted);
    REQUIRE_THROWS_AS(system.clockMasterCycle(), IOPDMATransferError);
    REQUIRE(sio2.transmitInputSize() == 4);
    REQUIRE(dma.sio2InputRemainingWords() == 35);
  }

  TEST_CASE("IOP SIO2 input DMA restores every ingress origin and rejects cross-owner damage",
    "[iop][dma][sio2-input-dma][save-state]")
  {
    NekoSystem original;
    programSIO2Input(original);
    startSIO2Input(original);
    original.iopDMAController().writePriority(SECONDARY, 8 << 16);
    original.runMasterCycles(13 * 8);
    const auto checkpoint = original.saveState();
    const auto hash = original.iopStateHash();
    NekoSystem restored;
    restored.loadState(checkpoint);
    REQUIRE(restored.iopStateHash() == hash);
    REQUIRE(restored.sio2Controller().transmitInputPhase() == SIO2IngressPhase::DMAOpen);
    REQUIRE(restored.sio2Controller().transmitInputSize() == 52);
    original.runMasterCycles(23 * 8);
    restored.runMasterCycles(23 * 8);
    REQUIRE(original.saveState() == restored.saveState());
    const auto sealed = original.saveState();
    restored.loadState(checkpoint);
    restored.loadState(sealed);
    REQUIRE(restored.saveState() == sealed);
    REQUIRE(restored.sio2Controller().transmitInputPhase() == SIO2IngressPhase::DMASealed);
    for (const auto &mutation : std::vector<std::pair<std::string, std::uint64_t>>{
      {"system.iop.sio2.transmitInput.phase", 0},
      {"system.iop.sio2.transmitInput.phase", 1},
      {"system.iop.sio2.transmitInput.phase", 3},
      {"system.iop.sio2.transmitInput.phase", 4},
      {"system.iop.sio2.transmitInput.count", 56},
      {"system.iop.sio2.transmitInput.bytes[52].value", 1},
      {"system.iop.dmaController.sio2InputWordsTransferred", 12},
      {"system.iop.dmaController.sio2InputWordsTransferred", 36},
      {"system.iop.dmaController.sio2Programming[0].channelControl", 0x201},
      {"system.iop.dmaController.sio2Programming[0].blockControl", 0x20024},
      {"system.iop.dmaController.sio2Programming[1].channelControl", 0x01000201},
      {"system.iop.dmaController.nextChannel", 12},
      {"container.version", 104}})
    {
      CAPTURE(mutation.first);
      SaveStateMutation invalid(checkpoint);
      invalid.writeScalar(mutation.first, mutation.second);
      invalid.refreshChecksum();
      REQUIRE_THROWS_AS(inspectSaveState(invalid), std::invalid_argument);
      REQUIRE_THROWS_AS(restored.loadState(invalid), std::invalid_argument);
      requireState(restored, sealed);
    }
    // An identically sized CPU packet must not become a sealed DMA frame on adoption.
    NekoSystem cpu;
    for (unsigned byte = 0; byte < 144; ++byte)
      REQUIRE(cpu.sio2Controller().writeTransmitByte(static_cast<std::uint8_t>(byte)) ==
        SIO2AccessStatus::Accepted);
    const auto cpuState = cpu.saveState();
    restored.loadState(cpuState);
    REQUIRE(restored.sio2Controller().transmitInputPhase() == SIO2IngressPhase::CPU);
    REQUIRE(restored.sio2Controller().writeTransmitByte(0xaa) == SIO2AccessStatus::Accepted);
    REQUIRE(restored.sio2Controller().transmitInputSize() == 145);
    restored.loadState(sealed);
    restored.sio2Controller().reset();
    REQUIRE(restored.sio2Controller().transmitInputSize() == 0);
    REQUIRE(restored.iopDMAController().sio2MemoryAddress(IOPSIO2DMAChannel::Input) == 0x1090);
  }

  TEST_CASE("IOP SIO2 input DMA guest start restores branch load and traced continuations",
    "[iop][dma][sio2-input-dma][save-state][trace]")
  {
    for (const unsigned cut : {40u, 56u})
    {
      NekoSystem original;
      programSIO2Input(original);
      original.iopDMAController().writePriority(SECONDARY, 8 << 16);
      const std::array<IOPWord, 13> program = {{
        0x3c01bf80, 0x34211548, 0x3c020100, 0x34420201,
        0x10000002, 0xac220000, 0xffffffff, 0x8c230000, 0,
        0x8c240000, 0, 0x1000fffd, 0
      }};
      for (std::size_t index = 0; index < program.size(); ++index)
        REQUIRE(original.iopBus().write32(static_cast<IOPAddress>(index * 4),
          program[index]).status == IOPBusStatus::Completed);
      original.iopCore().startExecution(0xa0000000);
      original.runMasterCycles(cut);
      REQUIRE(original.iopCore().branchContinuation().active == (cut == 40));
      REQUIRE(original.iopCore().delayedResultContinuation().source ==
        (cut == 56 ? IOPDelayedResultSource::Load : IOPDelayedResultSource::None));
      const auto checkpoint = original.saveState();
      NekoSystem restored;
      NekoSystem plain;
      restored.loadState(checkpoint);
      plain.loadState(checkpoint);
      original.startTrace();
      restored.startTrace();
      original.runMasterCycles(400 - cut);
      restored.runMasterCycles(400 - cut);
      plain.runMasterCycles(400 - cut);
      REQUIRE(original.iopCore().executionState() == IOPExecutionState::Running);
      REQUIRE(original.iopCore().generalRegister(3) == 0x01000201);
      REQUIRE(original.iopCore().generalRegister(4) == 0x201);
      REQUIRE(original.sio2Controller().transmitInputPhase() == SIO2IngressPhase::DMASealed);
      REQUIRE(original.sio2Controller().transmitInputSize() == 144);
      IOPTraceTest::requireEvents(original.trace(), restored.trace());
      REQUIRE(original.saveState() == restored.saveState());
      REQUIRE(original.saveState() == plain.saveState());
      REQUIRE(plain.trace().empty());
      const auto firstBeat = std::find_if(original.trace().begin(), original.trace().end(),
        [](const NekoTraceEvent &event) { return event.subsystem == NekoTraceSubsystem::SIO2; });
      REQUIRE(firstBeat != original.trace().end());
      REQUIRE(firstBeat->masterCycle == (cut == 40 ? 49 : 57));
    }
  }

  TEST_CASE("IOP SIO2 input DMA observations suppress waits without changing machine state",
    "[iop][dma][sio2-input-dma][trace]")
  {
    NekoSystem system;
    programSIO2Input(system);
    startSIO2Input(system);
    NekoSystem plain;
    plain.loadState(system.saveState());
    system.startTrace();
    system.runMasterCycles(80);
    plain.runMasterCycles(80);
    const auto countBeats = [&]()
    {
      return std::count_if(system.trace().begin(), system.trace().end(),
        [](const NekoTraceEvent &event) { return event.subsystem == NekoTraceSubsystem::SIO2; });
    };
    REQUIRE(countBeats() == 1);
    REQUIRE(system.saveState() == plain.saveState());
    system.startTrace();
    system.runMasterCycles(80);
    plain.runMasterCycles(80);
    REQUIRE(countBeats() == 1);
    system.iopDMAController().writePriority(SECONDARY, 8 << 16);
    plain.iopDMAController().writePriority(SECONDARY, 8 << 16);
    system.runMasterCycles(36 * 8);
    plain.runMasterCycles(36 * 8);
    REQUIRE(countBeats() == 37);
    REQUIRE(system.saveState() == plain.saveState());
    unsigned word = 0;
    for (const auto &event : system.trace())
    {
      if (event.subsystem != NekoTraceSubsystem::SIO2) continue;
      const auto beat = nekoDecodeSIO2DMABeat(event);
      if (beat.outcome == NekoSPUTrace::Outcome::Wait) continue;
      REQUIRE(beat.sourceAddress == 0x1000 + word * 4);
      REQUIRE(beat.remainingBefore == 36 - word);
      REQUIRE(beat.remainingAfter == 35 - word);
      REQUIRE(beat.stagedBefore == word * 4);
      REQUIRE(beat.stagedAfter == (word + 1) * 4);
      REQUIRE(beat.outcome == (word == 35 ? NekoSPUTrace::Outcome::Completed :
        NekoSPUTrace::Outcome::Accepted));
      ++word;
    }
    REQUIRE(word == 36);
  }

  TEST_CASE("IOP SIO2 DMA CHCR retains stopped configuration without starting",
    "[iop][dma][sio2-dma][sio2-chcr]")
  {
    NekoSystem system;
    for (const auto physical : SIO2_CHCR)
    {
      REQUIRE(system.iopBus().read32(physical).value == 0);
      const auto other = physical == SIO2_CHCR[0] ? SIO2_CHCR[1] : SIO2_CHCR[0];
      REQUIRE(system.iopBus().write32(other, 0x200).status == IOPBusStatus::Completed);
      for (const IOPAddress segment : {0u, 0x80000000u, 0xa0000000u})
      for (const IOPWord value : SIO2_OUTPUT_STOPPED_CONTROLS)
      {
        if (physical == SIO2_CHCR[0] && (value & 0x40000000))
          continue;
        CAPTURE(physical);
        CAPTURE(segment);
        CAPTURE(value);
        REQUIRE(system.iopBus().writeMasked32(physical, value, 15).status ==
          IOPBusStatus::Completed);
        REQUIRE(system.iopCore().writeData32(physical | segment, value).outcome ==
          IOPAccessOutcome::Completed);
        REQUIRE(system.iopCore().readData32(physical | segment).value == value);
        REQUIRE(system.iopBus().read32(physical).value == value);
        REQUIRE(system.iopBus().read32(other).value == 0x200);
      }
      REQUIRE(system.iopBus().write32(other, 0).status == IOPBusStatus::Completed);
    }
    IOPBus dmaOnly;
    dmaOnly.attachDMA(&system.iopDMAController(), &system.iopSIF1DMAC());
    for (const auto value : SIO2_OUTPUT_STOPPED_CONTROLS)
    {
      REQUIRE(dmaOnly.write32(SIO2_CHCR[1], value).status == IOPBusStatus::Completed);
      REQUIRE(dmaOnly.read32(SIO2_CHCR[1]).value == value);
    }
    REQUIRE(system.sio2Controller().transmitInputSize() == 0);
    REQUIRE(system.iopDMAController().interruptStatus(SECONDARY) == 0);
  }

  TEST_CASE("IOP SIO2 output stopped flags preserve open and sealed input transport",
    "[iop][dma][sio2-dma][sio2-chcr]")
  {
    for (const IOPWord words : {1u, 36u})
    {
      NekoSystem system;
      auto &bus = system.iopBus();
      auto &dma = system.iopDMAController();
      dma.writePriority(SECONDARY, 0x00080000);
      programSIO2Input(system);
      REQUIRE(bus.write32(SIO2_MADR[1], 0x60dc0).status == IOPBusStatus::Completed);
      REQUIRE(bus.write32(SIO2_BCR[1], 0x10024).status == IOPBusStatus::Completed);
      REQUIRE(bus.write32(0x60dc0, 0x91827364).status == IOPBusStatus::Completed);
      startSIO2Input(system);
      for (IOPWord word = 0; word < words; ++word)
        dma.clockSIO2Input(bus, system.sio2Controller());
      REQUIRE(system.sio2Controller().transmitInputSize() == words * 4);
      SaveStateMutation expected(system.saveState());
      for (const auto value : SIO2_OUTPUT_STOPPED_CONTROLS)
      {
        CAPTURE(words);
        CAPTURE(value);
        expected.writeScalar("system.iop.dmaController.sio2Programming[1].channelControl",
          value);
        expected.refreshChecksum();
        REQUIRE(bus.write32(SIO2_CHCR[1], value).status == IOPBusStatus::Completed);
        REQUIRE(bus.write32(SIO2_CHCR[1], value).status == IOPBusStatus::Completed);
        REQUIRE(bus.read32(SIO2_CHCR[1]).value == value);
        if (value & 0x40000000)
        {
          REQUIRE(bus.write32(SIO2_CHCR[0], value).status == IOPBusStatus::Unmapped);
          const auto start = bus.write32(SIO2_CHCR[1], value | 0x01000000);
          REQUIRE(start.status == (value == 0x40000200 ? IOPBusStatus::Completed :
            IOPBusStatus::Unmapped));
          if (start.status == IOPBusStatus::Completed)
            REQUIRE(bus.write32(SIO2_CHCR[1], value).status == IOPBusStatus::Completed);
        }
        requireState(system, expected.bytes());
      }
    }
  }

  TEST_CASE("IOP SIO2 DMA MADR retains the originating BIOS address",
    "[iop][dma][sio2-dma]")
  {
    NekoSystem system;
    REQUIRE(system.iopBus().write32(0x1f801540, 0x00060790).status ==
      IOPBusStatus::Completed);
    REQUIRE(system.iopBus().read32(0x1f801540).value == 0x00060790);
    REQUIRE(system.iopBus().read32(0x1f801550).value == 0);
    REQUIRE(system.iopBus().read32(0x1f801548).value == 0);
    REQUIRE(system.iopDMAController().interruptStatus(SECONDARY) == 0);
    REQUIRE(system.sio2Controller().transmitInputSize() == 0);
  }

  TEST_CASE("IOP SIO2 DMA programming retains paired addresses and block lanes",
    "[iop][dma][sio2-dma]")
  {
    NekoSystem system;
    auto &bus = system.iopBus();
    const auto initialHash = system.iopStateHash();
    REQUIRE(bus.write32(SIO2_MADR[0], 0x60790).status == IOPBusStatus::Completed);
    REQUIRE(system.iopStateHash() != initialHash);
    REQUIRE(bus.writeMasked32(SIO2_MADR[1], 0xabfffffc, 15).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.read32(SIO2_MADR[0]).value == 0x60790);
    REQUIRE(bus.read32(SIO2_MADR[1]).value == 0x00fffffc);
    for (const auto address : SIO2_BCR)
    {
      for (IOPWord value : {0u, 0xffffffffu, 0x89abcdefu})
      {
        REQUIRE(bus.write32(address, value).status == IOPBusStatus::Completed);
        REQUIRE(bus.read32(address).value == value);
        REQUIRE(bus.read16(address).value == (value & 0xffff));
        REQUIRE(bus.read16(address + 2).value == (value >> 16));
      }
      REQUIRE(bus.write16(address, 0x0010).status == IOPBusStatus::Completed);
      REQUIRE(bus.read32(address).value == 0x89ab0010);
      REQUIRE(bus.write16(address + 2, 0x8000).status == IOPBusStatus::Completed);
      REQUIRE(bus.read32(address).value == 0x80000010);
    }
    REQUIRE(bus.writeMasked32(SIO2_BCR[0], 0x12345678, 15).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.read32(SIO2_BCR[0]).value == 0x12345678);
    REQUIRE(bus.read32(SIO2_BCR[1]).value == 0x80000010);
    const auto programmed = system.saveState();
    for (const auto address : SIO2_MADR)
    for (IOPWord low = 1; low < 4; ++low)
    {
      REQUIRE(bus.write32(address, 0xff060790 | low).status ==
        IOPBusStatus::Unmapped);
      REQUIRE(bus.writeMasked32(address, 0xff060790 | low, 15).status ==
        IOPBusStatus::Unmapped);
    }
    requireState(system, programmed);
  }

  TEST_CASE("IOP SIO2 DMA passive writes preserve peripheral grants and reset boundaries",
    "[iop][dma][sio2-dma]")
  {
    NekoSystem system;
    auto &bus = system.iopBus();
    auto &dma = system.iopDMAController();
    dma.writePriority(PRIMARY, 0xffffffff);
    dma.writePriority(SECONDARY, 0xffffffff);
    dma.writeAuxiliaryControl(0xffffffff);
    dma.writeInterrupt(PRIMARY, FORCE);
    system.sio2Controller().writeCommandDescriptor(0, 0x12345678);
    REQUIRE(system.sio2Controller().writeTransmitByte(0x61) == SIO2AccessStatus::Accepted);
    SaveStateMutation expected(system.saveState());
    for (std::size_t channel = 0; channel < 2; ++channel)
    {
      REQUIRE(bus.write32(SIO2_MADR[channel], 0x60790 + channel * 4).status ==
        IOPBusStatus::Completed);
      REQUIRE(bus.write32(SIO2_BCR[channel], 0xffffffff - channel).status ==
        IOPBusStatus::Completed);
      const auto path = "system.iop.dmaController.sio2Programming[" +
        std::to_string(channel) + "].";
      expected.writeScalar(path + "memoryAddress", 0x60790 + channel * 4);
      expected.writeScalar(path + "blockControl", 0xffffffff - channel);
      const auto control = channel == 0 ? 0x201u : 0x40000200u;
      REQUIRE(bus.write32(SIO2_CHCR[channel], control).status ==
        IOPBusStatus::Completed);
      expected.writeScalar(path + "channelControl", control);
      for (IOPWord control : {1u, 0x100u, 0x202u, 0x400u, 0x600u,
        0x01000201u, 0x01000200u, 0x41000200u, 0x10000000u, 0x10000201u,
        0x20000201u, 0x40000202u, 0xffffffffu})
      {
        REQUIRE(bus.write32(SIO2_CHCR[channel], control).status ==
          IOPBusStatus::Unmapped);
      }
    }
    expected.refreshChecksum();
    requireState(system, expected.bytes());
    system.runMasterCycles(80);
    bus.reset();
    system.iopCore().reset();
    for (std::size_t channel = 0; channel < 2; ++channel)
    {
      REQUIRE(bus.read32(SIO2_MADR[channel]).value == 0x60790 + channel * 4);
      REQUIRE(bus.read32(SIO2_BCR[channel]).value == 0xffffffff - channel);
      REQUIRE(bus.read32(SIO2_CHCR[channel]).value ==
        (channel == 0 ? 0x201u : 0x40000200u));
    }
    REQUIRE(system.sio2Controller().transmitInputSize() == 1);
    REQUIRE(system.sio2Controller().transmitInputByte(0) == 0x61);
    REQUIRE(dma.interruptStatus(SECONDARY) == 0);
    system.reset();
    for (const auto address : {SIO2_MADR[0], SIO2_MADR[1], SIO2_BCR[0], SIO2_BCR[1],
      SIO2_CHCR[0], SIO2_CHCR[1]})
    {
      REQUIRE(system.iopBus().read32(address).value == 0);
    }
  }

  TEST_CASE("IOP SIO2 DMA programming rejects excluded transports atomically",
    "[iop][dma][sio2-dma][bus]")
  {
    IOPBus detached;
    NekoSystem system;
    auto &bus = system.iopBus();
    for (const auto address : {SIO2_MADR[0], SIO2_MADR[1], SIO2_BCR[0], SIO2_BCR[1],
      SIO2_CHCR[0], SIO2_CHCR[1]})
    {
      const auto value = address == SIO2_CHCR[1] ? 0x40000200u :
        address == SIO2_CHCR[0] ? 0x201u : 0x60790u;
      REQUIRE(bus.write32(address, value).status == IOPBusStatus::Completed);
    }
    const auto before = system.saveState();
    for (const auto address : {SIO2_MADR[0], SIO2_MADR[1], SIO2_BCR[0], SIO2_BCR[1],
      SIO2_CHCR[0], SIO2_CHCR[1]})
    {
      const bool block = address == SIO2_BCR[0] || address == SIO2_BCR[1];
      REQUIRE(detached.read32(address).status == IOPBusStatus::Unmapped);
      REQUIRE(detached.write32(address, 0).status == IOPBusStatus::Unmapped);
      REQUIRE(detached.read16(address + 2).status == IOPBusStatus::Unmapped);
      REQUIRE(detached.write16(address + 2, 0).status == IOPBusStatus::Unmapped);
      for (std::uint8_t mask = 1; mask < 15; ++mask)
      {
        REQUIRE(bus.writeMasked32(address, 0, mask).status == IOPBusStatus::Unmapped);
      }
      for (IOPAddress lane = 0; lane < 4; ++lane)
      {
        REQUIRE(bus.read8(address + lane).status == IOPBusStatus::Unmapped);
        REQUIRE(bus.write8(address + lane, 0).status == IOPBusStatus::Unmapped);
        if (lane % 2 != 0 || !block)
        {
          const auto status = lane % 2 == 0
            ? IOPBusStatus::Unmapped : IOPBusStatus::Misaligned;
          REQUIRE(bus.read16(address + lane).status == status);
          REQUIRE(bus.write16(address + lane, 0).status == status);
        }
        if (lane != 0)
        {
          REQUIRE(bus.read32(address + lane).status == IOPBusStatus::Misaligned);
          REQUIRE(bus.write32(address + lane, 0).status == IOPBusStatus::Misaligned);
        }
      }
      for (const IOPAddress segment : {0u, 0x80000000u, 0xa0000000u})
      {
        REQUIRE(bus.readInstruction32(address | segment).status == IOPBusStatus::Unmapped);
      }
      REQUIRE(bus.readDMAC32(address).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.writeDMAC32(address, 0).status == IOPBusStatus::Unmapped);
      std::uint8_t byte = 0;
      REQUIRE(bus.inspectPhysical(address, &byte, 1) == IOPBusStatus::Unmapped);
      REQUIRE(bus.loadPhysical(address, &byte, 1) == IOPBusStatus::Unmapped);
      std::uint32_t eeValue = 0;
      REQUIRE_FALSE(system.eeBus().readData32(address, &eeValue));
      REQUIRE_FALSE(system.eeBus().writeData32(address, 0));
      REQUIRE(bus.write32(address + 0x100000, 0).status == IOPBusStatus::Unmapped);
    }
    for (const IOPAddress neighbor : {0x1f80153cu, 0x1f80154cu, 0x1f80155cu})
    {
      REQUIRE(bus.read32(neighbor).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write32(neighbor, 0).status == IOPBusStatus::Unmapped);
    }
    requireState(system, before);
  }

  TEST_CASE("IOP SIO2 DMA programmed states restore and reject malformed addresses transactionally",
    "[iop][dma][sio2-dma][save-state]")
  {
    NekoSystem source;
    NekoSystem destination;
    for (std::size_t channel = 0; channel < 2; ++channel)
    {
      REQUIRE(source.iopBus().write32(SIO2_MADR[channel], 0x60790 + channel * 4).status ==
        IOPBusStatus::Completed);
      REQUIRE(source.iopBus().write32(SIO2_BCR[channel], 0xffffffff - channel).status ==
        IOPBusStatus::Completed);
      REQUIRE(destination.iopBus().write32(SIO2_MADR[channel], 0x100 + channel * 4).status ==
        IOPBusStatus::Completed);
      REQUIRE(destination.iopBus().write32(SIO2_BCR[channel], 0x12345678 + channel).status ==
        IOPBusStatus::Completed);
    }
    const auto sourceState = source.saveState();
    const auto before = destination.saveState();
    for (std::size_t channel = 0; channel < 2; ++channel)
    for (IOPWord invalid : {1u, 3u, 0x01000000u, 0xffffffffu})
    {
      SaveStateMutation malformed(sourceState);
      malformed.writeScalar("system.iop.dmaController.sio2Programming[" +
        std::to_string(channel) + "].memoryAddress", invalid);
      malformed.refreshChecksum();
      REQUIRE_THROWS_AS(destination.loadState(malformed.bytes()), std::invalid_argument);
      requireState(destination, before);
    }
    destination.loadState(sourceState);
    requireState(destination, sourceState);
    REQUIRE(destination.iopStateHash() == source.iopStateHash());
    REQUIRE(destination.iopBus().write16(SIO2_BCR[0] + 2, 0).status ==
      IOPBusStatus::Completed);
    REQUIRE(destination.iopBus().read32(SIO2_BCR[0]).value == 0xffff);
    REQUIRE(source.iopBus().read32(SIO2_BCR[0]).value == 0xffffffff);
  }

  TEST_CASE("IOP SIO2 DMA stopped controls restore and validate transactionally",
    "[iop][dma][sio2-dma][sio2-chcr][save-state]")
  {
    NekoSystem source;
    NekoSystem destination;
    constexpr IOPWord controls[] = {0, 0x200, 0x201};
    for (std::size_t index = 0; index < 6; ++index)
    {
      const auto inputControl = controls[index % 3];
      const auto outputControl = SIO2_OUTPUT_STOPPED_CONTROLS[index];
      REQUIRE(source.iopBus().write32(SIO2_CHCR[0], inputControl).status ==
        IOPBusStatus::Completed);
      REQUIRE(source.iopBus().write32(SIO2_CHCR[1], outputControl).status ==
        IOPBusStatus::Completed);
      REQUIRE(destination.iopBus().write32(SIO2_CHCR[0], 0x200).status ==
        IOPBusStatus::Completed);
      REQUIRE(destination.iopBus().write32(SIO2_CHCR[1], 0x201).status ==
        IOPBusStatus::Completed);
      const auto saved = source.saveState();
      destination.loadState(saved);
      requireState(destination, saved);
      REQUIRE(destination.iopStateHash() == source.iopStateHash());
      REQUIRE(destination.iopBus().read32(SIO2_CHCR[0]).value == inputControl);
      REQUIRE(destination.iopBus().read32(SIO2_CHCR[1]).value == outputControl);
    }
    const auto saved = source.saveState();
    REQUIRE(destination.iopBus().write32(SIO2_CHCR[0], 0).status == IOPBusStatus::Completed);
    const auto before = destination.saveState();
    for (std::size_t channel = 0; channel < 2; ++channel)
    for (IOPWord invalid : {1u, 0x01000201u, 0x10000201u, 0x41000200u, 0x40000202u})
    {
      SaveStateMutation malformed(saved);
      malformed.writeScalar("system.iop.dmaController.sio2Programming[" +
        std::to_string(channel) + "].channelControl", invalid);
      malformed.refreshChecksum();
      REQUIRE_THROWS_AS(destination.loadState(malformed.bytes()), std::invalid_argument);
      requireState(destination, before);
    }
    for (const auto invalid : {0x40000000u, 0x40000200u, 0x40000201u})
    {
      SaveStateMutation malformed(saved);
      malformed.writeScalar("system.iop.dmaController.sio2Programming[0].channelControl",
        invalid);
      malformed.refreshChecksum();
      REQUIRE_THROWS_AS(destination.loadState(malformed.bytes()), std::invalid_argument);
      requireState(destination, before);
    }
  }

  TEST_CASE("IOP SIO2 DMA stopped guest controls restore branch and load continuations",
    "[iop][dma][sio2-dma][sio2-chcr][save-state][trace]")
  {
    namespace Memory = NekoIOPTraceMemory;
    for (const auto physical : SIO2_CHCR)
    for (const IOPAddress segment : {0u, 0x80000000u, 0xa0000000u})
    for (const std::uint64_t cut : {40u, 56u})
    {
      const auto target = physical | segment;
      CAPTURE(target);
      CAPTURE(cut);
      const IOPWord flags = physical == SIO2_CHCR[1] ? 0x40000000 : 0;
      NekoSystem original;
      const IOPWord program[] = {
        0x3c010000 | (target >> 16), 0x34210000 | (target & 0xffff),
        0x3c020000 | (flags >> 16), 0x34420201,
        0x10000002, 0xac220000,
        0xffffffff, 0x8c230000,
        0x2444ffff, 0xac240000, 0x8c250000, 0,
        0x24060001, 0xac260000
      };
      IOPAddress address = 0;
      for (const auto word : program)
      {
        REQUIRE(original.iopBus().write32(address, word).status ==
          IOPBusStatus::Completed);
        address += 4;
      }
      original.iopCore().startExecution(0xa0000000);
      original.runMasterCycles(cut);
      REQUIRE(original.iopCore().branchContinuation().active == (cut == 40));
      REQUIRE(original.iopCore().delayedResultContinuation().source ==
        (cut == 56 ? IOPDelayedResultSource::Load : IOPDelayedResultSource::None));
      const auto checkpoint = original.saveState();
      NekoSystem restored;
      NekoSystem plain;
      restored.loadState(checkpoint);
      plain.loadState(checkpoint);
      original.startTrace();
      restored.startTrace();
      original.runMasterCycles(112 - cut);
      restored.runMasterCycles(112 - cut);
      plain.runMasterCycles(112 - cut);
      const auto &core = original.iopCore();
      REQUIRE(core.stopReason() == IOPStopReason::StoreFailure);
      REQUIRE(core.exception().kind == IOPException::None);
      REQUIRE(core.retiredInstructions() == 12);
      REQUIRE(core.programCounter() == 0xa0000034);
      REQUIRE(core.generalRegister(3) == (flags | 0x201));
      REQUIRE(core.generalRegister(5) == (flags | 0x200));
      REQUIRE(core.storeFailure().access.virtualAddress == target);
      REQUIRE(core.storeFailure().access.physicalAddress == physical);
      REQUIRE(original.iopBus().read32(physical).value == (flags | 0x200));
      const auto other = physical == SIO2_CHCR[0] ? SIO2_CHCR[1] : SIO2_CHCR[0];
      REQUIRE(original.iopBus().read32(other).value == 0);
      if (cut == 40)
      {
        const auto firstStore = std::find_if(original.trace().begin(), original.trace().end(),
          [target](const NekoTraceEvent &event)
          {
            return event.type == NekoTraceEventType::MemoryAccess &&
              event.value0 == target && event.masterCycle == 41;
          });
        REQUIRE(firstStore != original.trace().end());
        REQUIRE(firstStore->value1 == (flags | 0x201));
        REQUIRE(firstStore->value3 == nekoPackIOPMemoryTraceMetadata({
          physical, 4, Memory::AccessKind::DataStore, Memory::Outcome::Completed,
          segment == 0xa0000000 ? Memory::CacheRoute::Uncached : Memory::CacheRoute::Cached,
          true, 15}));
      }
      IOPTraceTest::requireMachine(original, restored);
      IOPTraceTest::requireMachine(original, plain);
      IOPTraceTest::requireEvents(original.trace(), restored.trace());
      REQUIRE(original.saveState() == restored.saveState());
      REQUIRE(original.saveState() == plain.saveState());
      REQUIRE(plain.trace().empty());
    }
  }

  TEST_CASE("IOP SIO2 DMA guest programming restores branch and load continuations",
    "[iop][dma][sio2-dma][save-state][trace]")
  {
    namespace Memory = NekoIOPTraceMemory;
    for (const auto physical : SIO2_MADR)
    for (const IOPAddress segment : {0u, 0x80000000u, 0xa0000000u})
    for (const std::uint64_t cut : {40u, 56u})
    {
      const auto target = physical | segment;
      CAPTURE(target);
      CAPTURE(cut);
      NekoSystem original;
      const IOPWord program[] = {
        0x3c010000 | (target >> 16), 0x34210000 | (target & 0xffff),
        0x3c02ff06, 0x34420790,
        0x10000002, 0xac220000, // SW in the branch delay slot
        0xffffffff, 0x8c230000,
        0xa4220004, 0xa4220006,
        0xa8220007, 0xb8220004, // full-mask SWL/SWR to BCR
        0x94240006, 0x8c250004, 0,
        0xa8220005 // partial-mask SWL must fail without replacing BCR
      };
      IOPAddress address = 0;
      for (const auto word : program)
      {
        REQUIRE(original.iopBus().write32(address, word).status ==
          IOPBusStatus::Completed);
        address += 4;
      }
      original.iopCore().startExecution(0xa0000000);
      original.runMasterCycles(cut);
      REQUIRE(original.iopCore().branchContinuation().active == (cut == 40));
      REQUIRE(original.iopCore().delayedResultContinuation().source ==
        (cut == 56 ? IOPDelayedResultSource::Load : IOPDelayedResultSource::None));
      const auto checkpoint = original.saveState();
      NekoSystem restored;
      NekoSystem plain;
      restored.loadState(checkpoint);
      plain.loadState(checkpoint);
      original.startTrace();
      restored.startTrace();
      original.runMasterCycles(128 - cut);
      restored.runMasterCycles(128 - cut);
      plain.runMasterCycles(128 - cut);
      const auto &core = original.iopCore();
      REQUIRE(core.stopReason() == IOPStopReason::StoreFailure);
      REQUIRE(core.exception().kind == IOPException::None);
      REQUIRE(core.retiredInstructions() == 14);
      REQUIRE(core.programCounter() == 0xa000003c);
      REQUIRE(core.generalRegister(3) == 0x60790);
      REQUIRE(core.generalRegister(4) == 0xff06);
      REQUIRE(core.generalRegister(5) == 0xff060790);
      REQUIRE(core.storeFailure().access.virtualAddress == target + 5);
      REQUIRE(core.storeFailure().access.physicalAddress == physical + 5);
      REQUIRE(core.storeFailure().mergeByteEnable == 3);
      REQUIRE(original.iopBus().read32(physical).value == 0x60790);
      REQUIRE(original.iopBus().read32(physical + 4).value == 0xff060790);
      const auto other = physical == SIO2_MADR[0] ? SIO2_MADR[1] : SIO2_MADR[0];
      REQUIRE(original.iopBus().read32(other).value == 0);
      REQUIRE(original.iopBus().read32(other + 4).value == 0);
      if (cut == 40)
      {
        const auto firstStore = std::find_if(original.trace().begin(), original.trace().end(),
          [target](const NekoTraceEvent &event)
          {
            return event.type == NekoTraceEventType::MemoryAccess &&
              event.value0 == target && event.masterCycle == 41;
          });
        REQUIRE(firstStore != original.trace().end());
        REQUIRE(firstStore->value1 == 0xff060790);
        REQUIRE(firstStore->value3 == nekoPackIOPMemoryTraceMetadata({
          physical, 4, Memory::AccessKind::DataStore, Memory::Outcome::Completed,
          segment == 0xa0000000 ? Memory::CacheRoute::Uncached : Memory::CacheRoute::Cached,
          true, 15}));
      }
      IOPTraceTest::requireMachine(original, restored);
      IOPTraceTest::requireMachine(original, plain);
      IOPTraceTest::requireEvents(original.trace(), restored.trace());
      REQUIRE(original.saveState() == restored.saveState());
      REQUIRE(original.saveState() == plain.saveState());
      REQUIRE(plain.trace().empty());
    }
  }

  TEST_CASE("IOP SPU diagnostic beats identify both source channels and target positions",
    "[spu-diagnostics][iop][trace]")
  {
    for (std::size_t core : {0u, 1u})
    {
      CAPTURE(core);
      NekoSystem system;
      SPU2InputTest::programProfile(system.spu2Controller());
      REQUIRE(system.spu2Controller().writeAutoDMAWriteSelection(core, 1u << core));
      REQUIRE(system.iopBus().write32(SPU_BCR[core], 0x00100010).status ==
        IOPBusStatus::Completed);
      REQUIRE(system.iopBus().write32(SPU_MADR[core], 0x1000).status ==
        IOPBusStatus::Completed);
      REQUIRE(system.iopBus().write32(0x1000, 0x12345678).status ==
        IOPBusStatus::Completed);
      REQUIRE(system.iopBus().write32(SPU_CHCR[core], 0x01000201).status ==
        IOPBusStatus::Completed);
      system.iopDMAController().writePriority(core == 0 ? PRIMARY : SECONDARY,
        core == 0 ? 0x80000 : 8);
      system.startTrace();
      system.clockMasterCycle();
      const auto events = IOPTraceTest::subsystemEvents(
        system, static_cast<NekoTraceSubsystem>(16));
      REQUIRE(events.size() == 1);
      REQUIRE(events[0].type == NekoTraceEventType::DMABeat);
      REQUIRE(events[0].masterCycle == 1);
      REQUIRE(events[0].value0 == (core == 0 ? 4u : 7u) + (core << 8));
      REQUIRE(events[0].value1 == 0x1000);
      REQUIRE(events[0].value2 == (UINT64_C(255) << 32 | 256));
      REQUIRE(events[0].value3 == 1); // Captured free half, first word.
    }
  }

  TEST_CASE("IOP SPU failure probes retain source rejection without CPU attribution",
    "[spu-diagnostics][iop][startup-failure]")
  {
    for (std::size_t core : {0u, 1u})
    {
      CAPTURE(core);
      NekoSystem system;
      SPU2InputTest::programProfile(system.spu2Controller());
      REQUIRE(system.spu2Controller().writeAutoDMAWriteSelection(core, 1u << core));
      REQUIRE(system.iopBus().write32(SPU_BCR[core], 0x00100010).status ==
        IOPBusStatus::Completed);
      REQUIRE(system.iopBus().write32(SPU_MADR[core], 0x00900000).status ==
        IOPBusStatus::Completed);
      REQUIRE(system.iopBus().write32(SPU_CHCR[core], 0x01000201).status ==
        IOPBusStatus::Completed);
      system.iopCore().startExecution(0xa0000000);
      system.startFailureProbe();
      system.runMasterCycles(8);
      REQUIRE(system.iopCore().retiredInstructions() == 1);
      system.iopDMAController().writePriority(core == 0 ? PRIMARY : SECONDARY,
        core == 0 ? 0x80000 : 8);
      const auto checkpoint = system.saveState();
      REQUIRE_THROWS_AS(system.clockMasterCycle(), IOPDMATransferError);
      REQUIRE(system.iopCore().retiredInstructions() == 1);
      for (const auto &event : system.trace())
        REQUIRE_FALSE((event.subsystem == NekoTraceSubsystem::IOP &&
          event.type == NekoTraceEventType::InstructionIssued));
      const auto failure = firstStartupFailure(system.trace());
      REQUIRE(failure.found);
      REQUIRE(failure.kind == StartupFailureKind::DMARejection);
      REQUIRE(failure.event.subsystem == static_cast<NekoTraceSubsystem>(16));
      REQUIRE(failure.event.value1 == 0x00900000);
      REQUIRE(failure.event.value2 == (UINT64_C(256) << 32 | 256));
      REQUIRE_FALSE(failure.instruction.available);
      REQUIRE(SPU2ObservationTestAccess::detached(system.spu2Controller()));
      std::ostringstream output;
      writeStartupFirstFailureJsonLine(
        output, system, StartupExecutionBoundary::IncompleteCycle);
      REQUIRE(output.str().find("\"cpu\":null") != std::string::npos);
      REQUIRE(output.str().find("\"endpoint\":\"iop_spu" + std::to_string(core) +
        "\"") != std::string::npos);
      REQUIRE(output.str().find("\"failure\":\"source_read\"") != std::string::npos);
      REQUIRE(output.str().find("\"bus_status\":\"unmapped\"") != std::string::npos);
      const auto ending = system.saveState();
      for (const bool traced : {false, true})
      {
        CAPTURE(traced);
        NekoSystem restored;
        restored.loadState(checkpoint);
        const auto capture = std::make_shared<StartupFailureCapture>();
        if (traced)
          restored.startTrace(0, capture, NekoTraceStopPolicy::FirstFailure);
        REQUIRE_THROWS_AS(restored.clockMasterCycle(), IOPDMATransferError);
        REQUIRE(restored.saveState() == ending);
        REQUIRE(SPU2ObservationTestAccess::detached(restored.spu2Controller()));
        if (traced)
        {
          const auto streamedFailure = firstStartupFailure(capture->events());
          REQUIRE(streamedFailure.found);
          REQUIRE(streamedFailure.event.value0 == failure.event.value0);
          REQUIRE(streamedFailure.event.value1 == failure.event.value1);
          REQUIRE(streamedFailure.event.masterCycle == failure.event.masterCycle);
          REQUIRE_FALSE(streamedFailure.instruction.available);
          REQUIRE(restored.trace().empty());
          REQUIRE(restored.traceTruncated());
        }
      }
    }
  }

  TEST_CASE("IOP SPU diagnostics bound waits and order buffer release and terminal IRQ",
    "[spu-diagnostics][iop][trace]")
  {
    for (std::size_t core : {0u, 1u})
    {
      CAPTURE(core);
      NekoSystem system;
      auto &spu = system.spu2Controller();
      auto &dma = system.iopDMAController();
      SPU2InputTest::programProfile(spu);
      REQUIRE(spu.writeAutoDMAWriteSelection(core, 1u << core));
      REQUIRE(dma.writeSPUBlockControl(core, 0x00200010) == IOPDMAWriteOutcome::Completed);
      REQUIRE(dma.writeSPUMemoryAddress(core, 0x1000) == IOPDMAWriteOutcome::Completed);
      REQUIRE(dma.startSPUTransfer(core, system.iopBus(), spu) == IOPDMAWriteOutcome::Completed);
      dma.writePriority(core == 0 ? PRIMARY : SECONDARY, core == 0 ? 0x80000 : 8);
      REQUIRE(system.iopBus().write32(core == 0 ? DICR : DICR2,
        (1u << 23) | (1u << (16 + (core == 0 ? 4 : 0)))).status ==
        IOPBusStatus::Completed);
      for (unsigned word = 0; word < 255; ++word)
        REQUIRE(dma.clockSPU(core, system.iopBus(), spu).outcome ==
          IOPDMABeatOutcome::Accepted);
      system.startTrace();
      system.clockMasterCycle();
      system.runMasterCycles(80);
      const auto initial = IOPTraceTest::subsystemEvents(system, NekoTraceSubsystem::SPU2);
      REQUIRE(initial.size() == 3);
      REQUIRE(initial[0].type == NekoTraceEventType::DMABeat);
      REQUIRE(nekoDecodeSPUDMABeat(initial[0]).targetWord == 255);
      REQUIRE(initial[1].type == NekoTraceEventType::StateChanged);
      const auto full = nekoDecodeSPUInput(initial[1]);
      REQUIRE(full.before.requested);
      REQUIRE_FALSE(full.after.requested);
      REQUIRE(full.after.fillWords == 256);
      REQUIRE(nekoDecodeSPUDMABeat(initial[2]).outcome == NekoSPUTrace::Outcome::Wait);
      REQUIRE(nekoDecodeSPUDMABeat(initial[2]).targetWord == 256);
      REQUIRE((system.iopInterruptController().status() & DMAC_CAUSE) == 0);

      SPU2InputTest::advanceSamples(spu, 254);
      while (!spu.advanceInputClock().sampleTick) {}
      system.runMasterCycles(6144);
      const auto released = IOPTraceTest::subsystemEvents(system, NekoTraceSubsystem::SPU2);
      REQUIRE(released.size() == 5);
      const auto switched = nekoDecodeSPUInput(released[3]);
      REQUIRE(switched.before.fillHalf == 1);
      REQUIRE(switched.after.fillHalf == 0);
      REQUIRE(switched.after.requested);
      REQUIRE(switched.after.fillWords == 0);
      REQUIRE(released[3].masterCycle == released[4].masterCycle);
      REQUIRE(nekoDecodeSPUDMABeat(released[4]).targetHalf == 0);
      REQUIRE(nekoDecodeSPUDMABeat(released[4]).targetWord == 0);
      system.runMasterCycles(2040);
      REQUIRE_FALSE(dma.spuActive(core));
      REQUIRE((system.iopInterruptController().status() & DMAC_CAUSE) != 0);
      REQUIRE((system.iopInterruptController().status() & (1u << 9)) == 0);
      const auto &events = system.trace();
      std::size_t completion = events.size();
      std::size_t close = events.size();
      std::size_t interrupt = events.size();
      for (std::size_t index = 0; index < events.size(); ++index)
      {
        const auto &event = events[index];
        if (event.subsystem == NekoTraceSubsystem::SPU2)
        {
          if (event.type == NekoTraceEventType::DMABeat &&
              nekoDecodeSPUDMABeat(event).outcome == NekoSPUTrace::Outcome::Completed)
          {
            completion = index;
            REQUIRE(nekoDecodeSPUDMABeat(event).remainingBefore == 1);
            REQUIRE(nekoDecodeSPUDMABeat(event).remainingAfter == 0);
          }
          if (event.type == NekoTraceEventType::StateChanged &&
              !nekoDecodeSPUInput(event).after.writeOpen)
            close = index;
        }
        if (event.subsystem == NekoTraceSubsystem::IOPInterruptController)
          interrupt = index;
      }
      REQUIRE(completion < close);
      REQUIRE(close < interrupt);
      REQUIRE(interrupt < events.size());
      REQUIRE(SPU2ObservationTestAccess::detached(spu));
    }
  }

  TEST_CASE("IOP SPU observation links and restored bounded streams remain diagnostic only",
    "[spu-diagnostics][iop][trace][persistence]")
  {
    NekoSystem source;
    auto &spu = source.spu2Controller();
    SPU2InputTest::programProfile(spu);
    REQUIRE(spu.writeAutoDMAWriteSelection(0, 1));
    REQUIRE(source.iopBus().write32(SPU_BCR[0], 0x00100010).status ==
      IOPBusStatus::Completed);
    REQUIRE(source.iopBus().write32(SPU_CHCR[0], 0x01000201).status ==
      IOPBusStatus::Completed);
    source.iopDMAController().writePriority(PRIMARY, 0x80000);
    source.runMasterCycles(17);
    const auto checkpoint = source.saveState();
    NekoObservationOrder order;
    SPU2ObservationTestAccess::enable(spu, &order);
    REQUIRE(spu.writeAutoDMAWriteSelection(1, 2));
    const auto count = SPU2ObservationTestAccess::count(spu);
    REQUIRE(count == 2);
    REQUIRE_THROWS(source.loadState({}));
    REQUIRE(SPU2ObservationTestAccess::attached(spu, &order));
    REQUIRE(SPU2ObservationTestAccess::count(spu) == count);
    source.loadState(checkpoint);
    REQUIRE(SPU2ObservationTestAccess::attached(spu, &order));
    REQUIRE(SPU2ObservationTestAccess::count(spu) == 0);
    REQUIRE(source.saveState() == checkpoint);
    SPU2ObservationTestAccess::disable(spu);
    source.startTrace();
    source.runMasterCycles(40);
    const auto reference = source.trace();
    REQUIRE_FALSE(reference.empty());
    const auto ending = source.saveState();
    for (const unsigned mode : {0u, 1u, 2u, 3u})
    {
      CAPTURE(mode);
      NekoSystem restored;
      restored.loadState(checkpoint);
      const auto capture = std::make_shared<StartupFailureCapture>();
      if (mode == 1)
        restored.startTrace();
      else if (mode >= 2)
        restored.startTrace(mode == 2 ? 1 : 0, capture);
      restored.runMasterCycles(40);
      REQUIRE(restored.saveState() == ending);
      REQUIRE(SPU2ObservationTestAccess::detached(restored.spu2Controller()));
      if (mode == 1)
        IOPTraceTest::requireEvents(restored.trace(), reference);
      else if (mode >= 2)
      {
        IOPTraceTest::requireEvents(capture->events(), reference);
        REQUIRE(restored.trace().size() == (mode == 2 ? 1 : 0));
        REQUIRE(restored.traceTruncated());
      }
      else
        REQUIRE(restored.trace().empty());
    }
  }

  TEST_CASE("IOP SPU guest input transitions precede their CPU stores without polling noise",
    "[spu-diagnostics][iop][trace]")
  {
    for (std::size_t core : {0u, 1u})
    {
      CAPTURE(core);
      NekoSystem system;
      SPU2InputTest::programProfile(system.spu2Controller());
      REQUIRE(system.iopBus().write32(SPU_BCR[core], 0x00100010).status ==
        IOPBusStatus::Completed);
      const auto admas = static_cast<IOPWord>(0x1b0 + core * 0x400);
      const IOPWord program[] = {
        0x3c01bf90, 0x34210000 | admas, 0x24020000 | (1u << core), 0xa4220000,
        0x3c01bf80, 0x34210000 | (SPU_CHCR[core] & 0xffff),
        0x3c020100, 0x34420201, 0xac220000, 0xac200000,
        0x3c01bf90, 0x34210000 | admas, 0xa4200000
      };
      for (std::size_t word = 0; word < sizeof(program) / sizeof(program[0]); ++word)
        REQUIRE(system.iopBus().write32(static_cast<IOPAddress>(word * 4),
          program[word]).status == IOPBusStatus::Completed);
      system.iopCore().startExecution(0xa0000000);
      system.startTrace();
      system.runMasterCycles(97);
      REQUIRE(system.iopCore().retiredInstructions() == 13);
      REQUIRE(system.iopCore().stopReason() == IOPStopReason::None);
      const auto changes = IOPTraceTest::subsystemEvents(system, NekoTraceSubsystem::SPU2);
      REQUIRE(changes.size() == 4);
      REQUIRE(nekoDecodeSPUInput(changes[0]).after.selected);
      REQUIRE(nekoDecodeSPUInput(changes[1]).after.requested);
      REQUIRE_FALSE(nekoDecodeSPUInput(changes[2]).after.writeOpen);
      REQUIRE_FALSE(nekoDecodeSPUInput(changes[3]).after.selected);
      for (std::size_t index = 0; index < system.trace().size(); ++index)
      {
        const auto &event = system.trace()[index];
        if (event.subsystem != NekoTraceSubsystem::SPU2)
          continue;
        bool storeAfter = false;
        for (std::size_t after = index + 1; after < system.trace().size(); ++after)
        {
          const auto &next = system.trace()[after];
          if (next.masterCycle != event.masterCycle)
            break;
          if (next.subsystem == NekoTraceSubsystem::IOP &&
              next.type == NekoTraceEventType::MemoryAccess)
            storeAfter = true;
        }
        REQUIRE(storeAfter);
      }
      system.iopBus().read32(SPU_CHCR[core]);
      REQUIRE(system.trace().size() > changes.size());
      REQUIRE(IOPTraceTest::subsystemEvents(system, NekoTraceSubsystem::SPU2).size() == 4);
    }
  }

  void requireInitializationStore(
    IOPAddress physical,
    IOPAddress target,
    IOPWord value,
    IOPWord store,
    IOPStopReason expectedStop)
  {
    CAPTURE(target);
    CAPTURE(value);
    CAPTURE(store);
    IOPInterruptController interrupts;
    IOPDMAController dma(interrupts);
    IOPSIF1DMAChannel channel;
    IOPBus bus;
    bus.attachDMA(&dma, &channel);
    IOPCore core;
    core.attachBus(&bus);
    const IOPWord program[] = {
      0x3c010000 | (target >> 16),
      0x34210000 | (target & 0xffff),
      0x24020000 | value,
      0x10000002, // beq r0, r0, +2
      store
    };
    IOPAddress address = 0;
    for (IOPWord word : program)
    {
      REQUIRE(bus.write32(address, word).status == IOPBusStatus::Completed);
      address += 4;
    }
    core.startExecution(0xa0000000);
    for (unsigned step = 0; step < 5; ++step)
    {
      core.stepInstruction();
    }
    const bool completed = expectedStop == IOPStopReason::None;
    REQUIRE(core.exception().kind == IOPException::None);
    REQUIRE(core.generalRegister(2) == value);
    REQUIRE(core.retiredInstructions() == (completed ? 5 : 4));
    REQUIRE(core.programCounter() == (completed ? 0xa0000018 : 0xa0000010));
    REQUIRE(core.branchContinuation().active == !completed);
    REQUIRE(core.stopReason() == expectedStop);
    if (!completed)
    {
      REQUIRE(core.storeFailure().access.virtualAddress == target + (store & 3));
      REQUIRE(core.storeFailure().access.physicalAddress == physical + (store & 3));
      REQUIRE(core.storeFailure().access.outcome == IOPAccessOutcome::Unmapped);
    }
    core.startExecution(target);
    REQUIRE(core.fetchInstruction().outcome == IOPAccessOutcome::Unmapped);
  }
}

namespace
{
  using SPUGuest = std::vector<IOPWord>;
  constexpr IOPWord GUEST_SW = 0xac220000;
  constexpr IOPWord GUEST_SH = 0xa4220000;
  constexpr IOPWord GUEST_BNE = 0x14640000;
  constexpr IOPWord GUEST_BEQ = 0x10640000;
  constexpr IOPWord GUEST_DONE = 0x600dcafe;

  void guestImmediate(SPUGuest &program, unsigned reg, IOPWord value)
  {
    program.push_back(0x3c000000 | (reg << 16) | (value >> 16));
    program.push_back(0x34000000 | (reg << 21) | (reg << 16) | (value & 0xffff));
  }

  void guestStore(SPUGuest &program, IOPAddress address, IOPWord value,
    IOPWord opcode = GUEST_SW)
  {
    guestImmediate(program, 1, address | 0xa0000000);
    guestImmediate(program, 2, value);
    program.push_back(opcode);
  }

  void guestReadback(SPUGuest &program, IOPAddress address, IOPAddress result)
  {
    guestImmediate(program, 1, address | 0xa0000000);
    program.push_back(0x8c230000); // lw r3, 0(r1)
    program.push_back(0);
    guestImmediate(program, 1, result | 0xa0000000);
    program.push_back(0xac230000);
  }

  std::size_t guestSPUStart(SPUGuest &program, std::size_t core)
  {
    guestStore(program, SPU_CHCR[core], 0x01000201);
    program.back() = 0x10000001; // Start in the branch delay slot.
    const auto delaySlot = program.size();
    program.push_back(GUEST_SW);
    return delaySlot;
  }

  void guestPoll(SPUGuest &program, IOPAddress address, IOPWord value,
    IOPWord branch)
  {
    guestImmediate(program, 1, address | 0xa0000000);
    guestImmediate(program, 4, value);
    program.push_back(0x8c230000);
    program.push_back(0);
    program.push_back(branch | 0xfffd); // Back to lw, with its load delay.
    program.push_back(0);
  }

  void guestSPUProfile(SPUGuest &program, IOPWord blocks)
  {
    const IOPAddress spdif[] = {
      0x1f9007c6, 0x1f9007c8, 0x1f9007cc, 0x1f9007ca, 0x1f9007c0
    };
    for (std::size_t index = 0; index < SPU2InputTest::configuration.size(); ++index)
      guestStore(program, spdif[index], SPU2InputTest::configuration[index], GUEST_SH);
    for (std::size_t core = 0; core < 2; ++core)
    {
      guestStore(program, static_cast<IOPAddress>(0x1f90019a + core * 0x400),
        0xc001, GUEST_SH);
      guestStore(program, static_cast<IOPAddress>(0x1f9001b0 + core * 0x400),
        1u << core, GUEST_SH);
      guestStore(program, SPU_MADR[core],
        static_cast<IOPAddress>(0x4000 + core * 0x2000));
      guestStore(program, SPU_BCR[core], 16, GUEST_SH);
      guestStore(program, SPU_BCR[core] + 2, blocks, GUEST_SH);
    }
    guestStore(program, DICR, 0x00900000);
    guestStore(program, DICR2, 0x00810000);
  }

  void guestSPUFinish(SPUGuest &program)
  {
    for (std::size_t core = 0; core < 2; ++core)
    {
      guestPoll(program, SPU_CHCR[core], 0x201, GUEST_BNE);
      guestReadback(program, SPU_MADR[core],
        static_cast<IOPAddress>(0x1200 + core * 8));
      guestReadback(program, SPU_BCR[core],
        static_cast<IOPAddress>(0x1204 + core * 8));
    }
    guestStore(program, 0x1000, GUEST_DONE);
  }

  IOPWord guestPayload(std::size_t core, unsigned word)
  {
    return 0x51000000 | (static_cast<IOPWord>(core) << 20) | word;
  }

  IOPWord restartPayload(std::size_t core, unsigned word)
  {
    return 0x72000000 | (static_cast<IOPWord>(core) << 20) | word;
  }

  void installSPUGuest(NekoSystem &system, const SPUGuest &program)
  {
    auto &bus = system.iopBus();
    for (std::size_t word = 0; word < program.size(); ++word)
      REQUIRE(bus.write32(static_cast<IOPAddress>(word * 4), program[word]).status ==
        IOPBusStatus::Completed);
    for (std::size_t core = 0; core < 2; ++core)
    {
      for (unsigned word = 0; word < 512; ++word)
        REQUIRE(bus.write32(static_cast<IOPAddress>(0x4000 + core * 0x2000 + word * 4),
          guestPayload(core, word)).status == IOPBusStatus::Completed);
      for (unsigned word = 0; word < 256; ++word)
        REQUIRE(bus.write32(static_cast<IOPAddress>(0xc000 + core * 0x2000 + word * 4),
          restartPayload(core, word)).status == IOPBusStatus::Completed);
    }
    system.iopCore().startExecution(0xa0000000);
  }

  void installSPUSIFPeers(NekoSystem &system)
  {
    const IOPWord descriptor[] = {0x80003000, 4, 0x70000001, 0x10000, 0, 0};
    for (unsigned index = 0; index < 6; ++index)
      REQUIRE(system.iopBus().write32(0x2000 + index * 4, descriptor[index]).status ==
        IOPBusStatus::Completed);
    for (unsigned index = 0; index < 4; ++index)
      REQUIRE(system.iopBus().write32(0x3000 + index * 4, index + 1).status ==
        IOPBusStatus::Completed);
    REQUIRE(system.eeBus().writeQuadword(0x12000, {{0x8000a000, 4, 0, 0}}));
    REQUIRE(system.eeBus().writeQuadword(0x12010, {{9, 10, 11, 12}}));
    system.sif0DMAC().writeMemoryAddress(0x10000);
    system.sif0DMAC().writeQuadwordCount(0);
    system.sif0DMAC().writeChannelControl(
      DMACChannelControl::START | DMACChannelControl::CHAIN_MODE);
    system.sif1DMAC().writeMemoryAddress(0x12000);
    system.sif1DMAC().writeQuadwordCount(2);
    system.sif1DMAC().writeChannelControl(DMACChannelControl::START);
    system.dmacController().writeControl(1);
  }

  void requireSPUGuestContinuation(NekoSystem &source, std::uint64_t cycles)
  {
    const auto checkpoint = source.saveState();
    source.startTrace();
    source.runMasterCycles(cycles);
    const auto reference = source.trace();
    REQUIRE_FALSE(reference.empty());
    for (const unsigned mode : {0u, 1u, 2u, 3u})
    {
      CAPTURE(mode);
      NekoSystem restored;
      restored.loadState(checkpoint);
      const auto capture = std::make_shared<StartupFailureCapture>();
      if (mode == 1)
        restored.startTrace();
      else if (mode >= 2)
        restored.startTrace(mode == 2 ? 1 : 0, capture);
      restored.runMasterCycles(cycles);
      IOPTraceTest::requireMachine(restored, source);
      if (mode == 1)
        IOPTraceTest::requireEvents(restored.trace(), reference);
      else if (mode >= 2)
      {
        IOPTraceTest::requireEvents(capture->events(), reference);
        REQUIRE(restored.trace().size() == (mode == 2 ? 1 : 0));
        REQUIRE(restored.traceTruncated());
      }
      else
        REQUIRE(restored.trace().empty());
    }
    source.stopTrace();
  }
}

TEST_CASE("Authored IOP SPU guest restores paired completion across full buffers and SIF contention",
  "[iop][spu-conformance][save-state][trace]")
{
  SPUGuest program;
  guestSPUProfile(program, 32);
  guestStore(program, 0x1f80152c, 0x2000);
  guestStore(program, 0x1f801528, 0x01000401);
  guestStore(program, CHCR, START);
  const auto firstStart = guestSPUStart(program, 0);
  guestSPUStart(program, 1);
  guestStore(program, DPCR, 0x80000);
  guestStore(program, DPCR2, 0x8808);
  guestSPUStart(program, 0);
  guestSPUStart(program, 1);
  guestPoll(program, 0x1f801528, 0x401, GUEST_BNE);
  guestPoll(program, CHCR, 0x400, GUEST_BNE);
  // SIF's existing completion policy asserts DMAC independently of its masks.
  guestStore(program, DICR2, 0x0c810000);
  guestStore(program, 0x1f801070, 0xfffffff7);
  guestSPUFinish(program);
  program.push_back(0x1000ffff);
  program.push_back(0);
  NekoSystem source;
  installSPUGuest(source, program);
  installSPUSIFPeers(source);
  source.runMasterCycles(firstStart * 8);
  REQUIRE(source.iopCore().branchContinuation().active);
  REQUIRE_FALSE(source.iopDMAController().spuActive(0));
  requireSPUGuestContinuation(source, 33);
  REQUIRE(source.iopDMAController().spuActive(0));
  source.startTrace();
  source.runMasterCycles(20000 - source.masterClockScheduler().currentCycle());
  std::array<unsigned, 2> accepted{{0, 0}};
  std::array<bool, 2> sifCompleted{{false, false}};
  std::vector<unsigned> grants;
  for (const auto &event : source.trace())
  {
    if (event.subsystem == NekoTraceSubsystem::SPU2 &&
        event.type == NekoTraceEventType::DMABeat)
    {
      const auto beat = nekoDecodeSPUDMABeat(event);
      if (beat.outcome == NekoSPUTrace::Outcome::Accepted)
      {
        ++accepted[beat.core];
        grants.push_back(beat.channel);
      }
    }
    if (event.subsystem == NekoTraceSubsystem::SIF &&
        event.type == NekoTraceEventType::DMABeat)
    {
      const auto endpoint = static_cast<NekoSIFDMATrace::Endpoint>(event.value0 & 255);
      const auto outcome = static_cast<NekoSIFDMATrace::Outcome>(
        (event.value0 >> NekoSIFDMATrace::OUTCOME_SHIFT) & 255);
      if (endpoint == NekoSIFDMATrace::Endpoint::IOPSIF0 ||
          endpoint == NekoSIFDMATrace::Endpoint::IOP)
      {
        if (outcome == NekoSIFDMATrace::Outcome::Completed)
          sifCompleted[endpoint == NekoSIFDMATrace::Endpoint::IOPSIF0 ? 0 : 1] = true;
        if (outcome == NekoSIFDMATrace::Outcome::Accepted ||
            outcome == NekoSIFDMATrace::Outcome::Completed)
          grants.push_back(endpoint == NekoSIFDMATrace::Endpoint::IOPSIF0 ? 9 : 10);
      }
    }
  }
  REQUIRE(accepted[0] == 256);
  REQUIRE(accepted[1] == 256);
  REQUIRE(sifCompleted[0]);
  REQUIRE(sifCompleted[1]);
  const auto firstPairedGrant = std::find(grants.begin(), grants.end(), 7);
  REQUIRE(std::distance(firstPairedGrant, grants.end()) >= 4);
  const std::vector<unsigned> firstRotation{7, 9, 10, 4};
  REQUIRE(std::vector<unsigned>(firstPairedGrant, firstPairedGrant + 4) ==
    firstRotation);
  for (std::size_t core = 0; core < 2; ++core)
  {
    REQUIRE(source.iopDMAController().spuRemainingWords(core) == 256);
    const auto input = source.spu2Controller().inputWriteState(core);
    REQUIRE(input.fillHalf == 1);
    REQUIRE(input.fillWords == 256);
    REQUIRE_FALSE(input.requested);
  }
  REQUIRE((source.iopInterruptController().status() & DMAC_CAUSE) == 0);
  REQUIRE((source.iopDMAController().interruptStatus(PRIMARY) & 0x10000000) == 0);
  REQUIRE((source.iopDMAController().interruptStatus(SECONDARY) & 0x01000000) == 0);
  const auto blockedCheckpoint = source.saveState();
  const auto blockedCycle = source.masterClockScheduler().currentCycle();
  requireSPUGuestContinuation(source, 48);
  constexpr std::uint64_t release = (768u * 256u - 1) * 8u + 1;
  source.runMasterCycles(release - 1 - source.masterClockScheduler().currentCycle());
  REQUIRE(source.spu2Controller().inputWriteState(0).readHalf == 0);
  requireSPUGuestContinuation(source, 32);
  for (std::size_t core = 0; core < 2; ++core)
  {
    REQUIRE(source.spu2Controller().inputWriteState(core).readHalf == 1);
    REQUIRE(source.spu2Controller().inputWriteState(core).fillHalf == 0);
    REQUIRE(source.iopDMAController().spuRemainingWords(core) == 254);
  }
  source.runMasterCycles(release + 4079 - source.masterClockScheduler().currentCycle());
  REQUIRE(source.iopDMAController().spuRemainingWords(0) == 1);
  REQUIRE(source.iopDMAController().spuRemainingWords(1) == 1);
  requireSPUGuestContinuation(source, 128);
  source.runMasterCycles(1024);
  REQUIRE(source.iopCore().stopReason() == IOPStopReason::None);
  REQUIRE(source.iopBus().read32(0x1000).value == GUEST_DONE);
  REQUIRE((source.iopInterruptController().status() & DMAC_CAUSE) != 0);
  REQUIRE((source.iopDMAController().interruptStatus(PRIMARY) & 0x10000000) != 0);
  REQUIRE((source.iopDMAController().interruptStatus(SECONDARY) & 0x01000000) != 0);
  REQUIRE((source.iopInterruptController().status() & (1u << 9)) == 0);
  for (std::size_t core = 0; core < 2; ++core)
  {
    CAPTURE(core);
    REQUIRE_FALSE(source.iopDMAController().spuActive(core));
    REQUIRE(source.iopBus().read32(SPU_CHCR[core]).value == 0x201);
    REQUIRE(source.iopBus().read32(static_cast<IOPAddress>(0x1200 + core * 8)).value ==
      0x4800 + core * 0x2000);
    REQUIRE(source.iopBus().read32(static_cast<IOPAddress>(0x1204 + core * 8)).value == 16);
    for (unsigned word = 0; word < 512; ++word)
    {
      const auto lane = word % 256;
      const auto address = static_cast<std::uint32_t>(0x2000 + core * 0x400 +
        (word < 256 ? 0x100 : 0) + (lane / 128) * 0x200 + (lane % 128) * 2);
      REQUIRE(source.spu2Controller().readSoundRAMHalfword(address) ==
        static_cast<std::uint16_t>(guestPayload(core, word)));
      REQUIRE(source.spu2Controller().readSoundRAMHalfword(address + 1) ==
        static_cast<std::uint16_t>(guestPayload(core, word) >> 16));
    }
  }
  EEQuadword sif0{};
  REQUIRE(source.eeBus().readDMAC128(0x10000, &sif0));
  REQUIRE(sif0.low == UINT64_C(0x0000000200000001));
  REQUIRE(sif0.high == UINT64_C(0x0000000400000003));
  for (unsigned word = 0; word < 4; ++word)
    REQUIRE(source.iopBus().read32(0xa000 + word * 4).value == 9 + word);
  NekoSystem coldTraced;
  installSPUGuest(coldTraced, program);
  installSPUSIFPeers(coldTraced);
  coldTraced.startTrace(32);
  coldTraced.runMasterCycles(source.masterClockScheduler().currentCycle());
  IOPTraceTest::requireMachine(coldTraced, source);
  REQUIRE(coldTraced.traceTruncated());
  NekoSystem coldPlain;
  installSPUGuest(coldPlain, program);
  installSPUSIFPeers(coldPlain);
  coldPlain.runMasterCycles(source.masterClockScheduler().currentCycle());
  IOPTraceTest::requireMachine(coldPlain, source);
  REQUIRE(coldPlain.trace().empty());
  for (const bool traced : {false, true})
  {
    CAPTURE(traced);
    NekoSystem restored;
    restored.loadState(blockedCheckpoint);
    if (traced) restored.startTrace(32);
    restored.runMasterCycles(source.masterClockScheduler().currentCycle() - blockedCycle);
    IOPTraceTest::requireMachine(restored, source);
    REQUIRE(restored.trace().size() == (traced ? 32 : 0));
    REQUIRE(restored.traceTruncated() == traced);
  }
}

TEST_CASE("Authored IOP SPU guest preserves partial aborts and restores fresh paired restarts",
  "[iop][spu-conformance][save-state][trace]")
{
  SPUGuest program;
  guestSPUProfile(program, 32);
  guestSPUStart(program, 0);
  guestSPUStart(program, 1);
  guestStore(program, DPCR, 0x80000);
  guestStore(program, DPCR2, 8);
  guestPoll(program, SPU_BCR[0], 0x00200010, GUEST_BEQ);
  guestStore(program, SPU_CHCR[0], 0);
  guestStore(program, SPU_CHCR[1], 0x201);
  const auto stopped = program.size();
  for (std::size_t core = 0; core < 2; ++core)
  {
    guestReadback(program, SPU_CHCR[core],
      static_cast<IOPAddress>(0x1100 + core * 4));
    guestReadback(program, SPU_MADR[core],
      static_cast<IOPAddress>(0x1110 + core * 16));
    guestReadback(program, SPU_BCR[core],
      static_cast<IOPAddress>(0x1114 + core * 16));
    guestStore(program, SPU_MADR[core],
      static_cast<IOPAddress>(0xc000 + core * 0x2000));
    guestStore(program, SPU_BCR[core], 0x00100010);
  }
  const auto restart = guestSPUStart(program, 0);
  guestSPUStart(program, 1);
  guestSPUFinish(program);
  for (std::size_t core = 0; core < 2; ++core)
    guestStore(program, static_cast<IOPAddress>(0x1f9001b0 + core * 0x400), 0, GUEST_SH);
  program.push_back(0x1000ffff);
  program.push_back(0);
  NekoSystem source;
  installSPUGuest(source, program);
  // Polling adds dynamic instructions; locate the actual guest stop boundary.
  unsigned pulses = 0;
  while ((source.iopCore().programCounter() != 0xa0000000 + stopped * 4 ||
      source.iopDMAController().spuActive(0) ||
      source.iopDMAController().spuActive(1)) && pulses < 1024)
  {
    source.runMasterCycles(8);
    ++pulses;
  }
  REQUIRE(pulses < 1024);
  std::array<IOPWord, 2> addresses{};
  std::array<IOPWord, 2> blocks{};
  for (std::size_t core = 0; core < 2; ++core)
  {
    addresses[core] = source.iopBus().read32(SPU_MADR[core]).value;
    blocks[core] = source.iopBus().read32(SPU_BCR[core]).value;
    REQUIRE(source.iopDMAController().spuRemainingWords(core) == 0);
    REQUIRE(addresses[core] > 0x4000 + core * 0x2000);
    REQUIRE(addresses[core] < 0x4400 + core * 0x2000);
    REQUIRE_FALSE(source.spu2Controller().inputWriteState(core).writeOpen);
    REQUIRE(source.spu2Controller().inputWriteState(core).selected);
    REQUIRE(source.spu2Controller().readSoundRAMHalfword(
      static_cast<std::uint32_t>(0x2101 + core * 0x400)) ==
      static_cast<std::uint16_t>(guestPayload(core, 0) >> 16));
  }
  REQUIRE((source.iopInterruptController().status() & DMAC_CAUSE) == 0);
  requireSPUGuestContinuation(source, 16);
  pulses = 0;
  while (source.iopCore().programCounter() != 0xa0000000 + restart * 4 &&
      pulses < 1024)
  {
    source.runMasterCycles(8);
    ++pulses;
  }
  REQUIRE(pulses < 1024);
  REQUIRE(source.iopCore().branchContinuation().active);
  requireSPUGuestContinuation(source, 64);
  source.runMasterCycles(10000);
  REQUIRE(source.iopBus().read32(0x1000).value == GUEST_DONE);
  REQUIRE(source.iopCore().stopReason() == IOPStopReason::None);
  for (std::size_t core = 0; core < 2; ++core)
  {
    REQUIRE(source.iopBus().read32(static_cast<IOPAddress>(0x1100 + core * 4)).value ==
      (core == 0 ? 0 : 0x201));
    REQUIRE(source.iopBus().read32(static_cast<IOPAddress>(0x1110 + core * 16)).value ==
      addresses[core]);
    REQUIRE(source.iopBus().read32(static_cast<IOPAddress>(0x1114 + core * 16)).value ==
      blocks[core]);
    REQUIRE_FALSE(source.spu2Controller().inputWriteState(core).selected);
    REQUIRE_FALSE(source.spu2Controller().inputWriteState(core).writeOpen);
    REQUIRE(source.iopBus().read32(SPU_CHCR[core]).value == 0x201);
    for (unsigned word = 0; word < 256; ++word)
    {
      const auto address = static_cast<std::uint32_t>(0x2100 + core * 0x400 +
        (word / 128) * 0x200 + (word % 128) * 2);
      REQUIRE(source.spu2Controller().readSoundRAMHalfword(address) ==
        static_cast<std::uint16_t>(restartPayload(core, word)));
      REQUIRE(source.spu2Controller().readSoundRAMHalfword(address + 1) ==
        static_cast<std::uint16_t>(restartPayload(core, word) >> 16));
    }
  }
  NekoSystem coldTraced;
  installSPUGuest(coldTraced, program);
  coldTraced.startTrace(32);
  coldTraced.runMasterCycles(source.masterClockScheduler().currentCycle());
  IOPTraceTest::requireMachine(coldTraced, source);
  NekoSystem coldPlain;
  installSPUGuest(coldPlain, program);
  coldPlain.runMasterCycles(source.masterClockScheduler().currentCycle());
  IOPTraceTest::requireMachine(coldPlain, source);
}

TEST_CASE("Authored IOP SPU source faults restore before CPU retirement in every observation mode",
  "[iop][spu-conformance][save-state][startup-failure]")
{
  for (std::size_t badCore : {0u, 1u})
  {
    CAPTURE(badCore);
    SPUGuest program;
    guestSPUProfile(program, 16);
    guestStore(program, SPU_MADR[badCore], 0x00900000);
    guestSPUStart(program, 0);
    guestSPUStart(program, 1);
    guestStore(program, badCore == 0 ? DPCR2 : DPCR, badCore == 0 ? 8 : 0x80000);
    guestStore(program, badCore == 0 ? DPCR : DPCR2, badCore == 0 ? 0x80000 : 8);
    NekoSystem source;
    installSPUGuest(source, program);
    source.runMasterCycles(program.size() * 8);
    const auto checkpoint = source.saveState();
    const auto retired = source.iopCore().retiredInstructions();
    const auto goodRemaining = source.iopDMAController().spuRemainingWords(badCore ^ 1);
    REQUIRE(goodRemaining < 256);
    source.startFailureProbe();
    REQUIRE_THROWS_AS(source.clockMasterCycle(), IOPDMATransferError);
    const auto expected = firstStartupFailure(source.trace());
    REQUIRE(expected.found);
    REQUIRE(expected.kind == StartupFailureKind::DMARejection);
    REQUIRE(nekoDecodeSPUDMABeat(expected.event).core == badCore);
    REQUIRE(source.iopCore().retiredInstructions() == retired);
    REQUIRE(source.iopDMAController().spuRemainingWords(badCore) == 256);
    REQUIRE(source.iopDMAController().spuRemainingWords(badCore ^ 1) == goodRemaining);
    REQUIRE_FALSE(expected.instruction.available);
    for (const unsigned mode : {0u, 1u, 2u, 3u})
    {
      CAPTURE(mode);
      NekoSystem restored;
      restored.loadState(checkpoint);
      const auto capture = std::make_shared<StartupFailureCapture>();
      if (mode == 1) restored.startTrace();
      if (mode == 2) restored.startFailureProbe();
      if (mode == 3) restored.startTrace(0, capture);
      REQUIRE_THROWS_AS(restored.clockMasterCycle(), IOPDMATransferError);
      IOPTraceTest::requireMachine(restored, source);
      if (mode != 0)
      {
        const auto actual = firstStartupFailure(mode == 3 ?
          capture->events() : restored.trace());
        REQUIRE(actual.found);
        IOPTraceTest::requireEvents({actual.event}, {expected.event});
        REQUIRE_FALSE(actual.instruction.available);
        NekoSystem coldPlain;
        installSPUGuest(coldPlain, program);
        coldPlain.runMasterCycles(program.size() * 8);
        REQUIRE_THROWS_AS(coldPlain.clockMasterCycle(), IOPDMATransferError);
        IOPTraceTest::requireMachine(coldPlain, source);
      }
    }
    NekoSystem coldTraced;
    installSPUGuest(coldTraced, program);
    const auto capture = std::make_shared<StartupFailureCapture>();
    coldTraced.startTrace(8, capture);
    coldTraced.runMasterCycles(program.size() * 8);
    REQUIRE_THROWS_AS(coldTraced.clockMasterCycle(), IOPDMATransferError);
    IOPTraceTest::requireMachine(coldTraced, source);
    const auto actual = firstStartupFailure(capture->events());
    REQUIRE(actual.found);
    IOPTraceTest::requireEvents({actual.event}, {expected.event});
    REQUIRE_FALSE(actual.instruction.available);
  }
}

TEST_CASE("IOP CPU SPU CHCR starts and reads each bounded source channel",
  "[iop][dma][spu-chcr]")
{
  for (std::size_t index = 0; index < 2; ++index)
  for (const IOPWord segment : {0u, 0x80000000u, 0xa0000000u})
  {
    CAPTURE(index);
    CAPTURE(segment);
    NekoSystem system;
    auto &bus = system.iopBus();
    auto &dma = system.iopDMAController();
    auto &cpu = system.iopCore();
    SPU2InputTest::programProfile(system.spu2Controller());
    REQUIRE(bus.write16(index == 0 ? 0x1f9001b0 : 0x1f9005b0,
      static_cast<std::uint16_t>(1u << index)).status == IOPBusStatus::Completed);
    REQUIRE(bus.write32(SPU_MADR[index], 0x1000).status == IOPBusStatus::Completed);
    REQUIRE(bus.write32(SPU_BCR[index], 0x00100010).status == IOPBusStatus::Completed);
    REQUIRE(bus.write32(0x1000, 0x12345678).status == IOPBusStatus::Completed);
    const auto target = SPU_CHCR[index] | segment;
    const IOPWord program[] = {
      0x3c010000 | (target >> 16), 0x34210000 | (target & 0xffff),
      0x3c020100, 0x34420201, 0xac220000, 0x8c230000, 0
    };
    constexpr auto programWords = sizeof(program) / sizeof(program[0]);
    for (std::size_t word = 0; word < programWords; ++word)
      REQUIRE(bus.write32(static_cast<IOPAddress>(word * 4), program[word]).status ==
        IOPBusStatus::Completed);
    cpu.startExecution(0xa0000000);
    for (unsigned instruction = 0; instruction < programWords &&
         cpu.executionState() == IOPExecutionState::Running; ++instruction)
      cpu.stepInstruction();
    REQUIRE(cpu.stopReason() == IOPStopReason::None);
    REQUIRE(cpu.retiredInstructions() == programWords);
    REQUIRE(cpu.generalRegister(3) == 0x01000201);
    REQUIRE(dma.spuActive(index));
    REQUIRE(dma.spuRemainingWords(index) == 256);
    dma.writePriority(index == 0 ? PRIMARY : SECONDARY, index == 0 ? 0x80000 : 8);
    system.clockMasterCycle();
    REQUIRE(dma.spuRemainingWords(index) == 255);
    REQUIRE(bus.read32(SPU_MADR[index]).value == 0x1004);
    REQUIRE(system.spu2Controller().readSoundRAMHalfword(
      static_cast<std::uint32_t>(0x2100 + index * 0x400)) == 0x5678);
  }
}

TEST_CASE("IOP SPU CHCR rejects excluded CPU and non CPU access routes atomically",
  "[iop][dma][spu-chcr][bus]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  IOPBus detached;
  for (const auto address : SPU_CHCR)
  {
    REQUIRE(detached.read32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(detached.write32(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read32(address).status == IOPBusStatus::Completed);
    REQUIRE(bus.read32(address).value == 0);
    REQUIRE(bus.write32(address, 0x201).status == IOPBusStatus::Completed);
    const auto state = system.iopStateHash();
    for (IOPAddress lane = 0; lane < 4; ++lane)
    {
      REQUIRE(bus.read8(address + lane).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write8(address + lane, 0).status == IOPBusStatus::Unmapped);
    }
    for (IOPAddress lane : {0u, 2u})
    {
      REQUIRE(bus.read16(address + lane).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write16(address + lane, 0).status == IOPBusStatus::Unmapped);
    }
    for (std::uint8_t mask = 1; mask < 15; ++mask)
      REQUIRE(bus.writeMasked32(address, 0, mask).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read32(address + 1).status == IOPBusStatus::Misaligned);
    REQUIRE(bus.write32(address + 1, 0).status == IOPBusStatus::Misaligned);
    REQUIRE(bus.readInstruction32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.readDMAC32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.writeDMAC32(address, 0).status == IOPBusStatus::Unmapped);
    std::uint32_t eeValue = 0;
    REQUIRE_FALSE(system.eeBus().readData32(address, &eeValue));
    REQUIRE_FALSE(system.eeBus().writeData32(address, 0));
    REQUIRE(system.iopStateHash() == state);
    REQUIRE(bus.writeMasked32(address, 0, 15).status == IOPBusStatus::Completed);
    REQUIRE(bus.read32(address).value == 0);
  }
}

TEST_CASE("IOP SPU CHCR unsupported start controls and profiles preserve selected input",
  "[iop][dma][spu-chcr]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  SPU2InputTest::programProfile(system.spu2Controller());
  for (std::size_t core = 0; core < 2; ++core)
  {
    CAPTURE(core);
    REQUIRE(bus.write32(SPU_CHCR[core], 0x201).status == IOPBusStatus::Completed);
    REQUIRE(bus.write32(SPU_BCR[core], 0x00100010).status == IOPBusStatus::Completed);
    const auto unselected = system.iopStateHash();
    REQUIRE(bus.write32(SPU_CHCR[core], 0x01000201).status == IOPBusStatus::Unmapped);
    REQUIRE(system.iopStateHash() == unselected);
    REQUIRE(bus.write16(core == 0 ? 0x1f9001b0 : 0x1f9005b0,
      static_cast<std::uint16_t>(1u << core)).status == IOPBusStatus::Completed);
    for (const IOPWord control : {
      1u, 0x200u, 0x01000000u, 0x01000200u, 0x01000401u, 0x11000201u, 0xffffffffu})
    {
      const auto before = system.iopStateHash();
      REQUIRE(bus.write32(SPU_CHCR[core], control).status == IOPBusStatus::Unmapped);
      REQUIRE(system.iopStateHash() == before);
    }
    for (const IOPWord bcr : {0u, 16u, 0x00010010u, 0x001f0010u, 0x00100020u})
    {
      REQUIRE(bus.write32(SPU_BCR[core], bcr).status == IOPBusStatus::Completed);
      const auto before = system.iopStateHash();
      REQUIRE(bus.write32(SPU_CHCR[core], 0x01000201).status == IOPBusStatus::Unmapped);
      REQUIRE(system.iopStateHash() == before);
      REQUIRE(bus.read32(SPU_CHCR[core]).value == 0x201);
    }
  }
}

TEST_CASE("IOP SPU CHCR repeated starts preserve progress and stopped controls abort without IRQ",
  "[iop][dma][spu-chcr]")
{
  for (std::size_t core = 0; core < 2; ++core)
  for (const IOPWord stop : {0u, 0x201u})
  {
    CAPTURE(core);
    CAPTURE(stop);
    NekoSystem system;
    auto &bus = system.iopBus();
    auto &dma = system.iopDMAController();
    auto &spu = system.spu2Controller();
    const auto bank = core == 0 ? PRIMARY : SECONDARY;
    const auto admas = core == 0 ? 0x1f9001b0u : 0x1f9005b0u;
    const auto target = static_cast<std::uint32_t>(0x2100 + core * 0x400);
    SPU2InputTest::programProfile(spu);
    REQUIRE(bus.write16(admas, static_cast<std::uint16_t>(1u << core)).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.write32(SPU_MADR[core], 0x1000).status == IOPBusStatus::Completed);
    REQUIRE(bus.write32(SPU_BCR[core], 0x00100010).status == IOPBusStatus::Completed);
    REQUIRE(bus.write32(0x1000, 0x12345678).status == IOPBusStatus::Completed);
    REQUIRE(bus.write32(SPU_CHCR[core], 0x01000201).status == IOPBusStatus::Completed);
    dma.writePriority(bank, core == 0 ? 0x80000 : 8);
    dma.writeInterrupt(bank, core == 0 ? 0x900000 : 0x810000);
    system.clockMasterCycle();
    const auto started = system.iopStateHash();
    REQUIRE(bus.writeMasked32(SPU_CHCR[core], 0x01000201, 15).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.write32(SPU_CHCR[core], 0x01000200).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(SPU_MADR[core], 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(SPU_BCR[core], 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write16(admas, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(system.iopStateHash() == started);
    const auto cursor = dma.tieBreakChannel();
    REQUIRE(bus.write32(SPU_CHCR[core], stop).status == IOPBusStatus::Completed);
    REQUIRE(bus.read32(SPU_CHCR[core]).value == stop);
    REQUIRE(bus.read32(SPU_MADR[core]).value == 0x1004);
    REQUIRE(bus.read32(SPU_BCR[core]).value == 0x00100010);
    REQUIRE_FALSE(spu.inputWriteRequested(core));
    REQUIRE(dma.interruptStatus(bank) == (core == 0 ? 0x900000 : 0x810000));
    REQUIRE((system.iopInterruptController().status() & DMAC_CAUSE) == 0);
    REQUIRE(dma.tieBreakChannel() == cursor);
    REQUIRE(spu.readSoundRAMHalfword(target) == 0x5678);
    REQUIRE(bus.write16(admas, 0).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(core == 0 ? 0x1f900344 : 0x1f900744).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(SPU_MADR[core], 0x2000).status == IOPBusStatus::Completed);
    REQUIRE(bus.write32(0x2000, 0xabcdfedc).status == IOPBusStatus::Completed);
    REQUIRE(bus.write16(admas, static_cast<std::uint16_t>(1u << core)).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.write32(SPU_CHCR[core], 0x01000201).status == IOPBusStatus::Completed);
    system.runMasterCycles(8);
    REQUIRE(dma.spuRemainingWords(core) == 255);
    REQUIRE(spu.readSoundRAMHalfword(target) == 0xfedc);
    REQUIRE(spu.readSoundRAMHalfword(target + 2) == 0);
  }
}

TEST_CASE("IOP SPU CPU started channels wait on capacity and complete only after final acceptance",
  "[iop][dma][spu-chcr]")
{
  for (std::size_t core = 0; core < 2; ++core)
  for (const bool masked : {false, true})
  {
    CAPTURE(core);
    CAPTURE(masked);
    NekoSystem system;
    auto &bus = system.iopBus();
    auto &dma = system.iopDMAController();
    auto &spu = system.spu2Controller();
    const auto bank = core == 0 ? PRIMARY : SECONDARY;
    const IOPWord mask = core == 0 ? 0x100000 : 0x10000;
    SPU2InputTest::programProfile(spu);
    REQUIRE(bus.write16(core == 0 ? 0x1f9001b0 : 0x1f9005b0,
      static_cast<std::uint16_t>(1u << core)).status == IOPBusStatus::Completed);
    REQUIRE(bus.write32(SPU_MADR[core], 0x1000).status == IOPBusStatus::Completed);
    REQUIRE(bus.write32(SPU_BCR[core], 0x00200010).status == IOPBusStatus::Completed);
    REQUIRE(bus.write32(SPU_CHCR[core], 0x01000201).status == IOPBusStatus::Completed);
    dma.writePriority(bank, core == 0 ? 0x80000 : 8);
    dma.writeInterrupt(bank, 0x800000 | (masked ? mask : 0));
    system.runMasterCycles(2041);
    REQUIRE(dma.spuRemainingWords(core) == 256);
    REQUIRE(bus.read32(SPU_CHCR[core]).value == 0x01000201);
    REQUIRE_FALSE(spu.inputWriteRequested(core));
    const auto before = dma.spuMemoryAddress(core);
    system.runMasterCycles(8);
    REQUIRE(dma.spuMemoryAddress(core) == before);
    REQUIRE(dma.interruptStatus(bank) == (0x800000 | (masked ? mask : 0)));
    SPU2InputTest::advanceSamples(spu, 256);
    system.runMasterCycles(2040);
    REQUIRE(dma.spuRemainingWords(core) == 1);
    REQUIRE(bus.read32(SPU_CHCR[core]).value == 0x01000201);
    system.runMasterCycles(8);
    REQUIRE(bus.read32(SPU_CHCR[core]).value == 0x201);
    REQUIRE(bus.read32(SPU_BCR[core]).value == 16);
    REQUIRE_FALSE(dma.spuActive(core));
    REQUIRE_FALSE(spu.inputWriteRequested(core));
    REQUIRE(dma.interruptStatus(bank) == (0x800000 |
      (masked ? SUMMARY | mask | (mask << 8) : 0)));
    REQUIRE(((system.iopInterruptController().status() & DMAC_CAUSE) != 0) == masked);
    REQUIRE((system.iopInterruptController().status() & (1u << 9)) == 0);
  }
}

TEST_CASE("IOP SPU DMA completion uses its own bank and stored mask",
  "[iop][dma][spu-transfer]")
{
  NekoSystem system;
  auto &dma = system.iopDMAController();
  for (std::size_t core = 0; core < 2; ++core)
  {
    CAPTURE(core);
    const auto bank = core == 0 ? PRIMARY : SECONDARY;
    const auto other = core == 0 ? SECONDARY : PRIMARY;
    const auto channel = core == 0 ? IOPDMAChannel::SPU0 : IOPDMAChannel::SPU1;
    const IOPWord mask = core == 0 ? 0x00100000 : 0x00010000;
    const IOPWord flag = mask << 8;
    dma.writeInterrupt(bank, 0);
    dma.signalChannelCompletion(channel);
    REQUIRE(dma.interruptStatus(bank) == 0);
    REQUIRE(dma.interruptStatus(other) == 0);
    dma.writeInterrupt(bank, 0x800000 | mask);
    dma.signalChannelCompletion(channel);
    REQUIRE(dma.interruptStatus(bank) == (SUMMARY | 0x800000 | mask | flag));
    REQUIRE(dma.interruptStatus(other) == 0);
    REQUIRE(dma.interruptPending());
    REQUIRE((system.iopInterruptController().status() & DMAC_CAUSE) != 0);
    dma.writeInterrupt(bank, 0);
    REQUIRE(dma.interruptStatus(bank) == flag);
    REQUIRE_FALSE(dma.interruptPending());
    dma.writeInterrupt(bank, flag);
    REQUIRE(dma.interruptStatus(bank) == 0);
  }
}

TEST_CASE("IOP SPU DMA source grants advance paired planar input and block progress",
  "[iop][dma][spu2][spu-transfer]")
{
  NekoSystem system;
  auto &dma = system.iopDMAController();
  auto &bus = system.iopBus();
  auto &spu = system.spu2Controller();
  SPU2InputTest::programProfile(spu);
  for (IOPWord word = 0; word < 256; ++word)
  {
    REQUIRE(bus.write32(0x1000 + word * 4, 0x80000000 | (word << 16) | word).status ==
      IOPBusStatus::Completed);
  }
  for (std::size_t core = 0; core < 2; ++core)
  {
    CAPTURE(core);
    const auto channel = core == 0 ? IOPDMAChannel::SPU0 : IOPDMAChannel::SPU1;
    const auto bank = core == 0 ? PRIMARY : SECONDARY;
    const IOPWord enable = core == 0 ? 0x00080000 : 8;
    const IOPWord mask = core == 0 ? 0x00100000 : 0x00010000;
    const auto address = static_cast<std::uint32_t>(0x2100 + core * 0x400);
    REQUIRE(spu.writeAutoDMAWriteSelection(core, static_cast<std::uint16_t>(1u << core)));
    REQUIRE(dma.writeSPUMemoryAddress(core, 0x1000) == IOPDMAWriteOutcome::Completed);
    REQUIRE(dma.writeSPUBlockControl(core, 0x00100010) == IOPDMAWriteOutcome::Completed);
    REQUIRE(dma.startSPUTransfer(core, bus, spu) == IOPDMAWriteOutcome::Completed);
    REQUIRE(dma.spuActive(core));
    REQUIRE(dma.spuChannelControl(core) == 0x01000201);
    REQUIRE(dma.spuRemainingWords(core) == 256);
    REQUIRE(dma.clockSPU(core, bus, spu).outcome == IOPDMABeatOutcome::Wait);
    dma.writePriority(bank, enable | (core == 0 ? 0x50000 : 5));
    REQUIRE(dma.channelEnabled(channel));
    REQUIRE(dma.channelPriority(channel) == 5);
    dma.writeInterrupt(bank, mask);
    const auto before = system.iopStateHash();
    const auto prepared = dma.inspectSPU(core, bus, spu);
    REQUIRE(prepared.channel == channel);
    REQUIRE(prepared.address == 0x1000);
    REQUIRE(prepared.value == 0x80000000);
    REQUIRE(system.iopStateHash() == before);
    for (IOPWord word = 0; word < 256; ++word)
    {
      const auto result = dma.clockSPU(core, bus, spu);
      REQUIRE(result.channel == channel);
      REQUIRE(result.address == 0x1000 + word * 4);
      REQUIRE(result.outcome == (word == 255 ?
        IOPDMABeatOutcome::Completed : IOPDMABeatOutcome::Accepted));
      REQUIRE(dma.spuMemoryAddress(core) == 0x1000 + (word + 1) * 4);
      REQUIRE(dma.spuBlockControl(core) == (((16 - (word + 1) / 16) << 16) | 16));
      REQUIRE(dma.spuRemainingWords(core) == 255 - word);
      const auto target = address + (word / 128) * 0x200 + (word % 128) * 2;
      REQUIRE(spu.readSoundRAMHalfword(target) == word);
      REQUIRE(spu.readSoundRAMHalfword(target + 1) == (0x8000 | word));
    }
    REQUIRE_FALSE(dma.spuActive(core));
    REQUIRE(dma.spuChannelControl(core) == 0x201);
    REQUIRE_FALSE(spu.inputWriteRequested(core));
    REQUIRE(dma.interruptStatus(bank) == (mask | (mask << 8)));
    REQUIRE((system.iopInterruptController().status() & (1u << 9)) == 0);
    REQUIRE(dma.clockSPU(core, bus, spu).outcome == IOPDMABeatOutcome::Wait);
    REQUIRE(bus.write32(SPU_MADR[core] + 8, 0x01000201).status == IOPBusStatus::Unmapped);
  }
}

TEST_CASE("IOP SPU DMA rejected source beats preserve prior accepted work",
  "[iop][dma][spu2][spu-transfer]")
{
  NekoSystem system;
  auto &dma = system.iopDMAController();
  auto &bus = system.iopBus();
  auto &spu = system.spu2Controller();
  SPU2InputTest::programProfile(spu);
  REQUIRE(spu.writeAutoDMAWriteSelection(0, 1));
  dma.writePriority(PRIMARY, 0x80000);
  REQUIRE(dma.writeSPUMemoryAddress(0, 0x7ffffc) == IOPDMAWriteOutcome::Completed);
  REQUIRE(dma.writeSPUBlockControl(0, 0x00100010) == IOPDMAWriteOutcome::Completed);
  REQUIRE(bus.write32(0x7ffffc, 0xabcd1234).status == IOPBusStatus::Completed);
  REQUIRE(dma.startSPUTransfer(0, bus, spu) == IOPDMAWriteOutcome::Completed);
  REQUIRE(dma.clockSPU(0, bus, spu).outcome == IOPDMABeatOutcome::Accepted);
  REQUIRE(spu.readSoundRAMHalfword(0x2100) == 0x1234);
  REQUIRE(spu.readSoundRAMHalfword(0x2101) == 0xabcd);
  REQUIRE(dma.spuMemoryAddress(0) == 0x800000);
  REQUIRE(dma.spuRemainingWords(0) == 255);
  const auto accepted = system.saveState();
  for (unsigned repeat = 0; repeat < 2; ++repeat)
  {
    const auto rejected = dma.clockSPU(0, bus, spu);
    REQUIRE(rejected.outcome == IOPDMABeatOutcome::Rejected);
    REQUIRE(rejected.channel == IOPDMAChannel::SPU0);
    REQUIRE(rejected.failure == IOPDMAFailure::SourceRead);
    REQUIRE(rejected.address == 0x800000);
    REQUIRE(rejected.status == IOPBusStatus::Unmapped);
    REQUIRE(std::string(IOPDMATransferError(rejected).what()).find("SPU0") !=
      std::string::npos);
    requireState(system, accepted);
  }
  for (const IOPAddress address : {0x800000u, 0xffffffu})
  {
    REQUIRE(dma.writeSPUMemoryAddress(0, address) == IOPDMAWriteOutcome::ActiveTransfer);
  }
  REQUIRE(dma.writeSPUBlockControl(0, 0) == IOPDMAWriteOutcome::ActiveTransfer);
  REQUIRE(dma.writeSPUBlockSize(0, 16) == IOPDMAWriteOutcome::ActiveTransfer);
  REQUIRE(dma.writeSPUBlockCount(0, 16) == IOPDMAWriteOutcome::ActiveTransfer);
  for (const auto address : {SPU_MADR[0], SPU_BCR[0]})
  {
    REQUIRE(bus.write32(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.writeMasked32(address, 0, 15).status == IOPBusStatus::Unmapped);
  }
  REQUIRE(bus.write16(SPU_BCR[0], 0).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.write16(SPU_BCR[0] + 2, 0).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.write16(0x1f9001b0, 0).status == IOPBusStatus::Unmapped);
  REQUIRE(dma.startSPUTransfer(0, bus, spu) == IOPDMAWriteOutcome::Completed);
  requireState(system, accepted);
  dma.stopSPUTransfer(0, bus, spu);
  REQUIRE_FALSE(dma.spuActive(0));
  REQUIRE(dma.spuMemoryAddress(0) == 0x800000);
  REQUIRE(dma.spuBlockControl(0) == 0x00100010);
  REQUIRE(dma.interruptStatus(PRIMARY) == 0);
  REQUIRE(spu.readSoundRAMHalfword(0x2100) == 0x1234);
  REQUIRE(dma.writeSPUMemoryAddress(0, 0x100) == IOPDMAWriteOutcome::Completed);
  REQUIRE(bus.write32(0x100, 0x98766543).status == IOPBusStatus::Completed);
  REQUIRE(dma.startSPUTransfer(0, bus, spu) == IOPDMAWriteOutcome::Completed);
  REQUIRE(dma.clockSPU(0, bus, spu).outcome == IOPDMABeatOutcome::Accepted);
  REQUIRE(spu.readSoundRAMHalfword(0x2100) == 0x6543);
  REQUIRE(spu.readSoundRAMHalfword(0x2101) == 0x9876);
  dma.stopSPUTransfer(0, bus, spu);
  REQUIRE(bus.write16(0x1f9001b0, 0).status == IOPBusStatus::Completed);
}

TEST_CASE("IOP SPU DMA host setup rejects unsupported profiles and detached wiring",
  "[iop][dma][spu2][spu-transfer]")
{
  NekoSystem system;
  auto &dma = system.iopDMAController();
  auto &bus = system.iopBus();
  auto &spu = system.spu2Controller();
  REQUIRE(dma.writeSPUBlockControl(0, 0x00100010) == IOPDMAWriteOutcome::Completed);
  const auto cold = system.saveState();
  REQUIRE(dma.startSPUTransfer(0, bus, spu) == IOPDMAWriteOutcome::UnsupportedControl);
  requireState(system, cold);
  SPU2InputTest::programProfile(spu);
  REQUIRE(spu.writeAutoDMAWriteSelection(0, 1));
  for (const IOPWord bcr : {0u, 16u, 0x00010010u, 0x001f0010u, 0x00100020u, 0xffffffffu})
  {
    REQUIRE(dma.writeSPUBlockControl(0, bcr) == IOPDMAWriteOutcome::Completed);
    const auto before = system.iopStateHash();
    REQUIRE(dma.startSPUTransfer(0, bus, spu) == IOPDMAWriteOutcome::UnsupportedControl);
    REQUIRE(system.iopStateHash() == before);
  }
  IOPBus detached;
  SPU2Controller other;
  REQUIRE_THROWS_AS(dma.startSPUTransfer(0, detached, spu), std::invalid_argument);
  REQUIRE_THROWS_AS(dma.inspectSPU(0, bus, other), std::invalid_argument);
  REQUIRE_THROWS_AS(dma.clockSPU(0, detached, spu), std::invalid_argument);
  REQUIRE_THROWS_AS(dma.stopSPUTransfer(0, bus, other), std::invalid_argument);
  REQUIRE_THROWS_AS(dma.startSPUTransfer(2, bus, spu), std::out_of_range);
}

TEST_CASE("IOP SPU DMA restores partial blocks full waits and masked completion",
  "[iop][dma][spu2][spu-transfer][save-state]")
{
  NekoSystem source;
  auto &dma = source.iopDMAController();
  auto &bus = source.iopBus();
  auto &spu = source.spu2Controller();
  SPU2InputTest::programProfile(spu);
  dma.writePriority(PRIMARY, 0x80000);
  dma.writePriority(SECONDARY, 8);
  dma.writeInterrupt(PRIMARY, 0x100000);
  dma.writeInterrupt(SECONDARY, 0x10000);
  for (IOPWord word = 0; word < 512; ++word)
  {
    REQUIRE(bus.write32(0x1000 + word * 4, 0xfedc0000 | word).status ==
      IOPBusStatus::Completed);
  }
  for (std::size_t core = 0; core < 2; ++core)
  {
    REQUIRE(spu.writeAutoDMAWriteSelection(core, static_cast<std::uint16_t>(1u << core)));
    REQUIRE(dma.writeSPUMemoryAddress(core, 0x1000) == IOPDMAWriteOutcome::Completed);
    REQUIRE(dma.writeSPUBlockControl(core, 0x00200010) == IOPDMAWriteOutcome::Completed);
    REQUIRE(dma.startSPUTransfer(core, bus, spu) == IOPDMAWriteOutcome::Completed);
    for (unsigned word = 0; word < (core == 0 ? 15u : 256u); ++word)
    {
      REQUIRE(dma.clockSPU(core, bus, spu).outcome == IOPDMABeatOutcome::Accepted);
    }
  }
  REQUIRE(dma.spuBlockCount(0) == 32);
  REQUIRE(dma.spuRemainingWords(0) == 497);
  REQUIRE(dma.spuRemainingWords(1) == 256);
  REQUIRE(dma.clockSPU(1, bus, spu).outcome == IOPDMABeatOutcome::Wait);
  const auto checkpoint = source.saveState();
  NekoSystem restored;
  restored.spu2Controller().writeSoundRAMHalfword(0x2100, 0xffff);
  restored.loadState(checkpoint);
  requireState(restored, checkpoint);
  for (auto *system : {&source, &restored})
  {
    auto &owner = system->iopDMAController();
    auto &input = system->spu2Controller();
    SPU2InputTest::advanceSamples(input, 256);
    REQUIRE(owner.clockSPU(0, system->iopBus(), input).outcome == IOPDMABeatOutcome::Wait);
    REQUIRE(owner.clockSPU(1, system->iopBus(), input).outcome == IOPDMABeatOutcome::Accepted);
    SPU2InputTest::advanceSamples(input, 256);
    for (unsigned word = 15; word < 256; ++word)
    {
      REQUIRE(owner.clockSPU(0, system->iopBus(), input).outcome == IOPDMABeatOutcome::Accepted);
    }
    REQUIRE(owner.clockSPU(0, system->iopBus(), input).outcome == IOPDMABeatOutcome::Wait);
    REQUIRE(owner.clockSPU(1, system->iopBus(), input).outcome == IOPDMABeatOutcome::Wait);
    SPU2InputTest::advanceSamples(input, 256);
    for (unsigned word = 0; word < 256; ++word)
    {
      REQUIRE(owner.clockSPU(0, system->iopBus(), input).outcome ==
        (word == 255 ? IOPDMABeatOutcome::Completed : IOPDMABeatOutcome::Accepted));
    }
    SPU2InputTest::advanceSamples(input, 256);
    REQUIRE(owner.clockSPU(1, system->iopBus(), input).outcome == IOPDMABeatOutcome::Wait);
    SPU2InputTest::advanceSamples(input, 256);
    for (unsigned word = 1; word < 256; ++word)
    {
      REQUIRE(owner.clockSPU(1, system->iopBus(), input).outcome ==
        (word == 255 ? IOPDMABeatOutcome::Completed : IOPDMABeatOutcome::Accepted));
    }
    REQUIRE(owner.interruptStatus(PRIMARY) == 0x10100000);
    REQUIRE(owner.interruptStatus(SECONDARY) == 0x01010000);
  }
  requireState(restored, source.saveState());
  NekoSystem completed;
  completed.loadState(source.saveState());
  requireState(completed, source.saveState());
  struct InvalidField
  {
    const char *path;
    std::uint64_t value;
  };
  for (const auto invalid : {
      InvalidField{"system.iop.dmaController.spuTransfers[0].channelControl", 0},
      InvalidField{"system.iop.dmaController.spuTransfers[0].channelControl", 0x01000401},
      InvalidField{"system.iop.dmaController.spuTransfers[0].blockWordsTransferred", 16},
      InvalidField{"system.iop.dmaController.spuBlockControl[0].value", 0x00200020},
      InvalidField{"system.iop.dmaController.spuBlockControl[0].value", 0x001f0010},
      InvalidField{"system.iop.spu2.inputStates[0].fillWords", 14},
      InvalidField{"system.iop.spu2.inputStates[0].writeOpen", 0}})
  {
    CAPTURE(invalid.path);
    SaveStateMutation corrupt(checkpoint);
    corrupt.writeScalar(invalid.path, invalid.value);
    corrupt.refreshChecksum();
    REQUIRE_THROWS_AS(completed.loadState(corrupt.bytes()), std::invalid_argument);
    requireState(completed, source.saveState());
  }
  source.reset();
  for (std::size_t core = 0; core < 2; ++core)
  {
    REQUIRE_FALSE(source.iopDMAController().spuActive(core));
    REQUIRE(source.iopDMAController().spuChannelControl(core) == 0);
    REQUIRE(source.iopDMAController().spuMemoryAddress(core) == 0);
    REQUIRE(source.iopDMAController().spuBlockControl(core) == 0);
  }
}

TEST_CASE("IOP SPU DMA block control retains independent words and halfword lanes",
  "[iop][dma][spu-bcr]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  REQUIRE(bus.write16(0x1f8010c4, 0x0010).status == IOPBusStatus::Completed);
  REQUIRE(bus.read32(0x1f8010c4).value == 0x0010);
  for (const IOPAddress address : {0x1f8010c4u, 0x1f801504u})
  {
    REQUIRE(bus.write32(address, 0x89abcdef).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(address).value == 0xcdef);
    REQUIRE(bus.read16(address + 2).value == 0x89ab);
    REQUIRE(bus.write16(address, 0x0010).status == IOPBusStatus::Completed);
    REQUIRE(bus.read32(address).value == 0x89ab0010);
    REQUIRE(bus.write16(address + 2, 0xffff).status == IOPBusStatus::Completed);
    REQUIRE(bus.read32(address).value == 0xffff0010);
    REQUIRE(bus.write16(address, 0xffff).status == IOPBusStatus::Completed);
    REQUIRE(bus.read32(address).value == 0xffffffff);
    REQUIRE(bus.write16(address + 2, 0).status == IOPBusStatus::Completed);
    REQUIRE(bus.read32(address).value == 0xffff);
    REQUIRE(bus.writeMasked32(address, 0x12345678, 15).status ==
      IOPBusStatus::Completed);
  }
  REQUIRE(bus.write16(0x1f8010c4, 0).status == IOPBusStatus::Completed);
  REQUIRE(bus.read32(0x1f8010c4).value == 0x12340000);
  REQUIRE(bus.read32(0x1f801504).value == 0x12345678);
}

TEST_CASE("IOP SPU DMA addresses retain normalized independent programming",
  "[iop][dma][spu-madr]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  const auto initialHash = system.iopStateHash();
  REQUIRE(bus.write32(0x1f8010c0, 0x0004fa00).status == IOPBusStatus::Completed);
  REQUIRE(bus.read32(0x1f8010c0).value == 0x0004fa00);
  REQUIRE(bus.read32(0x1f801500).value == 0);
  REQUIRE(system.iopStateHash() != initialHash);
  REQUIRE(bus.write32(0x1f801500, 0xabfffffc).status == IOPBusStatus::Completed);
  REQUIRE(bus.read32(0x1f801500).value == 0x00fffffc);
  REQUIRE(bus.read32(0x1f8010c0).value == 0x0004fa00);
  const auto programmed = system.saveState();
  for (IOPWord low = 1; low < 4; ++low)
  {
    REQUIRE(bus.write32(0x1f8010c0, 0xff04fa00 | low).status ==
      IOPBusStatus::Unmapped);
    REQUIRE(bus.writeMasked32(0x1f801500, 0x00fffffc | low, 15).status ==
      IOPBusStatus::Unmapped);
  }
  requireState(system, programmed);
  REQUIRE(bus.writeMasked32(0x1f8010c0, 0xff04fa00, 15).status ==
    IOPBusStatus::Completed);
  requireState(system, programmed);
  REQUIRE(bus.write32(0x1f8010c0, 0).status == IOPBusStatus::Completed);
  REQUIRE(bus.read32(0x1f8010c0).value == 0);
  REQUIRE(bus.read32(0x1f801500).value == 0x00fffffc);
}

TEST_CASE("IOP SPU DMA programming preserves inactive engines and other owners",
  "[iop][dma][spu-madr][spu2]")
{
  NekoSystem system;
  auto &dma = system.iopDMAController();
  auto &bus = system.iopBus();
  dma.writePriority(PRIMARY, 0xffffffff);
  dma.writePriority(SECONDARY, 0xffffffff);
  dma.writeAuxiliaryControl(0x89abcdef);
  dma.writeInterrupt(PRIMARY, FORCE);
  dma.signalChannelCompletion(IOPDMAChannel::SIF1);
  SPU2InputTest::programProfile(system.spu2Controller());
  REQUIRE(bus.write16(0x1f9001b0, 1).status == IOPBusStatus::Completed);
  REQUIRE(bus.write16(0x1f9005b0, 2).status == IOPBusStatus::Completed);
  REQUIRE(bus.write16(0x1f9001a8, 0x1234).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(0x100, 0x76543210).status == IOPBusStatus::Completed);
  SaveStateMutation expected(system.saveState());
  for (std::size_t core = 0; core < 2; ++core)
  {
    CAPTURE(core);
    for (IOPWord high = 0; high < 256; ++high)
    {
      const IOPWord value = 0x4fa00 + static_cast<IOPWord>(core * 4);
      REQUIRE(dma.writeSPUMemoryAddress(core, (high << 24) | value) ==
        IOPDMAWriteOutcome::Completed);
      REQUIRE(dma.spuMemoryAddress(core) == value);
      REQUIRE(bus.read32(SPU_MADR[core]).value == value);
    }
    expected.writeScalar("system.iop.dmaController.spuMemoryAddress[" +
      std::to_string(core) + "].value", dma.spuMemoryAddress(core));
    for (IOPAddress offset : {8u})
    {
      REQUIRE(bus.write32(SPU_MADR[core] + offset, 0).status == IOPBusStatus::Completed);
      REQUIRE(bus.write32(SPU_MADR[core] + offset, 1).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write32(SPU_MADR[core] + offset, 0x01000000).status ==
        IOPBusStatus::Unmapped);
      REQUIRE(bus.read32(SPU_MADR[core] + offset).value == 0);
    }
    const auto input = "system.iop.spu2.inputStates[" + std::to_string(core) + "].";
    expected.writeScalar(input + "writeOpen", 0);
    expected.writeScalar(input + "fillHalf", 0);
  }
  expected.refreshChecksum();
  requireState(system, expected.bytes());
  REQUIRE_THROWS_AS(dma.spuMemoryAddress(2), std::out_of_range);
  REQUIRE_THROWS_AS(dma.writeSPUMemoryAddress(2, 1), std::out_of_range);
  system.runMasterCycles(80);
  REQUIRE(bus.read32(0x100).value == 0x76543210);
  bus.reset();
  system.iopCore().reset();
  REQUIRE(dma.spuMemoryAddress(0) == 0x4fa00);
  REQUIRE(dma.spuMemoryAddress(1) == 0x4fa04);
  REQUIRE(bus.read16(0x1f9001a8).value == 0x1234);
  REQUIRE(bus.read16(0x1f900344).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.read16(0x1f900744).status == IOPBusStatus::Unmapped);
  REQUIRE(dma.interruptStatus(PRIMARY) == (FORCE | SUMMARY));
  REQUIRE(dma.interruptStatus(SECONDARY) == FLAG);
  system.reset();
  for (const auto address : SPU_MADR)
  {
    REQUIRE(system.iopBus().read32(address).value == 0);
  }
}

TEST_CASE("IOP SPU DMA addresses reject excluded transports without mutation",
  "[iop][dma][spu-madr][bus]")
{
  IOPBus detached;
  NekoSystem system;
  auto &bus = system.iopBus();
  for (const auto address : SPU_MADR)
  {
    REQUIRE(detached.read32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(detached.write32(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(address, 0x4fa00).status == IOPBusStatus::Completed);
  }
  const auto before = system.saveState();
  for (const auto address : SPU_MADR)
  {
    for (std::uint8_t mask = 1; mask < 15; ++mask)
    {
      REQUIRE(bus.writeMasked32(address, 0, mask).status == IOPBusStatus::Unmapped);
    }
    for (IOPAddress lane = 0; lane < 4; ++lane)
    {
      REQUIRE(bus.read8(address + lane).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write8(address + lane, 0).status == IOPBusStatus::Unmapped);
      const auto half = lane % 2 == 0 ? IOPBusStatus::Unmapped : IOPBusStatus::Misaligned;
      REQUIRE(bus.read16(address + lane).status == half);
      REQUIRE(bus.write16(address + lane, 0).status == half);
      if (lane != 0)
      {
        REQUIRE(bus.read32(address + lane).status == IOPBusStatus::Misaligned);
        REQUIRE(bus.write32(address + lane, 0).status == IOPBusStatus::Misaligned);
      }
    }
    REQUIRE(bus.readInstruction32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.readDMAC32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.writeDMAC32(address, 0).status == IOPBusStatus::Unmapped);
    std::uint8_t byte = 0;
    REQUIRE(bus.inspectPhysical(address, &byte, 1) == IOPBusStatus::Unmapped);
    REQUIRE(bus.loadPhysical(address, &byte, 1) == IOPBusStatus::Unmapped);
    std::uint32_t eeValue = 0;
    REQUIRE_FALSE(system.eeBus().readData32(address, &eeValue));
    REQUIRE_FALSE(system.eeBus().writeData32(address, 0));
    REQUIRE(bus.write32(address + 0x100000, 0).status == IOPBusStatus::Unmapped);
  }
  requireState(system, before);
}

TEST_CASE("IOP SPU DMA pending word stores and loads restore canonically",
  "[iop][dma][spu-madr][save-state]")
{
  for (const auto physical : SPU_MADR)
  for (const IOPAddress segment : {0u, 0x80000000u, 0xa0000000u})
  for (const std::uint64_t cut : {32u, 40u, 41u, 48u, 56u})
  {
    const auto target = physical | segment;
    CAPTURE(target);
    CAPTURE(cut);
    NekoSystem original;
    const IOPWord program[] = {
      0x3c010000 | (target >> 16), 0x34210000 | (target & 0xffff),
      0x3c02ab04, 0x3442fa00, 0x10000002, 0xac220000,
      0, 0x8c230000, 0, 0x1000ffff, 0
    };
    IOPAddress address = 0;
    for (const auto word : program)
    {
      REQUIRE(original.iopBus().write32(address, word).status ==
        IOPBusStatus::Completed);
      address += 4;
    }
    original.iopCore().startExecution(0xa0000000);
    original.runMasterCycles(cut);
    const auto checkpoint = original.saveState();
    NekoSystem restored;
    NekoSystem plain;
    restored.loadState(checkpoint);
    plain.loadState(checkpoint);
    REQUIRE(restored.iopBus().read32(physical).value ==
      original.iopBus().read32(physical).value);
    original.startTrace();
    restored.startTrace();
    original.runMasterCycles(96 - cut);
    restored.runMasterCycles(96 - cut);
    plain.runMasterCycles(96 - cut);
    REQUIRE(original.iopCore().stopReason() == IOPStopReason::None);
    REQUIRE(original.iopCore().generalRegister(3) == 0x4fa00);
    REQUIRE(original.iopBus().read32(physical).value == 0x4fa00);
    REQUIRE(restored.iopBus().write32(physical, 0xab04fa00).status ==
      IOPBusStatus::Completed);
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireMachine(plain, original);
  }
}

TEST_CASE("IOP SPU DMA block control preserves inactive engines and lifecycle boundaries",
  "[iop][dma][spu-bcr][spu2]")
{
  NekoSystem system;
  auto &dma = system.iopDMAController();
  auto &bus = system.iopBus();
  dma.writePriority(PRIMARY, 0xffffffff);
  dma.writePriority(SECONDARY, 0xffffffff);
  dma.writeAuxiliaryControl(0x89abcdef);
  dma.writeInterrupt(PRIMARY, FORCE);
  dma.signalChannelCompletion(IOPDMAChannel::SIF1);
  SPU2InputTest::programProfile(system.spu2Controller());
  REQUIRE(bus.write16(0x1f9001b0, 1).status == IOPBusStatus::Completed);
  REQUIRE(bus.write16(0x1f9005b0, 2).status == IOPBusStatus::Completed);
  REQUIRE(bus.write16(0x1f9001a8, 0x1234).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(0x100, 0x76543210).status == IOPBusStatus::Completed);
  SaveStateMutation expected(system.saveState());
  for (std::size_t core = 0; core < 2; ++core)
  {
    REQUIRE(dma.spuBlockControl(core) == 0);
    dma.writeSPUBlockControl(core, 0xffffffff);
    dma.writeSPUBlockSize(core, 0x0010);
    dma.writeSPUBlockCount(core, 0x8000);
    REQUIRE(dma.spuBlockSize(core) == 0x0010);
    REQUIRE(dma.spuBlockCount(core) == 0x8000);
    REQUIRE(bus.read32(SPU_BCR[core]).value == 0x80000010);
    expected.writeScalar("system.iop.dmaController.spuBlockControl[" +
      std::to_string(core) + "].value", 0x80000010);
    REQUIRE(bus.write32(SPU_MADR[core] + 8, 0x01000000).status ==
      IOPBusStatus::Unmapped);
  }
  expected.refreshChecksum();
  requireState(system, expected.bytes());
  REQUIRE_THROWS_AS(dma.spuBlockControl(2), std::out_of_range);
  REQUIRE_THROWS_AS(dma.writeSPUBlockControl(2, 0), std::out_of_range);
  REQUIRE_THROWS_AS(dma.spuBlockSize(2), std::out_of_range);
  REQUIRE_THROWS_AS(dma.writeSPUBlockSize(2, 0), std::out_of_range);
  REQUIRE_THROWS_AS(dma.spuBlockCount(2), std::out_of_range);
  REQUIRE_THROWS_AS(dma.writeSPUBlockCount(2, 0), std::out_of_range);
  system.runMasterCycles(80);
  REQUIRE(bus.read32(0x100).value == 0x76543210);
  bus.reset();
  system.iopCore().reset();
  for (std::size_t core = 0; core < 2; ++core)
  {
    REQUIRE(dma.spuBlockControl(core) == 0x80000010);
    REQUIRE(dma.spuMemoryAddress(core) == 0);
  }
  REQUIRE(bus.read16(0x1f9001a8).value == 0x1234);
  REQUIRE(bus.read16(0x1f900344).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.read16(0x1f900744).status == IOPBusStatus::Unmapped);
  REQUIRE(dma.interruptStatus(PRIMARY) == (FORCE | SUMMARY));
  REQUIRE(dma.interruptStatus(SECONDARY) == FLAG);
  REQUIRE_FALSE(system.iopSIF1DMAC().active());
  REQUIRE_FALSE(system.iopSIF0DMAC().active());
  system.reset();
  for (const auto address : SPU_BCR)
  {
    REQUIRE(system.iopBus().read32(address).value == 0);
  }
}

TEST_CASE("IOP SPU DMA block control rejects excluded transports without mutation",
  "[iop][dma][spu-bcr][bus]")
{
  IOPBus detached;
  NekoSystem system;
  auto &bus = system.iopBus();
  for (const auto address : SPU_BCR)
  {
    REQUIRE(bus.write32(address, 0x89abcdef).status == IOPBusStatus::Completed);
  }
  const auto before = system.saveState();
  for (const auto address : SPU_BCR)
  {
    REQUIRE(detached.read32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(detached.write32(address, 0).status == IOPBusStatus::Unmapped);
    for (IOPAddress lane = 0; lane < 4; ++lane)
    {
      REQUIRE(bus.read8(address + lane).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write8(address + lane, 0).status == IOPBusStatus::Unmapped);
      if (lane % 2 != 0)
      {
        REQUIRE(bus.read16(address + lane).status == IOPBusStatus::Misaligned);
        REQUIRE(bus.write16(address + lane, 0).status == IOPBusStatus::Misaligned);
      }
      else
      {
        REQUIRE(detached.read16(address + lane).status == IOPBusStatus::Unmapped);
        REQUIRE(detached.write16(address + lane, 0).status == IOPBusStatus::Unmapped);
      }
      if (lane != 0)
      {
        REQUIRE(bus.read32(address + lane).status == IOPBusStatus::Misaligned);
        REQUIRE(bus.write32(address + lane, 0).status == IOPBusStatus::Misaligned);
      }
    }
    for (std::uint8_t mask = 1; mask < 15; ++mask)
    {
      REQUIRE(bus.writeMasked32(address, 0, mask).status == IOPBusStatus::Unmapped);
    }
    REQUIRE(bus.readInstruction32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.readDMAC32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.writeDMAC32(address, 0).status == IOPBusStatus::Unmapped);
    std::uint8_t byte = 0;
    REQUIRE(bus.inspectPhysical(address, &byte, 1) == IOPBusStatus::Unmapped);
    REQUIRE(bus.loadPhysical(address, &byte, 1) == IOPBusStatus::Unmapped);
    std::uint32_t eeValue = 0;
    REQUIRE_FALSE(system.eeBus().readData32(address, &eeValue));
    REQUIRE_FALSE(system.eeBus().writeData32(address, 0));
    REQUIRE(bus.write32(address + 0x100000, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read16(address + 4).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write16(address + 4, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(address + 4, 1).status == IOPBusStatus::Unmapped);
  }
  requireState(system, before);
}

TEST_CASE("IOP SPU DMA block control guest halfwords restore pending branches and loads",
  "[iop][dma][spu-bcr][save-state][trace]")
{
  namespace Memory = NekoIOPTraceMemory;
  for (const auto physical : SPU_BCR)
  for (const IOPAddress segment : {0u, 0x80000000u, 0xa0000000u})
  for (const std::uint64_t cut : {32u, 48u, 64u})
  {
    const auto target = physical | segment;
    CAPTURE(target);
    CAPTURE(cut);
    NekoSystem original;
    REQUIRE(original.iopBus().write32(physical, 0x89abcdef).status ==
      IOPBusStatus::Completed);
    const auto other = physical == SPU_BCR[0] ? SPU_BCR[1] : SPU_BCR[0];
    REQUIRE(original.iopBus().write32(other, 0xffffffff).status ==
      IOPBusStatus::Completed);
    const IOPWord program[] = {
      0x3c010000 | (target >> 16), 0x34210000 | (target & 0xffff),
      0x24020010, 0x10000002, 0xa4220000, // SH in branch delay slot
      0xffffffff, 0x94230000, 0x94240002, 0x8c250000, 0,
      0xa8220001 // partial-mask SWL is not a native halfword
    };
    IOPAddress address = 0;
    for (const auto word : program)
    {
      REQUIRE(original.iopBus().write32(address, word).status ==
        IOPBusStatus::Completed);
      address += 4;
    }
    original.iopCore().startExecution(0xa0000000);
    original.runMasterCycles(cut);
    const auto checkpoint = original.saveState();
    NekoSystem restored;
    NekoSystem plain;
    restored.loadState(checkpoint);
    plain.loadState(checkpoint);
    original.startTrace();
    restored.startTrace();
    original.runMasterCycles(80 - cut);
    restored.runMasterCycles(80 - cut);
    plain.runMasterCycles(80 - cut);
    const auto &core = original.iopCore();
    REQUIRE(core.stopReason() == IOPStopReason::StoreFailure);
    REQUIRE(core.exception().kind == IOPException::None);
    REQUIRE(core.retiredInstructions() == 9);
    REQUIRE(core.programCounter() == 0xa0000028);
    REQUIRE(core.generalRegister(3) == 0x0010);
    REQUIRE(core.generalRegister(4) == 0x89ab);
    REQUIRE(core.generalRegister(5) == 0x89ab0010);
    REQUIRE(core.storeFailure().access.physicalAddress == physical + 1);
    REQUIRE(core.storeFailure().access.virtualAddress == target + 1);
    REQUIRE(core.storeFailure().access.outcome == IOPAccessOutcome::Unmapped);
    REQUIRE(core.storeFailure().mergeByteEnable == 3);
    REQUIRE(original.iopBus().read32(physical).value == 0x89ab0010);
    REQUIRE(restored.iopBus().read32(other).value == 0xffffffff);
    const auto route = segment == 0xa0000000 ? Memory::CacheRoute::Uncached :
      Memory::CacheRoute::Cached;
    std::size_t stores = 0;
    for (const auto &event : original.trace())
    {
      if (event.type != NekoTraceEventType::MemoryAccess || event.value0 != target ||
          (event.masterCycle != 33 && event.masterCycle != 73))
      {
        continue;
      }
      const bool halfword = event.masterCycle == 33;
      REQUIRE(event.value1 == (halfword ? 0x0010 : 0));
      REQUIRE(event.value3 == nekoPackIOPMemoryTraceMetadata({
        physical, static_cast<std::uint8_t>(halfword ? 2 : 4),
        Memory::AccessKind::DataStore,
        halfword ? Memory::Outcome::Completed : Memory::Outcome::Unmapped,
        route, true, 3}));
      ++stores;
    }
    REQUIRE(stores == (cut == 32 ? 2 : 1));
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireMachine(plain, original);
  }
}

TEST_CASE("IOP DMA common controls retain words and decode implemented channels",
  "[iop][dma]")
{
  IOPInterruptController interrupts;
  IOPDMAController dma(interrupts);
  REQUIRE(dma.priority(PRIMARY) == 0);
  REQUIRE(dma.priority(SECONDARY) == 0);
  REQUIRE(dma.interruptStatus(PRIMARY) == 0);
  REQUIRE(dma.interruptStatus(SECONDARY) == 0);
  REQUIRE_FALSE(dma.channelEnabled(IOPDMAChannel::SIF1));
  REQUIRE(dma.channelPriority(IOPDMAChannel::SIF1) == 0);
  for (std::uint32_t priority = 0; priority < 8; ++priority)
  {
    dma.writePriority(SECONDARY, 0xffff0fff | (priority << 12));
    REQUIRE(dma.channelPriority(IOPDMAChannel::SIF1) == priority);
    REQUIRE_FALSE(dma.channelEnabled(IOPDMAChannel::SIF1));
    dma.writePriority(SECONDARY, 0xffff8fff | (priority << 12));
    REQUIRE(dma.channelEnabled(IOPDMAChannel::SIF1));
  }
  dma.writePriority(PRIMARY, 0xffffffff);
  REQUIRE(dma.priority(PRIMARY) == 0xffffffff);
  dma.writeInterrupt(PRIMARY, 0xff7f7fff);
  dma.writeInterrupt(SECONDARY, 0xff7f7fff);
  REQUIRE(dma.interruptStatus(PRIMARY) == 0x007f7fff);
  REQUIRE(dma.interruptStatus(SECONDARY) == 0x007f7fff);
  REQUIRE_FALSE(dma.interruptPending());
  REQUIRE(interrupts.status() == 0);
  REQUIRE_THROWS_AS(dma.priority(static_cast<IOPDMABank>(2)), std::out_of_range);
  REQUIRE_THROWS_AS(
    dma.signalChannelCompletion(static_cast<IOPDMAChannel>(8)), std::out_of_range);
  REQUIRE(interrupts.status() == 0);
}

TEST_CASE("IOP DMA auxiliary word retains every bit without assigned effects",
  "[iop][dma][auxiliary]")
{
  IOPBus detached;
  REQUIRE(detached.read32(AUXILIARY).status == IOPBusStatus::Unmapped);
  REQUIRE(detached.write32(AUXILIARY, 1).status == IOPBusStatus::Unmapped);
  NekoSystem system;
  auto &bus = system.iopBus();
  REQUIRE(bus.read32(AUXILIARY).status == IOPBusStatus::Completed);
  REQUIRE(bus.read32(AUXILIARY).value == 0);
  system.iopDMAController().writePriority(SECONDARY, 0x8800);
  const auto initial = system.iopStateHash();
  const auto eeHash = system.eeStateHash();
  const auto sifHash = system.sifStateHash();
  for (unsigned bit = 0; bit < 32; ++bit)
  {
    CAPTURE(bit);
    const IOPWord value = IOPWord(1) << bit;
    REQUIRE(bus.write32(AUXILIARY, value).status == IOPBusStatus::Completed);
    REQUIRE(bus.read32(AUXILIARY).value == value);
    REQUIRE(system.iopStateHash() != initial);
    REQUIRE(system.iopDMAController().priority(PRIMARY) == 0);
    REQUIRE(system.iopDMAController().priority(SECONDARY) == 0x8800);
    REQUIRE(system.iopDMAController().channelEnabled(IOPDMAChannel::SIF0));
    REQUIRE(system.iopDMAController().channelEnabled(IOPDMAChannel::SIF1));
    REQUIRE_FALSE(system.iopDMAController().interruptPending());
    REQUIRE(system.iopInterruptController().status() == 0);
  }
  REQUIRE(bus.writeMasked32(AUXILIARY, 0, 15).status ==
    IOPBusStatus::Completed);
  REQUIRE(system.iopStateHash() == initial);
  REQUIRE(system.eeStateHash() == eeHash);
  REQUIRE(system.sifStateHash() == sifHash);
  REQUIRE(bus.readDMAC32(AUXILIARY).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.writeDMAC32(AUXILIARY, 1).status == IOPBusStatus::Unmapped);
  REQUIRE(system.iopStateHash() == initial);
  system.reset();
  REQUIRE(system.iopBus().read32(AUXILIARY).value == 0);
}

TEST_CASE("IOP DMA auxiliary word supports ordinary guest LW and SW",
  "[iop][dma][auxiliary]")
{
  NekoSystem system;
  const IOPWord program[] = {
    0x3c01bf80, // lui r1, 0xbf80
    0x34211578, // ori r1, r1, 0x1578
    0x24021234, // addiu r2, r0, 0x1234
    0xac220000, // sw r2, 0(r1)
    0x8c230000, // lw r3, 0(r1)
    0
  };
  IOPAddress address = 0;
  for (const IOPWord word : program)
  {
    REQUIRE(system.iopBus().write32(address, word).status ==
      IOPBusStatus::Completed);
    address += 4;
  }
  system.iopCore().startExecution(0x80000000);
  for (unsigned step = 0; step < 6; ++step)
  {
    system.iopCore().stepInstruction();
    REQUIRE(system.iopCore().executionState() == IOPExecutionState::Running);
  }
  REQUIRE(system.iopCore().exception().kind == IOPException::None);
  REQUIRE(system.iopCore().retiredInstructions() == 6);
  REQUIRE(system.iopCore().generalRegister(3) == 0x1234);
  REQUIRE(system.iopBus().read32(AUXILIARY).value == 0x1234);
}

TEST_CASE("IOP DPCR3 initialization requires the common DMA owner and changes no state",
  "[iop][dma][dpcr3]")
{
  IOPBus detached;
  REQUIRE(detached.write32(DPCR3, 0x777).status == IOPBusStatus::Unmapped);
  REQUIRE(detached.writeMasked32(DPCR3, 0x777, 0xf).status ==
    IOPBusStatus::Unmapped);

  NekoSystem system;
  auto &dma = system.iopDMAController();
  dma.writePriority(PRIMARY, 0x12345678);
  dma.writePriority(SECONDARY, 0x8800);
  dma.writeAuxiliaryControl(0x89abcdef);
  dma.writeInterrupt(SECONDARY, 0x00800000);
  dma.signalChannelCompletion(IOPDMAChannel::SIF1);
  system.iopInterruptController().writeStatus(~DMAC_CAUSE);
  REQUIRE(dma.interruptPending());
  REQUIRE(system.iopInterruptController().status() == 0);
  REQUIRE(system.iopSIF1DMAC().writeMemoryAddress(0x200) ==
    IOPDMAWriteOutcome::Completed);
  REQUIRE(system.iopSIF1DMAC().writeChannelControl(START) ==
    IOPDMAWriteOutcome::Completed);
  REQUIRE(system.sif().pushSIF1Quadword({{1, 2, 3, 4}}) ==
    SIFTransferOutcome::Completed);
  const auto before = system.saveState();
  const auto iopHash = system.iopStateHash();
  const auto sifHash = system.sifStateHash();
  system.startTrace();
  REQUIRE(dma.writeDPCR3Initialization(0x777) == IOPDMAWriteOutcome::Completed);
  REQUIRE(dma.writeDPCR3Initialization(0) == IOPDMAWriteOutcome::UnsupportedControl);
  for (unsigned repeat = 0; repeat < 2; ++repeat)
  {
    REQUIRE(system.iopBus().write32(DPCR3, 0x777).status ==
      IOPBusStatus::Completed);
    REQUIRE(system.iopBus().writeMasked32(DPCR3, 0x777, 0xf).status ==
      IOPBusStatus::Completed);
  }
  REQUIRE(system.iopStateHash() == iopHash);
  REQUIRE(system.sifStateHash() == sifHash);
  requireState(system, before);
  REQUIRE(system.trace().empty());
}

TEST_CASE("IOP DPCR3 rejects other values widths masks and neighboring addresses",
  "[iop][dma][dpcr3][bus]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  const auto before = system.saveState();
  for (unsigned bit = 0; bit < 32; ++bit)
  {
    CAPTURE(bit);
    REQUIRE(bus.write32(DPCR3, 0x777 ^ (IOPWord{1} << bit)).status ==
      IOPBusStatus::Unmapped);
  }
  for (IOPWord value : {0u, 0xffffffffu, 0x77777777u})
  {
    REQUIRE(bus.writeMasked32(DPCR3, value, 0xf).status ==
      IOPBusStatus::Unmapped);
  }
  for (std::uint8_t mask = 1; mask < 0xf; ++mask)
  {
    REQUIRE(bus.writeMasked32(DPCR3, 0x777, mask).status == IOPBusStatus::Unmapped);
  }
  for (IOPAddress lane = 0; lane < 4; ++lane)
  {
    CAPTURE(lane);
    REQUIRE(bus.read8(DPCR3 + lane).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(DPCR3 + lane, 0x77).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read32(DPCR3 + lane).status ==
      (lane == 0 ? IOPBusStatus::Unmapped : IOPBusStatus::Misaligned));
    const auto status = lane == 0 ? IOPBusStatus::Completed : IOPBusStatus::Misaligned;
    REQUIRE(bus.write32(DPCR3 + lane, 0x777).status == status);
    REQUIRE(bus.writeMasked32(DPCR3 + lane, 0x777, 0xf).status == status);
    REQUIRE(bus.read16(DPCR3 + lane).status ==
      ((lane & 1) != 0 ? IOPBusStatus::Misaligned : IOPBusStatus::Unmapped));
    REQUIRE(bus.write16(DPCR3 + lane, 0x777).status ==
      ((lane & 1) != 0 ? IOPBusStatus::Misaligned : IOPBusStatus::Unmapped));
  }
  for (IOPAddress address : {DPCR3 - 4, DPCR3 + 4, 0x9f8015f0u, 0xbf8015f0u})
  {
    CAPTURE(address);
    REQUIRE(bus.regionForRange(address, 1) == IOPBusRegion::Unmapped);
    REQUIRE(bus.read32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(address, 0x777).status == IOPBusStatus::Unmapped);
  }
  for (std::uint8_t mask : {0u, 0x10u, 0xffu})
  {
    REQUIRE_THROWS_AS(bus.writeMasked32(DPCR3 + 1, 0x777, mask),
      std::invalid_argument);
  }
  REQUIRE(bus.regionForRange(DPCR3, 4) == IOPBusRegion::Mmio);
  REQUIRE(bus.regionForRange(DPCR3, 5) == IOPBusRegion::Unmapped);
  REQUIRE(bus.readDMAC32(DPCR3).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.writeDMAC32(DPCR3, 0x777).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.dmaWriteStatus(DPCR3) == IOPBusStatus::Unmapped);
  std::uint8_t byte = 0xa5;
  REQUIRE(bus.loadPhysical(DPCR3, &byte, 1) == IOPBusStatus::Unmapped);
  REQUIRE(bus.inspectPhysical(DPCR3, &byte, 1) == IOPBusStatus::Unmapped);
  REQUIRE(byte == 0xa5);
  requireState(system, before);
}

TEST_CASE("IOP DMA disabled channel words read zero before and after initialization",
  "[iop][dma][channel-read][bus]")
{
  IOPBus detached;
  NekoSystem system;
  auto &bus = system.iopBus();
  for (const IOPAddress address : CHANNEL_INITIALIZATION_REGISTERS)
  {
    CAPTURE(address);
    REQUIRE(detached.read32(address).status == IOPBusStatus::Unmapped);
    auto read = bus.read32(address);
    REQUIRE(read.status == IOPBusStatus::Completed);
    REQUIRE(read.value == 0);
    REQUIRE(bus.write32(address, 1).status == IOPBusStatus::Unmapped);
    for (unsigned repeat = 0; repeat < 2; ++repeat)
    {
      REQUIRE(bus.write32(address, 0).status == IOPBusStatus::Completed);
      REQUIRE(bus.writeMasked32(address, 0, 0xf).status == IOPBusStatus::Completed);
      read = bus.read32(address);
      REQUIRE(read.status == IOPBusStatus::Completed);
      REQUIRE(read.value == 0);
    }
  }
}

TEST_CASE("IOP DMA register initialization and channel reads preserve other owners",
  "[iop][dma][channel-read][channel-init][supplemental-init][channel0-init][channel1-init][channel2-init][channel3-init]")
{
  IOPBus detached;
  for (IOPAddress address : INITIALIZATION_REGISTERS)
  {
    REQUIRE(detached.write32(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(detached.writeMasked32(address, 0, 0xf).status == IOPBusStatus::Unmapped);
  }

  NekoSystem system;
  auto &dma = system.iopDMAController();
  dma.writePriority(PRIMARY, 0x89abcdef);
  dma.writePriority(SECONDARY, 0x8800);
  dma.writeAuxiliaryControl(0x12345678);
  dma.writeInterrupt(SECONDARY, 0x00800000);
  dma.signalChannelCompletion(IOPDMAChannel::SIF1);
  system.iopInterruptController().writeStatus(~DMAC_CAUSE);
  REQUIRE(dma.interruptPending());
  REQUIRE(system.iopInterruptController().status() == 0);
  REQUIRE(system.iopSIF1DMAC().writeMemoryAddress(0x200) ==
    IOPDMAWriteOutcome::Completed);
  REQUIRE(system.iopSIF1DMAC().writeChannelControl(START) ==
    IOPDMAWriteOutcome::Completed);
  REQUIRE(system.sif().pushSIF1Quadword({{1, 2, 3, 4}}) ==
    SIFTransferOutcome::Completed);
  const IOPWord descriptor[] = {0xc0002000, 4, 0xdead, 0xbeef, 0xf00d, 0xcafe};
  for (std::size_t index = 0; index < sizeof(descriptor) / sizeof(descriptor[0]); ++index)
  {
    REQUIRE(system.iopBus().write32(0x1000 + static_cast<IOPAddress>(index * 4),
      descriptor[index]).status == IOPBusStatus::Completed);
  }
  REQUIRE(system.iopSIF0DMAC().writeTagAddress(0x1000) ==
    IOPDMAWriteOutcome::Completed);
  REQUIRE(system.iopSIF0DMAC().writeChannelControl(0x01000401) ==
    IOPDMAWriteOutcome::Completed);
  REQUIRE(dma.clockSIF(system.iopSIF0DMAC(), system.iopBus(), system.sif()).outcome ==
    IOPDMABeatOutcome::Accepted);
  REQUIRE(system.iopSIF0DMAC().remainingWords() == 4);
  const auto before = system.saveState();
  system.startTrace();
  REQUIRE(dma.writeChannelRegisterInitialization(0) == IOPDMAWriteOutcome::Completed);
  REQUIRE(dma.writeSupplementalInitialization(0) == IOPDMAWriteOutcome::Completed);
  for (IOPAddress address : INITIALIZATION_REGISTERS)
  {
    CAPTURE(address);
    for (unsigned repeat = 0; repeat < 2; ++repeat)
    {
      REQUIRE(system.iopBus().write32(address, 0).status == IOPBusStatus::Completed);
      REQUIRE(system.iopBus().writeMasked32(address, 0, 0xf).status ==
        IOPBusStatus::Completed);
    }
  }
  for (const IOPAddress address : CHANNEL_INITIALIZATION_REGISTERS)
  {
    CAPTURE(address);
    const auto read = system.iopBus().read32(address);
    REQUIRE(read.status == IOPBusStatus::Completed);
    REQUIRE(read.value == 0);
  }
  REQUIRE(system.iopBus().read32(CHCR).value == START);
  REQUIRE(system.iopBus().read32(0x1f801528).value == 0x01000401);
  REQUIRE(dma.interruptPending());
  REQUIRE(system.iopInterruptController().status() == 0);
  requireState(system, before);
  REQUIRE(system.trace().empty());
  dma.signalChannelCompletion(IOPDMAChannel::SIF1);
  REQUIRE(system.iopInterruptController().status() == DMAC_CAUSE);
  const auto pending = system.saveState();
  for (const IOPAddress address : CHANNEL_INITIALIZATION_REGISTERS)
  {
    CAPTURE(address);
    const auto read = system.iopBus().read32(address);
    REQUIRE(read.status == IOPBusStatus::Completed);
    REQUIRE(read.value == 0);
  }
  requireState(system, pending);
}

TEST_CASE("IOP DMA register initialization rejects other accesses without effects",
  "[iop][dma][channel-read][channel-init][supplemental-init][channel0-init][channel1-init][channel2-init][channel3-init][bus]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  const auto before = system.saveState();
  for (unsigned bit = 0; bit < 32; ++bit)
  {
    REQUIRE(system.iopDMAController().writeChannelRegisterInitialization(
      IOPWord{1} << bit) == IOPDMAWriteOutcome::UnsupportedControl);
    REQUIRE(system.iopDMAController().writeSupplementalInitialization(
      IOPWord{1} << bit) == IOPDMAWriteOutcome::UnsupportedControl);
  }
  for (IOPAddress address : INITIALIZATION_REGISTERS)
  {
    CAPTURE(address);
    for (unsigned bit = 0; bit < 32; ++bit)
    {
      const IOPWord value = IOPWord{1} << bit;
      REQUIRE(bus.write32(address, value).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.writeMasked32(address, value, 0xf).status == IOPBusStatus::Unmapped);
    }
    REQUIRE(bus.write32(address, 0xffffffff).status == IOPBusStatus::Unmapped);
    for (std::uint8_t mask = 1; mask < 0xf; ++mask)
    {
      REQUIRE(bus.writeMasked32(address, 0, mask).status == IOPBusStatus::Unmapped);
    }
    for (IOPAddress lane = 0; lane < 4; ++lane)
    {
      CAPTURE(lane);
      REQUIRE(bus.read8(address + lane).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write8(address + lane, 0).status == IOPBusStatus::Unmapped);
      if (lane != 0)
      {
        REQUIRE(bus.read32(address + lane).status == IOPBusStatus::Misaligned);
        REQUIRE(bus.write32(address + lane, 0).status == IOPBusStatus::Misaligned);
        REQUIRE(bus.writeMasked32(address + lane, 0, 0xf).status ==
          IOPBusStatus::Misaligned);
      }
      const auto halfStatus = (lane & 1) == 0 ? IOPBusStatus::Unmapped :
        IOPBusStatus::Misaligned;
      REQUIRE(bus.read16(address + lane).status == halfStatus);
      REQUIRE(bus.write16(address + lane, 0).status == halfStatus);
    }
    for (std::uint8_t mask : {0u, 0x10u, 0xffu})
    {
      REQUIRE_THROWS_AS(bus.writeMasked32(address + 1, 0, mask),
        std::invalid_argument);
    }
    for (IOPAddress alias : {address | 0x80000000u, address | 0xa0000000u})
    {
      REQUIRE(bus.write32(alias, 0).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.read32(alias).status == IOPBusStatus::Unmapped);
    }
    REQUIRE(bus.regionForRange(address, 4) == IOPBusRegion::Mmio);
    REQUIRE(bus.regionForRange(address, 5) == IOPBusRegion::Unmapped);
    REQUIRE(bus.readDMAC32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.writeDMAC32(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.dmaWriteStatus(address) == IOPBusStatus::Unmapped);
    std::uint8_t byte = 0xa5;
    REQUIRE(bus.loadPhysical(address, &byte, 1) == IOPBusStatus::Unmapped);
    REQUIRE(bus.inspectPhysical(address, &byte, 1) == IOPBusStatus::Unmapped);
    REQUIRE(byte == 0xa5);
  }
  for (const IOPAddress address : SUPPLEMENTAL_INITIALIZATION_REGISTERS)
  {
    CAPTURE(address);
    REQUIRE(bus.read32(address).status == IOPBusStatus::Unmapped);
  }
  for (IOPAddress address : {
    0x1f80107cu, 0x1f80108cu, 0x1f80109cu, 0x1f8010acu, 0x1f8010bcu,
    0x1f8010dcu, 0x1f8010ecu,
    0x1f80150cu, 0x1f80151cu, 0x1f80153cu, 0x1f80154cu, 0x1f80155cu,
    0x1f80156cu, 0x1f80157cu, 0x1f801580u})
  {
    CAPTURE(address);
    REQUIRE(bus.regionForRange(address, 1) == IOPBusRegion::Unmapped);
    REQUIRE(bus.write32(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.writeMasked32(address, 0, 0xf).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read32(address).status == IOPBusStatus::Unmapped);
  }
  requireState(system, before);
}

TEST_CASE("IOP DMA register guest initialization requires zero words and complete merge masks",
  "[iop][dma][channel-read][channel-init][supplemental-init][channel0-init][channel1-init][channel2-init][channel3-init][execution][merge]")
{
  for (IOPAddress physical : INITIALIZATION_REGISTERS)
  {
    for (IOPAddress target : {physical, physical | 0x80000000u, physical | 0xa0000000u})
    {
      for (IOPWord value : {0u, 1u})
      {
        for (IOPWord store : {
          0xac220000u,
          0xa8220000u, 0xa8220001u, 0xa8220002u, 0xa8220003u,
          0xb8220000u, 0xb8220001u, 0xb8220002u, 0xb8220003u})
        {
          const bool completed = value == 0 &&
            (store == 0xac220000 || store == 0xa8220003 || store == 0xb8220000);
          requireInitializationStore(physical, target, value, store,
            completed ? IOPStopReason::None : IOPStopReason::StoreFailure);
        }
      }
    }
  }
}

TEST_CASE("IOP DMA initialization remains stateless after cold reset and restore",
  "[iop][dma][channel-read][dpcr3][channel-init][supplemental-init][channel0-init][channel1-init][channel2-init][channel3-init][conformance][save-state]")
{
  NekoSystem system;
  const auto initialize = [&system]()
  {
    REQUIRE(system.iopBus().write32(DPCR3, 0x777).status == IOPBusStatus::Completed);
    REQUIRE(system.iopBus().writeMasked32(DPCR3, 0x777, 0xf).status ==
      IOPBusStatus::Completed);
    REQUIRE(system.iopBus().read32(DPCR3).status == IOPBusStatus::Unmapped);
    for (const IOPAddress address : CHANNEL_INITIALIZATION_REGISTERS)
    {
      CAPTURE(address);
      const auto read = system.iopBus().read32(address);
      REQUIRE(read.status == IOPBusStatus::Completed);
      REQUIRE(read.value == 0);
    }
    for (IOPAddress address : INITIALIZATION_REGISTERS)
    {
      CAPTURE(address);
      REQUIRE(system.iopBus().write32(address, 0).status == IOPBusStatus::Completed);
      REQUIRE(system.iopBus().writeMasked32(address, 0, 0xf).status ==
        IOPBusStatus::Completed);
    }
    for (const IOPAddress address : CHANNEL_INITIALIZATION_REGISTERS)
    {
      CAPTURE(address);
      const auto read = system.iopBus().read32(address);
      REQUIRE(read.status == IOPBusStatus::Completed);
      REQUIRE(read.value == 0);
    }
    for (const IOPAddress address : SUPPLEMENTAL_INITIALIZATION_REGISTERS)
    {
      REQUIRE(system.iopBus().read32(address).status == IOPBusStatus::Unmapped);
    }
  };
  const auto cold = system.saveState();
  initialize();
  requireState(system, cold);
  system.reset();
  initialize();
  requireState(system, cold);
  system.iopDMAController().writePriority(SECONDARY, 0xffffffff);
  system.loadState(cold);
  initialize();
  requireState(system, cold);
}

TEST_CASE("IOP DMA channel word loads preserve delay aliases and completed trace metadata",
  "[iop][dma][channel-read][execution][trace]")
{
  for (const IOPAddress prefix : {0u, 0x80000000u, 0xa0000000u})
  {
    CAPTURE(prefix);
    NekoSystem traced;
    NekoSystem plain;
    const IOPAddress base = 0x1f800000 | prefix;
    std::vector<IOPWord> program = {0x3c010000 | (base >> 16)};
    IOPAddress destination = 0x400;
    for (const IOPAddress address : CHANNEL_INITIALIZATION_REGISTERS)
    {
      program.push_back(0x24021234); // addiu r2, r0, 0x1234
      program.push_back(0x8c220000 | (address & 0xffff));
      program.push_back(0xac020000 | destination); // store old r2 in load-delay slot
      program.push_back(0xac020000 | (destination + 4));
      program.push_back(0xac200000 | (address & 0xffff));
      destination += 8;
    }
    for (auto *system : {&traced, &plain})
    {
      IOPAddress pc = 0;
      for (const IOPWord word : program)
      {
        REQUIRE(system->iopBus().write32(pc, word).status == IOPBusStatus::Completed);
        pc += 4;
      }
      system->iopCore().startExecution(0xa0000000);
    }
    traced.startTrace();
    traced.runMasterCycles(program.size() * 8);
    plain.runMasterCycles(program.size() * 8);
    REQUIRE(traced.iopCore().executionState() == IOPExecutionState::Running);
    REQUIRE(traced.iopCore().exception().kind == IOPException::None);
    REQUIRE(traced.iopCore().retiredInstructions() == program.size());
    REQUIRE(traced.iopCore().programCounter() == 0xa0000000 + program.size() * 4);
    REQUIRE(traced.iopCore().generalRegister(2) == 0);
    destination = 0x400;
    for (const IOPAddress address : CHANNEL_INITIALIZATION_REGISTERS)
    {
      CAPTURE(address);
      REQUIRE(traced.iopBus().read32(destination).value == 0x1234);
      REQUIRE(traced.iopBus().read32(destination + 4).value == 0);
      destination += 8;
    }
    IOPTraceTest::requireMachine(traced, plain);
    REQUIRE(plain.trace().empty());
    namespace Memory = NekoIOPTraceMemory;
    std::size_t accesses = 0;
    for (const auto &event : traced.trace())
    {
      if (event.subsystem != NekoTraceSubsystem::IOP ||
          event.type != NekoTraceEventType::MemoryAccess ||
          (event.value0 & 0xffff0000) != base)
      {
        continue;
      }
      const auto index = accesses / 2;
      REQUIRE(index < sizeof(CHANNEL_INITIALIZATION_REGISTERS) /
        sizeof(CHANNEL_INITIALIZATION_REGISTERS[0]));
      const bool store = accesses % 2 != 0;
      const IOPAddress physical = CHANNEL_INITIALIZATION_REGISTERS[index];
      REQUIRE(event.masterCycle == (1 + index * 5 + (store ? 4 : 1)) * 8 + 1);
      REQUIRE(event.value0 == (physical | prefix));
      REQUIRE(event.value1 == 0);
      REQUIRE(event.value2 == 0);
      REQUIRE(event.value3 == nekoPackIOPMemoryTraceMetadata({
        physical, 4, store ? Memory::AccessKind::DataStore : Memory::AccessKind::DataLoad,
        Memory::Outcome::Completed, prefix == 0xa0000000 ?
          Memory::CacheRoute::Uncached : Memory::CacheRoute::Cached,
        true, static_cast<std::uint8_t>(store ? 0xf : 0)}));
      ++accesses;
    }
    REQUIRE(accesses == 2 * sizeof(CHANNEL_INITIALIZATION_REGISTERS) /
      sizeof(CHANNEL_INITIALIZATION_REGISTERS[0]));
  }
}

TEST_CASE("IOP DPCR3 guest stores translate aliases and require complete merge masks",
  "[iop][dma][dpcr3][execution][merge]")
{
  for (IOPAddress target : {DPCR3, 0x9f8015f0u, 0xbf8015f0u})
  {
    for (IOPWord value : {0x777u, 0x776u})
    {
      for (IOPWord store : {
        0xac220000u,
        0xa8220000u, 0xa8220001u, 0xa8220002u, 0xa8220003u,
        0xb8220000u, 0xb8220001u, 0xb8220002u, 0xb8220003u})
      {
        const bool completed = value == 0x777 &&
          (store == 0xac220000 || store == 0xa8220003 || store == 0xb8220000);
        requireInitializationStore(DPCR3, target, value, store,
          completed ? IOPStopReason::None : IOPStopReason::StoreFailure);
      }
    }
  }
}

TEST_CASE("IOP DMA auxiliary word follows existing IOP MMIO fetch routing",
  "[iop][dma][auxiliary][cache]")
{
  for (const IOPAddress address : {0x1f801578u, 0x9f801578u, 0xbf801578u})
  {
    CAPTURE(address);
    NekoSystem system;
    auto &core = system.iopCore();
    REQUIRE(system.iopBus().write32(AUXILIARY, 0x24021234).status ==
      IOPBusStatus::Completed);
    core.startExecution(address);
    const auto fetch = core.fetchInstruction();
    REQUIRE(fetch.outcome == IOPAccessOutcome::Completed);
    REQUIRE(fetch.physicalAddress == AUXILIARY);
    REQUIRE(fetch.value == 0x24021234);
    core.stepInstruction();
    REQUIRE(core.executionState() == IOPExecutionState::Running);
    REQUIRE(core.retiredInstructions() == 1);
    REQUIRE(core.generalRegister(2) == 0x1234);
    REQUIRE(system.iopBus().read32(AUXILIARY).value == 0x24021234);
  }
}

TEST_CASE("IOP DMA auxiliary word round trips transactionally with destination wiring",
  "[iop][dma][auxiliary][save-state]")
{
  NekoSystem source;
  REQUIRE(source.iopBus().write32(AUXILIARY, 0x89abcdef).status ==
    IOPBusStatus::Completed);
  const auto state = source.saveState();
  const auto snapshot = inspectSaveState(state);
  const auto *field = snapshot.find("system.iop.dmaController.auxiliaryControl");
  REQUIRE(field != nullptr);
  REQUIRE(field->kind == SaveStateFieldKind::U32);
  REQUIRE(field->scalarValue == 0x89abcdef);
  NekoSystem destination;
  REQUIRE(destination.iopBus().write32(AUXILIARY, 0xffffffff).status ==
    IOPBusStatus::Completed);
  destination.loadState(state);
  REQUIRE(destination.iopStateHash() == source.iopStateHash());
  REQUIRE(destination.iopBus().read32(AUXILIARY).value == 0x89abcdef);
  REQUIRE(destination.saveState() == state);

  REQUIRE(destination.iopBus().write32(AUXILIARY, 0x13579bdf).status ==
    IOPBusStatus::Completed);
  const auto before = destination.saveState();
  SaveStateMutation invalid(state);
  invalid.writeScalar("system.iop.dmaController.banks[0].flags", 1);
  invalid.refreshChecksum();
  REQUIRE_THROWS_AS(destination.loadState(invalid), std::invalid_argument);
  requireState(destination, before);
  REQUIRE(destination.iopCore().writeData32(0xbf801578, 0x2468ace0).outcome ==
    IOPAccessOutcome::Completed);
  REQUIRE(destination.iopBus().read32(AUXILIARY).value == 0x2468ace0);
  REQUIRE(source.iopBus().read32(AUXILIARY).value == 0x89abcdef);
}

TEST_CASE("IOP DMA completion and INTC acknowledgement remain independent",
  "[iop][dma]")
{
  IOPInterruptController interrupts;
  IOPDMAController dma(interrupts);
  dma.signalChannelCompletion(IOPDMAChannel::SIF1);
  REQUIRE(dma.interruptStatus(SECONDARY) == FLAG);
  REQUIRE(dma.interruptPending());
  REQUIRE(interrupts.status() == DMAC_CAUSE);
  REQUIRE_FALSE(interrupts.interruptPending());
  interrupts.writeMask(DMAC_CAUSE);
  interrupts.writeControl(1);
  REQUIRE(interrupts.interruptPending());
  interrupts.writeStatus(~DMAC_CAUSE);
  REQUIRE(interrupts.status() == 0);
  REQUIRE(dma.interruptPending());
  dma.writePriority(SECONDARY, 0xffffffff);
  REQUIRE(dma.interruptStatus(SECONDARY) == FLAG);
  REQUIRE(interrupts.status() == 0);
  dma.writeInterrupt(PRIMARY, 0);
  REQUIRE(interrupts.status() == DMAC_CAUSE);

  dma.writeInterrupt(SECONDARY, 0x00800000);
  REQUIRE(dma.interruptStatus(SECONDARY) == (FLAG | SUMMARY | 0x00800000));
  dma.writeInterrupt(SECONDARY, FLAG);
  REQUIRE(dma.interruptStatus(SECONDARY) == 0);
  REQUIRE_FALSE(dma.interruptPending());
  REQUIRE(interrupts.status() == DMAC_CAUSE);
  interrupts.writeStatus(~DMAC_CAUSE);
  REQUIRE(interrupts.status() == 0);

  for (const auto bank : {PRIMARY, SECONDARY})
  {
    dma.writeInterrupt(bank, FORCE);
    REQUIRE(dma.interruptStatus(bank) == (FORCE | SUMMARY));
    REQUIRE(interrupts.status() == DMAC_CAUSE);
    interrupts.writeStatus(~DMAC_CAUSE);
    REQUIRE(dma.interruptPending());
    REQUIRE(interrupts.status() == 0);
    dma.writeInterrupt(bank, FORCE);
    REQUIRE(interrupts.status() == DMAC_CAUSE);
    dma.writeInterrupt(bank, SUMMARY);
    REQUIRE(dma.interruptStatus(bank) == 0);
    REQUIRE_FALSE(dma.interruptPending());
    interrupts.writeStatus(~DMAC_CAUSE);
  }
  dma.writeInterrupt(PRIMARY, FORCE);
  dma.signalChannelCompletion(IOPDMAChannel::SIF1);
  interrupts.writeStatus(~DMAC_CAUSE);
  dma.writeInterrupt(SECONDARY, FLAG);
  REQUIRE(dma.interruptStatus(SECONDARY) == 0);
  REQUIRE(dma.interruptPending());
  REQUIRE(interrupts.status() == DMAC_CAUSE);
  dma.writeInterrupt(PRIMARY, 0);
  REQUIRE_FALSE(dma.interruptPending());
  interrupts.writeStatus(~DMAC_CAUSE);
  dma.signalChannelCompletion(IOPDMAChannel::SIF1);
  interrupts.writeStatus(~DMAC_CAUSE);
  dma.signalChannelCompletion(IOPDMAChannel::SIF1);
  REQUIRE(interrupts.status() == DMAC_CAUSE);
}

TEST_CASE("IOP DMA channel 10 validates starts active writes and local abort",
  "[iop][dma]")
{
  NekoSystem system;
  auto &channel = system.iopSIF1DMAC();
  REQUIRE(channel.memoryAddress() == 0);
  REQUIRE(channel.blockControl() == 0);
  REQUIRE(channel.channelControl() == 0);
  REQUIRE(channel.writeMemoryAddress(0xff123454) == IOPDMAWriteOutcome::Completed);
  REQUIRE(channel.memoryAddress() == 0x123454);
  REQUIRE(channel.writeMemoryAddress(0x123455) ==
    IOPDMAWriteOutcome::UnalignedAddress);
  REQUIRE(channel.memoryAddress() == 0x123454);
  REQUIRE(channel.writeBlockControl(0xabcd1234) == IOPDMAWriteOutcome::Completed);
  REQUIRE(channel.writeChannelControl(0x701) == IOPDMAWriteOutcome::Completed);
  for (const IOPWord control : {
    0x01000000u, 0x01000401u, 0x01000600u, 0x21000400u, 0x81000400u})
  {
    CAPTURE(control);
    const auto before = system.saveState();
    REQUIRE(channel.writeChannelControl(control) ==
      IOPDMAWriteOutcome::UnsupportedControl);
    requireState(system, before);
  }
  REQUIRE(channel.writeChannelControl(START | 0x100) ==
    IOPDMAWriteOutcome::Completed);
  REQUIRE(channel.active());
  REQUIRE_FALSE(system.iopDMAController().channelEnabled(IOPDMAChannel::SIF1));
  const auto active = system.saveState();
  REQUIRE(channel.writeChannelControl(START | 0x100) ==
    IOPDMAWriteOutcome::Completed);
  REQUIRE(channel.writeChannelControl(START) == IOPDMAWriteOutcome::ActiveTransfer);
  REQUIRE(channel.writeChannelControl(0x01000200) == IOPDMAWriteOutcome::ActiveTransfer);
  REQUIRE(channel.writeMemoryAddress(0x4000) == IOPDMAWriteOutcome::ActiveTransfer);
  REQUIRE(channel.writeBlockControl(0) == IOPDMAWriteOutcome::ActiveTransfer);
  requireState(system, active);
  system.iopDMAController().writePriority(SECONDARY, 0x8000);
  system.runMasterCycles(65);
  REQUIRE(channel.active());
  REQUIRE(channel.memoryAddress() == 0x123454);
  REQUIRE(channel.blockControl() == 0xabcd1234);
  REQUIRE(system.iopDMAController().interruptStatus(SECONDARY) == 0);
  REQUIRE(system.iopInterruptController().status() == 0);
  REQUIRE(system.sif().pushSIF1Quadword({{1, 2, 3, 4}}) ==
    SIFTransferOutcome::Completed);
  system.iopDMAController().signalChannelCompletion(IOPDMAChannel::SIF1);
  const auto sifHash = system.sifStateHash();
  REQUIRE(channel.writeChannelControl(0x500) == IOPDMAWriteOutcome::Completed);
  REQUIRE_FALSE(channel.active());
  REQUIRE(channel.memoryAddress() == 0x123454);
  REQUIRE(channel.blockControl() == 0xabcd1234);
  REQUIRE(system.sifStateHash() == sifHash);
  REQUIRE(system.iopDMAController().interruptStatus(SECONDARY) == FLAG);
  REQUIRE(channel.writeChannelControl(START) == IOPDMAWriteOutcome::Completed);
  REQUIRE(system.iopDMAController().interruptStatus(SECONDARY) == FLAG);
}

TEST_CASE("IOP SIF1 guest startup control stores retire with precise word metadata",
  "[iop][dma][bus][trace][chcr-startup]")
{
  for (const IOPAddress target : {0x9f801538u, 0xbf801538u})
  {
    CAPTURE(target);
    NekoSystem traced;
    NekoSystem plain;
    const IOPWord program[] = {
      0x3c010000 | (target >> 16),
      0x34210000 | (target & 0xffff),
      0x3c024100, 0x34420300,
      0x10000001, 0xac220000,
      0x8c230000, 0
    };
    for (auto *system : {&traced, &plain})
    {
      IOPAddress pc = 0;
      for (const IOPWord word : program)
      {
        REQUIRE(system->iopBus().write32(pc, word).status ==
          IOPBusStatus::Completed);
        pc += 4;
      }
      system->iopCore().startExecution(0xa0000000);
    }
    traced.startTrace();
    traced.runMasterCycles(64);
    plain.runMasterCycles(64);
    REQUIRE(traced.iopCore().stopReason() == IOPStopReason::None);
    REQUIRE(traced.iopCore().exception().kind == IOPException::None);
    REQUIRE(traced.iopCore().retiredInstructions() == 8);
    REQUIRE(traced.iopCore().programCounter() == 0xa0000020);
    REQUIRE(traced.iopCore().generalRegister(3) == 0x41000300);
    REQUIRE(traced.iopSIF1DMAC().channelControl() == 0x41000300);
    REQUIRE(traced.iopStateHash() == plain.iopStateHash());
    REQUIRE(traced.eeStateHash() == plain.eeStateHash());
    REQUIRE(traced.sifStateHash() == plain.sifStateHash());
    REQUIRE(plain.trace().empty());
    namespace Memory = NekoIOPTraceMemory;
    unsigned stores = 0;
    for (const auto &event : traced.trace())
    {
      if (event.subsystem != NekoTraceSubsystem::IOP ||
          event.type != NekoTraceEventType::MemoryAccess ||
          event.value0 != target || event.masterCycle != 41)
      {
        continue;
      }
      REQUIRE(event.value1 == 0x41000300);
      REQUIRE(event.value2 == 0);
      REQUIRE(event.value3 == nekoPackIOPMemoryTraceMetadata({
        CHCR, 4, Memory::AccessKind::DataStore, Memory::Outcome::Completed,
        target < 0xa0000000 ? Memory::CacheRoute::Cached :
          Memory::CacheRoute::Uncached, true, 0xf}));
      ++stores;
    }
    REQUIRE(stores == 1);
  }
}

TEST_CASE("IOP SIF whole-word stops validate atomically and retain other owners",
  "[iop][dma][bus][iop-active-stop]")
{
  for (const IOPAddress physical : {0x1f801528u, CHCR})
  {
    const IOPWord control = physical == CHCR ? 0x41000300 : 0x01000501;
    for (const IOPWord stopped : {
      0u, 0x701u, physical == CHCR ? 0x40000100u : 0x100u})
    {
      CAPTURE(physical);
      CAPTURE(stopped);
      NekoSystem system;
      auto &bus = system.iopBus();
      REQUIRE(bus.write32(physical - 8, 0x123450).status == IOPBusStatus::Completed);
      REQUIRE(bus.write32(physical - 4, 0xabcd0020).status == IOPBusStatus::Completed);
      if (physical != CHCR)
        REQUIRE(bus.write32(physical + 4, 0x1800).status == IOPBusStatus::Completed);
      REQUIRE(bus.write32(physical, control).status == IOPBusStatus::Completed);
      REQUIRE(bus.write32(DICR2, FORCE).status == IOPBusStatus::Completed);
      system.iopDMAController().signalChannelCompletion(IOPDMAChannel::SIF0);
      system.iopDMAController().signalChannelCompletion(IOPDMAChannel::SIF1);
      const auto iopHash = system.iopStateHash();
      const auto eeHash = system.eeStateHash();
      const auto sifHash = system.sifStateHash();
      const auto status = system.iopDMAController().interruptStatus(SECONDARY);
      const auto interrupts = system.iopInterruptController().status();
      REQUIRE(interrupts == DMAC_CAUSE);
      REQUIRE(bus.write32(physical, control).status == IOPBusStatus::Completed);
      REQUIRE(system.iopStateHash() == iopHash);
      for (const IOPWord invalid : {
        2u, 0x80000000u, control ^ 0x100u,
        physical == CHCR ? 0x20000000u : 0x40000000u})
      {
        CAPTURE(invalid);
        REQUIRE(bus.write32(physical, invalid).status == IOPBusStatus::Unmapped);
        REQUIRE(system.iopStateHash() == iopHash);
        REQUIRE(system.eeStateHash() == eeHash);
        REQUIRE(system.sifStateHash() == sifHash);
      }
      REQUIRE(bus.write32(physical, stopped).status == IOPBusStatus::Completed);
      REQUIRE(bus.read32(physical).value == stopped);
      REQUIRE(bus.read32(physical - 8).value == 0x123450);
      REQUIRE(bus.read32(physical - 4).value == 0xabcd0020);
      if (physical != CHCR)
        REQUIRE(bus.read32(physical + 4).value == 0x1800);
      REQUIRE_FALSE(system.iopSIF0DMAC().active());
      REQUIRE_FALSE(system.iopSIF1DMAC().active());
      REQUIRE(system.eeStateHash() == eeHash);
      REQUIRE(system.sifStateHash() == sifHash);
      REQUIRE(system.iopDMAController().interruptStatus(SECONDARY) == status);
      REQUIRE(system.iopInterruptController().status() == interrupts);
      REQUIRE(bus.write32(physical, 0x101).status == IOPBusStatus::Completed);
      REQUIRE(bus.read32(physical).value == 0x101);
      REQUIRE(bus.write32(physical, control).status == IOPBusStatus::Completed);
      REQUIRE(bus.read32(physical).value == control);
    }
  }
}

TEST_CASE("IOP SIF guest zero stops retire through aliases with exact store metadata",
  "[iop][dma][bus][trace][iop-active-stop]")
{
  for (const IOPAddress physical : {0x1f801528u, CHCR})
  {
    for (const IOPAddress segment : {0x80000000u, 0xa0000000u})
    {
      const auto target = physical | segment;
      CAPTURE(target);
      NekoSystem traced;
      NekoSystem plain;
      const IOPWord program[] = {
        0x3c010000 | (target >> 16),
        0x34210000 | (target & 0xffff),
        0x24030055,
        0x10000001, 0xac200000,
        0x8c230000, 0
      };
      for (auto *system : {&traced, &plain})
      {
        REQUIRE(system->iopBus().write32(physical,
          physical == CHCR ? 0x41000300 : 0x01000401).status ==
          IOPBusStatus::Completed);
        IOPAddress pc = 0;
        for (const IOPWord word : program)
        {
          REQUIRE(system->iopBus().write32(pc, word).status ==
            IOPBusStatus::Completed);
          pc += 4;
        }
        system->iopCore().startExecution(0xa0000000);
      }
      traced.startTrace();
      traced.runMasterCycles(56);
      plain.runMasterCycles(56);
      REQUIRE(traced.iopCore().stopReason() == IOPStopReason::None);
      REQUIRE(traced.iopCore().exception().kind == IOPException::None);
      REQUIRE(traced.iopCore().retiredInstructions() == 7);
      REQUIRE(traced.iopCore().programCounter() == 0xa000001c);
      REQUIRE_FALSE(traced.iopCore().branchContinuation().active);
      REQUIRE(traced.iopCore().generalRegister(3) == 0);
      REQUIRE(traced.iopBus().read32(physical).value == 0);
      IOPTraceTest::requireMachine(traced, plain);
      REQUIRE(plain.trace().empty());
      namespace Memory = NekoIOPTraceMemory;
      unsigned stores = 0;
      for (const auto &event : traced.trace())
      {
        if (event.subsystem != NekoTraceSubsystem::IOP ||
            event.type != NekoTraceEventType::MemoryAccess ||
            event.value0 != target || event.masterCycle != 33)
          continue;
        REQUIRE(event.value1 == 0);
        REQUIRE(event.value2 == 0);
        REQUIRE(event.value3 == nekoPackIOPMemoryTraceMetadata({
          physical, 4, Memory::AccessKind::DataStore, Memory::Outcome::Completed,
          segment == 0x80000000 ? Memory::CacheRoute::Cached :
            Memory::CacheRoute::Uncached, true, 0xf}));
        ++stores;
      }
      REQUIRE(stores == 1);
    }
  }
}

TEST_CASE("IOP SIF DMA BCR halfword writes preserve the other lane through both owners",
  "[iop][dma][bus][bcr-halfword]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  const auto initial = system.iopStateHash();
  const auto eeHash = system.eeStateHash();
  const auto sifHash = system.sifStateHash();
  for (const IOPAddress address : {0x1f801524u, BCR})
  {
    CAPTURE(address);
    REQUIRE(bus.write32(address, 0xabcd1234).status == IOPBusStatus::Completed);
    REQUIRE(bus.write16(address, 0x20).status == IOPBusStatus::Completed);
    REQUIRE(bus.read32(address).value == 0xabcd0020);
    REQUIRE(bus.write16(address + 2, 0xfedc).status == IOPBusStatus::Completed);
    REQUIRE(bus.read32(address).value == 0xfedc0020);
    for (IOPWord value : {0u, 0xffffu, 0x8001u})
    {
      REQUIRE(bus.write16(address, static_cast<std::uint16_t>(value)).status ==
        IOPBusStatus::Completed);
      REQUIRE(bus.read32(address).value == (0xfedc0000 | value));
      REQUIRE(bus.write16(address + 2, static_cast<std::uint16_t>(value)).status ==
        IOPBusStatus::Completed);
      REQUIRE(bus.read32(address).value == ((value << 16) | value));
      REQUIRE(bus.write16(address + 2, 0xfedc).status == IOPBusStatus::Completed);
    }
    const auto retained = address == BCR ? system.iopSIF1DMAC().blockControl() :
      system.iopSIF0DMAC().blockControl();
    REQUIRE(retained == 0xfedc8001);
    REQUIRE(system.iopStateHash() != initial);
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Completed);
    REQUIRE(bus.write16(address + 2, 0).status == IOPBusStatus::Completed);
    REQUIRE(bus.read32(address).value == 0);
    REQUIRE(system.iopStateHash() == initial);
  }
  REQUIRE(system.eeStateHash() == eeHash);
  REQUIRE(system.sifStateHash() == sifHash);
  REQUIRE(system.iopDMAController().interruptStatus(SECONDARY) == 0);
  REQUIRE(system.iopInterruptController().status() == 0);
}

TEST_CASE("IOP SIF DMA BCR halfword writes preserve attachment width and active boundaries",
  "[iop][dma][bus][bcr-halfword]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  IOPBus detached;
  IOPBus receiverOnly;
  receiverOnly.attachDMA(&system.iopDMAController(), &system.iopSIF1DMAC());
  for (const IOPAddress address : {0x1f801524u, BCR})
  {
    REQUIRE(bus.write32(address, 0xabcd1234).status == IOPBusStatus::Completed);
    const auto before = system.iopStateHash();
    for (IOPAddress lane : {0u, 2u})
    {
      REQUIRE(detached.write16(address + lane, 0).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.read16(address + lane).status == IOPBusStatus::Unmapped);
    }
    for (IOPAddress lane = 0; lane < 4; ++lane)
    {
      REQUIRE(bus.read8(address + lane).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write8(address + lane, 0).status == IOPBusStatus::Unmapped);
    }
    for (IOPAddress lane : {1u, 3u})
    {
      REQUIRE(bus.write16(address + lane, 0).status == IOPBusStatus::Misaligned);
    }
    for (std::uint8_t mask = 1; mask < 15; ++mask)
    {
      REQUIRE(bus.writeMasked32(address, 0, mask).status == IOPBusStatus::Unmapped);
    }
    REQUIRE(bus.writeDMAC32(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.readDMAC32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(system.iopStateHash() == before);
  }
  for (const IOPAddress address : {0x1f801524u, 0x1f801526u})
  {
    REQUIRE(receiverOnly.write16(address, 0).status == IOPBusStatus::Unmapped);
  }
  for (const IOPAddress address : {
    DPCR, DICR, DPCR2, DICR2, AUXILIARY,
    0x1f801520u, 0x1f801522u, 0x1f801528u, 0x1f80152au,
    0x1f80152cu, 0x1f80152eu, MADR, MADR + 2, CHCR, CHCR + 2})
  {
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(receiverOnly.write16(address, 0).status == IOPBusStatus::Unmapped);
  }
  REQUIRE(receiverOnly.write16(BCR, 0x20).status == IOPBusStatus::Completed);
  REQUIRE(bus.read32(BCR).value == 0xabcd0020);
  REQUIRE(bus.write32(0x1f801528, 0x01000401).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(CHCR, START).status == IOPBusStatus::Completed);
  REQUIRE(system.sif().pushSIF1Quadword({{0x80001000, 4, 1, 2}}) ==
    SIFTransferOutcome::Completed);
  system.iopDMAController().writePriority(SECONDARY, 0x8000);
  system.runMasterCycles(1);
  REQUIRE(system.iopSIF1DMAC().remainingWords() == 4);
  const auto active = system.iopStateHash();
  const auto sifHash = system.sifStateHash();
  for (const IOPAddress address : {0x1f801524u, 0x1f801526u, BCR, BCR + 2})
  {
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Unmapped);
  }
  REQUIRE(system.iopStateHash() == active);
  REQUIRE(system.sifStateHash() == sifHash);
  REQUIRE(system.iopSIF1DMAC().remainingWords() == 4);
  REQUIRE(system.iopSIF1DMAC().terminalPacket());
  REQUIRE(system.iopSIF0DMAC().active());
  REQUIRE(system.iopSIF1DMAC().active());
}

TEST_CASE("IOP SIF DMA guest BCR halfword stores retire with precise lane metadata",
  "[iop][dma][bus][trace][bcr-halfword]")
{
  for (IOPAddress target : {0x9f801524u, 0xbf801524u, 0x9f801534u, 0xbf801534u})
  {
    CAPTURE(target);
    NekoSystem traced;
    NekoSystem plain;
    const auto physical = target & 0x1fffffff;
    const IOPWord program[] = {
      0x3c010000 | (target >> 16),
      0x34210000 | (target & 0xffff),
      0x24020020, // addiu r2, r0, 0x20
      0x10000001, // beq r0, r0, +1
      0xa4220000, // sh r2, 0(r1), in the delay slot
      0x3402ffff, // ori r2, r0, 0xffff
      0xa4220002, // sh r2, 2(r1)
      0x8c230000, // lw r3, 0(r1)
      0
    };
    for (auto *system : {&traced, &plain})
    {
      REQUIRE(system->iopBus().write32(physical, 0xabcd1234).status ==
        IOPBusStatus::Completed);
      IOPAddress pc = 0;
      for (IOPWord word : program)
      {
        REQUIRE(system->iopBus().write32(pc, word).status ==
          IOPBusStatus::Completed);
        pc += 4;
      }
      system->iopCore().startExecution(0xa0000000);
    }
    traced.startTrace();
    traced.runMasterCycles(9 * 8);
    plain.runMasterCycles(9 * 8);
    REQUIRE(traced.iopCore().stopReason() == IOPStopReason::None);
    REQUIRE(traced.iopCore().exception().kind == IOPException::None);
    REQUIRE(traced.iopCore().retiredInstructions() == 9);
    REQUIRE(traced.iopCore().programCounter() == 0xa0000024);
    REQUIRE(traced.iopCore().generalRegister(3) == 0xffff0020);
    REQUIRE(traced.iopBus().read32(physical).value == 0xffff0020);
    REQUIRE(traced.iopStateHash() == plain.iopStateHash());
    REQUIRE(traced.eeStateHash() == plain.eeStateHash());
    REQUIRE(traced.sifStateHash() == plain.sifStateHash());
    REQUIRE(plain.trace().empty());
    namespace Memory = NekoIOPTraceMemory;
    unsigned stores = 0;
    for (const auto &event : traced.trace())
    {
      if (event.subsystem != NekoTraceSubsystem::IOP ||
          event.type != NekoTraceEventType::MemoryAccess ||
          (event.value0 != target && event.value0 != target + 2) ||
          event.value1 == 0xffff0020)
      {
        continue;
      }
      REQUIRE(stores < 2);
      const auto lane = stores * 2;
      REQUIRE(event.masterCycle == 33 + stores * 16);
      REQUIRE(event.value0 == target + lane);
      REQUIRE(event.value1 == (stores == 0 ? 0x20 : 0xffff));
      REQUIRE(event.value2 == 0);
      REQUIRE(event.value3 == nekoPackIOPMemoryTraceMetadata({
        physical + lane, 2, Memory::AccessKind::DataStore,
        Memory::Outcome::Completed,
        target < 0xa0000000 ? Memory::CacheRoute::Cached :
          Memory::CacheRoute::Uncached,
        true, static_cast<std::uint8_t>(3 << lane)}));
      ++stores;
    }
    REQUIRE(stores == 2);
  }
}

TEST_CASE("IOP SIF DMA guest BCR stores retain precise unsupported access failures",
  "[iop][dma][bus][bcr-halfword]")
{
  for (const IOPWord store : {
    0xa0220000u, // sb r2, 0(r1)
    0xa8220001u, // swl r2, 1(r1), partial low halfword mask
    0xb8220002u, // swr r2, 2(r1), partial high halfword mask
    0xa4220000u  // sh r2, 0(r1), while active
  })
  {
    CAPTURE(store);
    IOPInterruptController interrupts;
    IOPDMAController dma(interrupts);
    IOPSIF1DMAChannel channel;
    IOPBus bus;
    bus.attachDMA(&dma, &channel);
    IOPCore core;
    core.attachBus(&bus);
    REQUIRE(channel.writeBlockControl(0xabcd1234) == IOPDMAWriteOutcome::Completed);
    if (store == 0xa4220000)
    {
      REQUIRE(channel.writeChannelControl(START) == IOPDMAWriteOutcome::Completed);
    }
    const IOPWord program[] = {
      0x3c01bf80, 0x34211534, 0x24020020, 0x10000002, store
    };
    IOPAddress pc = 0;
    for (IOPWord word : program)
    {
      REQUIRE(bus.write32(pc, word).status == IOPBusStatus::Completed);
      pc += 4;
    }
    core.startExecution(0xa0000000);
    for (unsigned step = 0; step < 5; ++step)
    {
      core.stepInstruction();
    }
    REQUIRE(core.stopReason() == IOPStopReason::StoreFailure);
    REQUIRE(core.exception().kind == IOPException::None);
    REQUIRE(core.retiredInstructions() == 4);
    REQUIRE(core.programCounter() == 0xa0000010);
    REQUIRE(core.branchContinuation().active);
    REQUIRE(core.storeFailure().access.virtualAddress == 0xbf801534 + (store & 3));
    REQUIRE(core.storeFailure().access.physicalAddress == BCR + (store & 3));
    REQUIRE(core.storeFailure().access.outcome == IOPAccessOutcome::Unmapped);
    REQUIRE(channel.blockControl() == 0xabcd1234);
    REQUIRE(channel.active() == (store == 0xa4220000));
    REQUIRE(channel.remainingWords() == 0);
    REQUIRE(dma.interruptStatus(SECONDARY) == 0);
    REQUIRE(interrupts.status() == 0);
  }
}

TEST_CASE("IOP DMA MMIO admits selected widths and rejects unsupported boundaries atomically",
  "[iop][dma][bus]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  for (const IOPAddress address :
       {DPCR, DICR, DPCR2, DICR2, AUXILIARY, MADR, BCR, CHCR})
  {
    CAPTURE(address);
    REQUIRE(bus.regionForRange(address, 4) == IOPBusRegion::Mmio);
    REQUIRE(bus.read32(address).status == IOPBusStatus::Completed);
    REQUIRE(bus.read32(address).value == 0);
    const auto before = system.saveState();
    for (IOPAddress lane = 0; lane < 4; ++lane)
    {
      REQUIRE(bus.read8(address + lane).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write8(address + lane, 0xff).status == IOPBusStatus::Unmapped);
    }
    for (IOPAddress lane : {0u, 2u})
    {
      REQUIRE(bus.read16(address + lane).status == IOPBusStatus::Unmapped);
      if (address != BCR)
      {
        REQUIRE(bus.write16(address + lane, 0xffff).status == IOPBusStatus::Unmapped);
      }
    }
    for (std::uint8_t lanes = 1; lanes < 15; ++lanes)
    {
      REQUIRE(bus.writeMasked32(address, 0xffffffff, lanes).status ==
        IOPBusStatus::Unmapped);
    }
    REQUIRE(bus.read32(address + 1).status == IOPBusStatus::Misaligned);
    REQUIRE(bus.write32(address + 1, 0).status == IOPBusStatus::Misaligned);
    requireState(system, before);
  }
  const auto before = system.saveState();
  for (const IOPAddress address : {
    0x1f8010f8u, 0x1f80151cu,
    0x1f80153cu, 0x1f801560u, 0x1f80157cu, 0x1f801580u})
  {
    REQUIRE(bus.read32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(address, 0xffffffff).status == IOPBusStatus::Unmapped);
  }
  requireState(system, before);
  for (const auto bank : {PRIMARY, SECONDARY})
  {
    const auto priorityAddress = bank == PRIMARY ? DPCR : DPCR2;
    const auto interruptAddress = bank == PRIMARY ? DICR : DICR2;
    REQUIRE(bus.write32(priorityAddress, 0x87654321).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.read32(priorityAddress).value == 0x87654321);
    REQUIRE(system.iopDMAController().priority(bank) == 0x87654321);
    REQUIRE(bus.write32(interruptAddress, FORCE | 0x321).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.read32(interruptAddress).value == (SUMMARY | FORCE | 0x321));
    REQUIRE(system.iopDMAController().interruptStatus(bank) ==
      (SUMMARY | FORCE | 0x321));
    REQUIRE(bus.write32(interruptAddress, 0).status == IOPBusStatus::Completed);
  }
  REQUIRE(bus.writeMasked32(BCR, 0xabcd1234, 15).status == IOPBusStatus::Completed);
  REQUIRE(bus.read32(BCR).value == 0xabcd1234);
  REQUIRE(bus.write32(MADR, 1).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.write32(CHCR, 0x01000000).status == IOPBusStatus::Unmapped);
  REQUIRE(system.iopCore().writeData32(0xbf801530, 0xff001234).outcome ==
    IOPAccessOutcome::Completed);
  REQUIRE(system.iopCore().readData32(0xbf801530).value == 0x1234);
  REQUIRE(bus.write32(CHCR, START).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(BCR, 0).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.write32(CHCR, 0x400).status == IOPBusStatus::Completed);
  IOPBus unattached;
  REQUIRE(unattached.read32(DPCR).status == IOPBusStatus::Unmapped);
  REQUIRE(unattached.write32(CHCR, START).status == IOPBusStatus::Unmapped);
  REQUIRE_THROWS_AS(
    unattached.attachDMA(nullptr, &system.iopSIF1DMAC()), std::invalid_argument);
  REQUIRE_THROWS_AS(
    unattached.attachDMA(&system.iopDMAController(), nullptr), std::invalid_argument);
  unattached.attachDMA(&system.iopDMAController(), &system.iopSIF1DMAC());
  REQUIRE_THROWS_AS(
    unattached.attachDMA(&system.iopDMAController(), &system.iopSIF1DMAC()),
    std::logic_error);
}

TEST_CASE("IOP DMA force reaches real cause 3 and the existing IOP exception owner",
  "[iop][dma][system]")
{
  NekoSystem system;
  REQUIRE(system.iopBus().write32(0, 0x34010401).status ==
    IOPBusStatus::Completed); // ori r1, r0, IEc | IM2
  REQUIRE(system.iopBus().write32(4, 0x40816000).status ==
    IOPBusStatus::Completed); // mtc0 r1, Status
  system.iopCore().startExecution(0xa0000000);
  system.runMasterCycles(9);
  system.iopInterruptController().writeMask(DMAC_CAUSE);
  system.iopInterruptController().writeControl(1);
  REQUIRE(system.iopBus().write32(DICR, FORCE).status == IOPBusStatus::Completed);
  system.runMasterCycles(7);
  REQUIRE(system.iopCore().programCounter() == 0xa0000008);
  system.clockMasterCycle();
  REQUIRE(system.iopCore().exception().kind == IOPException::Interrupt);
  REQUIRE(system.iopCore().exceptionProgramCounter() == 0xa0000008);
  REQUIRE(system.iopCore().retiredInstructions() == 2);
  REQUIRE(system.iopInterruptController().status() == DMAC_CAUSE);
}

TEST_CASE("IOP DMA persistence preserves independent acknowledgement without republication",
  "[iop][dma][save-state]")
{
  NekoSystem original;
  auto &dma = original.iopDMAController();
  auto &channel = original.iopSIF1DMAC();
  const auto emptyHash = original.iopStateHash();
  dma.writePriority(PRIMARY, 0x76543210);
  dma.writePriority(SECONDARY, 0xffffbfff);
  REQUIRE(channel.writeMemoryAddress(0x1234) == IOPDMAWriteOutcome::Completed);
  REQUIRE(channel.writeBlockControl(0x12345678) == IOPDMAWriteOutcome::Completed);
  REQUIRE(channel.writeChannelControl(START) == IOPDMAWriteOutcome::Completed);
  dma.writeInterrupt(PRIMARY, FORCE);
  dma.signalChannelCompletion(IOPDMAChannel::SIF1);
  dma.writeInterrupt(SECONDARY, 0x00800000);
  original.iopInterruptController().writeMask(0x0a);
  original.iopInterruptController().writeControl(1);
  original.iopInterruptController().assertSource(IOPInterruptSource::SBUS);
  original.iopInterruptController().writeStatus(~DMAC_CAUSE);
  REQUIRE(original.iopInterruptController().status() == 2);
  REQUIRE(original.iopStateHash() != emptyHash);
  original.runMasterCycles(9);
  original.startTrace();
  NekoSystem restored;
  restored.iopDMAController().writeInterrupt(SECONDARY, FORCE);
  REQUIRE(restored.iopSIF1DMAC().writeChannelControl(0x701) ==
    IOPDMAWriteOutcome::Completed);
  restored.startTrace();
  restored.loadState(original.saveState());
  restored.clearTrace();
  IOPTraceTest::requireMachine(restored, original);
  REQUIRE(restored.iopDMAController().interruptPending());
  REQUIRE(restored.iopInterruptController().status() == 2);
  original.runMasterCycles(16);
  restored.runMasterCycles(16);
  IOPTraceTest::requireMachine(restored, original);
  IOPTraceTest::requireEvents(restored.trace(), original.trace());
  REQUIRE(restored.iopInterruptController().status() == 2);
  REQUIRE(restored.iopBus().write32(DICR2, FLAG).status == IOPBusStatus::Completed);
  REQUIRE(restored.iopInterruptController().status() == 0x0a);
  restored.iopInterruptController().writeStatus(~DMAC_CAUSE);
  REQUIRE(restored.iopBus().write32(DICR, 0).status == IOPBusStatus::Completed);
  REQUIRE_FALSE(restored.iopDMAController().interruptPending());
  REQUIRE(restored.iopInterruptController().status() == 2);
  restored.iopDMAController().signalChannelCompletion(IOPDMAChannel::SIF1);
  REQUIRE(restored.iopInterruptController().status() == 0x0a);
  restored.reset();
  NekoSystem fresh;
  IOPTraceTest::requireMachine(restored, fresh);
  REQUIRE(restored.iopBus().write32(DICR, FORCE).status == IOPBusStatus::Completed);
  REQUIRE(restored.iopInterruptController().status() == DMAC_CAUSE);
}

TEST_CASE("IOP DMA malformed persistence rejects before destination mutation",
  "[iop][dma][save-state]")
{
  struct InvalidField
  {
    const char *path;
    std::uint64_t value;
  };
  NekoSystem source;
  NekoSystem destination;
  REQUIRE(source.iopBus().write32(SPU_MADR[0], 0x4fa00).status ==
    IOPBusStatus::Completed);
  REQUIRE(source.iopBus().write32(SPU_MADR[1], 0x00fffffc).status ==
    IOPBusStatus::Completed);
  REQUIRE(destination.iopBus().write32(SPU_MADR[0], 0x100).status ==
    IOPBusStatus::Completed);
  REQUIRE(destination.iopBus().write32(SPU_MADR[1], 0x200).status ==
    IOPBusStatus::Completed);
  REQUIRE(source.iopBus().write32(SPU_BCR[0], 0xffffffff).status ==
    IOPBusStatus::Completed);
  REQUIRE(source.iopBus().write32(SPU_BCR[1], 0x80000010).status ==
    IOPBusStatus::Completed);
  REQUIRE(destination.iopBus().write32(SPU_BCR[0], 0x12345678).status ==
    IOPBusStatus::Completed);
  REQUIRE(destination.iopBus().write32(SPU_BCR[1], 0x89abcdef).status ==
    IOPBusStatus::Completed);
  destination.iopDMAController().writeInterrupt(PRIMARY, FORCE);
  destination.runMasterCycles(9);
  destination.startTrace();
  const auto before = destination.saveState();
  const auto traceHash = destination.traceHash();
  const auto sourceState = source.saveState();
  for (const auto field : {
    InvalidField{"system.iop.dmaController.spuMemoryAddress[0].value", 1},
    InvalidField{"system.iop.dmaController.spuMemoryAddress[0].value", 0x01000000},
    InvalidField{"system.iop.dmaController.spuMemoryAddress[1].value", 3},
    InvalidField{"system.iop.dmaController.spuMemoryAddress[1].value", 0x80000000},
    InvalidField{"system.iop.dmaController.banks[0].control", 0x01000000},
    InvalidField{"system.iop.dmaController.banks[0].flags", 1},
    InvalidField{"system.iop.dmaController.banks[1].flags", 2},
    InvalidField{"system.iop.sif1DMA.memoryAddress", 0x01000000},
    InvalidField{"system.iop.sif1DMA.memoryAddress", 1},
    InvalidField{"system.iop.sif1DMA.channelControl", 0x01000000},
    InvalidField{"system.iop.sif1DMA.channelControl", 0x20000000}})
  {
    CAPTURE(field.path);
    CAPTURE(field.value);
    SaveStateMutation corrupt(sourceState);
    corrupt.writeScalar(field.path, field.value);
    corrupt.refreshChecksum();
    REQUIRE_THROWS_AS(destination.loadState(corrupt.bytes()), std::invalid_argument);
    requireState(destination, before);
    REQUIRE(destination.traceEnabled());
    REQUIRE(destination.traceHash() == traceHash);
  }
  SaveStateMutation checksum(sourceState);
  checksum.writeScalar("system.iop.dmaController.spuBlockControl[0].value", 0);
  REQUIRE_THROWS_AS(destination.loadState(checksum.bytes()), std::invalid_argument);
  requireState(destination, before);
}

TEST_CASE("IOP DMA canonical registers independently contribute to IOP hashes",
  "[iop][dma][save-state]")
{
  NekoSystem system;
  const auto hash = system.iopStateHash();
  for (std::size_t core = 0; core < 2; ++core)
  {
    for (IOPWord bit : {1u, 0x8000u, 0x10000u, 0x80000000u})
    {
      system.iopDMAController().writeSPUBlockControl(core, bit);
      REQUIRE(system.iopStateHash() != hash);
      system.iopDMAController().writeSPUBlockControl(core, 0);
      REQUIRE(system.iopStateHash() == hash);
    }
    REQUIRE(system.iopDMAController().writeSPUMemoryAddress(core, 4) ==
      IOPDMAWriteOutcome::Completed);
    REQUIRE(system.iopStateHash() != hash);
    REQUIRE(system.iopDMAController().writeSPUMemoryAddress(core, 0) ==
      IOPDMAWriteOutcome::Completed);
    REQUIRE(system.iopStateHash() == hash);
  }
  for (const auto bank : {PRIMARY, SECONDARY})
  {
    auto &dma = system.iopDMAController();
    dma.writePriority(bank, 1);
    REQUIRE(system.iopStateHash() != hash);
    dma.writePriority(bank, 0);
    REQUIRE(system.iopStateHash() == hash);
    dma.writeInterrupt(bank, 1);
    REQUIRE(system.iopStateHash() != hash);
    dma.writeInterrupt(bank, 0);
    REQUIRE(system.iopStateHash() == hash);
  }
  auto &channel = system.iopSIF1DMAC();
  REQUIRE(channel.writeMemoryAddress(4) == IOPDMAWriteOutcome::Completed);
  REQUIRE(system.iopStateHash() != hash);
  REQUIRE(channel.writeMemoryAddress(0) == IOPDMAWriteOutcome::Completed);
  REQUIRE(system.iopStateHash() == hash);
  REQUIRE(channel.writeBlockControl(1) == IOPDMAWriteOutcome::Completed);
  REQUIRE(system.iopStateHash() != hash);
  REQUIRE(channel.writeBlockControl(0) == IOPDMAWriteOutcome::Completed);
  REQUIRE(system.iopStateHash() == hash);
  REQUIRE(channel.writeChannelControl(0x100) == IOPDMAWriteOutcome::Completed);
  REQUIRE(system.iopStateHash() != hash);
  REQUIRE(channel.writeChannelControl(0) == IOPDMAWriteOutcome::Completed);
  REQUIRE(system.iopStateHash() == hash);
  system.iopDMAController().signalChannelCompletion(IOPDMAChannel::SIF1);
  system.iopInterruptController().writeStatus(~DMAC_CAUSE);
  REQUIRE(system.iopStateHash() != hash);
  system.iopDMAController().writeInterrupt(SECONDARY, FLAG);
  REQUIRE(system.iopStateHash() == hash);
}
