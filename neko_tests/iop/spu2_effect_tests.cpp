#include <array>
#include <stdexcept>
#include <string>

#include "catch.hpp"
#include "neko_system.hpp"
#include "save_state_diagnostics.hpp"
#include "save_state_mutation.hpp"

struct SPU2EffectTestAccess
{
  using Side = SPU2Controller::EffectSide;
  static auto &state(SPU2Controller &spu, std::size_t core) { return spu.effects.at(core); }
  static auto window(const SPU2Controller &spu, std::size_t core)
  {
    return spu.effectWindow(core);
  }
  static auto address(const SPU2Controller &spu, std::size_t core, std::int64_t offset)
  {
    return spu.effectTapAddress(spu.effectWindow(core), spu.effects.at(core).position, offset);
  }
  static auto offset(const SPU2Controller &spu, std::size_t core, std::size_t pair)
  {
    return spu.effectOffset(core, pair);
  }
  static void enableWrites(SPU2Controller &spu, std::size_t core)
  {
    spu.coreAttributes.at(core) |= 0x80;
  }
  static auto kernel(SPU2Controller &spu, std::size_t core, Side side, std::int16_t input)
  {
    return spu.processEffectSide(core, side, input);
  }
  static auto down(const SPU2Controller &spu, std::size_t core, Side side)
  {
    return spu.filterEffectInput(core, side);
  }
  static void push(SPU2Controller &spu, std::size_t core, SPU2StereoSample input)
  {
    spu.pushEffectInputs(core, input);
  }
  static auto up(SPU2Controller &spu, std::size_t core, Side side, std::int16_t input)
  {
    return spu.filterEffectOutput(core, side, input);
  }
  static auto &phase(SPU2Controller &spu) { return spu.inputReadPosition; }
};

namespace
{
  using Access = SPU2EffectTestAccess;
  using Side = Access::Side;

  void window(SPU2Controller &spu, std::size_t core, std::uint32_t start,
              std::uint16_t endHigh)
  {
    spu.writeEffectStartAddress(core * 2, static_cast<std::uint16_t>(start >> 16));
    spu.writeEffectStartAddress(core * 2 + 1, static_cast<std::uint16_t>(start));
    spu.writeEffectEndAddressHigh(core, endHigh);
  }

  void offset(SPU2Controller &spu, std::size_t core, std::size_t pair, std::uint32_t value)
  {
    spu.writeEffectOffset(core * 44 + pair * 2, static_cast<std::uint16_t>(value >> 16));
    spu.writeEffectOffset(core * 44 + pair * 2 + 1, static_cast<std::uint16_t>(value));
  }

  void coefficient(SPU2Controller &spu, std::size_t core, std::size_t index,
                   std::int16_t value)
  {
    spu.writeEffectCoefficient(core * 10 + index, static_cast<std::uint16_t>(value));
  }

  void seed(SPU2Controller &spu, std::size_t core)
  {
    auto &state = Access::state(spu, core);
    state.position = 6;
    for (std::size_t side = 0; side < 2; ++side)
      for (std::size_t slot = 0; slot < 39; ++slot)
      {
        state.downHistory[side][slot] = static_cast<std::int16_t>(
          -32768 + static_cast<int>(core) * 8000 + static_cast<int>(side) * 2000 +
          static_cast<int>(slot) * 37);
        state.upHistory[side][slot] = static_cast<std::int16_t>(
          32767 - static_cast<int>(core) * 9000 - static_cast<int>(side) * 3000 -
          static_cast<int>(slot) * 71);
      }
  }
}

