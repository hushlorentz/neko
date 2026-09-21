#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "catch.hpp"
#include "ee_bus.hpp"
#include "floating_point_ops.hpp"
#include "gif_dmac_channel.hpp"
#include "neko_system.hpp"
#include "vif_command.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_register_ids.hpp"
#include "vpu_upper_instruction_utils.hpp"

namespace
{
  constexpr std::size_t SAVE_STATE_HEADER_SIZE = 28;
  constexpr std::size_t SAVE_STATE_CHECKSUM_OFFSET = 20;
  constexpr std::size_t SAVE_STATE_VERSION_OFFSET = 8;
  constexpr std::size_t MASTER_CLOCK_FIRST_COMPONENT_OFFSET = 46;
  constexpr std::size_t MASTER_CLOCK_COMPONENT_SIZE = 17;
  constexpr std::size_t
    VERSION_25_PREPARED_STATE_SIZE = 37800432;
  constexpr std::uint64_t
    VERSION_25_PREPARED_STATE_HASH =
      UINT64_C(0x27c762f3e0d1cfb9);
  constexpr std::size_t PREPARED_EE_GPR_ZERO_HIGH_OFFSET = 173;
  constexpr std::size_t PREPARED_EE_FCR31_OFFSET = 809;
  constexpr std::size_t EE_COP1_DIVIDER_INITIATION_OFFSET = 972;
  constexpr std::size_t EE_COP1_DIVIDER_OPERATION_OFFSET = 973;
  constexpr std::size_t
    SIMPLE_EE_COP1_DIVIDER_INITIATION_OFFSET = 955;
  constexpr std::size_t
    SIMPLE_EE_COP1_DIVIDER_OPERATION_OFFSET = 956;
  constexpr std::size_t
    SIMPLE_EE_RETIRED_COP1_OPERATE_RESOURCE_OFFSET = 957;
  constexpr std::size_t
    SIMPLE_EE_YOUNGER_A_STAGE_ACTIVE_OFFSET = 959;
  constexpr std::size_t
    SIMPLE_EE_YOUNGER_A_STAGE_INSTRUCTION_OFFSET = 960;
  constexpr std::size_t SIMPLE_EE_EXECUTION_STATE_OFFSET = 869;
  constexpr std::size_t SIMPLE_EE_STOP_REASON_OFFSET = 870;
  constexpr std::size_t
    SIMPLE_EE_PENDING_MAC0_REMAINING_CYCLES_OFFSET = 893;
  constexpr std::size_t
    SIMPLE_EE_PENDING_MAC0_GENERAL_REGISTER_OFFSET = 911;
  constexpr std::size_t
    SIMPLE_EE_PENDING_MAC1_REMAINING_CYCLES_OFFSET = 921;
  constexpr std::size_t
    SIMPLE_EE_PENDING_MAC1_ACTIVE_OFFSET = 920;
  constexpr std::size_t
    SIMPLE_EE_PENDING_MAC1_GENERAL_REGISTER_OFFSET = 939;
  constexpr std::size_t
    SIMPLE_EE_NEXT_PROGRAM_ORDER_OFFSET = 1012;
  constexpr std::size_t
    PREPARED_EE_BRANCH_DELAY_LIKELY_OFFSET = 1003;
  constexpr std::size_t EE_COP1_POST_DELAY_COUNT_OFFSET = 1005;
  constexpr std::size_t EE_COP1_POST_DELAY_ADDRESS_OFFSET = 1006;
  constexpr std::size_t EE_COP1_POST_DELAY_TARGET_OFFSET = 1010;
  constexpr std::size_t EE_COP1_POST_DELAY_TAKEN_OFFSET = 1014;
  constexpr std::size_t EE_COP1_POST_TARGET_COUNT_OFFSET = 1015;
  constexpr std::size_t EE_COP1_POST_TARGET_ADDRESS_OFFSET = 1016;
  constexpr std::size_t EE_ISSUE_LATCH_ADDRESS_OFFSET = 1021;
  constexpr std::size_t EE_NEXT_PROGRAM_ORDER_OFFSET = 1029;
  constexpr std::size_t EE_FIRST_IN_FLIGHT_COP1_ACTIVE_OFFSET = 1037;
  constexpr std::size_t EE_FIRST_IN_FLIGHT_COP1_ORDER_OFFSET = 1038;
  constexpr std::size_t EE_FIRST_IN_FLIGHT_COP1_STAGE_OFFSET = 1046;
  constexpr std::size_t
    EE_FIRST_IN_FLIGHT_COP1_ADDRESS_OFFSET = 1047;
  constexpr std::size_t
    EE_FIRST_IN_FLIGHT_COP1_INSTRUCTION_OFFSET = 1051;
  constexpr std::size_t
    EE_FIRST_IN_FLIGHT_COP1_CAPTURED_GPR_OFFSET = 1071;
  constexpr std::size_t
    EE_FIRST_IN_FLIGHT_COP1_MEMORY_ADDRESS_OFFSET = 1079;
  constexpr std::size_t
    EE_FIRST_IN_FLIGHT_COP1_DESTINATION_MASK_OFFSET = 1087;
  constexpr std::size_t
    EE_FIRST_IN_FLIGHT_COP1_DESTINATION_FPR_OFFSET = 1088;
  constexpr std::size_t
    EE_FIRST_IN_FLIGHT_COP1_REMAINING_CYCLES_OFFSET = 1098;
  constexpr std::size_t
    EE_SECOND_IN_FLIGHT_COP1_DESTINATION_FPR_OFFSET = 1150;
  constexpr std::size_t
    SIMPLE_EE_FIRST_IN_FLIGHT_COP1_DESTINATION_MASK_OFFSET = 1070;
  constexpr std::size_t
    SIMPLE_EE_FIRST_IN_FLIGHT_COP1_DESTINATION_FPR_OFFSET = 1071;
  constexpr std::size_t
    SIMPLE_EE_FIRST_IN_FLIGHT_COP1_RAW_RESULT_OFFSET = 1073;
  constexpr std::size_t
    SIMPLE_EE_FIRST_IN_FLIGHT_COP1_RAISED_FLAGS_OFFSET = 1078;
  constexpr std::size_t
    SIMPLE_EE_FIRST_IN_FLIGHT_COP1_CONDITION_OFFSET = 1080;
  constexpr std::size_t
    SIMPLE_EE_FIRST_IN_FLIGHT_COP1_ORDER_OFFSET = 1021;
  constexpr std::size_t
    SIMPLE_EE_FIRST_IN_FLIGHT_COP1_STAGE_OFFSET = 1029;
  constexpr std::size_t
    SIMPLE_EE_FIRST_IN_FLIGHT_COP1_INSTRUCTION_OFFSET = 1034;
  constexpr std::size_t
    SIMPLE_EE_FIRST_IN_FLIGHT_COP1_CAPTURED_FS_OFFSET = 1038;
  constexpr std::size_t
    SIMPLE_EE_FIRST_IN_FLIGHT_COP1_CAPTURED_ACC_OFFSET = 1046;
  constexpr std::size_t
    SIMPLE_EE_FIRST_IN_FLIGHT_COP1_RAISED_STICKY_FLAGS_OFFSET = 1079;
  constexpr std::size_t
    SIMPLE_EE_FIRST_IN_FLIGHT_COP1_REMAINING_CYCLES_OFFSET = 1081;
  constexpr std::size_t
    SIMPLE_EE_SECOND_IN_FLIGHT_COP1_ACTIVE_OFFSET = 1082;
  constexpr std::size_t
    SIMPLE_EE_SECOND_IN_FLIGHT_COP1_ORDER_OFFSET = 1083;
  constexpr std::size_t
    SIMPLE_EE_SECOND_IN_FLIGHT_COP1_STAGE_OFFSET = 1091;
  constexpr std::size_t
    SIMPLE_EE_SECOND_IN_FLIGHT_COP1_ADDRESS_OFFSET = 1092;
  constexpr std::size_t
    SIMPLE_EE_SECOND_IN_FLIGHT_COP1_INSTRUCTION_OFFSET = 1096;
  constexpr std::size_t
    SIMPLE_EE_SECOND_IN_FLIGHT_COP1_CAPTURED_GPR_OFFSET = 1116;
  constexpr std::size_t
    SIMPLE_EE_SECOND_IN_FLIGHT_COP1_DESTINATION_MASK_OFFSET = 1132;
  constexpr std::size_t
    SIMPLE_EE_SECOND_IN_FLIGHT_COP1_DESTINATION_FPR_OFFSET = 1133;
  constexpr std::size_t
    SIMPLE_EE_SECOND_IN_FLIGHT_COP1_DESTINATION_GPR_OFFSET = 1134;
  constexpr std::size_t
    SIMPLE_EE_SECOND_IN_FLIGHT_COP1_REMAINING_CYCLES_OFFSET = 1143;
  constexpr std::size_t
    SIMPLE_EE_THIRD_IN_FLIGHT_COP1_ORDER_OFFSET = 1145;
  constexpr std::size_t
    SIMPLE_EE_THIRD_IN_FLIGHT_COP1_STAGE_OFFSET = 1153;
  constexpr std::size_t
    SIMPLE_EE_THIRD_IN_FLIGHT_COP1_CAPTURED_FS_OFFSET = 1162;
  constexpr std::size_t
    SIMPLE_EE_THIRD_IN_FLIGHT_COP1_RAW_RESULT_OFFSET = 1197;
  constexpr std::size_t PREPARED_MAIN_MEMORY_SIZE_OFFSET = 2037;
  constexpr std::uint8_t COP1_STAGE_X = 2;
  constexpr std::uint8_t COP1_STAGE_T = 1;
  constexpr std::uint8_t COP1_STAGE_Y = 3;
  constexpr std::uint8_t COP1_STAGE_Z = 4;
  constexpr std::uint8_t COP1_STAGE_S1 = 5;
  constexpr std::uint8_t COP1_STAGE_S2 = 6;
  constexpr std::uint64_t SAVE_STATE_FNV_OFFSET_BASIS =
    UINT64_C(14695981039346656037);
  constexpr std::uint64_t SAVE_STATE_FNV_PRIME =
    UINT64_C(1099511628211);

  std::uint64_t hashBytes(
    const std::vector<std::uint8_t> &bytes,
    std::size_t offset = 0)
  {
    std::uint64_t hash = SAVE_STATE_FNV_OFFSET_BASIS;
    for (std::size_t index = offset;
         index < bytes.size();
         ++index)
    {
      hash ^= bytes[index];
      hash *= SAVE_STATE_FNV_PRIME;
    }
    return hash;
  }

  void requireStateBytesEqual(
    const std::vector<std::uint8_t> &actual,
    const std::vector<std::uint8_t> &expected)
  {
    REQUIRE(actual.size() == expected.size());
    std::size_t mismatch = SAVE_STATE_HEADER_SIZE;
    while (mismatch < actual.size() &&
           actual[mismatch] == expected[mismatch])
    {
      ++mismatch;
    }
    INFO("save-state byte offset: " << mismatch);
    REQUIRE(mismatch == actual.size());
  }

  void updateChecksum(std::vector<std::uint8_t> *state)
  {
    const std::uint64_t checksum =
      hashBytes(*state, SAVE_STATE_HEADER_SIZE);
    for (std::size_t index = 0; index < 8; ++index)
    {
      (*state)[SAVE_STATE_CHECKSUM_OFFSET + index] =
        static_cast<std::uint8_t>(
          checksum >> (index * 8));
    }
  }

  void writeU32(
    std::vector<std::uint8_t> *state,
    std::size_t offset,
    std::uint32_t value)
  {
    for (std::size_t index = 0; index < 4; ++index)
    {
      (*state)[offset + index] =
        static_cast<std::uint8_t>(value >> (index * 8));
    }
  }

  void writeU64(
    std::vector<std::uint8_t> *state,
    std::size_t offset,
    std::uint64_t value)
  {
    for (std::size_t index = 0; index < 8; ++index)
    {
      (*state)[offset + index] =
        static_cast<std::uint8_t>(value >> (index * 8));
    }
  }

  std::uint32_t cop1TransferInstruction(
    std::uint8_t type,
    std::uint8_t generalRegister,
    std::uint8_t floatingPointRegister)
  {
    return
      (UINT32_C(0x11) << 26) |
      (static_cast<std::uint32_t>(type) << 21) |
      (static_cast<std::uint32_t>(generalRegister) << 16) |
      (static_cast<std::uint32_t>(
        floatingPointRegister) << 11);
  }

  std::uint32_t cop1SingleInstruction(
    std::uint8_t function,
    std::uint8_t sourceRegister,
    std::uint8_t destinationRegister,
    std::uint8_t targetRegister)
  {
    return
      (UINT32_C(0x11) << 26) |
      (UINT32_C(0x10) << 21) |
      (static_cast<std::uint32_t>(targetRegister) << 16) |
      (static_cast<std::uint32_t>(sourceRegister) << 11) |
      (static_cast<std::uint32_t>(
        destinationRegister) << 6) |
      function;
  }

  std::uint32_t vifCode(
    std::uint8_t command,
    std::uint16_t immediate = 0)
  {
    return
      (static_cast<std::uint32_t>(command) << 24) |
      immediate;
  }

  GIFQuadword gifTag(
    std::uint16_t loopCount,
    bool endOfPacket,
    GIFDataFormat format,
    std::uint8_t descriptor)
  {
    const std::uint64_t low =
      loopCount |
      (static_cast<std::uint64_t>(endOfPacket) << 15) |
      (static_cast<std::uint64_t>(format) << 58) |
      (UINT64_C(1) << 60);
    return GIFQuadword{{
      static_cast<std::uint32_t>(low),
      static_cast<std::uint32_t>(low >> 32),
      descriptor,
      0
    }};
  }

  GIFQuadword adWrite(
    std::uint8_t address,
    std::uint64_t value)
  {
    return GIFQuadword{{
      static_cast<std::uint32_t>(value),
      static_cast<std::uint32_t>(value >> 32),
      address,
      0
    }};
  }

  GIFQuadword dmaTag(
    GIFDMATagID id,
    std::uint16_t qwc,
    std::uint32_t address = 0)
  {
    return GIFQuadword{{
      qwc | (static_cast<std::uint32_t>(id) << 28),
      address,
      0,
      0
    }};
  }

  std::uint32_t esum(std::uint8_t source)
  {
    return
      VPU_ESUM_ENCODING |
      (static_cast<std::uint32_t>(source) <<
       VPU_FS_REG_SHIFT);
  }

