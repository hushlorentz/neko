#include <chrono>

#include "stop_watch.hpp"

void StopWatch::start()
{
  startTime = chrono::high_resolution_clock::now();
}

double StopWatch::elapsedCycles()
{
  double elapsedTime = chrono::nanoseconds(chrono::high_resolution_clock::now() - startTime).count();
  double cyclesPerNanosecond = 0.294;

  return elapsedTime * cyclesPerNanosecond;
}
