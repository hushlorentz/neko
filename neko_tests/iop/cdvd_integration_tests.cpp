#include <array>
#include <cstdint>
#include <stdexcept>

#include "catch.hpp"
#include "neko_system.hpp"
#include "save_state_mutation.hpp"

namespace
{
  constexpr std::uint32_t BASE = 0x1f402000;
  constexpr std::uint32_t IRQ = 1u << 2;
  constexpr std::uint64_t RTC_PERIOD_TICKS =
    UINT64_C(3155760000) * PS2Clock::MASTER_HZ;
  const std::array<std::uint8_t, 8> EPOCH = {{0, 0, 0, 0, 0, 1, 1, 0}};

  void write(NekoSystem &system, unsigned offset, std::uint8_t value)
  {
    REQUIRE(system.iopBus().write8(BASE + offset, value).status ==
      IOPBusStatus::Completed);
  }

  std::uint8_t read(NekoSystem &system, unsigned offset)
  {
    const auto value = system.iopBus().read8(BASE + offset);
    REQUIRE(value.status == IOPBusStatus::Completed);
    return static_cast<std::uint8_t>(value.value);
  }

  void requireSame(NekoSystem &original, NekoSystem &restored)
  {
    REQUIRE(original.iopStateHash() == restored.iopStateHash());
    REQUIRE(original.saveState() == restored.saveState());
  }

  template<std::size_t Size>
  void startProgram(NekoSystem &system, const std::array<IOPWord, Size> &words)
  {
    for (std::size_t index = 0; index < words.size(); ++index)
    {
      REQUIRE(system.iopBus().write32(static_cast<IOPAddress>(index * 4),
        words[index]).status == IOPBusStatus::Completed);
    }
    system.iopCore().startExecution(0xa0000000);
  }

  void requireClockResult(NekoSystem &system,
    const std::array<std::uint8_t, 8> &expected)
  {
    REQUIRE(read(system, 0x17) == 0);
    for (const auto byte : expected)
    {
      REQUIRE(read(system, 0x18) == byte);
    }
    REQUIRE(read(system, 0x17) == 0x40);
  }
}