  void prepareInFlightSystem(NekoSystem *system)
  {
    system->setInput({0xa55a, 1, 2, 3, 4});
    system->eeCore().setGeneralRegister(
      1,
      {
        UINT64_C(0x0123456789abcdef),
        UINT64_C(0xfedcba9876543210)
      });
    system->eeCore().setProgramCounter(0x80001000);
    system->eeCore().setHI(UINT64_C(0x1111111122222222));
    system->eeCore().setLO(UINT64_C(0x3333333344444444));
    system->eeCore().setHI1(UINT64_C(0x5555555566666666));
    system->eeCore().setLO1(UINT64_C(0x7777777788888888));
    system->eeCore().setShiftAmount(0x99);
    system->eeCore().setFloatingPointRegister(
      0,
      UINT32_C(0x3f800000));
    system->eeCore().setFloatingPointRegister(
      31,
      UINT32_C(0x80000000));
    system->eeCore().setFloatingPointAccumulator(
      UINT32_C(0x40400000));
    system->eeCore().setCOP1ControlRegister(
      31,
      EECOP1Control::STATUS_WRITABLE_MASK);
    system->eeCore().setCOP0Register(
      EECOP0Register::BadVAddr,
      UINT32_C(0x81234567));
    system->eeCore().setCOP0Register(
      EECOP0Register::Count,
      UINT32_C(0x12345678));
    system->eeCore().setCOP0Register(
      EECOP0Register::Compare,
      UINT32_C(0x87654321));
    system->eeCore().setCOP0Register(
      EECOP0Register::Status,
      UINT32_C(0xf0c79c1f));
    system->eeCore().setCOP0Register(
      EECOP0Register::Cause,
      UINT32_C(0x80008030));
    system->eeCore().setCOP0Register(
      EECOP0Register::EPC,
      UINT32_C(0x80001000));
    system->eeCore().setCOP0Register(
      EECOP0Register::ErrorEPC,
      UINT32_C(0xbfc00000));
    system->gsDisplay().configureTiming({3, 7});
    system->masterClockScheduler().registerComponent(
      system->gifPathArbiter(),
      3,
      1);
    system->eeBus().write32(
      EEMemoryMap::INTC_MASK,
      EEInterruptSource::mask(EEInterruptSource::GS) |
      EEInterruptSource::mask(EEInterruptSource::VIF0));

    system->vu1().loadFPRegister(
      VPU_REGISTER_VF01,
      1,
      2,
      3,
      4);
    system->vu1().writeMicroInstruction(
      0,
      esum(VPU_REGISTER_VF01),
      VPU_NOP);
    system->vu1().writeMicroInstruction(
      1,
      VPU_LOWER_NOP,
      VPU_E_BIT | VPU_NOP);
    system->vu1().writeMicroInstruction(
      2,
      VPU_LOWER_NOP,
      VPU_NOP);
    system->vu1().startMicroMode();

    system->vif0().ingestWord(
      vifCode(VIFCommandEncoding::STROW));
    system->vif0().ingestWord(0x11111111);
    system->vif0().ingestWord(0x22222222);

    system->gs().writeRegister(
      GSRegisterAddress::PRIM,
      static_cast<std::uint8_t>(GSPrimitiveType::Triangle));
    system->gs().writeRegister(
      GSRegisterAddress::RGBAQ,
      UINT64_C(0x3f800000ffffffff));
    system->gs().writeRegister(
      GSRegisterAddress::XYZ2,
      UINT64_C(0x0000000100100010));
    system->gs().writeDisplayPSMCT32(
      4,
      1,
      2,
      3,
      0xaabbccdd);
    system->eeBus().write64(EEMemoryMap::GS_BUSDIR, 1);
    system->eeBus().write32(
      EEMemoryMap::GIF_MODE,
      GIFMode::IMT);

    const GIFQuadword tag = gifTag(
      1,
      true,
      GIFDataFormat::Packed,
      GIFRegisterDescriptor::AD);
    const GIFQuadword payload = adWrite(
      GSRegisterAddress::PRIM,
      static_cast<std::uint8_t>(
        GSPrimitiveType::TriangleFan));
    REQUIRE(system->eeBus().writeQuadword(
      0x1000,
      dmaTag(GIFDMATagID::Call, 2, 0x2000)));
    REQUIRE(system->eeBus().writeQuadword(0x1010, tag));
    REQUIRE(system->eeBus().writeQuadword(0x1020, payload));
    REQUIRE(system->eeBus().writeQuadword(
      0x1030,
      dmaTag(GIFDMATagID::End, 0)));
    REQUIRE(system->eeBus().writeQuadword(
      0x2000,
      dmaTag(GIFDMATagID::Return, 0)));
    system->eeBus().write32(
      EEMemoryMap::D_CTRL,
      DMACControl::DMA_ENABLE);
    system->eeBus().write32(EEMemoryMap::D2_TADR, 0x1000);
    system->eeBus().write32(
      EEMemoryMap::D_STAT,
      DMACStatus::CHANNEL_2_MASK);
    system->eeBus().write32(
      EEMemoryMap::D2_CHCR,
      GIFDMACChannelControl::CHAIN_MODE |
      GIFDMACChannelControl::START);

    system->runMasterCycles(2);
    system->gsDisplay().clock();
    REQUIRE(system->vu1().getState() == VPU_STATE_RUN);
    REQUIRE(system->vif0().payloadWordsRemaining() == 2);
    REQUIRE(system->gifDecoder().packetInProgress());
    REQUIRE(system->gifDMAC().quadwordCount() == 1);
    REQUIRE(system->gifDMAC().addressStack(0) == 0x1030);
    REQUIRE(system->gs().queuedVertexCount() == 1);
    REQUIRE(system->gs().hostInterfaceReversed());
    REQUIRE(system->gsDisplay().inVerticalBlank());
    REQUIRE(
      system->masterClockScheduler().currentCycle() == 2);
  }

  void prepareSuspendedPath3(NekoSystem *system)
  {
    system->gs().writeRegister(
      GSRegisterAddress::BITBLTBUF,
      UINT64_C(1) << 48);
    system->gs().writeRegister(
      GSRegisterAddress::TRXPOS,
      0);
    system->gs().writeRegister(
      GSRegisterAddress::TRXREG,
      UINT64_C(40) | (UINT64_C(1) << 32));
    system->gs().writeRegister(
      GSRegisterAddress::TRXDIR,
      static_cast<std::uint8_t>(
        GSImageTransferDirection::HostToLocal));
    system->gifPathArbiter().setPath3IntermittentMode(true);

    const GIFQuadword tag = gifTag(
      10,
      true,
      GIFDataFormat::Image,
      GIFRegisterDescriptor::NOP);
    REQUIRE(
      system->gifPath3().submitQuadwords(&tag, 1)
        .transferredQuadwords == 1);
    system->vu1().writeDataQuadword(
      0,
      gifTag(
        0,
        true,
        GIFDataFormat::Packed,
        GIFRegisterDescriptor::NOP));
    system->gifPath1().startPath1Transfer(0);

    const GIFQuadword image = {{
      0x11223344,
      0x55667788,
      0x99aabbcc,
      0xddeeff00
    }};
    for (std::size_t index = 0; index < 8; ++index)
    {
      REQUIRE(
        system->gifPath3().submitQuadwords(&image, 1)
          .transferredQuadwords == 1);
    }
    REQUIRE(system->gifPathArbiter().path3Interrupted());
    REQUIRE(
      system->gifPathArbiter().activePath() ==
      GIFPath::Path1);
    REQUIRE(system->gifPath1().path1TransferActive());
    REQUIRE(system->gifDecoder().awaitingTag());
  }

  void finishSuspendedPath3(NekoSystem *system)
  {
    system->gifPath1().advancePath1Transfer();
    REQUIRE_FALSE(system->gifPath1().path1TransferActive());
    REQUIRE_FALSE(system->gifPathArbiter().path3Interrupted());

    const GIFQuadword image = {{
      0x01234567,
      0x89abcdef,
      0xfedcba98,
      0x76543210
    }};
    REQUIRE(
      system->gifPath3().submitQuadwords(&image, 1)
        .transferredQuadwords == 1);
    REQUIRE(
      system->gifPath3().submitQuadwords(&image, 1)
        .transferredQuadwords == 1);
    REQUIRE_FALSE(system->gifDecoder().packetInProgress());
    REQUIRE_FALSE(system->gs().imageTransfer().active);
  }
}

TEST_CASE("Neko save states are canonical and deterministic")
{
  NekoSystem first;
  NekoSystem second;
  prepareInFlightSystem(&first);
  prepareInFlightSystem(&second);

  const std::vector<std::uint8_t> firstState =
    first.saveState();

  REQUIRE(firstState == first.saveState());
  REQUIRE(firstState == second.saveState());

  std::size_t traceCount = 0;
  first.vu1().setTraceCallback(
    [&traceCount](const VPUTraceEvent &)
    {
      ++traceCount;
    });
  first.gifPathArbiter().setTraceCallback(
    [&traceCount](const GIFTraceEvent &)
    {
      ++traceCount;
    });
  REQUIRE(firstState == first.saveState());

  first.loadState(firstState);
  first.vu1().forceBreak();
  REQUIRE(traceCount == 1);
}

TEST_CASE("Partial GS primitive assembly resumes after save-state restore")
{
  NekoSystem original;
  NekoSystem restored;
  constexpr std::uint64_t FRAME_WIDTH_ONE = UINT64_C(1) << 16;
  constexpr std::uint64_t SCISSOR_8_BY_8 =
    (UINT64_C(7) << 16) | (UINT64_C(7) << 48);
  constexpr std::uint64_t TRIANGLE =
    static_cast<std::uint64_t>(GSPrimitiveType::Triangle);
  const auto vertex =
    [](std::uint16_t x, std::uint16_t y, std::uint32_t z)
    {
      return
        static_cast<std::uint64_t>(x) |
        (static_cast<std::uint64_t>(y) << 16) |
        (static_cast<std::uint64_t>(z) << 32);
    };

  original.gs().writeRegister(
    GSRegisterAddress::FRAME_1,
    FRAME_WIDTH_ONE);
  original.gs().writeRegister(
    GSRegisterAddress::SCISSOR_1,
    SCISSOR_8_BY_8);
  original.gs().writeRegister(
    GSRegisterAddress::PRIM,
    TRIANGLE);
  original.gs().writeRegister(
    GSRegisterAddress::RGBAQ,
    UINT64_C(0x04030201));
  original.gs().writeRegister(
    GSRegisterAddress::XYZ2,
    vertex(16, 16, 1));
  original.gs().writeRegister(
    GSRegisterAddress::RGBAQ,
    UINT64_C(0x08070605));
  original.gs().writeRegister(
    GSRegisterAddress::XYZ2,
    vertex(64, 16, 2));

  REQUIRE(original.gs().queuedVertexCount() == 2);
  restored.loadState(original.saveState());

  for (NekoSystem *system : {&original, &restored})
  {
    system->gs().writeRegister(
      GSRegisterAddress::RGBAQ,
      UINT64_C(0x80402010));
    system->gs().writeRegister(
      GSRegisterAddress::XYZ2,
      vertex(16, 64, 3));
  }

  REQUIRE(original.gs().queuedVertexCount() == 0);
  REQUIRE(restored.gs().queuedVertexCount() == 0);
  REQUIRE(original.gs().triangleCount() == 1);
  REQUIRE(restored.gs().triangleCount() == 1);
  REQUIRE(original.gs().pixelWriteCount() == 6);
  REQUIRE(restored.gs().pixelWriteCount() == 6);
  REQUIRE(
    original.gs().framebufferHash(0, 8, 8) ==
    restored.gs().framebufferHash(0, 8, 8));
  REQUIRE(original.saveState() == restored.saveState());
}

TEST_CASE("Version 25 save-state layout is byte-stable")
{
  NekoSystem system;
  prepareInFlightSystem(&system);
  const std::vector<std::uint8_t> state = system.saveState();
  const std::uint8_t magic[] = {
    'N', 'E', 'K', 'O', 'S', 'T', 'A', 'T'
  };
  REQUIRE(state.size() >= SAVE_STATE_HEADER_SIZE);
  for (std::size_t index = 0;
       index < sizeof(magic);
       ++index)
  {
    REQUIRE(state[index] == magic[index]);
  }
  REQUIRE(state[SAVE_STATE_VERSION_OFFSET] == 25);
  REQUIRE(state[SAVE_STATE_VERSION_OFFSET + 1] == 0);
  REQUIRE(state[SAVE_STATE_VERSION_OFFSET + 2] == 0);
  REQUIRE(state[SAVE_STATE_VERSION_OFFSET + 3] == 0);
  REQUIRE(state.size() == VERSION_25_PREPARED_STATE_SIZE);
  REQUIRE(
    hashBytes(state) ==
    VERSION_25_PREPARED_STATE_HASH);
}

TEST_CASE(
  "VU and GIF trace callbacks do not alter machine continuation")
{
  SECTION("VU compound arithmetic diagnostics")
  {
    NekoSystem unobserved;
    NekoSystem observed;
    bool sawArithmeticWriteback = false;
    observed.vu0().setTraceCallback(
      [&sawArithmeticWriteback](
        const VPUTraceEvent &event)
      {
        sawArithmeticWriteback =
          sawArithmeticWriteback ||
          (event.type == VPUTraceEventType::PipelineWriteback &&
           event.arithmetic.present);
      });
    for (NekoSystem *system : {&unobserved, &observed})
    {
      system->vu0().loadFPRegister(
        VPU_REGISTER_VF01, 1, 2, 3, 4);
      system->vu0().loadFPRegister(
        VPU_REGISTER_VF02, 5, 6, 7, 8);
      system->vu0().loadAccumulator(9, 10, 11, 12);
    }
    std::vector<std::uint8_t> unobservedInstructions;
    std::vector<std::uint8_t> observedInstructions;
    executeSingleUpperInstruction(
      &unobserved.vu0(),
      &unobservedInstructions,
      0,
      VPU_DEST_X_BIT,
      VPU_REGISTER_VF01,
      VPU_REGISTER_VF02,
      VPU_REGISTER_VF03,
      VPU_MADD);
    executeSingleUpperInstruction(
      &observed.vu0(),
      &observedInstructions,
      0,
      VPU_DEST_X_BIT,
      VPU_REGISTER_VF01,
      VPU_REGISTER_VF02,
      VPU_REGISTER_VF03,
      VPU_MADD);

    REQUIRE(sawArithmeticWriteback);
    REQUIRE(observed.saveState() == unobserved.saveState());
  }

  SECTION("Active VU and GIF continuation")
  {
    NekoSystem unobserved;
    NekoSystem observed;
    prepareInFlightSystem(&unobserved);
    prepareInFlightSystem(&observed);
    unobserved.eeBus().write64(EEMemoryMap::GS_BUSDIR, 0);
    observed.eeBus().write64(EEMemoryMap::GS_BUSDIR, 0);
    std::size_t vpuEvents = 0;
    std::size_t gifEvents = 0;
    observed.vu1().setTraceCallback(
      [&vpuEvents](const VPUTraceEvent &)
      {
        ++vpuEvents;
      });
    observed.gifPathArbiter().setTraceCallback(
      [&gifEvents](const GIFTraceEvent &)
      {
        ++gifEvents;
      });

    for (std::size_t cycle = 0; cycle < 32; ++cycle)
    {
      unobserved.clockMasterCycle();
      observed.clockMasterCycle();
      REQUIRE(observed.saveState() == unobserved.saveState());
    }

    REQUIRE(vpuEvents > 0);
    REQUIRE(gifEvents > 0);
    REQUIRE(
      observed.eeCore().stateHash() ==
      unobserved.eeCore().stateHash());
  }
}

