#include "catch.hpp"
#include "vpu_pipeline.hpp"
#include "vpu_pipeline_orchestrator.hpp"

TEST_CASE("VPU Pipeline Tests")
{
  PipelineOrchestrator orchestrator;
  Pipeline pipeline(4);
  uint8_t cycles = 0;

  SECTION("The FMAC Pipeline executes in 4 cycles")
  {
    orchestrator.addPipeline(&pipeline);

    while (orchestrator.hasNext())
    {
      orchestrator.update();
      cycles++;
    }

    REQUIRE(cycles == 4);
  }
}
