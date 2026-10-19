#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <stdexcept>
#include <type_traits>

#include "catch.hpp"
#include "sif.hpp"

static_assert(std::is_final<SIF>::value,
  "SIF state must have one concrete owner.");
static_assert(std::is_trivially_copyable<SIF>::value,
  "SIF storage must remain fixed-capacity value state.");

namespace
{
  constexpr std::array<SIFRegister, 5> registers = {{
    SIFRegister::MSCOM, SIFRegister::SMCOM,
    SIFRegister::MSFLAG, SIFRegister::SMFLAG, SIFRegister::CTRL
  }};
  constexpr std::array<SIFDirection, 2> directions = {{
    SIFDirection::SIF0, SIFDirection::SIF1
  }};

  void requireCompleted(
    const SIFWriteResult &result, SIFSignal signal = SIFSignal::None)
  {
    REQUIRE(result.outcome == SIFWriteOutcome::Completed);
    REQUIRE(result.signal == signal);
  }

  void requireCold(const SIF &sif)
  {
    for (SIFRegister reg : registers)
    {
      REQUIRE(sif.readEE(reg) ==
        (reg == SIFRegister::CTRL ? UINT32_C(0xf0000102) : 0));
      REQUIRE(sif.readIOP(reg) ==
        (reg == SIFRegister::CTRL ? UINT32_C(0xf0000002) : 0));
    }
    REQUIRE(sif.iopDoorbell() == 0);
    REQUIRE(sif.control() == 0);
    REQUIRE(sif.freeWordCount() == 32);
    REQUIRE(sif.wordCount(SIFDirection::SIF0) == 0);
    REQUIRE(sif.wordCount(SIFDirection::SIF1) == 0);
    REQUIRE(sif.sif0PackingLatch() == SIFQuadword{});
    REQUIRE(sif.sif0PackingCursor() == 0);
  }

  void requireWords(
    const SIF &sif, SIFDirection direction,
    const std::deque<std::uint32_t> &expected)
  {
    REQUIRE(sif.wordCount(direction) == expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
      REQUIRE(sif.wordAt(direction, index) == expected[index]);
    }
    REQUIRE_THROWS_AS(
      sif.wordAt(direction, expected.size()), std::out_of_range);
  }
}

TEST_CASE("SIF cold state and side-effect-free projections")
{
  const SIF sif;
  requireCold(sif);
  requireCold(sif);
  REQUIRE(SIF::WORD_CAPACITY == 32);
}

TEST_CASE("SIF mailbox writes obey directional ownership")
{
  SIF sif;
  for (std::uint32_t value : {0u, UINT32_C(0xffffffff), UINT32_C(0x81234567)})
  {
    requireCompleted(sif.writeEE(SIFRegister::MSCOM, value));
    REQUIRE(sif.readEE(SIFRegister::MSCOM) == value);
    REQUIRE(sif.readIOP(SIFRegister::MSCOM) == value);
    requireCompleted(sif.writeIOP(SIFRegister::MSCOM, ~value));
    REQUIRE(sif.readEE(SIFRegister::MSCOM) == value);

    requireCompleted(sif.writeIOP(SIFRegister::SMCOM, ~value));
    REQUIRE(sif.readEE(SIFRegister::SMCOM) == ~value);
    REQUIRE(sif.readIOP(SIFRegister::SMCOM) == ~value);
    requireCompleted(sif.writeEE(SIFRegister::SMCOM, value));
    REQUIRE(sif.readIOP(SIFRegister::SMCOM) == ~value);
    REQUIRE(sif.control() == 0);
    REQUIRE(sif.iopDoorbell() == 0);
    REQUIRE(sif.freeWordCount() == 32);
  }
}

