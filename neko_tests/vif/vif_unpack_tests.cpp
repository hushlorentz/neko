#include <array>
#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "vif.hpp"
#include "vpu.hpp"

namespace
{
  constexpr std::uint32_t VU_QUADWORD_BYTES = 16;
  constexpr std::uint8_t UNPACK_MASK_INPUT = 0;
  constexpr std::uint8_t UNPACK_MASK_ROW = 1;
  constexpr std::uint8_t UNPACK_MASK_COLUMN = 2;
  constexpr std::uint8_t UNPACK_MASK_PROTECT = 3;

  std::uint32_t vifCode(
    std::uint8_t command,
    std::uint8_t count = 0,
    std::uint16_t immediate = 0)
  {
    return
      (static_cast<std::uint32_t>(command) << 24) |
      (static_cast<std::uint32_t>(count) << 16) |
      immediate;
  }

  void setCycle(VIF *vif, std::uint8_t write, std::uint8_t cycle)
  {
    vif->ingestWord(vifCode(
      VIFCommandEncoding::STCYCL,
      0,
      (static_cast<std::uint16_t>(write) << 8) | cycle));
  }

  void setVectorRegister(
    VIF *vif,
    VIFCommandKind kind,
    const std::array<std::uint32_t, 4> &values)
  {
    const std::uint8_t command =
      kind == VIFCommandKind::STROW
        ? VIFCommandEncoding::STROW
        : VIFCommandEncoding::STCOL;
    vif->ingestWord(vifCode(command));
    for (const std::uint32_t value : values)
    {
      vif->ingestWord(value);
    }
  }

  std::uint32_t repeatedMask(
    std::uint8_t cycle,
    std::uint8_t value)
  {
    std::uint32_t mask = 0;
    for (std::uint8_t lane = 0; lane < 4; ++lane)
    {
      mask |=
        static_cast<std::uint32_t>(value) <<
        ((cycle * 4 + lane) * 2);
    }
    return mask;
  }

  struct FormatContract
  {
    std::uint8_t encoding;
    std::vector<std::uint32_t> payload;
    std::array<std::uint32_t, 4> expected;
  };
}

TEST_CASE("VIF Payload Register Tests")
{
  VIF vif(VIFType::VIF1);

  vif.ingestWord(vifCode(VIFCommandEncoding::STMASK));
  vif.ingestWord(0xe4e4e4e4);
  setVectorRegister(
    &vif,
    VIFCommandKind::STROW,
    {{0x10, 0x20, 0x30, 0x40}});
  setVectorRegister(
    &vif,
    VIFCommandKind::STCOL,
    {{0x50, 0x60, 0x70, 0x80}});

  REQUIRE(vif.mask() == 0xe4e4e4e4);
  REQUIRE(vif.row(0) == 0x10);
  REQUIRE(vif.row(3) == 0x40);
  REQUIRE(vif.column(0) == 0x50);
  REQUIRE(vif.column(3) == 0x80);
  REQUIRE_THROWS_WITH(
    vif.row(4),
    "VIF row register index is outside range.");
  REQUIRE_THROWS_WITH(
    vif.column(4),
    "VIF column register index is outside range.");
}

TEST_CASE("VIF UNPACK Format Tests")
{
  const std::array<FormatContract, 13> formats = {{
    {
      VIFUnpackEncoding::S_32,
      {0x00000001},
      {{1, 1, 1, 1}}
    },
    {
      VIFUnpackEncoding::S_16,
      {0x00000002},
      {{2, 2, 2, 2}}
    },
    {
      VIFUnpackEncoding::S_8,
      {0x00000003},
      {{3, 3, 3, 3}}
    },
    {
      VIFUnpackEncoding::V2_32,
      {1, 2},
      {{1, 2, 0, 0}}
    },
    {
      VIFUnpackEncoding::V2_16,
      {0x00020001},
      {{1, 2, 0, 0}}
    },
    {
      VIFUnpackEncoding::V2_8,
      {0x00000201},
      {{1, 2, 0, 0}}
    },
    {
      VIFUnpackEncoding::V3_32,
      {1, 2, 3},
      {{1, 2, 3, 0}}
    },
    {
      VIFUnpackEncoding::V3_16,
      {0x00020001, 0x00000003},
      {{1, 2, 3, 0}}
    },
    {
      VIFUnpackEncoding::V3_8,
      {0x00030201},
      {{1, 2, 3, 0}}
    },
    {
      VIFUnpackEncoding::V4_32,
      {1, 2, 3, 4},
      {{1, 2, 3, 4}}
    },
    {
      VIFUnpackEncoding::V4_16,
      {0x00020001, 0x00040003},
      {{1, 2, 3, 4}}
    },
    {
      VIFUnpackEncoding::V4_8,
      {0x04030201},
      {{1, 2, 3, 4}}
    },
    {
      VIFUnpackEncoding::V4_5,
      {1 | (2 << 5) | (3 << 10) | (1 << 15)},
      {{8, 16, 24, 128}}
    }
  }};

  VPU vpu(VPUType::VU1);
  VIF vif(VIFType::VIF1);
  vif.attachVPU(&vpu);
  setCycle(&vif, 1, 1);

  for (std::size_t index = 0; index < formats.size(); ++index)
  {
    const FormatContract &format = formats[index];
    INFO("UNPACK format index " << index);
    vif.ingestWord(vifCode(
      VIFCommandEncoding::UNPACK | format.encoding,
      1,
      index));
    for (const std::uint32_t word : format.payload)
    {
      vif.ingestWord(word);
    }

    REQUIRE(vpu.readDataQuadword(index) == format.expected);
  }
}

