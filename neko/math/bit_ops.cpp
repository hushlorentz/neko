#include "bit_ops.hpp"

bool hasFlag(uint64_t value, uint64_t flag)
{
    return (value & flag) == flag;
}