TEST_CASE("CDVD integration admits only exact physical bytes without side effects",
  "[iop][cdvd-integration]")
{
  IOPBus unattached;
  REQUIRE(unattached.read8(BASE + 5).status == IOPBusStatus::Unmapped);
  NekoSystem system;
  for (const auto offset : {4u, 5u, 6u, 8u, 10u, 15u, 22u, 23u, 24u})
  {
    const auto result = system.iopBus().read8(BASE + offset);
    REQUIRE(result.status == IOPBusStatus::Completed);
    REQUIRE(result.value == (offset == 5 || offset == 23 ? 0x40 : 0));
  }
  write(system, 0x16, 8);
  system.clockMasterCycle();
  const auto before = system.iopStateHash();
  for (const auto address : {BASE + 4, BASE + 8, BASE + 0x18})
  {
    CAPTURE(address);
    auto &bus = system.iopBus();
    REQUIRE(bus.read16(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(address, 0).status == IOPBusStatus::Unmapped);
    for (unsigned mask = 1; mask < 16; ++mask)
    {
      REQUIRE(bus.writeMasked32(address, 0, mask).status == IOPBusStatus::Unmapped);
    }
    REQUIRE(bus.readDMAC32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.writeDMAC32(address, 0).status == IOPBusStatus::Unmapped);
    std::uint8_t byte = 0xa5;
    REQUIRE(bus.inspectPhysical(address, &byte, 1) == IOPBusStatus::Unmapped);
    REQUIRE(byte == 0xa5);
    REQUIRE(bus.loadPhysical(address, &byte, 1) == IOPBusStatus::Unmapped);
  }
  for (const auto address : {BASE + 3, BASE + 7, BASE + 9, BASE + 0x19,
         0xbf402018u, 0x1f403018u})
  {
    REQUIRE(system.iopBus().read8(address).status == IOPBusStatus::Unmapped);
    REQUIRE(system.iopBus().write8(address, 0).status == IOPBusStatus::Unmapped);
  }
  REQUIRE(system.iopStateHash() == before);
  for (const auto byte : EPOCH)
  {
    REQUIRE(read(system, 0x18) == byte);
  }
}

TEST_CASE("CDVD guest NOP completes before the next IOP instruction not its store",
  "[iop][cdvd-integration]")
{
  NekoSystem system;
  const std::uint32_t program[] = {
    0x3c01bf40, 0x34212000, 0xa0200004, 0
  };
  for (unsigned index = 0; index < 4; ++index)
  {
    REQUIRE(system.iopBus().write32(index * 4, program[index]).status ==
      IOPBusStatus::Completed);
  }
  system.iopCore().startExecution(0xa0000000);
  system.runMasterCycles(17);
  REQUIRE(system.iopCore().retiredInstructions() == 3);
  REQUIRE(read(system, 5) == 0x80);
  REQUIRE(read(system, 8) == 0);
  REQUIRE(system.iopCore().readData8(0xbf402005).value == 0x80);
  REQUIRE(system.iopInterruptController().status() == 0);
  system.runMasterCycles(7);
  REQUIRE(read(system, 5) == 0x80);
  system.clockMasterCycle();
  REQUIRE(system.iopCore().retiredInstructions() == 4);
  REQUIRE(read(system, 5) == 0x40);
  REQUIRE(read(system, 8) == 1);
  REQUIRE(system.iopInterruptController().status() == IRQ);
}

TEST_CASE("CDVD device cause and INTC acknowledge independently without reassertion",
  "[iop][cdvd-integration]")
{
  NekoSystem system;
  auto &interrupts = system.iopInterruptController();
  interrupts.writeMask(IRQ);
  interrupts.writeControl(1);
  write(system, 4, 0);
  write(system, 0x16, 8);
  system.clockMasterCycle();
  REQUIRE(interrupts.interruptPending());
  REQUIRE(read(system, 8) == 1);
  REQUIRE(read(system, 0x17) == 0);
  interrupts.writeStatus(~IRQ);
  REQUIRE_FALSE(interrupts.interruptPending());
  write(system, 4, 0);
  system.runMasterCycles(8);
  REQUIRE(read(system, 8) == 1);
  REQUIRE(interrupts.status() == 0);
  write(system, 8, 1);
  write(system, 4, 0);
  system.runMasterCycles(8);
  REQUIRE(interrupts.status() == IRQ);
  write(system, 8, 1);
  REQUIRE(read(system, 8) == 0);
  REQUIRE(interrupts.status() == IRQ);
  REQUIRE(interrupts.interruptPending());
}

TEST_CASE("CDVD pending restore preserves every master phase and next pulse",
  "[iop][cdvd-integration][save-state]")
{
  for (unsigned phase = 0; phase < 8; ++phase)
  {
    CAPTURE(phase);
    NekoSystem original;
    original.runMasterCycles(phase);
    write(original, 4, 0);
    write(original, 0x16, 8);
    NekoSystem restored;
    restored.loadState(original.saveState());
    requireSame(original, restored);
    const unsigned beforePulse = (8 - phase) % 8;
    original.runMasterCycles(beforePulse);
    restored.runMasterCycles(beforePulse);
    REQUIRE(read(original, 5) == 0x80);
    REQUIRE(read(restored, 0x17) == 0xc0);
    original.clockMasterCycle();
    restored.clockMasterCycle();
    REQUIRE(read(original, 8) == 1);
    REQUIRE(read(restored, 0x17) == 0);
    requireSame(original, restored);
  }
}

TEST_CASE("CDVD restore retains partial results and independent device INTC latches",
  "[iop][cdvd-integration][save-state]")
{
  for (const bool clearDevice : {false, true})
  {
    NekoSystem original;
    write(original, 4, 0);
    write(original, 0x16, 8);
    original.clockMasterCycle();
    for (unsigned index = 0; index < 3; ++index)
    {
      REQUIRE(read(original, 0x18) == EPOCH[index]);
    }
    if (clearDevice)
    {
      write(original, 8, 1);
    }
    else
    {
      original.iopInterruptController().writeStatus(~IRQ);
    }
    NekoSystem restored;
    restored.loadState(original.saveState());
    requireSame(original, restored);
    original.runMasterCycles(8);
    restored.runMasterCycles(8);
    REQUIRE(read(restored, 8) == (clearDevice ? 0 : 1));
    REQUIRE(restored.iopInterruptController().status() == (clearDevice ? IRQ : 0));
    for (unsigned index = 3; index < 8; ++index)
    {
      REQUIRE(read(original, 0x18) == EPOCH[index]);
      REQUIRE(read(restored, 0x18) == EPOCH[index]);
    }
    requireSame(original, restored);
    restored.reset();
    REQUIRE(read(restored, 5) == 0x40);
    REQUIRE(read(restored, 8) == 0);
    REQUIRE(read(restored, 0x16) == 0);
    REQUIRE(read(restored, 0x17) == 0x40);
  }
}

TEST_CASE("CDVD malformed persisted state fails without destination mutation",
  "[iop][cdvd-integration][save-state]")
{
  NekoSystem source;
  write(source, 0x16, 8);
  source.clockMasterCycle();
  const auto valid = source.saveState();
  struct InvalidField
  {
    const char *path;
    std::uint64_t value;
  };
  const InvalidField invalid[] = {
    {"rtcTicks", RTC_PERIOD_TICKS}, {"rtcTicks", UINT64_MAX},
    {"normalPending", 2}, {"deviceCause", 2}, {"lastSubcommand", 0},
    {"lastSubcommand", 1},
    {"subcommandPhase", 3}, {"resultCursor", 8}, {"subcommandPhase", 0}
  };
  NekoSystem destination;
  write(destination, 4, 0);
  write(destination, 0x16, 8);
  destination.clockMasterCycle();
  REQUIRE(read(destination, 0x18) == 0);
  REQUIRE(destination.iopBus().write32(0x100, 0x12345678).status ==
    IOPBusStatus::Completed);
  const auto before = destination.saveState();
  for (const auto &field : invalid)
  {
    CAPTURE(field.path);
    CAPTURE(field.value);
    SaveStateMutation bad(valid);
    bad.writeScalar(std::string("system.iop.cdvd.") + field.path, field.value);
    bad.refreshChecksum();
    REQUIRE_THROWS_AS(destination.loadState(bad), std::invalid_argument);
    REQUIRE(destination.saveState() == before);
  }

  const std::array<std::array<std::uint8_t, 2>, 12> invalidBytes = {{
    {{0, 1}}, {{1, 0x60}}, {{1, 0x1a}}, {{2, 0x60}}, {{3, 0x24}},
    {{4, 1}}, {{5, 0}}, {{5, 0x32}}, {{6, 0}}, {{6, 0x13}},
    {{7, 0xfa}}, {{7, 0xa0}}
  }};
  for (const auto &byte : invalidBytes)
  {
    CAPTURE(byte[0]);
    CAPTURE(byte[1]);
    SaveStateMutation bad(valid);
    bad.writeByte("system.iop.cdvd.result", byte[0], byte[1]);
    bad.refreshChecksum();
    REQUIRE_THROWS_AS(destination.loadState(bad), std::invalid_argument);
    REQUIRE(destination.saveState() == before);
  }
  for (const auto &date : std::array<std::array<std::uint8_t, 3>, 3>{{
         {{0, 2, 0x30}}, {{1, 2, 0x29}}, {{0, 4, 0x31}}
       }})
  {
    CAPTURE(date[0]);
    CAPTURE(date[1]);
    CAPTURE(date[2]);
    SaveStateMutation bad(valid);
    bad.writeByte("system.iop.cdvd.result", 5, date[2]);
    bad.writeByte("system.iop.cdvd.result", 6, date[1]);
    bad.writeByte("system.iop.cdvd.result", 7, date[0]);
    bad.refreshChecksum();
    REQUIRE_THROWS_AS(destination.loadState(bad), std::invalid_argument);
    REQUIRE(destination.saveState() == before);
  }
}

TEST_CASE("CDVD RTC clocks before completion while halted and keeps system ownership",
  "[iop][cdvd-integration][save-state]")
{
  NekoSystem system;
  const auto initialRTC = system.cdvdController().stateHash();
  system.runMasterCycles(8);
  REQUIRE(system.cdvdController().stateHash() != initialRTC);
  const auto before = system.iopStateHash();
  SaveStateMutation seeded(system.saveState());
  seeded.writeScalar("system.iop.cdvd.rtcTicks", PS2Clock::MASTER_HZ - 1);
  seeded.refreshChecksum();
  system.loadState(seeded);
  REQUIRE(system.iopStateHash() != before);
  write(system, 0x16, 8);
  system.iopBus().reset();
  REQUIRE(read(system, 0x17) == 0xc0);
  system.clockMasterCycle();
  REQUIRE(read(system, 0x18) == 0);
  REQUIRE(read(system, 0x18) == 1);

  CDVDController foreign;
  system.iopBus().attachCDVD(&foreign);
  REQUIRE_THROWS_AS(system.saveState(), std::logic_error);
  REQUIRE_THROWS_AS(system.iopStateHash(), std::logic_error);
  system.reset();
  REQUIRE(system.cdvdController().stateHash() == initialRTC);
  REQUIRE(read(system, 0x17) == 0x40);
}

TEST_CASE("CDVD guest ReadClock delay-slot acceptance polls and drains through every alias",
  "[iop][cdvd-integration][cdvd-conformance][save-state]")
{
  for (const IOPWord upper : {0x1f40u, 0x9f40u, 0xbf40u})
  {
    CAPTURE(upper);
    NekoSystem original;
    std::array<IOPWord, 32> words = {{
      0x3c010000 | upper, // lui r1, selected segment
      0x34212000,        // ori r1, r1, 0x2000
      0x34020008,        // ori r2, r0, ReadClock
      0x10000002,        // beq r0, r0, poll
      0xa0220016,        // sb r2, 0x16(r1), in the delay slot
      0xffffffff,        // must be skipped
      0x90230017,        // poll: lbu r3, 0x17(r1)
      0,                // load delay
      0x30640080,        // andi r4, r3, busy
      0x1480fffc,        // bne r4, r0, poll
      0                 // branch delay
    }};
    for (IOPWord index = 0; index < 8; ++index)
    {
      words[11 + index * 2] = 0x90200018 | ((8 + index) << 16);
    }
    words[27] = 0x90300017; // lbu r16, SREADY
    words[29] = 0x90310018; // lbu r17, empty result
    words[31] = 0x34120001; // ori r18, r0, done
    startProgram(original, words);
    original.runMasterCycles(33);
    REQUIRE(original.iopCore().retiredInstructions() == 5);
    REQUIRE(original.iopCore().programCounter() == 0xa0000018);
    REQUIRE(read(original, 0x17) == 0xc0);
    NekoSystem restored;
    restored.loadState(original.saveState());
    original.runMasterCycles(7);
    restored.runMasterCycles(7);
    REQUIRE(read(original, 0x17) == 0xc0);
    original.clockMasterCycle();
    restored.clockMasterCycle();
    REQUIRE(read(original, 0x17) == 0);
    REQUIRE(original.iopCore().delayedResultContinuation().source ==
      IOPDelayedResultSource::Load);
    original.runMasterCycles(200);
    restored.runMasterCycles(200);
    REQUIRE(original.iopCore().exception().kind == IOPException::None);
    REQUIRE(original.iopCore().retiredInstructions() == 31);
    for (unsigned index = 0; index < EPOCH.size(); ++index)
    {
      CAPTURE(index);
      REQUIRE(original.iopCore().generalRegister(8 + index) == EPOCH[index]);
    }
    REQUIRE(original.iopCore().generalRegister(3) == 0);
    REQUIRE(original.iopCore().generalRegister(16) == 0x40);
    REQUIRE(original.iopCore().generalRegister(17) == 0);
    REQUIRE(original.iopCore().generalRegister(18) == 1);
    REQUIRE(original.iopInterruptController().status() == 0);
    requireSame(original, restored);
  }
}

TEST_CASE("CDVD IRQ admits before the next instruction and survives masked restore",
  "[iop][cdvd-integration][cdvd-conformance][save-state]")
{
  for (const IOPWord mask : {0u, IRQ})
  {
    for (const IOPWord control : {0u, 1u})
    {
      CAPTURE(mask);
      CAPTURE(control);
      NekoSystem system;
      const std::array<IOPWord, 6> words = {{
        0x34070401, // ori r7, r0, IEc | IM2
        0x40876000, // mtc0 r7, Status
        0x3c01bf40,
        0x34212000,
        0xa0200004, // sb r0, NCMD
        0x34020001  // must not retire before an enabled IRQ
      }};
      system.iopInterruptController().writeMask(mask);
      system.iopInterruptController().writeControl(control);
      startProgram(system, words);
      system.runMasterCycles(33);
      REQUIRE(read(system, 5) == 0x80);
      REQUIRE(system.iopInterruptController().status() == 0);
      system.runMasterCycles(7);
      REQUIRE(read(system, 5) == 0x80);
      system.clockMasterCycle();
      REQUIRE(read(system, 8) == 1);
      REQUIRE(system.iopInterruptController().status() == IRQ);
      if (mask == IRQ && control == 1)
      {
        REQUIRE(system.iopCore().exception().kind == IOPException::Interrupt);
        REQUIRE(system.iopCore().exceptionProgramCounter() == 0xa0000014);
        REQUIRE(system.iopCore().retiredInstructions() == 5);
        REQUIRE(system.iopCore().generalRegister(2) == 0);
      }
      else
      {
        REQUIRE(system.iopCore().exception().kind == IOPException::None);
        REQUIRE(system.iopCore().generalRegister(2) == 1);
        write(system, 8, 1);
        system.loadState(system.saveState());
        REQUIRE(read(system, 8) == 0);
        REQUIRE(system.iopInterruptController().status() == IRQ);
        system.iopInterruptController().writeMask(IRQ);
        system.iopInterruptController().writeControl(1);
        system.runMasterCycles(7);
        REQUIRE(system.iopCore().exception().kind == IOPException::None);
        system.clockMasterCycle();
        REQUIRE(system.iopCore().exception().kind == IOPException::Interrupt);
        REQUIRE(system.iopCore().exceptionProgramCounter() == 0xa0000018);
        REQUIRE(system.iopCore().retiredInstructions() == 6);
      }
    }
  }
}

TEST_CASE("CDVD completion follows timers and precedes SIF DMA and CPU admission",
  "[iop][cdvd-integration][cdvd-conformance][trace]")
{
  NekoSystem system;
  const std::array<IOPWord, 2> words = {{0x34010401, 0x40816000}};
  startProgram(system, words);
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
  system.iopInterruptController().writeMask(0x1c07c);
  system.iopInterruptController().writeControl(1);
  write(system, 4, 0);
  write(system, 0x16, 8);
  system.startTrace();
  system.clockMasterCycle();
  const std::array<IOPWord, 8> expected = {{
    0x10, 0x30, 0x70, 0x4070, 0xc070, 0x1c070, 0x1c074, 0x1c07c
  }};
  std::size_t transitions = 0;
  bool exceptionSeen = false;
  for (const auto &event : system.trace())
  {
    if (event.subsystem == NekoTraceSubsystem::IOPInterruptController &&
        event.type == NekoTraceEventType::InterruptChanged)
    {
      REQUIRE_FALSE(exceptionSeen);
      REQUIRE(transitions < expected.size());
      REQUIRE(event.masterCycle == 17);
      REQUIRE(event.value0 == expected[transitions++]);
    }
    if (event.subsystem == NekoTraceSubsystem::IOP &&
        event.type == NekoTraceEventType::ExceptionEntered)
    {
      REQUIRE(transitions == expected.size());
      exceptionSeen = true;
    }
  }
  REQUIRE(transitions == expected.size());
  REQUIRE(exceptionSeen);
  REQUIRE_FALSE(system.iopSIF1DMAC().active());
  REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 0);
  REQUIRE(system.iopCore().exception().kind == IOPException::Interrupt);
  REQUIRE(system.iopCore().exceptionProgramCounter() == 0xa0000008);
  REQUIRE(system.iopCore().retiredInstructions() == 2);
  requireClockResult(system, EPOCH);
}

