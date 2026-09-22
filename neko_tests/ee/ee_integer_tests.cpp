#include <cstdint>

#include "catch.hpp"
#include "ee_core.hpp"
#include "neko_system.hpp"

namespace
{
  std::uint32_t registerInstruction(
    std::uint8_t function,
    std::uint8_t rs,
    std::uint8_t rt,
    std::uint8_t rd,
    std::uint8_t shiftAmount = 0)
  {
    return
      (static_cast<std::uint32_t>(rs) << 21) |
      (static_cast<std::uint32_t>(rt) << 16) |
      (static_cast<std::uint32_t>(rd) << 11) |
      (static_cast<std::uint32_t>(shiftAmount) << 6) |
      function;
  }

  std::uint32_t immediateInstruction(
    std::uint8_t opcode,
    std::uint8_t rs,
    std::uint8_t rt,
    std::uint16_t immediate)
  {
    return
      (static_cast<std::uint32_t>(opcode) << 26) |
      (static_cast<std::uint32_t>(rs) << 21) |
      (static_cast<std::uint32_t>(rt) << 16) |
      immediate;
  }

  std::uint32_t nestedMmiInstruction(
    std::uint8_t primaryFunction,
    std::uint8_t nestedFunction,
    std::uint8_t rs,
    std::uint8_t rt,
    std::uint8_t rd)
  {
    return
      UINT32_C(0x70000000) |
      registerInstruction(
        primaryFunction,
        rs,
        rt,
        rd,
        nestedFunction);
  }

  void runInstruction(
    NekoSystem *system,
    std::uint32_t instruction)
  {
    system->eeBus().write32(0, instruction);
    system->eeCore().startExecution(0);
    system->clockMasterCycle();
  }

  void setRegister(
    EECore *core,
    std::uint8_t index,
    std::uint64_t low,
    std::uint64_t high =
      UINT64_C(0xfeedfacecafebeef))
  {
    core->setGeneralRegister(index, {low, high});
  }
}

TEST_CASE("EE immediate integer execution")
{
  NekoSystem system;
  EECore &core = system.eeCore();

  SECTION("LUI and logical immediates preserve the upper doubleword")
  {
    setRegister(&core, 1, UINT64_C(0x123456789abcdef0));
    runInstruction(
      &system,
      immediateInstruction(0x0f, 0, 1, 0x8000));

    REQUIRE(
      core.generalRegister(1).low ==
      UINT64_C(0xffffffff80000000));
    REQUIRE(
      core.generalRegister(1).high ==
      UINT64_C(0xfeedfacecafebeef));

    runInstruction(
      &system,
      immediateInstruction(0x0d, 1, 1, 0x1234));
    REQUIRE(
      core.generalRegister(1).low ==
      UINT64_C(0xffffffff80001234));

    runInstruction(
      &system,
      immediateInstruction(0x0c, 1, 1, 0x00ff));
    REQUIRE(core.generalRegister(1).low == 0x34);

    runInstruction(
      &system,
      immediateInstruction(0x0e, 1, 1, 0xffff));
    REQUIRE(core.generalRegister(1).low == 0xffcb);
  }

  SECTION("ADDIU sign extends its word result")
  {
    setRegister(&core, 1, 1);
    setRegister(&core, 2, 0);

    runInstruction(
      &system,
      immediateInstruction(0x09, 1, 2, 0xfffe));

    REQUIRE(
      core.generalRegister(2).low ==
      UINT64_C(0xffffffffffffffff));
    REQUIRE(
      core.generalRegister(2).high ==
      UINT64_C(0xfeedfacecafebeef));
  }

  SECTION("DADDIU performs 64-bit modulo arithmetic")
  {
    setRegister(&core, 1, UINT64_MAX);

    runInstruction(
      &system,
      immediateInstruction(0x19, 1, 2, 1));

    REQUIRE(core.generalRegister(2).low == 0);
  }

  SECTION("Signed and unsigned immediate comparisons use 64 bits")
  {
    setRegister(&core, 1, UINT64_MAX);

    runInstruction(
      &system,
      immediateInstruction(0x0a, 1, 2, 0));
    REQUIRE(core.generalRegister(2).low == 1);

    runInstruction(
      &system,
      immediateInstruction(0x0b, 1, 3, 0));
    REQUIRE(core.generalRegister(3).low == 0);

    runInstruction(
      &system,
      immediateInstruction(0x0b, 1, 4, 0xffff));
    REQUIRE(core.generalRegister(4).low == 0);
  }
}

TEST_CASE("EE register integer execution")
{
  NekoSystem system;
  EECore &core = system.eeCore();

  SECTION("Word arithmetic wraps and sign extends")
  {
    setRegister(&core, 1, 0x7fffffff);
    setRegister(&core, 2, 1);

    runInstruction(
      &system,
      registerInstruction(0x21, 1, 2, 3));

    REQUIRE(
      core.generalRegister(3).low ==
      UINT64_C(0xffffffff80000000));

    runInstruction(
      &system,
      registerInstruction(0x23, 2, 1, 4));
    REQUIRE(
      core.generalRegister(4).low ==
      UINT64_C(0xffffffff80000002));
  }

  SECTION("Doubleword arithmetic uses modulo 64-bit results")
  {
    setRegister(&core, 1, UINT64_MAX);
    setRegister(&core, 2, 2);

    runInstruction(
      &system,
      registerInstruction(0x2d, 1, 2, 3));
    REQUIRE(core.generalRegister(3).low == 1);

    runInstruction(
      &system,
      registerInstruction(0x2f, 3, 2, 4));
    REQUIRE(core.generalRegister(4).low == UINT64_MAX);
  }

  SECTION("Logic and comparisons operate on the low doubleword")
  {
    setRegister(&core, 1, UINT64_C(0xf0f0000000000001));
    setRegister(&core, 2, UINT64_C(0x0ff0000000000003));

    runInstruction(
      &system,
      registerInstruction(0x24, 1, 2, 3));
    REQUIRE(
      core.generalRegister(3).low ==
      UINT64_C(0x00f0000000000001));

    runInstruction(
      &system,
      registerInstruction(0x25, 1, 2, 4));
    REQUIRE(
      core.generalRegister(4).low ==
      UINT64_C(0xfff0000000000003));

    runInstruction(
      &system,
      registerInstruction(0x26, 1, 2, 5));
    REQUIRE(
      core.generalRegister(5).low ==
      UINT64_C(0xff00000000000002));

    runInstruction(
      &system,
      registerInstruction(0x27, 1, 2, 6));
    REQUIRE(
      core.generalRegister(6).low ==
      UINT64_C(0x000ffffffffffffc));

    setRegister(&core, 1, UINT64_MAX);
    setRegister(&core, 2, 0);
    runInstruction(
      &system,
      registerInstruction(0x2a, 1, 2, 7));
    runInstruction(
      &system,
      registerInstruction(0x2b, 1, 2, 8));
    REQUIRE(core.generalRegister(7).low == 1);
    REQUIRE(core.generalRegister(8).low == 0);
  }

  SECTION("Register zero remains immutable")
  {
    setRegister(&core, 1, 1);
    setRegister(&core, 2, 2);

    runInstruction(
      &system,
      registerInstruction(0x2d, 1, 2, 0));

    REQUIRE(core.generalRegister(0) == EERegister128{});
  }
}

