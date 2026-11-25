#include "gs_draw_diagnostics.hpp"

#include <cassert>
#include <locale>
#include <sstream>
#include <stdexcept>

StartupGSDrawCapture::StartupGSDrawCapture(
  const NekoSystem &system, std::size_t limit) :
  system(system), drawLimit(limit)
{
  if (limit == 0 || limit > MAX_DRAWS)
  {
    throw std::invalid_argument("GS draw limit must be from 1 to 4096.");
  }
  captured.reserve(limit);
}

bool StartupGSDrawCapture::captureDraw() noexcept
{
  ++observed;
  return captured.size() < drawLimit;
}

void StartupGSDrawCapture::observe(const GSDrawObservation &draw) noexcept
{
  assert(captured.size() < drawLimit);
  captured.push_back({system.masterClockScheduler().currentCycle(), draw});
}

const std::vector<StartupGSDraw> &StartupGSDrawCapture::draws() const
{
  return captured;
}

std::size_t StartupGSDrawCapture::limit() const { return drawLimit; }
std::uint64_t StartupGSDrawCapture::observedDraws() const { return observed; }

namespace
{
  const char *outcomeName(GSFragmentOutcome outcome)
  {
    switch (outcome)
    {
      case GSFragmentOutcome::NotReached: return "not_reached";
      case GSFragmentOutcome::AlphaRejected: return "alpha_rejected";
      case GSFragmentOutcome::DestinationAlphaRejected: return "destination_alpha_rejected";
      case GSFragmentOutcome::DepthRejected: return "depth_rejected";
      case GSFragmentOutcome::Written: return "written";
    }
    throw std::invalid_argument("Unknown GS fragment observation outcome.");
  }

  std::uint32_t colorBits(const GSColor &color)
  {
    return std::uint32_t(color.red) | (std::uint32_t(color.green) << 8) |
      (std::uint32_t(color.blue) << 16) | (std::uint32_t(color.alpha) << 24);
  }

  void writeDraw(std::ostream &out, const StartupGSDraw &entry)
  {
    const auto &draw = entry.draw;
    if (static_cast<unsigned>(draw.primitive.type) > 6 ||
        draw.primitive.context > 1 || draw.sampleCount > draw.samples.size())
    {
      throw std::invalid_argument("Invalid GS draw observation.");
    }
    out << "{\"scheduler_cycle\":" << entry.schedulerCycle <<
      ",\"completed\":" << draw.completed <<
      ",\"primitive\":{\"type\":" << static_cast<unsigned>(draw.primitive.type) <<
      ",\"context\":" << unsigned(draw.primitive.context) <<
      ",\"gouraud\":" << draw.primitive.gouraudShading <<
      ",\"textured\":" << draw.primitive.textureMapping <<
      ",\"fogging\":" << draw.primitive.fogging <<
      ",\"blending\":" << draw.primitive.alphaBlending <<
      ",\"antialiasing\":" << draw.primitive.antialiasing <<
      ",\"fixed_uv\":" << draw.primitive.fixedTextureCoordinates <<
      ",\"fixed_fragment\":" << draw.primitive.fixedFragmentValue <<
      "},\"registers\":[";
    bool comma = false;
    for (const auto &reg : draw.registers)
    {
      if (comma) { out << ','; }
      comma = true;
      out << "{\"address\":" << unsigned(reg.address) << ",\"value\":" << reg.data << '}';
    }
    out << "],\"vertices\":[";
    const auto type = draw.primitive.type;
    const std::size_t count = type == GSPrimitiveType::Point ? 1 :
      (type == GSPrimitiveType::Sprite || type == GSPrimitiveType::Line ||
       type == GSPrimitiveType::LineStrip) ? 2 : 3;
    for (std::size_t index = 0; index < count; ++index)
    {
      if (index != 0) { out << ','; }
      const auto &vertex = draw.vertices[index];
      const auto &texture = draw.textureCoordinates[index];
      out << "{\"x\":" << vertex.x << ",\"y\":" << vertex.y << ",\"z\":" << vertex.z <<
        ",\"rgba\":" << colorBits(draw.colors[index]) << ",\"q\":" << draw.colors[index].q <<
        ",\"s\":" << texture.s << ",\"t\":" << texture.t <<
        ",\"u\":" << texture.u << ",\"v\":" << texture.v << '}';
    }
    out << "],\"counts\":{\"fragments\":" << draw.fragments <<
      ",\"textured\":" << draw.texturedFragments <<
      ",\"nonblack_texels\":" << draw.nonblackTexels <<
      ",\"nonblack_sources\":" << draw.nonblackSources <<
      ",\"alpha_rejected\":" << draw.alphaRejected <<
      ",\"destination_alpha_rejected\":" << draw.destinationAlphaRejected <<
      ",\"depth_rejected\":" << draw.depthRejected <<
      ",\"written\":" << draw.written <<
      ",\"nonblack_writes\":" << draw.nonblackWrites << "},\"samples\":[";
    for (std::size_t index = 0; index < draw.sampleCount; ++index)
    {
      if (index != 0) { out << ','; }
      const auto &sample = draw.samples[index];
      out << "{\"x\":" << sample.x << ",\"y\":" << sample.y <<
        ",\"vertex_color\":" << sample.vertexColor <<
        ",\"texture_sampled\":" << sample.textureSampled <<
        ",\"u\":" << sample.u << ",\"v\":" << sample.v << ",\"texel\":" << sample.texel <<
        ",\"source_color\":" << sample.sourceColor << ",\"source_depth\":" << sample.sourceDepth <<
        ",\"destination_color\":";
      if (sample.destinationAvailable) { out << sample.destinationColor; } else { out << "null"; }
      out << ",\"destination_depth\":";
      if (sample.depthAvailable) { out << sample.destinationDepth; } else { out << "null"; }
      out << ",\"blended\":" << sample.blended << ",\"output_color\":";
      if (sample.storedColorAvailable) { out << sample.outputColor; } else { out << "null"; }
      out << ",\"stored_color\":";
      if (sample.storedColorAvailable) { out << sample.storedColor; } else { out << "null"; }
      out << ",\"framebuffer_word_address\":";
      if (sample.destinationAvailable) { out << sample.framebufferWordAddress; } else { out << "null"; }
      out << ",\"outcome\":\"" << outcomeName(sample.outcome) << "\"}";
    }
    out << "]}";
  }
}

void writeStartupGSDrawCaptureJsonLine(
  std::ostream &output, const StartupGSDrawCapture &capture)
{
  std::ostringstream record;
  record.imbue(std::locale::classic());
  record << std::boolalpha <<
    "{\"type\":\"startup_gs_draw_capture\",\"draw_limit\":" << capture.limit() <<
    ",\"observed_draws\":" << capture.observedDraws() <<
    ",\"truncated\":" << (capture.observedDraws() > capture.draws().size()) << ",\"draws\":[";
  bool comma = false;
  for (const auto &draw : capture.draws())
  {
    if (comma) { record << ','; }
    comma = true;
    writeDraw(record, draw);
  }
  record << "]}\n";
  output << record.str();
}
