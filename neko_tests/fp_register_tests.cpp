#include "catch.hpp"
#include "vpu.hpp"
#include "fp_register.hpp"

TEST_CASE("FP Register Tests")
{
  FPRegister reg1(1.0f, 3.5f, -3.0f, 5.0f);
  FPRegister reg2(-2.0f, 3.5f, -5.0f, 10.0f);
  FPRegister reg3(0, 10, 1, 2);
  FPRegister reg4(-4, -3.5f, 2.75f, -1);
  uint16_t resultFlags = 0;

  SECTION("Subtracting the x field leaves the other fields unchanged")
  {
    subFPRegisters(&reg1, &reg2, &reg3, FP_REGISTER_X_FIELD, &resultFlags);

    REQUIRE(reg3.x == 3.0f);
    REQUIRE(reg3.y == 10);
    REQUIRE(reg3.z == 1);
    REQUIRE(reg3.w == 2);
  }

  SECTION("Subtracting the y field leaves the other fields unchanged")
  {
    subFPRegisters(&reg1, &reg2, &reg3, FP_REGISTER_Y_FIELD, &resultFlags);

    REQUIRE(reg3.x == 0);
    REQUIRE(reg3.y == 0);
    REQUIRE(reg3.z == 1);
    REQUIRE(reg3.w == 2);
  }

  SECTION("Subtracting the z field leaves the other fields unchanged")
  {
    subFPRegisters(&reg1, &reg2, &reg3, FP_REGISTER_Z_FIELD, &resultFlags);

    REQUIRE(reg3.x == 0);
    REQUIRE(reg3.y == 10);
    REQUIRE(reg3.z == 2.0f);
    REQUIRE(reg3.w == 2);
  }

  SECTION("Subtracting the w field leaves the other fields unchanged")
  {
    subFPRegisters(&reg1, &reg2, &reg3, FP_REGISTER_W_FIELD, &resultFlags);

    REQUIRE(reg3.x == 0);
    REQUIRE(reg3.y == 10);
    REQUIRE(reg3.z == 1);
    REQUIRE(reg3.w == -5.0f);
  }

  SECTION("All fields of two FP Registers can be subtracted")
  {
    subFPRegisters(&reg1, &reg2, &reg3, FP_REGISTER_ALL_FIELDS, &resultFlags);

    REQUIRE(reg3.x == 3.0f);
    REQUIRE(reg3.y == 0);
    REQUIRE(reg3.z == 2.0f);
    REQUIRE(reg3.w == -5.0f);
  }

  SECTION("Multiplying the x field leaves the other fields unchanged")
  {
    mulFPRegisters(&reg1, &reg2, &reg3, FP_REGISTER_X_FIELD, &resultFlags);

    REQUIRE(reg3.x == -2.0f);
    REQUIRE(reg3.y == 10);
    REQUIRE(reg3.z == 1);
    REQUIRE(reg3.w == 2);
  }

  SECTION("Multiplying the y field leaves the other fields unchanged")
  {
    mulFPRegisters(&reg1, &reg2, &reg3, FP_REGISTER_Y_FIELD, &resultFlags);

    REQUIRE(reg3.x == 0);
    REQUIRE(reg3.y == 12.25f);
    REQUIRE(reg3.z == 1);
    REQUIRE(reg3.w == 2);
  }

  SECTION("Multiplying the z field leaves the other fields unchanged")
  {
    mulFPRegisters(&reg1, &reg2, &reg3, FP_REGISTER_Z_FIELD, &resultFlags);

    REQUIRE(reg3.x == 0);
    REQUIRE(reg3.y == 10);
    REQUIRE(reg3.z == 15.0f);
    REQUIRE(reg3.w == 2);
  }

  SECTION("Multiplying the w field leaves the other fields unchanged")
  {
    mulFPRegisters(&reg1, &reg2, &reg3, FP_REGISTER_W_FIELD, &resultFlags);

    REQUIRE(reg3.x == 0);
    REQUIRE(reg3.y == 10);
    REQUIRE(reg3.z == 1);
    REQUIRE(reg3.w == 50.0f);
  }

  SECTION("All fields of two FP Registers can be multiplied")
  {
    FPRegister reg3;

    mulFPRegisters(&reg1, &reg2, &reg3, FP_REGISTER_ALL_FIELDS, &resultFlags);

    REQUIRE(reg3.x == -2.0f);
    REQUIRE(reg3.y == 12.25f);
    REQUIRE(reg3.z == 15.0f);
    REQUIRE(reg3.w == 50.0f);
  }

  SECTION("Dividing the x field leaves the other fields unchanged")
  {
    divFPRegisters(&reg1, &reg2, &reg3, FP_REGISTER_X_FIELD, &resultFlags);

    REQUIRE(reg3.x == -0.5f);
    REQUIRE(reg3.y == 10);
    REQUIRE(reg3.z == 1);
    REQUIRE(reg3.w == 2);
  }

  SECTION("Dividing the y field leaves the other fields unchanged")
  {
    divFPRegisters(&reg1, &reg2, &reg3, FP_REGISTER_Y_FIELD, &resultFlags);

    REQUIRE(reg3.x == 0);
    REQUIRE(reg3.y == 1.0f);
    REQUIRE(reg3.z == 1);
    REQUIRE(reg3.w == 2);
  }

  SECTION("Dividing the z field leaves the other fields unchanged")
  {
    divFPRegisters(&reg1, &reg2, &reg3, FP_REGISTER_Z_FIELD, &resultFlags);

    REQUIRE(reg3.x == 0);
    REQUIRE(reg3.y == 10);
    REQUIRE(reg3.z == 0.6f);
    REQUIRE(reg3.w == 2);
  }

  SECTION("Dividing the w field leaves the other fields unchanged")
  {
    divFPRegisters(&reg1, &reg2, &reg3, FP_REGISTER_W_FIELD, &resultFlags);

    REQUIRE(reg3.x == 0);
    REQUIRE(reg3.y == 10);
    REQUIRE(reg3.z == 1);
    REQUIRE(reg3.w == 0.5f);
  }

  SECTION("All fields of two FP Registers can be divided")
  {
    divFPRegisters(&reg1, &reg2, &reg3, FP_REGISTER_ALL_FIELDS, &resultFlags);

    REQUIRE(reg3.x == -0.5f);
    REQUIRE(reg3.y == 1.0f);
    REQUIRE(reg3.z == 0.6f);
    REQUIRE(reg3.w == 0.5f);
  }

  SECTION("Adding the x field leaves the other fields unchanged")
  {
    addFPRegisters(&reg1, &reg2, &reg3, FP_REGISTER_X_FIELD, &resultFlags);

    REQUIRE(reg3.x == -1);
    REQUIRE(reg3.y == 10);
    REQUIRE(reg3.z == 1);
    REQUIRE(reg3.w == 2);
  }

  SECTION("Adding the y field leaves the other fields unchanged")
  {
    addFPRegisters(&reg1, &reg2, &reg3, FP_REGISTER_Y_FIELD, &resultFlags);

    REQUIRE(reg3.x == 0);
    REQUIRE(reg3.y == 7);
    REQUIRE(reg3.z == 1);
    REQUIRE(reg3.w == 2);
  }

  SECTION("Adding the z field leaves the other fields unchanged")
  {
    addFPRegisters(&reg1, &reg2, &reg3, FP_REGISTER_Z_FIELD, &resultFlags);

    REQUIRE(reg3.x == 0);
    REQUIRE(reg3.y == 10);
    REQUIRE(reg3.z == -8);
    REQUIRE(reg3.w == 2);
  }

  SECTION("Adding the w field leaves the other fields unchanged")
  {
    addFPRegisters(&reg1, &reg2, &reg3, FP_REGISTER_W_FIELD, &resultFlags);

    REQUIRE(reg3.x == 0);
    REQUIRE(reg3.y == 10);
    REQUIRE(reg3.z == 1);
    REQUIRE(reg3.w == 15);
  }

  SECTION("All fields of two FP Registers can be added")
  {
    addFPRegisters(&reg1, &reg2, &reg3, FP_REGISTER_ALL_FIELDS, &resultFlags);

    REQUIRE(reg3.x == -1);
    REQUIRE(reg3.y == 7);
    REQUIRE(reg3.z == -8);
    REQUIRE(reg3.w == 15);
  }

  SECTION("Taking the absolute value of the x field leaves the other fields unchanged")
  {
    absFPRegisters(&reg4, &reg3, FP_REGISTER_X_FIELD);

    REQUIRE(reg3.x == 4);
    REQUIRE(reg3.y == 10);
    REQUIRE(reg3.z == 1);
    REQUIRE(reg3.w == 2);
  }

  SECTION("Taking the absolute value of the y field leaves the other fields unchanged")
  {
    absFPRegisters(&reg4, &reg3, FP_REGISTER_Y_FIELD);

    REQUIRE(reg3.x == 0);
    REQUIRE(reg3.y == 3.5);
    REQUIRE(reg3.z == 1);
    REQUIRE(reg3.w == 2);
  }

  SECTION("Taking the absolute value of the z field leaves the other fields unchanged")
  {
    absFPRegisters(&reg4, &reg3, FP_REGISTER_Z_FIELD);

    REQUIRE(reg3.x == 0);
    REQUIRE(reg3.y == 10);
    REQUIRE(reg3.z == 2.75f);
    REQUIRE(reg3.w == 2);
  }

  SECTION("Taking the absolute value of the w field leaves the other fields unchanged")
  {
    absFPRegisters(&reg4, &reg3, FP_REGISTER_W_FIELD);

    REQUIRE(reg3.x == 0);
    REQUIRE(reg3.y == 10);
    REQUIRE(reg3.z == 1);
    REQUIRE(reg3.w == 1);
  }

  SECTION("We can take the absolute value of all fields of an FP Register")
  {
    absFPRegisters(&reg4, &reg3, FP_REGISTER_ALL_FIELDS);

    REQUIRE(reg3.x == 4);
    REQUIRE(reg3.y == 3.5);
    REQUIRE(reg3.z == 2.75);
    REQUIRE(reg3.w == 1);
  }
}
