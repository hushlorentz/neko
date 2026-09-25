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
constexpr std::uint32_t SAVE_STATE_VERSION = 27;
constexpr std::size_t SAVE_STATE_HEADER_SIZE = 28;
constexpr std::uint64_t SAVE_STATE_FNV_OFFSET_BASIS =
  UINT64_C(14695981039346656037);
constexpr std::uint64_t SAVE_STATE_FNV_PRIME =
  UINT64_C(1099511628211);
constexpr std::size_t SAVE_STATE_RESERVE_BYTES =
  EEMemoryMap::MAIN_MEMORY_SIZE + 5 * 1024 * 1024;

class SaveStateWriter
{
  public:
    SaveStateWriter()
    {
      bytes.reserve(SAVE_STATE_RESERVE_BYTES);
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

    void writeByteVector(
      const std::vector<std::uint8_t> &values)
    {
      writeSize(values.size());
      writeBytes(values.data(), values.size());
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
    std::vector<std::uint8_t> bytes;
};

class SaveStateReader
{
  public:
    explicit SaveStateReader(
      const std::vector<std::uint8_t> &input) :
      bytes(input)
    {
    }

    std::uint8_t readU8()
    {
      requireAvailable(1);
      return bytes[position++];
    }

    std::uint16_t readU16()
    {
      const std::uint16_t low = readU8();
      return low |
        (static_cast<std::uint16_t>(readU8()) << 8);
    }

    std::uint32_t readU32()
    {
      const std::uint32_t low = readU16();
      return low |
        (static_cast<std::uint32_t>(readU16()) << 16);
    }

    std::uint64_t readU64()
    {
      const std::uint64_t low = readU32();
      return low |
        (static_cast<std::uint64_t>(readU32()) << 32);
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

    void expectBytes(
      const std::uint8_t *expected,
      std::size_t count,
      const char *name)
    {
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

    void requireEnd() const
    {
      if (position != bytes.size())
      {
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
    void requireAvailable(std::size_t count) const
    {
      if (count > bytes.size() - position)
      {
        invalid("data is truncated");
      }
    }

    const std::vector<std::uint8_t> &bytes;
    std::size_t position = 0;
};


template<typename Enum>
Enum readEnum(
  SaveStateReader *reader,
  std::uint8_t maximum,
  const char *name)
{
  const std::uint8_t value = reader->readU8();
  if (value > maximum)
  {
    SaveStateReader::invalid(
      std::string(name) + " is outside its enum");
  }
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
      const NekoSystem &system);
    static void load(
      NekoSystem *system,
      const std::vector<std::uint8_t> &state);

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
    static void validateSystem(const NekoSystem &system);
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
};
