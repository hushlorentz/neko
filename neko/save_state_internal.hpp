#pragma once

#include "neko_system.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <list>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

constexpr std::uint8_t SAVE_STATE_MAGIC[] = {
  'N', 'E', 'K', 'O', 'S', 'T', 'A', 'T'
};
constexpr std::uint32_t SAVE_STATE_VERSION = 31;
constexpr std::size_t SAVE_STATE_HEADER_SIZE = 28;
constexpr std::uint64_t SAVE_STATE_FNV_OFFSET_BASIS =
  UINT64_C(14695981039346656037);
constexpr std::uint64_t SAVE_STATE_FNV_PRIME =
  UINT64_C(1099511628211);
constexpr std::size_t SAVE_STATE_RESERVE_BYTES =
  EEMemoryMap::MAIN_MEMORY_SIZE + 5 * 1024 * 1024;

enum class SaveStateFieldKind : std::uint8_t
{
  U8,
  U16,
  U32,
  U64,
  Boolean,
  Bytes
};

struct SaveStateFieldObservation
{
  std::string path;
  SaveStateFieldKind kind = SaveStateFieldKind::U8;
  std::size_t containerOffset = 0;
  bool hasPayloadOffset = false;
  std::size_t payloadOffset = 0;
  std::size_t size = 0;
  std::uint64_t scalarValue = 0;
  // The byte view is valid only during SaveStateObserver::observe().
  const std::uint8_t *bytes = nullptr;
};

class SaveStateObserver
{
  public:
    virtual ~SaveStateObserver() = default;
    // Patched fields may be reported after later encoded ranges.
    // Consumers that need byte order must sort by containerOffset.
    virtual void observe(
      const SaveStateFieldObservation &field) = 0;
};

class SaveStatePathState
{
  public:
    static constexpr std::size_t MAX_DEPTH = 32;

    void pushName(const char *name)
    {
      requireCapacity();
      segments[depth].name = name;
      segments[depth].index = 0;
      segments[depth].isIndex = false;
      ++depth;
    }

    void pushIndex(std::size_t index)
    {
      requireCapacity();
      segments[depth].name = nullptr;
      segments[depth].index = index;
      segments[depth].isIndex = true;
      ++depth;
    }

    void pop()
    {
      if (depth == 0)
      {
        throw std::logic_error(
          "Save-state diagnostic path scope is unbalanced.");
      }
      --depth;
    }

    std::string format(const char *leaf = nullptr) const
    {
      std::string result;
      for (std::size_t index = 0; index < depth; ++index)
      {
        const Segment &segment = segments[index];
        if (segment.isIndex)
        {
          result += "[";
          result += std::to_string(segment.index);
          result += "]";
        }
        else
        {
          if (!result.empty())
          {
            result += ".";
          }
          result += segment.name;
        }
      }
      if (leaf != nullptr)
      {
        if (!result.empty())
        {
          result += ".";
        }
        result += leaf;
      }
      return result;
    }

  private:
    struct Segment
    {
      const char *name = nullptr;
      std::size_t index = 0;
      bool isIndex = false;
    };

    void requireCapacity() const
    {
      if (depth == segments.size())
      {
        throw std::logic_error(
          "Save-state diagnostic path is too deep.");
      }
    }

    std::array<Segment, MAX_DEPTH> segments = {};
    std::size_t depth = 0;
};

class SaveStatePathScope
{
  public:
    SaveStatePathScope(
      SaveStatePathState *path,
      const char *name) :
      state(path)
    {
      state->pushName(name);
    }

    SaveStatePathScope(
      SaveStatePathState *path,
      std::size_t index) :
      state(path)
    {
      state->pushIndex(index);
    }

    SaveStatePathScope(const SaveStatePathScope &) = delete;
    SaveStatePathScope &operator=(
      const SaveStatePathScope &) = delete;

    SaveStatePathScope(SaveStatePathScope &&other) noexcept :
      state(other.state)
    {
      other.state = nullptr;
    }