TEST_CASE("Save-state loads clear retained callback failures")
{
  NekoSystem system;
  const std::vector<std::uint8_t> state = system.saveState();
  system.vu1().setTraceCallback(
    [](const VPUTraceEvent &)
    {
      throw std::runtime_error("VU observer failed.");
    });
  system.gifPathArbiter().setTraceCallback(
    [](const GIFTraceEvent &event)
    {
      if (event.type == GIFTraceEventType::QuadwordTransferred)
      {
        throw std::runtime_error("GIF observer failed.");
      }
    });

  REQUIRE_NOTHROW(system.vu1().forceBreak());
  REQUIRE_NOTHROW(
    system.gifPathArbiter().transferQuadword(
      GIFPath::Path2,
      gifTag(0, true, GIFDataFormat::Packed, 0)));
  REQUIRE(system.vu1().traceCallbackFailed());
  REQUIRE(system.gifPathArbiter().traceCallbackFailed());

  system.loadState(state);

  REQUIRE_FALSE(system.vu1().traceCallbackFailed());
  REQUIRE_FALSE(system.gifPathArbiter().traceCallbackFailed());
}

TEST_CASE("In-flight EE COP1 memory-source state is canonical")
{
  NekoSystem source;
  prepareInFlightSystem(&source);
  std::vector<std::uint8_t> state = source.saveState();
  writeU64(&state, EE_NEXT_PROGRAM_ORDER_OFFSET, 2);
  state[EE_FIRST_IN_FLIGHT_COP1_ACTIVE_OFFSET] = 1;
  writeU64(&state, EE_FIRST_IN_FLIGHT_COP1_ORDER_OFFSET, 1);
  state[EE_FIRST_IN_FLIGHT_COP1_STAGE_OFFSET] = COP1_STAGE_T;
  writeU32(&state, EE_FIRST_IN_FLIGHT_COP1_ADDRESS_OFFSET, 4);
  writeU32(
    &state,
    EE_FIRST_IN_FLIGHT_COP1_INSTRUCTION_OFFSET,
    UINT32_C(0xc4430000));
  writeU64(
    &state,
    EE_FIRST_IN_FLIGHT_COP1_CAPTURED_GPR_OFFSET,
    0x100);
  writeU32(
    &state,
    EE_FIRST_IN_FLIGHT_COP1_MEMORY_ADDRESS_OFFSET,
    0x100);
  state[EE_FIRST_IN_FLIGHT_COP1_DESTINATION_MASK_OFFSET] = 1;
  state[EE_FIRST_IN_FLIGHT_COP1_DESTINATION_FPR_OFFSET] = 3;
  updateChecksum(&state);

  NekoSystem restored;
  restored.loadState(state);

  REQUIRE(restored.saveState() == state);
  REQUIRE(
    restored.eeCore().stateHash() !=
    source.eeCore().stateHash());
}

TEST_CASE("COP1 Move waits when an older Operate enters T")
{
  std::uint32_t operateInstruction = 0;
  EEOperation expectedBlocker = EEOperation::Nop;
  bool stagedOperate = false;
  SECTION("staged operation")
  {
    operateInstruction =
      cop1SingleInstruction(0x02, 2, 4, 3);
    expectedBlocker = EEOperation::MultiplySingleCOP1;
    stagedOperate = true;
  }
  SECTION("divider operation")
  {
    operateInstruction =
      cop1SingleInstruction(0x03, 2, 4, 3);
    expectedBlocker = EEOperation::DivideSingleCOP1;
  }

  NekoSystem source;
  EECore &sourceCore = source.eeCore();
  sourceCore.setFloatingPointRegister(
    2,
    UINT32_C(0x40000000));
  sourceCore.setFloatingPointRegister(
    3,
    UINT32_C(0x40400000));
  sourceCore.setFloatingPointRegister(
    7,
    UINT32_C(0x89abcdef));
  source.eeBus().write32(
    0,
    operateInstruction);
  sourceCore.startExecution(0);
  source.clockMasterCycle();
  sourceCore.haltExecution();

  std::vector<std::uint8_t> state = source.saveState();
  const std::uint32_t moveInstruction =
    cop1TransferInstruction(0x00, 5, 7);
  writeU64(&state, SIMPLE_EE_NEXT_PROGRAM_ORDER_OFFSET, 3);
  state[SIMPLE_EE_SECOND_IN_FLIGHT_COP1_ACTIVE_OFFSET] = 1;
  writeU64(
    &state,
    SIMPLE_EE_SECOND_IN_FLIGHT_COP1_ORDER_OFFSET,
    2);
  writeU32(
    &state,
    SIMPLE_EE_SECOND_IN_FLIGHT_COP1_ADDRESS_OFFSET,
    4);
  writeU32(
    &state,
    SIMPLE_EE_SECOND_IN_FLIGHT_COP1_INSTRUCTION_OFFSET,
    moveInstruction);
  state[
    SIMPLE_EE_SECOND_IN_FLIGHT_COP1_DESTINATION_MASK_OFFSET] =
      1 << 4;
  state[
    SIMPLE_EE_SECOND_IN_FLIGHT_COP1_DESTINATION_GPR_OFFSET] = 5;
  updateChecksum(&state);

  NekoSystem restored;
  restored.loadState(state);
  restored.eeCore().setProgramCounter(8);
  restored.eeCore().startExecution(8);
  restored.startTrace();

  restored.clockMasterCycle();

  REQUIRE(
    restored.eeCore().generalRegister(5) ==
    EERegister128{});
  std::vector<NekoTraceEvent> stageInterlocks;
  for (const NekoTraceEvent &event : restored.trace())
  {
    if (event.type ==
          NekoTraceEventType::COP1ResourceInterlock &&
        event.value0 == 4)
    {
      stageInterlocks.push_back(event);
    }
  }
  REQUIRE(stageInterlocks.size() == 1);
  REQUIRE(stageInterlocks[0].masterCycle == 2);
  REQUIRE(stageInterlocks[0].value1 == moveInstruction);
  REQUIRE(
    stageInterlocks[0].value2 ==
    static_cast<std::uint8_t>(expectedBlocker));

  if (stagedOperate)
  {
    restored.runMasterCycles(2);
    REQUIRE(
      restored.eeCore().generalRegister(5) ==
      EERegister128{});
    restored.clockMasterCycle();
    REQUIRE(
      restored.eeCore().generalRegister(5) ==
      EERegister128{});
    restored.clockMasterCycle();
    REQUIRE(
      restored.eeCore().generalRegister(5).low ==
      UINT64_C(0xffffffff89abcdef));
  }
}

TEST_CASE("Older CFC1 ignores younger restored FCR31 producers")
{
  NekoSystem source;
  EECore &sourceCore = source.eeCore();
  sourceCore.setCOP1ControlRegister(
    31,
    EECOP1Control::CAUSE_INVALID);
  sourceCore.setGeneralRegister(
    6,
    {EECOP1Control::CONDITION, 0});
  source.eeBus().write32(
    0,
    cop1TransferInstruction(0x02, 5, 31));
  sourceCore.startExecution(0);
  source.clockMasterCycle();
  sourceCore.haltExecution();

  std::vector<std::uint8_t> state = source.saveState();
  writeU64(&state, SIMPLE_EE_NEXT_PROGRAM_ORDER_OFFSET, 3);
  state[SIMPLE_EE_SECOND_IN_FLIGHT_COP1_ACTIVE_OFFSET] = 1;
  writeU64(
    &state,
    SIMPLE_EE_SECOND_IN_FLIGHT_COP1_ORDER_OFFSET,
    2);
  writeU32(
    &state,
    SIMPLE_EE_SECOND_IN_FLIGHT_COP1_ADDRESS_OFFSET,
    4);
  writeU32(
    &state,
    SIMPLE_EE_SECOND_IN_FLIGHT_COP1_INSTRUCTION_OFFSET,
    cop1TransferInstruction(0x06, 6, 31));
  writeU64(
    &state,
    SIMPLE_EE_SECOND_IN_FLIGHT_COP1_CAPTURED_GPR_OFFSET,
    EECOP1Control::CONDITION);
  state[
    SIMPLE_EE_SECOND_IN_FLIGHT_COP1_DESTINATION_MASK_OFFSET] =
      1 << 2;
  updateChecksum(&state);

  NekoSystem restored;
  restored.loadState(state);
  restored.eeCore().setProgramCounter(8);
  restored.eeCore().startExecution(8);
  restored.runMasterCycles(3);

  REQUIRE(
    restored.eeCore().generalRegister(5).low ==
    (EECOP1Control::STATUS_FIXED |
     EECOP1Control::CAUSE_INVALID));
  REQUIRE(
    restored.eeCore().cop1ControlRegister(31) ==
    (EECOP1Control::STATUS_FIXED |
     EECOP1Control::CONDITION));
}

TEST_CASE("Retired COP1 occupancy state is rejected")
{
  NekoSystem source;
  std::vector<std::uint8_t> invalid = source.saveState();
  invalid[
    SIMPLE_EE_RETIRED_COP1_OPERATE_RESOURCE_OFFSET] = 1;
  updateChecksum(&invalid);

  NekoSystem destination;
  const std::vector<std::uint8_t> before =
    destination.saveState();
  REQUIRE_THROWS(destination.loadState(invalid));
  REQUIRE(destination.saveState() == before);
}

TEST_CASE("Active system save states round trip and continue identically")
{
  NekoSystem original;
  prepareInFlightSystem(&original);
  const std::vector<std::uint8_t> state =
    original.saveState();

  NekoSystem restored;
  restored.eeBus().write32(0, UINT32_C(0x24020001));
  restored.eeBus().write32(4, UINT32_C(0x24030002));
  restored.eeCore().startExecution(0);
  restored.clockMasterCycle();
  REQUIRE(
    restored.eeCore().lastIssueSelection().instructionCount ==
    2);
  restored.loadState(state);
  const std::vector<std::uint8_t> restoredState =
    restored.saveState();
  REQUIRE(restoredState == state);
  REQUIRE(restored.saveState() == restoredState);
  REQUIRE(
    restored.eeCore().lastIssueSelection().instructionCount ==
    0);
  REQUIRE(
    restored.eeCore().stateHash() ==
    original.eeCore().stateHash());
  REQUIRE(
    restored.eeCore().generalRegister(1) ==
    EERegister128{
      UINT64_C(0x0123456789abcdef),
      UINT64_C(0xfedcba9876543210)
    });
  REQUIRE(restored.eeCore().programCounter() == 0x80001000);
  REQUIRE(
    restored.eeCore().hi() ==
    UINT64_C(0x1111111122222222));
  REQUIRE(
    restored.eeCore().lo() ==
    UINT64_C(0x3333333344444444));
  REQUIRE(
    restored.eeCore().hi1() ==
    UINT64_C(0x5555555566666666));
  REQUIRE(
    restored.eeCore().lo1() ==
    UINT64_C(0x7777777788888888));
  REQUIRE(restored.eeCore().shiftAmount() == 0x99);
  REQUIRE(
    restored.eeCore().floatingPointRegister(0) ==
    UINT32_C(0x3f800000));
  REQUIRE(
    restored.eeCore().floatingPointRegister(31) ==
    UINT32_C(0x80000000));
  REQUIRE(
    restored.eeCore().floatingPointAccumulator() ==
    UINT32_C(0x40400000));
  REQUIRE(
    restored.eeCore().cop1ControlRegister(0) ==
    EECOP1Control::IMPLEMENTATION_REVISION);
  REQUIRE(
    restored.eeCore().cop1ControlRegister(31) ==
    (EECOP1Control::STATUS_FIXED |
     EECOP1Control::STATUS_WRITABLE_MASK));
  REQUIRE(
    restored.eeCore().cop0Register(EECOP0Register::BadVAddr) ==
    UINT32_C(0x81234567));
  REQUIRE(
    restored.eeCore().cop0Register(EECOP0Register::Count) ==
    UINT32_C(0x12345678));
  REQUIRE(
    restored.eeCore().cop0Register(EECOP0Register::Compare) ==
    UINT32_C(0x87654321));
  REQUIRE(
    restored.eeCore().cop0Register(EECOP0Register::Status) ==
    UINT32_C(0xf0c79c1f));
  REQUIRE(
    restored.eeCore().cop0Register(EECOP0Register::Cause) ==
    UINT32_C(0x80008030));
  REQUIRE(
    restored.eeCore().cop0Register(EECOP0Register::EPC) ==
    UINT32_C(0x80001000));
  REQUIRE(
    restored.eeCore().cop0Register(EECOP0Register::ErrorEPC) ==
    UINT32_C(0xbfc00000));

  original.eeBus().write64(EEMemoryMap::GS_BUSDIR, 0);
  restored.eeBus().write64(EEMemoryMap::GS_BUSDIR, 0);
  original.runMasterCycles(64);
  restored.runMasterCycles(64);

  REQUIRE(
    original.gs().primitive().type ==
    GSPrimitiveType::TriangleFan);
  REQUIRE(original.vu1().getState() == VPU_STATE_READY);
  REQUIRE(original.vif0().payloadWordsRemaining() == 2);
  REQUIRE(original.saveState() == restored.saveState());
}

