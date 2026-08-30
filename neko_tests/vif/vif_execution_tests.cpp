#include <cstdint>

#include "catch.hpp"
#include "vif.hpp"
#include "vpu.hpp"
#include "vpu_opcodes.hpp"

namespace
{
  constexpr std::uint16_t MICRO_INSTRUCTION_SIZE = 8;

  std::uint32_t vifCode(
    std::uint8_t command,
    std::uint16_t immediate = 0)
  {
    return
      (static_cast<std::uint32_t>(command) << 24) |
      immediate;
  }

  std::uint32_t vifControlInstruction(
    std::uint32_t encoding,
    std::uint8_t destination)
  {
    constexpr std::uint8_t DESTINATION_SHIFT = 16;
    return encoding |
      (static_cast<std::uint32_t>(destination) <<
       DESTINATION_SHIFT);
  }

  std::uint32_t iaddiu(
    std::uint8_t destination,
    std::uint8_t source,
    std::uint16_t immediate)
  {
    constexpr std::uint8_t IMMEDIATE_HIGH_SHIFT = 21;
    constexpr std::uint8_t DESTINATION_SHIFT = 16;
    constexpr std::uint8_t SOURCE_SHIFT = 11;
    constexpr std::uint16_t IMMEDIATE_HIGH_MASK = 0x7800;
    constexpr std::uint16_t IMMEDIATE_LOW_MASK = 0x07ff;

    return
      VPU_IADDIU_ENCODING |
      (static_cast<std::uint32_t>(
        immediate & IMMEDIATE_HIGH_MASK) <<
       (IMMEDIATE_HIGH_SHIFT - SOURCE_SHIFT)) |
      (static_cast<std::uint32_t>(destination) <<
       DESTINATION_SHIFT) |
      (static_cast<std::uint32_t>(source) << SOURCE_SHIFT) |
      (immediate & IMMEDIATE_LOW_MASK);
  }

  void writeTerminatingProgram(
    VPU *vpu,
    std::uint16_t startInstruction,
    std::uint32_t firstLower = VPU_LOWER_NOP)
  {
    vpu->writeMicroInstruction(
      startInstruction,
      firstLower,
      VPU_E_BIT | VPU_NOP);
    vpu->writeMicroInstruction(
      startInstruction + 1,
      VPU_LOWER_NOP,
      VPU_NOP);
  }
}