    SaveStatePathScope &operator=(
      SaveStatePathScope &&other) = delete;

    ~SaveStatePathScope()
    {
      if (state != nullptr)
      {
        state->pop();
      }
    }

  private:
    SaveStatePathState *state = nullptr;
};

class SaveStateWriter
{
  public:
    explicit SaveStateWriter(
      SaveStateObserver *fieldObserver = nullptr) :
      observer(fieldObserver)
    {
      bytes.reserve(SAVE_STATE_RESERVE_BYTES);
    }

    SaveStateWriter(const SaveStateWriter &) = delete;
    SaveStateWriter &operator=(const SaveStateWriter &) = delete;
    SaveStateWriter(SaveStateWriter &&) = delete;
    SaveStateWriter &operator=(SaveStateWriter &&) = delete;

    SaveStatePathScope scope(const char *name)
    {
      return SaveStatePathScope(&path, name);
    }

    SaveStatePathScope element(std::size_t index)
    {
      return SaveStatePathScope(&path, index);
    }

    void setPayloadOrigin(std::size_t offset)
    {
      payloadOrigin = offset;
      hasPayloadOrigin = true;
    }

    void writeU8(std::uint8_t value)
    {
      bytes.push_back(value);
    }

    void writeU16(std::uint16_t value)
    {
      writeU8(value & 0xff);
      writeU8((value >> 8) & 0xff);
    }

    void writeU32(std::uint32_t value)
    {
      writeU16(value & 0xffff);
      writeU16((value >> 16) & 0xffff);
    }

    void writeU64(std::uint64_t value)
    {
      writeU32(value & UINT64_C(0xffffffff));
      writeU32(value >> 32);
    }

    void writeBool(bool value)
    {
      writeU8(value ? 1 : 0);
    }

    void writeFieldU8(
      const char *name,
      std::uint8_t value)
    {
      const std::size_t start = size();
      writeU8(value);
      observeScalar(
        name,
        SaveStateFieldKind::U8,
        start,
        sizeof(value),
        value);
    }

    void writeFieldU16(
      const char *name,
      std::uint16_t value)
    {
      const std::size_t start = size();
      writeU16(value);
      observeScalar(
        name,
        SaveStateFieldKind::U16,
        start,
        sizeof(value),
        value);
    }

    void writeFieldU32(
      const char *name,
      std::uint32_t value)
    {
      const std::size_t start = size();
      writeU32(value);
      observeScalar(
        name,
        SaveStateFieldKind::U32,
        start,
        sizeof(value),
        value);
    }

    void writeFieldU64(
      const char *name,
      std::uint64_t value)
    {
      const std::size_t start = size();
      writeU64(value);
      observeScalar(
        name,
        SaveStateFieldKind::U64,
        start,
        sizeof(value),
        value);
    }

    void writeFieldBool(
      const char *name,
      bool value)
    {
      const std::size_t start = size();
      writeBool(value);
      observeScalar(
        name,
        SaveStateFieldKind::Boolean,
        start,
        1,
        value ? 1 : 0);
    }

    void writeFieldSize(
      const char *name,
      std::size_t value)
    {
      if (value > std::numeric_limits<std::uint32_t>::max())
      {
        throw std::runtime_error(
          "Neko save-state container is too large.");
      }
      writeFieldU32(
        name,
        static_cast<std::uint32_t>(value));
    }

    void writeBytes(
      const std::uint8_t *values,
      std::size_t count)
    {
      if (count == 0)
      {
        return;
      }
      bytes.insert(bytes.end(), values, values + count);
    }

    void writeFieldBytes(
      const char *name,
      const std::uint8_t *values,
      std::size_t count)
    {
      const std::size_t start = size();
      writeBytes(values, count);
      observe(
        name,
        SaveStateFieldKind::Bytes,
        start,
        count,
        0,
        values);
    }

    template<typename Function>
    void writeFieldRange(
      const char *name,
      Function writeRange)
    {
      const std::size_t start = size();
      writeRange();
      const std::size_t count = size() - start;
      observe(
        name,
        SaveStateFieldKind::Bytes,
        start,
        count,
        0,
        count == 0 ? nullptr : bytes.data() + start);
    }

