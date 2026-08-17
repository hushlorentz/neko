#include "catch.hpp"
#include "fp_register.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_pipeline.hpp"
#include "vpu_pipeline_handler.hpp"
#include "vpu_pipeline_orchestrator.hpp"
#include "vpu_register_ids.hpp"

using namespace Catch;

class TestPipelineHandler : public PipelineHandler
{
  public:
    Pipeline *startedPipeline = nullptr;
    Pipeline *finishedPipeline = nullptr;

    void pipelineStarted(Pipeline *pipeline) override
    {
      startedPipeline = pipeline;
    }

    void pipelineFinished(Pipeline *pipeline) override
    {
      finishedPipeline = pipeline;
    }
};

namespace
{
  int runOrchestrator(PipelineOrchestrator *orchestrator, int cycles = 0)
  {
    while (orchestrator->hasNext())
    {
      orchestrator->update();
      cycles++;
    }

    return cycles;
  }
}

TEST_CASE("VPU Pipeline Tests")
{
  PipelineOrchestrator orchestrator;

  SECTION("The FMAC pipeline executes in 6 cycles")
  {
    orchestrator.initPipeline(
      VPU_PIPELINE_TYPE_FMAC,
      VPU_ADD,
      VPU_REGISTER_VF02,
      VPU_REGISTER_VF03,
      VPU_REGISTER_VF01,
      FP_REGISTER_X_FIELD,
      FP_REGISTER_X_FIELD,
      FP_REGISTER_X_FIELD);

    REQUIRE(runOrchestrator(&orchestrator) == 6);
  }

  SECTION("A pipeline retains explicit source and destination lane masks")
  {
    TestPipelineHandler handler;
    orchestrator.setPipelineHandler(&handler);
    orchestrator.initPipeline(
      VPU_PIPELINE_TYPE_FMAC,
      VPU_ADDx,
      VPU_REGISTER_VF02,
      VPU_REGISTER_VF03,
      VPU_REGISTER_VF01,
      FP_REGISTER_Y_FIELD,
      FP_REGISTER_X_FIELD,
      FP_REGISTER_Y_FIELD);

    runOrchestrator(&orchestrator);

    REQUIRE(handler.startedPipeline == handler.finishedPipeline);
    REQUIRE(handler.finishedPipeline->destFieldMask == FP_REGISTER_Y_FIELD);
    REQUIRE(handler.finishedPipeline->srcReg1FieldMask == FP_REGISTER_X_FIELD);
    REQUIRE(handler.finishedPipeline->srcReg2FieldMask == FP_REGISTER_Y_FIELD);
  }

  SECTION("Independent FMAC pipelines execute without a stall")
  {
    orchestrator.initPipeline(
      VPU_PIPELINE_TYPE_FMAC, VPU_ADD,
      VPU_REGISTER_VF01, VPU_REGISTER_VF02, VPU_REGISTER_VF03,
      FP_REGISTER_X_FIELD, FP_REGISTER_X_FIELD, FP_REGISTER_X_FIELD);
    orchestrator.update();

    orchestrator.initPipeline(
      VPU_PIPELINE_TYPE_FMAC, VPU_ADD,
      VPU_REGISTER_VF04, VPU_REGISTER_VF05, VPU_REGISTER_VF06,
      FP_REGISTER_X_FIELD, FP_REGISTER_X_FIELD, FP_REGISTER_X_FIELD);

    REQUIRE(runOrchestrator(&orchestrator, 1) == 7);
  }

  SECTION("A source stalls when it reads a pending destination lane")
  {
    orchestrator.initPipeline(
      VPU_PIPELINE_TYPE_FMAC, VPU_ADD,
      VPU_REGISTER_VF01, VPU_REGISTER_VF02, VPU_REGISTER_VF03,
      FP_REGISTER_X_FIELD, FP_REGISTER_X_FIELD, FP_REGISTER_X_FIELD);
    orchestrator.update();

    orchestrator.initPipeline(
      VPU_PIPELINE_TYPE_FMAC, VPU_ADD,
      VPU_REGISTER_VF03, VPU_REGISTER_VF05, VPU_REGISTER_VF06,
      FP_REGISTER_Y_FIELD, FP_REGISTER_X_FIELD, FP_REGISTER_Y_FIELD);

    REQUIRE(runOrchestrator(&orchestrator, 1) == 10);
  }

  SECTION("A source does not stall when it reads another lane of the same register")
  {
    orchestrator.initPipeline(
      VPU_PIPELINE_TYPE_FMAC, VPU_ADD,
      VPU_REGISTER_VF01, VPU_REGISTER_VF02, VPU_REGISTER_VF03,
      FP_REGISTER_X_FIELD, FP_REGISTER_X_FIELD, FP_REGISTER_X_FIELD);
    orchestrator.update();

    orchestrator.initPipeline(
      VPU_PIPELINE_TYPE_FMAC, VPU_ADD,
      VPU_REGISTER_VF03, VPU_REGISTER_VF05, VPU_REGISTER_VF06,
      FP_REGISTER_Y_FIELD, FP_REGISTER_Y_FIELD, FP_REGISTER_Y_FIELD);

    REQUIRE(runOrchestrator(&orchestrator, 1) == 7);
  }

  SECTION("The second source uses its own lane mask for dependencies")
  {
    orchestrator.initPipeline(
      VPU_PIPELINE_TYPE_FMAC, VPU_ADD,
      VPU_REGISTER_VF01, VPU_REGISTER_VF02, VPU_REGISTER_VF03,
      FP_REGISTER_Z_FIELD, FP_REGISTER_Z_FIELD, FP_REGISTER_Z_FIELD);
    orchestrator.update();

    orchestrator.initPipeline(
      VPU_PIPELINE_TYPE_FMAC, VPU_ADD,
      VPU_REGISTER_VF04, VPU_REGISTER_VF03, VPU_REGISTER_VF06,
      FP_REGISTER_X_FIELD, FP_REGISTER_X_FIELD, FP_REGISTER_Z_FIELD);

    REQUIRE(runOrchestrator(&orchestrator, 1) == 10);
  }

  SECTION("Broadcast dependencies use the broadcast lane rather than the destination lane")
  {
    orchestrator.initPipeline(
      VPU_PIPELINE_TYPE_FMAC, VPU_ADD,
      VPU_REGISTER_VF01, VPU_REGISTER_VF02, VPU_REGISTER_VF03,
      FP_REGISTER_X_FIELD, FP_REGISTER_X_FIELD, FP_REGISTER_X_FIELD);
    orchestrator.update();

    orchestrator.initPipeline(
      VPU_PIPELINE_TYPE_FMAC, VPU_ADDx,
      VPU_REGISTER_VF03, VPU_REGISTER_VF05, VPU_REGISTER_VF06,
      FP_REGISTER_Y_FIELD, FP_REGISTER_X_FIELD, FP_REGISTER_Y_FIELD);

    REQUIRE(runOrchestrator(&orchestrator, 1) == 10);
  }

  SECTION("The orchestrator rejects more than the maximum number of pipelines")
  {
    for (int i = 0; i < MAX_PIPELINES; i++)
    {
      orchestrator.initPipeline(
        VPU_PIPELINE_TYPE_FMAC, VPU_ADD,
        VPU_REGISTER_VF02, VPU_REGISTER_VF03, VPU_REGISTER_VF01,
        FP_REGISTER_X_FIELD, FP_REGISTER_X_FIELD, FP_REGISTER_X_FIELD);
    }

    REQUIRE_THROWS_WITH(
      orchestrator.initPipeline(
        VPU_PIPELINE_TYPE_FMAC, VPU_ADD,
        VPU_REGISTER_VF02, VPU_REGISTER_VF03, VPU_REGISTER_VF01,
        FP_REGISTER_X_FIELD, FP_REGISTER_X_FIELD, FP_REGISTER_X_FIELD),
      Contains("Trying to add a pipeline to the PipelineOrchestrator when the max number of pipelines is already in use!"));
  }
}
