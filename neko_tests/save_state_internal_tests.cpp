#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "catch.hpp"
#include "save_state_internal.hpp"

namespace
{
  static_assert(
    !std::is_copy_constructible<SaveStateWriter>::value,
    "SaveStateWriter must not be copy constructible.");
  static_assert(
    !std::is_move_constructible<SaveStateWriter>::value,
    "SaveStateWriter must not be move constructible.");
  static_assert(
    !std::is_copy_constructible<SaveStateReader>::value,
    "SaveStateReader must not be copy constructible.");
  static_assert(
    !std::is_move_constructible<SaveStateReader>::value,
    "SaveStateReader must not be move constructible.");

  struct RetainedSaveStateField
  {
    std::string path;
    SaveStateFieldKind kind = SaveStateFieldKind::U8;
    std::size_t containerOffset = 0;
    bool hasPayloadOffset = false;
    std::size_t payloadOffset = 0;
    std::size_t size = 0;
    std::uint64_t scalarValue = 0;
    std::vector<std::uint8_t> bytes;
  };

  class RetainingSaveStateObserver final :
    public SaveStateObserver
  {
    public:
      void observe(
        const SaveStateFieldObservation &field) override
      {
        RetainedSaveStateField retained;
        retained.path = field.path;
        retained.kind = field.kind;
        retained.containerOffset = field.containerOffset;
        retained.hasPayloadOffset = field.hasPayloadOffset;
        retained.payloadOffset = field.payloadOffset;
        retained.size = field.size;
        retained.scalarValue = field.scalarValue;
        if (field.bytes != nullptr)
        {
          retained.bytes.assign(
            field.bytes,
            field.bytes + field.size);
        }
        fields.push_back(std::move(retained));
      }

      std::vector<RetainedSaveStateField> fields;
  };

  struct CompactSaveStateField
  {
    std::string path;
    SaveStateFieldKind kind = SaveStateFieldKind::U8;
    std::size_t containerOffset = 0;
    bool hasPayloadOffset = false;
    std::size_t payloadOffset = 0;
    std::size_t size = 0;
    std::uint64_t scalarValue = 0;
    std::uint64_t byteHash = SAVE_STATE_FNV_OFFSET_BASIS;
  };

  class CompactSaveStateObserver final :
    public SaveStateObserver
  {
    public:
      void observe(
        const SaveStateFieldObservation &field) override
      {
        CompactSaveStateField compact;
        compact.path = field.path;
        compact.kind = field.kind;
        compact.containerOffset = field.containerOffset;
        compact.hasPayloadOffset = field.hasPayloadOffset;
        compact.payloadOffset = field.payloadOffset;
        compact.size = field.size;
        compact.scalarValue = field.scalarValue;
        for (std::size_t index = 0;
             index < field.size && field.bytes != nullptr;
             ++index)
        {
          compact.byteHash ^= field.bytes[index];
          compact.byteHash *= SAVE_STATE_FNV_PRIME;
        }
        fields.push_back(std::move(compact));
      }

      std::vector<CompactSaveStateField> fields;
  };

  void sortFields(
    std::vector<CompactSaveStateField> *fields)
  {
    std::stable_sort(
      fields->begin(),
      fields->end(),
      [](const CompactSaveStateField &left,
         const CompactSaveStateField &right)
      {
        return left.containerOffset < right.containerOffset;
      });
  }
}

