#define CATCH_CONFIG_EXTERNAL_INTERFACES
#include "catch.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

TEST_CASE("Test timers retain long elapsed durations", "[test-runner]")
{
  using Nanoseconds =
    decltype(std::declval<Catch::Timer>().getElapsedNanoseconds());
  using Microseconds =
    decltype(std::declval<Catch::Timer>().getElapsedMicroseconds());
  using Milliseconds =
    decltype(std::declval<Catch::Timer>().getElapsedMilliseconds());
  REQUIRE(std::numeric_limits<Nanoseconds>::max() >= UINT64_C(5000000000));
  REQUIRE(std::numeric_limits<Microseconds>::max() >= UINT64_C(7200000000));
  REQUIRE(std::numeric_limits<Milliseconds>::max() >= UINT64_C(31536000000));
}

TEST_CASE("Test shards partition literal names independently of ordering", "[test-runner]")
{
  const std::vector<std::string> names =
  {
    "ordinary test",
    "literal [tag], comma and \"quotes\"",
    "*wildcards*",
    "back\\slash",
    "hidden test",
    "test with a different name"
  };
  for (unsigned count = 1; count <= 8; ++count)
  {
    for (const auto &name : names)
    {
      unsigned matches = 0;
      for (unsigned index = 0; index < count; ++index)
      {
        if (Catch::isTestInShard(name, count, index))
        {
          ++matches;
        }
      }
      REQUIRE(matches == 1);
    }
  }
  REQUIRE(Catch::isTestInShard("ordinary test", 1, 0));
  REQUIRE(Catch::isTestInShard("a", 4, 0));
}

TEST_CASE("Invalid test shards fail explicitly", "[test-runner]")
{
  REQUIRE_THROWS(Catch::isTestInShard("test", 0, 0));
  REQUIRE_THROWS(Catch::isTestInShard("test", 65, 0));
  REQUIRE_THROWS(Catch::isTestInShard("test", 4, 4));
  REQUIRE_THROWS(Catch::isTestInShard("test", 4, 5));
}

TEST_CASE("Shard numbers require complete bounded decimal integers", "[test-runner]")
{
  for (const auto &text :
       {"", "1.5", "4x", " 4", "4 ", "+4", "-1", "65", "42949672960"})
  {
    CAPTURE(text);
    unsigned value = 37;
    REQUIRE_FALSE(Catch::parseTestShardNumber(text, value));
    REQUIRE(value == 37);
  }
  const std::pair<const char *, unsigned> valid[] =
  {
    {"0", 0}, {"1", 1}, {"4", 4}, {"64", 64}
  };
  for (const auto &number : valid)
  {
    unsigned value = 37;
    REQUIRE(Catch::parseTestShardNumber(number.first, value));
    REQUIRE(value == number.second);
  }
}

TEST_CASE("Test shards preserve Catch filters hidden tests and empty selections", "[test-runner]")
{
  const Catch::SourceLineInfo location(__FILE__, __LINE__);
  std::vector<Catch::TestCase> tests =
  {
    Catch::makeTestCase(
      nullptr, "", "visible [name], with \"quotes\"", "[runner-filter]", location),
    Catch::makeTestCase(
      nullptr, "", "*wildcards*", "[runner-filter][omit]", location),
    Catch::makeTestCase(
      nullptr, "", "hidden", "[.][runner-filter]", location),
    Catch::makeTestCase(
      nullptr, "", "throwing", "[!throws][runner-filter]", location)
  };
  for (const auto &filter :
       {"~[.]", "[runner-filter]", "[runner-filter]~[omit]", "no matching name"})
  {
    for (bool noThrow : {false, true})
    {
      Catch::ConfigData data;
      data.testsOrTags = {filter};
      data.noThrow = noThrow;
      const Catch::Config unsharded(data);
      const auto selected =
        Catch::filterTests(tests, unsharded.testSpec(), unsharded);
      std::vector<std::string> expected;
      for (const auto &test : selected)
      {
        expected.push_back(test.name);
      }
      std::sort(expected.begin(), expected.end());
      for (unsigned count : {1u, 2u, 3u, 4u, 64u})
      {
        data.shardCount = count;
        std::vector<std::string> actual;
        for (unsigned index = 0; index < count; ++index)
        {
          data.shardIndex = index;
          const Catch::Config sharded(data);
          for (const auto &test :
               Catch::filterTests(tests, sharded.testSpec(), sharded))
          {
            actual.push_back(test.name);
          }
          std::reverse(tests.begin(), tests.end());
        }
        std::sort(actual.begin(), actual.end());
        REQUIRE(actual == expected);
      }
    }
  }
}

TEST_CASE("Invalid shard configurations fail before running tests", "[test-runner]")
{
  Catch::ConfigData data;
  data.shardCount = 0;
  REQUIRE_THROWS_AS(Catch::Config(data), std::exception);
  data.shardCount = 65;
  REQUIRE_THROWS_AS(Catch::Config(data), std::exception);
  data.shardCount = 4;
  data.shardIndex = 4;
  REQUIRE_THROWS_AS(Catch::Config(data), std::exception);
}