TEST_CASE("CDVD guest rejected accesses and instruction fetches never consume results",
  "[iop][cdvd-integration][cdvd-conformance]")
{
  struct Access
  {
    IOPWord instruction;
    IOPException exception;
  };
  const Access accesses[] = {
    {0x8c220018, IOPException::DataBusError}, // lw
    {0x94220018, IOPException::DataBusError}, // lhu
    {0x94220017, IOPException::AddressErrorLoadOrFetch},
    {0xa4200017, IOPException::AddressErrorStore},
    {0xa0200017, IOPException::None}, // parameter byte write
    {0xa0200016, IOPException::None}, // unsupported command
    {0xac200004, IOPException::None}, // sw
    {0xa8200018, IOPException::None}, // swl
    {0xb8200018, IOPException::None}  // swr
  };
  for (const auto &access : accesses)
  {
    CAPTURE(access.instruction);
    NekoSystem system;
    write(system, 0x16, 8);
    system.clockMasterCycle();
    const std::array<IOPWord, 3> words = {{
      0x3c01bf40, 0x34212000, access.instruction
    }};
    startProgram(system, words);
    system.runMasterCycles(24);
    REQUIRE(system.iopCore().exception().kind == access.exception);
    if (access.exception == IOPException::None)
    {
      REQUIRE(system.iopCore().stopReason() == IOPStopReason::StoreFailure);
      REQUIRE(system.iopCore().programCounter() == 0xa0000008);
      REQUIRE(system.iopCore().storeFailure().access.outcome ==
        IOPAccessOutcome::Unmapped);
      REQUIRE(system.iopCore().storeFailure().access.physicalAddress ==
        (BASE + (access.instruction & 0x1f)));
    }
    else
    {
      REQUIRE(system.iopCore().exceptionProgramCounter() == 0xa0000008);
    }
    REQUIRE(system.iopCore().retiredInstructions() == 2);
    REQUIRE(read(system, 5) == 0x40);
    REQUIRE(read(system, 8) == 0);
    requireClockResult(system, EPOCH);
  }
  for (const IOPAddress address : {0x9f402018u, 0xbf402018u})
  {
    CAPTURE(address);
    NekoSystem system;
    write(system, 0x16, 8);
    system.clockMasterCycle();
    system.iopCore().startExecution(address);
    system.runMasterCycles(8);
    REQUIRE(system.iopCore().exception().kind == IOPException::InstructionBusError);
    REQUIRE(system.iopCore().exceptionProgramCounter() == address);
    REQUIRE(system.iopCore().retiredInstructions() == 0);
    requireClockResult(system, EPOCH);
  }
}

