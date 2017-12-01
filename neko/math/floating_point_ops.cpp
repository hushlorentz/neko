#include "floating_point_ops.hpp"

float convertFromIEEE(float f)
{
    float_cast fu;
    fu.f = f;
    
    if (fu.parts.exponent == 0xff)
    {
        fu.parts.exponent = 0x80;
        fu.parts.mantissa = 0x2;
        
        return fu.f;
    }
    if (fu.parts.exponent == 0 && fu.parts.mantissa > 0)
    {
        return 0;
    }
    
    return f;
}

float addFP(float f1, float f2)
{
    return convertFromIEEE(f1 + f2);
}

float mulFP(float f1, float f2)
{
    return convertFromIEEE(f1 * f2);
}

float divFP(float f1, float f2)
{
    return convertFromIEEE(f1 / f2);
}

float subFP(float f1, float f2)
{
    return convertFromIEEE(f1 - f2);
}