TEST_CASE("SPU2 effect state is canonical without admitting active effects",
          "[iop][spu2][spu2-effect-kernel][save-state]")
{
  NekoSystem system;
  const auto snapshot = inspectSaveState(system.saveState());
  const auto *position = snapshot.find("system.iop.spu2.effects[0].position");
  REQUIRE(position != nullptr);
  REQUIRE(position->scalarValue == 0);
  REQUIRE_FALSE(system.spu2Controller().writeCoreAttribute(0, 0xc080));
  REQUIRE_FALSE(system.spu2Controller().writeCoreAttribute(1, 0xc080));
  const auto *tail = snapshot.find("system.iop.spu2.effectCoefficients[19].value");
  REQUIRE(tail != nullptr);
  for (std::size_t core = 0; core < 2; ++core)
  {
    const auto base = "system.iop.spu2.effects[" + std::to_string(core) + "].";
    const auto *cursor = snapshot.find(base + "position");
    REQUIRE(cursor != nullptr);
    REQUIRE(cursor->size == 4);
    REQUIRE(cursor->containerOffset == tail->containerOffset + 2 + core * 316);
    for (std::size_t history = 0; history < 2; ++history)
      for (std::size_t side = 0; side < 2; ++side)
        for (std::size_t slot = 0; slot < 39; ++slot)
        {
          const auto *sample = snapshot.find(base +
            (history == 0 ? "downHistory[" : "upHistory[") +
            std::to_string(side) + "][" + std::to_string(slot) + "].value");
          REQUIRE(sample != nullptr);
          REQUIRE(sample->size == 2);
          REQUIRE(sample->scalarValue == 0);
          REQUIRE(sample->containerOffset ==
            cursor->containerOffset + 4 + history * 156 + side * 78 + slot * 2);
        }
  }
}

TEST_CASE("SPU2 effect windows preserve raw bits and wrap full unsigned offsets",
          "[iop][spu2][spu2-effect-kernel]")
{
  NekoSystem system;
  auto &spu = system.spu2Controller();
  for (std::size_t core = 0; core < 2; ++core)
  {
    window(spu, core, 0xfff9, 0xfff0);
    spu.writeEffectStartAddress(core * 2, 0xfff0);
    REQUIRE(spu.readEffectStartAddress(core * 2) == 0xfff0);
    REQUIRE(spu.readEffectEndAddressHigh(core) == 0xfff0);
    REQUIRE(Access::window(spu, core).start == 0xfff9);
    REQUIRE(Access::window(spu, core).size == 7);
    Access::state(spu, core).position = 3;
    REQUIRE(Access::address(spu, core, UINT32_MAX) == 0xffff);
    REQUIRE(Access::address(spu, core, -8) == 0xfffb);
    REQUIRE(Access::address(spu, core, -INT64_C(4294967295)) == 0xfff9);
    for (std::size_t pair = 0; pair < 22; ++pair)
    {
      const auto address = static_cast<IOPAddress>(0x1f9002e4 + core * 0x400 + pair * 4);
      const auto raw = UINT32_MAX - static_cast<std::uint32_t>(pair * 0x10001);
      REQUIRE(system.iopBus().write16(address, raw >> 16).status == IOPBusStatus::Completed);
      REQUIRE(system.iopBus().write16(address + 2, raw & 0xffff).status == IOPBusStatus::Completed);
      REQUIRE(Access::offset(spu, core, pair) == raw);
      REQUIRE(system.iopBus().read16(address).value == raw >> 16);
      REQUIRE(system.iopBus().read16(address + 2).value == (raw & 0xffff));
    }
    window(spu, core, 0xffff, 0);
    REQUIRE(Access::window(spu, core).size == 1);
    REQUIRE(Access::address(spu, core, -INT64_C(4294967295)) == 0xffff);
    window(spu, core, 0, 0xffff);
    REQUIRE(Access::window(spu, core).size == 0x100000);
    REQUIRE(Access::address(spu, core, -1) == 0xfffff);
    window(spu, core, 0x10000, 0);
    REQUIRE(Access::window(spu, core).size == 0);
  }
}

