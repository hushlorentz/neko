#include <cstdint>
#include <stdexcept>
#include <string>

#include "catch.hpp"
#include "iop/iop_trace_test_utils.hpp"
#include "save_state_mutation.hpp"

namespace
{
  constexpr auto BANK = IOPDMABank::Secondary;
  constexpr std::uint32_t START = DMACChannelControl::START;
  constexpr std::uint32_t CHAIN = DMACChannelControl::CHAIN_MODE;
  constexpr SIFQuadword HEADER = {{0x70000001, 0x3000, 0xaaaa, 0xbbbb}};
  constexpr SIFQuadword PAYLOAD = {{1, 2, 3, 4}};

  void push(NekoSystem &system, const SIFQuadword &words)
  {
    REQUIRE(system.sif().pushSIF0Prologue(words) == SIFTransferOutcome::Completed);
  }

  void appendWord(NekoSystem &system, std::uint32_t word)
  {
    REQUIRE(system.sif().pushSIF0PayloadWord(word, SIFPayloadBoundary::Continuing) ==
      SIFTransferOutcome::Completed);
  }

  void writeIOP(NekoSystem &system, IOPAddress address, IOPWord value)
  {
    REQUIRE(system.iopBus().write32(address, value).status == IOPBusStatus::Completed);
  }

  void receiver(NekoSystem &system, std::uint32_t control = START | CHAIN)
  {
    system.sif0DMAC().writeChannelControl(control);
    system.dmacController().writeControl(1);
  }

  void installTransfer(NekoSystem &system, unsigned count = 7,
    std::uint32_t tag = 0x70000002, std::uint32_t control = START | CHAIN)
  {
    writeIOP(system, 0x1000, 0x80002000);
    writeIOP(system, 0x1004, count);
    writeIOP(system, 0x1008, tag);
    writeIOP(system, 0x100c, 0x3000);
    writeIOP(system, 0x1010, 0xaaaa);
    writeIOP(system, 0x1014, 0xbbbb);
    for (unsigned index = 0; index < count; ++index)
      writeIOP(system, 0x2000 + index * 4, index + 1);
    REQUIRE(system.iopSIF0DMAC().writeTagAddress(0x1000) ==
      IOPDMAWriteOutcome::Completed);
    REQUIRE(system.iopSIF0DMAC().writeChannelControl(0x01000401) ==
      IOPDMAWriteOutcome::Completed);
    system.iopDMAController().writePriority(BANK, 0x800);
    receiver(system, control);
  }

  void requireQuadword(NekoSystem &system, std::uint32_t address,
    const SIFQuadword &expected)
  {
    EEQuadword value = {};
    REQUIRE(system.eeBus().readDMAC128(address, &value));
    REQUIRE(static_cast<std::uint32_t>(value.low) == expected[0]);
    REQUIRE(static_cast<std::uint32_t>(value.low >> 32) == expected[1]);
    REQUIRE(static_cast<std::uint32_t>(value.high) == expected[2]);
    REQUIRE(static_cast<std::uint32_t>(value.high >> 32) == expected[3]);
  }

  void requireTransferCompleted(NekoSystem &system)
  {
    REQUIRE_FALSE(system.iopSIF0DMAC().active());
    REQUIRE(system.iopSIF0DMAC().remainingWords() == 0);
    REQUIRE(system.iopSIF0DMAC().memoryAddress() == 0x201c);
    REQUIRE_FALSE(system.sif0DMAC().active());
    REQUIRE(system.sif0DMAC().memoryAddress() == 0x3020);
    REQUIRE(system.sif0DMAC().quadwordCount() == 0);
    REQUIRE(system.sif().wordCount(SIFDirection::SIF0) == 0);
    REQUIRE(system.sif().sif0PackingCursor() == 3);
    REQUIRE((system.dmacController().status() & DMACStatus::CHANNEL_5) != 0);
    REQUIRE((system.iopDMAController().interruptStatus(BANK) & 0x04000000) != 0);
    requireQuadword(system, 0x3000, PAYLOAD);
    requireQuadword(system, 0x3010, {{5, 6, 7, 4}});
  }

