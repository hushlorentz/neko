#ifndef SPU2_INPUT_TEST_UTILS_HPP
#define SPU2_INPUT_TEST_UTILS_HPP

#include "catch.hpp"
#include "spu2_controller.hpp"

namespace SPU2InputTest
{
  constexpr std::array<std::uint16_t, 5> configuration = {{
    0x0900, 0x0200, 0, 0x0008, 0xc032
  }};

  inline void programProfile(SPU2Controller &spu)
  {
    for (std::size_t index = 0; index < configuration.size(); ++index)
    {
      REQUIRE(spu.writeSPDIFConfiguration(index, configuration[index]));
    }
    for (std::size_t core = 0; core < SPU2Controller::CORE_COUNT; ++core)
    {
      REQUIRE(spu.writeCoreAttribute(core, 0xc001));
    }
  }

  inline SPU2InputTick advanceSamples(SPU2Controller &spu, unsigned samples)
  {
    SPU2InputTick result;
    for (unsigned pulse = 0;
         pulse < samples * SPU2Controller::INPUT_CLOCK_PERIOD; ++pulse)
    {
      result = spu.advanceInputClock();
    }
    return result;
  }
}

#endif
