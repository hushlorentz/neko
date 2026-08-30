#ifndef VPU_VIF_REGISTER_SOURCE_HPP
#define VPU_VIF_REGISTER_SOURCE_HPP

#include <cstdint>

class VUVIFRegisterSource
{
  public:
    virtual ~VUVIFRegisterSource() = default;
    virtual std::uint16_t top() const = 0;
    virtual std::uint16_t itop() const = 0;
};

#endif