    void writeByteVector(
      const std::vector<std::uint8_t> &values)
    {
      writeSize(values.size());
      writeBytes(values.data(), values.size());
    }

    void writeFieldByteVector(
      const char *name,
      const std::vector<std::uint8_t> &values)
    {
      auto field = scope(name);
      writeFieldSize("size", values.size());
      writeFieldBytes("bytes", values.data(), values.size());
    }

    void writeSize(std::size_t value)
    {
      if (value > std::numeric_limits<std::uint32_t>::max())
      {
        throw std::runtime_error(
          "Neko save-state container is too large.");
      }
      writeU32(static_cast<std::uint32_t>(value));
    }

    void patchU64(
      std::size_t offset,
      std::uint64_t value)
    {
      if (offset + sizeof(value) > bytes.size())
      {
        throw std::logic_error(
          "Neko save-state header patch is out of range.");
      }
      for (std::size_t index = 0;
           index < sizeof(value);
           ++index)
      {
        bytes[offset + index] =
          static_cast<std::uint8_t>(
            value >> (index * 8));
      }
    }

    void patchFieldU64(
      const char *name,
      std::size_t offset,
      std::uint64_t value)
    {
      patchU64(offset, value);
      observeScalar(
        name,
        SaveStateFieldKind::U64,
        offset,
        sizeof(value),
        value);
    }

    std::uint64_t checksumFrom(std::size_t offset) const
    {
      if (offset > bytes.size())
      {
        throw std::logic_error(
          "Neko save-state checksum offset is out of range.");
      }
      std::uint64_t checksum =
        SAVE_STATE_FNV_OFFSET_BASIS;
      for (std::size_t index = offset;
           index < bytes.size();
           ++index)
      {
        checksum ^= bytes[index];
        checksum *= SAVE_STATE_FNV_PRIME;
      }
      return checksum;
    }

    std::vector<std::uint8_t> finish()
    {
      return std::move(bytes);
    }

    std::size_t size() const
    {
      return bytes.size();
    }

  private:
    void observeScalar(
      const char *name,
      SaveStateFieldKind kind,
      std::size_t start,
      std::size_t fieldSize,
      std::uint64_t value)
    {
      observe(
        name,
        kind,
        start,
        fieldSize,
        value,
        nullptr);
    }

    void observe(
      const char *name,
      SaveStateFieldKind kind,
      std::size_t start,
      std::size_t fieldSize,
      std::uint64_t value,
      const std::uint8_t *fieldBytes)
    {
      if (observer == nullptr)
      {
        return;
      }
      SaveStateFieldObservation field;
      field.path = path.format(name);
      field.kind = kind;
      field.containerOffset = start;
      field.hasPayloadOffset =
        hasPayloadOrigin && start >= payloadOrigin;
      if (field.hasPayloadOffset)
      {
        field.payloadOffset = start - payloadOrigin;
      }
      field.size = fieldSize;
      field.scalarValue = value;
      field.bytes = fieldBytes;
      observer->observe(field);
    }

    std::vector<std::uint8_t> bytes;
    SaveStateObserver *observer = nullptr;
    SaveStatePathState path;
    std::size_t payloadOrigin = 0;
    bool hasPayloadOrigin = false;
};

class SaveStateReader
{
  public:
    explicit SaveStateReader(
      const std::vector<std::uint8_t> &input,
      SaveStateObserver *fieldObserver = nullptr) :
      bytes(input),
      observer(fieldObserver)
    {
    }

    SaveStateReader(const SaveStateReader &) = delete;
    SaveStateReader &operator=(const SaveStateReader &) = delete;
    SaveStateReader(SaveStateReader &&) = delete;
    SaveStateReader &operator=(SaveStateReader &&) = delete;

    SaveStatePathScope scope(const char *name)
    {
      return SaveStatePathScope(&path, name);
    }

