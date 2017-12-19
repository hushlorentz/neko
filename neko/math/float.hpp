#ifndef FLOAT_HPP
#define FLOAT_HPP

#include <cstdint>

#define FLOAT_EXPONENT_BIAS 127
#define MAX_MANTISSA 0x7fffff
#define MAX_EXPONENT 0xff

class Float
{
  public:
    Float(float value);
    Float(bool sign, uint8_t exponent, uint32_t mantissa);
    bool sign;
    uint32_t mantissa;
    uint8_t exponent;

    float value();
};

#endif
