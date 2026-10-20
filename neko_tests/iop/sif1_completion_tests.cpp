#include <cstdint>

#include "catch.hpp"
#include "neko_system.hpp"

namespace
{
  constexpr std::uint32_t EE_CHCR = 0x1000c400;
  constexpr std::uint32_t EE_MADR = 0x1000c410;
  constexpr std::uint32_t EE_QWC = 0x1000c420;
  constexpr std::uint32_t EE_CTRL = 0x1000e000;
  constexpr std::uint32_t EE_STAT = 0x1000e010;
  constexpr std::uint32_t EE_FLAG = DMACStatus::CHANNEL_6;
  constexpr std::uint32_t EE_MASK = DMACStatus::CHANNEL_6_MASK;
  constexpr IOPAddress IOP_CHCR = 0x1f801538;
  constexpr IOPAddress IOP_DPCR2 = 0x1f801570;
  constexpr IOPAddress IOP_DICR = 0x1f8010f4;
  constexpr IOPAddress IOP_DICR2 = 0x1f801574;
  constexpr IOPWord IOP_FLAG = 0x08000000;
  constexpr IOPWord IOP_MASK = 0x00080000;
  constexpr IOPWord IOP_SUMMARY_ENABLE = 0x00800000;
  constexpr IOPWord IOP_SUMMARY = 0x80000000;
  constexpr IOPWord IOP_DMA_CAUSE = 8;
  constexpr IOPWord IOP_START = 0x01000400;
  constexpr IOPDMABank BANK = IOPDMABank::Secondary;

  void writeEE(NekoSystem &system, std::uint32_t address, std::uint32_t value)
  {
    REQUIRE(system.eeBus().writeData32(address, value));
  }

  void writeIOP(NekoSystem &system, IOPAddress address, IOPWord value)
  {
    REQUIRE(system.iopBus().write32(address, value).status == IOPBusStatus::Completed);
  }

  void writeSource(NekoSystem &system, std::uint32_t address,
    const SIFQuadword &words)
  {
    REQUIRE(system.eeBus().writeDMAC128(address, {
      static_cast<std::uint64_t>(words[0]) |
        (static_cast<std::uint64_t>(words[1]) << 32),
      static_cast<std::uint64_t>(words[2]) |
        (static_cast<std::uint64_t>(words[3]) << 32)}));
  }

  void installPacket(NekoSystem &system, std::uint32_t terminal)
  {
    writeSource(system, 0x1000, {{terminal | 0x2000, 4, 0xdeadbeef, 0xabcdef01}});
    writeSource(system, 0x1010, {{0x11111111, 0x22222222, 0x33333333, 0x44444444}});
  }

  void startSender(NekoSystem &system, std::uint32_t address, std::uint32_t count)
  {
    writeEE(system, EE_MADR, address);
    writeEE(system, EE_QWC, count);
    writeEE(system, EE_CHCR, 0x100);
    writeEE(system, EE_CTRL, DMACControl::DMA_ENABLE);
  }

  void armReceiver(NekoSystem &system)
  {
    writeIOP(system, IOP_DPCR2, 0x8000);
    writeIOP(system, IOP_CHCR, IOP_START);
  }

  IOPWord ramWord(NekoSystem &system, IOPAddress address)
  {
    const auto result = system.iopBus().read32(address);
    REQUIRE(result.status == IOPBusStatus::Completed);
    return result.value;
  }
}