  void executeEE(NekoSystem &system, std::uint32_t instruction)
  {
    system.eeBus().write32(0, instruction);
    system.eeCore().startExecution(0xa0000000);
    system.clockMasterCycle();
    REQUIRE_FALSE(system.eeCore().exceptionPending());
    REQUIRE(system.eeCore().programCounter() == 0xa0000004);
    system.eeCore().haltExecution();
  }

  struct Field
  {
    const char *path;
    std::uint32_t value;
  };
}

TEST_CASE("SIF0 composed packing boundaries restore observation wiring and active reset",
  "[sif0][persistence][lifecycle]")
{
  for (const unsigned prefix : {0u, 1u, 2u, 9u, 17u, 25u, 33u, 34u,
    41u, 49u, 57u, 58u})
  {
    CAPTURE(prefix);
    NekoSystem original;
    installTransfer(original);
    original.runMasterCycles(prefix);
    const auto saved = original.saveState();
    NekoSystem restored;
    installTransfer(restored, 1, 0x70000001);
    restored.startTrace();
    restored.runMasterCycles(10);
    const auto history = restored.trace();
    restored.loadState(saved);
    REQUIRE(restored.traceEnabled());
    IOPTraceTest::requireEvents(restored.trace(), history);
    REQUIRE(restored.saveState() == saved);
    original.clockMasterCycle();
    restored.clockMasterCycle();
    IOPTraceTest::requireMachine(restored, original);
    original.startTrace();
    restored.startTrace();
    original.runMasterCycles(64);
    restored.runMasterCycles(64);
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    requireTransferCompleted(original);
    for (auto *system : {&original, &restored})
    {
      system->loadState(saved);
      system->reset();
      REQUIRE_FALSE(system->traceEnabled());
      REQUIRE(system->trace().empty());
      NekoSystem fresh;
      IOPTraceTest::requireMachine(*system, fresh);
      installTransfer(*system);
      system->startTrace();
      system->runMasterCycles(58);
      requireTransferCompleted(*system);
    }
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
  }
}

TEST_CASE("SIF0 scheduled partial tag and payload waits preserve every queued word",
  "[sif0][persistence][framing]")
{
  for (const bool header : {false, true})
    for (unsigned count = 0; count < 4; ++count)
    {
      CAPTURE(header);
      CAPTURE(count);
      NekoSystem original;
      receiver(original);
      if (!header)
      {
        push(original, HEADER);
        original.clockMasterCycle();
        REQUIRE(original.sif0DMAC().quadwordCount() == 1);
      }
      const auto &words = header ? HEADER : PAYLOAD;
      for (unsigned index = 0; index < count; ++index)
        appendWord(original, words[index]);
      const auto transportHash = original.sifStateHash();
      const auto receiverHash = original.sif0DMAC().stateHash();
      NekoSystem restored;
      restored.loadState(original.saveState());
      original.startTrace();
      restored.startTrace();
      original.runMasterCycles(9);
      restored.runMasterCycles(9);
      REQUIRE(original.sifStateHash() == transportHash);
      REQUIRE(original.sif0DMAC().stateHash() == receiverHash);
      REQUIRE(original.trace().size() == 9);
      const auto metadata =
        static_cast<std::uint64_t>(NekoSIFDMATrace::Endpoint::EESIF0) |
        (static_cast<std::uint64_t>(header ? NekoSIFDMATrace::Kind::DestinationTag :
          NekoSIFDMATrace::Kind::Payload) << NekoSIFDMATrace::KIND_SHIFT) |
        (static_cast<std::uint64_t>(NekoSIFDMATrace::Outcome::Wait) <<
          NekoSIFDMATrace::OUTCOME_SHIFT);
      for (const auto &event : original.trace())
      {
        REQUIRE(event.type == NekoTraceEventType::DMABeat);
        REQUIRE(event.value0 == metadata);
      }
      IOPTraceTest::requireEvents(restored.trace(), original.trace());
      for (auto *system : {&original, &restored})
      {
        for (unsigned index = count; index < 4; ++index)
          appendWord(*system, words[index]);
        system->clockMasterCycle();
        if (header)
        {
          REQUIRE(system->sif0DMAC().quadwordCount() == 1);
          push(*system, PAYLOAD);
          system->clockMasterCycle();
        }
        REQUIRE_FALSE(system->sif0DMAC().active());
        REQUIRE(system->sif().wordCount(SIFDirection::SIF0) == 0);
        requireQuadword(*system, 0x3000, PAYLOAD);
      }
      IOPTraceTest::requireMachine(restored, original);
      IOPTraceTest::requireEvents(restored.trace(), original.trace());
    }
}