TEST_CASE("SIF flag set and clear preserves every guest bit")
{
  SIF sif;
  std::uint32_t expected = 0;
  for (unsigned int bit = 0; bit < 32; ++bit)
  {
    const std::uint32_t value = UINT32_C(1) << bit;
    expected |= value;
    requireCompleted(sif.writeEE(SIFRegister::MSFLAG, value));
    requireCompleted(sif.writeIOP(SIFRegister::SMFLAG, value));
    requireCompleted(sif.writeEE(SIFRegister::MSFLAG, 0));
    requireCompleted(sif.writeIOP(SIFRegister::SMFLAG, 0));
    REQUIRE(sif.readEE(SIFRegister::MSFLAG) == expected);
    REQUIRE(sif.readIOP(SIFRegister::MSFLAG) == expected);
    REQUIRE(sif.readEE(SIFRegister::SMFLAG) == expected);
    REQUIRE(sif.readIOP(SIFRegister::SMFLAG) == expected);
  }
  for (std::uint32_t value :
      {0u, UINT32_C(0x80000000), UINT32_C(0x00070000),
       UINT32_C(0x55555555), UINT32_C(0xffffffff)})
  {
    expected &= ~value;
    requireCompleted(sif.writeIOP(SIFRegister::MSFLAG, value));
    requireCompleted(sif.writeEE(SIFRegister::SMFLAG, value));
    REQUIRE(sif.readEE(SIFRegister::MSFLAG) == expected);
    REQUIRE(sif.readIOP(SIFRegister::SMFLAG) == expected);
    REQUIRE(sif.control() == 0);
    REQUIRE(sif.iopDoorbell() == 0);
  }
}

TEST_CASE("SIF EE CTRL latch is distinct from forced read bits")
{
  SIF sif;
  requireCompleted(sif.writeEE(SIFRegister::CTRL, 0x100));
  REQUIRE(sif.control() == 0x100);
  REQUIRE(sif.readEE(SIFRegister::CTRL) == UINT32_C(0xf0000102));
  REQUIRE(sif.readIOP(SIFRegister::CTRL) == UINT32_C(0xf0000102));
  requireCompleted(sif.writeEE(SIFRegister::CTRL, 0));
  REQUIRE(sif.control() == 0);
  REQUIRE(sif.readEE(SIFRegister::CTRL) == UINT32_C(0xf0000102));
  REQUIRE(sif.readIOP(SIFRegister::CTRL) == UINT32_C(0xf0000002));

  requireCompleted(sif.writeIOP(SIFRegister::CTRL, 0xa0));
  const auto before = sif.control();
  requireCompleted(sif.writeEE(SIFRegister::CTRL, UINT32_C(0xfff3feff)));
  REQUIRE(sif.control() == (before & ~UINT32_C(0x100)));
  requireCompleted(sif.writeEE(SIFRegister::CTRL, UINT32_C(0xfff3ffff)));
  REQUIRE(sif.control() == (before | UINT32_C(0x100)));
}

TEST_CASE("SIF IOP CTRL commands use grouped set clear rather than XOR")
{
  for (std::uint32_t field = 0; field < 16; ++field)
  {
    for (std::uint32_t selection = 0; selection < 16; ++selection)
    {
      CAPTURE(field);
      CAPTURE(selection);
      SIF sif;
      requireCompleted(sif.writeEE(SIFRegister::CTRL, 0x100));
      requireCompleted(sif.writeIOP(SIFRegister::CTRL, field << 4));
      const std::uint32_t before = sif.control();
      const std::uint32_t selected = selection << 4;
      const std::uint32_t low = (before & selected) != 0
        ? (before & 0xf0) & ~selected : (before & 0xf0) | selected;
      const std::uint32_t high = (selected & 0xa0) != 0
        ? 0x2000 : before & 0xf000;
      requireCompleted(sif.writeIOP(SIFRegister::CTRL,
        selected | UINT32_C(0xffff0f0f)));
      REQUIRE(sif.control() == (high | 0x100 | low));
      REQUIRE(sif.readEE(SIFRegister::CTRL) ==
        (sif.control() | UINT32_C(0xf0000102)));
      REQUIRE(sif.readIOP(SIFRegister::CTRL) ==
        (sif.control() | UINT32_C(0xf0000002)));
    }
  }
}

TEST_CASE("SIF doorbell effects occur per accepted write without pending copies")
{
  SIF sif;
  for (unsigned int repeat = 0; repeat < 3; ++repeat)
  {
    requireCompleted(sif.writeEE(SIFRegister::CTRL, 0x40100),
      SIFSignal::RequestIOP);
    REQUIRE(sif.control() == 0x100);
    REQUIRE((sif.readEE(SIFRegister::CTRL) & 0x40000) == 0);
    requireCompleted(sif.writeIOPDoorbell(UINT32_C(0xabcdef03)),
      SIFSignal::RequestEE);
    REQUIRE(sif.iopDoorbell() == UINT32_C(0xabcdef03));
  }
  requireCompleted(sif.writeIOPDoorbell(UINT32_C(0xfffffffd)));
  REQUIRE(sif.iopDoorbell() == UINT32_C(0xfffffffd));
  requireCompleted(sif.writeIOPDoorbell(0));
  requireCompleted(sif.writeEE(SIFRegister::CTRL, 0));
  REQUIRE(sif.control() == 0);
  REQUIRE(sif.iopDoorbell() == 0);
  REQUIRE(sif.readEE(SIFRegister::MSFLAG) == 0);
  REQUIRE(sif.readIOP(SIFRegister::SMFLAG) == 0);
}

