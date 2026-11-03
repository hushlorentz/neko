#include <stdexcept>

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
  for (const auto control : {0u, 0x123400e4u, 1u, 0u})
  {
    REQUIRE(vif1.writeChannelControl(control) ==
      DMACControlWriteEffect::ResetContinuation);
    REQUIRE(vif1.channelControl() == control);
    REQUIRE_FALSE(vif1.active());
    REQUIRE_THROWS_WITH(
      vif1.writeChannelControl(DMACChannelControl::START),
      "VIF1 DMAC supports only transfers from memory.");
    REQUIRE(vif1.channelControl() == control);
  }
  vif1.writeChannelControl(
    DMACChannelControl::FROM_MEMORY | DMACChannelControl::START);
  REQUIRE_THROWS_AS(vif1.writeChannelControl(
    DMACChannelControl::FROM_MEMORY | DMACChannelControl::START |
      DMACChannelControl::TAG_INTERRUPT_ENABLE), std::logic_error);
  REQUIRE(vif1.channelControl() ==
    (DMACChannelControl::FROM_MEMORY | DMACChannelControl::START));
  REQUIRE(vif1.writeChannelControl(DMACChannelControl::FROM_MEMORY) ==
    DMACControlWriteEffect::ResetContinuation);
  REQUIRE(vif1.writeChannelControl(0) ==
    DMACControlWriteEffect::ResetContinuation);
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

TEST_CASE("DMAC active stops retain programming and discard continuation",
  "[ee][dma][active-stop]")
{
  for (const auto profile : {DMACChannelProfile::VIFGIF,
    DMACChannelProfile::IPU, DMACChannelProfile::SIF0, DMACChannelProfile::SIF1})
    for (const auto stop : {0u, 0x123400e4u})
    {
      CAPTURE(profile);
      CAPTURE(stop);
      DMACChannelState state("Test",
        DMACChannelDirectionPolicy::FromMemory, profile);
      state.writeMemoryAddress(0x4000);
      state.writeQuadwordCount(profile == DMACChannelProfile::SIF0 ? 0 : 3);
      if (profile != DMACChannelProfile::SIF0)
        state.writeTagAddress(0x1000);
      if (profile == DMACChannelProfile::VIFGIF)
        state.writeAddressStack(1, 0x6000);
      state.writeChannelControl(profile == DMACChannelProfile::IPU ?
        0x181 : 0x185);
      if (profile == DMACChannelProfile::SIF0)
        REQUIRE(state.acceptDestinationChainTag(0xf0000003, 0x4000) ==
          DMACChannelTransition::Continue);
      else if (profile != DMACChannelProfile::IPU)
        REQUIRE(state.acceptSourceChainTag(
          profile == DMACChannelProfile::VIFGIF ? 0xd0000003 : 0xf0000003,
          0x2000) == DMACChannelTransition::Continue);
      REQUIRE(state.acceptQuadword() == DMACChannelTransition::Continue);
      const auto retained = state.channelControl() & ~DMACChannelControl::START;
      const auto address = state.memoryAddress();
      const auto tag = state.tagAddress();
      REQUIRE(state.writeChannelControl(stop) ==
        DMACControlWriteEffect::ResetContinuation);
      REQUIRE(state.channelControl() == retained);
      REQUIRE(state.memoryAddress() == address);
      REQUIRE(state.quadwordCount() == 2);
      REQUIRE(state.tagAddress() == tag);
      REQUIRE_FALSE(state.continuation().terminateAfterPacket);
      REQUIRE_FALSE(state.continuation().destinationPacketAccepted);
      REQUIRE(state.continuation().sliceQuadwordsRemaining == 0);
      if (profile == DMACChannelProfile::VIFGIF)
      {
        REQUIRE(state.addressStack(0) == 0x1040);
        REQUIRE(state.addressStack(1) == 0x6000);
        REQUIRE((state.channelControl() &
          DMACChannelControl::ADDRESS_STACK_MASK) == 0x10);
      }
    }
}

TEST_CASE("DMAC malformed active stops reject before continuation mutation",
  "[ee][dma][active-stop]")
{
  DMACChannelState state("VIF1", DMACChannelDirectionPolicy::FromMemory);
  state.writeMemoryAddress(0x4000);
  state.writeQuadwordCount(3);
  state.writeChannelControl(0x195);
  REQUIRE(state.acceptQuadword() == DMACChannelTransition::Continue);
  for (const auto stop : {2u, 8u, 12u, 0x30u})
  {
    CAPTURE(stop);
    REQUIRE_THROWS_AS(state.writeChannelControl(stop), std::invalid_argument);
    REQUIRE(state.channelControl() == 0x195);
    REQUIRE(state.memoryAddress() == 0x4010);
    REQUIRE(state.quadwordCount() == 2);
    REQUIRE(state.continuation().sliceQuadwordsRemaining == 2);
  }
  REQUIRE_THROWS_AS(state.writeChannelControl(0x101), std::logic_error);
  REQUIRE(state.writeChannelControl(0x195) == DMACControlWriteEffect::None);
  REQUIRE(state.continuation().sliceQuadwordsRemaining == 2);
}