TEST_CASE("Save-state field observers receive scoped paths and ranges")
{
  RetainingSaveStateObserver observer;
  SaveStateWriter writer(&observer);
  writer.writeFieldU8("version", 31);
  writer.setPayloadOrigin(writer.size());

  {
    auto system = writer.scope("system");
    auto components = writer.scope("components");
    auto component = writer.element(3);
    writer.writeFieldU16("period", UINT16_C(0x1234));
  }
  const std::uint8_t memory[] = {0xaa, 0xbb, 0xcc};
  {
    auto system = writer.scope("system");
    writer.writeFieldBytes("memory", memory, sizeof(memory));
  }

  REQUIRE(observer.fields.size() == 3);
  REQUIRE(observer.fields[0].path == "version");
  REQUIRE(observer.fields[0].kind == SaveStateFieldKind::U8);
  REQUIRE(observer.fields[0].containerOffset == 0);
  REQUIRE_FALSE(observer.fields[0].hasPayloadOffset);
  REQUIRE(observer.fields[0].scalarValue == 31);

  REQUIRE(
    observer.fields[1].path ==
    "system.components[3].period");
  REQUIRE(observer.fields[1].kind == SaveStateFieldKind::U16);
  REQUIRE(observer.fields[1].containerOffset == 1);
  REQUIRE(observer.fields[1].hasPayloadOffset);
  REQUIRE(observer.fields[1].payloadOffset == 0);
  REQUIRE(observer.fields[1].size == 2);
  REQUIRE(observer.fields[1].scalarValue == UINT16_C(0x1234));

  REQUIRE(observer.fields[2].path == "system.memory");
  REQUIRE(observer.fields[2].kind == SaveStateFieldKind::Bytes);
  REQUIRE(observer.fields[2].containerOffset == 3);
  REQUIRE(observer.fields[2].payloadOffset == 2);
  REQUIRE(observer.fields[2].bytes == std::vector<std::uint8_t>{
    0xaa, 0xbb, 0xcc
  });
}

TEST_CASE("Save-state readers report decoded values through the same schema")
{
  const std::vector<std::uint8_t> bytes = {
    31, 0x34, 0x12, 0xaa, 0xbb, 0xcc
  };
  RetainingSaveStateObserver observer;
  SaveStateReader reader(bytes, &observer);
  REQUIRE(reader.readFieldU8("version") == 31);
  reader.setPayloadOrigin(reader.offset());

  {
    auto system = reader.scope("system");
    auto components = reader.scope("components");
    auto component = reader.element(3);
    REQUIRE(
      reader.readFieldU16("period") ==
      UINT16_C(0x1234));
  }
  std::uint8_t memory[3] = {};
  {
    auto system = reader.scope("system");
    reader.readFieldBytes("memory", memory, sizeof(memory));
  }

  REQUIRE(observer.fields.size() == 3);
  REQUIRE(
    observer.fields[1].path ==
    "system.components[3].period");
  REQUIRE(observer.fields[1].payloadOffset == 0);
  REQUIRE(observer.fields[1].scalarValue == UINT16_C(0x1234));
  REQUIRE(observer.fields[2].path == "system.memory");
  REQUIRE(observer.fields[2].payloadOffset == 2);
  REQUIRE(observer.fields[2].bytes == std::vector<std::uint8_t>{
    0xaa, 0xbb, 0xcc
  });
}

TEST_CASE("Save-state observers retain every scalar field kind")
{
  RetainingSaveStateObserver observer;
  SaveStateWriter writer(&observer);
  writer.writeFieldU32("word", UINT32_C(0x89abcdef));
  writer.writeFieldU64(
    "doubleword",
    UINT64_C(0x0123456789abcdef));
  writer.writeFieldBool("enabled", true);

  REQUIRE(observer.fields.size() == 3);
  REQUIRE(observer.fields[0].kind == SaveStateFieldKind::U32);
  REQUIRE(
    observer.fields[0].scalarValue ==
    UINT32_C(0x89abcdef));
  REQUIRE(observer.fields[1].kind == SaveStateFieldKind::U64);
  REQUIRE(
    observer.fields[1].scalarValue ==
    UINT64_C(0x0123456789abcdef));
  REQUIRE(
    observer.fields[2].kind ==
    SaveStateFieldKind::Boolean);
  REQUIRE(observer.fields[2].scalarValue == 1);
}

