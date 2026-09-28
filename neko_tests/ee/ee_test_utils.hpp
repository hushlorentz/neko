#ifndef NEKO_TESTS_EE_EE_TEST_UTILS_HPP
#define NEKO_TESTS_EE_EE_TEST_UTILS_HPP

#include "ee_core.hpp"

inline void mapLowKusegForTest(EECore *core)
{
  core->setTLBEntry(
    0,
    {
      EECOP0PageMask::SIZE_16_KIB,
      0,
      {UINT32_C(0x0000001f)},
      {UINT32_C(0x0000011f)}
    });
}

#endif