TEST_CASE("CDVD restores every snapshot cursor after RTC century wrap independently of NOP",
  "[iop][cdvd-integration][cdvd-conformance][save-state]")
{
  NekoSystem source;
  SaveStateMutation seeded(source.saveState());
  seeded.writeScalar("system.iop.cdvd.rtcTicks", RTC_PERIOD_TICKS - 2);
  seeded.refreshChecksum();
  source.loadState(seeded);
  write(source, 0x16, 8);
  source.runMasterCycles(2);
  const auto snapshot = source.saveState();
  const std::array<std::uint8_t, 8> lastSecond = {{
    0, 0x59, 0x59, 0x23, 0, 0x31, 0x12, 0x99
  }};
  for (unsigned cursor = 0; cursor < 8; ++cursor)
  {
    CAPTURE(cursor);
    NekoSystem original;
    original.loadState(snapshot);
    for (unsigned index = 0; index < cursor; ++index)
    {
      REQUIRE(read(original, 0x18) == lastSecond[index]);
    }
    write(original, 4, 0);
    NekoSystem restored;
    restored.loadState(original.saveState());
    original.runMasterCycles(7);
    restored.runMasterCycles(7);
    REQUIRE(read(restored, 8) == 1);
    REQUIRE(restored.iopInterruptController().status() == IRQ);
    for (unsigned index = cursor; index < 8; ++index)
    {
      REQUIRE(read(original, 0x18) == lastSecond[index]);
      REQUIRE(read(restored, 0x18) == lastSecond[index]);
    }
    REQUIRE(read(restored, 0x17) == 0x40);
    REQUIRE(read(restored, 0x16) == 8);
    write(original, 0x16, 8);
    write(restored, 0x16, 8);
    original.runMasterCycles(8);
    restored.runMasterCycles(8);
    requireClockResult(original, EPOCH);
    requireClockResult(restored, EPOCH);
    requireSame(original, restored);
  }
}