TEST_CASE("SIF0 accepted destination packets restore independently of transport CTRL and source completion",
  "[sif0][persistence][destination]")
{
  for (const auto id : {0u, 1u, 7u})
    for (const bool tieIRQ : {false, true})
    {
      CAPTURE(id);
      CAPTURE(tieIRQ);
      NekoSystem original;
      installTransfer(original, 7, (id << 28) | (tieIRQ ? 0x80000000 : 0) | 2,
        START | CHAIN | (tieIRQ ? DMACChannelControl::TAG_INTERRUPT_ENABLE : 0));
      original.dmacController().writeControl(1 | (1u << 4));
      original.runMasterCycles(34);
      REQUIRE(original.sif0DMAC().quadwordCount() == 1);
      REQUIRE(original.iopSIF0DMAC().remainingWords() == 3);
      REQUIRE(original.dmacController().stallAddress() == (id == 0 ? 0x3010 : 0));
      SaveStateMutation independent(original.saveState());
      independent.writeScalar("system.sif.control", 0);
      independent.refreshChecksum();
      original.loadState(independent.bytes());
      NekoSystem restored;
      restored.loadState(independent.bytes());
      original.startTrace();
      restored.startTrace();
      for (auto *system : {&original, &restored})
      {
        system->runMasterCycles(24);
        REQUIRE_FALSE(system->iopSIF0DMAC().active());
        REQUIRE(system->sif0DMAC().quadwordCount() == 0);
        REQUIRE(system->sif0DMAC().active() == (id != 7 && !tieIRQ));
        REQUIRE(system->dmacController().stallAddress() == (id == 0 ? 0x3020 : 0));
        requireQuadword(*system, 0x3000, PAYLOAD);
        requireQuadword(*system, 0x3010, {{5, 6, 7, 4}});
        if (system->sif0DMAC().active())
        {
          push(*system, {{0x70000000, 0x3020, 0, 0}});
          system->clockMasterCycle();
        }
        REQUIRE_FALSE(system->sif0DMAC().active());
        REQUIRE((system->dmacController().status() & DMACStatus::CHANNEL_5) != 0);
      }
      IOPTraceTest::requireMachine(restored, original);
      IOPTraceTest::requireEvents(restored.trace(), original.trace());
    }
}

