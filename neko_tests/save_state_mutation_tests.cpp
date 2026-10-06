#include "catch.hpp"
#include "save_state_mutation.hpp"

#include "neko_system.hpp"

TEST_CASE("Save-state mutations resolve exact schema paths")
{
  NekoSystem source;
  SaveStateMutation mutation(source.saveState());
  mutation.writeScalar("system.input.buttons", 0x12);
  mutation.writeScalar("system.input.leftStickX", 0x34);
  mutation["system.input.buttons"] =
    mutation["system.input.leftStickX"];
  mutation.refreshChecksum();

  NekoSystem restored;
  restored.loadState(mutation.bytes());
  REQUIRE(restored.input().buttons == 0x34);
}

TEST_CASE("Save-state mutations stay inside resolved fields")
{
  NekoSystem source;

  SECTION("Missing paths")
  {
    SaveStateMutation mutation(source.saveState());
    REQUIRE_THROWS_WITH(
      mutation.writeScalar("system.missing", 1),
      "Save-state mutation field not found: system.missing");
  }

  SECTION("Scalar width")
  {
    SaveStateMutation mutation(source.saveState());
    REQUIRE_THROWS_WITH(
      mutation.writeScalar("system.vu0.mode", 0x100),
      "Save-state mutation value does not fit field: "
      "system.vu0.mode");
  }

  SECTION("Relative byte offsets")
  {
    SaveStateMutation mutation(source.saveState());
    REQUIRE_THROWS_WITH(
      mutation.writeByte("system.input.buttons", 2, 0),
      "Save-state mutation byte is outside field: "
      "system.input.buttons");
  }
}
