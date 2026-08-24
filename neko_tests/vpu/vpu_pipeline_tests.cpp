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
    std::vector<VUPipelineStage> advancedStages;
    std::vector<uint8_t> advancedStageIndices;

    void pipelineStarted(Pipeline *pipeline) override
    {
      startedPipeline = pipeline;
    }

    void pipelineAdvanced(Pipeline *pipeline) override
    {
      advancedStages.push_back(pipeline->stage());
      advancedStageIndices.push_back(pipeline->stageIndex());
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

  SECTION("The FMAC pipeline advances through explicit hardware stages")
  {
    TestPipelineHandler handler;
    orchestrator.setPipelineHandler(&handler);
    orchestrator.initPipeline(
      VPU_PIPELINE_TYPE_FMAC,
      VPU_ADD,
      VPU_REGISTER_VF02,
      VPU_REGISTER_VF03,
      VPU_REGISTER_VF01,
      FP_REGISTER_X_FIELD,
      FP_REGISTER_X_FIELD,
      FP_REGISTER_X_FIELD);

    orchestrator.update();
    REQUIRE(handler.startedPipeline->stage() == VUPipelineStage::M);
    orchestrator.update();
    REQUIRE(handler.startedPipeline->stage() == VUPipelineStage::T);
    orchestrator.update();
    REQUIRE(handler.startedPipeline->stage() == VUPipelineStage::X);
    orchestrator.update();
    REQUIRE(handler.startedPipeline->stage() == VUPipelineStage::Y);
    orchestrator.update();
    REQUIRE(handler.startedPipeline->stage() == VUPipelineStage::Z);
    orchestrator.update();
    REQUIRE(handler.finishedPipeline->stage() == VUPipelineStage::S);
  }

  SECTION("IALU uses explicit X, y, and z execution stages")
  {
    TestPipelineHandler handler;
    orchestrator.setPipelineHandler(&handler);
    orchestrator.initPipeline(
      VPU_PIPELINE_TYPE_IALU,
      VPU_IADD,
      VPU_REGISTER_VI01,
      VPU_REGISTER_VI02,
      VPU_REGISTER_VI03,
      FP_REGISTER_NO_FIELDS,
      FP_REGISTER_NO_FIELDS,
      FP_REGISTER_NO_FIELDS);

    REQUIRE(runOrchestrator(&orchestrator) == 6);
    REQUIRE(handler.advancedStages == std::vector<VUPipelineStage>{
      VUPipelineStage::T,
      VUPipelineStage::X,
      VUPipelineStage::IY,
      VUPipelineStage::IZ,
      VUPipelineStage::S
    });
  }

  SECTION("LSU uses the FMAC-shaped six-stage pipeline")
  {
    TestPipelineHandler handler;
    orchestrator.setPipelineHandler(&handler);
    orchestrator.initPipeline(
      VPU_PIPELINE_TYPE_LSU,
      VPU_LQ,
      VPU_REGISTER_VI01,
      VPU_REGISTER_VI02,
      VPU_REGISTER_VI03,
      FP_REGISTER_NO_FIELDS,
      FP_REGISTER_NO_FIELDS,
      FP_REGISTER_NO_FIELDS);

    REQUIRE(runOrchestrator(&orchestrator) == 6);
    REQUIRE(handler.advancedStages == std::vector<VUPipelineStage>{
      VUPipelineStage::T,
      VUPipelineStage::X,
      VUPipelineStage::Y,
      VUPipelineStage::Z,
      VUPipelineStage::S
    });
  }

  SECTION("The branch pipeline completes at T in two cycles")
  {
    TestPipelineHandler handler;
    orchestrator.setPipelineHandler(&handler);
    orchestrator.initPipeline(
      VPU_PIPELINE_TYPE_BRANCH,
      VPU_IBNE,
      VPU_REGISTER_VI01,
      VPU_REGISTER_VI02,
      VPU_REGISTER_VI00,
      FP_REGISTER_NO_FIELDS,
      FP_REGISTER_NO_FIELDS,
      FP_REGISTER_NO_FIELDS);

    REQUIRE(runOrchestrator(&orchestrator) == 2);
    REQUIRE(handler.advancedStages ==
            std::vector<VUPipelineStage>{VUPipelineStage::T});
    REQUIRE(handler.finishedPipeline->stage() == VUPipelineStage::T);
  }

  SECTION("I-register writes become available at T in two cycles")
  {
    TestPipelineHandler handler;
    orchestrator.setPipelineHandler(&handler);
    orchestrator.initPipeline(
      VPU_PIPELINE_TYPE_I_REGISTER,
      0,
      0,
      0,
      0,
      FP_REGISTER_NO_FIELDS,
      FP_REGISTER_NO_FIELDS,
      FP_REGISTER_NO_FIELDS);

    REQUIRE(runOrchestrator(&orchestrator) == 2);
    REQUIRE(handler.advancedStages ==
            std::vector<VUPipelineStage>{VUPipelineStage::T});
    REQUIRE(handler.finishedPipeline->stage() == VUPipelineStage::T);
  }

  SECTION("DIV and SQRT use six FDIV execution stages")
  {
    for (uint16_t opCode : {VPU_DIV, VPU_SQRT})
    {
      PipelineOrchestrator typedOrchestrator;
      TestPipelineHandler handler;
      typedOrchestrator.setPipelineHandler(&handler);
      typedOrchestrator.initPipeline(
        VPU_PIPELINE_TYPE_FDIV,
        opCode,
        VPU_REGISTER_VF01,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VF00,
        FP_REGISTER_NO_FIELDS,
        FP_REGISTER_X_FIELD,
        FP_REGISTER_X_FIELD);

      REQUIRE(runOrchestrator(&typedOrchestrator) == 9);
      REQUIRE(handler.advancedStages.front() == VUPipelineStage::T);
      REQUIRE(handler.advancedStages[1] == VUPipelineStage::D);
      REQUIRE(handler.advancedStageIndices[1] == 1);
      REQUIRE(handler.advancedStages[6] == VUPipelineStage::D);
      REQUIRE(handler.advancedStageIndices[6] == 6);
      REQUIRE(handler.advancedStages.back() == VUPipelineStage::F);
    }
  }

  SECTION("RSQRT uses twelve FDIV execution stages")
  {
    TestPipelineHandler handler;
    orchestrator.setPipelineHandler(&handler);
    orchestrator.initPipeline(
      VPU_PIPELINE_TYPE_FDIV,
      VPU_RSQRT,
      VPU_REGISTER_VF01,
      VPU_REGISTER_VF02,
      VPU_REGISTER_VF00,
      FP_REGISTER_NO_FIELDS,
      FP_REGISTER_X_FIELD,
      FP_REGISTER_X_FIELD);

    REQUIRE(runOrchestrator(&orchestrator) == 15);
    REQUIRE(handler.advancedStages[12] == VUPipelineStage::D);
    REQUIRE(handler.advancedStageIndices[12] == 12);
    REQUIRE(handler.advancedStages.back() == VUPipelineStage::F);
  }

  SECTION("EFU timing follows each instruction's documented latency")
  {
    struct EFUTiming
    {
      uint16_t opCode;
      uint8_t latency;
    };
    const EFUTiming timings[] = {
      {VPU_EATAN, 54}, {VPU_EATANxy, 54}, {VPU_EATANxz, 54},
      {VPU_EEXP, 44}, {VPU_ELENG, 18}, {VPU_ERCPR, 12},
      {VPU_ERLENG, 24}, {VPU_ERSADD, 18}, {VPU_ERSQRT, 18},
      {VPU_ESADD, 11}, {VPU_ESIN, 29}, {VPU_ESQRT, 12},
      {VPU_ESUM, 12}
    };

    for (const EFUTiming &timing : timings)
    {
      PipelineOrchestrator typedOrchestrator;
      TestPipelineHandler handler;
      typedOrchestrator.setPipelineHandler(&handler);
      typedOrchestrator.initPipeline(
        VPU_PIPELINE_TYPE_EFU,
        timing.opCode,
        VPU_REGISTER_VF01,
        VPU_REGISTER_VF00,
        VPU_REGISTER_VF00,
        FP_REGISTER_NO_FIELDS,
        FP_REGISTER_X_FIELD,
        FP_REGISTER_NO_FIELDS);

      REQUIRE(runOrchestrator(&typedOrchestrator) == timing.latency + 2);
      REQUIRE(handler.advancedStages.front() == VUPipelineStage::T);
      REQUIRE(handler.advancedStages[1] == VUPipelineStage::N);
      REQUIRE(handler.advancedStageIndices[1] == 1);
      REQUIRE(handler.advancedStages[timing.latency - 1] ==
              VUPipelineStage::N);
      REQUIRE(handler.advancedStageIndices[timing.latency - 1] ==
              timing.latency - 1);
      REQUIRE(handler.advancedStages.back() == VUPipelineStage::P);
    }
  }

  SECTION("XGKICK uses the FMAC-shaped base pipeline")
  {
    TestPipelineHandler handler;
    orchestrator.setPipelineHandler(&handler);
    orchestrator.initPipeline(
      VPU_PIPELINE_TYPE_XGKICK,
      VPU_XGKICK,
      VPU_REGISTER_VI01,
      VPU_REGISTER_VI00,
      VPU_REGISTER_VI00,
      FP_REGISTER_NO_FIELDS,
      FP_REGISTER_NO_FIELDS,
      FP_REGISTER_NO_FIELDS);

    REQUIRE(runOrchestrator(&orchestrator) == 6);
    REQUIRE(handler.advancedStages == std::vector<VUPipelineStage>{
      VUPipelineStage::T,
      VUPipelineStage::X,
      VUPipelineStage::Y,
      VUPipelineStage::Z,
      VUPipelineStage::S
    });
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

    REQUIRE(runOrchestrator(&orchestrator, 1) == 11);
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

    REQUIRE(runOrchestrator(&orchestrator, 1) == 11);
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

    REQUIRE(runOrchestrator(&orchestrator, 1) == 11);
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

  SECTION("Pipeline types without explicit stage timing are rejected")
  {
    REQUIRE_THROWS_WITH(
      orchestrator.initPipeline(
        VPU_PIPELINE_TYPE_NONE, VPU_ADD,
        VPU_REGISTER_VF02, VPU_REGISTER_VF03, VPU_REGISTER_VF01,
        FP_REGISTER_X_FIELD, FP_REGISTER_X_FIELD, FP_REGISTER_X_FIELD),
      "VU pipeline type does not have defined stage timing.");
  }

  SECTION("Variable pipelines reject operations without timing definitions")
  {
    REQUIRE_THROWS_WITH(
      orchestrator.initPipeline(
        VPU_PIPELINE_TYPE_FDIV, VPU_ADD,
        VPU_REGISTER_VF02, VPU_REGISTER_VF03, VPU_REGISTER_VF01,
        FP_REGISTER_X_FIELD, FP_REGISTER_X_FIELD, FP_REGISTER_X_FIELD),
      "VU FDIV operation does not have defined stage timing.");
    REQUIRE_THROWS_WITH(
      orchestrator.initPipeline(
        VPU_PIPELINE_TYPE_EFU, VPU_ADD,
        VPU_REGISTER_VF02, VPU_REGISTER_VF03, VPU_REGISTER_VF01,
        FP_REGISTER_X_FIELD, FP_REGISTER_X_FIELD, FP_REGISTER_X_FIELD),
      "VU EFU operation does not have defined stage timing.");
  }
}
