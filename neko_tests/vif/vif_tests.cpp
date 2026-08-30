#include <array>
#include <stdexcept>

#include "catch.hpp"
#include "vif.hpp"

namespace
{
  constexpr std::uint16_t MAX_ENCODED_COUNT = 256;
  constexpr std::uint8_t UNDEFINED_COMMAND = 0x08;

  std::uint32_t vifCode(
    std::uint8_t command,
    std::uint8_t count = 0,
    std::uint16_t immediate = 0,
    bool interrupt = false)
  {
    return
      (static_cast<std::uint32_t>(
        command |
        (interrupt ? VIFCommandEncoding::Interrupt : 0)) << 24) |
      (static_cast<std::uint32_t>(count) << 16) |
      immediate;
  }
}

TEST_CASE("VIF Command Decoder Tests")
{
  SECTION("Every non-UNPACK command decodes to its typed operation")
  {
    const std::array<std::pair<std::uint8_t, VIFCommandKind>, 20> commands = {{
      {VIFCommandEncoding::NOP, VIFCommandKind::NOP},
      {VIFCommandEncoding::STCYCL, VIFCommandKind::STCYCL},
      {VIFCommandEncoding::OFFSET, VIFCommandKind::OFFSET},
      {VIFCommandEncoding::BASE, VIFCommandKind::BASE},
      {VIFCommandEncoding::ITOP, VIFCommandKind::ITOP},
      {VIFCommandEncoding::STMOD, VIFCommandKind::STMOD},
      {VIFCommandEncoding::MSKPATH3, VIFCommandKind::MSKPATH3},
      {VIFCommandEncoding::MARK, VIFCommandKind::MARK},
      {VIFCommandEncoding::FLUSHE, VIFCommandKind::FLUSHE},
      {VIFCommandEncoding::FLUSH, VIFCommandKind::FLUSH},
      {VIFCommandEncoding::FLUSHA, VIFCommandKind::FLUSHA},
      {VIFCommandEncoding::MSCAL, VIFCommandKind::MSCAL},
      {VIFCommandEncoding::MSCALF, VIFCommandKind::MSCALF},
      {VIFCommandEncoding::MSCNT, VIFCommandKind::MSCNT},
      {VIFCommandEncoding::STMASK, VIFCommandKind::STMASK},
      {VIFCommandEncoding::STROW, VIFCommandKind::STROW},
      {VIFCommandEncoding::STCOL, VIFCommandKind::STCOL},
      {VIFCommandEncoding::MPG, VIFCommandKind::MPG},
      {VIFCommandEncoding::DIRECT, VIFCommandKind::DIRECT},
      {VIFCommandEncoding::DIRECTHL, VIFCommandKind::DIRECTHL}
    }};

    for (const auto &contract : commands)
    {
      const VIFCommand command = decodeVIFCommand(
        vifCode(contract.first, 7, 0x1234, true),
        VIFType::VIF1);

      REQUIRE(command.kind == contract.second);
      REQUIRE(command.command == contract.first);
      REQUIRE(command.encodedCount == 7);
      REQUIRE(command.count == 7);
      REQUIRE(command.immediate == 0x1234);
      REQUIRE(command.interrupt);
    }
  }

  SECTION("Zero NUM represents 256 entries")
  {
    const VIFCommand command = decodeVIFCommand(
      vifCode(VIFCommandEncoding::MPG),
      VIFType::VIF0);

    REQUIRE(command.encodedCount == 0);
    REQUIRE(command.count == MAX_ENCODED_COUNT);
  }

  SECTION("All thirteen defined UNPACK formats decode")
  {
    const std::array<std::pair<std::uint8_t, VIFUnpackFormat>, 13> formats = {{
      {VIFUnpackEncoding::S_32, VIFUnpackFormat::S_32},
      {VIFUnpackEncoding::S_16, VIFUnpackFormat::S_16},
      {VIFUnpackEncoding::S_8, VIFUnpackFormat::S_8},
      {VIFUnpackEncoding::V2_32, VIFUnpackFormat::V2_32},
      {VIFUnpackEncoding::V2_16, VIFUnpackFormat::V2_16},
      {VIFUnpackEncoding::V2_8, VIFUnpackFormat::V2_8},
      {VIFUnpackEncoding::V3_32, VIFUnpackFormat::V3_32},
      {VIFUnpackEncoding::V3_16, VIFUnpackFormat::V3_16},
      {VIFUnpackEncoding::V3_8, VIFUnpackFormat::V3_8},
      {VIFUnpackEncoding::V4_32, VIFUnpackFormat::V4_32},
      {VIFUnpackEncoding::V4_16, VIFUnpackFormat::V4_16},
      {VIFUnpackEncoding::V4_8, VIFUnpackFormat::V4_8},
      {VIFUnpackEncoding::V4_5, VIFUnpackFormat::V4_5}
    }};

    for (const auto &contract : formats)
    {
      const VIFCommand command = decodeVIFCommand(
        vifCode(
          VIFCommandEncoding::UNPACK |
            VIFCommandEncoding::UNPACKMask |
            contract.first,
          9,
          VIFImmediateEncoding::UNPACKAddTOPS |
            VIFImmediateEncoding::UNPACKUnsigned |
            0x02aa,
          true),
        VIFType::VIF1);

      REQUIRE(command.kind == VIFCommandKind::UNPACK);
      REQUIRE(command.unpackFormat == contract.second);
      REQUIRE(command.masked);
      REQUIRE(command.unsignedData);
      REQUIRE(command.addTops);
      REQUIRE(
        command.address ==
        (0x02aa & VIFImmediateEncoding::AddressMask));
      REQUIRE(command.count == 9);
      REQUIRE(command.interrupt);
    }
  }

  SECTION("VIF0 ignores the VIF1-only UNPACK TOPS flag")
  {
    const VIFCommand command = decodeVIFCommand(
      vifCode(
        VIFCommandEncoding::UNPACK |
          VIFUnpackEncoding::V4_32,
        1,
        VIFImmediateEncoding::UNPACKAddTOPS),
      VIFType::VIF0);

    REQUIRE(!command.addTops);
  }

  SECTION("Undefined commands and UNPACK formats are rejected")
  {
    REQUIRE_THROWS_WITH(
      decodeVIFCommand(vifCode(UNDEFINED_COMMAND), VIFType::VIF1),
      "Unsupported VIF command.");

    for (const std::uint8_t format : {0x3, 0x7, 0xb})
    {
      REQUIRE_THROWS_WITH(
        decodeVIFCommand(
          vifCode(VIFCommandEncoding::UNPACK | format),
          VIFType::VIF1),
        "Unsupported VIF UNPACK format.");
    }
  }

  SECTION("VIF1-only commands are rejected on VIF0")
  {
    for (const std::uint8_t command : {
      VIFCommandEncoding::OFFSET,
      VIFCommandEncoding::BASE,
      VIFCommandEncoding::MSKPATH3,
      VIFCommandEncoding::FLUSH,
      VIFCommandEncoding::FLUSHA,
      VIFCommandEncoding::MSCALF,
      VIFCommandEncoding::DIRECT,
      VIFCommandEncoding::DIRECTHL})
    {
      REQUIRE_THROWS_WITH(
        decodeVIFCommand(vifCode(command), VIFType::VIF0),
        "VIF command is only supported on VIF1.");
    }
  }

  SECTION("Undefined addition mode is rejected")
  {
    REQUIRE_THROWS_WITH(
      decodeVIFCommand(
        vifCode(VIFCommandEncoding::STMOD, 0, 3),
        VIFType::VIF1),
      "Unsupported VIF addition mode.");
  }
}