TEST_CASE("Save-state reader failures include field paths and offsets")
{
  SECTION("Truncated fields retain their attempted location")
  {
    const std::vector<std::uint8_t> bytes = {0xaa};
    SaveStateReader reader(bytes);
    reader.setPayloadOrigin(0);

    try
    {
      auto system = reader.scope("system");
      auto entries = reader.scope("entries");
      auto entry = reader.element(2);
      static_cast<void>(reader.readFieldU32("value"));
      FAIL("Expected a truncated save-state field.");
    }
    catch (const std::invalid_argument &error)
    {
      const std::string message = error.what();
      REQUIRE(
        message.find("system.entries[2].value") !=
        std::string::npos);
      REQUIRE(
        message.find("container offset 0") !=
        std::string::npos);
      REQUIRE(
        message.find("payload offset 0") !=
        std::string::npos);
      REQUIRE(
        message.find("data is truncated") !=
        std::string::npos);
    }
  }

  SECTION("Validation uses the most recently decoded field")
  {
    const std::vector<std::uint8_t> bytes = {3};
    SaveStateReader reader(bytes);

    try
    {
      auto system = reader.scope("system");
      const std::uint8_t mode =
        reader.readFieldU8("mode");
      reader.requireField(mode < 3, "mode is invalid");
      FAIL("Expected invalid field validation.");
    }
    catch (const std::invalid_argument &error)
    {
      const std::string message = error.what();
      REQUIRE(
        message.find("system.mode") !=
        std::string::npos);
      REQUIRE(
        message.find("container offset 0") !=
        std::string::npos);
      REQUIRE(
        message.find("mode is invalid") !=
        std::string::npos);
    }
  }

  SECTION("Unnamed reads clear prior field context")
  {
    const std::vector<std::uint8_t> bytes = {1};
    SaveStateReader reader(bytes);
    REQUIRE(reader.readFieldU8("named") == 1);

    try
    {
      static_cast<void>(reader.readU8());
      FAIL("Expected an unnamed truncated read.");
    }
    catch (const std::invalid_argument &error)
    {
      const std::string message = error.what();
      REQUIRE(message.find("named") == std::string::npos);
      REQUIRE(
        message ==
        "Invalid Neko save state: data is truncated.");
    }
  }
}

TEST_CASE("Unobserved named save-state writes preserve encoded bytes")
{
  SaveStateWriter unnamed;
  unnamed.writeU8(0x12);
  unnamed.writeU16(UINT16_C(0x3456));
  unnamed.writeU32(UINT32_C(0x789abcde));
  unnamed.writeBool(true);
  const std::uint8_t data[] = {1, 2, 3};
  unnamed.writeBytes(data, sizeof(data));

  SaveStateWriter named;
  {
    auto ignored = named.scope("ignored");
    named.writeFieldU8("byte", 0x12);
    named.writeFieldU16("halfword", UINT16_C(0x3456));
    named.writeFieldU32("word", UINT32_C(0x789abcde));
    named.writeFieldBool("flag", true);
    named.writeFieldBytes("data", data, sizeof(data));
  }

  REQUIRE(named.finish() == unnamed.finish());
}

TEST_CASE("Complete save-state codec exposes one matching schema")
{
  NekoSystem source;
  CompactSaveStateObserver writeObserver;
  const std::vector<std::uint8_t> state =
    NekoSaveStateCodec::save(source, &writeObserver);

  NekoSystem restored;
  CompactSaveStateObserver readObserver;
  NekoSaveStateCodec::load(
    &restored,
    state,
    &readObserver);

  sortFields(&writeObserver.fields);
  sortFields(&readObserver.fields);
  REQUIRE(writeObserver.fields.size() ==
          readObserver.fields.size());

  std::size_t expectedOffset = 0;
  bool observedPayload = false;
  for (std::size_t index = 0;
       index < writeObserver.fields.size();
       ++index)
  {
    const CompactSaveStateField &written =
      writeObserver.fields[index];
    const CompactSaveStateField &read =
      readObserver.fields[index];
    INFO("Field index: " << index);
    INFO("Written path: " << written.path);
    INFO("Read path: " << read.path);
    REQUIRE_FALSE(written.path.empty());
    REQUIRE(written.containerOffset == expectedOffset);
    expectedOffset += written.size;
    REQUIRE(read.path == written.path);
    REQUIRE(read.kind == written.kind);
    REQUIRE(read.containerOffset == written.containerOffset);
    REQUIRE(read.hasPayloadOffset == written.hasPayloadOffset);
    REQUIRE(read.payloadOffset == written.payloadOffset);
    REQUIRE(read.size == written.size);
    REQUIRE(read.scalarValue == written.scalarValue);
    REQUIRE(read.byteHash == written.byteHash);
    if (written.hasPayloadOffset)
    {
      observedPayload = true;
      REQUIRE(
        written.containerOffset ==
        SAVE_STATE_HEADER_SIZE + written.payloadOffset);
    }
  }

  REQUIRE(expectedOffset == state.size());
  REQUIRE(observedPayload);
  REQUIRE(writeObserver.fields.front().path == "container.magic");
  REQUIRE(writeObserver.fields[1].path == "container.version");
  REQUIRE(
    std::any_of(
      writeObserver.fields.begin(),
      writeObserver.fields.end(),
      [](const CompactSaveStateField &field)
      {
        return field.path == "system.input.buttons";
      }));
  REQUIRE(
    std::any_of(
      writeObserver.fields.begin(),
      writeObserver.fields.end(),
      [](const CompactSaveStateField &field)
      {
        return field.path ==
          "system.eeBus.mainMemory.bytes";
      }));
}

