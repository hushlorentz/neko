#include <array>
#include <cstdint>
#include <stdexcept>
#include <type_traits>

#include "catch.hpp"
#include "iop_interrupt_controller.hpp"

static_assert(std::is_final<IOPInterruptController>::value,
  "IOP interrupt ownership must remain concrete.");
static_assert(std::is_trivially_copyable<IOPInterruptController>::value,
  "IOP interrupt state must remain fixed-capacity value state.");

namespace
{
  const std::array<IOPInterruptSource, 26> sources = {{
    IOPInterruptSource::VBlankStart,
    IOPInterruptSource::SBUS,
    IOPInterruptSource::CDVD,
    IOPInterruptSource::DMAC,
    IOPInterruptSource::Timer0,
    IOPInterruptSource::Timer1,
    IOPInterruptSource::Timer2,
    IOPInterruptSource::SIO0,
    IOPInterruptSource::SIO1,
    IOPInterruptSource::SPU,
    IOPInterruptSource::PIO,
    IOPInterruptSource::VBlankEnd,
    IOPInterruptSource::DVD,
    IOPInterruptSource::DEV9,
    IOPInterruptSource::Timer3,
    IOPInterruptSource::Timer4,
    IOPInterruptSource::Timer5,
    IOPInterruptSource::SIO2,
    IOPInterruptSource::HTR0,
    IOPInterruptSource::HTR1,
    IOPInterruptSource::HTR2,
    IOPInterruptSource::HTR3,
    IOPInterruptSource::USB,
    IOPInterruptSource::Expansion,
    IOPInterruptSource::ILink,
    IOPInterruptSource::FireWireDMA
  }};

  void requireState(
    const IOPInterruptController &interrupts,
    IOPWord status,
    IOPWord mask,
    IOPWord control,
    bool pending)
  {
    REQUIRE(interrupts.status() == status);
    REQUIRE(interrupts.mask() == mask);
    REQUIRE(interrupts.control() == control);
    REQUIRE(interrupts.interruptPending() == pending);
  }
}

TEST_CASE("IOP interrupt controller pins every retail cause")
{
  REQUIRE(IOPInterruptController::SOURCE_COUNT == 26);
  REQUIRE(IOPInterruptController::SOURCE_MASK == UINT32_C(0x03ffffff));

  for (std::size_t index = 0; index < sources.size(); ++index)
  {
    CAPTURE(index);
    REQUIRE(static_cast<std::uint8_t>(sources[index]) == index);
    IOPInterruptController interrupts;
    const IOPWord bit = UINT32_C(1) << index;
    requireState(interrupts, 0, 0, 0, false);

    interrupts.assertSource(sources[index]);
    requireState(interrupts, bit, 0, 0, false);
    interrupts.writeControl(1);
    requireState(interrupts, bit, 0, 1, false);
    interrupts.writeMask(IOPInterruptController::SOURCE_MASK & ~bit);
    requireState(interrupts, bit,
      IOPInterruptController::SOURCE_MASK & ~bit, 1, false);
    interrupts.writeMask(bit);
    requireState(interrupts, bit, bit, 1, true);

    interrupts.writeStatus(~bit);
    requireState(interrupts, 0, bit, 1, false);
    interrupts.assertSource(sources[index]);
    requireState(interrupts, bit, bit, 1, true);
  }
}

TEST_CASE("IOP interrupt controller status latches until write-zero acknowledgement")
{
  IOPInterruptController interrupts;
  interrupts.writeMask(UINT32_C(0xffffffff));
  interrupts.writeControl(1);
  for (IOPInterruptSource source : sources)
  {
    interrupts.assertSource(source);
    interrupts.assertSource(source);
  }
  requireState(interrupts, UINT32_C(0x03ffffff),
    UINT32_C(0x03ffffff), 1, true);

  interrupts.writeStatus(UINT32_C(0xffffffff));
  requireState(interrupts, UINT32_C(0x03ffffff),
    UINT32_C(0x03ffffff), 1, true);
  interrupts.writeStatus(UINT32_C(0xaaaaaaaa));
  requireState(interrupts, UINT32_C(0x02aaaaaa),
    UINT32_C(0x03ffffff), 1, true);
  interrupts.writeStatus(UINT32_C(0x55555555));
  requireState(interrupts, 0, UINT32_C(0x03ffffff), 1, false);

  interrupts.writeStatus(UINT32_C(0xffffffff));
  requireState(interrupts, 0, UINT32_C(0x03ffffff), 1, false);
  interrupts.assertSource(IOPInterruptSource::Timer0);
  interrupts.assertSource(IOPInterruptSource::Timer5);
  interrupts.writeStatus(0);
  requireState(interrupts, 0, UINT32_C(0x03ffffff), 1, false);
}

