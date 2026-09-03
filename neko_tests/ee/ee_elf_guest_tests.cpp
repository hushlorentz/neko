#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "catch.hpp"
#include "elf_runner.hpp"
#include "neko_system.hpp"

namespace
{
  std::string guestPath(const std::string &fileName)
  {
    return
      std::string(NEKO_EE_ELF_GUEST_DIR) + "/" + fileName;
  }

  std::vector<std::uint8_t> readGuest(
    const std::string &fileName)
  {
    const std::string path = guestPath(fileName);
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
      throw std::runtime_error(
        "Could not open EE ELF guest: " + path);
    }
    return std::vector<std::uint8_t>(
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>());
  }
}

TEST_CASE("PS2DEV scalar EE ELF guests complete successfully")
{
  struct GuestExpectation
  {
    const char *fileName;
    std::uint64_t instructions;
  };
  const GuestExpectation guests[] = {
    {"arithmetic.elf", 16},
    {"branches.elf", 12},
    {"memory.elf", 23},
    {"mmio.elf", 20},
    {"fifo.elf", 16},
    {"vif1_dma.elf", 34},
    {"cop2_transfer.elf", 19}
  };

  for (const GuestExpectation &guest : guests)
  {
    CAPTURE(guest.fileName);
    NekoSystem system;
    const EEGuestExecutionResult result =
      system.runELF(readGuest(guest.fileName), 128);

    REQUIRE(result.outcome == EEGuestOutcome::Completed);
    REQUIRE(result.exitCode == 0);
    REQUIRE(
      result.execution.instructions ==
      guest.instructions);
    REQUIRE_FALSE(result.execution.cycleLimitReached);
    REQUIRE(
      result.execution.programCounter ==
      EEGuestRuntime::RETURN_ADDRESS);
  }
}

TEST_CASE("PS2DEV EE ELF guest transfers vectors through VU0 COP2")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("cop2_transfer.elf"), 64);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  const FPRegister *vf1 = system.vu0().fpRegisterValue(1);
  const FPRegister *vf2 = system.vu0().fpRegisterValue(2);
  REQUIRE(vf1->x.bits() == UINT32_C(0x33221100));
  REQUIRE(vf1->y.bits() == UINT32_C(0x77665544));
  REQUIRE(vf1->z.bits() == UINT32_C(0xbbaa9988));
  REQUIRE(vf1->w.bits() == UINT32_C(0xffeeddcc));
  REQUIRE(vf2->x.bits() == vf1->x.bits());
  REQUIRE(vf2->y.bits() == vf1->y.bits());
  REQUIRE(vf2->z.bits() == vf1->z.bits());
  REQUIRE(vf2->w.bits() == vf1->w.bits());
}

TEST_CASE("PS2DEV EE ELF guest configures VIF1 DMA")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("vif1_dma.elf"), 128);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(system.vif1().wordsIngested() == 4);
  REQUIRE(system.vif1().fifoQuadwordCount() == 0);
  REQUIRE(system.vif1DMAC().transferredQuadwordCount() == 1);
  REQUIRE(
    (system.vif1DMAC().channelControl() &
     GIFDMACChannelControl::START) == 0);
  REQUIRE(
    (system.gifDMAC().globalStatus() &
     GIFDMACStatus::CHANNEL_1) != 0);
  REQUIRE(
    (system.gifDMAC().globalStatus() &
     GIFDMACStatus::CHANNEL_1_MASK) != 0);
  REQUIRE(system.gifDMAC().interruptPending());
}

TEST_CASE("PS2DEV EE ELF guest writes VIF and GIF FIFOs")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("fifo.elf"), 128);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(system.vif0().wordsIngested() == 4);
  REQUIRE(system.vif1().wordsIngested() == 4);
  REQUIRE(system.vif0().fifoQuadwordCount() == 0);
  REQUIRE(system.vif1().fifoQuadwordCount() == 0);
  REQUIRE(system.gifPath3().guestFIFOQuadwordCount() == 0);
  REQUIRE(system.gifPath3().transferredQuadwordCount() == 1);
}

TEST_CASE("PS2DEV EE ELF guest drives mapped device registers")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("mmio.elf"), 128);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(
    system.interruptController().mask() ==
    EEInterruptSource::mask(EEInterruptSource::VIF0));
  REQUIRE(system.gifDMAC().globalControl() == 1);
  REQUIRE(system.gs().hostInterfaceReversed());
}

TEST_CASE("PS2DEV EE ELF guest runs through frontend support")
{
  const neko_frontend::ELFRunReport report =
    neko_frontend::runELFFile(
      guestPath("arithmetic.elf"),
      128);

  REQUIRE(report.result.outcome == EEGuestOutcome::Completed);
  REQUIRE(report.result.exitCode == 0);
  REQUIRE(report.hostExitCode == 0);
  REQUIRE(
    report.diagnostic.find("outcome=completed") !=
    std::string::npos);
}