TEST_CASE("SIF1 sender completion and acknowledgement do not complete receiver drain",
  "[ee][iop][sif1][dma][completion]")
{
  NekoSystem system;
  installPacket(system, 0x80000000);
  startSender(system, 0x1000, 2);
  armReceiver(system);
  system.runMasterCycles(2);
  REQUIRE_FALSE(system.sif1DMAC().active());
  REQUIRE(system.dmacController().status() == EE_FLAG);
  REQUIRE_FALSE(system.dmacController().interruptPending());
  REQUIRE(system.iopSIF1DMAC().active());
  REQUIRE(system.iopSIF1DMAC().remainingWords() == 4);
  REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 4);
  REQUIRE(system.iopDMAController().interruptStatus(BANK) == 0);
  REQUIRE(system.iopInterruptController().status() == 0);
  REQUIRE(ramWord(system, 0x2000) == 0);

  const auto iopHash = system.iopStateHash();
  const auto sifHash = system.sifStateHash();
  writeEE(system, EE_STAT, EE_FLAG);
  REQUIRE(system.dmacController().status() == 0);
  REQUIRE(system.iopStateHash() == iopHash);
  REQUIRE(system.sifStateHash() == sifHash);
  system.runMasterCycles(30);
  REQUIRE(system.iopSIF1DMAC().active());
  REQUIRE(system.iopSIF1DMAC().remainingWords() == 1);
  REQUIRE(system.iopDMAController().interruptStatus(BANK) == 0);
  system.clockMasterCycle();
  REQUIRE_FALSE(system.iopSIF1DMAC().active());
  REQUIRE(system.iopDMAController().interruptStatus(BANK) == IOP_FLAG);
  REQUIRE(system.iopInterruptController().status() == IOP_DMA_CAUSE);
  REQUIRE(system.dmacController().status() == 0);
  REQUIRE(ramWord(system, 0x200c) == 0x44444444);
}

TEST_CASE("SIF1 receiver completion cannot stop its sender or clear sender CTRL",
  "[ee][iop][sif1][dma][completion]")
{
  NekoSystem system;
  writeSource(system, 0x1000, {{0x80002000, 0, 0, 0}});
  writeSource(system, 0x1010, {{0x80003000, 4, 0, 0}});
  writeSource(system, 0x1020, {{1, 2, 3, 4}});
  startSender(system, 0x1000, 3);
  armReceiver(system);
  system.clockMasterCycle();
  REQUIRE_FALSE(system.iopSIF1DMAC().active());
  REQUIRE(system.iopDMAController().interruptStatus(BANK) == IOP_FLAG);
  REQUIRE(system.sif1DMAC().active());
  REQUIRE(system.sif1DMAC().quadwordCount() == 2);
  REQUIRE(system.dmacController().status() == 0);
  REQUIRE(system.sif().control() == 0x4000);
  const auto sifHash = system.sifStateHash();
  writeIOP(system, IOP_DICR2, IOP_FLAG);
  writeIOP(system, IOPBus::INTC_STAT, ~IOP_DMA_CAUSE);
  REQUIRE(system.sif().control() == 0x4000);
  REQUIRE(system.sifStateHash() == sifHash);
  REQUIRE(system.sif1DMAC().active());
  system.runMasterCycles(2);
  REQUIRE_FALSE(system.sif1DMAC().active());
  REQUIRE(system.dmacController().status() == EE_FLAG);
  REQUIRE(system.sif().control() == 0);
  REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 8);
  system.runMasterCycles(48);
  REQUIRE_FALSE(system.iopSIF1DMAC().active());
  REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 8);
  REQUIRE(system.iopDMAController().interruptStatus(BANK) == 0);
  REQUIRE(system.iopInterruptController().status() == 0);
  REQUIRE(ramWord(system, 0x3000) == 0);
  armReceiver(system);
  system.runMasterCycles(40);
  REQUIRE_FALSE(system.iopSIF1DMAC().active());
  REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 0);
  REQUIRE(ramWord(system, 0x300c) == 4);
  REQUIRE(system.iopDMAController().interruptStatus(BANK) == IOP_FLAG);
  REQUIRE(system.iopInterruptController().status() == IOP_DMA_CAUSE);
}

