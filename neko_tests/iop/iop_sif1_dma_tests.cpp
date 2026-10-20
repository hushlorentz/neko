#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "catch.hpp"
#include "iop_trace_test_utils.hpp"
#include "save_state_mutation.hpp"

namespace
{
  constexpr IOPWord START = 0x01000400;
  constexpr IOPWord FLAG = 0x08000000;
  constexpr IOPDMABank BANK = IOPDMABank::Secondary;

  void push(NekoSystem &system, const SIFQuadword &words)
  {
    REQUIRE(system.sif().pushSIF1Quadword(words) == SIFTransferOutcome::Completed);
  }

  void start(NekoSystem &system)
  {
    REQUIRE(system.iopSIF1DMAC().writeChannelControl(START) ==
      IOPDMAWriteOutcome::Completed);
    system.iopDMAController().writePriority(BANK, 0x8000);
  }

  IOPWord ramWord(NekoSystem &system, IOPAddress address)
  {
    const auto result = system.iopBus().read32(address);
    REQUIRE(result.status == IOPBusStatus::Completed);
    return result.value;
  }

  void requireState(const NekoSystem &system, const std::vector<std::uint8_t> &state)
  {
    const bool identical = system.saveState() == state;
    REQUIRE(identical);
  }
}

TEST_CASE("IOP SIF1 receiver accepts headers and single words only on phase zero",
  "[iop][sif1][dma][receiver]")
{
  NekoSystem system;
  REQUIRE(system.iopSIF1DMAC().writeMemoryAddress(0x2000) ==
    IOPDMAWriteOutcome::Completed);
  REQUIRE(system.iopSIF1DMAC().writeBlockControl(0xabcd1234) ==
    IOPDMAWriteOutcome::Completed);
  push(system, {{0x80001000, 4, 0xdeadbeef, 0xabcdef01}});
  push(system, {{1, 2, 3, 4}});
  start(system);
  system.runMasterCycles(1);
  REQUIRE(system.iopSIF1DMAC().memoryAddress() == 0x1000);
  REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 4);
  REQUIRE(ramWord(system, 0x1000) == 0);
  system.runMasterCycles(7);
  REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 4);
  system.runMasterCycles(1);
  REQUIRE(ramWord(system, 0x1000) == 1);
  REQUIRE(system.iopSIF1DMAC().memoryAddress() == 0x1004);
  REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 3);
  REQUIRE(system.iopSIF1DMAC().active());
  system.runMasterCycles(24);
  for (IOPAddress index = 0; index < 4; ++index)
  {
    REQUIRE(ramWord(system, 0x1000 + index * 4) == index + 1);
  }
  REQUIRE(system.iopSIF1DMAC().memoryAddress() == 0x1010);
  REQUIRE(system.iopSIF1DMAC().blockControl() == 0xabcd1234);
  REQUIRE_FALSE(system.iopSIF1DMAC().active());
  REQUIRE(system.iopDMAController().interruptStatus(BANK) == FLAG);
  REQUIRE(system.iopInterruptController().status() == 8);
  REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 0);
  REQUIRE(system.sif().control() == 0);
}

TEST_CASE("IOP SIF1 incomplete headers and empty payloads preserve state atomically",
  "[iop][sif1][dma][receiver][save-state]")
{
  for (unsigned prefix = 0; prefix < 4; ++prefix)
  {
    CAPTURE(prefix);
    NekoSystem system;
    if (prefix != 0)
    {
      push(system, {{1, 2, 3, 4}});
      REQUIRE(system.sif().consumeWords(SIFDirection::SIF1, 4 - prefix) ==
        SIFTransferOutcome::Completed);
    }
    start(system);
    const auto sifHash = system.sifStateHash();
    system.runMasterCycles(17);
    REQUIRE(system.sifStateHash() == sifHash);
    REQUIRE(system.iopSIF1DMAC().memoryAddress() == 0);
    REQUIRE(system.iopSIF1DMAC().active());
    REQUIRE(system.iopDMAController().interruptStatus(BANK) == 0);
    NekoSystem restored;
    restored.loadState(system.saveState());
    IOPTraceTest::requireMachine(restored, system);
  }

  NekoSystem system;
  push(system, {{0x80001000, 4, 0, 0}});
  start(system);
  system.runMasterCycles(1);
  REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 0);
  REQUIRE(system.iopSIF1DMAC().memoryAddress() == 0x1000);
  const auto sifHash = system.sifStateHash();
  system.runMasterCycles(16);
  REQUIRE(system.sifStateHash() == sifHash);
  REQUIRE(system.iopSIF1DMAC().memoryAddress() == 0x1000);
  REQUIRE(system.iopDMAController().interruptStatus(BANK) == 0);
}