TEST_CASE("Accepted EE younger A-stage work survives save states")
{
  NekoSystem original;
  EECore &core = original.eeCore();
  original.eeBus().write32(
    0,
    UINT32_C(0x70000000) |
      (UINT32_C(1) << 21) |
      (UINT32_C(2) << 16) |
      (UINT32_C(3) << 11) |
      (UINT32_C(0x12) << 6) |
      UINT32_C(0x09));
  original.eeBus().write32(4, UINT32_C(0x24040001));
  core.setGeneralRegister(
    1,
    {UINT64_MAX, UINT64_C(0xffff0000ffff0000)});
  core.setGeneralRegister(
    2,
    {UINT64_C(0x00ff00ff00ff00ff),
     UINT64_C(0x00ff00ff00ff00ff)});
  core.startExecution(0);
  original.clockMasterCycle();

  REQUIRE(core.acceptanceRecordsThisCycle().size() == 2);
  REQUIRE(core.generalRegister(4).low == 0);

  const std::vector<std::uint8_t> state =
    original.saveState();
  NekoSystem restored;
  restored.loadState(state);
  requireStateBytesEqual(restored.saveState(), state);
  REQUIRE(
    restored.eeCore().stateHash() ==
    original.eeCore().stateHash());

  original.clockMasterCycle();
  restored.clockMasterCycle();

  REQUIRE(restored.eeCore().generalRegister(4).low == 1);
  requireStateBytesEqual(
    restored.saveState(),
    original.saveState());
  REQUIRE(
    original.eeCore().stateHash() ==
    restored.eeCore().stateHash());
}

TEST_CASE("Malformed EE younger A-stage state is rejected")
{
  SECTION("Inactive continuation cannot contain an instruction")
  {
    NekoSystem source;
    std::vector<std::uint8_t> invalid = source.saveState();
    invalid[SIMPLE_EE_YOUNGER_A_STAGE_INSTRUCTION_OFFSET] = 1;
    updateChecksum(&invalid);

    NekoSystem destination;
    const std::vector<std::uint8_t> before =
      destination.saveState();
    REQUIRE(
      invalid[SIMPLE_EE_YOUNGER_A_STAGE_ACTIVE_OFFSET] ==
      0);
    REQUIRE_THROWS(destination.loadState(invalid));
    REQUIRE(destination.saveState() == before);
  }

  SECTION("Continuation cannot overlap active MAC state")
  {
    NekoSystem source;
    EECore &core = source.eeCore();
    source.eeBus().write32(
      0,
      UINT32_C(0x70000000) |
        (UINT32_C(1) << 21) |
        (UINT32_C(2) << 16) |
        (UINT32_C(3) << 11) |
        (UINT32_C(0x12) << 6) |
        UINT32_C(0x09));
    source.eeBus().write32(4, UINT32_C(0x24040001));
    core.setGeneralRegister(1, {UINT64_MAX, UINT64_MAX});
    core.setGeneralRegister(2, {UINT64_MAX, UINT64_MAX});
    core.startExecution(0);
    source.clockMasterCycle();

    std::vector<std::uint8_t> invalid = source.saveState();
    invalid[SIMPLE_EE_PENDING_MAC1_ACTIVE_OFFSET] = 1;
    invalid[SIMPLE_EE_PENDING_MAC1_REMAINING_CYCLES_OFFSET] = 4;
    updateChecksum(&invalid);

    NekoSystem destination;
    const std::vector<std::uint8_t> before =
      destination.saveState();
    REQUIRE_THROWS(destination.loadState(invalid));
    REQUIRE(destination.saveState() == before);
  }
}

TEST_CASE("Suspended PATH3 and PATH1 progress survive save states")
{
  NekoSystem original;
  prepareSuspendedPath3(&original);
  const std::vector<std::uint8_t> state =
    original.saveState();

  NekoSystem restored;
  restored.loadState(state);
  REQUIRE(restored.saveState() == state);

  finishSuspendedPath3(&original);
  finishSuspendedPath3(&restored);

  REQUIRE(
    original.gifPath1().transferredQuadwordCount() == 1);
  REQUIRE(
    original.gifPath3().transferredQuadwordCount() == 11);
  REQUIRE(original.saveState() == restored.saveState());
}

TEST_CASE("Stalled GIF DMA state resumes identically after load")
{
  NekoSystem original;
  const GIFQuadword tag = gifTag(
    1,
    true,
    GIFDataFormat::Packed,
    GIFRegisterDescriptor::AD);
  const GIFQuadword payload = adWrite(
    GSRegisterAddress::PRIM,
    static_cast<std::uint8_t>(GSPrimitiveType::Sprite));
  REQUIRE(original.eeBus().writeQuadword(0x3000, tag));
  REQUIRE(original.eeBus().writeQuadword(0x3010, payload));
  original.eeBus().write32(
    EEMemoryMap::D_CTRL,
    DMACControl::DMA_ENABLE);
  original.eeBus().write32(EEMemoryMap::D2_MADR, 0x3000);
  original.eeBus().write32(EEMemoryMap::D2_QWC, 2);
  original.eeBus().write32(
    EEMemoryMap::D2_CHCR,
    GIFDMACChannelControl::START);
  original.eeBus().write32(
    EEMemoryMap::GIF_MODE,
    GIFMode::M3R);
  original.clockMasterCycle();
  REQUIRE(original.gifDMAC().stalledByPATH3());

  NekoSystem restored;
  restored.loadState(original.saveState());
  REQUIRE(restored.gifDMAC().stalledByPATH3());

  original.eeBus().write32(EEMemoryMap::GIF_MODE, 0);
  restored.eeBus().write32(EEMemoryMap::GIF_MODE, 0);
  original.runMasterCycles(2);
  restored.runMasterCycles(2);

  REQUIRE(
    original.gs().primitive().type ==
    GSPrimitiveType::Sprite);
  REQUIRE(original.saveState() == restored.saveState());
}

TEST_CASE("Queued guest FIFO data survives save states")
{
  NekoSystem original;
  const EEQuadword interruptedNops = {
    UINT64_C(0x0000000080000000),
    0
  };
  const EEQuadword gifTag = {
    UINT64_C(1) << 15,
    0
  };

  REQUIRE(
    original.eeBus().writeGuestData128(
      EEMemoryMap::VIF0_FIFO,
      interruptedNops) ==
    EEDataWriteResult::Completed);
  original.eeBus().advanceGuestFIFOs();
  REQUIRE(
    original.eeBus().writeGuestData128(
      EEMemoryMap::VIF0_FIFO,
      {}) ==
    EEDataWriteResult::Completed);
  original.eeBus().write32(
    EEMemoryMap::GIF_MODE,
    GIFMode::M3R);
  REQUIRE(
    original.eeBus().writeGuestData128(
      EEMemoryMap::GIF_FIFO,
      gifTag) ==
    EEDataWriteResult::Completed);

  NekoSystem restored;
  restored.loadState(original.saveState());

  REQUIRE(restored.vif0().fifoQuadwordCount() == 2);
  REQUIRE(restored.gifPath3().guestFIFOQuadwordCount() == 1);
  REQUIRE(restored.saveState() == original.saveState());

  original.eeBus().write32(EEMemoryMap::VIF0_FBRST, 1u << 3);
  restored.eeBus().write32(EEMemoryMap::VIF0_FBRST, 1u << 3);
  original.eeBus().write32(EEMemoryMap::GIF_MODE, 0);
  restored.eeBus().write32(EEMemoryMap::GIF_MODE, 0);
  original.clockMasterCycle();
  restored.clockMasterCycle();
  REQUIRE(restored.saveState() == original.saveState());
}

TEST_CASE("In-flight VIF1 DMA resumes identically after load")
{
  NekoSystem original;
  REQUIRE(original.eeBus().writeQuadword(0x1000, {}));
  REQUIRE(original.eeBus().writeQuadword(0x1010, {}));
  original.eeBus().write32(
    EEMemoryMap::D_CTRL,
    DMACControl::DMA_ENABLE);
  original.eeBus().write32(EEMemoryMap::D1_MADR, 0x1000);
  original.eeBus().write32(EEMemoryMap::D1_QWC, 2);
  original.eeBus().write32(
    EEMemoryMap::D1_CHCR,
    GIFDMACChannelControl::FROM_MEMORY |
    GIFDMACChannelControl::START);
  original.clockMasterCycle();

  NekoSystem restored;
  restored.loadState(original.saveState());
  REQUIRE(restored.vif1DMAC().memoryAddress() == 0x1010);
  REQUIRE(restored.vif1DMAC().quadwordCount() == 1);
  REQUIRE(restored.vif1().fifoQuadwordCount() == 1);

  original.runMasterCycles(2);
  restored.runMasterCycles(2);
  REQUIRE(restored.saveState() == original.saveState());
  REQUIRE(restored.vif1().wordsIngested() == 8);
  REQUIRE(
    (restored.dmacController().status() &
     DMACStatus::CHANNEL_1) != 0);
}

TEST_CASE("Reset machines can load prior save states")
{
  NekoSystem system;
  prepareInFlightSystem(&system);
  const std::vector<std::uint8_t> state =
    system.saveState();

  system.reset();
  REQUIRE(system.saveState() != state);

  system.loadState(state);
  REQUIRE(system.saveState() == state);
}

TEST_CASE("EE fetch exceptions survive save states")
{
  NekoSystem original;
  original.eeCore().setProgramCounter(0x80000102);
  REQUIRE_FALSE(original.eeCore().fetchInstruction().succeeded);

  NekoSystem restored;
  restored.loadState(original.saveState());

  REQUIRE(restored.eeCore().exceptionPending());
  REQUIRE(
    restored.eeCore().pendingException() ==
    EEException::AddressErrorLoadOrFetch);
  REQUIRE(restored.eeCore().exceptionAddress() == 0x80000102);
  REQUIRE(restored.eeCore().programCounter() == 0x80000102);

  restored.eeCore().clearPendingException();
  restored.eeCore().setProgramCounter(0);
  restored.eeBus().write32(0, UINT32_C(0x12345678));
  REQUIRE(restored.eeCore().fetchInstruction().succeeded);
}

TEST_CASE("Running EE scheduler state survives save states")
{
  NekoSystem original;
  original.eeBus().write32(0, 0);
  original.eeBus().write32(4, 0);
  original.eeBus().write32(8, 0);
  original.eeCore().startExecution(0);
  original.clockMasterCycle();

  NekoSystem restored;
  restored.loadState(original.saveState());

  REQUIRE(restored.eeCore().clockActive());
  REQUIRE(restored.eeCore().elapsedCycles() == 1);
  REQUIRE(restored.eeCore().programCounter() == 4);
  REQUIRE(restored.eeCore().hasLastInstruction());
  REQUIRE(restored.eeCore().lastInstructionAddress() == 0);

  original.runMasterCycles(2);
  restored.runMasterCycles(2);

  REQUIRE(original.saveState() == restored.saveState());
}

TEST_CASE("EE integer execution exceptions survive save states")
{
  NekoSystem original;
  original.eeCore().setGeneralRegister(1, {0x7fffffff, 0});
  original.eeCore().setGeneralRegister(2, {1, 0});
  original.eeCore().setGeneralRegister(3, {0x1234, 0x5678});
  original.eeBus().write32(
    0,
    (UINT32_C(1) << 21) |
    (UINT32_C(2) << 16) |
    (UINT32_C(3) << 11) |
    0x20);
  original.eeCore().startExecution(0);
  original.clockMasterCycle();

  NekoSystem restored;
  restored.loadState(original.saveState());

  REQUIRE(restored.eeCore().clockActive());
  REQUIRE(restored.eeCore().stopReason() == EEStopReason::None);
  REQUIRE(
    restored.eeCore().pendingException() ==
    EEException::ArithmeticOverflow);
  REQUIRE(
    (restored.eeCore().cop0Register(EECOP0Register::Status) &
      EECOP0Status::EXCEPTION_LEVEL) != 0);
  REQUIRE(
    (restored.eeCore().cop0Register(EECOP0Register::Cause) &
      EECOP0Cause::EXCEPTION_CODE_MASK) ==
    (static_cast<std::uint32_t>(
      EEExceptionCode::ARITHMETIC_OVERFLOW) << 2));
  REQUIRE(restored.eeCore().cop0Register(EECOP0Register::EPC) == 0);
  REQUIRE(restored.eeCore().rejectedInstruction() != 0);
  REQUIRE(
    restored.eeCore().generalRegister(3) ==
    EERegister128{0x1234, 0x5678});
  REQUIRE(
    restored.eeCore().programCounter() ==
    EEExceptionVector::BOOTSTRAP_GENERAL);
  REQUIRE(original.saveState() == restored.saveState());
}

TEST_CASE("In-flight EE multiply latency survives save states")
{
  NekoSystem original;
  original.eeCore().setGeneralRegister(1, {6, 0});
  original.eeCore().setGeneralRegister(2, {7, 0});
  original.eeBus().write32(
    0,
    (UINT32_C(1) << 21) |
    (UINT32_C(2) << 16) |
    (UINT32_C(3) << 11) |
    0x18);
  original.eeCore().startExecution(0);
  original.runMasterCycles(2);

  NekoSystem restored;
  restored.loadState(original.saveState());

  REQUIRE(restored.eeCore().programCounter() == 4);
  REQUIRE(
    restored.eeCore().stateHash() ==
    original.eeCore().stateHash());
  REQUIRE(restored.eeCore().lo() == 0);
  REQUIRE(restored.eeCore().generalRegister(3).low == 0);

  original.runMasterCycles(2);
  restored.runMasterCycles(2);
  REQUIRE(original.saveState() == restored.saveState());
  REQUIRE(
    restored.eeCore().stateHash() ==
    original.eeCore().stateHash());
  REQUIRE(restored.eeCore().lo() == 0);

  original.clockMasterCycle();
  restored.clockMasterCycle();
  REQUIRE(original.saveState() == restored.saveState());
  REQUIRE(restored.eeCore().lo() == 42);
  REQUIRE(restored.eeCore().generalRegister(3).low == 42);
}

TEST_CASE("Pending EE branch delay slots survive save states")
{
  NekoSystem original;
  original.eeBus().write32(
    0,
    UINT32_C(0x0c000003));
  original.eeBus().write32(
    4,
    (UINT32_C(0x19) << 26) |
    (UINT32_C(31) << 21) |
    (UINT32_C(3) << 16) |
    0);
  original.eeBus().write32(
    12,
    (UINT32_C(0x0d) << 26) |
    (UINT32_C(4) << 16) |
    2);
  original.eeCore().startExecution(0);
  original.clockMasterCycle();

  NekoSystem restored;
  restored.loadState(original.saveState());

  REQUIRE(restored.eeCore().programCounter() == 4);
  REQUIRE(restored.eeCore().generalRegister(3).low == 0);

  original.clockMasterCycle();
  restored.clockMasterCycle();
  REQUIRE(original.saveState() == restored.saveState());
  REQUIRE(restored.eeCore().programCounter() == 12);
  REQUIRE(restored.eeCore().generalRegister(3).low == 8);

  original.clockMasterCycle();
  restored.clockMasterCycle();
  REQUIRE(original.saveState() == restored.saveState());
  REQUIRE(restored.eeCore().generalRegister(4).low == 2);
}

