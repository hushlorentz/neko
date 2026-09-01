#include <cstdint>
#include <type_traits>

#include "catch.hpp"
#include "neko_system.hpp"
#include "vpu_opcodes.hpp"

namespace
{
  GIFQuadword gifTag(
    std::uint16_t loopCount,
    std::uint8_t descriptor)
  {
    const std::uint64_t low =
      loopCount |
      (UINT64_C(1) << 15) |
      (static_cast<std::uint64_t>(GIFDataFormat::Packed) << 58) |
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

  void writeTerminatingProgram(VPU *vpu)
  {
    vpu->writeMicroInstruction(
      0,
      VPU_LOWER_NOP,
      VPU_E_BIT | VPU_NOP);
    vpu->writeMicroInstruction(
      1,
      VPU_LOWER_NOP,
      VPU_NOP);
  }
}

TEST_CASE("Neko System Tests")
{
  SECTION("The system owns correctly typed hardware components")
  {
    static_assert(
      !std::is_copy_constructible<NekoSystem>::value,
      "NekoSystem must preserve its internal component wiring.");
    static_assert(
      !std::is_move_constructible<NekoSystem>::value,
      "NekoSystem must preserve its internal component wiring.");

    NekoSystem system;
    const NekoSystem &constSystem = system;

    REQUIRE(&constSystem.eeCore() == &system.eeCore());
    REQUIRE(system.vu0().unitType() == VPUType::VU0);
    REQUIRE(system.vu1().unitType() == VPUType::VU1);
    REQUIRE(system.vif0().unitType() == VIFType::VIF0);
    REQUIRE(system.vif1().unitType() == VIFType::VIF1);
    REQUIRE(&constSystem.vu0() == &system.vu0());
    REQUIRE(&constSystem.vu1() == &system.vu1());
    REQUIRE(&constSystem.vif0() == &system.vif0());
    REQUIRE(&constSystem.vif1() == &system.vif1());
    REQUIRE(&constSystem.gifDecoder() == &system.gifDecoder());
    REQUIRE(
      &constSystem.gifPathArbiter() ==
      &system.gifPathArbiter());
    REQUIRE(&constSystem.gifPath1() == &system.gifPath1());
    REQUIRE(&constSystem.gifPath3() == &system.gifPath3());
    REQUIRE(&constSystem.gs() == &system.gs());
    REQUIRE(
      &constSystem.gifRegisters() ==
      &system.gifRegisters());
    REQUIRE(&constSystem.gifDMAC() == &system.gifDMAC());
    REQUIRE(&constSystem.gsDisplay() == &system.gsDisplay());
    REQUIRE(&constSystem.eeBus() == &system.eeBus());
    REQUIRE(
      &constSystem.interruptController() ==
      &system.interruptController());
    REQUIRE(
      &constSystem.masterClockScheduler() ==
      &system.masterClockScheduler());
  }

  SECTION("PATH1 is wired from VU1 through GIF to GS")
  {
    NekoSystem system;
    system.vu1().writeDataQuadword(
      0,
      gifTag(1, GIFRegisterDescriptor::AD));
    system.vu1().writeDataQuadword(
      1,
      adWrite(GSRegisterAddress::PRIM, 3));

    system.gifPath1().startPath1Transfer(0);
    system.gifPath1().advancePath1Transfer();
    system.gifPath1().advancePath1Transfer();

    REQUIRE(!system.gifPath1().path1TransferActive());
    REQUIRE(
      system.gs().primitive().type ==
      GSPrimitiveType::Triangle);
  }

  SECTION("PATH3 shares the system GIF and GS")
  {
    NekoSystem system;
    const GIFQuadword packet[] = {
      gifTag(1, GIFRegisterDescriptor::AD),
      adWrite(GSRegisterAddress::PRIM, 6)
    };

    const GIFPath3SubmissionResult result =
      system.gifPath3().submitQuadwords(packet, 2);

    REQUIRE(result.transferredQuadwords == 2);
    REQUIRE(result.packetComplete);
    REQUIRE(
      system.gs().primitive().type ==
      GSPrimitiveType::Sprite);
  }

  SECTION("The master clock advances both VUs at half the EE rate")
  {
    static_assert(
      NekoSystem::EE_CLOCK_HZ ==
        NekoSystem::VU_CLOCK_HZ * 2,
      "The EE clock must be twice the VU clock.");

    NekoSystem system;
    writeTerminatingProgram(&system.vu0());
    writeTerminatingProgram(&system.vu1());
    system.vu0().startMicroMode();
    system.vu1().startMicroMode();

    REQUIRE(system.runMasterCycles(4) == 4);

    REQUIRE(system.vu0().elapsedCycles() == 2);
    REQUIRE(system.vu1().elapsedCycles() == 2);
    REQUIRE(
      system.masterClockScheduler().currentCycle() == 4);
  }

  SECTION("The master clock advances a running EE every cycle")
  {
    NekoSystem system;
    system.eeBus().write32(0, 0);
    system.eeBus().write32(4, 0);
    system.eeBus().write32(8, 0);
    system.eeCore().startExecution(0);

    REQUIRE(system.runMasterCycles(3) == 3);

    REQUIRE(system.eeCore().elapsedCycles() == 3);
    REQUIRE(system.eeCore().programCounter() == 12);
    REQUIRE(
      system.masterClockScheduler().currentCycle() == 3);
  }
}