TEST_CASE("IOP SIF1 count masking discards all header lanes and closes on either terminal bit",
  "[iop][sif1][dma][receiver]")
{
  for (const IOPWord terminal : {0x40000000u, 0x80000000u, 0xc0000000u})
  {
    for (IOPWord rawCount = 0; rawCount <= 9; ++rawCount)
    {
      CAPTURE(terminal);
      CAPTURE(rawCount);
      NekoSystem system;
      const auto count = rawCount & 0x000ffffc;
      push(system, {{terminal | 0x1000, rawCount | 0xfff00000,
        0xdeadbeef, 0xabcdef01}});
      push(system, {{1, 2, 3, 4}});
      push(system, {{5, 6, 7, 8}});
      start(system);
      system.runMasterCycles(count * 8 + 1);
      REQUIRE_FALSE(system.iopSIF1DMAC().active());
      REQUIRE(system.iopSIF1DMAC().memoryAddress() == 0x1000 + count * 4);
      REQUIRE(system.iopDMAController().interruptStatus(BANK) == FLAG);
      REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 8 - count);
      for (IOPWord index = 0; index < count; ++index)
      {
        REQUIRE(ramWord(system, 0x1000 + index * 4) == index + 1);
      }
      REQUIRE(ramWord(system, 0x1000 + count * 4) == 0);
      system.iopInterruptController().writeStatus(~IOPWord{8});
      system.runMasterCycles(16);
      REQUIRE(system.iopInterruptController().status() == 0);
    }
  }
}

TEST_CASE("IOP SIF1 nonterminal zero headers consume one grant before the next packet",
  "[iop][sif1][dma][receiver]")
{
  NekoSystem system;
  push(system, {{0x00fffffc, 3, 0xdead, 0xbeef}});
  push(system, {{0x1000, 4, 0xdead, 0xbeef}});
  push(system, {{1, 2, 3, 4}});
  push(system, {{0x80002000, 0, 0xdead, 0xbeef}});
  start(system);
  system.runMasterCycles(1);
  REQUIRE(system.iopSIF1DMAC().memoryAddress() == 0x00fffffc);
  REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 12);
  REQUIRE(system.iopSIF1DMAC().active());
  system.runMasterCycles(40);
  REQUIRE(system.iopSIF1DMAC().memoryAddress() == 0x1010);
  REQUIRE(system.iopDMAController().interruptStatus(BANK) == 0);
  REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 4);
  system.runMasterCycles(8);
  REQUIRE(system.iopSIF1DMAC().memoryAddress() == 0x2000);
  REQUIRE_FALSE(system.iopSIF1DMAC().active());
  REQUIRE(system.iopDMAController().interruptStatus(BANK) == FLAG);
}

