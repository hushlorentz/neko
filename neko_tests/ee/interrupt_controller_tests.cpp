#include "catch.hpp"
#include "interrupt_controller.hpp"

TEST_CASE("EE Interrupt Controller Tests")
{
  EEInterruptController interrupts;

  SECTION("Pending sources are visible independently of masking")
  {
    interrupts.setSource(EEInterruptSource::VIF1, true);

    REQUIRE(
      interrupts.status() ==
      EEInterruptSource::mask(EEInterruptSource::VIF1));
    REQUIRE_FALSE(interrupts.interruptPending());

    interrupts.toggleMask(
      EEInterruptSource::mask(EEInterruptSource::VIF1));

    REQUIRE(interrupts.interruptPending());
  }

  SECTION("INTC mask writes toggle selected source bits")
  {
    const std::uint32_t sources =
      EEInterruptSource::mask(EEInterruptSource::VIF0) |
      EEInterruptSource::mask(EEInterruptSource::VIF1);
    interrupts.toggleMask(sources);
    interrupts.toggleMask(
      EEInterruptSource::mask(EEInterruptSource::VIF0));

    REQUIRE(
      interrupts.mask() ==
      EEInterruptSource::mask(EEInterruptSource::VIF1));
  }

  SECTION("INTC status writes acknowledge selected sources")
  {
    interrupts.setSource(EEInterruptSource::VIF0, true);
    interrupts.setSource(EEInterruptSource::VIF1, true);
    interrupts.acknowledge(
      EEInterruptSource::mask(EEInterruptSource::VIF0));

    REQUIRE(
      interrupts.status() ==
      EEInterruptSource::mask(EEInterruptSource::VIF1));
  }
}