TEST_CASE("EE packed logical execution")
{
  struct Contract
  {
    std::uint8_t primaryFunction;
    std::uint8_t nestedFunction;
    EERegister128 expected;
  };
  const EERegister128 source = {
    UINT64_C(0xf0f00f0faaaa5555),
    UINT64_C(0x0123456789abcdef)
  };
  const EERegister128 target = {
    UINT64_C(0xff00ff0012345678),
    UINT64_C(0xfedcba9876543210)
  };
  const Contract contracts[] = {
    {
      0x09,
      0x12,
      {source.low & target.low, source.high & target.high}
    },
    {
      0x29,
      0x12,
      {source.low | target.low, source.high | target.high}
    },
    {
      0x09,
      0x13,
      {source.low ^ target.low, source.high ^ target.high}
    },
    {
      0x29,
      0x13,
      {
        ~(source.low | target.low),
        ~(source.high | target.high)
      }
    }
  };

  for (const Contract &contract : contracts)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, source);
    core.setGeneralRegister(2, target);
    runInstruction(
      &system,
      nestedMmiInstruction(
        contract.primaryFunction,
        contract.nestedFunction,
        1,
        2,
        3));
    REQUIRE(core.generalRegister(3).low == contract.expected.low);
    REQUIRE(core.generalRegister(3).high == contract.expected.high);
  }

  SECTION("Source and destination aliases use captured operands")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, source);
    core.setGeneralRegister(2, target);
    runInstruction(
      &system,
      nestedMmiInstruction(0x09, 0x13, 1, 2, 1));
    REQUIRE(core.generalRegister(1).low == (source.low ^ target.low));
    REQUIRE(core.generalRegister(1).high == (source.high ^ target.high));

    core.setGeneralRegister(1, source);
    core.setGeneralRegister(2, target);
    runInstruction(
      &system,
      nestedMmiInstruction(0x29, 0x12, 1, 2, 2));
    REQUIRE(core.generalRegister(2).low == (source.low | target.low));
    REQUIRE(core.generalRegister(2).high == (source.high | target.high));

    core.setGeneralRegister(1, source);
    runInstruction(
      &system,
      nestedMmiInstruction(0x09, 0x12, 1, 1, 3));
    REQUIRE(core.generalRegister(3).low == source.low);
    REQUIRE(core.generalRegister(3).high == source.high);
  }

  SECTION("Register zero discards the complete result")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, source);
    core.setGeneralRegister(2, target);
    runInstruction(
      &system,
      nestedMmiInstruction(0x29, 0x12, 1, 2, 0));
    REQUIRE(core.generalRegister(0).low == 0);
    REQUIRE(core.generalRegister(0).high == 0);
  }

  SECTION("Repeated execution replaces both destination halves")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, source);
    core.setGeneralRegister(2, target);
    runInstruction(
      &system,
      nestedMmiInstruction(0x09, 0x12, 1, 2, 3));

    core.setGeneralRegister(
      1,
      {UINT64_C(0x1111222233334444),
       UINT64_C(0x5555666677778888)});
    core.setGeneralRegister(
      2,
      {UINT64_C(0xffff0000ffff0000),
       UINT64_C(0x0000ffff0000ffff)});
    runInstruction(
      &system,
      nestedMmiInstruction(0x29, 0x13, 1, 2, 3));

    REQUIRE(
      core.generalRegister(3).low ==
      UINT64_C(0x0000dddd0000bbbb));
    REQUIRE(
      core.generalRegister(3).high ==
      UINT64_C(0xaaaa000088880000));
  }
}

TEST_CASE("EE packed equality execution")
{
  struct Contract
  {
    std::uint8_t nestedFunction;
    EERegister128 source;
    EERegister128 target;
    EERegister128 expected;
  };
  const Contract contracts[] = {
    {
      0x0a,
      {
        UINT64_C(0x0011223344556677),
        UINT64_C(0x8899aabbccddeeff)
      },
      {
        UINT64_C(0x0011aa33bb55cc77),
        UINT64_C(0x889900bbcc00ee00)
      },
      {
        UINT64_C(0xffff00ff00ff00ff),
        UINT64_C(0xffff00ffff00ff00)
      }

    },
    {
      0x06,
      {
        UINT64_C(0x1111222233334444),
        UINT64_C(0xaaaabbbbccccdddd)
      },
      {
        UINT64_C(0x1111aaaa3333bbbb),
        UINT64_C(0xaaaaffff0000dddd)
      },
      {
        UINT64_C(0xffff0000ffff0000),
        UINT64_C(0xffff00000000ffff)
      }
    },
    {
      0x02,
      {
        UINT64_C(0x1111111122222222),
        UINT64_C(0x3333333344444444)
      },
      {
        UINT64_C(0xaaaaaaaa22222222),
        UINT64_C(0x33333333bbbbbbbb)
      },
      {
        UINT64_C(0x00000000ffffffff),
        UINT64_C(0xffffffff00000000)
      }
    }
  };

  for (const Contract &contract : contracts)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, contract.source);
    core.setGeneralRegister(2, contract.target);
    runInstruction(
      &system,
      nestedMmiInstruction(
        0x28,
        contract.nestedFunction,
        1,
        2,
        3));
    REQUIRE(core.generalRegister(3).low == contract.expected.low);
    REQUIRE(core.generalRegister(3).high == contract.expected.high);
  }

  SECTION("Aliases use captured operands and register zero is immutable")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    const EERegister128 source = {
      UINT64_C(0x1111222233334444),
      UINT64_C(0x5555666677778888)
    };
    const EERegister128 target = {
      UINT64_C(0x1111aaaa3333bbbb),
      UINT64_C(0x5555cccc7777dddd)
    };
    core.setGeneralRegister(1, source);
    core.setGeneralRegister(2, target);
    runInstruction(
      &system,
      nestedMmiInstruction(0x28, 0x06, 1, 2, 1));
    REQUIRE(
      core.generalRegister(1).low ==
      UINT64_C(0xffff0000ffff0000));
    REQUIRE(
      core.generalRegister(1).high ==
      UINT64_C(0xffff0000ffff0000));

    core.setGeneralRegister(1, source);
    core.setGeneralRegister(2, source);
    runInstruction(
      &system,
      nestedMmiInstruction(0x28, 0x02, 1, 2, 2));
    REQUIRE(core.generalRegister(2).low == UINT64_MAX);
    REQUIRE(core.generalRegister(2).high == UINT64_MAX);

    runInstruction(
      &system,
      nestedMmiInstruction(0x28, 0x0a, 1, 2, 0));
    REQUIRE(core.generalRegister(0).low == 0);
    REQUIRE(core.generalRegister(0).high == 0);
  }

  SECTION("Repeated execution replaces every lane")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, {UINT64_MAX, UINT64_MAX});
    core.setGeneralRegister(2, {UINT64_MAX, UINT64_MAX});
    runInstruction(
      &system,
      nestedMmiInstruction(0x28, 0x0a, 1, 2, 3));

    core.setGeneralRegister(2, {0, 0});
    runInstruction(
      &system,
      nestedMmiInstruction(0x28, 0x0a, 1, 2, 3));
    REQUIRE(core.generalRegister(3).low == 0);
    REQUIRE(core.generalRegister(3).high == 0);
  }
}