TEST_CASE("SIF0 nonterminal source packet restore preserves overlapping descriptor handoff",
  "[sif0][persistence][framing]")
{
  for (const unsigned prefix : {17u, 25u, 33u, 66u})
  {
    CAPTURE(prefix);
    NekoSystem original;
    installTransfer(original, 3, 0x10000001);
    writeIOP(original, 0x1000, 0x2000);
    writeIOP(original, 0x1010, 0x80002200);
    writeIOP(original, 0x1014, 5);
    writeIOP(original, 0x1018, 0x70000002);
    writeIOP(original, 0x101c, 0x3010);
    writeIOP(original, 0x1020, 0xcccc);
    writeIOP(original, 0x1024, 0xdddd);
    for (unsigned index = 0; index < 5; ++index)
      writeIOP(original, 0x2200 + index * 4, index + 5);
    original.runMasterCycles(prefix);
    NekoSystem restored;
    restored.loadState(original.saveState());
    IOPTraceTest::requireMachine(restored, original);
    original.startTrace();
    restored.startTrace();
    for (auto *system : {&original, &restored})
    {
      system->runMasterCycles(80);
      REQUIRE_FALSE(system->iopSIF0DMAC().active());
      REQUIRE(system->iopSIF0DMAC().tagAddress() == 0x1020);
      REQUIRE(system->iopSIF0DMAC().memoryAddress() == 0x2214);
      REQUIRE_FALSE(system->sif0DMAC().active());
      REQUIRE(system->sif0DMAC().memoryAddress() == 0x3030);
      REQUIRE(system->sif().wordCount(SIFDirection::SIF0) == 0);
      REQUIRE(system->sif().sif0PackingCursor() == 1);
      requireQuadword(*system, 0x3000, {{1, 2, 3, 5}});
      requireQuadword(*system, 0x3010, {{5, 6, 7, 8}});
      requireQuadword(*system, 0x3020, {{9, 6, 7, 8}});
    }
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
  }
}

TEST_CASE("SIF0 final fill waits atomically for shared capacity across restore",
  "[sif0][persistence][framing]")
{
  NekoSystem original;
  installTransfer(original, 3, 0x70000001);
  original.runMasterCycles(17);
  REQUIRE(original.sif().sif0PackingCursor() == 2);
  REQUIRE(original.iopSIF0DMAC().remainingWords() == 1);
  REQUIRE(original.sif().pushSIF1Quadword(PAYLOAD) == SIFTransferOutcome::Completed);
  REQUIRE(original.sif().consumeWords(SIFDirection::SIF1, 3) ==
    SIFTransferOutcome::Completed);
  for (unsigned index = 0; index < 7; ++index)
    REQUIRE(original.sif().pushSIF1Quadword(PAYLOAD) == SIFTransferOutcome::Completed);
  REQUIRE(original.sif().freeWordCount() == 1);
  const auto transportHash = original.sifStateHash();
  original.runMasterCycles(8);
  REQUIRE(original.sifStateHash() == transportHash);
  REQUIRE(original.iopSIF0DMAC().memoryAddress() == 0x2008);
  REQUIRE(original.iopSIF0DMAC().remainingWords() == 1);
  REQUIRE(original.iopSIF0DMAC().active());
  NekoSystem restored;
  restored.loadState(original.saveState());
  original.startTrace();
  restored.startTrace();
  for (auto *system : {&original, &restored})
  {
    REQUIRE(system->sif().consumeWords(SIFDirection::SIF1, 1) ==
      SIFTransferOutcome::Completed);
    system->runMasterCycles(9);
    REQUIRE_FALSE(system->iopSIF0DMAC().active());
    REQUIRE_FALSE(system->sif0DMAC().active());
    REQUIRE(system->sif().wordCount(SIFDirection::SIF0) == 0);
    REQUIRE(system->sif().wordCount(SIFDirection::SIF1) == 28);
    REQUIRE(system->sif().sif0PackingCursor() == 3);
    requireQuadword(*system, 0x3000, {{1, 2, 3, 0xbbbb}});
  }
  IOPTraceTest::requireMachine(restored, original);
  IOPTraceTest::requireEvents(restored.trace(), original.trace());
}

