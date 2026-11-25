#ifndef GS_DRAW_DIAGNOSTICS_HPP
#define GS_DRAW_DIAGNOSTICS_HPP

#include "neko_system.hpp"

#include <iosfwd>
#include <vector>

struct StartupGSDraw
{
  std::uint64_t schedulerCycle = 0;
  GSDrawObservation draw;
};

class StartupGSDrawCapture final : public GSDrawObserver
{
  public:
    static constexpr std::size_t MAX_DRAWS = 4096;
    StartupGSDrawCapture(const NekoSystem &system, std::size_t limit);
    bool captureDraw() noexcept override;
    void observe(const GSDrawObservation &draw) noexcept override;
    const std::vector<StartupGSDraw> &draws() const;
    std::size_t limit() const;
    std::uint64_t observedDraws() const;

  private:
    const NekoSystem &system;
    std::size_t drawLimit;
    std::uint64_t observed = 0;
    std::vector<StartupGSDraw> captured;
};

void writeStartupGSDrawCaptureJsonLine(
  std::ostream &output, const StartupGSDrawCapture &capture);

#endif
