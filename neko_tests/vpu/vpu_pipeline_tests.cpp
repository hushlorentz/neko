#include "catch.hpp"
#include "fp_register.hpp"
#include "vpu.hpp"
#include "vpu_pipeline_handler.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_pipeline.hpp"
#include "vpu_pipeline_orchestrator.hpp"
#include "vpu_register_ids.hpp"

using namespace Catch;

class TestPipelineHandler : public PipelineHandler
{
  public:
    Pipeline * startedPipeline;
    Pipeline * finishedPipeline;

    virtual void pipelineStarted(Pipeline * pipeline)
    {
      startedPipeline = pipeline;
    }

    virtual void pipelineFinished(Pipeline * pipeline)
    {
      finishedPipeline = pipeline;
    }
};

int runOrchestrator(PipelineOrchestrator * orchestrator, int cyclesSoFar)
{
  int cycles = cyclesSoFar;

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
  int cycles = 0;

  SECTION("The FMAC Pipeline executes in 6 cycles")
  {
    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, VPU_REGISTER_VF02, 0, VPU_REGISTER_VF01, FP_REGISTER_X_FIELD, FP_REGISTER_X_FIELD);
    REQUIRE(runOrchestrator(&orchestrator, cycles) == 6);
  }

  SECTION("When a pipeline finishes, we can retrieve the correct information")
  {
    TestPipelineHandler handler;
    orchestrator.setPipelineHandler(&handler);
    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, VPU_ABS, VPU_REGISTER_VF02, VPU_REGISTER_VF03, VPU_REGISTER_VF01, FP_REGISTER_X_FIELD, FP_REGISTER_X_FIELD);

    while (orchestrator.hasNext())
    {
      orchestrator.update();
    }

    Pipeline * startedPipeline = handler.startedPipeline;
    REQUIRE(startedPipeline->type == VPU_PIPELINE_TYPE_FMAC);
    REQUIRE(startedPipeline->opCode == VPU_ABS);
    REQUIRE(startedPipeline->srcReg1 == VPU_REGISTER_VF02);
    REQUIRE(startedPipeline->srcReg2 == VPU_REGISTER_VF03);
    REQUIRE(startedPipeline->destReg == VPU_REGISTER_VF01);

    Pipeline * finishedPipeline = handler.finishedPipeline;
    REQUIRE(finishedPipeline->type == VPU_PIPELINE_TYPE_FMAC);
    REQUIRE(startedPipeline->opCode == VPU_ABS);
    REQUIRE(finishedPipeline->srcReg1 == VPU_REGISTER_VF02);
    REQUIRE(finishedPipeline->srcReg2 == VPU_REGISTER_VF03);
    REQUIRE(finishedPipeline->destReg == VPU_REGISTER_VF01);
  }

  SECTION("Two FMAC Pipelines execute in 7 cycles with no stall")
  {
    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, VPU_REGISTER_VF02, 0, VPU_REGISTER_VF01, FP_REGISTER_X_FIELD, FP_REGISTER_X_FIELD);
    orchestrator.update();
    cycles++;

    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, VPU_REGISTER_VF03, 0, VPU_REGISTER_VF01, FP_REGISTER_X_FIELD, FP_REGISTER_X_FIELD);

    REQUIRE(runOrchestrator(&orchestrator, cycles) == 7);
  }

  SECTION("Two FMAC Pipelines cause a stall if the second instruction reads from the first instruction's output vector-field pair")
  {
    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, VPU_REGISTER_VF01, 0, VPU_REGISTER_VF02, FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD, 0);
    orchestrator.update();
    cycles++;

    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, VPU_REGISTER_VF02, 0, VPU_REGISTER_VF03, FP_REGISTER_Y_FIELD, 0);

    cycles = runOrchestrator(&orchestrator, cycles);
    REQUIRE(cycles == 10);
  }

  SECTION("Two FMAC Pipelines cause a stall if the second instruction reads from the first instruction's output vector-field pair with a non-stall in between")
  {
    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, VPU_REGISTER_VF01, 0, VPU_REGISTER_VF02, FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD, FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD);
    orchestrator.update();
    cycles++;

    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, VPU_REGISTER_VF10, 0, VPU_REGISTER_VF12, FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD, FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD);
    orchestrator.update();
    cycles++;

    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, VPU_REGISTER_VF02, 0, VPU_REGISTER_VF03, FP_REGISTER_Y_FIELD, FP_REGISTER_Y_FIELD);

    REQUIRE(runOrchestrator(&orchestrator, cycles) == 10);
  }

  SECTION("Three FMAC Pipelines all stall if they all read from the same vector fields")
  {
    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, VPU_REGISTER_VF01, 0, VPU_REGISTER_VF02, FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD, 0);
    orchestrator.update();
    cycles++;

    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, VPU_REGISTER_VF02, 0, VPU_REGISTER_VF03, FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD, 0);
    orchestrator.update();
    cycles++;

    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, VPU_REGISTER_VF03, 0, VPU_REGISTER_VF04, FP_REGISTER_Y_FIELD, 0);

    REQUIRE(runOrchestrator(&orchestrator, cycles) == 14);
  }

  SECTION("Three FMAC Pipelines middle one stalls")
  {
    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, VPU_REGISTER_VF01, 0, VPU_REGISTER_VF02, FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD, 0);
    orchestrator.update();
    cycles++;

    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, VPU_REGISTER_VF02, 0, VPU_REGISTER_VF03, FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD, 0);
    orchestrator.update();
    cycles++;

    while (orchestrator.stalling)
    {
      orchestrator.update();
      cycles++;
    }

    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, VPU_REGISTER_VF05, 0, VPU_REGISTER_VF04, FP_REGISTER_Y_FIELD, 0);

    cycles = runOrchestrator(&orchestrator, cycles);

    REQUIRE(cycles == 11);
  }

  SECTION("BCField Two FMAC Pipelines execute in 7 cycles with no stall")
  {
    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, VPU_REGISTER_VF02, 0, VPU_REGISTER_VF01, FP_REGISTER_X_FIELD, FP_REGISTER_X_FIELD);
    orchestrator.update();
    cycles++;
    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, VPU_REGISTER_VF02, 0, VPU_REGISTER_VF03, FP_REGISTER_Y_FIELD, FP_REGISTER_Z_FIELD);

    REQUIRE(runOrchestrator(&orchestrator, cycles) == 7);
  }

  SECTION("BCField: Two FMAC Pipelines cause a stall if the second instruction reads from the first instruction's output vector-field pair")
  {
    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, VPU_REGISTER_VF01, 0, VPU_REGISTER_VF02, FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD, 0);
    orchestrator.update();
    cycles++;
    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, VPU_REGISTER_VF03, VPU_REGISTER_VF02, VPU_REGISTER_VF04, 0, FP_REGISTER_Y_FIELD);

    REQUIRE(runOrchestrator(&orchestrator, cycles) == 10);
  }

  SECTION("BCField Two FMAC Pipelines cause a stall if the third instruction reads from the first instruction's output vector-field pair with a non-stall in between")
  {
    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, VPU_REGISTER_VF01, 0, VPU_REGISTER_VF02, FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD, 0);
    orchestrator.update();
    cycles++;

    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, VPU_REGISTER_VF10, VPU_REGISTER_VF02, VPU_REGISTER_VF12, FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD, FP_REGISTER_W_FIELD);
    orchestrator.update();
    cycles++;

    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, VPU_REGISTER_VF05, VPU_REGISTER_VF02, VPU_REGISTER_VF03, FP_REGISTER_Y_FIELD, FP_REGISTER_Y_FIELD);

    REQUIRE(runOrchestrator(&orchestrator, cycles) == 10);
  }

  SECTION("BCField Three FMAC Pipelines all stall if they all read from the same vector fields")
  {
    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, VPU_REGISTER_VF01, 0, VPU_REGISTER_VF02, FP_REGISTER_ALL_FIELDS, 0);
    orchestrator.update();
    cycles++;

    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, VPU_REGISTER_VF20, VPU_REGISTER_VF02, VPU_REGISTER_VF05, FP_REGISTER_Z_FIELD, FP_REGISTER_W_FIELD);
    orchestrator.update();
    cycles++;

    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, VPU_REGISTER_VF03, VPU_REGISTER_VF05, VPU_REGISTER_VF04, FP_REGISTER_Y_FIELD, FP_REGISTER_Z_FIELD);

    REQUIRE(runOrchestrator(&orchestrator, cycles) == 14);
  }

  SECTION("BCField Three FMAC Pipelines middle one stalls")
  {
    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, VPU_REGISTER_VF01, 0, VPU_REGISTER_VF02, FP_REGISTER_X_FIELD | FP_REGISTER_Z_FIELD, 0);
    orchestrator.update();
    cycles++;

    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, VPU_REGISTER_VF20, VPU_REGISTER_VF02, VPU_REGISTER_VF03, FP_REGISTER_Y_FIELD, FP_REGISTER_Z_FIELD);
    orchestrator.update();
    cycles++;

    while (orchestrator.stalling)
    {
      orchestrator.update();
      cycles++;
    }

    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, VPU_REGISTER_VF05, VPU_REGISTER_VF01, VPU_REGISTER_VF04, FP_REGISTER_Y_FIELD, FP_REGISTER_W_FIELD);

    REQUIRE(runOrchestrator(&orchestrator, cycles) == 11);
  }

  SECTION("The PipelineOrchestrator throws an exception if we try to add more pipelines than the maximum allowed")
  {
    for (int i = 0; i < MAX_PIPELINES; i++)
    {
      orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, VPU_REGISTER_VF02, VPU_REGISTER_VF03, VPU_REGISTER_VF01, 0, 0);
    }

    REQUIRE_THROWS_WITH(orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, VPU_REGISTER_VF02, VPU_REGISTER_VF03, VPU_REGISTER_VF01, 0, 0), Contains("Trying to add a pipeline to the PipelineOrchestrator when the max number of pipelines is already in use!"));
  }
}