TEST_CASE("Dual EE MAC pipelines survive halt and save-state resume")
{
  NekoSystem original;
  EECore &originalCore = original.eeCore();
  originalCore.setGeneralRegister(1, {3, 0});
  originalCore.setGeneralRegister(2, {4, 0});
  originalCore.setGeneralRegister(4, {5, 0});
  originalCore.setGeneralRegister(5, {6, 0});
  original.eeBus().write32(0, UINT32_C(0x00221818));
  original.eeBus().write32(4, UINT32_C(0x70853018));
  originalCore.startExecution(0);

  original.clockMasterCycle();

  REQUIRE(
    originalCore.acceptanceRecordsThisCycle().size() ==
    2);
  REQUIRE(originalCore.programCounter() == 8);
  originalCore.haltExecution();
  const std::vector<std::uint8_t> state =
    original.saveState();

  NekoSystem restored;
  restored.loadState(state);

  REQUIRE(restored.saveState() == state);
  REQUIRE(
    restored.eeCore().acceptanceRecordsThisCycle().size() ==
    0);
  REQUIRE(
    restored.eeCore().lastIssueSelection().instructionCount ==
    0);
  REQUIRE(
    restored.eeCore().executionState() ==
    EEExecutionState::Halted);

  originalCore.startExecution(8);
  restored.eeCore().startExecution(8);
  original.runMasterCycles(4);
  restored.runMasterCycles(4);

  REQUIRE(original.saveState() == restored.saveState());
  REQUIRE(
    originalCore.stateHash() ==
    restored.eeCore().stateHash());
  REQUIRE(restored.eeCore().generalRegister(3).low == 12);
  REQUIRE(restored.eeCore().generalRegister(6).low == 30);
  REQUIRE(restored.eeCore().lo() == 12);
  REQUIRE(restored.eeCore().lo1() == 30);
}

TEST_CASE("Unreachable concurrent EE MAC save states are rejected")
{
  NekoSystem source;
  EECore &core = source.eeCore();
  core.setGeneralRegister(1, {3, 0});
  core.setGeneralRegister(2, {4, 0});
  core.setGeneralRegister(4, {5, 0});
  core.setGeneralRegister(5, {6, 0});
  source.eeBus().write32(0, UINT32_C(0x00221818));
  source.eeBus().write32(4, UINT32_C(0x70853018));
  core.startExecution(0);
  source.clockMasterCycle();
  const std::vector<std::uint8_t> valid =
    source.saveState();

  REQUIRE(
    valid[SIMPLE_EE_PENDING_MAC0_REMAINING_CYCLES_OFFSET] ==
    4);
  REQUIRE(
    valid[SIMPLE_EE_PENDING_MAC1_REMAINING_CYCLES_OFFSET] ==
    4);
  REQUIRE(
    valid[SIMPLE_EE_PENDING_MAC0_GENERAL_REGISTER_OFFSET] ==
    3);
  REQUIRE(
    valid[SIMPLE_EE_PENDING_MAC1_GENERAL_REGISTER_OFFSET] ==
    6);

  SECTION("latencies must describe one co-issued pair")
  {
    std::vector<std::uint8_t> invalid = valid;
    invalid[
      SIMPLE_EE_PENDING_MAC1_REMAINING_CYCLES_OFFSET] = 3;
    updateChecksum(&invalid);
    NekoSystem destination;
    REQUIRE_THROWS(destination.loadState(invalid));
  }

  SECTION("multiply destinations must remain independent")
  {
    std::vector<std::uint8_t> invalid = valid;
    invalid[
      SIMPLE_EE_PENDING_MAC1_GENERAL_REGISTER_OFFSET] = 3;
    updateChecksum(&invalid);
    NekoSystem destination;
    REQUIRE_THROWS(destination.loadState(invalid));
  }

  SECTION("terminal halts cannot retain both pipelines")
  {
    std::vector<std::uint8_t> invalid = valid;
    invalid[SIMPLE_EE_EXECUTION_STATE_OFFSET] =
      static_cast<std::uint8_t>(EEExecutionState::Halted);
    invalid[SIMPLE_EE_STOP_REASON_OFFSET] =
      static_cast<std::uint8_t>(
        EEStopReason::FetchException);
    updateChecksum(&invalid);
    NekoSystem destination;
    REQUIRE_THROWS(destination.loadState(invalid));
  }
}

TEST_CASE("Concurrent multiply and divide save states are accepted")
{
  NekoSystem source;
  EECore &core = source.eeCore();
  core.setGeneralRegister(1, {12, 0});
  core.setGeneralRegister(2, {3, 0});
  core.setGeneralRegister(4, {20, 0});
  core.setGeneralRegister(5, {4, 0});

  SECTION("MAC0 multiply and MAC1 divide")
  {
    source.eeBus().write32(0, UINT32_C(0x00221818));
    source.eeBus().write32(4, UINT32_C(0x7085001a));
  }

  SECTION("MAC0 divide and MAC1 multiply")
  {
    source.eeBus().write32(0, UINT32_C(0x0022001a));
    source.eeBus().write32(4, UINT32_C(0x70853018));
  }

  core.startExecution(0);
  source.clockMasterCycle();
  REQUIRE(core.acceptanceRecordsThisCycle().size() == 2);

  const std::vector<std::uint8_t> state =
    source.saveState();
  NekoSystem restored;
  REQUIRE_NOTHROW(restored.loadState(state));
  REQUIRE(restored.saveState() == state);
}

TEST_CASE("Blocked dual EE front ends survive save-state restore")
{
  NekoSystem original;
  EECore &originalCore = original.eeCore();
  originalCore.setCOP0Register(
    EECOP0Register::Status,
    EECOP0Status::COP1_USABLE);
  originalCore.setFloatingPointRegister(
    1,
    UINT32_C(0x3f800000));
  originalCore.setFloatingPointRegister(
    2,
    UINT32_C(0x40000000));
  original.eeBus().write32(0, UINT32_C(0x460208c0));
  original.eeBus().write32(4, UINT32_C(0x24040001));
  original.eeBus().write32(8, UINT32_C(0x44051800));
  original.eeBus().write32(12, UINT32_C(0x24060002));
  original.eeBus().write32(16, UINT32_C(0x0000000c));
  originalCore.startExecution(0);

  original.clockMasterCycle();
  REQUIRE(
    originalCore.acceptanceRecordsThisCycle().size() ==
    2);
  original.clockMasterCycle();
  REQUIRE(
    originalCore.acceptanceRecordsThisCycle().size() ==
    0);
  REQUIRE(originalCore.programCounter() == 8);

  const std::vector<std::uint8_t> state =
    original.saveState();
  NekoSystem restored;
  restored.loadState(state);

  REQUIRE(restored.saveState() == state);
  REQUIRE(
    originalCore.stateHash() ==
    restored.eeCore().stateHash());
  REQUIRE(
    restored.eeCore().acceptanceRecordsThisCycle().size() ==
    0);

  const EEExecutionResult originalResult =
    original.runEE(32);
  const EEExecutionResult restoredResult =
    restored.runEE(32);

  REQUIRE(
    originalResult.instructions ==
    restoredResult.instructions);
  REQUIRE(
    originalResult.masterCycles ==
    restoredResult.masterCycles);
  REQUIRE(original.saveState() == restored.saveState());
  REQUIRE(
    restored.eeCore().generalRegister(5).low ==
    UINT32_C(0x40400000));
  REQUIRE(restored.eeCore().generalRegister(6).low == 2);
}

TEST_CASE("EE byte data faults survive save states")
{
  NekoSystem original;
  original.eeCore().setGeneralRegister(
    1,
    {EEMemoryMap::MAIN_MEMORY_SIZE, 0});
  original.eeCore().setGeneralRegister(2, {0x1234, 0x5678});
  original.eeBus().write32(
    0,
    (UINT32_C(0x20) << 26) |
    (UINT32_C(1) << 21) |
    (UINT32_C(2) << 16));
  original.eeCore().startExecution(0);
  original.clockMasterCycle();

  NekoSystem restored;
  restored.loadState(original.saveState());

  REQUIRE(
    restored.eeCore().pendingException() ==
    EEException::DataBusErrorLoad);
  REQUIRE(
    restored.eeCore().exceptionAddress() ==
    EEMemoryMap::MAIN_MEMORY_SIZE);
  REQUIRE(
    restored.eeCore().generalRegister(2) ==
    EERegister128{0x1234, 0x5678});
  REQUIRE(original.saveState() == restored.saveState());
}

TEST_CASE("EE halfword address faults survive save states")
{
  NekoSystem original;
  original.eeCore().setGeneralRegister(1, {0x101, 0});
  original.eeCore().setGeneralRegister(2, {0xabcd, 0});
  original.eeBus().write32(
    0,
    (UINT32_C(0x29) << 26) |
    (UINT32_C(1) << 21) |
    (UINT32_C(2) << 16));
  original.eeCore().startExecution(0);
  original.clockMasterCycle();

  NekoSystem restored;
  restored.loadState(original.saveState());

  REQUIRE(
    restored.eeCore().pendingException() ==
    EEException::AddressErrorStore);
  REQUIRE(restored.eeCore().exceptionAddress() == 0x101);
  REQUIRE(
    restored.eeCore().programCounter() ==
    EEExceptionVector::BOOTSTRAP_GENERAL);
  REQUIRE(original.saveState() == restored.saveState());
}

TEST_CASE("EE aligned word bus faults survive save states")
{
  NekoSystem original;
  original.eeCore().setGeneralRegister(
    1,
    {EEMemoryMap::MAIN_MEMORY_SIZE, 0});
  original.eeBus().write32(
    0,
    (UINT32_C(0x27) << 26) |
    (UINT32_C(1) << 21) |
    (UINT32_C(2) << 16));
  original.eeCore().startExecution(0);
  original.clockMasterCycle();

  NekoSystem restored;
  restored.loadState(original.saveState());

  REQUIRE(
    restored.eeCore().pendingException() ==
    EEException::DataBusErrorLoad);
  REQUIRE(
    restored.eeCore().exceptionAddress() ==
    EEMemoryMap::MAIN_MEMORY_SIZE);
  REQUIRE(
    restored.eeCore().programCounter() ==
    EEExceptionVector::BOOTSTRAP_GENERAL);
  REQUIRE(original.saveState() == restored.saveState());
}

TEST_CASE("EE paired word merge continuation survives save states")
{
  NekoSystem original;
  original.eeCore().setGeneralRegister(1, {0x101, 0});
  original.eeCore().setGeneralRegister(
    2,
    {UINT64_C(0x5566778899aabbcc), UINT64_MAX});
  original.eeBus().writeData32(
    0x100,
    UINT32_C(0x33221100));
  original.eeBus().writeData32(
    0x104,
    UINT32_C(0x88776684));
  original.eeBus().write32(
    0,
    (UINT32_C(0x22) << 26) |
    (UINT32_C(1) << 21) |
    (UINT32_C(2) << 16) |
    3);
  original.eeBus().write32(
    4,
    (UINT32_C(0x26) << 26) |
    (UINT32_C(1) << 21) |
    (UINT32_C(2) << 16));
  original.eeCore().startExecution(0);
  original.clockMasterCycle();

  NekoSystem restored;
  restored.loadState(original.saveState());
  original.clockMasterCycle();
  restored.clockMasterCycle();

  REQUIRE(original.saveState() == restored.saveState());
  REQUIRE(
    restored.eeCore().generalRegister(2).low ==
    UINT64_C(0xffffffff84332211));
  REQUIRE(restored.eeCore().generalRegister(2).high == UINT64_MAX);
}

TEST_CASE("EE aligned doubleword continuation survives save states")
{
  NekoSystem original;
  original.eeCore().setGeneralRegister(1, {0x100, 0});
  original.eeCore().setGeneralRegister(
    2,
    {UINT64_C(0x0123456789abcdef), UINT64_MAX});
  original.eeBus().write32(
    0,
    (UINT32_C(0x3f) << 26) |
    (UINT32_C(1) << 21) |
    (UINT32_C(2) << 16));
  original.eeBus().write32(
    4,
    (UINT32_C(0x37) << 26) |
    (UINT32_C(1) << 21) |
    (UINT32_C(3) << 16));
  original.eeCore().startExecution(0);
  original.clockMasterCycle();

  NekoSystem restored;
  restored.loadState(original.saveState());
  original.clockMasterCycle();
  restored.clockMasterCycle();

  REQUIRE(original.saveState() == restored.saveState());
  REQUIRE(
    restored.eeCore().generalRegister(3).low ==
    UINT64_C(0x0123456789abcdef));
}

TEST_CASE("EE paired doubleword merge continuation survives save states")
{
  NekoSystem original;
  original.eeCore().setGeneralRegister(1, {0x101, 0});
  original.eeCore().setGeneralRegister(
    2,
    {UINT64_C(0x1122334455667788), UINT64_MAX});
  original.eeBus().writeData64(
    0x100,
    UINT64_C(0x7060504030201000));
  original.eeBus().writeData64(
    0x108,
    UINT64_C(0xf0e0d0c0b0a09080));
  original.eeBus().write32(
    0,
    (UINT32_C(0x1a) << 26) |
    (UINT32_C(1) << 21) |
    (UINT32_C(2) << 16) |
    7);
  original.eeBus().write32(
    4,
    (UINT32_C(0x1b) << 26) |
    (UINT32_C(1) << 21) |
    (UINT32_C(2) << 16));
  original.eeCore().startExecution(0);
  original.clockMasterCycle();

  NekoSystem restored;
  restored.loadState(original.saveState());
  original.clockMasterCycle();
  restored.clockMasterCycle();

  REQUIRE(original.saveState() == restored.saveState());
  REQUIRE(
    restored.eeCore().generalRegister(2).low ==
    UINT64_C(0x8070605040302010));
  REQUIRE(restored.eeCore().generalRegister(2).high == UINT64_MAX);
}

TEST_CASE("EE quadword continuation survives save states")
{
  NekoSystem original;
  original.eeCore().setGeneralRegister(1, {0x100, 0});
  original.eeCore().setGeneralRegister(
    2,
    {
      UINT64_C(0x0123456789abcdef),
      UINT64_C(0xfedcba9876543210)
    });
  original.eeBus().write32(
    0,
    (UINT32_C(0x1f) << 26) |
    (UINT32_C(1) << 21) |
    (UINT32_C(2) << 16));
  original.eeBus().write32(
    4,
    (UINT32_C(0x1e) << 26) |
    (UINT32_C(1) << 21) |
    (UINT32_C(3) << 16));
  original.eeCore().startExecution(0);
  original.clockMasterCycle();

  NekoSystem restored;
  restored.loadState(original.saveState());
  original.clockMasterCycle();
  restored.clockMasterCycle();

  REQUIRE(original.saveState() == restored.saveState());
  REQUIRE((
    restored.eeCore().generalRegister(3) ==
    EERegister128{
      UINT64_C(0x0123456789abcdef),
      UINT64_C(0xfedcba9876543210)
    }));
}

