#ifndef VPU_COMPLETED_PIPELINE_HANDLER_HPP
#define VPU_COMPLETED_PIPELINE_HANDLER_HPP

#include "vpu_pipeline.hpp"

class CompletedPipelineHandler
{
  public:
    virtual void pipelineComplete(Pipeline * pipeline) = 0;
};

#endif
