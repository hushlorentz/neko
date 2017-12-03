#include "catch.hpp"
#include "fp_register.hpp"
#include "vpu.hpp"
#include "vpu_completed_pipeline_handler.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_pipeline.hpp"
#include "vpu_pipeline_orchestrator.hpp"
#include "vpu_register_ids.hpp"

using namespace Catch;

class PipelineHandler : public CompletedPipelineHandler
{
  public:
    Pipeline * handledPipeline;

    virtual void pipelineComplete(Pipeline * pipeline)
    {
      handledPipeline = pipeline;
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

  SECTION("The FMAC Pipeline executes in 4 cycles")
  {
    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, 0, 0, 0, 0, VPU_REGISTER_VF02, 0, VPU_REGISTER_VF01, FP_REGISTER_X_FIELD, FP_REGISTER_X_FIELD);
    REQUIRE(runOrchestrator(&orchestrator, 0) == 4);
  }

  SECTION("When a pipeline finishes, we can retrieve the correct information")
  {
    PipelineHandler handler;
    orchestrator.setPipelineHandler(&handler);
    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 10, 20, 30, 40, 50, VPU_REGISTER_VF02, VPU_REGISTER_VF03, VPU_REGISTER_VF01, FP_REGISTER_X_FIELD, FP_REGISTER_X_FIELD);

    while (orchestrator.hasNext())
    {
      orchestrator.update();
    }

