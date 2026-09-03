#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "catch.hpp"
#include "elf_runner.hpp"
#include "neko_system.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_register_ids.hpp"

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

  void uploadVectorCopyProgram(VPU *vpu)
  {
    vpu->writeMicroInstruction(
      0,
      VPU_MOVE_ENCODING |
        (UINT32_C(0xf) << 21) |
        (static_cast<std::uint32_t>(
          VPU_REGISTER_VF02) << 16) |
        (static_cast<std::uint32_t>(
          VPU_REGISTER_VF01) << 11),
      VPU_E_BIT | VPU_NOP);
    vpu->writeMicroInstruction(
      1,
      VPU_LOWER_NOP,
      VPU_NOP);
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
    {"cop2_transfer.elf", 19},
    {"cop2_control.elf", 23},
    {"vu_macro_arithmetic.elf", 35},
    {"vu_macro_families.elf", 107}
  };

  for (const GuestExpectation &guest : guests)
  {
    CAPTURE(guest.fileName);
    NekoSystem system;
    const EEGuestExecutionResult result =
      system.runELF(readGuest(guest.fileName), 512);

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

TEST_CASE("PS2DEV EE ELF guest controls and polls vector units through COP2")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("cop2_control.elf"), 96);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(system.vu0().intRegisterValue(3) == UINT16_C(0x7f01));
  REQUIRE(system.eeCore().generalRegister(11).low == 1);
  REQUIRE(system.eeCore().generalRegister(12).low == 1);
  REQUIRE(system.vu1().getState() == VPU_STATE_STOP);
  REQUIRE(system.vu1().stoppedByForceBreak());
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

TEST_CASE("PS2DEV EE ELF guest calls VU0 microprograms through COP2")
{
  NekoSystem system;
  uploadVectorCopyProgram(&system.vu0());

  const EEGuestExecutionResult result =
    system.runELF(readGuest("vcallms.elf"), 128);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  REQUIRE(result.execution.instructions == 36);
  const FPRegister *vf1 = system.vu0().fpRegisterValue(1);
  const FPRegister *vf2 = system.vu0().fpRegisterValue(2);
  REQUIRE(vf2->x.bits() == vf1->x.bits());
  REQUIRE(vf2->y.bits() == vf1->y.bits());
  REQUIRE(vf2->z.bits() == vf1->z.bits());
  REQUIRE(vf2->w.bits() == vf1->w.bits());
  REQUIRE_FALSE(system.vu0().clockActive());
}

TEST_CASE("PS2DEV EE ELF guest executes VU0 macro arithmetic")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(
      readGuest("vu_macro_arithmetic.elf"),
      128);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  const FPRegister *dependent =
    system.vu0().fpRegisterValue(4);
  REQUIRE(dependent->x == 1);
  REQUIRE(dependent->y == 2);
  REQUIRE(dependent->z == 3);
  REQUIRE(dependent->w == 4);
  const FPRegister *broadcast =
    system.vu0().fpRegisterValue(5);
  REQUIRE(broadcast->x == 6);
  REQUIRE(broadcast->y == 16);
  REQUIRE(broadcast->z == 26);
  REQUIRE(broadcast->w == 36);
}

TEST_CASE("PS2DEV EE ELF guest executes remaining VU0 macro families")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(
      readGuest("vu_macro_families.elf"),
      512);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  REQUIRE(result.execution.instructions == 107);
  REQUIRE(system.vu0().intRegisterValue(3) == 12);
  const FPRegister *moved =
    system.vu0().fpRegisterValue(8);
  REQUIRE(moved->x == 20);
  REQUIRE(moved->y == 3);
  REQUIRE(moved->z == 24);
  REQUIRE(moved->w == 10);
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

TEST_CASE("PS2DEV EE ELF guest renders a rotating VU1 triangle")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("rotation_vu1.elf"), 4096);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  REQUIRE(result.execution.instructions == 705);
  REQUIRE(system.vu1().getState() == VPU_STATE_READY);
  REQUIRE_FALSE(system.gifPath1().path1TransferActive());
  REQUIRE(system.gifPath1().transferredQuadwordCount() == 12);
  REQUIRE(system.vif1DMAC().transferredQuadwordCount() == 38);
  REQUIRE(system.vif1().wordsIngested() == 152);
  REQUIRE(system.gs().triangleCount() == 1);
  const GIFQuadword first =
    system.vu1().readDataQuadword(12);
  const GIFQuadword second =
    system.vu1().readDataQuadword(14);
  const GIFQuadword third =
    system.vu1().readDataQuadword(16);
  REQUIRE(first[0] == UINT32_C(0x00008000));
  REQUIRE(first[1] == UINT32_C(0x00007caf));
  REQUIRE(first[2] == UINT32_C(0x00000c00));
  REQUIRE(second[0] == UINT32_C(0x000071db));
  REQUIRE(second[1] == UINT32_C(0x00008ad4));
  REQUIRE(second[2] == UINT32_C(0x00000c00));
  REQUIRE(third[0] == UINT32_C(0x00007783));
  REQUIRE(third[1] == UINT32_C(0x0000907c));
  REQUIRE(third[2] == UINT32_C(0x00000c00));
  REQUIRE(system.gs().pixelWriteCount() == 20566);
  REQUIRE(
    system.gs().framebufferHash(0, 640, 448) ==
    UINT64_C(0xdf0bce57b91fbbd3));
}

TEST_CASE("PS2DEV EE ELF guest renders the point and sprite scene")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("point_sprite.elf"), 40000);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  REQUIRE(system.gifPath3().guestFIFOQuadwordCount() == 0);
  REQUIRE(system.gifPath3().transferredQuadwordCount() == 226);
  REQUIRE(system.gs().pointCount() == 96);
  REQUIRE(system.gs().lineCount() == 0);
  REQUIRE(system.gs().spriteCount() == 7);
  REQUIRE(system.gs().triangleCount() == 0);
  REQUIRE(
    system.gs().framebufferHash(0, 640, 448) ==
    UINT64_C(0xc1adcf6554c82b99));
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