TEST_CASE("SIF unsupported EE compatibility command rejects every effect atomically")
{
  SIF sif;
  requireCompleted(sif.writeEE(SIFRegister::MSCOM, 0x1234));
  requireCompleted(sif.writeIOP(SIFRegister::SMCOM, 0x5678));
  requireCompleted(sif.writeEE(SIFRegister::MSFLAG, UINT32_C(0xffffffff)));
  requireCompleted(sif.writeIOP(SIFRegister::SMFLAG, UINT32_C(0x80000000)));
  requireCompleted(sif.writeIOP(SIFRegister::CTRL, 0x30));
  requireCompleted(sif.writeIOPDoorbell(0x80));
  const SIFQuadword prologue = {{1, 2, 3, 4}};
  REQUIRE(sif.pushSIF0Prologue(prologue) == SIFTransferOutcome::Completed);
  REQUIRE(sif.pushSIF0PayloadWord(5, SIFPayloadBoundary::Continuing) ==
    SIFTransferOutcome::Completed);
  for (std::uint32_t value : {0x80000u, 0xc0100u, UINT32_C(0xffffffff)})
  {
    const auto result = sif.writeEE(SIFRegister::CTRL, value);
    REQUIRE(result.outcome == SIFWriteOutcome::UnsupportedControl);
    REQUIRE(result.signal == SIFSignal::None);
    REQUIRE(sif.control() == 0x2030);
    REQUIRE(sif.readEE(SIFRegister::MSCOM) == 0x1234);
    REQUIRE(sif.readEE(SIFRegister::SMCOM) == 0x5678);
    REQUIRE(sif.readEE(SIFRegister::MSFLAG) == UINT32_C(0xffffffff));
    REQUIRE(sif.readEE(SIFRegister::SMFLAG) == UINT32_C(0x80000000));
    REQUIRE(sif.iopDoorbell() == 0x80);
    requireWords(sif, SIFDirection::SIF0, {1, 2, 3, 4, 5});
    REQUIRE(sif.sif0PackingLatch() == SIFQuadword{{5, 2, 3, 4}});
    REQUIRE(sif.sif0PackingCursor() == 1);
  }
}

TEST_CASE("SIF typed register and transport inspection rejects invalid inputs")
{
  SIF sif;
  for (unsigned int value : {5u, 6u, 255u})
  {
    const auto reg = static_cast<SIFRegister>(value);
    REQUIRE_THROWS_AS(sif.readEE(reg), std::out_of_range);
    REQUIRE_THROWS_AS(sif.readIOP(reg), std::out_of_range);
    REQUIRE_THROWS_AS(sif.writeEE(reg, 0xffffffff), std::out_of_range);
    REQUIRE_THROWS_AS(sif.writeIOP(reg, 0xffffffff), std::out_of_range);
  }
  for (unsigned int value : {2u, 3u, 255u})
  {
    const auto direction = static_cast<SIFDirection>(value);
    SIFQuadword output = {{9, 8, 7, 6}};
    REQUIRE_THROWS_AS(sif.wordCount(direction), std::out_of_range);
    REQUIRE_THROWS_AS(sif.wordAt(direction, 0), std::out_of_range);
    REQUIRE_THROWS_AS(sif.peekWords(direction, output, 1), std::out_of_range);
    REQUIRE_THROWS_AS(sif.consumeWords(direction, 1), std::out_of_range);
    REQUIRE(output == SIFQuadword{{9, 8, 7, 6}});
  }
  for (std::size_t count : {std::size_t(0), std::size_t(5), SIZE_MAX})
  {
    SIFQuadword output = {{9, 8, 7, 6}};
    REQUIRE_THROWS_AS(sif.peekWords(SIFDirection::SIF0, output, count),
      std::out_of_range);
    REQUIRE_THROWS_AS(sif.consumeWords(SIFDirection::SIF1, count),
      std::out_of_range);
    REQUIRE(output == SIFQuadword{{9, 8, 7, 6}});
  }
  REQUIRE_THROWS_AS(sif.pushSIF0PayloadWord(1,
    static_cast<SIFPayloadBoundary>(255)), std::out_of_range);
  REQUIRE_THROWS_AS(sif.wordAt(SIFDirection::SIF0, SIZE_MAX),
    std::out_of_range);
  requireCold(sif);
}