TEST_CASE("EE wrapping packed arithmetic execution")
{
  struct Contract
  {
    std::uint8_t nestedFunction;
    EERegister128 expected;
  };
  const EERegister128 source = {
    UINT64_C(0xfffe7fff80000001),
    UINT64_C(0xffffffff7fffffff)
  };
  const EERegister128 target = {
    UINT64_C(0x020380010001ffff),
    UINT64_C(0x0000000180000001)
  };
  const Contract contracts[] = {
    {
      0x08,
      {
        UINT64_C(0x0101ff008001ff00),
        UINT64_C(0xffffff00ffffff00)
      }
    },
    {
      0x04,
      {
        UINT64_C(0x0201000080010000),
        UINT64_C(0xffff0000ffff0000)
      }
    },
    {
      0x00,
      {
        UINT64_C(0x0202000080020000),
        UINT64_C(0x0000000000000000)
      }
    },
    {
      0x09,
      {
        UINT64_C(0xfdfbfffe80ff0102),
        UINT64_C(0xfffffffefffffffe)
      }
    },
    {
      0x05,
      {
        UINT64_C(0xfdfbfffe7fff0002),
        UINT64_C(0xfffffffefffffffe)
      }
    },
    {
      0x01,
      {
        UINT64_C(0xfdfafffe7ffe0002),
        UINT64_C(0xfffffffefffffffe)
      }
    }
  };

  for (const Contract &contract : contracts)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, source);
    core.setGeneralRegister(2, target);
    runInstruction(
      &system,
      nestedMmiInstruction(
        0x08,
        contract.nestedFunction,
        1,
        2,
        3));
    REQUIRE(core.generalRegister(3).low == contract.expected.low);
    REQUIRE(core.generalRegister(3).high == contract.expected.high);
  }

  SECTION("Aliases register zero and repeated execution remain lane local")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, source);
    core.setGeneralRegister(2, target);
    runInstruction(
      &system,
      nestedMmiInstruction(0x08, 0x08, 1, 2, 1));
    REQUIRE(
      core.generalRegister(1).low ==
      UINT64_C(0x0101ff008001ff00));
    REQUIRE(
      core.generalRegister(1).high ==
      UINT64_C(0xffffff00ffffff00));

    core.setGeneralRegister(1, source);
    core.setGeneralRegister(2, target);
    runInstruction(
      &system,
      nestedMmiInstruction(0x08, 0x05, 1, 2, 2));
    REQUIRE(
      core.generalRegister(2).low ==
      UINT64_C(0xfdfbfffe7fff0002));
    REQUIRE(
      core.generalRegister(2).high ==
      UINT64_C(0xfffffffefffffffe));

    runInstruction(
      &system,
      nestedMmiInstruction(0x08, 0x00, 1, 2, 0));
    REQUIRE(core.generalRegister(0) == EERegister128{});

    core.setGeneralRegister(1, {UINT64_MAX, UINT64_MAX});
    core.setGeneralRegister(2, {1, 1});
    runInstruction(
      &system,
      nestedMmiInstruction(0x08, 0x08, 1, 2, 3));
    REQUIRE(
      core.generalRegister(3) ==
      EERegister128{UINT64_C(0xffffffffffffff00),
                    UINT64_C(0xffffffffffffff00)});

    core.setGeneralRegister(1, {});
    core.setGeneralRegister(2, {});
    runInstruction(
      &system,
      nestedMmiInstruction(0x08, 0x08, 1, 2, 3));
    REQUIRE(core.generalRegister(3) == EERegister128{});
  }
}

TEST_CASE("EE mixed packed halfword arithmetic execution")
{
  const EERegister128 source = {
    UINT64_C(0xfffe7fff80000001),
    UINT64_C(0xffffffff7fffffff)
  };
  const EERegister128 target = {
    UINT64_C(0x020380010001ffff),
    UINT64_C(0x0000000180000001)
  };

  SECTION("Low lanes subtract while high lanes add")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, source);
    core.setGeneralRegister(2, target);
    runInstruction(
      &system,
      nestedMmiInstruction(0x28, 0x04, 1, 2, 3));

    REQUIRE(
      core.generalRegister(3).low ==
      UINT64_C(0xfdfbfffe7fff0002));
    REQUIRE(
      core.generalRegister(3).high ==
      UINT64_C(0xffff0000ffff0000));
  }

  SECTION("Aliases capture both operands and register zero is immutable")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, source);
    core.setGeneralRegister(2, target);
    runInstruction(
      &system,
      nestedMmiInstruction(0x28, 0x04, 1, 2, 1));
    REQUIRE(
      core.generalRegister(1) ==
      EERegister128{UINT64_C(0xfdfbfffe7fff0002),
                    UINT64_C(0xffff0000ffff0000)});

    core.setGeneralRegister(1, source);
    core.setGeneralRegister(2, target);
    runInstruction(
      &system,
      nestedMmiInstruction(0x28, 0x04, 1, 2, 2));
    REQUIRE(
      core.generalRegister(2) ==
      EERegister128{UINT64_C(0xfdfbfffe7fff0002),
                    UINT64_C(0xffff0000ffff0000)});

    runInstruction(
      &system,
      nestedMmiInstruction(0x28, 0x04, 1, 2, 0));
    REQUIRE(core.generalRegister(0) == EERegister128{});
  }
}