TEST_CASE("Component decode failures report complete schema locations")
{
  NekoSystem source;
  CompactSaveStateObserver observer;
  std::vector<std::uint8_t> state =
    NekoSaveStateCodec::save(source, &observer);

  const auto findField =
    [&](const std::string &path) -> const CompactSaveStateField &
    {
      const auto field = std::find_if(
        observer.fields.begin(),
        observer.fields.end(),
        [&](const CompactSaveStateField &candidate)
        {
          return candidate.path == path;
        });
      REQUIRE(field != observer.fields.end());
      return *field;
    };

  const CompactSaveStateField &checksumField =
    findField("container.checksum");
  const auto updateChecksum = [&]()
  {
    std::uint64_t checksum = SAVE_STATE_FNV_OFFSET_BASIS;
    for (std::size_t index = SAVE_STATE_HEADER_SIZE;
         index < state.size();
         ++index)
    {
      checksum ^= state[index];
      checksum *= SAVE_STATE_FNV_PRIME;
    }
    for (std::size_t index = 0;
         index < checksumField.size;
         ++index)
    {
      state[checksumField.containerOffset + index] =
        static_cast<std::uint8_t>(checksum >> (index * 8));
    }
  };

  SECTION("Direct enum decoding reports its field")
  {
    const CompactSaveStateField &type =
      findField("system.vu0.type");
    state[type.containerOffset] = 0xff;
    updateChecksum();

    NekoSystem destination;
    try
    {
      NekoSaveStateCodec::load(&destination, state);
      FAIL("Expected an invalid VU type.");
    }
    catch (const std::invalid_argument &error)
    {
      const std::string message = error.what();
      REQUIRE(
        message.find("system.vu0.type") !=
        std::string::npos);
      REQUIRE(
        message.find(
          "container offset " +
          std::to_string(type.containerOffset)) !=
        std::string::npos);
      REQUIRE(type.hasPayloadOffset);
      REQUIRE(
        message.find(
          "payload offset " +
          std::to_string(type.payloadOffset)) !=
        std::string::npos);
    }
  }

  SECTION("Per-field validation retains the offending field")
  {
    const CompactSaveStateField &flags =
      findField(
        "system.vu0.fpRegisters[0].xResultFlags");
    state[flags.containerOffset] = 0x10;
    updateChecksum();

    NekoSystem destination;
    REQUIRE_THROWS_WITH(
      NekoSaveStateCodec::load(&destination, state),
      "Invalid Neko save state at "
      "system.vu0.fpRegisters[0].xResultFlags at "
      "container offset " +
      std::to_string(flags.containerOffset) +
      ", payload offset " +
      std::to_string(flags.payloadOffset) +
      ": VU floating-point result flags are invalid.");
  }

  SECTION("Aggregate validation does not blame an unrelated leaf")
  {
    const CompactSaveStateField &path3Mask =
      findField("system.vif1.path3Mask");
    state[path3Mask.containerOffset] = 1;
    updateChecksum();

    NekoSystem destination;
    REQUIRE_THROWS_WITH(
      NekoSaveStateCodec::load(&destination, state),
      "Invalid Neko save state: "
      "VIF1 and GIF PATH3 mask state disagree.");
  }
}