TEST_CASE("SIF streams share exactly 32 words and either may borrow all capacity")
{
  for (SIFDirection full : directions)
  {
    SIF sif;
    const SIFDirection empty = full == SIFDirection::SIF0
      ? SIFDirection::SIF1 : SIFDirection::SIF0;
    for (std::uint32_t index = 0; index < 8; ++index)
    {
      const SIFQuadword words = {{4 * index, 4 * index + 1,
        4 * index + 2, 4 * index + 3}};
      const auto outcome = full == SIFDirection::SIF0
        ? sif.pushSIF0Prologue(words) : sif.pushSIF1Quadword(words);
      REQUIRE(outcome == SIFTransferOutcome::Completed);
    }
    REQUIRE(sif.wordCount(full) == 32);
    REQUIRE(sif.wordCount(empty) == 0);
    REQUIRE(sif.freeWordCount() == 0);
    const auto latch = sif.sif0PackingLatch();
    const auto cursor = sif.sif0PackingCursor();
    REQUIRE(sif.pushSIF1Quadword({{90, 91, 92, 93}}) ==
      SIFTransferOutcome::Wait);
    REQUIRE(sif.pushSIF0Prologue({{80, 81, 82, 83}}) ==
      SIFTransferOutcome::Wait);
    REQUIRE(sif.pushSIF0PayloadWord(70, SIFPayloadBoundary::Continuing) ==
      SIFTransferOutcome::Wait);
    REQUIRE(sif.sif0PackingLatch() == latch);
    REQUIRE(sif.sif0PackingCursor() == cursor);
    for (std::size_t index = 0; index < 32; ++index)
    {
      REQUIRE(sif.wordAt(full, index) == index);
    }
    REQUIRE(sif.consumeWords(full, 4) == SIFTransferOutcome::Completed);
    const auto outcome = empty == SIFDirection::SIF0
      ? sif.pushSIF0Prologue({{100, 101, 102, 103}})
      : sif.pushSIF1Quadword({{100, 101, 102, 103}});
    REQUIRE(outcome == SIFTransferOutcome::Completed);
    requireWords(sif, empty, {100, 101, 102, 103});
    REQUIRE(sif.freeWordCount() == 0);
  }
}

TEST_CASE("SIF short peeks and consumes wait without mutation")
{
  SIF sif;
  SIFQuadword output = {{9, 8, 7, 6}};
  for (std::size_t available = 0; available < 4; ++available)
  {
    for (std::size_t requested = available + 1; requested <= 4; ++requested)
    {
      REQUIRE(sif.peekWords(SIFDirection::SIF0, output, requested) ==
        SIFTransferOutcome::Wait);
      REQUIRE(output == SIFQuadword{{9, 8, 7, 6}});
      REQUIRE(sif.consumeWords(SIFDirection::SIF0, requested) ==
        SIFTransferOutcome::Wait);
      REQUIRE(sif.wordCount(SIFDirection::SIF0) == available);
    }
    REQUIRE(sif.pushSIF0PayloadWord(
      static_cast<std::uint32_t>(available), SIFPayloadBoundary::Continuing) ==
      SIFTransferOutcome::Completed);
  }
  REQUIRE(sif.peekWords(SIFDirection::SIF0, output, 4) ==
    SIFTransferOutcome::Completed);
  REQUIRE(output == SIFQuadword{{0, 1, 2, 3}});
  REQUIRE(sif.peekWords(SIFDirection::SIF0, output, 4) ==
    SIFTransferOutcome::Completed);
  REQUIRE(sif.wordCount(SIFDirection::SIF0) == 4);
  REQUIRE(sif.consumeWords(SIFDirection::SIF0, 4) ==
    SIFTransferOutcome::Completed);
  REQUIRE(sif.freeWordCount() == 32);
}