TEST_CASE("Invalid save states are rejected transactionally")
{
  NekoSystem system;
  prepareInFlightSystem(&system);
  const std::vector<std::uint8_t> before =
    system.saveState();

  std::vector<std::uint8_t> invalid = before;
  invalid[0] ^= 0xff;
  REQUIRE_THROWS(system.loadState(invalid));
  REQUIRE(system.saveState() == before);

  invalid = before;
  invalid[8] = 13;
  REQUIRE_THROWS(system.loadState(invalid));
  REQUIRE(system.saveState() == before);

  invalid = before;
  invalid[12] ^= 1;
  REQUIRE_THROWS(system.loadState(invalid));
  REQUIRE(system.saveState() == before);

  invalid = before;
  invalid.resize(invalid.size() - 1);
  REQUIRE_THROWS(system.loadState(invalid));
  REQUIRE(system.saveState() == before);

  invalid = before;
  invalid.push_back(0);
  REQUIRE_THROWS(system.loadState(invalid));
  REQUIRE(system.saveState() == before);

  invalid = before;
  invalid[46] = 0xff;
  updateChecksum(&invalid);
  REQUIRE_THROWS(system.loadState(invalid));
  REQUIRE(system.saveState() == before);

  invalid = before;
  for (std::size_t index = 0;
       index < MASTER_CLOCK_COMPONENT_SIZE;
       ++index)
  {
    const std::size_t first =
      MASTER_CLOCK_FIRST_COMPONENT_OFFSET + index;
    const std::size_t second =
      first + MASTER_CLOCK_COMPONENT_SIZE;
    const std::uint8_t value = invalid[first];
    invalid[first] = invalid[second];
    invalid[second] = value;
  }
  updateChecksum(&invalid);
  REQUIRE_THROWS(system.loadState(invalid));
  REQUIRE(system.saveState() == before);

  invalid = before;
  invalid[PREPARED_EE_GPR_ZERO_HIGH_OFFSET] ^= 1;
  updateChecksum(&invalid);
  REQUIRE_THROWS(system.loadState(invalid));
  REQUIRE(system.saveState() == before);

  invalid = before;
  invalid[PREPARED_EE_FCR31_OFFSET] |= 1;
  updateChecksum(&invalid);
  REQUIRE_THROWS(system.loadState(invalid));
  REQUIRE(system.saveState() == before);

  invalid = before;
  invalid[EE_COP1_DIVIDER_INITIATION_OFFSET] = 1;
  invalid[EE_COP1_DIVIDER_OPERATION_OFFSET] =
    static_cast<std::uint8_t>(
      EEOperation::DivideSingleCOP1);
  updateChecksum(&invalid);
  REQUIRE_THROWS(system.loadState(invalid));
  REQUIRE(system.saveState() == before);

  invalid = before;
  invalid[PREPARED_EE_BRANCH_DELAY_LIKELY_OFFSET] = 1;
  updateChecksum(&invalid);
  REQUIRE_THROWS(system.loadState(invalid));
  REQUIRE(system.saveState() == before);

  invalid = before;
  invalid[EE_COP1_POST_DELAY_COUNT_OFFSET] = 3;
  updateChecksum(&invalid);
  REQUIRE_THROWS(system.loadState(invalid));
  REQUIRE(system.saveState() == before);

  invalid = before;
  invalid[EE_ISSUE_LATCH_ADDRESS_OFFSET] = 4;
  updateChecksum(&invalid);
  REQUIRE_THROWS(system.loadState(invalid));
  REQUIRE(system.saveState() == before);

  invalid = before;
  invalid[EE_FIRST_IN_FLIGHT_COP1_ADDRESS_OFFSET] = 4;
  updateChecksum(&invalid);
  REQUIRE_THROWS(system.loadState(invalid));
  REQUIRE(system.saveState() == before);

  invalid = before;
  invalid[EE_COP1_POST_DELAY_ADDRESS_OFFSET] = 4;
  updateChecksum(&invalid);
  REQUIRE_THROWS(system.loadState(invalid));
  REQUIRE(system.saveState() == before);

  invalid = before;
  invalid[EE_COP1_POST_DELAY_TARGET_OFFSET] = 4;
  updateChecksum(&invalid);
  REQUIRE_THROWS(system.loadState(invalid));
  REQUIRE(system.saveState() == before);

  invalid = before;
  invalid[EE_COP1_POST_DELAY_COUNT_OFFSET] = 2;
  invalid[EE_COP1_POST_DELAY_TARGET_OFFSET] = 4;
  invalid[EE_COP1_POST_DELAY_TAKEN_OFFSET] = 1;
  invalid[EE_COP1_POST_TARGET_COUNT_OFFSET] = 1;
  invalid[EE_COP1_POST_TARGET_ADDRESS_OFFSET] = 4;
  updateChecksum(&invalid);
  REQUIRE_THROWS(system.loadState(invalid));
  REQUIRE(system.saveState() == before);

  invalid = before;
  invalid[EE_COP1_POST_TARGET_COUNT_OFFSET] = 1;
  updateChecksum(&invalid);
  REQUIRE_THROWS(system.loadState(invalid));
  REQUIRE(system.saveState() == before);

  invalid = before;
  invalid[PREPARED_MAIN_MEMORY_SIZE_OFFSET] ^= 1;
  updateChecksum(&invalid);
  REQUIRE_THROWS(system.loadState(invalid));
  REQUIRE(system.saveState() == before);

  invalid = before;
  invalid.back() = 2;
  updateChecksum(&invalid);
  REQUIRE_THROWS(system.loadState(invalid));
  REQUIRE(system.saveState() == before);
}

TEST_CASE("In-flight COP1 load save states are internally consistent")
{
  NekoSystem source;
  source.eeCore().setGeneralRegister(1, {0x100, 0});
  REQUIRE(
    source.eeBus().writeData32(
      0x100,
      UINT32_C(0x89abcdef)));
  source.eeBus().write32(
    0,
    (UINT32_C(0x31) << 26) |
      (UINT32_C(1) << 21) |
      (UINT32_C(3) << 16));
  source.eeCore().startExecution(0);
  source.clockMasterCycle();

  NekoSystem destination;
  const std::vector<std::uint8_t> before =
    destination.saveState();

  SECTION("The destination matches the instruction")
  {
    std::vector<std::uint8_t> invalid = source.saveState();
    invalid[
      SIMPLE_EE_FIRST_IN_FLIGHT_COP1_DESTINATION_FPR_OFFSET] = 4;
    updateChecksum(&invalid);

    REQUIRE_THROWS(destination.loadState(invalid));
    REQUIRE(destination.saveState() == before);
  }

  SECTION("A pending load remains in R")
  {
    std::vector<std::uint8_t> invalid = source.saveState();
    invalid[SIMPLE_EE_FIRST_IN_FLIGHT_COP1_STAGE_OFFSET] =
      COP1_STAGE_S1;
    updateChecksum(&invalid);

    REQUIRE_THROWS(destination.loadState(invalid));
    REQUIRE(destination.saveState() == before);
  }

  SECTION("A pending load has no synthetic countdown")
  {
    std::vector<std::uint8_t> invalid = source.saveState();
    invalid[
      SIMPLE_EE_FIRST_IN_FLIGHT_COP1_REMAINING_CYCLES_OFFSET] = 1;
    updateChecksum(&invalid);

    REQUIRE_THROWS(destination.loadState(invalid));
    REQUIRE(destination.saveState() == before);
  }
}

TEST_CASE("Invalid pending COP1 divider states are rejected")
{
  NekoSystem source;
  source.eeCore().setFloatingPointRegister(2, UINT32_C(0x40c00000));
  source.eeCore().setFloatingPointRegister(3, UINT32_C(0x40000000));
  source.eeCore().setFloatingPointRegister(6, UINT32_C(0x41100000));
  source.eeBus().write32(
    0,
    (UINT32_C(0x11) << 26) |
      (UINT32_C(0x10) << 21) |
      (UINT32_C(3) << 16) |
      (UINT32_C(2) << 11) |
      (UINT32_C(4) << 6) |
      UINT32_C(0x03));
  source.eeBus().write32(
    4,
    (UINT32_C(0x11) << 26) |
      (UINT32_C(0x10) << 21) |
      (UINT32_C(6) << 16) |
      (UINT32_C(5) << 6) |
      UINT32_C(0x04));
  source.eeCore().startExecution(0);
  source.runMasterCycles(8);
  source.eeCore().haltExecution();

  NekoSystem destination;
  const std::vector<std::uint8_t> before =
    destination.saveState();

  SECTION("Divider occupancy requires a pending result")
  {
    std::vector<std::uint8_t> invalid = before;
    invalid[SIMPLE_EE_COP1_DIVIDER_INITIATION_OFFSET] = 1;
    invalid[SIMPLE_EE_COP1_DIVIDER_OPERATION_OFFSET] =
      static_cast<std::uint8_t>(
        EEOperation::DivideSingleCOP1);
    updateChecksum(&invalid);

    REQUIRE_THROWS(destination.loadState(invalid));
    REQUIRE(destination.saveState() == before);
  }

  SECTION("Overlapping results require distinct destinations")
  {
    std::vector<std::uint8_t> invalid = source.saveState();
    REQUIRE(
      invalid[
        SIMPLE_EE_FIRST_IN_FLIGHT_COP1_DESTINATION_FPR_OFFSET] ==
      4);
    REQUIRE(
      invalid[
        SIMPLE_EE_SECOND_IN_FLIGHT_COP1_DESTINATION_FPR_OFFSET] ==
      5);
    invalid[
      SIMPLE_EE_SECOND_IN_FLIGHT_COP1_DESTINATION_FPR_OFFSET] =
      invalid[
        SIMPLE_EE_FIRST_IN_FLIGHT_COP1_DESTINATION_FPR_OFFSET];
    updateChecksum(&invalid);

    REQUIRE_THROWS(destination.loadState(invalid));
    REQUIRE(destination.saveState() == before);
  }

  SECTION("A result cannot raise invalid and division by zero together")
  {
    std::vector<std::uint8_t> invalid = source.saveState();
    invalid[
      SIMPLE_EE_FIRST_IN_FLIGHT_COP1_RAISED_FLAGS_OFFSET] =
      FP_FLAG_I_BIT | FP_FLAG_D_BIT;
    updateChecksum(&invalid);

    REQUIRE_THROWS(destination.loadState(invalid));
    REQUIRE(destination.saveState() == before);
  }

  SECTION("Square root requires its canonical zero fs capture")
  {
    NekoSystem squareRootSource;
    squareRootSource.eeCore().setFloatingPointRegister(
      6,
      UINT32_C(0x41100000));
    squareRootSource.eeBus().write32(
      0,
      (UINT32_C(0x11) << 26) |
        (UINT32_C(0x10) << 21) |
        (UINT32_C(6) << 16) |
        (UINT32_C(5) << 6) |
        UINT32_C(0x04));
    squareRootSource.eeCore().startExecution(0);
    squareRootSource.clockMasterCycle();
    squareRootSource.eeCore().haltExecution();

    std::vector<std::uint8_t> invalid =
      squareRootSource.saveState();
    writeU32(
      &invalid,
      SIMPLE_EE_FIRST_IN_FLIGHT_COP1_CAPTURED_FS_OFFSET,
      UINT32_C(0xdeadbeef));
    updateChecksum(&invalid);

    REQUIRE_THROWS(destination.loadState(invalid));
    REQUIRE(destination.saveState() == before);
  }

  SECTION("A transient S1 result cannot be restored")
  {
    std::vector<std::uint8_t> invalid = source.saveState();
    invalid[SIMPLE_EE_FIRST_IN_FLIGHT_COP1_STAGE_OFFSET] =
      COP1_STAGE_S1;
    invalid[
      SIMPLE_EE_FIRST_IN_FLIGHT_COP1_REMAINING_CYCLES_OFFSET] =
      0;
    updateChecksum(&invalid);

    REQUIRE_THROWS(destination.loadState(invalid));
    REQUIRE(destination.saveState() == before);
  }

  SECTION("A result cannot contain a forged raw value")
  {
    std::vector<std::uint8_t> invalid = source.saveState();
    invalid[
      SIMPLE_EE_FIRST_IN_FLIGHT_COP1_RAW_RESULT_OFFSET] ^= 1;
    updateChecksum(&invalid);

    REQUIRE_THROWS(destination.loadState(invalid));
    REQUIRE(destination.saveState() == before);
  }

  SECTION("A permitted flag must still match the captured operands")
  {
    std::vector<std::uint8_t> invalid = source.saveState();
    invalid[
      SIMPLE_EE_FIRST_IN_FLIGHT_COP1_RAISED_FLAGS_OFFSET] =
        FP_FLAG_D_BIT;
    updateChecksum(&invalid);

    REQUIRE_THROWS(destination.loadState(invalid));
    REQUIRE(destination.saveState() == before);
  }

  SECTION("Overlapping results require unique program order")
  {
    std::vector<std::uint8_t> invalid = source.saveState();
    writeU64(
      &invalid,
      SIMPLE_EE_SECOND_IN_FLIGHT_COP1_ORDER_OFFSET,
      1);
    updateChecksum(&invalid);

    REQUIRE_THROWS(destination.loadState(invalid));
    REQUIRE(destination.saveState() == before);
  }

  SECTION("The older overlapping result must complete first")
  {
    std::vector<std::uint8_t> invalid = source.saveState();
    writeU64(
      &invalid,
      SIMPLE_EE_FIRST_IN_FLIGHT_COP1_ORDER_OFFSET,
      8);
    writeU64(
      &invalid,
      SIMPLE_EE_SECOND_IN_FLIGHT_COP1_ORDER_OFFSET,
      1);
    updateChecksum(&invalid);

    REQUIRE_THROWS(destination.loadState(invalid));
    REQUIRE(destination.saveState() == before);
  }

  SECTION("Divider occupancy names the newest result")
  {
    std::vector<std::uint8_t> invalid = source.saveState();
    invalid[SIMPLE_EE_COP1_DIVIDER_OPERATION_OFFSET] =
      static_cast<std::uint8_t>(
        EEOperation::DivideSingleCOP1);
    updateChecksum(&invalid);

    REQUIRE_THROWS(destination.loadState(invalid));
    REQUIRE(destination.saveState() == before);
  }
}