TEST_CASE("VIF Microprogram Execution Tests")
{
  SECTION("MSCAL starts at its instruction address and transfers ITOP")
  {
    VPU vpu(VPUType::VU0);
    VIF vif(VIFType::VIF0);
    vif.attachVPU(&vpu);
    writeTerminatingProgram(&vpu, 5);
    vif.ingestWord(vifCode(VIFCommandEncoding::ITOP, 0x0321));

    vif.ingestWord(vifCode(VIFCommandEncoding::MSCAL, 5));

    REQUIRE(vpu.getState() == VPU_STATE_RUN);
    REQUIRE(vpu.programCounter() == 5 * MICRO_INSTRUCTION_SIZE);
    REQUIRE(vif.itop() == 0x0321);
    REQUIRE_NOTHROW(vpu.run(32));
    REQUIRE(vpu.getState() == VPU_STATE_READY);
  }

  SECTION("A streamed MPG program executes through MSCAL")
  {
    VPU vpu(VPUType::VU1);
    VIF vif(VIFType::VIF1);
    vif.attachVPU(&vpu);
    vif.ingestWord(vifCode(VIFCommandEncoding::NOP));
    vif.ingestWord(
      (static_cast<std::uint32_t>(VIFCommandEncoding::MPG) << 24) |
      (UINT32_C(2) << 16) |
      6);
    vif.ingestWord(VPU_LOWER_NOP);
    vif.ingestWord(VPU_E_BIT | VPU_NOP);
    vif.ingestWord(VPU_LOWER_NOP);
    vif.ingestWord(VPU_NOP);

    vif.ingestWord(vifCode(VIFCommandEncoding::MSCAL, 6));
    vpu.run(32);

    REQUIRE(vpu.getState() == VPU_STATE_READY);
    REQUIRE(vpu.terminationPosition() == 8);
  }

  SECTION("VIF1 activation switches TOPS and DBF")
  {
    VPU vpu(VPUType::VU1);
    VIF vif(VIFType::VIF1);
    vif.attachVPU(&vpu);
    writeTerminatingProgram(&vpu, 0);
    writeTerminatingProgram(&vpu, 2);
    vif.ingestWord(vifCode(VIFCommandEncoding::BASE, 100));
    vif.ingestWord(vifCode(VIFCommandEncoding::OFFSET, 40));
    vif.ingestWord(vifCode(VIFCommandEncoding::ITOP, 12));

    vif.ingestWord(vifCode(VIFCommandEncoding::MSCAL, 0));

    REQUIRE(vif.top() == 100);
    REQUIRE(vif.tops() == 140);
    REQUIRE(vif.itop() == 12);
    REQUIRE(vif.doubleBufferFlag());
    vif.ingestWord(vifCode(VIFCommandEncoding::ITOP, 24));
    REQUIRE(vif.itop() == 12);
    REQUIRE(vif.itops() == 24);
    vpu.run(32);

    vif.ingestWord(vifCode(VIFCommandEncoding::MSCALF, 2));

    REQUIRE(vif.top() == 140);
    REQUIRE(vif.tops() == 100);
    REQUIRE(vif.itop() == 24);
    REQUIRE(!vif.doubleBufferFlag());
    vpu.run(32);
  }

  SECTION("MSCNT continues after the previously completed program")
  {
    VPU vpu(VPUType::VU1);
    VIF vif(VIFType::VIF1);
    vif.attachVPU(&vpu);
    writeTerminatingProgram(&vpu, 0);
    writeTerminatingProgram(&vpu, 2);

    vif.ingestWord(vifCode(VIFCommandEncoding::MSCAL, 0));
    vpu.run(32);
    REQUIRE(vpu.programCounter() == 2 * MICRO_INSTRUCTION_SIZE);

    vif.ingestWord(vifCode(VIFCommandEncoding::MSCNT));

    REQUIRE(vpu.getState() == VPU_STATE_RUN);
    REQUIRE(vpu.programCounter() == 2 * MICRO_INSTRUCTION_SIZE);
    vpu.run(32);
    REQUIRE(vpu.programCounter() == 4 * MICRO_INSTRUCTION_SIZE);
  }

  SECTION("Execution commands validate attachment and wait boundaries")
  {
    VIF vif(VIFType::VIF1);
    REQUIRE_THROWS_WITH(
      vif.ingestWord(vifCode(VIFCommandEncoding::MSCAL)),
      "VIF microprogram execution requires an attached VPU.");

    VPU vpu(VPUType::VU1);
    vif.attachVPU(&vpu);
    REQUIRE_THROWS_WITH(
      vif.ingestWord(vifCode(VIFCommandEncoding::MSCNT)),
      "VIF MSCNT requires a previously completed microprogram.");

    writeTerminatingProgram(&vpu, 0);
    vif.ingestWord(vifCode(VIFCommandEncoding::MSCAL));
    const std::uint16_t topBeforeRejectedStart = vif.top();
    const std::uint16_t topsBeforeRejectedStart = vif.tops();
    const bool dbfBeforeRejectedStart = vif.doubleBufferFlag();
    const std::uint64_t wordsBeforeStall = vif.wordsIngested();
    const VIFStreamWord stalled =
      vif.ingestWord(vifCode(VIFCommandEncoding::MSCAL));
    REQUIRE(stalled.stalled);
    REQUIRE(vif.wordsIngested() == wordsBeforeStall);
    REQUIRE(vif.top() == topBeforeRejectedStart);
    REQUIRE(vif.tops() == topsBeforeRejectedStart);
    REQUIRE(vif.doubleBufferFlag() == dbfBeforeRejectedStart);

    vpu.run(32);
    REQUIRE_THROWS_WITH(
      vif.ingestWord(vifCode(
        VIFCommandEncoding::MSCAL,
        vpu.microMemorySize() / MICRO_INSTRUCTION_SIZE)),
      "VIF microprogram start is outside VU micro memory.");
  }
}