TEST_CASE("EE signed saturating packed arithmetic execution")
{
  struct Contract
  {
    std::uint8_t nestedFunction;
    EERegister128 source;
    EERegister128 target;
    EERegister128 expected;
  };
  const Contract contracts[] = {
    {
      0x18,
      {
        UINT64_C(0xff01817e807f807f),
        UINT64_C(0xff01817e807f807f)
      },
      {
        UINT64_C(0x01ffff010000ff01),
        UINT64_C(0x01ffff010000ff01)
      },
      {
        UINT64_C(0x0000807f807f807f),
        UINT64_C(0x0000807f807f807f)
      }
    },
    {
      0x14,
      {
        UINT64_C(0x80007fff80007fff),
        UINT64_C(0xffff000180017ffe)
      },
      {
        UINT64_C(0x00000000ffff0001),
        UINT64_C(0x0001ffffffff0001)
      },
      {
        UINT64_C(0x80007fff80007fff),
        UINT64_C(0x0000000080007fff)
      }
    },
    {
      0x10,
      {
        UINT64_C(0x800000007fffffff),
        UINT64_C(0xffffffff00000001)
      },
      {
        UINT64_C(0xffffffff00000001),
        UINT64_C(0xfffffffe00000002)
      },
      {
        UINT64_C(0x800000007fffffff),
        UINT64_C(0xfffffffd00000003)
      }
    },
    {
      0x19,
      {
        UINT64_C(0xff01817e807f807f),
        UINT64_C(0xff01817e807f807f)
      },
      {
        UINT64_C(0x01ff01ff000001ff),
        UINT64_C(0x01ff01ff000001ff)
      },
      {
        UINT64_C(0xfe02807f807f807f),
        UINT64_C(0xfe02807f807f807f)
      }
    },
    {
      0x15,
      {
        UINT64_C(0x80007fff80007fff),
        UINT64_C(0xffff000180017ffe)
      },
      {
        UINT64_C(0x000000000001ffff),
        UINT64_C(0x0001ffff0001ffff)
      },
      {
        UINT64_C(0x80007fff80007fff),
        UINT64_C(0xfffe000280007fff)
      }
    },
    {
      0x11,
      {
        UINT64_C(0x800000007fffffff),
        UINT64_C(0xffffffff00000001)
      },
      {
        UINT64_C(0x00000001ffffffff),
        UINT64_C(0x00000002fffffffe)
      },
      {
        UINT64_C(0x800000007fffffff),
        UINT64_C(0xfffffffd00000003)
      }
    }
  };

  for (const Contract &contract : contracts)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, contract.source);
    core.setGeneralRegister(2, contract.target);
    runInstruction(
      &system,
      nestedMmiInstruction(
        0x08,
        contract.nestedFunction,
        1,
        2,
        3));
    REQUIRE(core.generalRegister(3) == contract.expected);
  }

  SECTION("Word extrema remain exact without overflow")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    const EERegister128 extrema = {
      UINT64_C(0x800000007fffffff),
      0
    };
    core.setGeneralRegister(1, extrema);
    runInstruction(
      &system,
      nestedMmiInstruction(0x08, 0x10, 1, 0, 3));
    REQUIRE(core.generalRegister(3) == extrema);
    runInstruction(
      &system,
      nestedMmiInstruction(0x08, 0x11, 1, 0, 3));
    REQUIRE(core.generalRegister(3) == extrema);
  }

  SECTION("Aliases capture operands and register zero discards results")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(
      1,
      {
        UINT64_C(0x80007fff80007fff),
        UINT64_C(0xffff000180017ffe)
      });
    core.setGeneralRegister(
      2,
      {
        UINT64_C(0x00000000ffff0001),
        UINT64_C(0x0001ffffffff0001)
      });
    runInstruction(
      &system,
      nestedMmiInstruction(0x08, 0x14, 1, 2, 1));
    REQUIRE(
      core.generalRegister(1) ==
      EERegister128{UINT64_C(0x80007fff80007fff),
                    UINT64_C(0x0000000080007fff)});

    core.setGeneralRegister(
      1,
      {
        UINT64_C(0xff01817e807f807f),
        UINT64_C(0xff01817e807f807f)
      });
    core.setGeneralRegister(
      2,
      {
        UINT64_C(0x01ff01ff000001ff),
        UINT64_C(0x01ff01ff000001ff)
      });
    runInstruction(
      &system,
      nestedMmiInstruction(0x08, 0x19, 1, 2, 2));
    REQUIRE(
      core.generalRegister(2) ==
      EERegister128{UINT64_C(0xfe02807f807f807f),
                    UINT64_C(0xfe02807f807f807f)});

    runInstruction(
      &system,
      nestedMmiInstruction(0x08, 0x10, 1, 2, 0));
    REQUIRE(core.generalRegister(0) == EERegister128{});
  }
}

