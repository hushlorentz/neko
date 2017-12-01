#include "catch.hpp"
#include "vpu.hpp"
#include "fp_register.hpp"

TEST_CASE("FP Register Tests")
{
  SECTION("Two FP Registers can be subtracted")
  {
    FPRegister reg1(1.0f, 3.25f, -3.0f, 4.0f);
    FPRegister reg2(-2.0f, 2.75f, -4.0f, 5.0f);
    FPRegister reg3;

    subFPRegisters(&reg1, &reg2, &reg3);

    REQUIRE(reg3.x == 3.0f);
    REQUIRE(reg3.y == 0.5f);
    REQUIRE(reg3.z == 1.0f);
    REQUIRE(reg3.w == -1.0f);
  }

  SECTION("Two FP Registers can be multiplied")
  {
    FPRegister reg1(1.0f, 3.5f, -3.0f, 4.0f);
    FPRegister reg2(-2.0f, 3.5f, -4.0f, 5.0f);
    FPRegister reg3;

    mulFPRegisters(&reg1, &reg2, &reg3);

    REQUIRE(reg3.x == -2.0f);
    REQUIRE(reg3.y == 12.25f);
    REQUIRE(reg3.z == 12.0f);
    REQUIRE(reg3.w == 20.0f);
  }
  
  SECTION("Two FP Registers can be divided")
  {
    FPRegister reg1(1.0f, 3.5f, -3.0f, 10.0f);
    FPRegister reg2(-2.0f, 3.5f, -4.0f, 5.0f);
    FPRegister reg3;

    divFPRegisters(&reg1, &reg2, &reg3);

    REQUIRE(reg3.x == -0.5f);
    REQUIRE(reg3.y == 1.0f);
    REQUIRE(reg3.z == 0.75f);
    REQUIRE(reg3.w == 2.0f);
  }

  SECTION("Adding the x field leaves the other fields unchanged")
  {
    FPRegister reg1(3, 2, 4, 5);
    FPRegister reg2(3, 2, 4, 5);
    FPRegister reg3(0, 0, 2, 1);

    addFPRegisters(&reg1, &reg2, &reg3, FP_REGISTER_X_FIELD);

    REQUIRE(reg3.x == 6);
    REQUIRE(reg3.y == 0);
    REQUIRE(reg3.z == 2);
    REQUIRE(reg3.w == 1);
  }

  SECTION("Adding the y field leaves the other fields unchanged")
  {
    FPRegister reg1(3, 2, 4, 5);
    FPRegister reg2(3, 2, 4, 5);
    FPRegister reg3(0, 0, 2, 1);

    addFPRegisters(&reg1, &reg2, &reg3, FP_REGISTER_Y_FIELD);

    REQUIRE(reg3.x == 0);
    REQUIRE(reg3.y == 4);
    REQUIRE(reg3.z == 2);
    REQUIRE(reg3.w == 1);
  }

  SECTION("Adding the z field leaves the other fields unchanged")
  {
    FPRegister reg1(3, 2, 4, 5);
    FPRegister reg2(3, 2, 4, 5);
    FPRegister reg3(0, 0, 2, 1);

    addFPRegisters(&reg1, &reg2, &reg3, FP_REGISTER_Z_FIELD);

    REQUIRE(reg3.x == 0);
    REQUIRE(reg3.y == 0);
    REQUIRE(reg3.z == 8);
    REQUIRE(reg3.w == 1);
  }

  SECTION("Adding the w field leaves the other fields unchanged")
  {
    FPRegister reg1(3, 2, 4, 5);
    FPRegister reg2(3, 2, 4, 5);
    FPRegister reg3(0, 0, 2, 1);

    addFPRegisters(&reg1, &reg2, &reg3, FP_REGISTER_W_FIELD);

    REQUIRE(reg3.x == 0);
    REQUIRE(reg3.y == 0);
    REQUIRE(reg3.z == 2);
    REQUIRE(reg3.w == 10);
  }

  SECTION("All fields in Two FP Registers can be added")
  {
    FPRegister reg1(1.0f, 2.11f, -3.0f, 4.0f);
    FPRegister reg2(-2.0f, 3.05f, -4.0f, 5.0f);
    FPRegister reg3;

    addFPRegisters(&reg1, &reg2, &reg3, FP_REGISTER_ALL_FIELDS);

    REQUIRE(reg3.x == -1.0f);
    REQUIRE(reg3.y == 5.16f);
    REQUIRE(reg3.z == -7.0f);
    REQUIRE(reg3.w == 9.0f);
  }

}