TEST_CASE("SIF1 EE completion uses W1C status and toggle masks independently of IOP",
  "[ee][iop][sif1][dma][completion]")
{
  for (const auto initialMask : {0u, EE_MASK})
  {
    CAPTURE(initialMask);
    NekoSystem system;
    installPacket(system, 0x80000000);
    writeEE(system, EE_STAT, initialMask);
    startSender(system, 0x1000, 2);
    armReceiver(system);
    system.runMasterCycles(2);
    REQUIRE(system.dmacController().status() == (EE_FLAG | initialMask));
    REQUIRE(system.dmacController().interruptPending() == (initialMask != 0));
    REQUIRE(system.interruptPending() == (initialMask != 0));
    REQUIRE(system.interruptController().status() == 0);
    const auto iopHash = system.iopStateHash();
    const auto sifHash = system.sifStateHash();
    writeEE(system, EE_STAT, EE_MASK);
    REQUIRE(system.dmacController().status() == (EE_FLAG | (initialMask ^ EE_MASK)));
    REQUIRE(system.dmacController().interruptPending() == (initialMask == 0));
    writeEE(system, EE_STAT, EE_MASK);
    REQUIRE(system.dmacController().status() == (EE_FLAG | initialMask));
    writeEE(system, EE_STAT, EE_FLAG);
    REQUIRE(system.dmacController().status() == initialMask);
    REQUIRE_FALSE(system.dmacController().interruptPending());
    REQUIRE_FALSE(system.interruptPending());
    REQUIRE(system.iopStateHash() == iopHash);
    REQUIRE(system.sifStateHash() == sifHash);
    system.runMasterCycles(31);
    REQUIRE(system.dmacController().status() == initialMask);
    REQUIRE(system.iopDMAController().interruptStatus(BANK) == IOP_FLAG);
  }
}

TEST_CASE("SIF1 IOP completion bypasses stored DMA masks but respects INTC delivery gates",
  "[ee][iop][sif1][dma][completion]")
{
  for (const auto terminal : {0x40000000u, 0x80000000u, 0xc0000000u})
  {
    for (const auto control : {0u, IOP_MASK, IOP_SUMMARY_ENABLE,
      IOP_MASK | IOP_SUMMARY_ENABLE})
    {
      for (const auto intcMask : {0u, IOP_DMA_CAUSE})
      {
        for (const IOPWord intcControl : {0u, 1u})
        {
          CAPTURE(terminal);
          CAPTURE(control);
          CAPTURE(intcMask);
          CAPTURE(intcControl);
          NekoSystem system;
          installPacket(system, terminal);
          writeIOP(system, IOP_DICR2, control);
          writeIOP(system, IOPBus::INTC_MASK, intcMask);
          writeIOP(system, IOPBus::INTC_CTRL, intcControl);
          startSender(system, 0x1000, 2);
          armReceiver(system);
          system.runMasterCycles(33);
          const auto summary = (control & IOP_SUMMARY_ENABLE) != 0 ?
            IOP_SUMMARY : 0;
          REQUIRE(system.iopDMAController().interruptStatus(BANK) ==
            (control | IOP_FLAG | summary));
          REQUIRE(system.iopDMAController().interruptPending());
          REQUIRE(system.iopInterruptController().status() == IOP_DMA_CAUSE);
          REQUIRE(system.iopInterruptController().interruptPending() ==
            (intcMask != 0 && intcControl != 0));
          REQUIRE_FALSE(system.iopSIF1DMAC().active());
          REQUIRE(ramWord(system, 0x200c) == 0x44444444);
          writeIOP(system, IOPBus::INTC_MASK, IOP_DMA_CAUSE);
          writeIOP(system, IOPBus::INTC_CTRL, 1);
          REQUIRE(system.iopInterruptController().interruptPending());
          REQUIRE(system.dmacController().status() == EE_FLAG);
          REQUIRE_FALSE(system.dmacController().interruptPending());
        }
      }
    }
  }
}