TEST_CASE("SIF streams recycle shared slots without cross-direction head of line blocking")
{
  SIF sif;
  std::array<std::deque<std::uint32_t>, 2> expected;
  std::uint32_t next = 1;
  for (std::size_t step = 0; step < 1500; ++step)
  {
    const std::size_t producer = (step / 3) % 2;
    SIFQuadword words = {{next, next + 1, next + 2, next + 3}};
    const auto outcome = producer == 0
      ? sif.pushSIF0Prologue(words) : sif.pushSIF1Quadword(words);
    const auto occupied = expected[0].size() + expected[1].size();
    REQUIRE(outcome == (occupied <= 28
      ? SIFTransferOutcome::Completed : SIFTransferOutcome::Wait));
    if (outcome == SIFTransferOutcome::Completed)
    {
      for (auto word : words)
      {
        expected[producer].push_back(word);
      }
      next += 4;
    }
    const std::size_t consumer = (step / 7) % 2;
    const std::size_t count = step % 4 + 1;
    SIFQuadword output = {{9, 8, 7, 6}};
    const auto read = sif.peekWords(directions[consumer], output, count);
    REQUIRE(read == (expected[consumer].size() >= count
      ? SIFTransferOutcome::Completed : SIFTransferOutcome::Wait));
    if (read == SIFTransferOutcome::Completed)
    {
      for (std::size_t index = 0; index < count; ++index)
      {
        REQUIRE(output[index] == expected[consumer][index]);
      }
      REQUIRE(sif.consumeWords(directions[consumer], count) ==
        SIFTransferOutcome::Completed);
      for (std::size_t index = 0; index < count; ++index)
      {
        expected[consumer].pop_front();
      }
    }
    else
    {
      REQUIRE(output == SIFQuadword{{9, 8, 7, 6}});
    }
    requireWords(sif, directions[0], expected[0]);
    requireWords(sif, directions[1], expected[1]);
    REQUIRE(sif.freeWordCount() ==
      32 - expected[0].size() - expected[1].size());
  }
}

TEST_CASE("SIF0 residual lanes come from admitted packing history")
{
  for (std::size_t length = 0; length <= 9; ++length)
  {
    CAPTURE(length);
    SIF sif;
    SIFQuadword latch = {{10, 11, 12, 13}};
    REQUIRE(sif.pushSIF0Prologue(latch) == SIFTransferOutcome::Completed);
    REQUIRE(sif.consumeWords(SIFDirection::SIF0, 4) ==
      SIFTransferOutcome::Completed);
    std::deque<std::uint32_t> expected;
    for (std::size_t index = 0; index < length; ++index)
    {
      const std::uint32_t word = static_cast<std::uint32_t>(100 + index);
      latch[index % 4] = word;
      const auto boundary = index + 1 == length
        ? SIFPayloadBoundary::Final : SIFPayloadBoundary::Continuing;
      REQUIRE(sif.pushSIF0PayloadWord(word, boundary) ==
        SIFTransferOutcome::Completed);
      expected.push_back(word);
      if (boundary == SIFPayloadBoundary::Final)
      {
        for (std::size_t lane = index % 4 + 1; lane < 4; ++lane)
        {
          expected.push_back(latch[lane]);
        }
      }
    }
    requireWords(sif, SIFDirection::SIF0, expected);
    REQUIRE(sif.sif0PackingLatch() == latch);
    REQUIRE(sif.sif0PackingCursor() == length % 4);
    while (sif.wordCount(SIFDirection::SIF0) != 0)
    {
      REQUIRE(sif.consumeWords(SIFDirection::SIF0, 4) ==
        SIFTransferOutcome::Completed);
    }
    REQUIRE(sif.sif0PackingLatch() == latch);
    REQUIRE(sif.sif0PackingCursor() == length % 4);
    REQUIRE(sif.pushSIF0Prologue({{20, 21, 22, 23}}) ==
      SIFTransferOutcome::Completed);
    REQUIRE(sif.sif0PackingLatch() == SIFQuadword{{20, 21, 22, 23}});
    REQUIRE(sif.sif0PackingCursor() == 0);
  }
}

