#include <chrono>

#include "stop_watch.hpp"

void StopWatch::start()
{
  startTime = std::chrono::high_resolution_clock::now();
}

double StopWatch::elapsedCycles()
{
  double elapsedTime = std::chrono::nanoseconds(std::chrono::high_resolution_clock::now() - startTime).count();
  double cyclesPerNanosecond = 0.294;

  return elapsedTime * cyclesPerNanosecond;
}