TEST_CASE("EE unsigned saturating packed arithmetic execution")
{
  struct Contract
  {
    std::uint8_t nestedFunction;
    EERegister128 source;
    EERegister128 target;
    EERegister128 expected;
  };
  const Contract contracts[] = {
    {
      0x18,
      {
        UINT64_C(0x0100ffff0100ffff),
        UINT64_C(0x0100ffff0100ffff)
      },
      {
        UINT64_C(0x0200000102000001),
        UINT64_C(0x0200000102000001)
      },
      {
        UINT64_C(0x0300ffff0300ffff),
        UINT64_C(0x0300ffff0300ffff)
      }
    },
    {
      0x14,
      {
        UINT64_C(0x00010000ffffffff),
        UINT64_C(0x00010000ffffffff)
      },
      {
        UINT64_C(0x0002000000000001),
        UINT64_C(0x0002000000000001)
      },
      {
        UINT64_C(0x00030000ffffffff),
        UINT64_C(0x00030000ffffffff)
      }
    },
    {
      0x10,
      {
        UINT64_C(0xffffffffffffffff),
        UINT64_C(0x0000000100000000)
      },
      {
        UINT64_C(0x0000000000000001),
        UINT64_C(0x0000000200000000)
      },
      {
        UINT64_C(0xffffffffffffffff),
        UINT64_C(0x0000000300000000)
      }
    },
    {
      0x19,
      {
        UINT64_C(0x03ff000003ff0000),
        UINT64_C(0x03ff000003ff0000)
      },
      {
        UINT64_C(0x0200000102000001),
        UINT64_C(0x0200000102000001)
      },
      {
        UINT64_C(0x01ff000001ff0000),
        UINT64_C(0x01ff000001ff0000)
      }
    },
    {
      0x15,
      {
        UINT64_C(0x0003ffff00000000),
        UINT64_C(0x0003ffff00000000)
      },
      {
        UINT64_C(0x0002000000000001),
        UINT64_C(0x0002000000000001)
      },
      {
        UINT64_C(0x0001ffff00000000),
        UINT64_C(0x0001ffff00000000)
      }
    },
    {
      0x11,
      {
        UINT64_C(0x0000000000000000),
        UINT64_C(0x00000003ffffffff)
      },
      {
        UINT64_C(0x0000000000000001),
        UINT64_C(0x0000000200000000)
      },
      {
        UINT64_C(0x0000000000000000),
        UINT64_C(0x00000001ffffffff)
      }
    }
  };

  for (const Contract &contract : contracts)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, contract.source);
    core.setGeneralRegister(2, contract.target);
    runInstruction(
      &system,
      nestedMmiInstruction(
        0x28,
        contract.nestedFunction,
        1,
        2,
        3));
    REQUIRE(core.generalRegister(3) == contract.expected);
  }

  SECTION("Aliases capture operands and register zero discards results")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(
      1,
      {
        UINT64_C(0x00010000ffffffff),
        UINT64_C(0x00010000ffffffff)
      });
    core.setGeneralRegister(
      2,
      {
        UINT64_C(0x0002000000000001),
        UINT64_C(0x0002000000000001)
      });
    runInstruction(
      &system,
      nestedMmiInstruction(0x28, 0x14, 1, 2, 1));
    REQUIRE(
      core.generalRegister(1) ==
      EERegister128{UINT64_C(0x00030000ffffffff),
                    UINT64_C(0x00030000ffffffff)});

    core.setGeneralRegister(
      1,
      {
        UINT64_C(0x03ff000003ff0000),
        UINT64_C(0x03ff000003ff0000)
      });
    core.setGeneralRegister(
      2,
      {
        UINT64_C(0x0200000102000001),
        UINT64_C(0x0200000102000001)
      });
    runInstruction(
      &system,
      nestedMmiInstruction(0x28, 0x19, 1, 2, 2));
    REQUIRE(
      core.generalRegister(2) ==
      EERegister128{UINT64_C(0x01ff000001ff0000),
                    UINT64_C(0x01ff000001ff0000)});

    runInstruction(
      &system,
      nestedMmiInstruction(0x28, 0x10, 1, 2, 0));
    REQUIRE(core.generalRegister(0) == EERegister128{});
  }

  SECTION("Repeated execution replaces every saturated lane")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, {UINT64_MAX, UINT64_MAX});
    core.setGeneralRegister(2, {UINT64_MAX, UINT64_MAX});
    runInstruction(
      &system,
      nestedMmiInstruction(0x28, 0x18, 1, 2, 3));
    REQUIRE(
      core.generalRegister(3) ==
      EERegister128{UINT64_MAX, UINT64_MAX});

    core.setGeneralRegister(1, {});
    core.setGeneralRegister(2, {});
    runInstruction(
      &system,
      nestedMmiInstruction(0x28, 0x18, 1, 2, 3));
    REQUIRE(core.generalRegister(3) == EERegister128{});
  }
}

TEST_CASE("EE lower packed interleave execution")
{
  struct Contract
  {
    std::uint8_t nestedFunction;
    EERegister128 expected;
  };
  const EERegister128 source = {
    UINT64_C(0x1716151413121110),
    UINT64_C(0xf7f6f5f4f3f2f1f0)
  };
  const EERegister128 target = {
    UINT64_C(0x0706050403020100),
    UINT64_C(0xe7e6e5e4e3e2e1e0)
  };
  const Contract contracts[] = {
    {
      0x1a,
      {
        UINT64_C(0x1303120211011000),
        UINT64_C(0x1707160615051404)
      }
    },
    {
      0x16,
      {
        UINT64_C(0x1312030211100100),
        UINT64_C(0x1716070615140504)
      }
    },
    {
      0x12,
      {
        UINT64_C(0x1312111003020100),
        UINT64_C(0x1716151407060504)
      }
    }
  };

  for (const Contract &contract : contracts)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, source);
    core.setGeneralRegister(2, target);
    runInstruction(
      &system,
      nestedMmiInstruction(
        0x08,
        contract.nestedFunction,
        1,
        2,
        3));
    REQUIRE(core.generalRegister(3) == contract.expected);
  }

  SECTION("Aliases capture low operands and register zero is immutable")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, source);
    core.setGeneralRegister(2, target);
    runInstruction(
      &system,
      nestedMmiInstruction(0x08, 0x1a, 1, 2, 1));
    REQUIRE(
      core.generalRegister(1) ==
      EERegister128{UINT64_C(0x1303120211011000),
                    UINT64_C(0x1707160615051404)});

    core.setGeneralRegister(1, source);
    core.setGeneralRegister(2, target);
    runInstruction(
      &system,
      nestedMmiInstruction(0x08, 0x16, 1, 2, 2));
    REQUIRE(
      core.generalRegister(2) ==
      EERegister128{UINT64_C(0x1312030211100100),
                    UINT64_C(0x1716070615140504)});

    runInstruction(
      &system,
      nestedMmiInstruction(0x08, 0x12, 1, 2, 0));
    REQUIRE(core.generalRegister(0) == EERegister128{});
  }

  SECTION("Repeated execution replaces every destination lane")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, source);
    core.setGeneralRegister(2, target);
    runInstruction(
      &system,
      nestedMmiInstruction(0x08, 0x1a, 1, 2, 3));

    core.setGeneralRegister(1, {});
    core.setGeneralRegister(2, {});
    runInstruction(
      &system,
      nestedMmiInstruction(0x08, 0x1a, 1, 2, 3));
    REQUIRE(core.generalRegister(3) == EERegister128{});
  }
}