    SaveStatePathScope element(std::size_t index)
    {
      return SaveStatePathScope(&path, index);
    }

    void setPayloadOrigin(std::size_t offset)
    {
      payloadOrigin = offset;
      hasPayloadOrigin = true;
    }

    std::uint8_t readU8()
    {
      clearActiveField();
      return readRawU8();
    }

    std::uint16_t readU16()
    {
      clearActiveField();
      return readRawU16();
    }

    std::uint32_t readU32()
    {
      clearActiveField();
      return readRawU32();
    }

    std::uint64_t readU64()
    {
      clearActiveField();
      return readRawU64();
    }

    std::uint8_t readFieldU8(const char *name)
    {
      const std::size_t start = beginField(name);
      const std::uint8_t value = readRawU8();
      observeScalar(
        name,
        SaveStateFieldKind::U8,
        start,
        sizeof(value),
        value);
      return value;
    }

    std::uint16_t readFieldU16(const char *name)
    {
      const std::size_t start = beginField(name);
      const std::uint16_t value = readRawU16();
      observeScalar(
        name,
        SaveStateFieldKind::U16,
        start,
        sizeof(value),
        value);
      return value;
    }

    std::uint32_t readFieldU32(const char *name)
    {
      const std::size_t start = beginField(name);
      const std::uint32_t value = readRawU32();
      observeScalar(
        name,
        SaveStateFieldKind::U32,
        start,
        sizeof(value),
        value);
      return value;
    }

    std::uint64_t readFieldU64(const char *name)
    {
      const std::size_t start = beginField(name);
      const std::uint64_t value = readRawU64();
      observeScalar(
        name,
        SaveStateFieldKind::U64,
        start,
        sizeof(value),
        value);
      return value;
    }

    bool readFieldBool(const char *name)
    {
      const std::size_t start = beginField(name);
      const std::uint8_t value = readRawU8();
      if (value > 1)
      {
        invalidCurrent("value is not a boolean");
      }
      observeScalar(
        name,
        SaveStateFieldKind::Boolean,
        start,
        1,
        value);
      return value != 0;
    }

    void requireField(
      bool condition,
      const std::string &detail) const
    {
      if (!condition)
      {
        if (activeFieldName != nullptr)
        {
          invalidCurrent(detail);
        }
        invalid(detail);
      }
    }

    void clearFieldContext()
    {
      clearActiveField();
    }

    void readFieldBytes(
      const char *name,
      std::uint8_t *values,
      std::size_t count)
    {
      const std::size_t start = beginField(name);
      requireAvailable(count);
      const std::uint8_t *source =
        count == 0 ? nullptr : bytes.data() + position;
      if (count != 0)
      {
        std::copy(source, source + count, values);
      }
      position += count;
      observe(
        name,
        SaveStateFieldKind::Bytes,
        start,
        count,
        0,
        source);
    }

    template<typename Function>
    void readFieldRange(
      const char *name,
      std::size_t count,
      Function readRange)
    {
      const std::size_t start = beginField(name);
      requireAvailable(count);
      const std::uint8_t *source =
        count == 0 ? nullptr : bytes.data() + position;
      const std::size_t expectedEnd = position + count;
      readRange();
      beginField(name);
      if (position != expectedEnd)
      {
        invalidCurrent("decoder consumed an invalid byte count");
      }
      observe(
        name,
        SaveStateFieldKind::Bytes,
        start,
        count,
        0,
        source);
    }

    bool readBool(const char *name)
    {
      const std::uint8_t value = readU8();
      if (value > 1)
      {
        invalid(std::string(name) + " is not a boolean");
      }
      return value != 0;
    }

    std::vector<std::uint8_t> readByteVector(
      std::size_t expectedSize,
      const char *name)
    {
      const std::uint32_t size = readU32();
      if (size != expectedSize)
      {
        invalid(std::string(name) + " has an invalid size");
      }
      requireAvailable(size);
      std::vector<std::uint8_t> result(
        bytes.begin() + position,
        bytes.begin() + position + size);
      position += size;
      return result;
    }