TEST_CASE("SPU2 effect kernel latches aliased reads before its ordered writes",
          "[iop][spu2][spu2-effect-kernel]")
{
  for (std::size_t core = 0; core < 2; ++core)
    for (const auto side : {Side::Left, Side::Right})
      for (const auto writes : {false, true})
      {
        SPU2Controller spu;
        window(spu, core, 0xfffe, 0);
        spu.writeSoundRAMHalfword(0xfffe, 1000);
        spu.writeSoundRAMHalfword(0xffff, static_cast<std::uint16_t>(-1000));
        coefficient(spu, core, 0, 32767);
        coefficient(spu, core, 1, 32767);
        coefficient(spu, core, 7, 16384);
        coefficient(spu, core, side == Side::Left ? 8 : 9, 32767);
        if (writes)
          Access::enableWrites(spu, core);
        REQUIRE(Access::kernel(spu, core, side, 8192) == 1250);
        REQUIRE(spu.readSoundRAMHalfword(0xfffe) == (writes ? 500 : 1000));
        REQUIRE(spu.readSoundRAMHalfword(0xffff) == static_cast<std::uint16_t>(-1000));
      }
}

TEST_CASE("SPU2 effect kernel preserves unsaturated products and negative floor",
          "[iop][spu2][spu2-effect-kernel]")
{
  for (const auto corner : {false, true})
  {
    SPU2Controller spu;
    offset(spu, 0, 2, 2);
    offset(spu, 0, 10, 4);
    coefficient(spu, 0, 0, corner ? 16384 : 32767);
    coefficient(spu, 0, 8, corner ? -32768 : -1);
    Access::enableWrites(spu, 0);
    REQUIRE(Access::kernel(spu, 0, Side::Left, corner ? -32768 : 16384) == 0);
    const auto expected = static_cast<std::uint16_t>(corner ? 16384 : -1);
    REQUIRE(spu.readSoundRAMHalfword(2) == expected);
    REQUIRE(spu.readSoundRAMHalfword(4) == expected);
  }
}

TEST_CASE("SPU2 effect taps cross DIFF sides and retain both cores raw role mapping",
          "[iop][spu2][spu2-effect-kernel]")
{
  for (std::size_t core = 0; core < 2; ++core)
    for (const auto side : {Side::Left, Side::Right})
    {
      NekoSystem system;
      auto &spu = system.spu2Controller();
      const auto channel = side == Side::Left ? 0u : 1u;
      offset(spu, core, 2 + channel, 20);
      offset(spu, core, 10 + channel, 24);
      offset(spu, core, 8, 30);
      offset(spu, core, 9, 31);
      const auto diff = static_cast<IOPAddress>(0x1f900324 + core * 0x400);
      REQUIRE(system.iopBus().write16(diff, 0).status == IOPBusStatus::Completed);
      REQUIRE(system.iopBus().write16(diff + 2, 40).status == IOPBusStatus::Completed);
      REQUIRE(system.iopBus().write16(diff + 4, 0).status == IOPBusStatus::Completed);
      REQUIRE(system.iopBus().write16(diff + 6, 50).status == IOPBusStatus::Completed);
      spu.writeSoundRAMHalfword(30, 1111);
      spu.writeSoundRAMHalfword(31, 2222);
      spu.writeSoundRAMHalfword(40, 3333);
      spu.writeSoundRAMHalfword(50, 4444);
      coefficient(spu, core, 0, 32767);
      coefficient(spu, core, 5, 32767);
      Access::enableWrites(spu, core);
      Access::kernel(spu, core, side, 0);
      REQUIRE(spu.readSoundRAMHalfword(20) == (channel == 0 ? 1109 : 2220));
      REQUIRE(spu.readSoundRAMHalfword(24) == (channel == 0 ? 4442 : 3331));
    }
}