TEST_CASE("IOP interrupt controller mask writes replace rather than toggle")
{
  IOPInterruptController interrupts;
  interrupts.assertSource(IOPInterruptSource::Timer0);
  interrupts.assertSource(IOPInterruptSource::Timer5);
  interrupts.writeControl(1);

  interrupts.writeMask(UINT32_C(0xfc000000));
  requireState(interrupts, UINT32_C(0x00010010), 0, 1, false);
  interrupts.writeMask(UINT32_C(0xfc000010));
  interrupts.writeMask(UINT32_C(0xfc000010));
  requireState(interrupts, UINT32_C(0x00010010), 0x10, 1, true);
  interrupts.writeMask(UINT32_C(0x00010000));
  requireState(interrupts, UINT32_C(0x00010010), 0x10000, 1, true);
  interrupts.writeMask(0);
  requireState(interrupts, UINT32_C(0x00010010), 0, 1, false);
  interrupts.writeMask(UINT32_C(0xffffffff));
  requireState(interrupts, UINT32_C(0x00010010),
    UINT32_C(0x03ffffff), 1, true);
}

TEST_CASE("IOP interrupt controller master read clears only control")
{
  IOPInterruptController interrupts;
  interrupts.assertSource(IOPInterruptSource::Timer0);
  interrupts.writeMask(0x10);
  interrupts.writeControl(UINT32_C(0xffffffff));
  requireState(interrupts, 0x10, 0x10, 1, true);
  requireState(interrupts, 0x10, 0x10, 1, true);

  REQUIRE(interrupts.readControl() == 1);
  requireState(interrupts, 0x10, 0x10, 0, false);
  REQUIRE(interrupts.readControl() == 0);
  requireState(interrupts, 0x10, 0x10, 0, false);

  interrupts.writeControl(1);
  requireState(interrupts, 0x10, 0x10, 1, true);
  interrupts.writeControl(UINT32_C(0xfffffffe));
  requireState(interrupts, 0x10, 0x10, 0, false);
  REQUIRE(interrupts.readControl() == 0);

  interrupts.writeStatus(0);
  interrupts.writeControl(1);
  REQUIRE(interrupts.readControl() == 1);
  requireState(interrupts, 0, 0x10, 0, false);
}

TEST_CASE("IOP interrupt controller rejects unassigned typed sources atomically")
{
  IOPInterruptController interrupts;
  interrupts.assertSource(IOPInterruptSource::VBlankStart);
  interrupts.writeMask(1);
  interrupts.writeControl(1);

  for (unsigned int source : {26u, 31u, 32u, 255u})
  {
    CAPTURE(source);
    REQUIRE_THROWS_AS(
      interrupts.assertSource(static_cast<IOPInterruptSource>(source)),
      std::out_of_range);
    requireState(interrupts, 1, 1, 1, true);
  }
}

TEST_CASE("IOP interrupt controller reset clears all state independently")
{
  IOPInterruptController interrupts;
  IOPInterruptController other;
  for (IOPInterruptSource source : sources)
  {
    interrupts.assertSource(source);
  }
  interrupts.writeMask(UINT32_C(0xffffffff));
  interrupts.writeControl(1);
  requireState(other, 0, 0, 0, false);

  other.assertSource(IOPInterruptSource::FireWireDMA);
  other.writeMask(UINT32_C(0x02000000));
  other.writeControl(1);
  interrupts.reset();
  requireState(interrupts, 0, 0, 0, false);
  requireState(other, UINT32_C(0x02000000), UINT32_C(0x02000000), 1, true);
  REQUIRE(interrupts.readControl() == 0);

  interrupts.assertSource(IOPInterruptSource::Timer0);
  requireState(interrupts, 0x10, 0, 0, false);
  interrupts.reset();
  interrupts.reset();
  requireState(interrupts, 0, 0, 0, false);
}