    Pipeline * pipeline = handler.handledPipeline;
    REQUIRE(pipeline->type == VPU_PIPELINE_TYPE_FMAC);
    REQUIRE(pipeline->intResult == 10);
    REQUIRE(pipeline->xResult == 20);
    REQUIRE(pipeline->yResult == 30);
    REQUIRE(pipeline->zResult == 40);
    REQUIRE(pipeline->wResult == 50);
    REQUIRE(pipeline->sourceReg1 == VPU_REGISTER_VF02);
    REQUIRE(pipeline->sourceReg2 == VPU_REGISTER_VF03);
    REQUIRE(pipeline->destReg == VPU_REGISTER_VF01);
  }

  SECTION("Two FMAC Pipelines execute in 5 cycles with no stall")
  {
    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, 0, 0, 0, 0, VPU_REGISTER_VF02, 0, VPU_REGISTER_VF01, FP_REGISTER_X_FIELD, FP_REGISTER_X_FIELD);
    orchestrator.update();
    cycles++;
    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, 0, 0, 0, 0, VPU_REGISTER_VF03, 0, VPU_REGISTER_VF01, FP_REGISTER_X_FIELD, FP_REGISTER_X_FIELD);

    REQUIRE(runOrchestrator(&orchestrator, cycles) == 5);
  }

  SECTION("Two FMAC Pipelines cause a stall if the second instruction reads from the first instruction's output vector-field pair")
  {
    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, 0, 0, 0, 0, VPU_REGISTER_VF01, 0, VPU_REGISTER_VF02, FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD, 0);
    orchestrator.update();
    cycles++;
    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, 0, 0, 0, 0, VPU_REGISTER_VF02, 0, VPU_REGISTER_VF03, FP_REGISTER_Y_FIELD, 0);

    REQUIRE(runOrchestrator(&orchestrator, cycles) == 8);
  }

  SECTION("Two FMAC Pipelines cause a stall if the second instruction reads from the first instruction's output vector-field pair with a non-stall in between")
  {
    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, 0, 0, 0, 0, VPU_REGISTER_VF01, 0, VPU_REGISTER_VF02, FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD, FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD);
    orchestrator.update();
    cycles++;

    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, 0, 0, 0, 0, VPU_REGISTER_VF10, 0, VPU_REGISTER_VF12, FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD, FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD);
    orchestrator.update();
    cycles++;

    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, 0, 0, 0, 0, VPU_REGISTER_VF02, 0, VPU_REGISTER_VF03, FP_REGISTER_Y_FIELD, FP_REGISTER_Y_FIELD);

    REQUIRE(runOrchestrator(&orchestrator, cycles) == 8);
  }

  SECTION("Three FMAC Pipelines all stall if they all read from the same vector fields")
  {
    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, 0, 0, 0, 0, VPU_REGISTER_VF01, 0, VPU_REGISTER_VF02, FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD, 0);
    orchestrator.update();
    cycles++;

    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, 0, 0, 0, 0, VPU_REGISTER_VF02, 0, VPU_REGISTER_VF03, FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD, 0);
    orchestrator.update();
    cycles++;

    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, 0, 0, 0, 0, VPU_REGISTER_VF03, 0, VPU_REGISTER_VF04, FP_REGISTER_Y_FIELD, 0);

    REQUIRE(runOrchestrator(&orchestrator, cycles) == 12);
  }

  SECTION("Three FMAC Pipelines middle one stalls")
  {
    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, 0, 0, 0, 0, VPU_REGISTER_VF01, 0, VPU_REGISTER_VF02, FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD, 0);
    orchestrator.update();
    cycles++;

    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, 0, 0, 0, 0, VPU_REGISTER_VF02, 0, VPU_REGISTER_VF03, FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD, 0);
    orchestrator.update();
    cycles++;

    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, 0, 0, 0, 0, VPU_REGISTER_VF05, 0, VPU_REGISTER_VF04, FP_REGISTER_Y_FIELD, 0);

    REQUIRE(runOrchestrator(&orchestrator, cycles) == 9);
  }

  SECTION("BCField Two FMAC Pipelines execute in 5 cycles with no stall")
  {
    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, 0, 0, 0, 0, VPU_REGISTER_VF02, 0, VPU_REGISTER_VF01, FP_REGISTER_X_FIELD, FP_REGISTER_X_FIELD);
    orchestrator.update();
    cycles++;
    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, 0, 0, 0, 0, VPU_REGISTER_VF02, 0, VPU_REGISTER_VF03, FP_REGISTER_Y_FIELD, FP_REGISTER_Z_FIELD);

    REQUIRE(runOrchestrator(&orchestrator, cycles) == 5);
  }

  SECTION("BCField: Two FMAC Pipelines cause a stall if the second instruction reads from the first instruction's output vector-field pair")
  {
    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, 0, 0, 0, 0, VPU_REGISTER_VF01, 0, VPU_REGISTER_VF02, FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD, 0);
    orchestrator.update();
    cycles++;
    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, 0, 0, 0, 0, VPU_REGISTER_VF03, VPU_REGISTER_VF02, VPU_REGISTER_VF04, 0, FP_REGISTER_Y_FIELD);

    REQUIRE(runOrchestrator(&orchestrator, cycles) == 8);
  }

  SECTION("BCField Two FMAC Pipelines cause a stall if the third instruction reads from the first instruction's output vector-field pair with a non-stall in between")
  {
    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, 0, 0, 0, 0, VPU_REGISTER_VF01, 0, VPU_REGISTER_VF02, FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD, 0);
    orchestrator.update();
    cycles++;

    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, 0, 0, 0, 0, VPU_REGISTER_VF10, VPU_REGISTER_VF02, VPU_REGISTER_VF12, FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD, FP_REGISTER_W_FIELD);
    orchestrator.update();
    cycles++;

    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, 0, 0, 0, 0, VPU_REGISTER_VF05, VPU_REGISTER_VF02, VPU_REGISTER_VF03, FP_REGISTER_Y_FIELD, FP_REGISTER_Y_FIELD);

    REQUIRE(runOrchestrator(&orchestrator, cycles) == 8);
  }

  SECTION("BCField Three FMAC Pipelines all stall if they all read from the same vector fields")
  {
    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, 0, 0, 0, 0, VPU_REGISTER_VF01, 0, VPU_REGISTER_VF02, FP_REGISTER_ALL_FIELDS, 0);
    orchestrator.update();
    cycles++;

    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, 0, 0, 0, 0, VPU_REGISTER_VF20, VPU_REGISTER_VF02, VPU_REGISTER_VF05, FP_REGISTER_Z_FIELD, FP_REGISTER_W_FIELD);
    orchestrator.update();
    cycles++;

    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, 0, 0, 0, 0, VPU_REGISTER_VF03, VPU_REGISTER_VF05, VPU_REGISTER_VF04, FP_REGISTER_Y_FIELD, FP_REGISTER_Z_FIELD);

    REQUIRE(runOrchestrator(&orchestrator, cycles) == 12);
  }

  SECTION("BCField Three FMAC Pipelines middle one stalls")
  {
    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, 0, 0, 0, 0, VPU_REGISTER_VF01, 0, VPU_REGISTER_VF02, FP_REGISTER_X_FIELD | FP_REGISTER_Z_FIELD, 0);
    orchestrator.update();
    cycles++;

    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, 0, 0, 0, 0, VPU_REGISTER_VF20, VPU_REGISTER_VF02, VPU_REGISTER_VF03, FP_REGISTER_Y_FIELD, FP_REGISTER_Z_FIELD);
    orchestrator.update();
    cycles++;

    orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, 0, 0, 0, 0, VPU_REGISTER_VF05, VPU_REGISTER_VF01, VPU_REGISTER_VF04, FP_REGISTER_Y_FIELD, FP_REGISTER_W_FIELD);

    REQUIRE(runOrchestrator(&orchestrator, cycles) == 9);
  }

  SECTION("The PipelineOrchestrator throws an exception if we try to add more pipelines than the maximum allowed")
  {
    for (int i = 0; i < MAX_PIPELINES; i++)
    {
      orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, 0, 0, 0, 0, VPU_REGISTER_VF02, VPU_REGISTER_VF03, VPU_REGISTER_VF01, 0, 0);
    }

    REQUIRE_THROWS_WITH(orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, 0, 0, 0, 0, 0, VPU_REGISTER_VF02, VPU_REGISTER_VF03, VPU_REGISTER_VF01, 0, 0), Contains("Trying to add a pipeline to the PipelineOrchestrator when the max number of pipelines is already in use!"));
  }
}