TEST_CASE("CDVD reset discards pending and partial operations and rebinds its own owner",
  "[iop][cdvd-integration][cdvd-conformance][save-state]")
{
  NekoSystem fresh;
  for (unsigned stage = 0; stage < 3; ++stage)
  {
    CAPTURE(stage);
    NekoSystem system;
    write(system, 4, 0);
    write(system, 0x16, 8);
    system.iopInterruptController().writeMask(IRQ);
    system.iopInterruptController().writeControl(1);
    if (stage != 0)
    {
      system.clockMasterCycle();
    }
    if (stage == 2)
    {
      for (unsigned index = 0; index < 3; ++index)
      {
        REQUIRE(read(system, 0x18) == EPOCH[index]);
      }
    }
    REQUIRE(system.iopBus().write32(0x100, 0x12345678).status ==
      IOPBusStatus::Completed);
    system.reset();
    requireSame(system, fresh);
    write(system, 4, 0);
    system.clockMasterCycle();
    REQUIRE(read(system, 8) == 1);
    REQUIRE(read(fresh, 8) == 0);
  }
}

TEST_CASE("CDVD inactive persisted results require zero storage and cursor",
  "[iop][cdvd-integration][cdvd-conformance][save-state]")
{
  for (const bool pending : {false, true})
  {
    CAPTURE(pending);
    NekoSystem system;
    if (pending)
    {
      write(system, 0x16, 8);
    }
    const auto valid = system.saveState();
    for (const auto *field : {"result", "resultCursor"})
    {
      CAPTURE(field);
      SaveStateMutation bad(valid);
      bad.writeByte(std::string("system.iop.cdvd.") + field, 0, 1);
      bad.refreshChecksum();
      REQUIRE_THROWS_AS(system.loadState(bad), std::invalid_argument);
      REQUIRE(system.saveState() == valid);
    }
  }
}