TEST_CASE("SIF1 real completion acknowledgements preserve peer status SIF state and SBUS",
  "[ee][iop][sif1][dma][completion]")
{
  for (const bool dmaFirst : {false, true})
  {
    CAPTURE(dmaFirst);
    NekoSystem system;
    installPacket(system, 0x80000000);
    system.sif().writeEE(SIFRegister::MSCOM, 0x1234);
    system.sif().writeIOP(SIFRegister::SMCOM, 0x5678);
    system.sif().writeEE(SIFRegister::MSFLAG, 0x12);
    system.sif().writeIOP(SIFRegister::SMFLAG, 0x34);
    system.interruptController().setSource(EEInterruptSource::SBUS, true);
    system.iopInterruptController().assertSource(IOPInterruptSource::SBUS);
    writeEE(system, EE_STAT, EE_MASK);
    writeIOP(system, IOP_DICR2, IOP_MASK | IOP_SUMMARY_ENABLE);
    startSender(system, 0x1000, 2);
    armReceiver(system);
    system.runMasterCycles(33);
    REQUIRE(system.dmacController().status() == (EE_FLAG | EE_MASK));
    REQUIRE(system.iopInterruptController().status() == (IOP_DMA_CAUSE | 2));
    const auto sifHash = system.sifStateHash();
    const auto eeHash = system.eeStateHash();
    const auto controls = IOP_MASK | IOP_SUMMARY_ENABLE;
    if (dmaFirst)
    {
      writeIOP(system, IOP_DICR2, controls | IOP_FLAG);
      REQUIRE(system.iopDMAController().interruptStatus(BANK) == controls);
      REQUIRE_FALSE(system.iopDMAController().interruptPending());
      REQUIRE(system.iopInterruptController().status() == (IOP_DMA_CAUSE | 2));
      writeIOP(system, IOPBus::INTC_STAT, ~IOP_DMA_CAUSE);
    }
    else
    {
      writeIOP(system, IOPBus::INTC_STAT, ~IOP_DMA_CAUSE);
      REQUIRE(system.iopDMAController().interruptPending());
      REQUIRE(system.iopInterruptController().status() == 2);
      system.runMasterCycles(16);
      REQUIRE(system.iopInterruptController().status() == 2);
      writeIOP(system, IOP_DICR2, controls | IOP_FLAG);
    }
    REQUIRE(system.iopDMAController().interruptStatus(BANK) == controls);
    REQUIRE_FALSE(system.iopDMAController().interruptPending());
    REQUIRE(system.iopInterruptController().status() == 2);
    REQUIRE(system.interruptController().status() == 2);
    REQUIRE(system.eeStateHash() == eeHash);
    REQUIRE(system.dmacController().interruptPending());
    const auto iopHash = system.iopStateHash();
    writeEE(system, EE_STAT, EE_FLAG);
    REQUIRE(system.dmacController().status() == EE_MASK);
    REQUIRE_FALSE(system.dmacController().interruptPending());
    REQUIRE(system.iopStateHash() == iopHash);
    REQUIRE(system.sifStateHash() == sifHash);
    REQUIRE(system.interruptController().status() == 2);
    REQUIRE(system.iopInterruptController().status() == 2);
  }
}

