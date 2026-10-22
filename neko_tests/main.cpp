#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include <algorithm>
#include <iomanip>
#include <map>
#include <string>
#include <vector>

namespace
{
  struct TestProfile
  {
    std::string name;
    double seconds = 0.0;
    std::size_t assertions = 0;
  };

  class ProfileReporter : public Catch::ConsoleReporter
  {
    public:
      explicit ProfileReporter(const Catch::ReporterConfig &config) :
        Catch::ConsoleReporter(config)
      {
      }

      static std::string getDescription()
      {
        return "Console results with per-case and per-file timing/assertion totals";
      }

      void testCaseStarting(const Catch::TestCaseInfo &info) override
      {
        Catch::ConsoleReporter::testCaseStarting(info);
        timer.start();
      }

      void testCaseEnded(const Catch::TestCaseStats &stats) override
      {
        const double seconds = timer.getElapsedSeconds();
        const auto assertions = stats.totals.assertions.total();
        cases.push_back({stats.testInfo.name, seconds, assertions});
        auto &file = files[stats.testInfo.lineInfo.file];
        file.seconds += seconds;
        file.assertions += assertions;
        Catch::ConsoleReporter::testCaseEnded(stats);
      }

      void testRunEnded(const Catch::TestRunStats &stats) override
      {
        Catch::ConsoleReporter::testRunEnded(stats);
        std::vector<TestProfile> fileProfiles;
        for (const auto &file : files)
        {
          fileProfiles.push_back(
            {file.first, file.second.seconds, file.second.assertions});
        }
        writeProfiles("FILE", fileProfiles);
        writeProfiles("CASE", cases);
      }

    private:
      void writeProfiles(
        const char *kind,
        std::vector<TestProfile> &profiles)
      {
        std::sort(
          profiles.begin(), profiles.end(),
          [](const TestProfile &left, const TestProfile &right)
          {
            if (left.seconds != right.seconds)
            {
              return left.seconds > right.seconds;
            }
            return left.name < right.name;
          });
        stream << "\nPROFILE " << kind << "\tseconds\tassertions\tname\n";
        for (const auto &profile : profiles)
        {
          stream << kind << '\t' << std::fixed << std::setprecision(6)
                 << profile.seconds << '\t' << profile.assertions
                 << '\t' << profile.name << '\n';
        }
      }

      Catch::Timer timer;
      std::vector<TestProfile> cases;
      std::map<std::string, TestProfile> files;
  };
}

CATCH_REGISTER_REPORTER("profile", ProfileReporter)
