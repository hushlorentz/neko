#include "catch.hpp"
#include "save_state_diagnostics.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace
{
  SaveStateDiagnosticField scalarField(
    const std::string &path,
    SaveStateFieldKind kind,
    std::uint64_t value,
    std::size_t size)
  {
    SaveStateDiagnosticField field;
    field.path = path;
    field.kind = kind;
    field.scalarValue = value;
    field.size = size;
    return field;
  }
}

TEST_CASE("Save-state diagnostics retain one deterministic schema")
{
  NekoSystem system;
  const SaveStateDiagnosticSnapshot snapshot =
    inspectSaveState(system.saveState());

  REQUIRE(snapshot.fields.size() > 4);
  REQUIRE(snapshot.fields[0].path == "container.magic");
  REQUIRE(snapshot.fields[1].path == "container.version");
  REQUIRE(snapshot.fields[2].path == "container.payloadLength");
  REQUIRE(snapshot.fields[3].path == "container.checksum");
  REQUIRE(std::is_sorted(
    snapshot.fields.begin(),
    snapshot.fields.end(),
    [](const SaveStateDiagnosticField &left,
       const SaveStateDiagnosticField &right)
    {
      return left.containerOffset < right.containerOffset;
    }));

  const SaveStateDiagnosticField *memory =
    snapshot.find("system.eeBus.mainMemory.bytes");
  REQUIRE(memory != nullptr);
  REQUIRE(memory->kind == SaveStateFieldKind::Bytes);
  REQUIRE(memory->size == EEMemoryMap::MAIN_MEMORY_SIZE);
  REQUIRE(memory->bytes == nullptr);
  REQUIRE(memory->hasPayloadOffset);

  const SaveStateDiagnosticField *buttons =
    snapshot.find("system.input.buttons");
  REQUIRE(buttons != nullptr);
  REQUIRE(
    formatSaveStateField(*buttons) ==
    "system.input.buttons u16 0x0000 "
    "container=28..30 payload=0..2");
}

TEST_CASE("Save-state diagnostics retain selected byte ranges explicitly")
{
  NekoSystem system;
  SaveStateDiagnosticOptions options;
  options.retainedBytePaths.push_back("container.magic");
  options.retainedBytePaths.push_back(
    "system.scratchpadDMA.scratchpad");

  const SaveStateDiagnosticSnapshot snapshot =
    inspectSaveState(system.saveState(), options);
  const SaveStateDiagnosticField *magic =
    snapshot.find("container.magic");
  REQUIRE(magic != nullptr);
  REQUIRE(magic->bytes != nullptr);
  REQUIRE(*magic->bytes == std::vector<std::uint8_t>{
    'N', 'E', 'K', 'O', 'S', 'T', 'A', 'T'
  });
  REQUIRE(
    formatSaveStateField(*magic) ==
    "container.magic bytes size=8 "
    "fnv1a64=0xad5d00c4b2c17fe4 "
    "data=4e454b4f53544154 container=0..8 payload=n/a");

  const SaveStateDiagnosticField *scratchpad =
    snapshot.find("system.scratchpadDMA.scratchpad");
  REQUIRE(scratchpad != nullptr);
  REQUIRE(scratchpad->bytes != nullptr);
  REQUIRE(scratchpad->bytes->size() == scratchpad->size);
}

TEST_CASE("Save-state diagnostics validate retained byte paths")
{
  NekoSystem system;

  SaveStateDiagnosticOptions missing;
  missing.retainedBytePaths.push_back("system.missing");
  REQUIRE_THROWS_WITH(
    inspectSaveState(system.saveState(), missing),
    "Save-state byte field not found: system.missing");

  SaveStateDiagnosticOptions scalar;
  scalar.retainedBytePaths.push_back("system.input.buttons");
  REQUIRE_THROWS_WITH(
    inspectSaveState(system.saveState(), scalar),
    "Save-state field is not a byte range: "
    "system.input.buttons");

  REQUIRE_THROWS(inspectSaveState({0}));
}

TEST_CASE("Save-state diagnostics format every field kind")
{
  REQUIRE(
    formatSaveStateField(
      scalarField("u8", SaveStateFieldKind::U8, 0xab, 1)) ==
    "u8 u8 0xab container=0..1 payload=n/a");
  REQUIRE(
    formatSaveStateField(
      scalarField("u16", SaveStateFieldKind::U16, 0xabcd, 2)) ==
    "u16 u16 0xabcd container=0..2 payload=n/a");
  REQUIRE(
    formatSaveStateField(
      scalarField(
        "u32",
        SaveStateFieldKind::U32,
        0xabcdef01,
        4)) ==
    "u32 u32 0xabcdef01 container=0..4 payload=n/a");
  REQUIRE(
    formatSaveStateField(
      scalarField(
        "u64",
        SaveStateFieldKind::U64,
        0xabcdef0123456789ULL,
        8)) ==
    "u64 u64 0xabcdef0123456789 "
    "container=0..8 payload=n/a");
  REQUIRE(
    formatSaveStateField(
      scalarField(
        "enabled",
        SaveStateFieldKind::Boolean,
        1,
        1)) ==
    "enabled bool true container=0..1 payload=n/a");

  SaveStateDiagnosticField bytes;
  bytes.path = "bytes";
  bytes.kind = SaveStateFieldKind::Bytes;
  bytes.size = 3;
  bytes.byteHash = 0x1234;
  REQUIRE(
    formatSaveStateField(bytes) ==
    "bytes bytes size=3 fnv1a64=0x0000000000001234 "
    "container=0..3 payload=n/a");
}