TEST_CASE("VU VIF Control Instruction Tests")
{
  SECTION("XTOP and XITOP decode their integer destinations")
  {
    const LowerInstruction xtop = decodeLowerInstruction(
      vifControlInstruction(VPU_XTOP_ENCODING, 10));
    const LowerInstruction xitop = decodeLowerInstruction(
      vifControlInstruction(VPU_XITOP_ENCODING, 11));

    REQUIRE(xtop.unit == LowerExecutionUnit::VIFControl);
    REQUIRE(xtop.opCode == VPU_XTOP);
    REQUIRE(xtop.integerDestinationRegister == 10);
    REQUIRE(xitop.unit == LowerExecutionUnit::VIFControl);
    REQUIRE(xitop.opCode == VPU_XITOP);
    REQUIRE(xitop.integerDestinationRegister == 11);

    REQUIRE_THROWS_WITH(
      decodeLowerInstruction(
        vifControlInstruction(VPU_XTOP_ENCODING, 10) |
        (UINT32_C(1) << 21)),
      "Unsupported VU lower instruction.");
  }

  SECTION("VU1 reads TOP and ITOP at the VIF control T stage")
  {
    VPU vpu(VPUType::VU1);
    VIF vif(VIFType::VIF1);
    vif.attachVPU(&vpu);
    vif.ingestWord(vifCode(VIFCommandEncoding::BASE, 200));
    vif.ingestWord(vifCode(VIFCommandEncoding::OFFSET, 50));
    vif.ingestWord(vifCode(VIFCommandEncoding::ITOP, 77));
    vpu.writeMicroInstruction(
      0,
      vifControlInstruction(VPU_XTOP_ENCODING, 10),
      VPU_NOP);
    vpu.writeMicroInstruction(
      1,
      vifControlInstruction(VPU_XITOP_ENCODING, 11),
      VPU_NOP);
    vpu.writeMicroInstruction(
      2,
      VPU_LOWER_NOP,
      VPU_E_BIT | VPU_NOP);
    vpu.writeMicroInstruction(
      3,
      VPU_LOWER_NOP,
      VPU_NOP);

    vif.ingestWord(vifCode(VIFCommandEncoding::MSCAL));
    vpu.run(32);

    REQUIRE(vpu.intRegisterValue(10) == 200);
    REQUIRE(vpu.intRegisterValue(11) == 77);
  }

  SECTION("The following instruction consumes XTOP after one cycle")
  {
    VPU vpu(VPUType::VU1);
    VIF vif(VIFType::VIF1);
    vif.attachVPU(&vpu);
    vif.ingestWord(vifCode(VIFCommandEncoding::BASE, 200));
    vif.ingestWord(vifCode(VIFCommandEncoding::OFFSET, 50));
    vpu.writeMicroInstruction(
      0,
      vifControlInstruction(VPU_XTOP_ENCODING, 1),
      VPU_NOP);
    vpu.writeMicroInstruction(
      1,
      iaddiu(2, 1, 1),
      VPU_NOP);
    vpu.writeMicroInstruction(
      2,
      VPU_LOWER_NOP,
      VPU_E_BIT | VPU_NOP);
    vpu.writeMicroInstruction(
      3,
      VPU_LOWER_NOP,
      VPU_NOP);

    vif.ingestWord(vifCode(VIFCommandEncoding::MSCAL));
    vpu.run(32);

    REQUIRE(vpu.intRegisterValue(1) == 200);
    REQUIRE(vpu.intRegisterValue(2) == 201);
  }

  SECTION("XITOP is supported on VU0 and VI00 remains constant")
  {
    VPU vpu(VPUType::VU0);
    VIF vif(VIFType::VIF0);
    vif.attachVPU(&vpu);
    vif.ingestWord(vifCode(VIFCommandEncoding::ITOP, 123));
    vpu.writeMicroInstruction(
      0,
      vifControlInstruction(VPU_XITOP_ENCODING, 5),
      VPU_NOP);
    vpu.writeMicroInstruction(
      1,
      vifControlInstruction(VPU_XITOP_ENCODING, 0),
      VPU_E_BIT | VPU_NOP);
    vpu.writeMicroInstruction(
      2,
      VPU_LOWER_NOP,
      VPU_NOP);

    vif.ingestWord(vifCode(VIFCommandEncoding::MSCAL));
    vpu.run(32);

    REQUIRE(vpu.intRegisterValue(5) == 123);
    REQUIRE(vpu.intRegisterValue(0) == 0);
  }

  SECTION("XTOP rejects VU0 and both instructions require a VIF")
  {
    VPU vu0(VPUType::VU0);
    writeTerminatingProgram(
      &vu0,
      0,
      vifControlInstruction(VPU_XTOP_ENCODING, 1));
    vu0.startMicroMode();
    REQUIRE_THROWS_WITH(
      vu0.run(32),
      "VU VIF control instruction requires an attached VIF.");

    VPU vu1(VPUType::VU1);
    writeTerminatingProgram(
      &vu1,
      0,
      vifControlInstruction(VPU_XITOP_ENCODING, 1));
    vu1.startMicroMode();
    REQUIRE_THROWS_WITH(
      vu1.run(32),
      "VU VIF control instruction requires an attached VIF.");

    VPU attachedVU0(VPUType::VU0);
    VIF vif0(VIFType::VIF0);
    vif0.attachVPU(&attachedVU0);
    writeTerminatingProgram(
      &attachedVU0,
      0,
      vifControlInstruction(VPU_XTOP_ENCODING, 1));
    vif0.ingestWord(vifCode(VIFCommandEncoding::MSCAL));
    REQUIRE_THROWS_WITH(
      attachedVU0.run(32),
      "XTOP is only supported on VU1.");
  }
}