TEST_CASE("EE signed packed greater-than execution")
{
  struct Contract
  {
    std::uint8_t nestedFunction;
    EERegister128 source;
    EERegister128 target;
    EERegister128 expected;
  };
  const Contract contracts[] = {
    {
      0x0a,
      {
        UINT64_C(0x05fe0200ff01807f),
        UINT64_C(0x807f00ff0102fe80)
      },
      {
        UINT64_C(0x05fd0300fe01817e),
        UINT64_C(0xff7f00000101ff7f)
      },
      {
        UINT64_C(0x00ff0000ff0000ff),
        UINT64_C(0x0000000000ff0000)
      }
    },
    {
      0x06,
      {
        UINT64_C(0xffff000080007fff),
        UINT64_C(0x0001fffe7fff8000)
      },
      {
        UINT64_C(0xfffe000080017ffe),
        UINT64_C(0x0000ffff7fff8000)
      },
      {
        UINT64_C(0xffff00000000ffff),
        UINT64_C(0xffff000000000000)
      }
    },
    {
      0x02,
      {
        UINT64_C(0x800000007fffffff),
        UINT64_C(0xffffffff00000000)
      },
      {
        UINT64_C(0x800000017ffffffe),
        UINT64_C(0xfffffffe00000000)
      },
      {
        UINT64_C(0x00000000ffffffff),
        UINT64_C(0xffffffff00000000)
      }
    }
  };

  for (const Contract &contract : contracts)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, contract.source);
    core.setGeneralRegister(2, contract.target);
    runInstruction(
      &system,
      nestedMmiInstruction(
        0x08,
        contract.nestedFunction,
        1,
        2,
        3));
    REQUIRE(core.generalRegister(3).low == contract.expected.low);
    REQUIRE(core.generalRegister(3).high == contract.expected.high);
  }

  SECTION("Equal lanes are false and aliases preserve source values")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    const EERegister128 value = {
      UINT64_C(0x800000007fffffff),
      UINT64_C(0xffffffff00000000)
    };
    core.setGeneralRegister(1, value);
    core.setGeneralRegister(2, value);
    runInstruction(
      &system,
      nestedMmiInstruction(0x08, 0x02, 1, 2, 1));
    REQUIRE(core.generalRegister(1).low == 0);
    REQUIRE(core.generalRegister(1).high == 0);

    core.setGeneralRegister(1, value);
    core.setGeneralRegister(2, value);
    runInstruction(
      &system,
      nestedMmiInstruction(0x08, 0x06, 1, 2, 2));
    REQUIRE(core.generalRegister(2).low == 0);
    REQUIRE(core.generalRegister(2).high == 0);
  }

  SECTION("Register zero is immutable and repeated execution replaces lanes")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, {UINT64_MAX, UINT64_MAX});
    core.setGeneralRegister(2, {0, 0});
    runInstruction(
      &system,
      nestedMmiInstruction(0x08, 0x0a, 1, 2, 0));
    REQUIRE(core.generalRegister(0).low == 0);
    REQUIRE(core.generalRegister(0).high == 0);

    runInstruction(
      &system,
      nestedMmiInstruction(0x08, 0x0a, 2, 1, 3));
    REQUIRE(core.generalRegister(3).low == UINT64_MAX);
    REQUIRE(core.generalRegister(3).high == UINT64_MAX);
    runInstruction(
      &system,
      nestedMmiInstruction(0x08, 0x0a, 1, 2, 3));
    REQUIRE(core.generalRegister(3).low == 0);
    REQUIRE(core.generalRegister(3).high == 0);
  }
}

TEST_CASE("EE signed packed min max execution")
{
  struct Contract
  {
    std::uint8_t function;
    std::uint8_t nestedFunction;
    EERegister128 expected;
  };
  const EERegister128 source = {
    UINT64_C(0xffff000080007fff),
    UINT64_C(0x0001fffe7fff8000)
  };
  const EERegister128 target = {
    UINT64_C(0xfffe000080017ffe),
    UINT64_C(0x0000ffff7fff8001)
  };
  const Contract halfwordContracts[] = {
    {
      0x08,
      0x07,
      {
        UINT64_C(0xffff000080017fff),
        UINT64_C(0x0001ffff7fff8001)
      }
    },
    {
      0x28,
      0x07,
      {
        UINT64_C(0xfffe000080007ffe),
        UINT64_C(0x0000fffe7fff8000)
      }
    }
  };

  for (const Contract &contract : halfwordContracts)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, source);
    core.setGeneralRegister(2, target);
    runInstruction(
      &system,
      nestedMmiInstruction(
        contract.function,
        contract.nestedFunction,
        1,
        2,
        3));
    REQUIRE(core.generalRegister(3).low == contract.expected.low);
    REQUIRE(core.generalRegister(3).high == contract.expected.high);
  }

  const EERegister128 wordSource = {
    UINT64_C(0x800000007fffffff),
    UINT64_C(0xffffffff00000000)
  };
  const EERegister128 wordTarget = {
    UINT64_C(0x800000017ffffffe),
    UINT64_C(0xfffffffe00000000)
  };
  const Contract wordContracts[] = {
    {
      0x08,
      0x03,
      {
        UINT64_C(0x800000017fffffff),
        UINT64_C(0xffffffff00000000)
      }
    },
    {
      0x28,
      0x03,
      {
        UINT64_C(0x800000007ffffffe),
        UINT64_C(0xfffffffe00000000)
      }
    }
  };

  for (const Contract &contract : wordContracts)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, wordSource);
    core.setGeneralRegister(2, wordTarget);
    runInstruction(
      &system,
      nestedMmiInstruction(
        contract.function,
        contract.nestedFunction,
        1,
        2,
        3));
    REQUIRE(core.generalRegister(3).low == contract.expected.low);
    REQUIRE(core.generalRegister(3).high == contract.expected.high);
  }

  SECTION("Equal lanes and source aliases preserve lane values")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, wordSource);
    core.setGeneralRegister(2, wordSource);
    runInstruction(
      &system,
      nestedMmiInstruction(0x08, 0x03, 1, 2, 1));
    REQUIRE(core.generalRegister(1).low == wordSource.low);
    REQUIRE(core.generalRegister(1).high == wordSource.high);

    core.setGeneralRegister(1, source);
    core.setGeneralRegister(2, target);
    runInstruction(
      &system,
      nestedMmiInstruction(0x28, 0x07, 1, 2, 2));
    REQUIRE(
      core.generalRegister(2).low ==
      UINT64_C(0xfffe000080007ffe));
    REQUIRE(
      core.generalRegister(2).high ==
      UINT64_C(0x0000fffe7fff8000));
  }

  SECTION("Register zero is immutable and repeated execution replaces lanes")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, wordSource);
    core.setGeneralRegister(2, wordTarget);
    runInstruction(
      &system,
      nestedMmiInstruction(0x08, 0x03, 1, 2, 0));
    REQUIRE(core.generalRegister(0).low == 0);
    REQUIRE(core.generalRegister(0).high == 0);

    runInstruction(
      &system,
      nestedMmiInstruction(0x08, 0x03, 1, 2, 3));
    REQUIRE(
      core.generalRegister(3).low ==
      UINT64_C(0x800000017fffffff));
    runInstruction(
      &system,
      nestedMmiInstruction(0x28, 0x03, 1, 2, 3));
    REQUIRE(
      core.generalRegister(3).low ==
      UINT64_C(0x800000007ffffffe));
  }
}

