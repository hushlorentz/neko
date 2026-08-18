#include "catch.hpp"
#include "vpu.hpp"
#include "vpu_opcodes.hpp"

namespace
{
  void appendInstruction(std::vector<uint8_t> *instructions, uint32_t upper, uint32_t lower)
  {
    for (uint32_t instruction : {upper, lower})
    {
      instructions->push_back((instruction >> 24) & 0xff);
      instructions->push_back((instruction >> 16) & 0xff);
      instructions->push_back((instruction >> 8) & 0xff);
      instructions->push_back(instruction & 0xff);
    }
  }
}

TEST_CASE("VPU Memory Tests")
{
  SECTION("VU0 uses 4 KiB code and data memories by default")
  {
    VPU vpu;

    REQUIRE(vpu.unitType() == VPUType::VU0);
    REQUIRE(vpu.microMemorySize() == 4 * 1024);
    REQUIRE(vpu.dataMemorySize() == 4 * 1024);
  }

  SECTION("VU1 uses 16 KiB code and data memories")
  {
    VPU vpu(VPUType::VU1);

    REQUIRE(vpu.unitType() == VPUType::VU1);
    REQUIRE(vpu.microMemorySize() == 16 * 1024);
    REQUIRE(vpu.dataMemorySize() == 16 * 1024);
  }

  SECTION("Microprogram uploads accept an exact fit and reject overflow")
  {
    VPU vpu;
    std::vector<uint8_t> exactFit(vpu.microMemorySize(), 0);
    std::vector<uint8_t> tooLarge(vpu.microMemorySize() + 8, 0);

    REQUIRE_NOTHROW(vpu.uploadMicroInstructions(exactFit));
    REQUIRE_THROWS_WITH(
      vpu.uploadMicroInstructions(tooLarge),
      "Microprogram exceeds VU micro memory.");
  }

  SECTION("Microprogram uploads contain complete instruction pairs")
  {
    VPU vpu;
    std::vector<uint8_t> incompletePair(7, 0);

    REQUIRE_THROWS_WITH(
      vpu.uploadMicroInstructions(incompletePair),
      "Microprogram size must be a multiple of 8 bytes.");
  }

  SECTION("Instruction fetch rejects execution beyond micro memory")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;

    while (instructions.size() < vpu.microMemorySize())
    {
      appendInstruction(&instructions, VPU_NOP, VPU_LOWER_NOP);
    }

    vpu.uploadMicroInstructions(instructions);

    REQUIRE_THROWS_WITH(
      vpu.initMicroMode(),
      "Microinstruction fetch is outside micro memory.");
    REQUIRE(vpu.getState() == VPU_STATE_STOP);
  }

  SECTION("Host data-memory access is checked at the byte boundary")
  {
    VPU vpu;
    std::vector<uint8_t> data = {1, 2, 3, 4};
    size_t finalAddress = vpu.dataMemorySize() - data.size();

    REQUIRE_NOTHROW(vpu.writeDataMemory(finalAddress, data));
    REQUIRE(vpu.readDataMemory(finalAddress, data.size()) == data);
    REQUIRE_THROWS_WITH(
      vpu.writeDataMemory(finalAddress + 1, data),
      "VU data-memory write is outside memory.");
    REQUIRE_THROWS_WITH(
      vpu.readDataMemory(finalAddress, data.size() + 1),
      "VU data-memory read is outside memory.");
  }
}
