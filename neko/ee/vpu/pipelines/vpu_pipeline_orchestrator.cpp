#include <stdexcept>

#include "vpu_pipeline.hpp"
#include "vpu_pipeline_orchestrator.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_register_ids.hpp"

namespace
{
  bool isVectorLoad(const Pipeline *pipeline)
  {
    return
      pipeline->type != VPU_PIPELINE_TYPE_LSU ||
      pipeline->opCode == VPU_LQ ||
      pipeline->opCode == VPU_LQD ||
      pipeline->opCode == VPU_LQI;
  }
}

PipelineOrchestrator::PipelineOrchestrator() : pipelineHandler(NULL), stalling(false)
{
  for (int i = 0; i < MAX_PIPELINES; i++)
  {
    Pipeline *pipeline = new Pipeline();
    pool.push_back(pipeline);
  }
}

PipelineOrchestrator::~PipelineOrchestrator()
{
  reset();

  while (!pool.empty())
  {
    Pipeline *pipeline = pool.front();
    delete pipeline;

    pool.pop_front();
  }
}

void PipelineOrchestrator::reset()
{
  pool.splice(pool.end(), executing);
  pool.splice(pool.end(), waiting);
  stalling = false;
}

void PipelineOrchestrator::update()
{
  stalling = false;
  updateExecutingPipelines();
  updateWaitingPipelines();
}

void PipelineOrchestrator::updateWaitingPipelines()
{
  if (waiting.size() == 0 || stalling)
  {
    return;
  }

  Pipeline * p = waiting.front();
  detectStalls(p);

  if (stalling)
  {
    return;
  }

  waiting.erase(waiting.begin());
  executing.push_back(p);

  if (pipelineHandler)
  {
    pipelineHandler->pipelineStarted(p);
  }
}

void PipelineOrchestrator::detectStalls(Pipeline * pipeline)
{
  for (list<Pipeline *>::iterator iter = executing.begin(); iter != executing.end(); ++iter)
  {
    Pipeline * checkPipeline = *iter;

    if (!isVectorLoad(checkPipeline) ||
        checkPipeline->destinationAvailableForNextTStage() ||
        checkPipeline->discardWriteback ||
        checkPipeline->destReg == VPU_REGISTER_VF00)
    {
      continue;
    }

    const bool srcReg1Hazard =
      pipeline->srcReg1FieldMask != 0 &&
      pipeline->srcReg1 != VPU_REGISTER_VF00 &&
      pipeline->srcReg1 == checkPipeline->destReg &&
      (pipeline->srcReg1FieldMask & checkPipeline->destFieldMask) != 0;
    const bool srcReg2Hazard =
      pipeline->srcReg2FieldMask != 0 &&
      pipeline->srcReg2 != VPU_REGISTER_VF00 &&
      pipeline->srcReg2 == checkPipeline->destReg &&
      (pipeline->srcReg2FieldMask & checkPipeline->destFieldMask) != 0;

    if (srcReg1Hazard || srcReg2Hazard)
    {
      stalling = true;
      return;
    }
  }
}

void PipelineOrchestrator::updateExecutingPipelines()
{
  list<Pipeline *>::iterator iter = executing.begin();

  while (iter != executing.end())
  {
    Pipeline * p = (Pipeline *)*iter;
    if (p->stage() == VUPipelineStage::M &&
        hasStructuralHazard(p))
    {
      stalling = true;
      ++iter;
      continue;
    }

    p->advanceStage();

    if (pipelineHandler)
    {
      pipelineHandler->pipelineAdvanced(p);
    }

    if (p->isComplete())
    {
      if (pipelineHandler)
      {
        pipelineHandler->pipelineFinished(p);
      }

      iter = executing.erase(iter);
      pool.push_back(p);
    }
    else
    {
      ++iter;
    }
  }
}

bool PipelineOrchestrator::hasStructuralHazard(
  const Pipeline *pipeline) const
{
  for (Pipeline *olderPipeline : executing)
  {
    if (olderPipeline == pipeline)
    {
      break;
    }
    if (olderPipeline->blocksStructuralHazardFor(pipeline->type))
    {
      return true;
    }
  }

  return false;
}

bool PipelineOrchestrator::hasNext()
{
  return executing.size() > 0 || waiting.size() > 0;
}

