#ifndef stop_watch_hpp
#define stop_watch_hpp

#include <chrono>

class StopWatch
{
  public:
    void start();
    double elapsedCycles();
  private:
    std::chrono::high_resolution_clock::time_point startTime;
};

#endif