    std::vector<std::uint8_t> readFieldByteVector(
      const char *name,
      std::size_t expectedSize)
    {
      auto field = scope(name);
      const std::uint32_t fieldSize = readFieldU32("size");
      requireField(
        fieldSize == expectedSize,
        "size does not match the expected value");
      std::vector<std::uint8_t> result(fieldSize);
      readFieldBytes("bytes", result.data(), result.size());
      return result;
    }

    void expectBytes(
      const std::uint8_t *expected,
      std::size_t count,
      const char *name)
    {
      clearActiveField();
      requireAvailable(count);
      if (!std::equal(
            expected,
            expected + count,
            bytes.begin() + position))
      {
        invalid(std::string(name) + " does not match");
      }
      position += count;
    }

    void expectFieldBytes(
      const char *name,
      const std::uint8_t *expected,
      std::size_t count)
    {
      const std::size_t start = beginField(name);
      requireAvailable(count);
      const std::uint8_t *source =
        count == 0 ? nullptr : bytes.data() + position;
      if (!std::equal(
            expected,
            expected + count,
            bytes.begin() + position))
      {
        invalidCurrent("value does not match");
      }
      position += count;
      observe(
        name,
        SaveStateFieldKind::Bytes,
        start,
        count,
        0,
        source);
    }

    void requireEnd() const
    {
      if (position != bytes.size())
      {
        if (activeFieldName != nullptr)
        {
          invalidCurrent("trailing data is present");
        }
        invalid("trailing data is present");
      }
    }

    std::size_t offset() const
    {
      return position;
    }

    std::size_t size() const
    {
      return bytes.size();
    }

    std::uint64_t checksumFrom(std::size_t offset) const
    {
      if (offset > bytes.size())
      {
        invalid("checksum offset is outside the input");
      }
      std::uint64_t checksum =
        SAVE_STATE_FNV_OFFSET_BASIS;
      for (std::size_t index = offset;
           index < bytes.size();
           ++index)
      {
        checksum ^= bytes[index];
        checksum *= SAVE_STATE_FNV_PRIME;
      }
      return checksum;
    }

    static void invalid(const std::string &detail)
    {
      throw std::invalid_argument(
        "Invalid Neko save state: " + detail + ".");
    }

  private:
    std::uint8_t readRawU8()
    {
      requireAvailable(1);
      return bytes[position++];
    }

    std::uint16_t readRawU16()
    {
      const std::uint16_t low = readRawU8();
      return low |
        (static_cast<std::uint16_t>(readRawU8()) << 8);
    }

    std::uint32_t readRawU32()
    {
      const std::uint32_t low = readRawU16();
      return low |
        (static_cast<std::uint32_t>(readRawU16()) << 16);
    }

    std::uint64_t readRawU64()
    {
      const std::uint64_t low = readRawU32();
      return low |
        (static_cast<std::uint64_t>(readRawU32()) << 32);
    }

    std::size_t beginField(const char *name)
    {
      activePath = path;
      activeFieldName = name;
      activeFieldOffset = position;
      return position;
    }

    void clearActiveField()
    {
      activeFieldName = nullptr;
    }

    void observeScalar(
      const char *name,
      SaveStateFieldKind kind,
      std::size_t start,
      std::size_t fieldSize,
      std::uint64_t value)
    {
      observe(
        name,
        kind,
        start,
        fieldSize,
        value,
        nullptr);
    }

    void observe(
      const char *name,
      SaveStateFieldKind kind,
      std::size_t start,
      std::size_t fieldSize,
      std::uint64_t value,
      const std::uint8_t *fieldBytes)
    {
      if (observer == nullptr)
      {
        return;
      }
      SaveStateFieldObservation field;
      field.path = path.format(name);
      field.kind = kind;
      field.containerOffset = start;
      field.hasPayloadOffset =
        hasPayloadOrigin && start >= payloadOrigin;
      if (field.hasPayloadOffset)
      {
        field.payloadOffset = start - payloadOrigin;
      }
      field.size = fieldSize;
      field.scalarValue = value;
      field.bytes = fieldBytes;
      observer->observe(field);
    }

