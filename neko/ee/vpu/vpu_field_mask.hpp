#ifndef VPU_FIELD_MASK_H
#define VPU_FIELD_MASK_H

#include <cstdint>

inline std::uint8_t vpuFieldMaskFromEncoding(std::uint8_t encodedMask)
{
  return static_cast<std::uint8_t>(
    ((encodedMask & 0x1) << 3) |
    ((encodedMask & 0x2) << 1) |
    ((encodedMask & 0x4) >> 1) |
    ((encodedMask & 0x8) >> 3));
}

inline std::uint8_t vpuFieldMaskToEncoding(std::uint8_t fieldMask)
{
  return vpuFieldMaskFromEncoding(fieldMask);
}

#endif