TEST_CASE("SPU2 effect comb and APF stages use wide sums and final-only clipping",
          "[iop][spu2][spu2-effect-kernel]")
{
  for (std::size_t core = 0; core < 2; ++core)
    for (const auto side : {Side::Left, Side::Right})
    {
      SPU2Controller spu;
      const auto channel = side == Side::Left ? 0u : 1u;
      const std::array<std::size_t, 4> combs{{4, 6, 12, 14}};
      for (std::size_t comb = 0; comb < combs.size(); ++comb)
      {
        offset(spu, core, combs[comb] + channel, static_cast<std::uint32_t>(100 + comb));
        spu.writeSoundRAMHalfword(static_cast<std::uint32_t>(100 + comb),
          static_cast<std::uint16_t>((comb + 1) * 1000));
        coefficient(spu, core, 1 + comb, 16384);
      }
      offset(spu, core, 0, 1);
      offset(spu, core, 1, 1);
      offset(spu, core, 18 + channel, 20);
      offset(spu, core, 20 + channel, 30);
      spu.writeSoundRAMHalfword(19, 100);
      spu.writeSoundRAMHalfword(29, static_cast<std::uint16_t>(-200));
      coefficient(spu, core, 6, 16384);
      coefficient(spu, core, 7, -16384);
      Access::enableWrites(spu, core);
      REQUIRE(Access::kernel(spu, core, side, 0) == -1438);
      REQUIRE(spu.readSoundRAMHalfword(20) == 4950);
      REQUIRE(spu.readSoundRAMHalfword(30) == 2475);
      for (std::size_t comb = 0; comb < combs.size(); ++comb)
      {
        spu.writeSoundRAMHalfword(static_cast<std::uint32_t>(100 + comb), 32767);
        coefficient(spu, core, 1 + comb, 32767);
      }
      coefficient(spu, core, 6, 0);
      coefficient(spu, core, 7, 0);
      Access::kernel(spu, core, side, 0);
      REQUIRE(spu.readSoundRAMHalfword(20) == 32767);
      for (std::size_t comb = 0; comb < combs.size(); ++comb)
        spu.writeSoundRAMHalfword(static_cast<std::uint32_t>(100 + comb), 0x8000);
      Access::kernel(spu, core, side, 0);
      REQUIRE(spu.readSoundRAMHalfword(20) == 0x8000);
      coefficient(spu, core, 6, -32768);
      coefficient(spu, core, 7, -32768);
      spu.writeSoundRAMHalfword(19, 32767);
      spu.writeSoundRAMHalfword(29, 32767);
      REQUIRE(Access::kernel(spu, core, side, 0) == -32768);
      REQUIRE(spu.readSoundRAMHalfword(20) == 0x8000);
      REQUIRE(spu.readSoundRAMHalfword(30) == 32767);
      for (std::size_t comb = 0; comb < combs.size(); ++comb)
        spu.writeSoundRAMHalfword(static_cast<std::uint32_t>(100 + comb), 32767);
      REQUIRE(Access::kernel(spu, core, side, 0) == 32767);
      REQUIRE(spu.readSoundRAMHalfword(20) == 32767);
      REQUIRE(spu.readSoundRAMHalfword(30) == 0x8000);
    }
}

TEST_CASE("SPU2 effect FIR primitives preserve delayed down and causal zero-stuffed up histories",
          "[iop][spu2][spu2-effect-kernel]")
{
  SPU2Controller spu;
  const std::array<std::int16_t, 39> expectedDown{{
    -1,0,1,0,-10,0,34,0,-103,0,265,0,-616,0,1331,0,-2960,0,
    10245,16383,10245,0,-2960,0,1331,0,-616,0,265,0,-103,0,34,0,-10,0,1,0,-1
  }};
  const std::array<std::int16_t, 39> expectedUp{{
    2,0,-4,0,20,0,-70,0,206,0,-532,0,1232,0,-2664,0,5920,0,
    -20492,-32767,-20492,0,5920,0,-2664,0,1232,0,-532,0,206,0,-70,0,20,0,-4,0,2
  }};
  for (std::size_t tick = 0; tick < 42; ++tick)
  {
    REQUIRE(Access::down(spu, 0, Side::Left) ==
      (tick == 0 || tick > 39 ? 0 : expectedDown[tick - 1]));
    REQUIRE(Access::down(spu, 0, Side::Right) == 0);
    const auto output = Access::up(spu, 0, Side::Left, tick == 0 ? -32768 : 0);
    REQUIRE(output.left == (tick < 39 ? expectedUp[tick] : 0));
    REQUIRE(output.right == 0);
    Access::push(spu, 0, {static_cast<std::int16_t>(tick == 0 ? 32767 : 0), 0});
  }
  Access::state(spu, 0).upHistory[1][0] = 1000;
  const auto tail = Access::up(spu, 0, Side::Left, 0);
  REQUIRE(Access::state(spu, 0).upHistory[1][0] == 0);
  REQUIRE(Access::state(spu, 0).upHistory[1][1] == 1000);
  REQUIRE(tail.right == 0);
  const auto next = Access::up(spu, 0, Side::Right, 32767);
  REQUIRE(next.right == -2);
  Access::push(spu, 1, {1234, -2345});
  REQUIRE(Access::state(spu, 1).downHistory[0][0] == 1234);
  REQUIRE(Access::state(spu, 1).downHistory[1][0] == -2345);
}