TEST_CASE("EE packed absolute execution")
{
  SECTION("Halfword lanes include signed extrema")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(
      2,
      {
        UINT64_C(0x80007fff0001ffff),
        UINT64_C(0x0000fffe80010002)
      });
    runInstruction(
      &system,
      nestedMmiInstruction(0x28, 0x05, 0, 2, 3));
    REQUIRE(
      core.generalRegister(3).low ==
      UINT64_C(0x7fff7fff00010001));
    REQUIRE(
      core.generalRegister(3).high ==
      UINT64_C(0x000000027fff0002));
  }

  SECTION("Word lanes include signed extrema")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(
      2,
      {
        UINT64_C(0x800000007fffffff),
        UINT64_C(0xffffffff00000000)
      });
    runInstruction(
      &system,
      nestedMmiInstruction(0x28, 0x01, 0, 2, 3));
    REQUIRE(
      core.generalRegister(3).low ==
      UINT64_C(0x7fffffff7fffffff));
    REQUIRE(
      core.generalRegister(3).high ==
      UINT64_C(0x0000000100000000));
  }

  SECTION("Target aliases and register zero remain valid")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(
      2,
      {
        UINT64_C(0x800000007fffffff),
        UINT64_C(0xffffffff00000000)
      });
    runInstruction(
      &system,
      nestedMmiInstruction(0x28, 0x01, 0, 2, 2));
    REQUIRE(
      core.generalRegister(2).low ==
      UINT64_C(0x7fffffff7fffffff));
    REQUIRE(
      core.generalRegister(2).high ==
      UINT64_C(0x0000000100000000));

    runInstruction(
      &system,
      nestedMmiInstruction(0x28, 0x01, 0, 2, 0));
    REQUIRE(core.generalRegister(0).low == 0);
    REQUIRE(core.generalRegister(0).high == 0);
  }

  SECTION("Repeated execution replaces all lanes")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(2, {UINT64_MAX, UINT64_MAX});
    runInstruction(
      &system,
      nestedMmiInstruction(0x28, 0x05, 0, 2, 3));
    REQUIRE(
      core.generalRegister(3).low ==
      UINT64_C(0x0001000100010001));
    REQUIRE(
      core.generalRegister(3).high ==
      UINT64_C(0x0001000100010001));

    core.setGeneralRegister(2, {0, 0});
    runInstruction(
      &system,
      nestedMmiInstruction(0x28, 0x05, 0, 2, 3));
    REQUIRE(core.generalRegister(3).low == 0);
    REQUIRE(core.generalRegister(3).high == 0);
  }
}

TEST_CASE("EE packed leading sign count execution")
{
  SECTION("Manual example counts both low words")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(
      1,
      {
        UINT64_C(0x000fff0fff0ff00f),
        UINT64_C(0x123456789abcdef0)
      });
    core.setGeneralRegister(
      3,
      {
        UINT64_MAX,
        UINT64_C(0xfedcba9876543210)
      });
    runInstruction(
      &system,
      UINT32_C(0x70000000) |
        registerInstruction(0x04, 1, 0, 3, 0));
    REQUIRE(
      core.generalRegister(3).low ==
      UINT64_C(0x0000000b00000007));
    REQUIRE(
      core.generalRegister(3).high ==
      UINT64_C(0xfedcba9876543210));
  }

  SECTION("Zero one and sign boundaries produce documented counts")
  {
    struct Contract
    {
      std::uint64_t source;
      std::uint64_t expected;
    };
    const Contract contracts[] = {
      {
        UINT64_C(0xffffffff00000000),
        UINT64_C(0x0000001f0000001f)
      },
      {
        UINT64_C(0x800000007fffffff),
        0
      },
      {
        UINT64_C(0xc00000003fffffff),
        UINT64_C(0x0000000100000001)
      }
    };
    for (const Contract &contract : contracts)
    {
      NekoSystem system;
      EECore &core = system.eeCore();
      core.setGeneralRegister(1, {contract.source, UINT64_MAX});
      runInstruction(
        &system,
        UINT32_C(0x70000000) |
          registerInstruction(0x04, 1, 0, 3, 0));
      REQUIRE(core.generalRegister(3).low == contract.expected);
      REQUIRE(core.generalRegister(3).high == 0);
    }
  }

  SECTION("Source aliases and register zero remain valid")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(
      1,
      {
        UINT64_C(0x000fff0fff0ff00f),
        UINT64_C(0x123456789abcdef0)
      });
    runInstruction(
      &system,
      UINT32_C(0x70000000) |
        registerInstruction(0x04, 1, 0, 1, 0));
    REQUIRE(
      core.generalRegister(1).low ==
      UINT64_C(0x0000000b00000007));
    REQUIRE(
      core.generalRegister(1).high ==
      UINT64_C(0x123456789abcdef0));

    runInstruction(
      &system,
      UINT32_C(0x70000000) |
        registerInstruction(0x04, 1, 0, 0, 0));
    REQUIRE(core.generalRegister(0).low == 0);
    REQUIRE(core.generalRegister(0).high == 0);
  }

  SECTION("Repeated execution replaces the low doubleword")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, {0, 0});
    runInstruction(
      &system,
      UINT32_C(0x70000000) |
        registerInstruction(0x04, 1, 0, 3, 0));
    REQUIRE(
      core.generalRegister(3).low ==
      UINT64_C(0x0000001f0000001f));

    core.setGeneralRegister(
      1,
      {UINT64_C(0x800000007fffffff), 0});
    runInstruction(
      &system,
      UINT32_C(0x70000000) |
        registerInstruction(0x04, 1, 0, 3, 0));
    REQUIRE(core.generalRegister(3).low == 0);
  }
}

TEST_CASE("EE integer shift execution")
{
  NekoSystem system;
  EECore &core = system.eeCore();

  SECTION("Word shifts sign extend their 32-bit result")
  {
    setRegister(&core, 1, UINT64_C(0xffffffff80000001));

    runInstruction(
      &system,
      registerInstruction(0x00, 0, 1, 2, 1));
    REQUIRE(core.generalRegister(2).low == 2);

    runInstruction(
      &system,
      registerInstruction(0x02, 0, 1, 3, 1));
    REQUIRE(core.generalRegister(3).low == 0x40000000);

    runInstruction(
      &system,
      registerInstruction(0x03, 0, 1, 4, 1));
    REQUIRE(
      core.generalRegister(4).low ==
      UINT64_C(0xffffffffc0000000));
  }

  SECTION("Variable shifts mask their shift counts")
  {
    setRegister(&core, 1, 33);
    setRegister(&core, 2, 1);

    runInstruction(
      &system,
      registerInstruction(0x04, 1, 2, 3));
    REQUIRE(core.generalRegister(3).low == 2);

    setRegister(&core, 1, 65);
    runInstruction(
      &system,
      registerInstruction(0x14, 1, 2, 4));
    REQUIRE(core.generalRegister(4).low == 2);
  }

  SECTION("Doubleword shifts preserve the upper doubleword")
  {
    setRegister(
      &core,
      1,
      UINT64_C(0x8000000000000001));
    setRegister(&core, 2, 0);

    runInstruction(
      &system,
      registerInstruction(0x3a, 0, 1, 2, 1));
    REQUIRE(
      core.generalRegister(2).low ==
      UINT64_C(0x4000000000000000));
    REQUIRE(
      core.generalRegister(2).high ==
      UINT64_C(0xfeedfacecafebeef));

    runInstruction(
      &system,
      registerInstruction(0x3b, 0, 1, 2, 1));
    REQUIRE(
      core.generalRegister(2).low ==
      UINT64_C(0xc000000000000000));

    runInstruction(
      &system,
      registerInstruction(0x3c, 0, 1, 2, 1));
    REQUIRE(
      core.generalRegister(2).low ==
      UINT64_C(0x0000000200000000));
  }
}

