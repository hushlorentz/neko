#include "catch.hpp"
#include "dmac_channel_state.hpp"

TEST_CASE("DMAC channel state owns shared transfer transitions")
{
  DMACChannelState state(
    "Test",
    DMACChannelDirectionPolicy::Unrestricted);
  state.writeTagAddress(0x1000);
  state.writeChannelControl(
    DMACChannelControl::CHAIN_MODE |
    DMACChannelControl::TAG_INTERRUPT_ENABLE |
    DMACChannelControl::START);

  REQUIRE(
    state.acceptSourceChainTag(
      1u |
        (static_cast<std::uint32_t>(DMACTagID::Call) << 28),
      0x2000) ==
    DMACChannelTransition::Continue);
  REQUIRE(state.memoryAddress() == 0x1010);
  REQUIRE(state.tagAddress() == 0x2000);
  REQUIRE(state.addressStack(0) == 0x1020);
  REQUIRE(
    (state.channelControl() &
     DMACChannelControl::ADDRESS_STACK_MASK) ==
    (1u << 4));

  REQUIRE(
    state.acceptQuadword() ==
    DMACChannelTransition::Continue);
  REQUIRE(state.memoryAddress() == 0x1020);
  REQUIRE(state.quadwordCount() == 0);
}

TEST_CASE("DMAC channel state handles source-chain boundaries")
{
  SECTION("Reference tags use the external address")
  {
    for (DMACTagID id : {
      DMACTagID::Reference,
      DMACTagID::ReferenceStall})
    {
      DMACChannelState state(
        "Test",
        DMACChannelDirectionPolicy::Unrestricted);
      state.writeTagAddress(0x1000);
      state.writeChannelControl(
        DMACChannelControl::CHAIN_MODE |
        DMACChannelControl::START);

      REQUIRE(
        state.acceptSourceChainTag(
          2u | (static_cast<std::uint32_t>(id) << 28),
          0x2004) ==
        DMACChannelTransition::Continue);
      REQUIRE(state.memoryAddress() == 0x2000);
      REQUIRE(state.tagAddress() == 0x1010);
      REQUIRE(state.quadwordCount() == 2);
    }
  }

  SECTION("Return with an empty stack terminates")
  {
    DMACChannelState state(
      "Test",
      DMACChannelDirectionPolicy::Unrestricted);
    state.writeTagAddress(0x1000);
    state.writeChannelControl(
      DMACChannelControl::CHAIN_MODE |
      DMACChannelControl::START);

    REQUIRE(
      state.acceptSourceChainTag(
        static_cast<std::uint32_t>(DMACTagID::Return) << 28,
        0) ==
      DMACChannelTransition::Complete);
    REQUIRE(state.memoryAddress() == 0x1010);
    REQUIRE(state.tagAddress() == 0x1010);
  }

  SECTION("Call with a full stack terminates")
  {
    DMACChannelState state(
      "Test",
      DMACChannelDirectionPolicy::Unrestricted);
    state.writeTagAddress(0x1000);
    state.writeChannelControl(
      DMACChannelControl::CHAIN_MODE |
      (2u << 4) |
      DMACChannelControl::START);

    REQUIRE(
      state.acceptSourceChainTag(
        static_cast<std::uint32_t>(DMACTagID::Call) << 28,
        0x2000) ==
      DMACChannelTransition::Complete);
    state.completeTransfer();
    REQUIRE_FALSE(state.active());
  }
}

TEST_CASE("DMAC channel state enforces register policy")
{
  DMACChannelState vif1(
    "VIF1",
    DMACChannelDirectionPolicy::FromMemory);
  REQUIRE_THROWS_WITH(
    vif1.writeChannelControl(DMACChannelControl::START),
    "VIF1 DMAC supports only transfers from memory.");

  DMACChannelState gif(
    "GIF",
    DMACChannelDirectionPolicy::Unrestricted);
  gif.writeChannelControl(DMACChannelControl::START);
  REQUIRE_THROWS_WITH(
    gif.writeMemoryAddress(0x1000),
    "GIF DMAC channel registers cannot change while active.");
  REQUIRE(
    gif.writeChannelControl(DMACChannelControl::START) ==
    DMACControlWriteEffect::None);
  REQUIRE(
    gif.writeChannelControl(0) ==
    DMACControlWriteEffect::ResetContinuation);
  REQUIRE_FALSE(gif.active());
}