TEST_CASE("Save-state streaming ignores caller formatting")
{
  SaveStateDiagnosticField field =
    scalarField(
      "value",
      SaveStateFieldKind::U32,
      0x1234,
      4);
  field.containerOffset = 16;
  field.hasPayloadOffset = true;
  field.payloadOffset = 8;

  std::ostringstream output;
  output << std::hex << std::showbase << std::setfill('*');
  output.width(20);
  const auto flags = output.flags();
  const char fill = output.fill();
  const std::streamsize width = output.width();

  writeSaveStateField(output, field);

  REQUIRE(output.str() == formatSaveStateField(field));
  REQUIRE(output.flags() == flags);
  REQUIRE(output.fill() == fill);
  REQUIRE(output.width() == width);
}

TEST_CASE("Save-state diagnostics locate exact paths")
{
  NekoSystem system;
  const SaveStateDiagnosticSnapshot snapshot =
    inspectSaveState(system.saveState());

  REQUIRE(snapshot.find("system.vu0.mode") != nullptr);
  REQUIRE(snapshot.find("system.vu0.missing") == nullptr);
}

TEST_CASE("Save-state semantic differences report values by path")
{
  NekoSystem leftSystem;
  NekoSystem rightSystem;
  NekoInputState input;
  input.buttons = 0x1234;
  rightSystem.setInput(input);
  rightSystem.eeBus().write8(0, 0xaa);

  const SaveStateDiagnosticSnapshot left =
    inspectSaveState(leftSystem.saveState());
  const SaveStateDiagnosticSnapshot right =
    inspectSaveState(rightSystem.saveState());
  const std::vector<SaveStateDifference> differences =
    diffSaveStates(left, right);

  REQUIRE(differences.size() == 2);
  REQUIRE(
    differences[0].path ==
    "system.eeBus.mainMemory.bytes");
  REQUIRE(
    differences[0].kind ==
    SaveStateDifferenceKind::Value);
  REQUIRE(differences[1].path == "system.input.buttons");
  REQUIRE(
    differences[1].kind ==
    SaveStateDifferenceKind::Value);
  REQUIRE(
    formatSaveStateDifference(differences[1]) ==
    "changed system.input.buttons: 0x0000 -> 0x1234");
}

TEST_CASE("Save-state semantic differences classify schema changes")
{
  SaveStateDiagnosticSnapshot left;
  left.fields.push_back(
    scalarField("removed", SaveStateFieldKind::U8, 1, 1));
  left.fields.push_back(
    scalarField("metadata", SaveStateFieldKind::U8, 2, 1));
  left.fields.push_back(
    scalarField(
      "boolean",
      SaveStateFieldKind::Boolean,
      0,
      1));

  SaveStateDiagnosticSnapshot right;
  right.fields.push_back(
    scalarField("added", SaveStateFieldKind::U8, 3, 1));
  right.fields.push_back(
    scalarField("metadata", SaveStateFieldKind::U16, 2, 2));
  right.fields.push_back(
    scalarField(
      "boolean",
      SaveStateFieldKind::Boolean,
      1,
      1));

  const std::vector<SaveStateDifference> differences =
    diffSaveStates(left, right);

  REQUIRE(differences.size() == 4);
  REQUIRE(differences[0].path == "added");
  REQUIRE(
    differences[0].kind ==
    SaveStateDifferenceKind::Added);
  REQUIRE(differences[1].path == "boolean");
  REQUIRE(
    formatSaveStateDifference(differences[1]) ==
    "changed boolean: false -> true");
  REQUIRE(differences[2].path == "metadata");
  REQUIRE(
    differences[2].kind ==
    SaveStateDifferenceKind::Metadata);
  REQUIRE(differences[3].path == "removed");
  REQUIRE(
    differences[3].kind ==
    SaveStateDifferenceKind::Removed);

  REQUIRE(diffSaveStates(left, left).empty());
}

TEST_CASE("Save-state semantic differences prefer retained bytes")
{
  SaveStateDiagnosticField leftField;
  leftField.path = "bytes";
  leftField.kind = SaveStateFieldKind::Bytes;
  leftField.size = 1;
  leftField.byteHash = 0x1234;
  leftField.bytes =
    std::make_shared<const std::vector<std::uint8_t>>(
      std::vector<std::uint8_t>{0x01});

  SaveStateDiagnosticField rightField = leftField;
  rightField.bytes =
    std::make_shared<const std::vector<std::uint8_t>>(
      std::vector<std::uint8_t>{0x02});

  SaveStateDiagnosticSnapshot left;
  left.fields.push_back(leftField);
  SaveStateDiagnosticSnapshot right;
  right.fields.push_back(rightField);

  const std::vector<SaveStateDifference> differences =
    diffSaveStates(left, right);
  REQUIRE(differences.size() == 1);
  REQUIRE(
    differences[0].kind ==
    SaveStateDifferenceKind::Value);
  REQUIRE(differences[0].left.bytes == leftField.bytes);
  REQUIRE(differences[0].right.bytes == rightField.bytes);
}