TEST_CASE("SPU2 effect FIR MACs exceed signed32 range without overflowing",
          "[iop][spu2][spu2-effect-kernel]")
{
  SPU2Controller spu;
  const std::array<int, 39> signs{{
    -1,0,1,0,-1,0,1,0,-1,0,1,0,-1,0,1,0,-1,0,
    1,1,1,0,-1,0,1,0,-1,0,1,0,-1,0,1,0,-1,0,1,0,-1
  }};
  for (std::size_t slot = 0; slot < 39; ++slot)
    Access::state(spu, 0).downHistory[0][slot] = signs[slot] < 0 ? -32768 : 32767;
  REQUIRE(Access::down(spu, 0, Side::Left) == 32767);
  for (std::size_t slot = 1; slot < 39; ++slot)
    Access::state(spu, 0).upHistory[0][slot - 1] = signs[slot] < 0 ? -32768 : 32767;
  REQUIRE(Access::up(spu, 0, Side::Left, -32768).left == 32767);
}

TEST_CASE("SPU2 effect address programming resets only the selected cursor and owner reset clears histories",
          "[iop][spu2][spu2-effect-kernel]")
{
  NekoSystem system;
  auto &spu = system.spu2Controller();
  for (std::size_t core = 0; core < 2; ++core)
    window(spu, core, 0xfff9, 0);
  for (std::size_t core = 0; core < 2; ++core)
    seed(spu, core);
  Access::phase(spu) = 17;
  spu.writeSoundRAMHalfword(0x8000, 0x1234);
  for (std::size_t core = 0; core < 2; ++core)
  {
    const auto prior = Access::state(spu, core);
    const auto peer = Access::state(spu, 1 - core);
    for (const auto half : {0u, 1u, 2u, 3u, 4u})
    {
      Access::state(spu, core).position = 6;
      if (half < 2)
        spu.writeEffectStartAddress(core * 2 + half, spu.readEffectStartAddress(core * 2 + half));
      else if (half < 4)
        spu.writeEffectEndAddressHigh(core, half == 2 ? 0 : 0xfff0);
      else
        spu.writeEffectStartAddress(core * 2, 0xfff0);
      REQUIRE(Access::state(spu, core).position == 0);
      REQUIRE(Access::state(spu, core).downHistory == prior.downHistory);
      REQUIRE(Access::state(spu, core).upHistory == prior.upHistory);
      REQUIRE(Access::state(spu, 1 - core).position == peer.position);
      REQUIRE(Access::state(spu, 1 - core).downHistory == peer.downHistory);
      REQUIRE(Access::state(spu, 1 - core).upHistory == peer.upHistory);
      REQUIRE(Access::phase(spu) == 17);
      REQUIRE(spu.readSoundRAMHalfword(0x8000) == 0x1234);
    }
    Access::state(spu, core).position = 6;
    offset(spu, core, 0, UINT32_MAX);
    REQUIRE(Access::state(spu, core).position == 6);
  }
  const auto state = system.saveState();
  system.iopBus().reset();
  system.iopCore().reset();
  REQUIRE(Access::state(spu, 0).position == 6);
  REQUIRE(Access::state(spu, 1).position == 6);
  const auto snapshot = inspectSaveState(state);
  const auto *sample = snapshot.find("system.iop.spu2.effects[1].upHistory[1][38].value");
  REQUIRE(sample != nullptr);
  REQUIRE(sample->scalarValue == static_cast<std::uint16_t>(Access::state(spu, 1).upHistory[1][38]));
  spu.reset();
  REQUIRE(Access::phase(spu) == 0);
  for (std::size_t core = 0; core < 2; ++core)
  {
    REQUIRE(Access::state(spu, core).position == 0);
    for (const auto &history : Access::state(spu, core).downHistory)
      for (const auto value : history)
        REQUIRE(value == 0);
    for (const auto &history : Access::state(spu, core).upHistory)
      for (const auto value : history)
        REQUIRE(value == 0);
  }
}