TEST_CASE("SIF0 physical destination preserves clean and dirty EE cached aliases through restore",
  "[sif0][persistence][cache]")
{
  for (const bool dirty : {false, true})
  {
    CAPTURE(dirty);
    NekoSystem original;
    auto &core = original.eeCore();
    core.setCOP0Register(EECOP0Register::Config, EECOP0Config::DATA_CACHE_ENABLE);
    core.setGeneralRegister(1, {0x80003000, 0});
    core.setGeneralRegister(2, {0xa0003000, 0});
    core.setGeneralRegister(3, {0x22222222, 0});
    REQUIRE(original.eeBus().writeData32(0x3000, 0x11111111));
    executeEE(original, 0x8c240000); // lw r4, 0(r1)
    REQUIRE(core.generalRegister(4).low == 0x11111111);
    if (dirty)
      executeEE(original, 0xac230000); // sw r3, 0(r1)
    REQUIRE(original.eeBus().read32(0x3000) == 0x11111111);
    installTransfer(original);
    original.runMasterCycles(17);
    const auto saved = original.saveState();
    NekoSystem restored;
    restored.loadState(saved);
    original.startTrace();
    restored.startTrace();
    for (auto *system : {&original, &restored})
    {
      system->runMasterCycles(80);
      requireTransferCompleted(*system);
      executeEE(*system, 0x8c250000); // lw r5, 0(r1)
      REQUIRE(system->eeCore().generalRegister(5).low ==
        (dirty ? 0x22222222 : 0x11111111));
      executeEE(*system, 0x8c460000); // lw r6, 0(r2)
      REQUIRE(system->eeCore().generalRegister(6).low == 1);
      if (dirty)
      {
        executeEE(*system, 0xbc3c0000); // dhwob 0(r1)
        REQUIRE(system->eeBus().read32(0x3000) == 0x22222222);
      }
      executeEE(*system, 0xbc3a0000); // dhin 0(r1)
      executeEE(*system, 0x8c270000); // lw r7, 0(r1)
      REQUIRE(system->eeCore().generalRegister(7).low == (dirty ? 0x22222222 : 1));
    }
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    original.loadState(saved);
    original.reset();
    NekoSystem fresh;
    IOPTraceTest::requireMachine(original, fresh);
  }
}

TEST_CASE("SIF0 physical source ignores isolated and swapped IOP cached aliases through restore",
  "[sif0][persistence][cache]")
{
  for (const bool swapped : {false, true})
  {
    CAPTURE(swapped);
    NekoSystem original;
    const auto mode = original.iopCore().status() | IOPCOP0Status::CACHE_ISOLATED |
      (swapped ? IOPCOP0Status::CACHES_SWAPPED : 0);
    writeIOP(original, 0, 0x3c010000 | (mode >> 16));
    writeIOP(original, 4, 0x34210000 | (mode & 0xffff));
    writeIOP(original, 8, 0x40816000); // mtc0 r1, Status
    original.iopCore().startExecution(0xa0000000);
    for (unsigned index = 0; index < 3; ++index)
      original.iopCore().stepInstruction();
    original.iopCore().haltExecution();
    REQUIRE(original.iopCore().status() == mode);
    installTransfer(original);
    for (const auto address : {0x1000u, 0x1004u, 0x1008u, 0x100cu,
      0x1010u, 0x1014u, 0x2000u})
    {
      REQUIRE(original.iopCore().writeData32(0x80000000 | address, 0x99999999).outcome ==
        IOPAccessOutcome::Completed);
    }
    REQUIRE(original.iopCore().readData32(0x80002000).value == 0x99999999);
    REQUIRE(original.iopBus().read32(0x2000).value == 1);
    original.runMasterCycles(17);
    const auto saved = original.saveState();
    NekoSystem restored;
    restored.loadState(saved);
    original.startTrace();
    restored.startTrace();
    for (auto *system : {&original, &restored})
    {
      system->runMasterCycles(64);
      requireTransferCompleted(*system);
      REQUIRE(system->iopCore().status() == mode);
      REQUIRE(system->iopCore().readData32(0xa0002000).value == 0x99999999);
      REQUIRE(system->iopBus().read32(0x2000).value == 1);
    }
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    original.loadState(saved);
    original.reset();
    NekoSystem fresh;
    IOPTraceTest::requireMachine(original, fresh);
  }
}

