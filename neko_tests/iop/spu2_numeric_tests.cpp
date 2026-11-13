#include "catch.hpp"
#include "spu2_numeric.hpp"
#include "spu2_numeric_vectors.hpp"

TEST_CASE("SPU2 numeric ADPCM words obey authored raw-header and feedback vectors",
          "[iop][spu2][numeric]")
{
  for (const auto &vector : SPU2NumericTest::adpcmWords)
  {
    INFO(vector.name);
    const auto header = SPU2Numeric::decodeADPCMHeader(vector.header);
    REQUIRE(header.flags == vector.effectiveFlags);
    REQUIRE(SPU2Numeric::decodeADPCMWord(
      header, vector.payload, vector.history1, vector.history2) == vector.samples);
  }
}

TEST_CASE("SPU2 numeric ADPCM continues history across payload words",
          "[iop][spu2][numeric]")
{
  const auto header = SPU2Numeric::decodeADPCMHeader(0x001c);
  const auto first = SPU2Numeric::decodeADPCMWord(header, 0x1111, 0, 0);
  const std::array<std::int16_t, 4> expectedFirst = {{1, 2, 3, 4}};
  REQUIRE(first == expectedFirst);
  const auto second = SPU2Numeric::decodeADPCMWord(header, 0x1111, first[3], first[2]);
  const std::array<std::int16_t, 4> expectedSecond = {{5, 6, 7, 8}};
  REQUIRE(second == expectedSecond);
}

TEST_CASE("SPU2 numeric Gaussian interpolation obeys authored tap and floor vectors",
          "[iop][spu2][numeric]")
{
  for (const auto &vector : SPU2NumericTest::interpolations)
  {
    INFO(vector.name);
    REQUIRE(SPU2Numeric::interpolate(vector.samples, vector.fraction) == vector.output);
  }
}

TEST_CASE("SPU2 numeric fractional pitch obeys authored carry and saturation vectors",
          "[iop][spu2][numeric]")
{
  for (const auto &vector : SPU2NumericTest::pitches)
  {
    INFO(vector.name);
    const auto step = SPU2Numeric::advancePitch(vector.fraction, vector.rawPitch);
    REQUIRE(step.consumed == vector.consumed);
    REQUIRE(step.fraction == vector.nextFraction);
  }
  const auto upperBits = SPU2Numeric::advancePitch(0x0fff, 0xffff);
  REQUIRE(upperBits.consumed == 4);
  REQUIRE(upperBits.fraction == 0x0ffe);
}

TEST_CASE("SPU2 numeric fixed and signed gains obey authored floor and clipping vectors",
          "[iop][spu2][numeric]")
{
  for (const auto &vector : SPU2NumericTest::fixedGains)
  {
    INFO(vector.name);
    const auto gain = SPU2Numeric::fixedVolumeGain(vector.control);
    REQUIRE(gain == vector.gain);
    REQUIRE(SPU2Numeric::applyGain(vector.sample, gain) == vector.output);
  }
  for (const auto &vector : SPU2NumericTest::q15Gains)
  {
    INFO(vector.name);
    REQUIRE(SPU2Numeric::applyGain(vector.sample, vector.gain) == vector.output);
  }
}