TEST_CASE("VIF State Tests")
{
  SECTION("VIF0 and VIF1 start with independent reset state")
  {
    VIF vif0(VIFType::VIF0);
    VIF vif1(VIFType::VIF1);

    REQUIRE(vif0.unitType() == VIFType::VIF0);
    REQUIRE(vif1.unitType() == VIFType::VIF1);
    REQUIRE(vif0.cycle() == 0);
    REQUIRE(vif1.cycle() == 0);
    REQUIRE(vif0.lastCode() == 0);
    REQUIRE(vif1.lastCode() == 0);
  }

  SECTION("Single-word configuration commands update their registers")
  {
    VIF vif(VIFType::VIF1);
    const std::uint32_t cycleCode = vifCode(
      VIFCommandEncoding::STCYCL,
      0xcc,
      0x0804);
    vif.processCode(cycleCode);
    vif.processCode(vifCode(VIFCommandEncoding::BASE, 0, 0xffff));
    vif.processCode(vifCode(VIFCommandEncoding::OFFSET, 0, 0x0555));
    vif.processCode(vifCode(VIFCommandEncoding::ITOP, 0, 0xf321));
    vif.processCode(vifCode(VIFCommandEncoding::STMOD, 0, 2));
    vif.processCode(vifCode(VIFCommandEncoding::MSKPATH3, 0, 0x8000));
    vif.processCode(vifCode(VIFCommandEncoding::MARK, 0, 0xbeef));

    REQUIRE(vif.cycle() == 0x0804);
    REQUIRE(vif.cycleLength() == 4);
    REQUIRE(vif.writeLength() == 8);
    REQUIRE(
      vif.base() ==
      (0xffff & VIFImmediateEncoding::AddressMask));
    REQUIRE(
      vif.offset() ==
      (0x0555 & VIFImmediateEncoding::AddressMask));
    REQUIRE(vif.tops() == vif.base());
    REQUIRE(!vif.doubleBufferFlag());
    REQUIRE(
      vif.itops() ==
      (0xf321 & VIFImmediateEncoding::AddressMask));
    REQUIRE(vif.mode() == 2);
    REQUIRE(vif.path3Masked());
    REQUIRE(vif.mark() == 0xbeef);
    REQUIRE(vif.markDetected());
    REQUIRE(
      vif.lastCode() ==
      vifCode(VIFCommandEncoding::MARK, 0, 0xbeef));
  }

  SECTION("NOP changes only the most recently processed code")
  {
    VIF vif(VIFType::VIF0);
    vif.processCode(vifCode(VIFCommandEncoding::STCYCL, 0, 0x0404));
    const std::uint32_t nop = vifCode(
      VIFCommandEncoding::NOP,
      9,
      0xabcd,
      true);

    const VIFCommand decoded = vif.processCode(nop);

    REQUIRE(decoded.kind == VIFCommandKind::NOP);
    REQUIRE(vif.cycle() == 0x0404);
    REQUIRE(vif.lastCode() == nop);
  }

  SECTION("Commands owned by later graphics steps are not silently executed")
  {
    VIF vif(VIFType::VIF1);

    REQUIRE_THROWS_WITH(
      vif.processCode(vifCode(VIFCommandEncoding::MPG, 1, 0)),
      "VIF command execution requires its owning subsystem.");
    REQUIRE(
      vif.lastCode() ==
      vifCode(VIFCommandEncoding::MPG, 1, 0));
  }
}