TEST_CASE("SIF1 retained completion republishes only on a new completion or DICR write",
  "[ee][iop][sif1][dma][completion]")
{
  NekoSystem system;
  installPacket(system, 0x80000000);
  startSender(system, 0x1000, 2);
  armReceiver(system);
  system.runMasterCycles(33);
  writeIOP(system, IOPBus::INTC_STAT, ~IOP_DMA_CAUSE);
  REQUIRE(system.iopDMAController().interruptStatus(BANK) == IOP_FLAG);
  REQUIRE(system.iopInterruptController().status() == 0);
  armReceiver(system);
  writeIOP(system, IOP_CHCR, IOP_START);
  const auto status = system.iopBus().read32(IOP_DICR2);
  REQUIRE(status.status == IOPBusStatus::Completed);
  REQUIRE(status.value == IOP_FLAG);
  system.runMasterCycles(16);
  REQUIRE(system.iopSIF1DMAC().active());
  REQUIRE(system.iopInterruptController().status() == 0);
  writeIOP(system, IOP_DICR, 0);
  REQUIRE(system.iopInterruptController().status() == IOP_DMA_CAUSE);
  writeIOP(system, IOPBus::INTC_STAT, ~IOP_DMA_CAUSE);
  writeIOP(system, IOP_DICR2, 0);
  REQUIRE(system.iopDMAController().interruptStatus(BANK) == IOP_FLAG);
  REQUIRE(system.iopInterruptController().status() == IOP_DMA_CAUSE);
  writeIOP(system, IOPBus::INTC_STAT, ~IOP_DMA_CAUSE);
  REQUIRE(system.dmacController().status() == EE_FLAG);
  startSender(system, 0x1000, 2);
  REQUIRE(system.dmacController().status() == EE_FLAG);
  REQUIRE(system.iopInterruptController().status() == 0);
  system.runMasterCycles(48);
  REQUIRE_FALSE(system.iopSIF1DMAC().active());
  REQUIRE(system.iopDMAController().interruptStatus(BANK) == IOP_FLAG);
  REQUIRE(system.iopInterruptController().status() == IOP_DMA_CAUSE);
}

TEST_CASE("SIF1 sender rearming can finish an independently waiting receiver packet",
  "[ee][iop][sif1][dma][completion]")
{
  NekoSystem system;
  writeSource(system, 0x1000, {{0x80002000, 8, 0, 0}});
  writeSource(system, 0x1010, {{1, 2, 3, 4}});
  writeSource(system, 0x1020, {{5, 6, 7, 8}});
  startSender(system, 0x1000, 2);
  armReceiver(system);
  system.runMasterCycles(65);
  REQUIRE_FALSE(system.sif1DMAC().active());
  REQUIRE(system.dmacController().status() == EE_FLAG);
  REQUIRE(system.iopSIF1DMAC().active());
  REQUIRE(system.iopSIF1DMAC().remainingWords() == 4);
  REQUIRE(system.iopDMAController().interruptStatus(BANK) == 0);
  REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 0);
  writeEE(system, EE_STAT, EE_FLAG);
  const auto iopHash = system.iopStateHash();
  startSender(system, 0x1020, 1);
  REQUIRE(system.iopStateHash() == iopHash);
  REQUIRE(system.iopSIF1DMAC().remainingWords() == 4);
  system.runMasterCycles(32);
  REQUIRE_FALSE(system.sif1DMAC().active());
  REQUIRE_FALSE(system.iopSIF1DMAC().active());
  REQUIRE(system.dmacController().status() == EE_FLAG);
  REQUIRE(system.iopDMAController().interruptStatus(BANK) == IOP_FLAG);
  REQUIRE(ramWord(system, 0x201c) == 8);
}

TEST_CASE("SIF1 EE completion reaches DMAC INT1 at the next EE instruction boundary",
  "[ee][iop][sif1][dma][completion]")
{
  NekoSystem system;
  auto &core = system.eeCore();
  core.setCOP0Register(EECOP0Register::Status,
    EECOP0Status::INTERRUPT_ENABLE | EECOP0Status::MASTER_INTERRUPT_ENABLE |
      EECOP0Status::DMAC_MASK);
  writeEE(system, 0, 0);
  writeEE(system, 4, 0);
  installPacket(system, 0x80000000);
  writeEE(system, EE_STAT, EE_MASK);
  startSender(system, 0x1000, 2);
  armReceiver(system);
  core.startExecution(0xa0000000);
  system.runMasterCycles(2);
  REQUIRE_FALSE(system.sif1DMAC().active());
  REQUIRE(core.pendingException() == EEException::None);
  REQUIRE((core.cop0Register(EECOP0Register::Cause) &
    EECOP0Cause::DMAC_PENDING) != 0);
  REQUIRE((core.cop0Register(EECOP0Register::Cause) &
    EECOP0Cause::INTC_PENDING) == 0);
  const auto nextPC = core.programCounter();
  const auto previousInstruction = core.lastInstructionAddress();
  system.clockMasterCycle();
  REQUIRE(core.pendingException() == EEException::Interrupt);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == nextPC);
  REQUIRE(core.programCounter() == EEExceptionVector::INTERRUPT);
  REQUIRE(core.lastInstructionAddress() == previousInstruction);
  REQUIRE(system.iopSIF1DMAC().remainingWords() == 4);
  REQUIRE(system.iopDMAController().interruptStatus(BANK) == 0);
  REQUIRE(system.iopInterruptController().status() == 0);
}

