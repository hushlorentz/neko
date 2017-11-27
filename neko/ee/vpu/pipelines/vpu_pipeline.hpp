#ifndef PIPELINE_H
#define PIPELINE_H

#include <cstdint>

class Pipeline
{
  public: 
    Pipeline(uint8_t maxStage);
    void execute();
    bool isExecuting;
  private:
    uint8_t currentStage;
    uint8_t maxStage;
};

#endif