TEST_CASE("VIF UNPACK Transfer Tests")
{
  SECTION("Signed and unsigned narrow elements are extended correctly")
  {
    VPU vpu(VPUType::VU0);
    VIF vif(VIFType::VIF0);
    vif.attachVPU(&vpu);
    setCycle(&vif, 1, 1);

    vif.ingestWord(vifCode(
      VIFCommandEncoding::UNPACK | VIFUnpackEncoding::S_16,
      1,
      0));
    vif.ingestWord(0x00008001);
    vif.ingestWord(vifCode(
      VIFCommandEncoding::UNPACK | VIFUnpackEncoding::S_8,
      1,
      2));
    vif.ingestWord(0x00000080);
    vif.ingestWord(vifCode(
      VIFCommandEncoding::UNPACK | VIFUnpackEncoding::S_16,
      1,
      VIFImmediateEncoding::UNPACKUnsigned | 1));
    vif.ingestWord(0x00008001);

    REQUIRE(
      vpu.readDataQuadword(0) ==
      std::array<std::uint32_t, 4>{{
        0xffff8001,
        0xffff8001,
        0xffff8001,
        0xffff8001
      }});
    REQUIRE(
      vpu.readDataQuadword(1) ==
      std::array<std::uint32_t, 4>{{
        0x00008001,
        0x00008001,
        0x00008001,
        0x00008001
      }});
    REQUIRE(
      vpu.readDataQuadword(2) ==
      std::array<std::uint32_t, 4>{{
        0xffffff80,
        0xffffff80,
        0xffffff80,
        0xffffff80
      }});
  }

  SECTION("Fragmented packed elements continue across input words")
  {
    VPU vpu(VPUType::VU1);
    VIF vif(VIFType::VIF1);
    vif.attachVPU(&vpu);
    setCycle(&vif, 1, 1);
    vif.ingestWord(vifCode(
      VIFCommandEncoding::UNPACK | VIFUnpackEncoding::V3_16,
      3,
      4));

    for (const std::uint32_t word : {
      0x00020001,
      0x00040003,
      0x00060005,
      0x00080007,
      0x00000009})
    {
      vif.ingestWord(word);
    }

    REQUIRE(
      vpu.readDataQuadword(4) ==
      std::array<std::uint32_t, 4>{{1, 2, 3, 0}});
    REQUIRE(
      vpu.readDataQuadword(5) ==
      std::array<std::uint32_t, 4>{{4, 5, 6, 0}});
    REQUIRE(
      vpu.readDataQuadword(6) ==
      std::array<std::uint32_t, 4>{{7, 8, 9, 0}});
  }

  SECTION("Skipping mode leaves CYCLE gaps in VU memory")
  {
    VPU vpu(VPUType::VU1);
    VIF vif(VIFType::VIF1);
    vif.attachVPU(&vpu);
    setCycle(&vif, 2, 4);
    vif.ingestWord(vifCode(
      VIFCommandEncoding::UNPACK | VIFUnpackEncoding::S_32,
      5,
      0));

    for (std::uint32_t value = 1; value <= 5; ++value)
    {
      vif.ingestWord(value);
    }

    for (const std::uint32_t address : {0, 1, 4, 5, 8})
    {
      const std::uint32_t expected =
        address == 0 ? 1 :
        address == 1 ? 2 :
        address == 4 ? 3 :
        address == 5 ? 4 : 5;
      REQUIRE(
        vpu.readDataQuadword(address) ==
        std::array<std::uint32_t, 4>{{
          expected, expected, expected, expected
        }});
    }
    REQUIRE(
      vpu.readDataQuadword(2) ==
      std::array<std::uint32_t, 4>{{0, 0, 0, 0}});
  }

  SECTION("Filling mode supplies row and cycle-selected column data")
  {
    VPU vpu(VPUType::VU1);
    VIF vif(VIFType::VIF1);
    vif.attachVPU(&vpu);
    setCycle(&vif, 4, 2);
    setVectorRegister(
      &vif,
      VIFCommandKind::STROW,
      {{10, 20, 30, 40}});
    setVectorRegister(
      &vif,
      VIFCommandKind::STCOL,
      {{50, 60, 70, 80}});
    const std::uint32_t mask =
      repeatedMask(2, UNPACK_MASK_ROW) |
      repeatedMask(3, UNPACK_MASK_COLUMN);
    vif.ingestWord(vifCode(VIFCommandEncoding::STMASK));
    vif.ingestWord(mask);
    vif.ingestWord(vifCode(
      VIFCommandEncoding::UNPACK |
        VIFCommandEncoding::UNPACKMask |
        VIFUnpackEncoding::V4_32,
      4,
      0));

    for (std::uint32_t value = 1; value <= 8; ++value)
    {
      vif.ingestWord(value);
    }

    REQUIRE(
      vpu.readDataQuadword(0) ==
      std::array<std::uint32_t, 4>{{1, 2, 3, 4}});
    REQUIRE(
      vpu.readDataQuadword(1) ==
      std::array<std::uint32_t, 4>{{5, 6, 7, 8}});
    REQUIRE(
      vpu.readDataQuadword(2) ==
      std::array<std::uint32_t, 4>{{10, 20, 30, 40}});
    REQUIRE(
      vpu.readDataQuadword(3) ==
      std::array<std::uint32_t, 4>{{80, 80, 80, 80}});
  }

  SECTION("Mask lanes select input, row, column, and protection")
  {
    VPU vpu(VPUType::VU1);
    VIF vif(VIFType::VIF1);
    vif.attachVPU(&vpu);
    setCycle(&vif, 1, 1);
    setVectorRegister(
      &vif,
      VIFCommandKind::STROW,
      {{10, 20, 30, 40}});
    setVectorRegister(
      &vif,
      VIFCommandKind::STCOL,
      {{50, 60, 70, 80}});
    vpu.writeDataQuadword(
      0,
      {{100, 100, 100, 100}});
    const std::uint32_t mask =
      UNPACK_MASK_INPUT |
      (UNPACK_MASK_ROW << 2) |
      (UNPACK_MASK_COLUMN << 4) |
      (UNPACK_MASK_PROTECT << 6);
    vif.ingestWord(vifCode(VIFCommandEncoding::STMASK));
    vif.ingestWord(mask);
    vif.ingestWord(vifCode(
      VIFCommandEncoding::UNPACK |
        VIFCommandEncoding::UNPACKMask |
        VIFUnpackEncoding::V4_32,
      1,
      0));
    for (std::uint32_t value = 1; value <= 4; ++value)
    {
      vif.ingestWord(value);
    }

    REQUIRE(
      vpu.readDataQuadword(0) ==
      std::array<std::uint32_t, 4>{{1, 20, 50, 100}});

    vif.ingestWord(vifCode(
      VIFCommandEncoding::UNPACK | VIFUnpackEncoding::V4_32,
      1,
      1));
    for (std::uint32_t value = 5; value <= 8; ++value)
    {
      vif.ingestWord(value);
    }
    REQUIRE(
      vpu.readDataQuadword(1) ==
      std::array<std::uint32_t, 4>{{5, 6, 7, 8}});
  }

  SECTION("Offset and difference modes apply and update row values")
  {
    VPU vpu(VPUType::VU1);
    VIF vif(VIFType::VIF1);
    vif.attachVPU(&vpu);
    setCycle(&vif, 1, 1);
    setVectorRegister(
      &vif,
      VIFCommandKind::STROW,
      {{10, 20, 30, 40}});
    vif.ingestWord(vifCode(VIFCommandEncoding::STMOD, 0, 1));
    vif.ingestWord(vifCode(
      VIFCommandEncoding::UNPACK | VIFUnpackEncoding::V4_32,
      1,
      0));
    for (std::uint32_t value = 1; value <= 4; ++value)
    {
      vif.ingestWord(value);
    }

    REQUIRE(
      vpu.readDataQuadword(0) ==
      std::array<std::uint32_t, 4>{{11, 22, 33, 44}});
    REQUIRE(vif.row(0) == 10);

    vif.ingestWord(vifCode(VIFCommandEncoding::STMOD, 0, 2));
    vif.ingestWord(vifCode(
      VIFCommandEncoding::UNPACK | VIFUnpackEncoding::V4_32,
      1,
      1));
    for (std::uint32_t value = 5; value <= 8; ++value)
    {
      vif.ingestWord(value);
    }

    REQUIRE(
      vpu.readDataQuadword(1) ==
      std::array<std::uint32_t, 4>{{15, 26, 37, 48}});
    REQUIRE(vif.row(0) == 15);
    REQUIRE(vif.row(3) == 48);
  }

  SECTION("TOPS-relative and mirrored addresses wrap to physical memory")
  {
    VPU vu1(VPUType::VU1);
    VIF vif1(VIFType::VIF1);
    vif1.attachVPU(&vu1);
    setCycle(&vif1, 1, 1);
    vif1.ingestWord(vifCode(VIFCommandEncoding::BASE, 0, 1020));
    vif1.ingestWord(vifCode(VIFCommandEncoding::OFFSET, 0, 0));
    vif1.ingestWord(vifCode(
      VIFCommandEncoding::UNPACK | VIFUnpackEncoding::S_32,
      1,
      VIFImmediateEncoding::UNPACKAddTOPS | 10));
    vif1.ingestWord(0x11111111);
    REQUIRE(vu1.readDataQuadword(6)[0] == 0x11111111);

    VPU vu0(VPUType::VU0);
    VIF vif0(VIFType::VIF0);
    vif0.attachVPU(&vu0);
    setCycle(&vif0, 1, 1);
    vif0.ingestWord(vifCode(
      VIFCommandEncoding::UNPACK | VIFUnpackEncoding::S_32,
      1,
      0x0101));
    vif0.ingestWord(0x22222222);
    REQUIRE(vu0.readDataQuadword(1)[0] == 0x22222222);
  }

  SECTION("Fill-only packets execute without payload words")
  {
    VPU vpu(VPUType::VU1);
    VIF vif(VIFType::VIF1);
    vif.attachVPU(&vpu);
    setCycle(&vif, 4, 0);
    setVectorRegister(
      &vif,
      VIFCommandKind::STROW,
      {{1, 2, 3, 4}});
    vif.ingestWord(vifCode(VIFCommandEncoding::STMASK));
    vif.ingestWord(
      repeatedMask(0, UNPACK_MASK_ROW) |
      repeatedMask(1, UNPACK_MASK_ROW) |
      repeatedMask(2, UNPACK_MASK_ROW) |
      repeatedMask(3, UNPACK_MASK_ROW));

    const VIFStreamWord command = vif.ingestWord(vifCode(
      VIFCommandEncoding::UNPACK |
        VIFCommandEncoding::UNPACKMask |
        VIFUnpackEncoding::V4_32,
      4,
      0));

    REQUIRE(command.payloadWordCount == 0);
    REQUIRE(command.packetComplete);
    for (std::uint32_t address = 0; address < 4; ++address)
    {
      REQUIRE(
        vpu.readDataQuadword(address) ==
        std::array<std::uint32_t, 4>{{1, 2, 3, 4}});
    }

    SECTION("Zero NUM writes 256 vectors")
    {
      constexpr std::uint32_t UNPACK_ZERO_COUNT = 256;
      constexpr std::uint32_t ELEMENTS_PER_WORD = 4;

      VPU vpu(VPUType::VU1);
      VIF vif(VIFType::VIF1);
      vif.attachVPU(&vpu);
      setCycle(&vif, 1, 1);
      const VIFStreamWord command = vif.ingestWord(vifCode(
        VIFCommandEncoding::UNPACK | VIFUnpackEncoding::S_8,
        0,
        100));

      REQUIRE(
        command.payloadWordCount ==
        UNPACK_ZERO_COUNT / ELEMENTS_PER_WORD);
      while (vif.awaitingPayload())
      {
        vif.ingestWord(0x7f7f7f7f);
      }

      REQUIRE(vpu.readDataQuadword(100)[0] == 0x7f);
      REQUIRE(
        vpu.readDataQuadword(100 + UNPACK_ZERO_COUNT - 1)[3] ==
        0x7f);
    }
  }

  SECTION("UNPACK validates its VPU and CYCLE prerequisites")
  {
    VIF vif(VIFType::VIF1);
    setCycle(&vif, 1, 1);
    REQUIRE_THROWS_WITH(
      vif.ingestWord(vifCode(
        VIFCommandEncoding::UNPACK | VIFUnpackEncoding::S_32,
        1,
        0)),
      "VIF UNPACK requires an attached VPU.");

    VPU vpu(VPUType::VU1);
    vif.attachVPU(&vpu);
    setCycle(&vif, 0, 1);
    REQUIRE_THROWS_WITH(
      vif.ingestWord(vifCode(
        VIFCommandEncoding::UNPACK | VIFUnpackEncoding::S_32,
        1,
        0)),
      "VIF UNPACK requires a nonzero write length.");
  }
}
