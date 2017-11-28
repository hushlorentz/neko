#include "catch.hpp"
#include "vpu_pipeline.hpp"
#include "vpu_pipeline_orchestrator.hpp"

uint8_t runSinglePipeline(uint8_t pipelineID, PipelineOrchestrator * orchestrator)
{
  uint8_t cycles = 0;
  Pipeline pipeline(pipelineID);

  orchestrator->addPipeline(&pipeline);

  while (orchestrator->hasNext())
  {
    orchestrator->update();
    cycles++;
  }

  return cycles;
}

TEST_CASE("VPU Pipeline Tests")
{
  PipelineOrchestrator orchestrator;
  uint8_t cycles = 0;

  SECTION("The FMAC Pipeline executes in 4 cycles")
  {
    REQUIRE(runSinglePipeline(VPU_PIPELINE_FMAC_EXECUTIONS, &orchestrator) == 4);
  }

  SECTION("The FDIV Pipeline executes in 7 cycles")
  {
    REQUIRE(runSinglePipeline(VPU_PIPELINE_FDIV_EXECUTIONS, &orchestrator) == 7);
  }

  SECTION("The RSQRT Pipeline executes in 13 cycles")
  {
    REQUIRE(runSinglePipeline(VPU_PIPELINE_RSQRT_EXECUTIONS, &orchestrator) == 13);
  }

  SECTION("The IALU Pipeline executes in 13 cycles")
  {
    REQUIRE(runSinglePipeline(VPU_PIPELINE_IALU_EXECUTIONS, &orchestrator) == 4);
  }
}