TEST_CASE("Every malformed COP1 divider result is rejected")
{
  struct DividerVector
  {
    std::uint8_t function;
    std::uint8_t sourceRegister;
    std::uint32_t fs;
    std::uint32_t ft;
  };
  const DividerVector vectors[] = {
    {0x03, 2, 0, 0},
    {0x04, 0, 0, UINT32_C(0xc1100000)},
    {0x16, 2, UINT32_C(0x3f800000), 0}
  };

  for (const DividerVector &vector : vectors)
  {
    NekoSystem source;
    source.eeCore().setFloatingPointRegister(2, vector.fs);
    source.eeCore().setFloatingPointRegister(3, vector.ft);
    source.eeBus().write32(
      0,
      cop1SingleInstruction(
        vector.function,
        vector.sourceRegister,
        4,
        3));
    source.eeCore().startExecution(0);
    source.clockMasterCycle();
    source.eeCore().haltExecution();

    std::vector<std::uint8_t> invalid = source.saveState();
    invalid[SIMPLE_EE_FIRST_IN_FLIGHT_COP1_RAW_RESULT_OFFSET] ^=
      1;
    updateChecksum(&invalid);

    NekoSystem destination;
    const std::vector<std::uint8_t> before =
      destination.saveState();
    REQUIRE_THROWS(destination.loadState(invalid));
    REQUIRE(destination.saveState() == before);
  }
}

TEST_CASE("Invalid staged COP1 add states are rejected")
{
  NekoSystem source;
  source.eeCore().setFloatingPointRegister(
    2,
    UINT32_C(0x3f800000));
  source.eeCore().setFloatingPointRegister(
    3,
    UINT32_C(0x40000000));
  source.eeBus().write32(
    0,
    cop1SingleInstruction(0x00, 2, 4, 3));
  source.eeCore().startExecution(0);
  source.runMasterCycles(5);
  source.eeCore().haltExecution();

  NekoSystem destination;
  const std::vector<std::uint8_t> before =
    destination.saveState();

  SECTION("Unblocked S1 and transient S2 cannot be restored")
  {
    for (const std::uint8_t stage : {
           COP1_STAGE_S1,
           COP1_STAGE_S2})
    {
      std::vector<std::uint8_t> invalid = source.saveState();
      invalid[SIMPLE_EE_FIRST_IN_FLIGHT_COP1_STAGE_OFFSET] =
        stage;
      updateChecksum(&invalid);

      REQUIRE_THROWS(destination.loadState(invalid));
      REQUIRE(destination.saveState() == before);
    }
  }

  SECTION("An older staged operation cannot justify a younger S1 result")
  {
    NekoSystem stagedSource;
    stagedSource.eeBus().write32(
      0,
      cop1SingleInstruction(0x00, 2, 4, 3));
    stagedSource.eeBus().write32(
      4,
      cop1SingleInstruction(0x00, 5, 7, 6));
    stagedSource.eeCore().startExecution(0);
    stagedSource.runMasterCycles(2);
    stagedSource.eeCore().haltExecution();

    std::vector<std::uint8_t> invalid =
      stagedSource.saveState();
    invalid[
      SIMPLE_EE_SECOND_IN_FLIGHT_COP1_STAGE_OFFSET] =
      COP1_STAGE_S1;
    updateChecksum(&invalid);

    REQUIRE_THROWS(destination.loadState(invalid));
    REQUIRE(destination.saveState() == before);
  }

  SECTION("A Z-stage result must match its captured operands")
  {
    std::vector<std::uint8_t> invalid = source.saveState();
    invalid[SIMPLE_EE_FIRST_IN_FLIGHT_COP1_RAW_RESULT_OFFSET] ^=
      1;
    updateChecksum(&invalid);

    REQUIRE_THROWS(destination.loadState(invalid));
    REQUIRE(destination.saveState() == before);
  }

  SECTION("An R-stage operation cannot contain captured operands")
  {
    NekoSystem rSource;
    rSource.eeCore().setFloatingPointRegister(
      2,
      UINT32_C(0x3f800000));
    rSource.eeCore().setFloatingPointRegister(
      3,
      UINT32_C(0x40000000));
    rSource.eeBus().write32(
      0,
      cop1SingleInstruction(0x00, 2, 4, 3));
    rSource.eeCore().startExecution(0);
    rSource.clockMasterCycle();
    rSource.eeCore().haltExecution();

    std::vector<std::uint8_t> invalid =
      rSource.saveState();
    invalid[
      SIMPLE_EE_FIRST_IN_FLIGHT_COP1_CAPTURED_FS_OFFSET] = 1;
    updateChecksum(&invalid);

    REQUIRE_THROWS(destination.loadState(invalid));
    REQUIRE(destination.saveState() == before);
  }

  SECTION("A Z-stage result must carry its computed flags")
  {
    std::vector<std::uint8_t> invalid = source.saveState();
    invalid[
      SIMPLE_EE_FIRST_IN_FLIGHT_COP1_RAISED_FLAGS_OFFSET] =
        FP_FLAG_UNDERFLOW;
    updateChecksum(&invalid);

    REQUIRE_THROWS(destination.loadState(invalid));
    REQUIRE(destination.saveState() == before);
  }

  SECTION("The destination must match the encoded fd")
  {
    std::vector<std::uint8_t> invalid = source.saveState();
    invalid[
      SIMPLE_EE_FIRST_IN_FLIGHT_COP1_DESTINATION_FPR_OFFSET] =
        5;
    updateChecksum(&invalid);

    REQUIRE_THROWS(destination.loadState(invalid));
    REQUIRE(destination.saveState() == before);
  }

  SECTION("Unmigrated COP1 operations cannot be restored in flight")
  {
    std::vector<std::uint8_t> invalid = source.saveState();
    writeU32(
      &invalid,
      SIMPLE_EE_FIRST_IN_FLIGHT_COP1_INSTRUCTION_OFFSET,
      cop1SingleInstruction(0x06, 2, 4, 0));
    updateChecksum(&invalid);

    REQUIRE_THROWS(destination.loadState(invalid));
    REQUIRE(destination.saveState() == before);
  }
}

TEST_CASE("Invalid staged COP1 unary results are rejected")
{
  NekoSystem source;
  source.eeCore().setFloatingPointRegister(
    2,
    UINT32_C(0xffc12345));
  source.eeBus().write32(
    0,
    cop1SingleInstruction(0x05, 2, 4, 0));
  source.eeCore().startExecution(0);
  source.runMasterCycles(5);
  source.eeCore().haltExecution();

  std::vector<std::uint8_t> invalid = source.saveState();
  invalid[SIMPLE_EE_FIRST_IN_FLIGHT_COP1_RAW_RESULT_OFFSET] ^=
    1;
  updateChecksum(&invalid);

  NekoSystem destination;
  const std::vector<std::uint8_t> before =
    destination.saveState();
  REQUIRE_THROWS(destination.loadState(invalid));
  REQUIRE(destination.saveState() == before);
}

TEST_CASE("Invalid staged COP1 multiply results are rejected")
{
  NekoSystem source;
  source.eeCore().setFloatingPointRegister(
    2,
    UINT32_C(0x7f800000));
  source.eeCore().setFloatingPointRegister(
    3,
    UINT32_C(0x40000000));
  source.eeBus().write32(
    0,
    cop1SingleInstruction(0x1a, 2, 0, 3));
  source.eeCore().startExecution(0);
  source.runMasterCycles(5);
  source.eeCore().haltExecution();

  NekoSystem destination;
  const std::vector<std::uint8_t> before =
    destination.saveState();

  SECTION("The Z-stage result must match its captured operands")
  {
    std::vector<std::uint8_t> invalid = source.saveState();
    invalid[SIMPLE_EE_FIRST_IN_FLIGHT_COP1_RAW_RESULT_OFFSET] ^=
      1;
    updateChecksum(&invalid);

    REQUIRE_THROWS(destination.loadState(invalid));
    REQUIRE(destination.saveState() == before);
  }

  SECTION("MULA must name ACC and FCR31 as its destinations")
  {
    std::vector<std::uint8_t> invalid = source.saveState();
    invalid[
      SIMPLE_EE_FIRST_IN_FLIGHT_COP1_DESTINATION_MASK_OFFSET] =
        1;
    updateChecksum(&invalid);

    REQUIRE_THROWS(destination.loadState(invalid));
    REQUIRE(destination.saveState() == before);
  }
}

TEST_CASE("Invalid staged COP1 accumulator add results are rejected")
{
  for (const std::uint8_t function : {0x18, 0x19})
  {
    NekoSystem source;
    source.eeCore().setFloatingPointRegister(
      2,
      function == 0x18
        ? UINT32_C(0x7f800000)
        : UINT32_C(0x00800001));
    source.eeCore().setFloatingPointRegister(
      3,
      function == 0x18
        ? UINT32_C(0x7f800000)
        : UINT32_C(0x00800000));
    source.eeBus().write32(
      0,
      cop1SingleInstruction(function, 2, 0, 3));
    source.eeCore().startExecution(0);
    source.runMasterCycles(5);
    source.eeCore().haltExecution();

    NekoSystem destination;
    const std::vector<std::uint8_t> before =
      destination.saveState();

    SECTION("The Z-stage result must match its captured operands")
    {
      std::vector<std::uint8_t> invalid = source.saveState();
      invalid[
        SIMPLE_EE_FIRST_IN_FLIGHT_COP1_RAW_RESULT_OFFSET] ^= 1;
      updateChecksum(&invalid);

      REQUIRE_THROWS(destination.loadState(invalid));
      REQUIRE(destination.saveState() == before);
    }

    SECTION("The instruction must name ACC and FCR31 as destinations")
    {
      std::vector<std::uint8_t> invalid = source.saveState();
      invalid[
        SIMPLE_EE_FIRST_IN_FLIGHT_COP1_DESTINATION_MASK_OFFSET] =
          1;
      updateChecksum(&invalid);

      REQUIRE_THROWS(destination.loadState(invalid));
      REQUIRE(destination.saveState() == before);
    }
  }
}

TEST_CASE("Invalid staged COP1 compound results are rejected")
{
  NekoSystem source;
  source.eeCore().setFloatingPointAccumulator(
    UINT32_C(0x40000000));
  source.eeCore().setFloatingPointRegister(
    2,
    UINT32_C(0x80800000));
  source.eeCore().setFloatingPointRegister(
    3,
    UINT32_C(0x3f000000));
  source.eeBus().write32(
    0,
    cop1SingleInstruction(0x1e, 2, 0, 3));
  source.eeCore().startExecution(0);
  source.runMasterCycles(5);
  source.eeCore().haltExecution();

  NekoSystem destination;
  const std::vector<std::uint8_t> before =
    destination.saveState();

  SECTION("The Z-stage result must match all captured operands")
  {
    std::vector<std::uint8_t> invalid = source.saveState();
    invalid[
      SIMPLE_EE_FIRST_IN_FLIGHT_COP1_CAPTURED_ACC_OFFSET] ^= 1;
    updateChecksum(&invalid);

    REQUIRE_THROWS(destination.loadState(invalid));
    REQUIRE(destination.saveState() == before);
  }

  SECTION("The Z-stage result must carry product sticky flags")
  {
    std::vector<std::uint8_t> invalid = source.saveState();
    invalid[
      SIMPLE_EE_FIRST_IN_FLIGHT_COP1_RAISED_STICKY_FLAGS_OFFSET] = 0;
    updateChecksum(&invalid);

    REQUIRE_THROWS(destination.loadState(invalid));
    REQUIRE(destination.saveState() == before);
  }

  SECTION("MADDA must name ACC and FCR31 as its destinations")
  {
    std::vector<std::uint8_t> invalid = source.saveState();
    invalid[
      SIMPLE_EE_FIRST_IN_FLIGHT_COP1_DESTINATION_MASK_OFFSET] =
        1;
    updateChecksum(&invalid);

    REQUIRE_THROWS(destination.loadState(invalid));
    REQUIRE(destination.saveState() == before);
  }
}

TEST_CASE("Invalid staged COP1 min/max results are rejected")
{
  NekoSystem source;
  source.eeCore().setFloatingPointRegister(
    2,
    UINT32_C(0x007fffff));
  source.eeCore().setFloatingPointRegister(
    3,
    UINT32_C(0x80000000));
  source.eeBus().write32(
    0,
    cop1SingleInstruction(0x28, 2, 4, 3));
  source.eeCore().startExecution(0);
  source.runMasterCycles(5);
  source.eeCore().haltExecution();

  std::vector<std::uint8_t> invalid = source.saveState();
  invalid[SIMPLE_EE_FIRST_IN_FLIGHT_COP1_RAW_RESULT_OFFSET] =
    1;
  updateChecksum(&invalid);

  NekoSystem destination;
  const std::vector<std::uint8_t> before =
    destination.saveState();
  REQUIRE_THROWS(destination.loadState(invalid));
  REQUIRE(destination.saveState() == before);
}

TEST_CASE("Invalid staged COP1 conversion results are rejected")
{
  NekoSystem source;
  source.eeCore().setFloatingPointRegister(
    2,
    UINT32_C(0x4f000000));
  source.eeBus().write32(
    0,
    cop1SingleInstruction(0x24, 2, 4, 0));
  source.eeCore().startExecution(0);
  source.runMasterCycles(5);
  source.eeCore().haltExecution();

  std::vector<std::uint8_t> invalid = source.saveState();
  invalid[SIMPLE_EE_FIRST_IN_FLIGHT_COP1_RAW_RESULT_OFFSET] ^=
    1;
  updateChecksum(&invalid);

  NekoSystem destination;
  const std::vector<std::uint8_t> before =
    destination.saveState();
  REQUIRE_THROWS(destination.loadState(invalid));
  REQUIRE(destination.saveState() == before);
}

TEST_CASE("Invalid staged COP1 comparison results are rejected")
{
  NekoSystem source;
  source.eeCore().setFloatingPointRegister(
    2,
    UINT32_C(0x3f800000));
  source.eeCore().setFloatingPointRegister(
    3,
    UINT32_C(0x3f800000));
  source.eeBus().write32(
    0,
    cop1SingleInstruction(0x32, 2, 0, 3));
  source.eeCore().startExecution(0);
  source.runMasterCycles(5);
  source.eeCore().haltExecution();

  std::vector<std::uint8_t> invalid = source.saveState();
  invalid[
    SIMPLE_EE_FIRST_IN_FLIGHT_COP1_CONDITION_OFFSET] ^= 1;
  updateChecksum(&invalid);

  NekoSystem destination;
  const std::vector<std::uint8_t> before =
    destination.saveState();
  REQUIRE_THROWS(destination.loadState(invalid));
  REQUIRE(destination.saveState() == before);
}