TEST_CASE("IOP SIF1 receiver enable pauses packet continuation without restarting it",
  "[iop][sif1][dma][receiver][save-state]")
{
  NekoSystem original;
  push(original, {{0x80001000, 4, 0, 0}});
  push(original, {{1, 2, 3, 4}});
  REQUIRE(original.iopSIF1DMAC().writeChannelControl(START) ==
    IOPDMAWriteOutcome::Completed);
  original.runMasterCycles(8);
  REQUIRE(original.sif().wordCount(SIFDirection::SIF1) == 8);
  original.iopDMAController().writePriority(BANK, 0x8000);
  original.runMasterCycles(9);
  REQUIRE(original.iopSIF1DMAC().memoryAddress() == 0x1004);
  original.iopDMAController().writePriority(BANK, 0x7000);
  original.runMasterCycles(16);
  REQUIRE(original.iopSIF1DMAC().memoryAddress() == 0x1004);
  REQUIRE(original.sif().wordCount(SIFDirection::SIF1) == 3);
  NekoSystem restored;
  restored.loadState(original.saveState());
  IOPTraceTest::requireMachine(restored, original);
  for (auto *system : {&original, &restored})
  {
    system->iopDMAController().writePriority(BANK, 0x8000);
    system->runMasterCycles(24);
    REQUIRE_FALSE(system->iopSIF1DMAC().active());
    REQUIRE(ramWord(*system, 0x100c) == 4);
  }
  IOPTraceTest::requireMachine(restored, original);
}

TEST_CASE("IOP SIF1 packet remainder is not truncated to BCR or sixteen bits",
  "[iop][sif1][dma][receiver]")
{
  NekoSystem system;
  push(system, {{0x80001000, 0x10004, 0, 0}});
  push(system, {{1, 2, 3, 4}});
  push(system, {{5, 6, 7, 8}});
  REQUIRE(system.iopSIF1DMAC().writeBlockControl(1) == IOPDMAWriteOutcome::Completed);
  start(system);
  system.runMasterCycles(65);
  REQUIRE(system.iopSIF1DMAC().active());
  REQUIRE(system.iopSIF1DMAC().memoryAddress() == 0x1020);
  REQUIRE(system.iopSIF1DMAC().blockControl() == 1);
  REQUIRE(system.iopDMAController().interruptStatus(BANK) == 0);
  REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 0);
}

TEST_CASE("IOP SIF1 receiver uses physical RAM mirrors without snooping CPU caches",
  "[iop][sif1][dma][receiver][cache]")
{
  NekoSystem system;
  REQUIRE(system.iopBus().write32(0x1000, 0xdeadbeef).status ==
    IOPBusStatus::Completed);
  REQUIRE(system.iopCore().readData32(0x80001000).value == 0xdeadbeef);
  push(system, {{0x80601000, 4, 0, 0}});
  push(system, {{1, 2, 3, 4}});
  start(system);
  system.runMasterCycles(33);
  REQUIRE(ramWord(system, 0x1000) == 1);
  REQUIRE(ramWord(system, 0x201000) == 1);
  REQUIRE(ramWord(system, 0x601000) == 1);
  REQUIRE(system.iopCore().readData32(0x80001000).value == 0xdeadbeef);
  REQUIRE(system.iopCore().readData32(0xa0001000).value == 1);
  REQUIRE(system.iopSIF1DMAC().memoryAddress() == 0x601010);
}

TEST_CASE("IOP SIF1 abort discards only private continuation and the next start awaits a header",
  "[iop][sif1][dma][receiver][save-state]")
{
  NekoSystem system;
  push(system, {{0x80001000, 4, 0, 0}});
  push(system, {{1, 2, 3, 4}});
  start(system);
  system.runMasterCycles(9);
  REQUIRE(ramWord(system, 0x1000) == 1);
  const auto sifHash = system.sifStateHash();
  REQUIRE(system.iopSIF1DMAC().writeChannelControl(0x400) ==
    IOPDMAWriteOutcome::Completed);
  REQUIRE(system.iopSIF1DMAC().memoryAddress() == 0x1004);
  REQUIRE(system.sifStateHash() == sifHash);
  REQUIRE(system.iopDMAController().interruptStatus(BANK) == 0);
  NekoSystem restored;
  restored.loadState(system.saveState());
  REQUIRE(restored.sif().consumeWords(SIFDirection::SIF1, 3) ==
    SIFTransferOutcome::Completed);
  push(restored, {{0x80002000, 0, 0, 0}});
  start(restored);
  restored.runMasterCycles(8);
  REQUIRE_FALSE(restored.iopSIF1DMAC().active());
  REQUIRE(restored.iopSIF1DMAC().memoryAddress() == 0x2000);
  REQUIRE(ramWord(restored, 0x1004) == 0);
}