TEST_CASE("SIF0 final real word and retained fill share one atomic admission")
{
  for (std::size_t cursor = 0; cursor < 4; ++cursor)
  {
    for (std::size_t free = 0; free <= 4; ++free)
    {
      CAPTURE(cursor);
      CAPTURE(free);
      SIF sif;
      REQUIRE(sif.pushSIF0Prologue({{10, 11, 12, 13}}) ==
        SIFTransferOutcome::Completed);
      for (std::size_t index = 0; index < cursor; ++index)
      {
        REQUIRE(sif.pushSIF0PayloadWord(
          static_cast<std::uint32_t>(20 + index),
          SIFPayloadBoundary::Continuing) == SIFTransferOutcome::Completed);
      }
      for (std::size_t index = 0; index < cursor; ++index)
      {
        REQUIRE(sif.consumeWords(SIFDirection::SIF0, 1) ==
          SIFTransferOutcome::Completed);
      }
      for (std::size_t index = 0; index < 7; ++index)
      {
        REQUIRE(sif.pushSIF1Quadword({{50, 51, 52, 53}}) ==
          SIFTransferOutcome::Completed);
      }
      for (std::size_t index = 0; index < free; ++index)
      {
        REQUIRE(sif.consumeWords(SIFDirection::SIF1, 1) ==
          SIFTransferOutcome::Completed);
      }
      REQUIRE(sif.freeWordCount() == free);
      const SIF before = sif;
      const std::size_t needed = 4 - cursor;
      const auto result = sif.pushSIF0PayloadWord(99, SIFPayloadBoundary::Final);
      REQUIRE(result == (free >= needed
        ? SIFTransferOutcome::Completed : SIFTransferOutcome::Wait));
      REQUIRE(sif.sif0PackingCursor() ==
        (result == SIFTransferOutcome::Completed ? (cursor + 1) % 4 : cursor));
      if (result == SIFTransferOutcome::Wait)
      {
        REQUIRE(sif.sif0PackingLatch() == before.sif0PackingLatch());
        for (auto direction : directions)
        {
          REQUIRE(sif.wordCount(direction) == before.wordCount(direction));
          for (std::size_t index = 0; index < sif.wordCount(direction); ++index)
          {
            REQUIRE(sif.wordAt(direction, index) == before.wordAt(direction, index));
          }
        }
      }
      else
      {
        REQUIRE(sif.freeWordCount() == free - needed);
        const auto start = before.wordCount(SIFDirection::SIF0);
        REQUIRE(sif.wordAt(SIFDirection::SIF0, start) == 99);
        for (std::size_t lane = cursor + 1; lane < 4; ++lane)
        {
          REQUIRE(sif.wordAt(SIFDirection::SIF0, start + lane - cursor) ==
            before.sif0PackingLatch()[lane]);
        }
      }
    }
  }
}

TEST_CASE("SIF reset clears active streams and retained state without affecting another owner")
{
  SIF sif;
  requireCompleted(sif.writeEE(SIFRegister::MSCOM, UINT32_C(0xffffffff)));
  requireCompleted(sif.writeIOP(SIFRegister::SMCOM, UINT32_C(0xffffffff)));
  requireCompleted(sif.writeEE(SIFRegister::MSFLAG, UINT32_C(0xffffffff)));
  requireCompleted(sif.writeIOP(SIFRegister::SMFLAG, UINT32_C(0xffffffff)));
  requireCompleted(sif.writeEE(SIFRegister::CTRL, 0x40100), SIFSignal::RequestIOP);
  requireCompleted(sif.writeIOP(SIFRegister::CTRL, 0xf0));
  requireCompleted(sif.writeIOPDoorbell(UINT32_C(0xffffffff)), SIFSignal::RequestEE);
  REQUIRE(sif.pushSIF0Prologue({{1, 2, 3, 4}}) == SIFTransferOutcome::Completed);
  REQUIRE(sif.pushSIF0PayloadWord(5, SIFPayloadBoundary::Continuing) ==
    SIFTransferOutcome::Completed);
  REQUIRE(sif.pushSIF1Quadword({{6, 7, 8, 9}}) == SIFTransferOutcome::Completed);
  const SIF other = sif;
  sif.reset();
  requireCold(sif);
  requireWords(other, SIFDirection::SIF0, {1, 2, 3, 4, 5});
  requireWords(other, SIFDirection::SIF1, {6, 7, 8, 9});
  REQUIRE(other.sif0PackingLatch() == SIFQuadword{{5, 2, 3, 4}});
  REQUIRE(other.control() == 0x21f0);
  REQUIRE(other.iopDoorbell() == UINT32_C(0xffffffff));
  for (unsigned int repeat = 0; repeat < 3; ++repeat)
  {
    for (unsigned int index = 0; index < 8; ++index)
    {
      REQUIRE(sif.pushSIF1Quadword({{1, 2, 3, 4}}) ==
        SIFTransferOutcome::Completed);
    }
    REQUIRE(sif.freeWordCount() == 0);
    sif.reset();
    requireCold(sif);
  }
}
