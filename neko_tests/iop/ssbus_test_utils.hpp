#ifndef SSBUS_TEST_UTILS_HPP
#define SSBUS_TEST_UTILS_HPP

#include <array>

#include "iop_types.hpp"

namespace SSBUSTest
{
  struct Register
  {
    IOPAddress address;
    const char *field;
  };

  constexpr std::array<Register, 18> registers = {{
    {0x1f801010, "system.iop.bus.ssbusConfiguration"},
    {0x1f801020, "system.iop.bus.ssbusCommonDelay"},
    {0x1f80101c, "system.iop.bus.ssbusPioDelay"},
    {0x1f801000, "system.iop.bus.ssbusSpdAddress"},
    {0x1f801004, "system.iop.bus.ssbusPioAddress"},
    {0x1f801008, "system.iop.bus.ssbusSpdDelay"},
    {0x1f80100c, "system.iop.bus.ssbusDev1Delay"},
    {0x1f801014, "system.iop.bus.ssbusSpuDelay"},
    {0x1f801018, "system.iop.bus.ssbusDev5Delay"},
    {0x1f801400, "system.iop.bus.ssbusDev1Address"},
    {0x1f801404, "system.iop.bus.ssbusSpuAddress"},
    {0x1f801408, "system.iop.bus.ssbusDev5Address"},
    {0x1f80140c, "system.iop.bus.ssbusSpu1Address"},
    {0x1f801410, "system.iop.bus.ssbusDev9Address3"},
    {0x1f801414, "system.iop.bus.ssbusSpu1Delay"},
    {0x1f801418, "system.iop.bus.ssbusDev9Delay2"},
    {0x1f80141c, "system.iop.bus.ssbusDev9Delay3"},
    {0x1f801420, "system.iop.bus.ssbusDev9Delay1"}
  }};

  constexpr std::array<IOPAddress, 18> addresses = {{
    0x1f801010, 0x1f801020, 0x1f80101c,
    0x1f801000, 0x1f801004, 0x1f801008, 0x1f80100c,
    0x1f801014, 0x1f801018, 0x1f801400, 0x1f801404,
    0x1f801408, 0x1f80140c, 0x1f801410, 0x1f801414,
    0x1f801418, 0x1f80141c, 0x1f801420
  }};
}

#endif
