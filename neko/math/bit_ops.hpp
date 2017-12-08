#ifndef BIT_OPS_H
#define BIT_OPS_H

#include <cstdint>

#define setFlag(value, flag) (value |= flag)
#define unsetFlag(value, flag) (value &= ~flag)
#define hasFlag(value, flag) ((value & flag) == flag)

#endif