TEST_CASE("EE trapping and undefined integer operations")
{
  NekoSystem system;
  EECore &core = system.eeCore();

  SECTION("Word overflow preserves the destination and enters COP0")
  {
    setRegister(&core, 1, 0x7fffffff);
    setRegister(&core, 2, 1);
    setRegister(&core, 3, 0x1234, 0x5678);

    runInstruction(
      &system,
      registerInstruction(0x20, 1, 2, 3));

    REQUIRE(
      core.generalRegister(3) ==
      EERegister128{0x1234, 0x5678});
    REQUIRE(core.clockActive());
    REQUIRE(core.stopReason() == EEStopReason::None);
    REQUIRE(
      core.pendingException() ==
      EEException::ArithmeticOverflow);
    REQUIRE(core.exceptionAddress() == 0);
    REQUIRE(
      core.programCounter() ==
      EEExceptionVector::BOOTSTRAP_GENERAL);
    REQUIRE_FALSE(core.hasLastInstruction());
  }

  SECTION("Doubleword subtraction overflow is detected")
  {
    setRegister(
      &core,
      1,
      UINT64_C(0x8000000000000000));
    setRegister(&core, 2, 1);
    setRegister(&core, 3, 0x1234);

    runInstruction(
      &system,
      registerInstruction(0x2e, 1, 2, 3));

    REQUIRE(core.generalRegister(3).low == 0x1234);
    REQUIRE(
      core.pendingException() ==
      EEException::ArithmeticOverflow);
  }

  SECTION("Word operations reject non-sign-extended operands")
  {
    setRegister(&core, 1, UINT64_C(0x0000000080000000));
    setRegister(&core, 2, 0);
    setRegister(&core, 3, 0x1234);

    runInstruction(
      &system,
      registerInstruction(0x21, 1, 2, 3));

    REQUIRE(core.generalRegister(3).low == 0x1234);
    REQUIRE_FALSE(core.exceptionPending());
    REQUIRE(
      core.stopReason() ==
      EEStopReason::UndefinedOperation);
    REQUIRE(core.programCounter() == 0);
  }
}

TEST_CASE("Every decoded base integer operation executes")
{
  const std::uint32_t instructions[] = {
    0,
    registerInstruction(0x00, 0, 2, 3, 1),
    registerInstruction(0x02, 0, 2, 3, 1),
    registerInstruction(0x03, 0, 2, 3, 1),
    registerInstruction(0x04, 1, 2, 3),
    registerInstruction(0x06, 1, 2, 3),
    registerInstruction(0x07, 1, 2, 3),
    registerInstruction(0x14, 1, 2, 3),
    registerInstruction(0x16, 1, 2, 3),
    registerInstruction(0x17, 1, 2, 3),
    registerInstruction(0x20, 1, 2, 3),
    registerInstruction(0x21, 1, 2, 3),
    registerInstruction(0x22, 1, 2, 3),
    registerInstruction(0x23, 1, 2, 3),
    registerInstruction(0x24, 1, 2, 3),
    registerInstruction(0x25, 1, 2, 3),
    registerInstruction(0x26, 1, 2, 3),
    registerInstruction(0x27, 1, 2, 3),
    registerInstruction(0x2a, 1, 2, 3),
    registerInstruction(0x2b, 1, 2, 3),
    registerInstruction(0x2c, 1, 2, 3),
    registerInstruction(0x2d, 1, 2, 3),
    registerInstruction(0x2e, 1, 2, 3),
    registerInstruction(0x2f, 1, 2, 3),
    registerInstruction(0x38, 0, 2, 3, 1),
    registerInstruction(0x3a, 0, 2, 3, 1),
    registerInstruction(0x3b, 0, 2, 3, 1),
    registerInstruction(0x3c, 0, 2, 3, 1),
    registerInstruction(0x3e, 0, 2, 3, 1),
    registerInstruction(0x3f, 0, 2, 3, 1),
    immediateInstruction(0x08, 1, 3, 1),
    immediateInstruction(0x09, 1, 3, 1),
    immediateInstruction(0x0a, 1, 3, 1),
    immediateInstruction(0x0b, 1, 3, 1),
    immediateInstruction(0x0c, 1, 3, 1),
    immediateInstruction(0x0d, 1, 3, 1),
    immediateInstruction(0x0e, 1, 3, 1),
    immediateInstruction(0x0f, 0, 3, 1),
    immediateInstruction(0x18, 1, 3, 1),
    immediateInstruction(0x19, 1, 3, 1)
  };

  for (std::uint32_t instruction : instructions)
  {
    NekoSystem system;
    setRegister(&system.eeCore(), 1, 8);
    setRegister(&system.eeCore(), 2, 2);

    runInstruction(&system, instruction);

    REQUIRE(system.eeCore().clockActive());
    REQUIRE(system.eeCore().programCounter() == 4);
    REQUIRE(system.eeCore().hasLastInstruction());
  }
}

TEST_CASE("A failing EE instruction preserves the last retired instruction")
{
  NekoSystem system;
  setRegister(&system.eeCore(), 1, 0x7fffffff);
  setRegister(&system.eeCore(), 2, 1);
  system.eeBus().write32(0, 0);
  system.eeBus().write32(
    4,
    registerInstruction(0x20, 1, 2, 3));
  system.eeCore().startExecution(0);

  system.runMasterCycles(2);

  REQUIRE(system.eeCore().hasLastInstruction());
  REQUIRE(system.eeCore().lastInstructionAddress() == 0);
  REQUIRE(
    system.eeCore().lastInstruction().operation ==
    EEOperation::Nop);
  REQUIRE(system.eeCore().stopReason() == EEStopReason::None);
  REQUIRE(
    system.eeCore().programCounter() ==
    EEExceptionVector::BOOTSTRAP_GENERAL);
  REQUIRE(
    system.eeCore().rejectedInstruction() ==
    registerInstruction(0x20, 1, 2, 3));
}
