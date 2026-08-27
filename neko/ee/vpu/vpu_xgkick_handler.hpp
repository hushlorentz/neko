#ifndef VPU_XGKICK_HANDLER_HPP
#define VPU_XGKICK_HANDLER_HPP

#include <cstdint>

class VUXGKICKHandler
{
  public:
    virtual ~VUXGKICKHandler() = default;
    virtual bool path1TransferActive() const = 0;
    virtual void startPath1Transfer(std::uint16_t qwordAddress) = 0;
};

#endif