TEST_CASE("SIF0 continuation and packing hashes include independent valid states",
  "[sif0][persistence][hash]")
{
  NekoSystem original;
  installTransfer(original, 7, 2);
  original.runMasterCycles(17);
  const auto saved = original.saveState();
  for (const auto field : {
    Field{"system.iop.sif0DMA.remainingWords", 4},
    Field{"system.iop.sif0DMA.terminal", 0},
    Field{"system.sif0DMAC.destinationPacketAccepted", 0},
    Field{"system.sif.packingCursor", 0},
    Field{"system.sif.packingLatch[0].value", 0x12345678},
    Field{"system.sif.packingLatch[1].value", 0x12345678},
    Field{"system.sif.packingLatch[2].value", 0x12345678},
    Field{"system.sif.packingLatch[3].value", 0x12345678},
    Field{"system.sif.sif0.words[0].value", 0x12345678}})
  {
    CAPTURE(field.path);
    SaveStateMutation independent(saved);
    independent.writeScalar(field.path, field.value);
    independent.refreshChecksum();
    NekoSystem restored;
    restored.loadState(independent.bytes());
    REQUIRE(restored.saveState() == independent.bytes());
    const bool source = std::string(field.path).find("system.iop.") == 0;
    const bool destination = std::string(field.path).find("system.sif0DMAC.") == 0;
    REQUIRE((restored.iopStateHash() != original.iopStateHash()) == source);
    REQUIRE((restored.eeStateHash() != original.eeStateHash()) == destination);
    REQUIRE((restored.sif0DMAC().stateHash() != original.sif0DMAC().stateHash()) ==
      destination);
    REQUIRE((restored.sifStateHash() != original.sifStateHash()) ==
      (!source && !destination));
  }
}

TEST_CASE("SIF0 malformed restore preserves active endpoints transport and ordered observations",
  "[sif0][persistence][transactional]")
{
  NekoSystem original;
  installTransfer(original);
  original.startTrace();
  original.runMasterCycles(17);
  const auto saved = original.saveState();
  const auto history = original.trace();
  const auto eeHash = original.eeStateHash();
  const auto iopHash = original.iopStateHash();
  const auto sifHash = original.sifStateHash();
  for (const auto field : {
    Field{"system.iop.sif0DMA.remainingWords", 0},
    Field{"system.iop.sif0DMA.remainingWords", 0x100000},
    Field{"system.iop.sif0DMA.channelControl", 0x401},
    Field{"system.sif0DMAC.destinationPacketAccepted", 0},
    Field{"system.sif0DMAC.terminateAfterPacket", 0},
    Field{"system.sif0DMAC.quadwordCountRegister", 0},
    Field{"system.sif.packingCursor", 4},
    Field{"system.sif.sif0.wordCount", 33}})
  {
    CAPTURE(field.path);
    SaveStateMutation invalid(saved);
    invalid.writeScalar(field.path, field.value);
    invalid.refreshChecksum();
    REQUIRE_THROWS_AS(original.loadState(invalid.bytes()), std::invalid_argument);
    REQUIRE(original.traceEnabled());
    REQUIRE(original.saveState() == saved);
    REQUIRE(original.eeStateHash() == eeHash);
    REQUIRE(original.iopStateHash() == iopHash);
    REQUIRE(original.sifStateHash() == sifHash);
    IOPTraceTest::requireEvents(original.trace(), history);
  }
  NekoSystem restored;
  restored.loadState(saved);
  original.startTrace();
  restored.startTrace();
  original.runMasterCycles(64);
  restored.runMasterCycles(64);
  requireTransferCompleted(original);
  IOPTraceTest::requireMachine(restored, original);
  IOPTraceTest::requireEvents(restored.trace(), original.trace());
}
