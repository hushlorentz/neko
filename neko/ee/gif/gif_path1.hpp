#ifndef GIF_PATH1_HPP
#define GIF_PATH1_HPP

#include <cstdint>
#include <memory>

#include "gif_path_arbiter.hpp"
#include "vpu_xgkick_handler.hpp"

class VPU;

class GIFPath1Transfer : public VUXGKICKHandler
{
  public:
    explicit GIFPath1Transfer(GIFDecoder *decoder);
    explicit GIFPath1Transfer(GIFPathArbiter &arbiter);

    void attachVPU(VPU *attachedVPU);
    bool path1TransferActive() const override;
    void startPath1Transfer(std::uint16_t qwordAddress) override;
    void advancePath1Transfer() override;

    std::uint16_t currentQwordAddress() const;
    std::uint64_t transferredQuadwordCount() const;

  private:
    friend class NekoSaveStateCodec;

    std::unique_ptr<GIFPathArbiter> ownedArbiter;
    GIFPathArbiter *gifPathArbiter;
    VPU *vpu = nullptr;
    bool active = false;
    std::uint16_t qwordAddress = 0;
    std::uint64_t transferredQuadwords = 0;
};

#endif