TEST_CASE("SIF1 final payload completion precedes same-pulse IOP interrupt admission",
  "[ee][iop][sif1][dma][completion]")
{
  NekoSystem system;
  auto &core = system.iopCore();
  writeIOP(system, 0, 0x34010401); // ori r1, r0, IEc | IM2
  writeIOP(system, 4, 0x40816000); // mtc0 r1, Status
  core.startExecution(0xa0000000);
  system.runMasterCycles(16);
  writeIOP(system, IOPBus::INTC_MASK, IOP_DMA_CAUSE);
  writeIOP(system, IOPBus::INTC_CTRL, 1);
  installPacket(system, 0x80000000);
  startSender(system, 0x1000, 2);
  armReceiver(system);
  system.runMasterCycles(32);
  REQUIRE(system.iopSIF1DMAC().remainingWords() == 1);
  REQUIRE(system.iopInterruptController().status() == 0);
  REQUIRE(ramWord(system, 0x200c) == 0);
  const auto nextPC = core.programCounter();
  const auto retired = core.retiredInstructions();
  system.clockMasterCycle();
  REQUIRE(ramWord(system, 0x200c) == 0x44444444);
  REQUIRE_FALSE(system.iopSIF1DMAC().active());
  REQUIRE(system.iopDMAController().interruptStatus(BANK) == IOP_FLAG);
  REQUIRE(system.iopInterruptController().status() == IOP_DMA_CAUSE);
  REQUIRE(core.exception().kind == IOPException::Interrupt);
  REQUIRE(core.exceptionProgramCounter() == nextPC);
  REQUIRE(core.retiredInstructions() == retired);
}

TEST_CASE("SIF1 completion acknowledgement cannot silence an outstanding other-bank force",
  "[ee][iop][sif1][dma][completion]")
{
  NekoSystem system;
  constexpr IOPWord FORCE = 0x8000;
  installPacket(system, 0x80000000);
  writeIOP(system, IOP_DICR, FORCE);
  startSender(system, 0x1000, 2);
  armReceiver(system);
  system.runMasterCycles(33);
  REQUIRE(system.iopDMAController().interruptStatus(BANK) == IOP_FLAG);
  writeIOP(system, IOPBus::INTC_STAT, ~IOP_DMA_CAUSE);
  REQUIRE(system.iopInterruptController().status() == 0);
  writeIOP(system, IOP_DICR2, IOP_FLAG);
  REQUIRE(system.iopDMAController().interruptStatus(BANK) == 0);
  REQUIRE(system.iopDMAController().interruptStatus(IOPDMABank::Primary) ==
    (FORCE | IOP_SUMMARY));
  REQUIRE(system.iopDMAController().interruptPending());
  REQUIRE(system.iopInterruptController().status() == IOP_DMA_CAUSE);
  writeIOP(system, IOP_DICR, 0);
  REQUIRE_FALSE(system.iopDMAController().interruptPending());
  REQUIRE(system.iopInterruptController().status() == IOP_DMA_CAUSE);
  writeIOP(system, IOPBus::INTC_STAT, ~IOP_DMA_CAUSE);
  REQUIRE(system.iopInterruptController().status() == 0);
  REQUIRE(system.dmacController().status() == EE_FLAG);
}