TEST_CASE("SPU2 effect state round trips at either phase and remains unwired from sample ticks",
          "[iop][spu2][spu2-effect-kernel][save-state]")
{
  for (const auto phase : {0u, 1u})
  {
    NekoSystem source;
    auto &spu = source.spu2Controller();
    for (std::size_t core = 0; core < 2; ++core)
    {
      window(spu, core, 0xfff9, 0);
      seed(spu, core);
    }
    Access::phase(spu) = phase;
    const auto saved = source.saveState();
    NekoSystem restored;
    restored.loadState(saved);
    const bool equal = restored.saveState() == saved;
    REQUIRE(equal);
    REQUIRE(restored.iopStateHash() == source.iopStateHash());
    for (std::size_t core = 0; core < 2; ++core)
    {
      const auto prior = Access::state(spu, core);
      const auto side = phase == 0 ? Side::Left : Side::Right;
      REQUIRE(Access::down(spu, core, side) ==
        Access::down(restored.spu2Controller(), core, side));
      const auto cold = Access::up(spu, core, side, 12345);
      const auto loaded = Access::up(restored.spu2Controller(), core, side, 12345);
      REQUIRE(cold.left == loaded.left);
      REQUIRE(cold.right == loaded.right);
      REQUIRE(Access::state(spu, core).downHistory == prior.downHistory);
    }
    const auto prior = Access::state(spu, 0);
    for (unsigned pulse = 0; pulse < 768; ++pulse)
      spu.advanceSampleClock();
    REQUIRE(Access::state(spu, 0).position == prior.position);
    REQUIRE(Access::state(spu, 0).downHistory == prior.downHistory);
    REQUIRE(Access::state(spu, 0).upHistory == prior.upHistory);
  }
}

TEST_CASE("SPU2 effect codec rejects out of window cursors with staged adoption",
          "[iop][spu2][spu2-effect-kernel][save-state]")
{
  NekoSystem source;
  NekoSystem target;
  target.spu2Controller().writeSoundRAMHalfword(0x1234, 0x5aa5);
  const auto before = target.saveState();
  for (const auto invalidWindow : {false, true})
    for (std::size_t core = 0; core < 2; ++core)
    {
      window(source.spu2Controller(), core, invalidWindow ? 0x10000 : 0xfff9, 0);
      SaveStateMutation broken(source.saveState());
      broken.writeScalar("system.iop.spu2.effects[" + std::to_string(core) + "].position",
        invalidWindow ? 1 : 7);
      broken.refreshChecksum();
      REQUIRE_THROWS_AS(inspectSaveState(broken.bytes()), std::invalid_argument);
      REQUIRE_THROWS_AS(target.loadState(broken.bytes()), std::invalid_argument);
      const bool unchanged = target.saveState() == before;
      REQUIRE(unchanged);
    }
  auto old = source.saveState();
  old[8] = 122;
  REQUIRE_THROWS_AS(inspectSaveState(old), std::invalid_argument);
  REQUIRE_THROWS_AS(target.loadState(old), std::invalid_argument);
  const bool unchanged = target.saveState() == before;
  REQUIRE(unchanged);
}