TEST_CASE("IOP SIF1 header and partial payload restore identically at every master phase",
  "[iop][sif1][dma][receiver][save-state]")
{
  for (const unsigned prefix : {0u, 1u, 7u, 8u, 9u, 10u, 11u, 12u, 13u,
    14u, 15u, 16u, 17u, 25u, 33u})
  {
    CAPTURE(prefix);
    NekoSystem original;
    push(original, {{0x80001000, 4, 0xdead, 0xbeef}});
    push(original, {{1, 2, 3, 4}});
    start(original);
    original.runMasterCycles(prefix);
    NekoSystem restored;
    restored.loadState(original.saveState());
    original.startTrace();
    restored.startTrace();
    original.runMasterCycles(40);
    restored.runMasterCycles(40);
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    REQUIRE_FALSE(restored.iopSIF1DMAC().active());
    REQUIRE(ramWord(restored, 0x100c) == 4);
    original.reset();
    restored.reset();
    NekoSystem fresh;
    IOPTraceTest::requireMachine(restored, fresh);
    IOPTraceTest::requireMachine(original, fresh);
  }
}

TEST_CASE("SIF1 sender and receiver progress independently in their ordered phases",
  "[iop][ee][sif1][dma][receiver]")
{
  NekoSystem system;
  REQUIRE(system.eeBus().writeDMAC128(0x1000,
    {UINT64_C(0x0000000480002000), UINT64_C(0xdeadbeefabcdef01)}));
  REQUIRE(system.eeBus().writeDMAC128(0x1010,
    {UINT64_C(0x0000000200000001), UINT64_C(0x0000000400000003)}));
  system.sif1DMAC().writeMemoryAddress(0x1000);
  system.sif1DMAC().writeQuadwordCount(2);
  system.sif1DMAC().writeChannelControl(0x100);
  system.dmacController().writeControl(1);
  start(system);
  system.runMasterCycles(1);
  REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 0);
  REQUIRE(system.iopSIF1DMAC().memoryAddress() == 0x2000);
  REQUIRE(system.sif1DMAC().active());
  system.runMasterCycles(1);
  REQUIRE_FALSE(system.sif1DMAC().active());
  REQUIRE(system.iopSIF1DMAC().active());
  REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 4);
  system.runMasterCycles(31);
  REQUIRE_FALSE(system.iopSIF1DMAC().active());
  REQUIRE(ramWord(system, 0x200c) == 4);
}

TEST_CASE("IOP SIF1 rejected headers and destination words preserve the full beat",
  "[iop][sif1][dma][receiver]")
{
  for (const bool header : {true, false})
  {
    CAPTURE(header);
    NekoSystem system;
    push(system, {{header ? 0x80001001u : 0x80800000u, 4, 0, 0}});
    push(system, {{0x11223344, 2, 3, 4}});
    start(system);
    auto &dma = system.iopDMAController();
    auto &channel = system.iopSIF1DMAC();
    if (!header)
    {
      REQUIRE(dma.clockSIF(channel, system.iopBus(), system.sif()).outcome ==
        IOPDMABeatOutcome::Accepted);
    }
    const auto before = system.saveState();
    for (unsigned repeat = 0; repeat < 2; ++repeat)
    {
      const auto ready = dma.inspectSIF(channel, system.iopBus(), system.sif());
      const auto result = dma.clockSIF(channel, system.iopBus(), system.sif());
      REQUIRE(ready.outcome == IOPDMABeatOutcome::Rejected);
      REQUIRE(result.outcome == IOPDMABeatOutcome::Rejected);
      REQUIRE(result.channel == IOPDMAChannel::SIF1);
      REQUIRE(result.failure == (header ?
        IOPDMAFailure::UnalignedHeader : IOPDMAFailure::DestinationWrite));
      REQUIRE(result.address == (header ? 0x1001u : 0x800000u));
      REQUIRE(result.value == (header ? 0x80001001u : 0x11223344u));
      REQUIRE(result.status == (header ?
        IOPBusStatus::Misaligned : IOPBusStatus::Unmapped));
      requireState(system, before);
    }
    try
    {
      system.clockMasterCycle();
      FAIL("Scheduled DMA rejection must surface to the caller.");
    }
    catch (const IOPDMATransferError &failure)
    {
      REQUIRE(failure.result().failure == (header ?
        IOPDMAFailure::UnalignedHeader : IOPDMAFailure::DestinationWrite));
      REQUIRE(failure.result().address == (header ? 0x1001u : 0x800000u));
      REQUIRE(std::string(failure.what()).find("IOP SIF1 DMA rejected") !=
        std::string::npos);
    }
    REQUIRE(channel.active());
    REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == (header ? 8 : 4));
    REQUIRE(dma.interruptStatus(BANK) == 0);
    REQUIRE(system.iopInterruptController().status() == 0);
  }
}

