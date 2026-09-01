#ifndef SYSTEM_INTERFACES_HPP
#define SYSTEM_INTERFACES_HPP

#include <cstdint>
#include <vector>

#include "gs_display.hpp"

namespace NekoButton
{
  constexpr std::uint16_t SELECT = 1u << 0;
  constexpr std::uint16_t L3 = 1u << 1;
  constexpr std::uint16_t R3 = 1u << 2;
  constexpr std::uint16_t START = 1u << 3;
  constexpr std::uint16_t UP = 1u << 4;
  constexpr std::uint16_t RIGHT = 1u << 5;
  constexpr std::uint16_t DOWN = 1u << 6;
  constexpr std::uint16_t LEFT = 1u << 7;
  constexpr std::uint16_t L2 = 1u << 8;
  constexpr std::uint16_t R2 = 1u << 9;
  constexpr std::uint16_t L1 = 1u << 10;
  constexpr std::uint16_t R1 = 1u << 11;
  constexpr std::uint16_t TRIANGLE = 1u << 12;
  constexpr std::uint16_t CIRCLE = 1u << 13;
  constexpr std::uint16_t CROSS = 1u << 14;
  constexpr std::uint16_t SQUARE = 1u << 15;
}

struct NekoInputState
{
  std::uint16_t buttons = 0;
  std::uint8_t leftStickX = 0x80;
  std::uint8_t leftStickY = 0x80;
  std::uint8_t rightStickX = 0x80;
  std::uint8_t rightStickY = 0x80;
};

struct NekoAudioFrame
{
  static constexpr std::uint32_t SAMPLE_RATE = 48000;
  static constexpr std::uint8_t CHANNEL_COUNT = 2;

  std::uint32_t sampleRate = SAMPLE_RATE;
  std::uint8_t channelCount = CHANNEL_COUNT;
  std::vector<std::int16_t> interleavedSamples;
};

struct NekoFrameResult
{
  std::uint64_t masterCycles = 0;
  std::uint64_t presentationBoundary = 0;
  std::uint64_t videoHash = 0;
  GSPresentation video;
  NekoAudioFrame audio;
};

#endif
