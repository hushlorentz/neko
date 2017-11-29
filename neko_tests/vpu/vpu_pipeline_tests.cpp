#include "catch.hpp"
#include "vpu_pipeline.hpp"
#include "vpu_pipeline_orchestrator.hpp"

using namespace Catch;

uint8_t runOrchestrator(PipelineOrchestrator * orchestrator, uint8_t cyclesSoFar)
{
  uint8_t cycles = cyclesSoFar;

  while (orchestrator->hasNext())
  {
    orchestrator->update();
    cycles++;
  }

  return cycles;
}

uint8_t runSinglePipeline(uint8_t pipelineID, PipelineOrchestrator * orchestrator)
{
  orchestrator->addPipeline(pipelineID);
  return runOrchestrator(orchestrator, 0);
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

  SECTION("Two FMAC Pipelines execute in 5 cycles")
  {
    uint8_t cycles = 0;

    orchestrator.addPipeline(VPU_PIPELINE_FMAC_EXECUTIONS);
    orchestrator.update();
    cycles++;
    orchestrator.addPipeline(VPU_PIPELINE_FMAC_EXECUTIONS);

    REQUIRE(runOrchestrator(&orchestrator, cycles) == 5);
  }

  SECTION("An FMAC Pipeline followed by an FDIV Pipeline should execute in 8 cycles")
  {
    uint8_t cycles = 0;

    orchestrator.addPipeline(VPU_PIPELINE_FMAC_EXECUTIONS);
    orchestrator.update();
    cycles++;
    orchestrator.addPipeline(VPU_PIPELINE_FDIV_EXECUTIONS);

    REQUIRE(runOrchestrator(&orchestrator, cycles) == 8);
  }

  SECTION("An FDIV Pipeline followed by an FMAC Pipeline should execute in 7 cycles")
  {
    uint8_t cycles = 0;

    orchestrator.addPipeline(VPU_PIPELINE_FDIV_EXECUTIONS);
    orchestrator.update();
    cycles++;
    orchestrator.addPipeline(VPU_PIPELINE_FMAC_EXECUTIONS);

    REQUIRE(runOrchestrator(&orchestrator, cycles) == 7);
  }

  SECTION("Two FMAC Pipelines cause a stall if the second instruction reads from the first instruction's output vector-field pair")
  {
    uint8_t cycles = 0;

    orchestrator.addPipeline(VPU_PIPELINE_FMAC_EXECUTIONS);
    orchestrator.update();
    cycles++;
    orchestrator.addPipeline(VPU_PIPELINE_FMAC_EXECUTIONS);

    REQUIRE(runOrchestrator(&orchestrator, cycles) == 5);
  }

  SECTION("The PipelineOrchestrator throws an exception if we try to add more pipelines than the maximum allowed")
  {
    for (int i = 0; i < MAX_PIPELINES; i++)
    {
      orchestrator.addPipeline(0);
    }

    REQUIRE_THROWS_WITH(orchestrator.addPipeline(0), Contains("Trying to add a pipeline to the PipelineOrchestrator when the max number of pipelines is already in use!"));
  }
}