TEST_CASE("IOP SIF1 last physical RAM word commits before the next address rejects",
  "[iop][sif1][dma][receiver]")
{
  NekoSystem system;
  push(system, {{0x807ffffc, 4, 0, 0}});
  push(system, {{0x11223344, 2, 3, 4}});
  start(system);
  auto &dma = system.iopDMAController();
  auto &channel = system.iopSIF1DMAC();
  REQUIRE(dma.clockSIF(channel, system.iopBus(), system.sif()).outcome ==
    IOPDMABeatOutcome::Accepted);
  REQUIRE(dma.clockSIF(channel, system.iopBus(), system.sif()).outcome ==
    IOPDMABeatOutcome::Accepted);
  REQUIRE(ramWord(system, 0x1ffffc) == 0x11223344);
  REQUIRE(channel.memoryAddress() == 0x800000);
  REQUIRE(channel.remainingWords() == 3);
  REQUIRE(channel.terminalPacket());
  const auto before = system.saveState();
  REQUIRE(dma.clockSIF(channel, system.iopBus(), system.sif()).outcome ==
    IOPDMABeatOutcome::Rejected);
  requireState(system, before);
}

TEST_CASE("IOP DMA physical writes reject non-RAM routes without device side effects",
  "[iop][sif1][dma][receiver][bus]")
{
  NekoSystem system;
  system.installBootROM(std::vector<std::uint8_t>(BootROMImage::SIZE, 0));
  system.iopInterruptController().writeMask(8);
  system.iopInterruptController().writeControl(1);
  for (const auto address : {0x800000u, 0x80000000u, 0xa0000000u,
    0x1f800000u, 0x1f801074u, 0x1f801078u, 0x1d000040u, 0x1fc00000u,
    0xfffffffcu, 1u, 0x1fffffu})
  {
    CAPTURE(address);
    const auto expected = (address & 3) != 0 ? IOPBusStatus::Misaligned :
      address == IOPBus::ROM_BASE ? IOPBusStatus::ReadOnly :
      IOPBusStatus::Unmapped;
    const auto before = system.saveState();
    REQUIRE(system.iopBus().dmaWriteStatus(address) == expected);
    REQUIRE(system.iopBus().writeDMAC32(address, 0xdeadbeef).status == expected);
    requireState(system, before);
  }
  for (const IOPAddress address : {0u, 0x1ffffcu, 0x200000u, 0x3ffffcu,
    0x400000u, 0x5ffffcu, 0x600000u, 0x7ffffcu})
  {
    CAPTURE(address);
    REQUIRE(system.iopBus().writeDMAC32(address, 0x12345678).status ==
      IOPBusStatus::Completed);
    REQUIRE(ramWord(system, address % IOPBus::RAM_SIZE) == 0x12345678);
    REQUIRE(system.iopBus().read8(address).value == 0x78);
    REQUIRE(system.iopBus().read8(address + 3).value == 0x12);
  }
}

