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
