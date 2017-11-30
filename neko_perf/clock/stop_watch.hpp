#ifndef stop_watch_hpp
#define stop_watch_hpp

using namespace std;

class StopWatch
{
  public:
    void start();
    double elapsedCycles();
  private:
    chrono::high_resolution_clock::time_point startTime;
};

#endif