TEST_CASE("IOP SIF1 malformed packet continuations reject transactionally",
  "[iop][sif1][dma][receiver][save-state]")
{
  NekoSystem source;
  NekoSystem destination;
  destination.runMasterCycles(9);
  destination.startTrace();
  const auto before = destination.saveState();
  const auto traceHash = destination.traceHash();
  struct Field
  {
    const char *path;
    std::uint64_t value;
  };
  for (const auto field : {
    Field{"system.iop.sif1DMA.remainingWords", 1},
    Field{"system.iop.sif1DMA.remainingWords", 0x100000},
    Field{"system.iop.sif1DMA.terminal", 1},
    Field{"system.iop.sif1DMA.terminal", 2}})
  {
    CAPTURE(field.path);
    SaveStateMutation corrupt(source.saveState());
    corrupt.writeScalar(field.path, field.value);
    corrupt.refreshChecksum();
    REQUIRE_THROWS_AS(destination.loadState(corrupt.bytes()), std::invalid_argument);
    requireState(destination, before);
    REQUIRE(destination.traceHash() == traceHash);
  }
  start(source);
  for (const auto remaining : {0xffffdu, 0xfffffu, 0x100000u})
  {
    SaveStateMutation corrupt(source.saveState());
    corrupt.writeScalar("system.iop.sif1DMA.remainingWords", remaining);
    corrupt.refreshChecksum();
    REQUIRE_THROWS_AS(destination.loadState(corrupt.bytes()), std::invalid_argument);
    requireState(destination, before);
  }
  SaveStateMutation terminalWithoutPacket(source.saveState());
  terminalWithoutPacket.writeScalar("system.iop.sif1DMA.terminal", 1);
  terminalWithoutPacket.refreshChecksum();
  REQUIRE_THROWS_AS(destination.loadState(terminalWithoutPacket.bytes()),
    std::invalid_argument);
  requireState(destination, before);
}

TEST_CASE("IOP SIF1 destination write precedes the same pulse CPU store",
  "[iop][sif1][dma][receiver][system]")
{
  NekoSystem system;
  REQUIRE(system.iopBus().write32(0, 0xac001000).status ==
    IOPBusStatus::Completed); // sw r0, 0x1000(r0)
  push(system, {{0x80001000, 4, 0, 0}});
  push(system, {{1, 2, 3, 4}});
  start(system);
  system.runMasterCycles(8);
  system.iopCore().startExecution(0xa0000000);
  system.clockMasterCycle();
  REQUIRE(system.iopCore().retiredInstructions() == 1);
  REQUIRE(system.iopSIF1DMAC().memoryAddress() == 0x1004);
  REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 3);
  REQUIRE(ramWord(system, 0x1000) == 0);
}

TEST_CASE("IOP SIF1 terminal completion reaches interrupt admission in the same pulse",
  "[iop][sif1][dma][receiver][system]")
{
  NekoSystem system;
  REQUIRE(system.iopBus().write32(0, 0x34010401).status ==
    IOPBusStatus::Completed); // ori r1, r0, IEc | IM2
  REQUIRE(system.iopBus().write32(4, 0x40816000).status ==
    IOPBusStatus::Completed); // mtc0 r1, Status
  system.iopCore().startExecution(0xa0000000);
  system.runMasterCycles(16);
  system.iopInterruptController().writeMask(8);
  system.iopInterruptController().writeControl(1);
  push(system, {{0x80001000, 0, 0, 0}});
  start(system);
  system.clockMasterCycle();
  REQUIRE_FALSE(system.iopSIF1DMAC().active());
  REQUIRE(system.iopCore().exception().kind == IOPException::Interrupt);
  REQUIRE(system.iopCore().exceptionProgramCounter() == 0xa0000008);
  REQUIRE(system.iopCore().retiredInstructions() == 2);
}