    [[noreturn]] void invalidCurrent(
      const std::string &detail) const
    {
      std::string location = activePath.format(activeFieldName);
      location += " at container offset ";
      location += std::to_string(activeFieldOffset);
      if (hasPayloadOrigin &&
          activeFieldOffset >= payloadOrigin)
      {
        location += ", payload offset ";
        location += std::to_string(
          activeFieldOffset - payloadOrigin);
      }
      throw std::invalid_argument(
        "Invalid Neko save state at " +
        location + ": " + detail + ".");
    }

    void requireAvailable(std::size_t count) const
    {
      if (count > bytes.size() - position)
      {
        if (activeFieldName != nullptr)
        {
          invalidCurrent("data is truncated");
        }
        invalid("data is truncated");
      }
    }

    const std::vector<std::uint8_t> &bytes;
    SaveStateObserver *observer = nullptr;
    SaveStatePathState path;
    SaveStatePathState activePath;
    const char *activeFieldName = nullptr;
    std::size_t activeFieldOffset = 0;
    std::size_t payloadOrigin = 0;
    bool hasPayloadOrigin = false;
    std::size_t position = 0;
};


template<typename Enum>
Enum readEnum(
  SaveStateReader *reader,
  std::uint8_t maximum,
  const char *name)
{
  const std::uint8_t value = reader->readFieldU8(name);
  reader->requireField(
    value <= maximum,
    "value is outside its enum");
  return static_cast<Enum>(value);
}

void require(bool condition, const std::string &detail);
void writeFPRegister(
  SaveStateWriter *writer,
  const FPRegister &value);
FPRegister readFPRegister(SaveStateReader *reader);

class NekoSaveStateCodec
{
  public:
    static std::vector<std::uint8_t> save(
      const NekoSystem &system,
      SaveStateObserver *observer = nullptr);
    static void load(
      NekoSystem *system,
      const std::vector<std::uint8_t> &state,
      SaveStateObserver *observer = nullptr);

  private:
    using PipelineLists =
      std::array<std::list<Pipeline *>, 3>;
    using PipelineListIndices =
      std::array<std::vector<std::uint8_t>, 3>;

    struct ScheduledComponentState
    {
      std::uint8_t id = 0;
      std::uint64_t period = 0;
      std::uint64_t phase = 0;
    };

    struct DecodedTopology
    {
      PipelineListIndices vu0PipelineLists;
      PipelineListIndices vu1PipelineLists;
      EECore::COP1DividerOccupancy dividerOccupancy;
      std::vector<ScheduledComponentState> schedule;
    };

    struct SystemReconciliation
    {
      PipelineLists vu0Lists;
      PipelineLists vu1Lists;
      EECore::COP1DividerOccupancy dividerOccupancy;
      std::vector<MasterClockScheduler::ScheduledComponent>
        schedule;
    };

    struct SystemLoadTransaction
    {
      NekoSystem parsed;
      DecodedTopology decodedTopology;
      SystemReconciliation reconciliation;
    };

    static_assert(
      noexcept(
        std::declval<std::vector<
          MasterClockScheduler::ScheduledComponent> &>().swap(
            std::declval<std::vector<
              MasterClockScheduler::ScheduledComponent> &>())),
      "Transactional save-state commit requires noexcept schedule swap.");

    static void writeSystem(
      SaveStateWriter *writer,
      const NekoSystem &system);
    static void readSystem(
      SaveStateReader *reader,
      NekoSystem *system,
      DecodedTopology *topology);
    static void validateSystem(
      SaveStateReader *reader,
      const NekoSystem &system);
    static SystemReconciliation reconcileSystem(
      const NekoSystem &source,
      const DecodedTopology &topology,
      NekoSystem *destination);
    static void commitSystem(
      NekoSystem *destination,
      SystemLoadTransaction *transaction);

