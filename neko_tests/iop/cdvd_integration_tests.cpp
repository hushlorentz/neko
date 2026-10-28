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
    {"rtcTicks", UINT64_C(3155760000) * PS2Clock::MASTER_HZ},
    {"normalPending", 2}, {"deviceCause", 2}, {"lastSubcommand", 0},
    {"subcommandPhase", 3}, {"resultCursor", 8}, {"subcommandPhase", 0}
  };
  NekoSystem destination;
  destination.runMasterCycles(3);
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

  for (const auto index : {0u, 1u, 4u, 5u, 6u, 7u})
  {
    CAPTURE(index);
    SaveStateMutation bad(valid);
    bad.writeByte("system.iop.cdvd.result", index, 0xfa);
    bad.refreshChecksum();
    REQUIRE_THROWS_AS(destination.loadState(bad), std::invalid_argument);
    REQUIRE(destination.saveState() == before);
  }
  SaveStateMutation february(valid);
  february.writeByte("system.iop.cdvd.result", 5, 0x30);
  february.writeByte("system.iop.cdvd.result", 6, 2);
  february.refreshChecksum();
  REQUIRE_THROWS_AS(destination.loadState(february), std::invalid_argument);
  REQUIRE(destination.saveState() == before);
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