TEST_CASE("IOP SIF1 incomplete header prefixes resume without duplicating their words",
  "[iop][sif1][dma][receiver][save-state]")
{
  const SIFQuadword header = {{0x80001000, 4, 0xdead, 0xbeef}};
  for (std::size_t prefix = 1; prefix <= 3; ++prefix)
  {
    CAPTURE(prefix);
    NekoSystem original;
    SIFQuadword initial = {};
    SIFQuadword continued = {};
    for (std::size_t index = 0; index < prefix; ++index)
    {
      initial[4 - prefix + index] = header[index];
      continued[4 - prefix + index] = static_cast<IOPWord>(index + 1);
    }
    for (std::size_t index = prefix; index < 4; ++index)
    {
      continued[index - prefix] = header[index];
    }
    push(original, initial);
    REQUIRE(original.sif().consumeWords(SIFDirection::SIF1, 4 - prefix) ==
      SIFTransferOutcome::Completed);
    start(original);
    original.runMasterCycles(1);
    NekoSystem restored;
    restored.loadState(original.saveState());
    for (auto *system : {&original, &restored})
    {
      push(*system, continued);
      system->runMasterCycles(33);
      REQUIRE(system->sif().wordCount(SIFDirection::SIF1) == 0);
      REQUIRE(system->iopSIF1DMAC().remainingWords() == 4 - prefix);
      push(*system, {{static_cast<IOPWord>(prefix + 1),
        static_cast<IOPWord>(prefix + 2), static_cast<IOPWord>(prefix + 3),
        static_cast<IOPWord>(prefix + 4)}});
      system->runMasterCycles(32);
      REQUIRE_FALSE(system->iopSIF1DMAC().active());
      REQUIRE(system->sif().wordCount(SIFDirection::SIF1) == prefix);
      for (IOPWord index = 0; index < 4; ++index)
      {
        REQUIRE(ramWord(*system, 0x1000 + index * 4) == index + 1);
      }
    }
    IOPTraceTest::requireMachine(restored, original);
  }
}

TEST_CASE("IOP SIF1 drain cannot retry an earlier blocked EE grant in the same cycle",
  "[iop][ee][sif1][dma][receiver]")
{
  NekoSystem system;
  push(system, {{0x80001000, 4, 0, 0}});
  for (unsigned index = 0; index < 7; ++index)
  {
    push(system, {{1, 2, 3, 4}});
  }
  REQUIRE(system.eeBus().writeDMAC128(0x2000, {}));
  system.sif1DMAC().writeMemoryAddress(0x2000);
  system.sif1DMAC().writeQuadwordCount(1);
  system.sif1DMAC().writeChannelControl(0x100);
  system.dmacController().writeControl(1);
  start(system);
  system.clockMasterCycle();
  REQUIRE(system.sif1DMAC().quadwordCount() == 1);
  REQUIRE(system.sif1DMAC().active());
  REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 28);
  system.clockMasterCycle();
  REQUIRE_FALSE(system.sif1DMAC().active());
  REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 32);
}

TEST_CASE("IOP SIF grants reject mismatched canonical owners",
  "[iop][sif1][dma][receiver]")
{
  NekoSystem system;
  NekoSystem other;
  auto &dma = system.iopDMAController();
  const auto before = system.saveState();
  REQUIRE_THROWS_AS(dma.clockSIF(other.iopSIF1DMAC(), system.iopBus(), system.sif()),
    std::invalid_argument);
  REQUIRE_THROWS_AS(dma.clockSIF(system.iopSIF1DMAC(), other.iopBus(), system.sif()),
    std::invalid_argument);
  REQUIRE_THROWS_AS(dma.clockSIF(system.iopSIF1DMAC(), system.iopBus(), other.sif()),
    std::invalid_argument);
  REQUIRE_THROWS_AS(
    dma.inspectSIF(system.iopSIF1DMAC(), system.iopBus(), other.sif()),
    std::invalid_argument);
  requireState(system, before);
}