    static void writeMasterClock(
      SaveStateWriter *writer,
      const NekoSystem &system);
    static void readMasterClock(
      SaveStateReader *reader,
      MasterClockScheduler *clock,
      std::vector<ScheduledComponentState> *schedule);
    static std::uint8_t componentID(
      const NekoSystem &system,
      const ClockedComponent *component);
    static ClockedComponent *componentForID(
      NekoSystem *system,
      std::uint8_t id);

    static void writeEECore(
      SaveStateWriter *writer,
      const EECore &core);
    static void readEECore(
      SaveStateReader *reader,
      EECore *core,
      EECore::COP1DividerOccupancy *dividerOccupancy);

    static void writeVPU(
      SaveStateWriter *writer,
      const VPU &vpu);
    static void readVPU(
      SaveStateReader *reader,
      VPU *vpu,
      PipelineListIndices *pipelineLists);
    static void commitVPU(
      VPU *destination,
      VPU *source,
      PipelineLists *lists);
    static void writePipeline(
      SaveStateWriter *writer,
      const Pipeline &pipeline);
    static void readPipeline(
      SaveStateReader *reader,
      Pipeline *pipeline);
    static void writeOrchestrator(
      SaveStateWriter *writer,
      const PipelineOrchestrator &orchestrator);
    static void readOrchestrator(
      SaveStateReader *reader,
      PipelineOrchestrator *orchestrator,
      PipelineListIndices *pipelineLists);
    static std::vector<
      MasterClockScheduler::ScheduledComponent>
      reconcileMasterClockSchedule(
        const std::vector<ScheduledComponentState> &source,
        NekoSystem *destination);
    static PipelineLists reconcilePipelineLists(
      const PipelineListIndices &source,
      PipelineOrchestrator *destination);
    static std::uint8_t pipelineIndex(
      const PipelineOrchestrator &orchestrator,
      const Pipeline *pipeline);

    static void writeVIF(
      SaveStateWriter *writer,
      const VIF &vif);
    static void readVIF(
      SaveStateReader *reader,
      VIF *vif);
    static void commitVIF(
      VIF *destination,
      VIF *source);

    static void writeGIFDecoder(
      SaveStateWriter *writer,
      const GIFDecoder &decoder);
    static void readGIFDecoder(
      SaveStateReader *reader,
      GIFDecoder *decoder);
    static void writeGIFArbiter(
      SaveStateWriter *writer,
      const GIFPathArbiter &arbiter);
    static void readGIFArbiter(
      SaveStateReader *reader,
      GIFPathArbiter *arbiter);
    static void writeGIFPath1(
      SaveStateWriter *writer,
      const GIFPath1Transfer &path);
    static void readGIFPath1(
      SaveStateReader *reader,
      GIFPath1Transfer *path);
    static void writeGIFPath3(
      SaveStateWriter *writer,
      const GIFPath3Transfer &path);
    static void readGIFPath3(
      SaveStateReader *reader,
      GIFPath3Transfer *path);

    static void writeGS(
      SaveStateWriter *writer,
      const GS &gs);
    static void readGS(
      SaveStateReader *reader,
      GS *gs);
    static void commitGS(
      GS *destination,
      GS *source);
    static void writeGSDisplay(
      SaveStateWriter *writer,
      const GSDisplay &display);
    static void readGSDisplay(
      SaveStateReader *reader,
      GSDisplay *display);

    static void writeDMAC(
      SaveStateWriter *writer,
      const GIFDMACChannel &channel,
      const DMACController &controller);
    static void readDMAC(
      SaveStateReader *reader,
      GIFDMACChannel *channel,
      DMACController *controller);
    static void writeVIF1DMAC(
      SaveStateWriter *writer,
      const VIF1DMACChannel &dmac);
    static void readVIF1DMAC(
      SaveStateReader *reader,
      VIF1DMACChannel *dmac);
    static void writeScratchpadDMAState(
      SaveStateWriter *writer,
      const NekoSystem &system);
    static void readScratchpadDMAState(
      SaveStateReader *reader,
      NekoSystem *system);
};