TEST_CASE("Blocked COP1 1S retirement survives save-state restore")
{
  NekoSystem original;
  EECore &core = original.eeCore();
  core.setGeneralRegister(1, {0x100, 0});
  core.setFloatingPointRegister(2, UINT32_C(0x3f800000));
  core.setFloatingPointRegister(3, UINT32_C(0x40000000));
  core.setFloatingPointRegister(5, UINT32_C(0xdeadbeef));
  REQUIRE(
    original.eeBus().writeData32(
      0x100,
      UINT32_C(0x12345678)));
  original.eeBus().write32(
    0,
    cop1SingleInstruction(0x00, 2, 4, 3));
  original.eeBus().write32(
    4,
    (UINT32_C(0x31) << 26) |
      (UINT32_C(1) << 21) |
      (UINT32_C(5) << 16));
  core.startExecution(0);
  original.runMasterCycles(3);

  const std::vector<std::uint8_t> state =
    original.saveState();
  NekoSystem restored;
  restored.loadState(state);

  original.runMasterCycles(3);
  restored.runMasterCycles(3);

  REQUIRE(
    restored.eeCore().floatingPointRegister(4) ==
    UINT32_C(0x40400000));
  REQUIRE(
    restored.eeCore().floatingPointRegister(5) ==
    UINT32_C(0x12345678));
  REQUIRE(restored.saveState() == original.saveState());
}

TEST_CASE("Unreachable COP1 load 1S states are rejected")
{
  NekoSystem destination;
  const std::vector<std::uint8_t> before =
    destination.saveState();

  SECTION("A parked load requires an older non-ready operation")
  {
    NekoSystem source;
    source.eeCore().setGeneralRegister(1, {0x100, 0});
    REQUIRE(
      source.eeBus().writeData32(
        0x100,
        UINT32_C(0x12345678)));
    source.eeBus().write32(
      0,
      (UINT32_C(0x31) << 26) |
        (UINT32_C(1) << 21) |
        (UINT32_C(5) << 16));
    source.eeCore().startExecution(0);
    source.clockMasterCycle();
    source.eeCore().haltExecution();

    std::vector<std::uint8_t> invalid = source.saveState();
    invalid[SIMPLE_EE_FIRST_IN_FLIGHT_COP1_STAGE_OFFSET] =
      COP1_STAGE_S1;
    invalid[
      SIMPLE_EE_FIRST_IN_FLIGHT_COP1_REMAINING_CYCLES_OFFSET] =
      0;
    updateChecksum(&invalid);

    REQUIRE_THROWS(destination.loadState(invalid));
    REQUIRE(destination.saveState() == before);
  }

  SECTION("A parked load cannot bypass an older FPR writer")
  {
    NekoSystem source;
    EECore &core = source.eeCore();
    core.setGeneralRegister(1, {0x100, 0});
    core.setFloatingPointRegister(2, UINT32_C(0x3f800000));
    core.setFloatingPointRegister(3, UINT32_C(0x40000000));
    REQUIRE(
      source.eeBus().writeData32(
        0x100,
        UINT32_C(0x12345678)));
    source.eeBus().write32(
      0,
      cop1SingleInstruction(0x00, 2, 4, 3));
    source.eeBus().write32(
      4,
      (UINT32_C(0x31) << 26) |
        (UINT32_C(1) << 21) |
        (UINT32_C(5) << 16));
    core.startExecution(0);
    source.runMasterCycles(3);
    core.haltExecution();

    std::vector<std::uint8_t> invalid = source.saveState();
    writeU32(
      &invalid,
      SIMPLE_EE_SECOND_IN_FLIGHT_COP1_INSTRUCTION_OFFSET,
      (UINT32_C(0x31) << 26) |
        (UINT32_C(1) << 21) |
        (UINT32_C(4) << 16));
    invalid[
      SIMPLE_EE_SECOND_IN_FLIGHT_COP1_DESTINATION_FPR_OFFSET] =
      4;
    invalid[SIMPLE_EE_SECOND_IN_FLIGHT_COP1_STAGE_OFFSET] =
      COP1_STAGE_S1;
    invalid[
      SIMPLE_EE_SECOND_IN_FLIGHT_COP1_REMAINING_CYCLES_OFFSET] =
      0;
    updateChecksum(&invalid);

    REQUIRE_THROWS(destination.loadState(invalid));
    REQUIRE(destination.saveState() == before);
  }
}

TEST_CASE("Blocked staged COP1 1S retirement survives save-state restore")
{
  NekoSystem original;
  EECore &core = original.eeCore();
  core.setFloatingPointRegister(2, UINT32_C(0x40c00000));
  core.setFloatingPointRegister(3, UINT32_C(0x40000000));
  core.setFloatingPointRegister(5, 2);
  original.eeBus().write32(
    0,
    cop1SingleInstruction(0x03, 2, 4, 3));
  original.eeBus().write32(
    4,
    (UINT32_C(0x11) << 26) |
      (UINT32_C(0x14) << 21) |
      (UINT32_C(5) << 11) |
      (UINT32_C(6) << 6) |
      UINT32_C(0x20));
  core.startExecution(0);
  original.runMasterCycles(7);
  core.haltExecution();

  const std::vector<std::uint8_t> state =
    original.saveState();
  NekoSystem restored;
  restored.loadState(state);
  original.eeCore().startExecution(
    original.eeCore().programCounter());
  restored.eeCore().startExecution(
    restored.eeCore().programCounter());

  original.runMasterCycles(2);
  restored.runMasterCycles(2);

  REQUIRE(
    restored.eeCore().floatingPointRegister(4) ==
    UINT32_C(0x40400000));
  REQUIRE(
    restored.eeCore().floatingPointRegister(6) ==
    UINT32_C(0x40000000));
  REQUIRE(restored.saveState() == original.saveState());
  REQUIRE(
    restored.eeCore().stateHash() ==
    original.eeCore().stateHash());
}

TEST_CASE("Dependent staged COP1 1S overlap is rejected")
{
  NekoSystem source;
  EECore &core = source.eeCore();
  core.setFloatingPointRegister(2, UINT32_C(0x40c00000));
  core.setFloatingPointRegister(3, UINT32_C(0x40000000));
  core.setFloatingPointRegister(5, 2);
  source.eeBus().write32(
    0,
    cop1SingleInstruction(0x03, 2, 4, 3));
  source.eeBus().write32(
    4,
    (UINT32_C(0x11) << 26) |
      (UINT32_C(0x14) << 21) |
      (UINT32_C(5) << 11) |
      (UINT32_C(6) << 6) |
      UINT32_C(0x20));
  core.startExecution(0);
  source.runMasterCycles(7);
  core.haltExecution();

  std::vector<std::uint8_t> invalid = source.saveState();
  writeU32(
    &invalid,
    SIMPLE_EE_SECOND_IN_FLIGHT_COP1_INSTRUCTION_OFFSET,
    (UINT32_C(0x11) << 26) |
      (UINT32_C(0x14) << 21) |
      (UINT32_C(4) << 11) |
      (UINT32_C(6) << 6) |
      UINT32_C(0x20));
  updateChecksum(&invalid);

  NekoSystem destination;
  const std::vector<std::uint8_t> before =
    destination.saveState();
  REQUIRE_THROWS(destination.loadState(invalid));
  REQUIRE(destination.saveState() == before);
}

TEST_CASE("Premature staged COP1 1S overlap is rejected")
{
  NekoSystem source;
  EECore &core = source.eeCore();
  core.setFloatingPointRegister(2, UINT32_C(0x40c00000));
  core.setFloatingPointRegister(3, UINT32_C(0x40000000));
  core.setFloatingPointRegister(5, 2);
  source.eeBus().write32(
    0,
    cop1SingleInstruction(0x03, 2, 4, 3));
  source.eeBus().write32(
    4,
    (UINT32_C(0x11) << 26) |
      (UINT32_C(0x14) << 21) |
      (UINT32_C(5) << 11) |
      (UINT32_C(6) << 6) |
      UINT32_C(0x20));
  core.startExecution(0);
  source.runMasterCycles(7);
  core.haltExecution();

  std::vector<std::uint8_t> invalid = source.saveState();
  invalid[SIMPLE_EE_COP1_DIVIDER_INITIATION_OFFSET] = 7;
  invalid[
    SIMPLE_EE_FIRST_IN_FLIGHT_COP1_REMAINING_CYCLES_OFFSET] =
    8;
  updateChecksum(&invalid);

  NekoSystem destination;
  const std::vector<std::uint8_t> before =
    destination.saveState();
  REQUIRE_THROWS(destination.loadState(invalid));
  REQUIRE(destination.saveState() == before);
}

TEST_CASE("Out-of-order staged COP1 1S overlap is rejected")
{
  NekoSystem source;
  EECore &core = source.eeCore();
  core.setFloatingPointRegister(2, UINT32_C(0x40c00000));
  core.setFloatingPointRegister(3, UINT32_C(0x40000000));
  core.setFloatingPointRegister(5, 2);
  core.setFloatingPointRegister(7, 3);
  source.eeBus().write32(
    0,
    cop1SingleInstruction(0x03, 2, 4, 3));
  source.eeBus().write32(
    4,
    (UINT32_C(0x11) << 26) |
      (UINT32_C(0x14) << 21) |
      (UINT32_C(5) << 11) |
      (UINT32_C(6) << 6) |
      UINT32_C(0x20));
  source.eeBus().write32(
    8,
    (UINT32_C(0x11) << 26) |
      (UINT32_C(0x14) << 21) |
      (UINT32_C(7) << 11) |
      (UINT32_C(8) << 6) |
      UINT32_C(0x20));
  core.startExecution(0);
  source.runMasterCycles(7);
  core.haltExecution();

  std::vector<std::uint8_t> invalid = source.saveState();
  invalid[SIMPLE_EE_SECOND_IN_FLIGHT_COP1_STAGE_OFFSET] =
    COP1_STAGE_Z;
  invalid[SIMPLE_EE_THIRD_IN_FLIGHT_COP1_STAGE_OFFSET] =
    COP1_STAGE_S1;
  updateChecksum(&invalid);

  NekoSystem destination;
  const std::vector<std::uint8_t> before =
    destination.saveState();
  REQUIRE_THROWS(destination.loadState(invalid));
  REQUIRE(destination.saveState() == before);
}

TEST_CASE("Staged COP1 pipeline order is validated")
{
  NekoSystem source;
  source.eeBus().write32(
    0,
    cop1SingleInstruction(0x00, 2, 4, 3));
  source.eeBus().write32(
    4,
    cop1SingleInstruction(0x00, 5, 7, 6));
  source.eeCore().startExecution(0);
  source.runMasterCycles(2);
  source.eeCore().haltExecution();

  std::vector<std::uint8_t> invalid = source.saveState();
  invalid[SIMPLE_EE_FIRST_IN_FLIGHT_COP1_STAGE_OFFSET] =
    COP1_STAGE_X;
  invalid[SIMPLE_EE_SECOND_IN_FLIGHT_COP1_STAGE_OFFSET] =
    COP1_STAGE_Y;
  updateChecksum(&invalid);

  NekoSystem destination;
  const std::vector<std::uint8_t> before =
    destination.saveState();
  REQUIRE_THROWS(destination.loadState(invalid));
  REQUIRE(destination.saveState() == before);
}

TEST_CASE("Staged COP1 1S cannot follow two active dividers")
{
  NekoSystem source;
  EECore &core = source.eeCore();
  core.setFloatingPointRegister(2, UINT32_C(0x40c00000));
  core.setFloatingPointRegister(3, UINT32_C(0x40000000));
  core.setFloatingPointRegister(5, 2);
  source.eeBus().write32(
    0,
    cop1SingleInstruction(0x03, 2, 4, 3));
  source.eeBus().write32(
    4,
    (UINT32_C(0x11) << 26) |
      (UINT32_C(0x14) << 21) |
      (UINT32_C(5) << 11) |
      (UINT32_C(6) << 6) |
      UINT32_C(0x20));
  for (std::uint32_t address = 8; address < 28; address += 4)
  {
    source.eeBus().write32(address, 0);
  }
  source.eeBus().write32(
    28,
    cop1SingleInstruction(0x03, 2, 8, 3));
  core.startExecution(0);
  source.runMasterCycles(8);
  core.haltExecution();

  std::vector<std::uint8_t> invalid = source.saveState();
  writeU64(
    &invalid,
    SIMPLE_EE_SECOND_IN_FLIGHT_COP1_ORDER_OFFSET,
    3);
  writeU64(
    &invalid,
    SIMPLE_EE_THIRD_IN_FLIGHT_COP1_ORDER_OFFSET,
    2);
  updateChecksum(&invalid);

  NekoSystem destination;
  const std::vector<std::uint8_t> before =
    destination.saveState();
  REQUIRE_THROWS(destination.loadState(invalid));
  REQUIRE(destination.saveState() == before);
}

TEST_CASE("Staged COP1 1S validates forwarded operands")
{
  NekoSystem source;
  EECore &core = source.eeCore();
  core.setFloatingPointRegister(2, UINT32_C(0x3f800000));
  core.setFloatingPointRegister(3, UINT32_C(0x40000000));
  core.setFloatingPointRegister(5, 2);
  source.eeBus().write32(
    0,
    cop1SingleInstruction(0x16, 2, 4, 3));
  source.eeBus().write32(
    4,
    (UINT32_C(0x11) << 26) |
      (UINT32_C(0x14) << 21) |
      (UINT32_C(5) << 11) |
      (UINT32_C(6) << 6) |
      UINT32_C(0x20));
  source.eeBus().write32(
    8,
    (UINT32_C(0x11) << 26) |
      (UINT32_C(0x14) << 21) |
      (UINT32_C(6) << 11) |
      (UINT32_C(7) << 6) |
      UINT32_C(0x20));
  core.startExecution(0);
  source.runMasterCycles(11);
  core.haltExecution();

  std::vector<std::uint8_t> invalid = source.saveState();
  writeU32(
    &invalid,
    SIMPLE_EE_THIRD_IN_FLIGHT_COP1_CAPTURED_FS_OFFSET,
    3);
  writeU32(
    &invalid,
    SIMPLE_EE_THIRD_IN_FLIGHT_COP1_RAW_RESULT_OFFSET,
    UINT32_C(0x40400000));
  updateChecksum(&invalid);

  NekoSystem destination;
  const std::vector<std::uint8_t> before =
    destination.saveState();
  REQUIRE_THROWS(destination.loadState(invalid));
  REQUIRE(destination.saveState() == before);
}
