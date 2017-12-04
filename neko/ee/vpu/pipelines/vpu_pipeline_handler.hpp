#ifndef VPU_PIPELINE_HANDLER_HPP
#define VPU_PIPELINE_HANDLER_HPP

#include "vpu_pipeline.hpp"

class PipelineHandler
{
  public:
    virtual void pipelineStarted(Pipeline * pipeline) = 0;
    virtual void pipelineFinished(Pipeline * pipeline) = 0;
};

#endif