bool PipelineOrchestrator::hasRegisterHazard(uint8_t srcReg1, uint8_t srcReg1FieldMask, uint8_t srcReg2, uint8_t srcReg2FieldMask) const
{
  for (list<Pipeline *>::const_iterator iter = executing.begin(); iter != executing.end(); ++iter)
  {
    Pipeline *pipeline = *iter;
    if (!isVectorLoad(pipeline) ||
        pipeline->destinationAvailableForNextTStage() ||
        pipeline->discardWriteback ||
        pipeline->destFieldMask == 0 ||
        pipeline->destReg == VPU_REGISTER_VF00)
    {
      continue;
    }

    const bool srcReg1Hazard =
      srcReg1FieldMask != 0 &&
      srcReg1 != VPU_REGISTER_VF00 &&
      srcReg1 == pipeline->destReg &&
      (srcReg1FieldMask & pipeline->destFieldMask) != 0;
    const bool srcReg2Hazard =
      srcReg2FieldMask != 0 &&
      srcReg2 != VPU_REGISTER_VF00 &&
      srcReg2 == pipeline->destReg &&
      (srcReg2FieldMask & pipeline->destFieldMask) != 0;

    if (srcReg1Hazard || srcReg2Hazard)
    {
      return true;
    }
  }

  return false;
}

void PipelineOrchestrator::initPipeline(uint8_t pipelineType, uint16_t opCode, uint8_t srcReg1, uint8_t srcReg2, uint8_t destReg, uint8_t destFieldMask, uint8_t srcReg1FieldMask, uint8_t srcReg2FieldMask, uint16_t instructionAddress, int16_t immediate)
{
  waiting.push_back(configurePipeline(
    pipelineType,
    opCode,
    srcReg1,
    srcReg2,
    destReg,
    destFieldMask,
    srcReg1FieldMask,
    srcReg2FieldMask,
    instructionAddress,
    false,
    immediate));
}

Pipeline *PipelineOrchestrator::startPipeline(uint8_t pipelineType, uint16_t opCode, uint8_t srcReg1, uint8_t srcReg2, uint8_t destReg, uint8_t destFieldMask, uint8_t srcReg1FieldMask, uint8_t srcReg2FieldMask, uint16_t instructionAddress, bool discardWriteback, int16_t immediate)
{
  Pipeline *pipeline = configurePipeline(
    pipelineType,
    opCode,
    srcReg1,
    srcReg2,
    destReg,
    destFieldMask,
    srcReg1FieldMask,
    srcReg2FieldMask,
    instructionAddress,
    discardWriteback,
    immediate);
  executing.push_back(pipeline);
  stalling = stalling || hasStructuralHazard(pipeline);

  if (pipelineHandler)
  {
    pipelineHandler->pipelineStarted(pipeline);
  }

  return pipeline;
}

Pipeline *PipelineOrchestrator::configurePipeline(uint8_t pipelineType, uint16_t opCode, uint8_t srcReg1, uint8_t srcReg2, uint8_t destReg, uint8_t destFieldMask, uint8_t srcReg1FieldMask, uint8_t srcReg2FieldMask, uint16_t instructionAddress, bool discardWriteback, int16_t immediate)
{
  if (pipelineType != VPU_PIPELINE_TYPE_FMAC &&
      pipelineType != VPU_PIPELINE_TYPE_FDIV &&
      pipelineType != VPU_PIPELINE_TYPE_EFU &&
      pipelineType != VPU_PIPELINE_TYPE_IALU &&
      pipelineType != VPU_PIPELINE_TYPE_XGKICK &&
      pipelineType != VPU_PIPELINE_TYPE_LSU &&
      pipelineType != VPU_PIPELINE_TYPE_BRANCH &&
      pipelineType != VPU_PIPELINE_TYPE_I_REGISTER &&
      pipelineType != VPU_PIPELINE_TYPE_WAITQ &&
      pipelineType != VPU_PIPELINE_TYPE_WAITP)
  {
    throw std::runtime_error(
      "VU pipeline type does not have defined stage timing.");
  }

  if (pool.size() == 0)
  {
    throw std::runtime_error("Trying to add a pipeline to the PipelineOrchestrator when the max number of pipelines is already in use!");
  }
  Pipeline * pipeline = pool.front();
  pipeline->configure(pipelineType, opCode, srcReg1, srcReg2, destReg, destFieldMask, srcReg1FieldMask, srcReg2FieldMask, instructionAddress, discardWriteback, immediate);
  pool.pop_front();
  return pipeline;
}

void PipelineOrchestrator::setPipelineHandler(PipelineHandler * handler)
{
  pipelineHandler = handler;
}
