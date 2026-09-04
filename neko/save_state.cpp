#include "neko_system.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <list>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "vpu_flags.hpp"
#include "vpu_register_ids.hpp"

namespace
{
  constexpr std::uint8_t SAVE_STATE_MAGIC[] = {
    'N', 'E', 'K', 'O', 'S', 'T', 'A', 'T'
  };
  constexpr std::uint32_t SAVE_STATE_VERSION = 15;
  constexpr std::size_t SAVE_STATE_HEADER_SIZE = 28;
  constexpr std::uint64_t SAVE_STATE_FNV_OFFSET_BASIS =
    UINT64_C(14695981039346656037);
  constexpr std::uint64_t SAVE_STATE_FNV_PRIME =
    UINT64_C(1099511628211);
  constexpr std::size_t SAVE_STATE_RESERVE_BYTES =
    EEMemoryMap::MAIN_MEMORY_SIZE + 5 * 1024 * 1024;
  constexpr std::uint32_t MAX_VIF_PAYLOAD_WORDS =
    65536u * 4u;
  constexpr std::uint32_t MAX_VIF_UNPACK_WORDS =
    256u * 4u;
  constexpr std::uint32_t MAX_GIF_REGISTER_VALUES =
    0x7fffu * 16u;
  constexpr std::uint8_t MAX_PIPELINE_STAGE_INDEX = 64;

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

  void require(
    bool condition,
    const std::string &detail)
  {
    if (!condition)
    {
      SaveStateReader::invalid(detail);
    }
  }

  void writeFPRegister(
    SaveStateWriter *writer,
    const FPRegister &value)
  {
    writer->writeU32(value.x.bits());
    writer->writeU32(value.y.bits());
    writer->writeU32(value.z.bits());
    writer->writeU32(value.w.bits());
    writer->writeU8(value.xResultFlags);
    writer->writeU8(value.yResultFlags);
    writer->writeU8(value.zResultFlags);
    writer->writeU8(value.wResultFlags);
  }

  FPRegister readFPRegister(SaveStateReader *reader)
  {
    FPRegister value;
    value.x.setBits(reader->readU32());
    value.y.setBits(reader->readU32());
    value.z.setBits(reader->readU32());
    value.w.setBits(reader->readU32());
    value.xResultFlags = reader->readU8();
    value.yResultFlags = reader->readU8();
    value.zResultFlags = reader->readU8();
    value.wResultFlags = reader->readU8();
    require(
      (value.xResultFlags & ~0x0f) == 0 &&
      (value.yResultFlags & ~0x0f) == 0 &&
      (value.zResultFlags & ~0x0f) == 0 &&
      (value.wResultFlags & ~0x0f) == 0,
      "VU floating-point result flags are invalid");
    return value;
  }

  void writeLowerInstruction(
    SaveStateWriter *writer,
    const LowerInstruction &instruction)
  {
    writer->writeU8(
      static_cast<std::uint8_t>(instruction.unit));
    writer->writeU32(instruction.opCode);
    writer->writeU8(instruction.sourceRegister1);
    writer->writeU8(instruction.sourceRegister2);
    writer->writeU8(instruction.destinationRegister);
    writer->writeU8(instruction.integerDestinationRegister);
    writer->writeU8(instruction.destinationFieldMask);
    writer->writeU8(instruction.sourceFieldMask1);
    writer->writeU8(instruction.sourceFieldMask2);
    writer->writeU16(
      static_cast<std::uint16_t>(instruction.immediate));
    writer->writeU32(instruction.immediateBits);
  }

  LowerInstruction readLowerInstruction(
    SaveStateReader *reader)
  {
    LowerInstruction instruction;
    instruction.unit = readEnum<LowerExecutionUnit>(
      reader,
      static_cast<std::uint8_t>(LowerExecutionUnit::Branch),
      "VU lower execution unit");
    instruction.opCode = reader->readU32();
    instruction.sourceRegister1 = reader->readU8();
    instruction.sourceRegister2 = reader->readU8();
    instruction.destinationRegister = reader->readU8();
    instruction.integerDestinationRegister = reader->readU8();
    instruction.destinationFieldMask = reader->readU8();
    instruction.sourceFieldMask1 = reader->readU8();
    instruction.sourceFieldMask2 = reader->readU8();
    instruction.immediate =
      static_cast<std::int16_t>(reader->readU16());
    instruction.immediateBits = reader->readU32();
    require(
      instruction.sourceRegister1 <= VPU_REGISTER_VF31 &&
      instruction.sourceRegister2 <= VPU_REGISTER_VF31 &&
      instruction.destinationRegister <= VPU_REGISTER_VF31 &&
      instruction.integerDestinationRegister <= VPU_REGISTER_VI15,
      "VU lower instruction register is invalid");
    require(
      instruction.destinationFieldMask <= FP_REGISTER_ALL_FIELDS &&
      instruction.sourceFieldMask1 <= FP_REGISTER_ALL_FIELDS &&
      instruction.sourceFieldMask2 <= FP_REGISTER_ALL_FIELDS,
      "VU lower instruction field mask is invalid");
    return instruction;
  }

  void writeVIFCommand(
    SaveStateWriter *writer,
    const VIFCommand &command)
  {
    writer->writeU8(
      static_cast<std::uint8_t>(command.kind));
    writer->writeU8(
      static_cast<std::uint8_t>(command.unpackFormat));
    writer->writeU32(command.raw);
    writer->writeU16(command.immediate);
    writer->writeU16(command.count);
    writer->writeU16(command.address);
    writer->writeU8(command.encodedCount);
    writer->writeU8(command.command);
    writer->writeBool(command.interrupt);
    writer->writeBool(command.masked);
    writer->writeBool(command.unsignedData);
    writer->writeBool(command.addTops);
  }

  VIFCommand readVIFCommand(SaveStateReader *reader)
  {
    VIFCommand command;
    command.kind = readEnum<VIFCommandKind>(
      reader,
      static_cast<std::uint8_t>(VIFCommandKind::UNPACK),
      "VIF command kind");
    command.unpackFormat = readEnum<VIFUnpackFormat>(
      reader,
      static_cast<std::uint8_t>(VIFUnpackFormat::V4_5),
      "VIF UNPACK format");
    command.raw = reader->readU32();
    command.immediate = reader->readU16();
    command.count = reader->readU16();
    command.address = reader->readU16();
    command.encodedCount = reader->readU8();
    command.command = reader->readU8();
    command.interrupt = reader->readBool("VIF command interrupt");
    command.masked = reader->readBool("VIF command mask");
    command.unsignedData =
      reader->readBool("VIF command unsigned flag");
    command.addTops =
      reader->readBool("VIF command TOPS flag");
    return command;
  }

  void writeGIFTag(
    SaveStateWriter *writer,
    const GIFTag &tag)
  {
    writer->writeU16(tag.loopCount);
    writer->writeBool(tag.endOfPacket);
    writer->writeBool(tag.primitiveEnabled);
    writer->writeU16(tag.primitive);
    writer->writeU8(static_cast<std::uint8_t>(tag.format));
    writer->writeU8(tag.registerCount);
    writer->writeU64(tag.registers);
  }

  GIFTag readGIFTag(SaveStateReader *reader)
  {
    GIFTag tag;
    tag.loopCount = reader->readU16();
    tag.endOfPacket =
      reader->readBool("GIF tag end-of-packet flag");
    tag.primitiveEnabled =
      reader->readBool("GIF tag primitive flag");
    tag.primitive = reader->readU16();
    tag.format = readEnum<GIFDataFormat>(
      reader,
      static_cast<std::uint8_t>(GIFDataFormat::Disabled),
      "GIF data format");
    tag.registerCount = reader->readU8();
    tag.registers = reader->readU64();
    require(tag.loopCount <= 0x7fff, "GIF loop count is invalid");
    require(tag.primitive <= 0x07ff, "GIF primitive is invalid");
    require(
      tag.registerCount <= 16,
      "GIF register count is invalid");
    return tag;
  }

  void writeGIFDecoderState(
    SaveStateWriter *writer,
    const GIFDecoderState &state)
  {
    writeGIFTag(writer, state.tag);
    writer->writeBool(state.waitingForTag);
    writer->writeBool(state.activePacket);
    writer->writeU32(state.remainingQuadwords);
    writer->writeU32(state.remainingRegisterValues);
    writer->writeU16(state.currentLoop);
    writer->writeU8(state.currentRegister);
    writer->writeU32(state.qValue);
  }

  GIFDecoderState readGIFDecoderState(
    SaveStateReader *reader,
    const char *name)
  {
    GIFDecoderState state;
    state.tag = readGIFTag(reader);
    state.waitingForTag =
      reader->readBool("GIF decoder waiting flag");
    state.activePacket =
      reader->readBool("GIF decoder packet flag");
    state.remainingQuadwords = reader->readU32();
    state.remainingRegisterValues = reader->readU32();
    state.currentLoop = reader->readU16();
    state.currentRegister = reader->readU8();
    state.qValue = reader->readU32();
    require(
      state.remainingQuadwords <= MAX_GIF_REGISTER_VALUES &&
      state.remainingRegisterValues <= MAX_GIF_REGISTER_VALUES,
      std::string(name) + " remaining count is invalid");
    require(
      state.currentLoop <= state.tag.loopCount,
      std::string(name) + " loop index is invalid");
    require(
      (state.tag.registerCount == 0 &&
       state.currentRegister == 0) ||
      (state.tag.registerCount != 0 &&
       state.currentRegister < state.tag.registerCount),
      std::string(name) + " register index is invalid");
    require(
      !state.waitingForTag ||
      state.remainingQuadwords == 0,
      std::string(name) + " waiting state has remaining data");
    require(
      state.waitingForTag ||
      state.remainingQuadwords != 0,
      std::string(name) + " active primitive has no data");
    return state;
  }

  void writeGSPrimitive(
    SaveStateWriter *writer,
    const GSPrimitive &primitive)
  {
    writer->writeU8(
      static_cast<std::uint8_t>(primitive.type));
    writer->writeBool(primitive.gouraudShading);
    writer->writeBool(primitive.textureMapping);
    writer->writeBool(primitive.fogging);
    writer->writeBool(primitive.alphaBlending);
    writer->writeBool(primitive.antialiasing);
    writer->writeBool(primitive.fixedTextureCoordinates);
    writer->writeU8(primitive.context);
    writer->writeBool(primitive.fixedFragmentValue);
  }

  GSPrimitive readGSPrimitive(SaveStateReader *reader)
  {
    GSPrimitive primitive;
    primitive.type = readEnum<GSPrimitiveType>(
      reader,
      static_cast<std::uint8_t>(GSPrimitiveType::Sprite),
      "GS primitive type");
    primitive.gouraudShading =
      reader->readBool("GS Gouraud-shading flag");
    primitive.textureMapping =
      reader->readBool("GS texture-mapping flag");
    primitive.fogging = reader->readBool("GS fog flag");
    primitive.alphaBlending =
      reader->readBool("GS alpha-blending flag");
    primitive.antialiasing =
      reader->readBool("GS antialiasing flag");
    primitive.fixedTextureCoordinates =
      reader->readBool("GS fixed-texture flag");
    primitive.context = reader->readU8();
    primitive.fixedFragmentValue =
      reader->readBool("GS fixed-fragment flag");
    require(primitive.context < 2, "GS context is invalid");
    return primitive;
  }

  void writeGSColor(
    SaveStateWriter *writer,
    const GSColor &color)
  {
    writer->writeU8(color.red);
    writer->writeU8(color.green);
    writer->writeU8(color.blue);
    writer->writeU8(color.alpha);
    writer->writeU32(color.q);
  }

  GSColor readGSColor(SaveStateReader *reader)
  {
    GSColor color;
    color.red = reader->readU8();
    color.green = reader->readU8();
    color.blue = reader->readU8();
    color.alpha = reader->readU8();
    color.q = reader->readU32();
    return color;
  }

  void writeGSVertex(
    SaveStateWriter *writer,
    const GSVertexCoordinate &vertex)
  {
    writer->writeU16(vertex.x);
    writer->writeU16(vertex.y);
    writer->writeU32(vertex.z);
  }

  GSVertexCoordinate readGSVertex(
    SaveStateReader *reader)
  {
    GSVertexCoordinate vertex;
    vertex.x = reader->readU16();
    vertex.y = reader->readU16();
    vertex.z = reader->readU32();
    return vertex;
  }

  void writeGSTextureCoordinate(
    SaveStateWriter *writer,
    const GSTextureCoordinate &coordinate)
  {
    writer->writeU32(coordinate.s);
    writer->writeU32(coordinate.t);
    writer->writeU16(coordinate.u);
    writer->writeU16(coordinate.v);
  }

  GSTextureCoordinate readGSTextureCoordinate(
    SaveStateReader *reader)
  {
    GSTextureCoordinate coordinate;
    coordinate.s = reader->readU32();
    coordinate.t = reader->readU32();
    coordinate.u = reader->readU16();
    coordinate.v = reader->readU16();
    return coordinate;
  }

  void writeGSFrame(
    SaveStateWriter *writer,
    const GSFrame &frame)
  {
    writer->writeU16(frame.basePointer);
    writer->writeU8(frame.width);
    writer->writeU8(frame.pixelStorageMode);
    writer->writeU32(frame.drawingMask);
  }

  GSFrame readGSFrame(SaveStateReader *reader)
  {
    GSFrame frame;
    frame.basePointer = reader->readU16();
    frame.width = reader->readU8();
    frame.pixelStorageMode = reader->readU8();
    frame.drawingMask = reader->readU32();
    return frame;
  }

  void writeGSScissor(
    SaveStateWriter *writer,
    const GSScissor &scissor)
  {
    writer->writeU16(scissor.x0);
    writer->writeU16(scissor.x1);
    writer->writeU16(scissor.y0);
    writer->writeU16(scissor.y1);
  }

  GSScissor readGSScissor(SaveStateReader *reader)
  {
    GSScissor scissor;
    scissor.x0 = reader->readU16();
    scissor.x1 = reader->readU16();
    scissor.y0 = reader->readU16();
    scissor.y1 = reader->readU16();
    return scissor;
  }

  void writeGSXYOffset(
    SaveStateWriter *writer,
    const GSXYOffset &offset)
  {
    writer->writeU16(offset.x);
    writer->writeU16(offset.y);
  }

  GSXYOffset readGSXYOffset(SaveStateReader *reader)
  {
    GSXYOffset offset;
    offset.x = reader->readU16();
    offset.y = reader->readU16();
    return offset;
  }

  void writeGSTest(
    SaveStateWriter *writer,
    const GSTest &test)
  {
    writer->writeBool(test.alphaTestEnabled);
    writer->writeU8(test.alphaTest);
    writer->writeU8(test.alphaReference);
    writer->writeU8(test.alphaFail);
    writer->writeBool(test.destinationAlphaTestEnabled);
    writer->writeBool(test.destinationAlphaMode);
    writer->writeBool(test.depthTestEnabled);
    writer->writeU8(test.depthTest);
  }

  GSTest readGSTest(SaveStateReader *reader)
  {
    GSTest test;
    test.alphaTestEnabled =
      reader->readBool("GS alpha-test flag");
    test.alphaTest = reader->readU8();
    test.alphaReference = reader->readU8();
    test.alphaFail = reader->readU8();
    test.destinationAlphaTestEnabled =
      reader->readBool("GS destination-alpha-test flag");
    test.destinationAlphaMode =
      reader->readBool("GS destination-alpha mode");
    test.depthTestEnabled =
      reader->readBool("GS depth-test flag");
    test.depthTest = reader->readU8();
    require(
      test.alphaTest <= 7 &&
      test.alphaFail <= 3 &&
      test.depthTest <= 3,
      "GS test function is invalid");
    return test;
  }

  void writeGSAlpha(
    SaveStateWriter *writer,
    const GSAlpha &alpha)
  {
    writer->writeU8(alpha.source);
    writer->writeU8(alpha.destination);
    writer->writeU8(alpha.alpha);
    writer->writeU8(alpha.result);
    writer->writeU8(alpha.fixedAlpha);
  }

  GSAlpha readGSAlpha(SaveStateReader *reader)
  {
    GSAlpha alpha;
    alpha.source = reader->readU8();
    alpha.destination = reader->readU8();
    alpha.alpha = reader->readU8();
    alpha.result = reader->readU8();
    alpha.fixedAlpha = reader->readU8();
    require(
      alpha.source <= 3 &&
      alpha.destination <= 3 &&
      alpha.alpha <= 3 &&
      alpha.result <= 3,
      "GS alpha selection is invalid");
    return alpha;
  }

  void writeGSDepthBuffer(
    SaveStateWriter *writer,
    const GSDepthBuffer &depth)
  {
    writer->writeU16(depth.basePointer);
    writer->writeU8(depth.pixelStorageMode);
    writer->writeBool(depth.drawingMasked);
  }

  GSDepthBuffer readGSDepthBuffer(SaveStateReader *reader)
  {
    GSDepthBuffer depth;
    depth.basePointer = reader->readU16();
    depth.pixelStorageMode = reader->readU8();
    depth.drawingMasked =
      reader->readBool("GS depth-mask flag");
    return depth;
  }

  void writeGSTexture(
    SaveStateWriter *writer,
    const GSTexture &texture)
  {
    writer->writeU16(texture.basePointer);
    writer->writeU8(texture.bufferWidth);
    writer->writeU8(texture.pixelStorageMode);
    writer->writeU8(texture.widthExponent);
    writer->writeU8(texture.heightExponent);
    writer->writeBool(texture.rgba);
    writer->writeU8(texture.function);
    writer->writeU8(texture.maximumMipLevel);
    writer->writeBool(texture.magnificationLinear);
    writer->writeU8(texture.minificationFilter);
  }

  GSTexture readGSTexture(SaveStateReader *reader)
  {
    GSTexture texture;
    texture.basePointer = reader->readU16();
    texture.bufferWidth = reader->readU8();
    texture.pixelStorageMode = reader->readU8();
    texture.widthExponent = reader->readU8();
    texture.heightExponent = reader->readU8();
    texture.rgba = reader->readBool("GS texture RGBA flag");
    texture.function = reader->readU8();
    texture.maximumMipLevel = reader->readU8();
    texture.magnificationLinear =
      reader->readBool("GS texture magnification flag");
    texture.minificationFilter = reader->readU8();
    require(
      texture.widthExponent <= 15 &&
      texture.heightExponent <= 15 &&
      texture.function <= 3 &&
      texture.maximumMipLevel <= 7 &&
      texture.minificationFilter <= 7,
      "GS texture state is invalid");
    return texture;
  }

  void writeGSTextureClamp(
    SaveStateWriter *writer,
    const GSTextureClamp &clamp)
  {
    writer->writeU8(
      static_cast<std::uint8_t>(clamp.horizontal));
    writer->writeU8(
      static_cast<std::uint8_t>(clamp.vertical));
    writer->writeU16(clamp.minimumU);
    writer->writeU16(clamp.maximumU);
    writer->writeU16(clamp.minimumV);
    writer->writeU16(clamp.maximumV);
  }

  GSTextureClamp readGSTextureClamp(
    SaveStateReader *reader)
  {
    GSTextureClamp clamp;
    clamp.horizontal = readEnum<GSTextureWrapMode>(
      reader,
      static_cast<std::uint8_t>(
        GSTextureWrapMode::RegionRepeat),
      "GS horizontal texture wrap mode");
    clamp.vertical = readEnum<GSTextureWrapMode>(
      reader,
      static_cast<std::uint8_t>(
        GSTextureWrapMode::RegionRepeat),
      "GS vertical texture wrap mode");
    clamp.minimumU = reader->readU16();
    clamp.maximumU = reader->readU16();
    clamp.minimumV = reader->readU16();
    clamp.maximumV = reader->readU16();
    return clamp;
  }

  void writeGSContext(
    SaveStateWriter *writer,
    const GSContext &context)
  {
    writeGSFrame(writer, context.frame);
    writeGSScissor(writer, context.scissor);
    writeGSXYOffset(writer, context.offset);
    writeGSTest(writer, context.test);
    writeGSAlpha(writer, context.alpha);
    writer->writeBool(context.forceAlphaBit);
    writeGSDepthBuffer(writer, context.depthBuffer);
    writeGSTexture(writer, context.texture);
    writeGSTextureClamp(writer, context.textureClamp);
  }

  GSContext readGSContext(SaveStateReader *reader)
  {
    GSContext context;
    context.frame = readGSFrame(reader);
    context.scissor = readGSScissor(reader);
    context.offset = readGSXYOffset(reader);
    context.test = readGSTest(reader);
    context.alpha = readGSAlpha(reader);
    context.forceAlphaBit =
      reader->readBool("GS force-alpha flag");
    context.depthBuffer = readGSDepthBuffer(reader);
    context.texture = readGSTexture(reader);
    context.textureClamp = readGSTextureClamp(reader);
    return context;
  }

  void writeGSImageTransfer(
    SaveStateWriter *writer,
    const GSImageTransfer &transfer)
  {
    writer->writeU16(transfer.sourceBasePointer);
    writer->writeU8(transfer.sourceBufferWidth);
    writer->writeU8(transfer.sourcePixelStorageMode);
    writer->writeU16(transfer.destinationBasePointer);
    writer->writeU8(transfer.destinationBufferWidth);
    writer->writeU8(transfer.destinationPixelStorageMode);
    writer->writeU16(transfer.sourceX);
    writer->writeU16(transfer.sourceY);
    writer->writeU16(transfer.destinationX);
    writer->writeU16(transfer.destinationY);
    writer->writeU16(transfer.width);
    writer->writeU16(transfer.height);
    writer->writeU32(transfer.transferredPixels);
    writer->writeU8(
      static_cast<std::uint8_t>(transfer.direction));
    writer->writeBool(transfer.active);
  }

  GSImageTransfer readGSImageTransfer(
    SaveStateReader *reader)
  {
    GSImageTransfer transfer;
    transfer.sourceBasePointer = reader->readU16();
    transfer.sourceBufferWidth = reader->readU8();
    transfer.sourcePixelStorageMode = reader->readU8();
    transfer.destinationBasePointer = reader->readU16();
    transfer.destinationBufferWidth = reader->readU8();
    transfer.destinationPixelStorageMode = reader->readU8();
    transfer.sourceX = reader->readU16();
    transfer.sourceY = reader->readU16();
    transfer.destinationX = reader->readU16();
    transfer.destinationY = reader->readU16();
    transfer.width = reader->readU16();
    transfer.height = reader->readU16();
    transfer.transferredPixels = reader->readU32();
    transfer.direction = readEnum<GSImageTransferDirection>(
      reader,
      static_cast<std::uint8_t>(
        GSImageTransferDirection::Deactivated),
      "GS image-transfer direction");
    transfer.active =
      reader->readBool("GS image-transfer active flag");
    const std::uint64_t pixelCount =
      static_cast<std::uint64_t>(transfer.width) *
      transfer.height;
    require(
      transfer.transferredPixels <= pixelCount,
      "GS image-transfer progress is invalid");
    return transfer;
  }
}

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

    static void writeSystem(
      SaveStateWriter *writer,
      const NekoSystem &system);
    static void readSystem(
      SaveStateReader *reader,
      NekoSystem *system);
    static void validateSystem(const NekoSystem &system);
    static void commitSystem(
      NekoSystem *destination,
      NekoSystem *source,
      PipelineLists *vu0Lists,
      PipelineLists *vu1Lists,
      std::vector<MasterClockScheduler::ScheduledComponent>
        *schedule);

    static void writeMasterClock(
      SaveStateWriter *writer,
      const NekoSystem &system);
    static void readMasterClock(
      SaveStateReader *reader,
      NekoSystem *system);
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
      EECore *core);

    static void writeVPU(
      SaveStateWriter *writer,
      const VPU &vpu);
    static void readVPU(
      SaveStateReader *reader,
      VPU *vpu);
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
      PipelineOrchestrator *orchestrator);
    static PipelineLists makePipelineLists(
      const PipelineOrchestrator &source,
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
      const GIFDMACChannel &dmac);
    static void readDMAC(
      SaveStateReader *reader,
      GIFDMACChannel *dmac);
    static void writeVIF1DMAC(
      SaveStateWriter *writer,
      const VIF1DMACChannel &dmac);
    static void readVIF1DMAC(
      SaveStateReader *reader,
      VIF1DMACChannel *dmac);
};

std::vector<std::uint8_t> NekoSaveStateCodec::save(
  const NekoSystem &system)
{
  SaveStateWriter writer;
  writer.writeBytes(
    SAVE_STATE_MAGIC,
    sizeof(SAVE_STATE_MAGIC));
  writer.writeU32(SAVE_STATE_VERSION);
  const std::size_t payloadLengthOffset = writer.size();
  writer.writeU64(0);
  const std::size_t checksumOffset = writer.size();
  writer.writeU64(0);
  writeSystem(&writer, system);
  writer.patchU64(
    payloadLengthOffset,
    writer.size() - SAVE_STATE_HEADER_SIZE);
  writer.patchU64(
    checksumOffset,
    writer.checksumFrom(SAVE_STATE_HEADER_SIZE));
  return writer.finish();
}

void NekoSaveStateCodec::load(
  NekoSystem *system,
  const std::vector<std::uint8_t> &state)
{
  SaveStateReader reader(state);
  reader.expectBytes(
    SAVE_STATE_MAGIC,
    sizeof(SAVE_STATE_MAGIC),
    "magic");
  const std::uint32_t version = reader.readU32();
  if (version != SAVE_STATE_VERSION)
  {
    SaveStateReader::invalid("version is incompatible");
  }
  const std::uint64_t payloadLength = reader.readU64();
  const std::uint64_t expectedChecksum = reader.readU64();
  require(
    payloadLength == reader.size() - reader.offset(),
    "payload length does not match the input");
  require(
    expectedChecksum ==
      reader.checksumFrom(reader.offset()),
    "payload checksum does not match");

  NekoSystem parsed;
  readSystem(&reader, &parsed);
  reader.requireEnd();
  validateSystem(parsed);

  PipelineLists vu0Lists = makePipelineLists(
    parsed.vu0Component.orchestrator,
    &system->vu0Component.orchestrator);
  PipelineLists vu1Lists = makePipelineLists(
    parsed.vu1Component.orchestrator,
    &system->vu1Component.orchestrator);
  std::vector<MasterClockScheduler::ScheduledComponent>
    schedule;
  schedule.reserve(parsed.masterClock.components.size());
  for (const auto &scheduled : parsed.masterClock.components)
  {
    schedule.push_back({
      componentForID(
        system,
        componentID(parsed, scheduled.component)),
      scheduled.period,
      scheduled.phase
    });
  }

  commitSystem(
    system,
    &parsed,
    &vu0Lists,
    &vu1Lists,
    &schedule);
}

void NekoSaveStateCodec::writeSystem(
  SaveStateWriter *writer,
  const NekoSystem &system)
{
  writer->writeU16(system.inputState.buttons);
  writer->writeU8(system.inputState.leftStickX);
  writer->writeU8(system.inputState.leftStickY);
  writer->writeU8(system.inputState.rightStickX);
  writer->writeU8(system.inputState.rightStickY);

  writeMasterClock(writer, system);
  writeEECore(writer, system.eeCoreComponent);
  writer->writeU32(
    system.interruptControllerComponent.statusRegister);
  writer->writeU32(
    system.interruptControllerComponent.maskRegister);
  writer->writeByteVector(system.eeBusComponent.mainMemory);
  writeVPU(writer, system.vu0Component);
  writeVPU(writer, system.vu1Component);
  writeVIF(writer, system.vif0Component);
  writeVIF(writer, system.vif1Component);
  writeGIFDecoder(writer, system.gifDecoderComponent);
  writeGIFArbiter(writer, system.gifPathArbiterComponent);
  writeGIFPath1(writer, system.gifPath1Component);
  writeGIFPath3(writer, system.gifPath3Component);
  writeGS(writer, system.gsComponent);
  writeDMAC(writer, system.gifDMACComponent);
  writeVIF1DMAC(writer, system.vif1DMACComponent);
  writeGSDisplay(writer, system.gsDisplayComponent);
}

void NekoSaveStateCodec::readSystem(
  SaveStateReader *reader,
  NekoSystem *system)
{
  system->inputState.buttons = reader->readU16();
  system->inputState.leftStickX = reader->readU8();
  system->inputState.leftStickY = reader->readU8();
  system->inputState.rightStickX = reader->readU8();
  system->inputState.rightStickY = reader->readU8();

  readMasterClock(reader, system);
  readEECore(reader, &system->eeCoreComponent);
  system->interruptControllerComponent.statusRegister =
    reader->readU32();
  system->interruptControllerComponent.maskRegister =
    reader->readU32();
  system->eeBusComponent.mainMemory =
    reader->readByteVector(
      EEMemoryMap::MAIN_MEMORY_SIZE,
      "EE main memory");
  readVPU(reader, &system->vu0Component);
  readVPU(reader, &system->vu1Component);
  readVIF(reader, &system->vif0Component);
  readVIF(reader, &system->vif1Component);
  readGIFDecoder(reader, &system->gifDecoderComponent);
  readGIFArbiter(reader, &system->gifPathArbiterComponent);
  readGIFPath1(reader, &system->gifPath1Component);
  readGIFPath3(reader, &system->gifPath3Component);
  readGS(reader, &system->gsComponent);
  readDMAC(reader, &system->gifDMACComponent);
  readVIF1DMAC(reader, &system->vif1DMACComponent);
  readGSDisplay(reader, &system->gsDisplayComponent);
}

void NekoSaveStateCodec::validateSystem(
  const NekoSystem &system)
{
  require(
    system.vu0Component.type == VPUType::VU0 &&
    system.vu1Component.type == VPUType::VU1,
    "VU identities do not match the system wiring");
  require(
    system.vif0Component.type == VIFType::VIF0 &&
    system.vif1Component.type == VIFType::VIF1,
    "VIF identities do not match the system wiring");
  require(
    system.vif1Component.path3Mask ==
      system.gifPathArbiterComponent.vifPath3Mask,
    "VIF1 and GIF PATH3 mask state disagree");
  if (system.gifPathArbiterComponent.interruptedPath3)
  {
    const GIFDecoderState &suspended =
      system.gifPathArbiterComponent.suspendedPath3State;
    require(
      suspended.activePacket &&
      !suspended.waitingForTag &&
      suspended.tag.format == GIFDataFormat::Image,
      "suspended PATH3 state is invalid");
    require(
      system.gifPathArbiterComponent.queuedPaths[2],
      "interrupted PATH3 is not queued");
  }
}

void NekoSaveStateCodec::commitSystem(
  NekoSystem *destination,
  NekoSystem *source,
  PipelineLists *vu0Lists,
  PipelineLists *vu1Lists,
  std::vector<MasterClockScheduler::ScheduledComponent>
    *schedule)
{
  destination->inputState = source->inputState;
  destination->masterClock.masterCycle =
    source->masterClock.masterCycle;
  destination->masterClock.components.swap(*schedule);
  destination->eeCoreComponent.generalRegisters =
    source->eeCoreComponent.generalRegisters;
  destination->eeCoreComponent.floatingPointRegisters =
    source->eeCoreComponent.floatingPointRegisters;
  destination->eeCoreComponent.floatingPointAccumulatorRegister =
    source->eeCoreComponent.floatingPointAccumulatorRegister;
  destination->eeCoreComponent.cop1StatusRegister =
    source->eeCoreComponent.cop1StatusRegister;
  destination->eeCoreComponent.pc =
    source->eeCoreComponent.pc;
  destination->eeCoreComponent.hiRegister =
    source->eeCoreComponent.hiRegister;
  destination->eeCoreComponent.loRegister =
    source->eeCoreComponent.loRegister;
  destination->eeCoreComponent.hi1Register =
    source->eeCoreComponent.hi1Register;
  destination->eeCoreComponent.lo1Register =
    source->eeCoreComponent.lo1Register;
  destination->eeCoreComponent.saRegister =
    source->eeCoreComponent.saRegister;
  destination->eeCoreComponent.cop0BadVAddr =
    source->eeCoreComponent.cop0BadVAddr;
  destination->eeCoreComponent.cop0Count =
    source->eeCoreComponent.cop0Count;
  destination->eeCoreComponent.cop0Compare =
    source->eeCoreComponent.cop0Compare;
  destination->eeCoreComponent.cop0Status =
    source->eeCoreComponent.cop0Status;
  destination->eeCoreComponent.cop0Cause =
    source->eeCoreComponent.cop0Cause;
  destination->eeCoreComponent.cop0EPC =
    source->eeCoreComponent.cop0EPC;
  destination->eeCoreComponent.cop0ErrorEPC =
    source->eeCoreComponent.cop0ErrorEPC;
  destination->eeCoreComponent.exception =
    source->eeCoreComponent.exception;
  destination->eeCoreComponent.faultAddress =
    source->eeCoreComponent.faultAddress;
  destination->eeCoreComponent.state =
    source->eeCoreComponent.state;
  destination->eeCoreComponent.haltReason =
    source->eeCoreComponent.haltReason;
  destination->eeCoreComponent.cycles =
    source->eeCoreComponent.cycles;
  destination->eeCoreComponent.lastInstructionValid =
    source->eeCoreComponent.lastInstructionValid;
  destination->eeCoreComponent.lastAddress =
    source->eeCoreComponent.lastAddress;
  destination->eeCoreComponent.lastDecodedInstruction =
    source->eeCoreComponent.lastDecodedInstruction;
  destination->eeCoreComponent.rejectedInstructionValue =
    source->eeCoreComponent.rejectedInstructionValue;
  destination->eeCoreComponent.pendingMac0 =
    source->eeCoreComponent.pendingMac0;
  destination->eeCoreComponent.pendingMac1 =
    source->eeCoreComponent.pendingMac1;
  destination->eeCoreComponent.pendingCOP1Load =
    source->eeCoreComponent.pendingCOP1Load;
  destination->eeCoreComponent.recentShiftAmountAccesses =
    source->eeCoreComponent.recentShiftAmountAccesses;
  destination->eeCoreComponent.recentShiftAmountReads =
    source->eeCoreComponent.recentShiftAmountReads;
  destination->eeCoreComponent.branchDelayPending =
    source->eeCoreComponent.branchDelayPending;
  destination->eeCoreComponent.branchDelayTarget =
    source->eeCoreComponent.branchDelayTarget;
  destination->eeCoreComponent.branchInstructionAddress =
    source->eeCoreComponent.branchInstructionAddress;
  destination->eeCoreComponent.branchDelayFromLikely =
    source->eeCoreComponent.branchDelayFromLikely;
  destination->eeCoreComponent.instructionRetiredThisCycle = false;
  destination->eeCoreComponent.exceptionEnteredThisCycle = false;
  destination->interruptControllerComponent.statusRegister =
    source->interruptControllerComponent.statusRegister;
  destination->interruptControllerComponent.maskRegister =
    source->interruptControllerComponent.maskRegister;
  destination->eeBusComponent.mainMemory.swap(
    source->eeBusComponent.mainMemory);
  commitVPU(
    &destination->vu0Component,
    &source->vu0Component,
    vu0Lists);
  commitVPU(
    &destination->vu1Component,
    &source->vu1Component,
    vu1Lists);
  commitVIF(
    &destination->vif0Component,
    &source->vif0Component);
  commitVIF(
    &destination->vif1Component,
    &source->vif1Component);

  GIFDecoder &decoder = destination->gifDecoderComponent;
  const GIFDecoder &sourceDecoder = source->gifDecoderComponent;
  decoder.tag = sourceDecoder.tag;
  decoder.waitingForTag = sourceDecoder.waitingForTag;
  decoder.activePacket = sourceDecoder.activePacket;
  decoder.remainingQuadwords =
    sourceDecoder.remainingQuadwords;
  decoder.remainingRegisterValues =
    sourceDecoder.remainingRegisterValues;
  decoder.currentLoop = sourceDecoder.currentLoop;
  decoder.currentRegister = sourceDecoder.currentRegister;
  decoder.qValue = sourceDecoder.qValue;

  GIFPathArbiter &arbiter =
    destination->gifPathArbiterComponent;
  const GIFPathArbiter &sourceArbiter =
    source->gifPathArbiterComponent;
  arbiter.currentPath = sourceArbiter.currentPath;
  arbiter.queuedPaths = sourceArbiter.queuedPaths;
  arbiter.vifPath3Mask = sourceArbiter.vifPath3Mask;
  arbiter.modePath3Mask = sourceArbiter.modePath3Mask;
  arbiter.intermittentPath3 =
    sourceArbiter.intermittentPath3;
  arbiter.timedTransfers = sourceArbiter.timedTransfers;
  arbiter.interruptedPath3 =
    sourceArbiter.interruptedPath3;
  arbiter.queuedPath2CanInterruptPath3 =
    sourceArbiter.queuedPath2CanInterruptPath3;
  arbiter.path3ImageSliceQuadwords =
    sourceArbiter.path3ImageSliceQuadwords;
  arbiter.path3Count = sourceArbiter.path3Count;
  arbiter.path3Tag = sourceArbiter.path3Tag;
  arbiter.remainingIdleCycles =
    sourceArbiter.remainingIdleCycles;
  arbiter.suspendedPath3State =
    sourceArbiter.suspendedPath3State;

  GIFPath1Transfer &path1 = destination->gifPath1Component;
  path1.active = source->gifPath1Component.active;
  path1.qwordAddress =
    source->gifPath1Component.qwordAddress;
  path1.transferredQuadwords =
    source->gifPath1Component.transferredQuadwords;

  GIFPath3Transfer &path3 = destination->gifPath3Component;
  path3.submissionAttempts =
    source->gifPath3Component.submissionAttempts;
  path3.transferredQuadwords =
    source->gifPath3Component.transferredQuadwords;
  path3.completedPackets =
    source->gifPath3Component.completedPackets;
  path3.guestFIFO = source->gifPath3Component.guestFIFO;

  commitGS(
    &destination->gsComponent,
    &source->gsComponent);

  GIFDMACChannel &dmac = destination->gifDMACComponent;
  const GIFDMACChannel &sourceDMAC =
    source->gifDMACComponent;
  dmac.channelControlRegister =
    sourceDMAC.channelControlRegister;
  dmac.memoryAddressRegister =
    sourceDMAC.memoryAddressRegister;
  dmac.quadwordCountRegister =
    sourceDMAC.quadwordCountRegister;
  dmac.tagAddressRegister =
    sourceDMAC.tagAddressRegister;
  dmac.addressStackRegisters =
    sourceDMAC.addressStackRegisters;
  dmac.globalControlRegister =
    sourceDMAC.globalControlRegister;
  dmac.statusRegister = sourceDMAC.statusRegister;
  dmac.statusMaskRegister = sourceDMAC.statusMaskRegister;
  dmac.terminateAfterPacket =
    sourceDMAC.terminateAfterPacket;
  dmac.path3Stalled = sourceDMAC.path3Stalled;
  dmac.addressStackDepth = sourceDMAC.addressStackDepth;
  dmac.transferredQuadwords =
    sourceDMAC.transferredQuadwords;

  VIF1DMACChannel &vif1DMAC =
    destination->vif1DMACComponent;
  const VIF1DMACChannel &sourceVIF1DMAC =
    source->vif1DMACComponent;
  vif1DMAC.channelControlRegister =
    sourceVIF1DMAC.channelControlRegister;
  vif1DMAC.memoryAddressRegister =
    sourceVIF1DMAC.memoryAddressRegister;
  vif1DMAC.quadwordCountRegister =
    sourceVIF1DMAC.quadwordCountRegister;
  vif1DMAC.tagAddressRegister =
    sourceVIF1DMAC.tagAddressRegister;
  vif1DMAC.addressStackRegisters =
    sourceVIF1DMAC.addressStackRegisters;
  vif1DMAC.terminateAfterPacket =
    sourceVIF1DMAC.terminateAfterPacket;
  vif1DMAC.vif1Stalled = sourceVIF1DMAC.vif1Stalled;
  vif1DMAC.addressStackDepth =
    sourceVIF1DMAC.addressStackDepth;
  vif1DMAC.transferredQuadwords =
    sourceVIF1DMAC.transferredQuadwords;

  GSDisplay &display = destination->gsDisplayComponent;
  const GSDisplay &sourceDisplay =
    source->gsDisplayComponent;
  display.circuits = sourceDisplay.circuits;
  display.videoTiming = sourceDisplay.videoTiming;
  display.modeRegister = sourceDisplay.modeRegister;
  display.syncModeRegister =
    sourceDisplay.syncModeRegister;
  display.backgroundColor = sourceDisplay.backgroundColor;
  display.interruptMaskRegister =
    sourceDisplay.interruptMaskRegister;
  display.cycleInFrame = sourceDisplay.cycleInFrame;
  display.frameBoundaries = sourceDisplay.frameBoundaries;
  display.verticalBlank = sourceDisplay.verticalBlank;
  display.oddField = sourceDisplay.oddField;
  display.vsyncInterrupt = sourceDisplay.vsyncInterrupt;
  display.verticalBlankStarted =
    sourceDisplay.verticalBlankStarted;
  display.verticalBlankEnded =
    sourceDisplay.verticalBlankEnded;
}

void NekoSaveStateCodec::writeMasterClock(
  SaveStateWriter *writer,
  const NekoSystem &system)
{
  writer->writeU64(system.masterClock.masterCycle);
  writer->writeSize(system.masterClock.components.size());
  for (const auto &scheduled : system.masterClock.components)
  {
    writer->writeU8(componentID(
      system,
      scheduled.component));
    writer->writeU64(scheduled.period);
    writer->writeU64(scheduled.phase);
  }
}

void NekoSaveStateCodec::readMasterClock(
  SaveStateReader *reader,
  NekoSystem *system)
{
  system->masterClock.masterCycle = reader->readU64();
  const std::uint32_t count = reader->readU32();
  require(count <= 7, "master-clock component count is invalid");
  std::array<bool, 8> used = {};
  std::vector<MasterClockScheduler::ScheduledComponent>
    components;
  components.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index)
  {
    const std::uint8_t id = reader->readU8();
    require(id >= 1 && id <= 7, "clock component ID is invalid");
    require(!used[id], "clock component ID is duplicated");
    used[id] = true;
    const std::uint64_t period = reader->readU64();
    const std::uint64_t phase = reader->readU64();
    require(period != 0, "clock period is zero");
    require(phase < period, "clock phase is outside its period");
    components.push_back({
      componentForID(system, id),
      period,
      phase
    });
  }
  system->masterClock.components.swap(components);
}

std::uint8_t NekoSaveStateCodec::componentID(
  const NekoSystem &system,
  const ClockedComponent *component)
{
  if (component == &system.vu0Component)
  {
    return 1;
  }
  if (component == &system.vu1Component)
  {
    return 2;
  }
  if (component == &system.gifPathArbiterComponent)
  {
    return 3;
  }
  if (component == &system.gifDMACComponent)
  {
    return 4;
  }
  if (component == &system.gsDisplayComponent)
  {
    return 5;
  }
  if (component == &system.eeCoreComponent)
  {
    return 6;
  }
  if (component == &system.vif1DMACComponent)
  {
    return 7;
  }
  throw std::runtime_error(
    "Cannot save a host-owned master-clock component.");
}

ClockedComponent *NekoSaveStateCodec::componentForID(
  NekoSystem *system,
  std::uint8_t id)
{
  switch (id)
  {
    case 1:
      return &system->vu0Component;
    case 2:
      return &system->vu1Component;
    case 3:
      return &system->gifPathArbiterComponent;
    case 4:
      return &system->gifDMACComponent;
    case 5:
      return &system->gsDisplayComponent;
    case 6:
      return &system->eeCoreComponent;
    case 7:
      return &system->vif1DMACComponent;
    default:
      SaveStateReader::invalid("clock component ID is invalid");
  }
  return nullptr;
}

void NekoSaveStateCodec::writeEECore(
  SaveStateWriter *writer,
  const EECore &core)
{
  for (const EERegister128 &value : core.generalRegisters)
  {
    writer->writeU64(value.low);
    writer->writeU64(value.high);
  }
  for (const std::uint32_t value : core.floatingPointRegisters)
  {
    writer->writeU32(value);
  }
  writer->writeU32(core.floatingPointAccumulatorRegister);
  writer->writeU32(core.cop1StatusRegister);
  writer->writeU32(core.pc);
  writer->writeU64(core.hiRegister);
  writer->writeU64(core.loRegister);
  writer->writeU64(core.hi1Register);
  writer->writeU64(core.lo1Register);
  writer->writeU32(core.saRegister);
  writer->writeU32(core.cop0BadVAddr);
  writer->writeU32(core.cop0Count);
  writer->writeU32(core.cop0Compare);
  writer->writeU32(core.cop0Status);
  writer->writeU32(core.cop0Cause);
  writer->writeU32(core.cop0EPC);
  writer->writeU32(core.cop0ErrorEPC);
  writer->writeU8(
    static_cast<std::uint8_t>(core.exception));
  writer->writeU32(core.faultAddress);
  writer->writeU8(
    static_cast<std::uint8_t>(core.state));
  writer->writeU8(
    static_cast<std::uint8_t>(core.haltReason));
  writer->writeU64(core.cycles);
  writer->writeBool(core.lastInstructionValid);
  writer->writeU32(core.lastAddress);
  writer->writeU32(core.lastDecodedInstruction.raw);
  writer->writeU32(core.rejectedInstructionValue);
  const auto writePending =
    [writer](const EECore::PendingMultiplyDivide &operation)
    {
      writer->writeBool(operation.active);
      writer->writeU8(operation.remainingCycles);
      writer->writeU64(operation.hiResult);
      writer->writeU64(operation.loResult);
      writer->writeBool(operation.writeGeneralRegister);
      writer->writeU8(operation.generalRegister);
      writer->writeU64(operation.generalRegisterResult);
    };
  writePending(core.pendingMac0);
  writePending(core.pendingMac1);
  writer->writeBool(core.pendingCOP1Load.active);
  writer->writeU8(core.pendingCOP1Load.registerIndex);
  writer->writeU32(core.pendingCOP1Load.value);
  writer->writeU8(core.recentShiftAmountAccesses);
  writer->writeU8(core.recentShiftAmountReads);
  writer->writeBool(core.branchDelayPending);
  writer->writeU32(core.branchDelayTarget);
  writer->writeU32(core.branchInstructionAddress);
  writer->writeBool(core.branchDelayFromLikely);
}

void NekoSaveStateCodec::readEECore(
  SaveStateReader *reader,
  EECore *core)
{
  for (EERegister128 &value : core->generalRegisters)
  {
    value.low = reader->readU64();
    value.high = reader->readU64();
  }
  for (std::uint32_t &value : core->floatingPointRegisters)
  {
    value = reader->readU32();
  }
  core->floatingPointAccumulatorRegister = reader->readU32();
  core->cop1StatusRegister = reader->readU32();
  core->pc = reader->readU32();
  core->hiRegister = reader->readU64();
  core->loRegister = reader->readU64();
  core->hi1Register = reader->readU64();
  core->lo1Register = reader->readU64();
  core->saRegister = reader->readU32();
  core->cop0BadVAddr = reader->readU32();
  core->cop0Count = reader->readU32();
  core->cop0Compare = reader->readU32();
  core->cop0Status = reader->readU32();
  core->cop0Cause = reader->readU32();
  core->cop0EPC = reader->readU32();
  core->cop0ErrorEPC = reader->readU32();
  core->exception = readEnum<EEException>(
    reader,
    static_cast<std::uint8_t>(
      EEException::CoprocessorUnusable),
    "EE exception");
  core->faultAddress = reader->readU32();
  core->state = readEnum<EEExecutionState>(
    reader,
    static_cast<std::uint8_t>(
      EEExecutionState::Running),
    "EE execution state");
  core->haltReason = readEnum<EEStopReason>(
    reader,
    static_cast<std::uint8_t>(
      EEStopReason::UndefinedOperation),
    "EE stop reason");
  core->cycles = reader->readU64();
  core->lastInstructionValid =
    reader->readBool("EE last instruction flag");
  core->lastAddress = reader->readU32();
  const std::uint32_t lastInstruction = reader->readU32();
  core->rejectedInstructionValue = reader->readU32();
  const auto readPending =
    [reader](
      EECore::PendingMultiplyDivide *operation,
      const char *label)
    {
      operation->active = reader->readBool(label);
      operation->remainingCycles = reader->readU8();
      operation->hiResult = reader->readU64();
      operation->loResult = reader->readU64();
      operation->writeGeneralRegister =
        reader->readBool(label);
      operation->generalRegister = reader->readU8();
      operation->generalRegisterResult = reader->readU64();
      require(
        !operation->active ||
          (operation->remainingCycles >= 1 &&
           operation->remainingCycles <= 37),
        "EE pending multiply/divide latency is invalid");
      require(
        operation->generalRegister <
          EECore::GENERAL_REGISTER_COUNT,
        "EE pending multiply/divide register is invalid");
      require(
        operation->writeGeneralRegister ||
          operation->generalRegister == 0,
        "EE pending divide contains a destination register");
    };
  readPending(
    &core->pendingMac0,
    "EE MAC0 pending flag");
  readPending(
    &core->pendingMac1,
    "EE MAC1 pending flag");
  core->pendingCOP1Load.active =
    reader->readBool("EE COP1 pending-load flag");
  core->pendingCOP1Load.registerIndex = reader->readU8();
  core->pendingCOP1Load.value = reader->readU32();
  require(
    core->pendingCOP1Load.registerIndex <
      EECore::FLOATING_POINT_REGISTER_COUNT,
    "EE pending COP1 load register is invalid");
  require(
    core->pendingCOP1Load.active ||
      (core->pendingCOP1Load.registerIndex == 0 &&
       core->pendingCOP1Load.value == 0),
    "EE inactive COP1 load contains state");
  core->recentShiftAmountAccesses = reader->readU8();
  core->recentShiftAmountReads = reader->readU8();
  require(
    (core->recentShiftAmountAccesses & 0xf8) == 0 &&
      (core->recentShiftAmountReads & 0xf8) == 0,
    "EE shift-amount ordering history is invalid");
  require(
    (core->recentShiftAmountReads &
      ~core->recentShiftAmountAccesses) == 0,
    "EE shift-amount read history is inconsistent");
  core->branchDelayPending =
    reader->readBool("EE branch delay flag");
  core->branchDelayTarget = reader->readU32();
  core->branchInstructionAddress = reader->readU32();
  core->branchDelayFromLikely =
    reader->readBool("EE branch-likely delay flag");
  core->lastDecodedInstruction = {};
  if (core->lastInstructionValid)
  {
    core->lastDecodedInstruction =
      decodeEEInstruction(lastInstruction);
  }

  require(
    core->generalRegisters[0] == EERegister128{},
    "EE general-purpose register zero is not immutable");
  require(
    (core->cop1StatusRegister &
      ~EECOP1Control::STATUS_WRITABLE_MASK) == 0,
    "EE FCR31 contains non-writable bits");
  require(
    core->exception != EEException::None ||
      core->faultAddress == 0,
    "EE exception address is present without an exception");
  require(
    core->state == EEExecutionState::Running ||
      core->haltReason != EEStopReason::None ||
      core->cycles == 0,
    "halted EE state has no stop reason");
  require(
    core->state != EEExecutionState::Running ||
      core->haltReason == EEStopReason::None,
    "running EE state has a stop reason");
  require(
    core->lastInstructionValid ||
      (core->lastAddress == 0 && lastInstruction == 0),
    "EE invalid last instruction contains state");
  require(
    !(core->pendingMac0.active && core->pendingMac1.active),
    "EE reference core has concurrent multiply/divide state");
  require(
    !core->pendingCOP1Load.active ||
      (core->lastInstructionValid &&
       core->lastDecodedInstruction.operation ==
         EEOperation::LoadWordToCOP1 &&
       core->pendingCOP1Load.registerIndex ==
         core->lastDecodedInstruction.targetRegister),
    "EE pending COP1 load state is inconsistent");
  require(
    core->branchDelayPending ||
      (core->branchDelayTarget == 0 &&
       core->branchInstructionAddress == 0 &&
       !core->branchDelayFromLikely),
    "EE inactive branch delay contains state");
  require(
    !core->branchDelayPending ||
      ((core->branchInstructionAddress & 3) == 0 &&
       core->pc == core->branchInstructionAddress + 4 &&
       core->lastInstructionValid &&
       core->lastAddress == core->branchInstructionAddress &&
       isEEBranchOperation(
         core->lastDecodedInstruction.operation) &&
       core->branchDelayFromLikely ==
         isEEBranchLikelyOperation(
           core->lastDecodedInstruction.operation)),
    "EE pending branch delay state is inconsistent");
}

void NekoSaveStateCodec::writeVPU(
  SaveStateWriter *writer,
  const VPU &vpu)
{
  writer->writeU8(static_cast<std::uint8_t>(vpu.type));
  writer->writeByteVector(vpu.microMem);
  writer->writeByteVector(vpu.vuMem);
  writer->writeU8(vpu.state);
  writer->writeU32(vpu.cycles);
  writer->writeU8(vpu.mode);
  writer->writeBool(vpu.macroIssueNeedsAdvance);
  writer->writeBool(vpu.macroTransferStallPending);
  writer->writeU16(vpu.microMemPC);
  writer->writeU16(vpu.terminationPositionCounter);
  writer->writeBool(vpu.terminationPositionValid);
  writer->writeBool(vpu.endDelaySlotPending);
  writer->writeBool(vpu.branchDelaySlotPending);
  writer->writeBool(vpu.pendingBranchTaken);
  writer->writeU16(vpu.pendingBranchTarget);
  writer->writeBool(vpu.pendingBranchLinkValid);
  writer->writeU8(vpu.pendingBranchLinkRegister);
  writer->writeU16(vpu.pendingBranchLinkValue);
  writer->writeBool(vpu.terminationRequested);
  writer->writeBool(vpu.haltAfterDrain);
  writer->writeBool(vpu.dEnabled);
  writer->writeBool(vpu.tEnabled);
  writer->writeBool(vpu.xgkickWaiting);
  writer->writeBool(vpu.xgkickTransferStarted);
  writer->writeBool(vpu.dBitStop);
  writer->writeBool(vpu.tBitStop);
  writer->writeBool(vpu.forceBreakStop);
  writer->writeBool(vpu.cop2WriteInterlockReleased);

  writer->writeSize(vpu.fpRegisters.size());
  for (const FPRegister &value : vpu.fpRegisters)
  {
    writeFPRegister(writer, value);
  }
  writer->writeSize(vpu.intRegisters.size());
  for (std::uint16_t value : vpu.intRegisters)
  {
    writer->writeU16(value);
  }
  writer->writeU32(vpu.iRegister.bits());
  writer->writeU32(vpu.qRegister.bits());
  writer->writeU32(vpu.pRegister.bits());
  writer->writeU32(vpu.rRegister);
  writer->writeU16(vpu.cmsarRegister);
  writer->writeU16(vpu.MACFlags);
  writer->writeU16(vpu.statusFlags);
  writeFPRegister(writer, vpu.accumulator);
  writer->writeU64(vpu.clippingFlags);
  writeOrchestrator(writer, vpu.orchestrator);
  writeFPRegister(writer, vpu.virtualDestRegister);
  writeFPRegister(writer, vpu.accumulatorForwardValue);
  writer->writeU8(vpu.pendingAccumulatorWrites);
  writer->writeBool(vpu.accumulatorForwardValid);
  writeLowerInstruction(writer, vpu.pendingLowerInstruction);
  writer->writeU16(vpu.pendingLowerInstructionAddress);
  writer->writeBool(vpu.lowerInstructionPending);
  writer->writeBool(vpu.pendingLowerInstructionReady);
  writer->writeBool(vpu.pendingLowerWritebackDiscarded);
  for (std::uint8_t value : vpu.pendingIntegerWrites)
  {
    writer->writeU8(value);
  }
  for (std::uint8_t value : vpu.pendingIALUWrites)
  {
    writer->writeU8(value);
  }
  for (std::uint16_t value : vpu.bypassedIntegerValues)
  {
    writer->writeU16(value);
  }
}

void NekoSaveStateCodec::readVPU(
  SaveStateReader *reader,
  VPU *vpu)
{
  const VPUType type = readEnum<VPUType>(
    reader,
    static_cast<std::uint8_t>(VPUType::VU1),
    "VU type");
  require(type == vpu->type, "VU type does not match its slot");
  vpu->microMem = reader->readByteVector(
    vpu->microMem.size(),
    "VU micro memory");
  vpu->vuMem = reader->readByteVector(
    vpu->vuMem.size(),
    "VU data memory");
  vpu->state = reader->readU8();
  vpu->cycles = reader->readU32();
  vpu->mode = reader->readU8();
  vpu->macroIssueNeedsAdvance =
    reader->readBool("VU macro issue-advance flag");
  vpu->macroTransferStallPending =
    reader->readBool("VU macro transfer-stall flag");
  vpu->microMemPC = reader->readU16();
  vpu->terminationPositionCounter = reader->readU16();
  vpu->terminationPositionValid =
    reader->readBool("VU termination-position flag");
  vpu->endDelaySlotPending =
    reader->readBool("VU end-delay flag");
  vpu->branchDelaySlotPending =
    reader->readBool("VU branch-delay flag");
  vpu->pendingBranchTaken =
    reader->readBool("VU pending-branch flag");
  vpu->pendingBranchTarget = reader->readU16();
  vpu->pendingBranchLinkValid =
    reader->readBool("VU branch-link flag");
  vpu->pendingBranchLinkRegister = reader->readU8();
  vpu->pendingBranchLinkValue = reader->readU16();
  vpu->terminationRequested =
    reader->readBool("VU termination-request flag");
  vpu->haltAfterDrain =
    reader->readBool("VU halt-after-drain flag");
  vpu->dEnabled = reader->readBool("VU D-bit enable");
  vpu->tEnabled = reader->readBool("VU T-bit enable");
  vpu->xgkickWaiting =
    reader->readBool("VU XGKICK wait flag");
  vpu->xgkickTransferStarted =
    reader->readBool("VU XGKICK start flag");
  vpu->dBitStop =
    reader->readBool("VU D-bit stop flag");
  vpu->tBitStop =
    reader->readBool("VU T-bit stop flag");
  vpu->forceBreakStop =
    reader->readBool("VU force-break stop flag");
  vpu->cop2WriteInterlockReleased =
    reader->readBool("VU COP2 write-interlock flag");

  const std::uint32_t fpRegisterCount = reader->readU32();
  require(
    fpRegisterCount == vpu->fpRegisters.size(),
    "VU floating-point register count is invalid");
  for (FPRegister &value : vpu->fpRegisters)
  {
    value = readFPRegister(reader);
  }
  const std::uint32_t intRegisterCount = reader->readU32();
  require(
    intRegisterCount == vpu->intRegisters.size(),
    "VU integer register count is invalid");
  for (std::uint16_t &value : vpu->intRegisters)
  {
    value = reader->readU16();
  }
  vpu->iRegister.setBits(reader->readU32());
  vpu->qRegister.setBits(reader->readU32());
  vpu->pRegister.setBits(reader->readU32());
  vpu->rRegister = reader->readU32();
  vpu->cmsarRegister = reader->readU16();
  vpu->MACFlags = reader->readU16();
  vpu->statusFlags = reader->readU16();
  vpu->accumulator = readFPRegister(reader);
  vpu->clippingFlags = reader->readU64();
  readOrchestrator(reader, &vpu->orchestrator);
  vpu->virtualDestRegister = readFPRegister(reader);
  vpu->accumulatorForwardValue = readFPRegister(reader);
  vpu->pendingAccumulatorWrites = reader->readU8();
  vpu->accumulatorForwardValid =
    reader->readBool("VU accumulator-forward flag");
  vpu->pendingLowerInstruction =
    readLowerInstruction(reader);
  vpu->pendingLowerInstructionAddress = reader->readU16();
  vpu->lowerInstructionPending =
    reader->readBool("VU lower-pending flag");
  vpu->pendingLowerInstructionReady =
    reader->readBool("VU lower-ready flag");
  vpu->pendingLowerWritebackDiscarded =
    reader->readBool("VU lower-discard flag");
  for (std::uint8_t &value : vpu->pendingIntegerWrites)
  {
    value = reader->readU8();
  }
  for (std::uint8_t &value : vpu->pendingIALUWrites)
  {
    value = reader->readU8();
  }
  for (std::uint16_t &value : vpu->bypassedIntegerValues)
  {
    value = reader->readU16();
  }

  require(
    vpu->state >= VPU_STATE_READY &&
    vpu->state <= VPU_STATE_STOP,
    "VU run state is invalid");
  require(
    vpu->mode == VPU_MODE_MICRO ||
    vpu->mode == VPU_MODE_MACRO,
    "VU mode is invalid");
  require(
    !vpu->macroIssueNeedsAdvance ||
    (vpu->state == VPU_STATE_RUN &&
     vpu->mode == VPU_MODE_MACRO),
    "VU macro issue-advance state is invalid");
  require(
    vpu->microMemPC <= vpu->microMem.size() &&
    vpu->microMemPC % 8 == 0,
    "VU program counter is invalid");
  require(
    vpu->pendingBranchTarget < vpu->microMem.size() &&
    vpu->pendingBranchTarget % 8 == 0,
    "VU pending branch target is invalid");
  require(
    vpu->pendingBranchLinkRegister <= VPU_REGISTER_VI15,
    "VU branch-link register is invalid");
  require(
    vpu->statusFlags <= VPU_FLAG_DS,
    "VU status flags are invalid");
  require(
    !vpu->forceBreakStop ||
      (!vpu->dBitStop && !vpu->tBitStop),
    "VU stop cause is invalid");
  require(
    (!vpu->dBitStop &&
     !vpu->tBitStop &&
     !vpu->forceBreakStop) ||
      vpu->state == VPU_STATE_STOP,
    "VU stop cause does not match run state");
  require(
    !vpu->cop2WriteInterlockReleased ||
      vpu->state == VPU_STATE_RUN,
    "VU COP2 write interlock does not match run state");
  require(
    vpu->clippingFlags <= VPU_CLIPPING_FLAG_MASK,
    "VU clipping flags are invalid");
  require(
    vpu->pendingAccumulatorWrites <= MAX_PIPELINES,
    "VU pending accumulator count is invalid");
  for (std::uint8_t value : vpu->pendingIntegerWrites)
  {
    require(
      value <= MAX_PIPELINES,
      "VU pending integer-write count is invalid");
  }
  for (std::uint8_t value : vpu->pendingIALUWrites)
  {
    require(
      value <= MAX_PIPELINES,
      "VU pending IALU-write count is invalid");
  }
}

void NekoSaveStateCodec::commitVPU(
  VPU *destination,
  VPU *source,
  PipelineLists *lists)
{
  destination->microMem.swap(source->microMem);
  destination->vuMem.swap(source->vuMem);
  destination->state = source->state;
  destination->cycles = source->cycles;
  destination->mode = source->mode;
  destination->macroIssueNeedsAdvance =
    source->macroIssueNeedsAdvance;
  destination->macroTransferStallPending =
    source->macroTransferStallPending;
  destination->microMemPC = source->microMemPC;
  destination->terminationPositionCounter =
    source->terminationPositionCounter;
  destination->terminationPositionValid =
    source->terminationPositionValid;
  destination->endDelaySlotPending =
    source->endDelaySlotPending;
  destination->branchDelaySlotPending =
    source->branchDelaySlotPending;
  destination->pendingBranchTaken =
    source->pendingBranchTaken;
  destination->pendingBranchTarget =
    source->pendingBranchTarget;
  destination->pendingBranchLinkValid =
    source->pendingBranchLinkValid;
  destination->pendingBranchLinkRegister =
    source->pendingBranchLinkRegister;
  destination->pendingBranchLinkValue =
    source->pendingBranchLinkValue;
  destination->terminationRequested =
    source->terminationRequested;
  destination->haltAfterDrain = source->haltAfterDrain;
  destination->dEnabled = source->dEnabled;
  destination->tEnabled = source->tEnabled;
  destination->xgkickWaiting = source->xgkickWaiting;
  destination->xgkickTransferStarted =
    source->xgkickTransferStarted;
  destination->dBitStop = source->dBitStop;
  destination->tBitStop = source->tBitStop;
  destination->forceBreakStop = source->forceBreakStop;
  destination->cop2WriteInterlockReleased =
    source->cop2WriteInterlockReleased;
  destination->fpRegisters.swap(source->fpRegisters);
  destination->intRegisters.swap(source->intRegisters);
  destination->iRegister = source->iRegister;
  destination->qRegister = source->qRegister;
  destination->pRegister = source->pRegister;
  destination->rRegister = source->rRegister;
  destination->cmsarRegister = source->cmsarRegister;
  destination->MACFlags = source->MACFlags;
  destination->statusFlags = source->statusFlags;
  destination->accumulator = source->accumulator;
  destination->clippingFlags = source->clippingFlags;
  destination->orchestrator.pipelines =
    source->orchestrator.pipelines;
  destination->orchestrator.stalling =
    source->orchestrator.stalling;
  destination->orchestrator.executing.swap((*lists)[0]);
  destination->orchestrator.waiting.swap((*lists)[1]);
  destination->orchestrator.pool.swap((*lists)[2]);
  destination->virtualDestRegister =
    source->virtualDestRegister;
  destination->accumulatorForwardValue =
    source->accumulatorForwardValue;
  destination->pendingAccumulatorWrites =
    source->pendingAccumulatorWrites;
  destination->accumulatorForwardValid =
    source->accumulatorForwardValid;
  destination->pendingLowerInstruction =
    source->pendingLowerInstruction;
  destination->pendingLowerInstructionAddress =
    source->pendingLowerInstructionAddress;
  destination->lowerInstructionPending =
    source->lowerInstructionPending;
  destination->pendingLowerInstructionReady =
    source->pendingLowerInstructionReady;
  destination->pendingLowerWritebackDiscarded =
    source->pendingLowerWritebackDiscarded;
  destination->pendingIntegerWrites =
    source->pendingIntegerWrites;
  destination->pendingIALUWrites =
    source->pendingIALUWrites;
  destination->bypassedIntegerValues =
    source->bypassedIntegerValues;
}

void NekoSaveStateCodec::writePipeline(
  SaveStateWriter *writer,
  const Pipeline &pipeline)
{
  writer->writeU8(pipeline.type);
  writer->writeU16(pipeline.opCode);
  writer->writeU32(
    static_cast<std::uint32_t>(pipeline.intResult));
  writeFPRegister(writer, pipeline.fpResult);
  writeFPRegister(writer, pipeline.flagResult);
  writeFPRegister(writer, pipeline.operationResult);
  writeFPRegister(writer, pipeline.accumulatorValue);
  writeFPRegister(writer, pipeline.sourceValue1);
  writeFPRegister(writer, pipeline.sourceValue2);
  writer->writeU8(pipeline.ignoredResultFields);
  writer->writeU8(pipeline.srcReg1);
  writer->writeU8(pipeline.srcReg2);
  writer->writeU8(pipeline.destReg);
  writer->writeU8(pipeline.integerDestReg);
  writer->writeU8(pipeline.destFieldMask);
  writer->writeU8(pipeline.srcReg1FieldMask);
  writer->writeU8(pipeline.srcReg2FieldMask);
  writer->writeU16(pipeline.instructionAddress);
  writer->writeU16(pipeline.memoryAddress);
  writer->writeU16(
    static_cast<std::uint16_t>(pipeline.immediate));
  writer->writeU32(pipeline.immediateBits);
  writer->writeU32(pipeline.scalarResultBits);
  writer->writeU8(pipeline.scalarResultFlags);
  writer->writeU16(pipeline.intSourceValue1);
  writer->writeU16(pipeline.intSourceValue2);
  writer->writeBool(pipeline.intSource1Sampled);
  writer->writeBool(pipeline.intSource2Sampled);
  writer->writeBool(pipeline.vectorSourcesSampled);
  writer->writeBool(pipeline.xgkickStarted);
  writer->writeBool(pipeline.discardWriteback);
  writer->writeU8(
    static_cast<std::uint8_t>(pipeline.currentStage));
  writer->writeU8(pipeline.currentStageIndex);
  writer->writeU8(pipeline.executionStageCount);
  writer->writeBool(pipeline.complete);
}

void NekoSaveStateCodec::readPipeline(
  SaveStateReader *reader,
  Pipeline *pipeline)
{
  pipeline->type = reader->readU8();
  pipeline->opCode = reader->readU16();
  pipeline->intResult =
    static_cast<std::int32_t>(reader->readU32());
  pipeline->fpResult = readFPRegister(reader);
  pipeline->flagResult = readFPRegister(reader);
  pipeline->operationResult = readFPRegister(reader);
  pipeline->accumulatorValue = readFPRegister(reader);
  pipeline->sourceValue1 = readFPRegister(reader);
  pipeline->sourceValue2 = readFPRegister(reader);
  pipeline->ignoredResultFields = reader->readU8();
  pipeline->srcReg1 = reader->readU8();
  pipeline->srcReg2 = reader->readU8();
  pipeline->destReg = reader->readU8();
  pipeline->integerDestReg = reader->readU8();
  pipeline->destFieldMask = reader->readU8();
  pipeline->srcReg1FieldMask = reader->readU8();
  pipeline->srcReg2FieldMask = reader->readU8();
  pipeline->instructionAddress = reader->readU16();
  pipeline->memoryAddress = reader->readU16();
  pipeline->immediate =
    static_cast<std::int16_t>(reader->readU16());
  pipeline->immediateBits = reader->readU32();
  pipeline->scalarResultBits = reader->readU32();
  pipeline->scalarResultFlags = reader->readU8();
  pipeline->intSourceValue1 = reader->readU16();
  pipeline->intSourceValue2 = reader->readU16();
  pipeline->intSource1Sampled =
    reader->readBool("VU pipeline source-1 sample flag");
  pipeline->intSource2Sampled =
    reader->readBool("VU pipeline source-2 sample flag");
  pipeline->vectorSourcesSampled =
    reader->readBool("VU pipeline vector-source sample flag");
  pipeline->xgkickStarted =
    reader->readBool("VU pipeline XGKICK flag");
  pipeline->discardWriteback =
    reader->readBool("VU pipeline discard flag");
  pipeline->currentStage = readEnum<VUPipelineStage>(
    reader,
    static_cast<std::uint8_t>(VUPipelineStage::P),
    "VU pipeline stage");
  pipeline->currentStageIndex = reader->readU8();
  pipeline->executionStageCount = reader->readU8();
  pipeline->complete =
    reader->readBool("VU pipeline completion flag");

  require(
    pipeline->type <= VPU_PIPELINE_TYPE_VIF_CONTROL,
    "VU pipeline type is invalid");
  require(
    pipeline->ignoredResultFields <= FP_REGISTER_ALL_FIELDS &&
    pipeline->destFieldMask <= FP_REGISTER_ALL_FIELDS &&
    pipeline->srcReg1FieldMask <= FP_REGISTER_ALL_FIELDS &&
    pipeline->srcReg2FieldMask <= FP_REGISTER_ALL_FIELDS,
    "VU pipeline field mask is invalid");
  require(
    pipeline->srcReg1 <= VPU_REGISTER_VF31 &&
    pipeline->srcReg2 <= VPU_REGISTER_VF31 &&
    pipeline->destReg <= VPU_REGISTER_ACCUMULATOR &&
    pipeline->integerDestReg <= VPU_REGISTER_VI15,
    "VU pipeline register is invalid");
  require(
    (pipeline->scalarResultFlags & ~0x0f) == 0,
    "VU scalar result flags are invalid");
  require(
    pipeline->currentStageIndex <= MAX_PIPELINE_STAGE_INDEX &&
    pipeline->executionStageCount <= MAX_PIPELINE_STAGE_INDEX,
    "VU pipeline stage timing is invalid");
}

void NekoSaveStateCodec::writeOrchestrator(
  SaveStateWriter *writer,
  const PipelineOrchestrator &orchestrator)
{
  writer->writeBool(orchestrator.stalling);
  writer->writeSize(orchestrator.pipelines.size());
  for (const Pipeline &pipeline : orchestrator.pipelines)
  {
    writePipeline(writer, pipeline);
  }
  const std::list<Pipeline *> *lists[] = {
    &orchestrator.executing,
    &orchestrator.waiting,
    &orchestrator.pool
  };
  for (const std::list<Pipeline *> *list : lists)
  {
    writer->writeSize(list->size());
    for (const Pipeline *pipeline : *list)
    {
      writer->writeU8(pipelineIndex(
        orchestrator,
        pipeline));
    }
  }
}

void NekoSaveStateCodec::readOrchestrator(
  SaveStateReader *reader,
  PipelineOrchestrator *orchestrator)
{
  orchestrator->stalling =
    reader->readBool("VU orchestrator stall flag");
  const std::uint32_t pipelineCount = reader->readU32();
  require(
    pipelineCount == orchestrator->pipelines.size(),
    "VU pipeline-array size is invalid");
  for (Pipeline &pipeline : orchestrator->pipelines)
  {
    readPipeline(reader, &pipeline);
  }

  orchestrator->executing.clear();
  orchestrator->waiting.clear();
  orchestrator->pool.clear();
  std::array<bool, MAX_PIPELINES> used = {};
  std::list<Pipeline *> *lists[] = {
    &orchestrator->executing,
    &orchestrator->waiting,
    &orchestrator->pool
  };
  std::size_t membershipCount = 0;
  for (std::list<Pipeline *> *list : lists)
  {
    const std::uint32_t count = reader->readU32();
    require(count <= MAX_PIPELINES, "VU pipeline-list size is invalid");
    membershipCount += count;
    for (std::uint32_t index = 0; index < count; ++index)
    {
      const std::uint8_t pipeline = reader->readU8();
      require(
        pipeline < MAX_PIPELINES,
        "VU pipeline-list index is invalid");
      require(
        !used[pipeline],
        "VU pipeline appears in multiple lists");
      used[pipeline] = true;
      list->push_back(&orchestrator->pipelines[pipeline]);
    }
  }
  require(
    membershipCount == MAX_PIPELINES,
    "VU pipeline-list membership is incomplete");
}

NekoSaveStateCodec::PipelineLists
NekoSaveStateCodec::makePipelineLists(
  const PipelineOrchestrator &source,
  PipelineOrchestrator *destination)
{
  PipelineLists result;
  const std::list<Pipeline *> *sourceLists[] = {
    &source.executing,
    &source.waiting,
    &source.pool
  };
  for (std::size_t listIndex = 0;
       listIndex < result.size();
       ++listIndex)
  {
    for (const Pipeline *pipeline : *sourceLists[listIndex])
    {
      result[listIndex].push_back(
        &destination->pipelines[pipelineIndex(
          source,
          pipeline)]);
    }
  }
  return result;
}

std::uint8_t NekoSaveStateCodec::pipelineIndex(
  const PipelineOrchestrator &orchestrator,
  const Pipeline *pipeline)
{
  const Pipeline *begin = orchestrator.pipelines.data();
  for (std::size_t index = 0;
       index < orchestrator.pipelines.size();
       ++index)
  {
    if (pipeline == begin + index)
    {
      return static_cast<std::uint8_t>(index);
    }
  }
  throw std::runtime_error(
    "Cannot save an invalid VU pipeline pointer.");
}

void NekoSaveStateCodec::writeVIF(
  SaveStateWriter *writer,
  const VIF &vif)
{
  writer->writeU8(static_cast<std::uint8_t>(vif.type));
  writer->writeU16(vif.cycleRegister);
  writer->writeU8(vif.modeRegister);
  writer->writeU32(vif.maskRegister);
  for (std::uint32_t value : vif.rowRegisters)
  {
    writer->writeU32(value);
  }
  for (std::uint32_t value : vif.columnRegisters)
  {
    writer->writeU32(value);
  }
  writer->writeU16(vif.topRegister);
  writer->writeU16(vif.itopRegister);
  writer->writeU16(vif.itopsRegister);
  writer->writeU16(vif.baseRegister);
  writer->writeU16(vif.offsetRegister);
  writer->writeU16(vif.topsRegister);
  writer->writeU16(vif.markRegister);
  writer->writeBool(vif.dbf);
  writer->writeBool(vif.path3Mask);
  writer->writeBool(vif.markFlag);
  writer->writeBool(vif.interruptFlag);
  writer->writeU32(vif.codeRegister);
  writeVIFCommand(writer, vif.streamCommand);
  writer->writeU32(vif.streamPayloadWordCount);
  writer->writeU32(vif.streamPayloadWordsRemaining);
  writer->writeU64(vif.streamWordsIngested);
  writer->writeU32(vif.mpgLowerInstruction);
  writer->writeBool(vif.mpgLowerInstructionPending);
  for (std::uint32_t value : vif.directQuadword)
  {
    writer->writeU32(value);
  }
  writer->writeSize(vif.unpackPayload.size());
  for (std::uint32_t value : vif.unpackPayload)
  {
    writer->writeU32(value);
  }
  writer->writeSize(vif.fifoWords.size());
  for (std::uint32_t value : vif.fifoWords)
  {
    writer->writeU32(value);
  }
}

void NekoSaveStateCodec::readVIF(
  SaveStateReader *reader,
  VIF *vif)
{
  const VIFType type = readEnum<VIFType>(
    reader,
    static_cast<std::uint8_t>(VIFType::VIF1),
    "VIF type");
  require(type == vif->type, "VIF type does not match its slot");
  vif->cycleRegister = reader->readU16();
  vif->modeRegister = reader->readU8();
  vif->maskRegister = reader->readU32();
  for (std::uint32_t &value : vif->rowRegisters)
  {
    value = reader->readU32();
  }
  for (std::uint32_t &value : vif->columnRegisters)
  {
    value = reader->readU32();
  }
  vif->topRegister = reader->readU16();
  vif->itopRegister = reader->readU16();
  vif->itopsRegister = reader->readU16();
  vif->baseRegister = reader->readU16();
  vif->offsetRegister = reader->readU16();
  vif->topsRegister = reader->readU16();
  vif->markRegister = reader->readU16();
  vif->dbf = reader->readBool("VIF double-buffer flag");
  vif->path3Mask = reader->readBool("VIF PATH3 mask");
  vif->markFlag = reader->readBool("VIF mark flag");
  vif->interruptFlag = reader->readBool("VIF interrupt flag");
  vif->codeRegister = reader->readU32();
  vif->streamCommand = readVIFCommand(reader);
  vif->streamPayloadWordCount = reader->readU32();
  vif->streamPayloadWordsRemaining = reader->readU32();
  vif->streamWordsIngested = reader->readU64();
  vif->mpgLowerInstruction = reader->readU32();
  vif->mpgLowerInstructionPending =
    reader->readBool("VIF MPG half-instruction flag");
  for (std::uint32_t &value : vif->directQuadword)
  {
    value = reader->readU32();
  }
  const std::uint32_t unpackCount = reader->readU32();
  require(
    unpackCount <= MAX_VIF_UNPACK_WORDS,
    "VIF UNPACK payload size is invalid");
  std::vector<std::uint32_t> unpackPayload;
  unpackPayload.reserve(unpackCount);
  for (std::uint32_t index = 0;
       index < unpackCount;
       ++index)
  {
    unpackPayload.push_back(reader->readU32());
  }
  vif->unpackPayload.swap(unpackPayload);
  const std::uint32_t fifoWordCount = reader->readU32();
  require(
    fifoWordCount <= vif->fifoCapacity() * 4,
    "VIF FIFO size is invalid");
  std::deque<std::uint32_t> fifoWords;
  for (std::uint32_t index = 0;
       index < fifoWordCount;
       ++index)
  {
    fifoWords.push_back(reader->readU32());
  }
  vif->fifoWords.swap(fifoWords);

  require(vif->modeRegister <= 2, "VIF mode is invalid");
  require(
    vif->streamPayloadWordCount <= MAX_VIF_PAYLOAD_WORDS &&
    vif->streamPayloadWordsRemaining <=
      vif->streamPayloadWordCount,
    "VIF payload progress is invalid");
}

void NekoSaveStateCodec::commitVIF(
  VIF *destination,
  VIF *source)
{
  destination->cycleRegister = source->cycleRegister;
  destination->modeRegister = source->modeRegister;
  destination->maskRegister = source->maskRegister;
  destination->rowRegisters = source->rowRegisters;
  destination->columnRegisters = source->columnRegisters;
  destination->topRegister = source->topRegister;
  destination->itopRegister = source->itopRegister;
  destination->itopsRegister = source->itopsRegister;
  destination->baseRegister = source->baseRegister;
  destination->offsetRegister = source->offsetRegister;
  destination->topsRegister = source->topsRegister;
  destination->markRegister = source->markRegister;
  destination->dbf = source->dbf;
  destination->path3Mask = source->path3Mask;
  destination->markFlag = source->markFlag;
  destination->interruptFlag = source->interruptFlag;
  destination->codeRegister = source->codeRegister;
  destination->streamCommand = source->streamCommand;
  destination->streamPayloadWordCount =
    source->streamPayloadWordCount;
  destination->streamPayloadWordsRemaining =
    source->streamPayloadWordsRemaining;
  destination->streamWordsIngested =
    source->streamWordsIngested;
  destination->mpgLowerInstruction =
    source->mpgLowerInstruction;
  destination->mpgLowerInstructionPending =
    source->mpgLowerInstructionPending;
  destination->directQuadword = source->directQuadword;
  destination->unpackPayload.swap(source->unpackPayload);
  destination->fifoWords.swap(source->fifoWords);
}

void NekoSaveStateCodec::writeGIFDecoder(
  SaveStateWriter *writer,
  const GIFDecoder &decoder)
{
  GIFDecoderState state;
  state.tag = decoder.tag;
  state.waitingForTag = decoder.waitingForTag;
  state.activePacket = decoder.activePacket;
  state.remainingQuadwords = decoder.remainingQuadwords;
  state.remainingRegisterValues =
    decoder.remainingRegisterValues;
  state.currentLoop = decoder.currentLoop;
  state.currentRegister = decoder.currentRegister;
  state.qValue = decoder.qValue;
  writeGIFDecoderState(writer, state);
}

void NekoSaveStateCodec::readGIFDecoder(
  SaveStateReader *reader,
  GIFDecoder *decoder)
{
  const GIFDecoderState state =
    readGIFDecoderState(reader, "GIF decoder");
  decoder->tag = state.tag;
  decoder->waitingForTag = state.waitingForTag;
  decoder->activePacket = state.activePacket;
  decoder->remainingQuadwords = state.remainingQuadwords;
  decoder->remainingRegisterValues =
    state.remainingRegisterValues;
  decoder->currentLoop = state.currentLoop;
  decoder->currentRegister = state.currentRegister;
  decoder->qValue = state.qValue;
}

void NekoSaveStateCodec::writeGIFArbiter(
  SaveStateWriter *writer,
  const GIFPathArbiter &arbiter)
{
  writer->writeU8(
    static_cast<std::uint8_t>(arbiter.currentPath));
  for (bool queued : arbiter.queuedPaths)
  {
    writer->writeBool(queued);
  }
  writer->writeBool(arbiter.vifPath3Mask);
  writer->writeBool(arbiter.modePath3Mask);
  writer->writeBool(arbiter.intermittentPath3);
  writer->writeBool(arbiter.timedTransfers);
  writer->writeBool(arbiter.interruptedPath3);
  writer->writeBool(
    arbiter.queuedPath2CanInterruptPath3);
  writer->writeU8(arbiter.path3ImageSliceQuadwords);
  writer->writeU16(arbiter.path3Count);
  writer->writeU16(arbiter.path3Tag);
  writer->writeU8(arbiter.remainingIdleCycles);
  writeGIFDecoderState(
    writer,
    arbiter.suspendedPath3State);
}

void NekoSaveStateCodec::readGIFArbiter(
  SaveStateReader *reader,
  GIFPathArbiter *arbiter)
{
  arbiter->currentPath = readEnum<GIFPath>(
    reader,
    static_cast<std::uint8_t>(GIFPath::Path3),
    "GIF active path");
  for (std::size_t index = 0;
       index < arbiter->queuedPaths.size();
       ++index)
  {
    arbiter->queuedPaths[index] =
      reader->readBool("GIF queued-path flag");
  }
  arbiter->vifPath3Mask =
    reader->readBool("GIF VIF PATH3 mask");
  arbiter->modePath3Mask =
    reader->readBool("GIF mode PATH3 mask");
  arbiter->intermittentPath3 =
    reader->readBool("GIF intermittent-mode flag");
  arbiter->timedTransfers =
    reader->readBool("GIF timing-mode flag");
  arbiter->interruptedPath3 =
    reader->readBool("GIF PATH3 interruption flag");
  arbiter->queuedPath2CanInterruptPath3 =
    reader->readBool("GIF PATH2 interruption flag");
  arbiter->path3ImageSliceQuadwords = reader->readU8();
  arbiter->path3Count = reader->readU16();
  arbiter->path3Tag = reader->readU16();
  arbiter->remainingIdleCycles = reader->readU8();
  arbiter->suspendedPath3State =
    readGIFDecoderState(reader, "suspended GIF PATH3 decoder");
  require(
    arbiter->path3ImageSliceQuadwords <= 7,
    "GIF PATH3 image-slice count is invalid");
  require(
    arbiter->path3Count <= 0x7fff,
    "GIF PATH3 count register is invalid");
}

void NekoSaveStateCodec::writeGIFPath1(
  SaveStateWriter *writer,
  const GIFPath1Transfer &path)
{
  writer->writeBool(path.active);
  writer->writeU16(path.qwordAddress);
  writer->writeU64(path.transferredQuadwords);
}

void NekoSaveStateCodec::readGIFPath1(
  SaveStateReader *reader,
  GIFPath1Transfer *path)
{
  path->active =
    reader->readBool("GIF PATH1 active flag");
  path->qwordAddress = reader->readU16();
  path->transferredQuadwords = reader->readU64();
  require(
    path->qwordAddress <
      path->vpu->dataMemorySize() / 16,
    "GIF PATH1 qword address is invalid");
}

void NekoSaveStateCodec::writeGIFPath3(
  SaveStateWriter *writer,
  const GIFPath3Transfer &path)
{
  writer->writeU64(path.submissionAttempts);
  writer->writeU64(path.transferredQuadwords);
  writer->writeU64(path.completedPackets);
  writer->writeSize(path.guestFIFO.size());
  for (const GIFQuadword &quadword : path.guestFIFO)
  {
    for (std::uint32_t word : quadword)
    {
      writer->writeU32(word);
    }
  }
}

void NekoSaveStateCodec::readGIFPath3(
  SaveStateReader *reader,
  GIFPath3Transfer *path)
{
  path->submissionAttempts = reader->readU64();
  path->transferredQuadwords = reader->readU64();
  path->completedPackets = reader->readU64();
  const std::uint32_t fifoCount = reader->readU32();
  require(fifoCount <= 16, "GIF FIFO size is invalid");
  std::deque<GIFQuadword> guestFIFO;
  for (std::uint32_t index = 0; index < fifoCount; ++index)
  {
    GIFQuadword quadword = {};
    for (std::uint32_t &word : quadword)
    {
      word = reader->readU32();
    }
    guestFIFO.push_back(quadword);
  }
  path->guestFIFO.swap(guestFIFO);
  require(
    path->transferredQuadwords <= path->submissionAttempts,
    "GIF PATH3 transfer counters are invalid");
}

void NekoSaveStateCodec::writeGS(
  SaveStateWriter *writer,
  const GS &gs)
{
  writer->writeSize(gs.registers.size());
  for (std::uint64_t value : gs.registers)
  {
    writer->writeU64(value);
  }
  writeGSPrimitive(writer, gs.primitiveRegister);
  writeGSColor(writer, gs.colorRegister);
  writeGSVertex(writer, gs.vertexRegister);
  writeGSTextureCoordinate(
    writer,
    gs.textureCoordinateRegister);
  for (const GSVertexCoordinate &vertex :
       gs.primitiveVertices)
  {
    writeGSVertex(writer, vertex);
  }
  for (const GSColor &color : gs.primitiveColors)
  {
    writeGSColor(writer, color);
  }
  for (const GSTextureCoordinate &coordinate :
       gs.primitiveTextureCoordinates)
  {
    writeGSTextureCoordinate(writer, coordinate);
  }
  writer->writeU64(gs.primitiveVertexCount);
  writer->writeU64(gs.renderedPoints);
  writer->writeU64(gs.renderedLines);
  writer->writeU64(gs.renderedSprites);
  writer->writeU64(gs.renderedTriangles);
  writer->writeU64(gs.writtenPixels);
  for (const GSContext &context : gs.contexts)
  {
    writeGSContext(writer, context);
  }
  writeGSImageTransfer(writer, gs.transfer);
  writer->writeBool(gs.reverseHostInterface);
  writer->writeBool(gs.perPixelAlphaBlending);
  writer->writeSize(gs.localMemory.size());
  for (std::uint32_t value : gs.localMemory)
  {
    writer->writeU32(value);
  }
}

void NekoSaveStateCodec::readGS(
  SaveStateReader *reader,
  GS *gs)
{
  const std::uint32_t registerCount = reader->readU32();
  require(
    registerCount == gs->registers.size(),
    "GS register-file size is invalid");
  for (std::uint64_t &value : gs->registers)
  {
    value = reader->readU64();
  }
  gs->primitiveRegister = readGSPrimitive(reader);
  gs->colorRegister = readGSColor(reader);
  gs->vertexRegister = readGSVertex(reader);
  gs->textureCoordinateRegister =
    readGSTextureCoordinate(reader);
  for (GSVertexCoordinate &vertex : gs->primitiveVertices)
  {
    vertex = readGSVertex(reader);
  }
  for (GSColor &color : gs->primitiveColors)
  {
    color = readGSColor(reader);
  }
  for (GSTextureCoordinate &coordinate :
       gs->primitiveTextureCoordinates)
  {
    coordinate = readGSTextureCoordinate(reader);
  }
  const std::uint64_t primitiveVertexCount =
    reader->readU64();
  require(
    primitiveVertexCount <= gs->primitiveVertices.size(),
    "GS primitive queue size is invalid");
  gs->primitiveVertexCount =
    static_cast<std::size_t>(primitiveVertexCount);
  gs->renderedPoints = reader->readU64();
  gs->renderedLines = reader->readU64();
  gs->renderedSprites = reader->readU64();
  gs->renderedTriangles = reader->readU64();
  gs->writtenPixels = reader->readU64();
  for (GSContext &context : gs->contexts)
  {
    context = readGSContext(reader);
  }
  gs->transfer = readGSImageTransfer(reader);
  gs->reverseHostInterface =
    reader->readBool("GS BUSDIR flag");
  gs->perPixelAlphaBlending =
    reader->readBool("GS per-pixel alpha flag");
  const std::uint32_t localMemorySize = reader->readU32();
  require(
    localMemorySize == gs->localMemory.size(),
    "GS local-memory size is invalid");
  std::vector<std::uint32_t> localMemory;
  localMemory.reserve(localMemorySize);
  for (std::uint32_t index = 0;
       index < localMemorySize;
       ++index)
  {
    localMemory.push_back(reader->readU32());
  }
  gs->localMemory.swap(localMemory);
}

void NekoSaveStateCodec::commitGS(
  GS *destination,
  GS *source)
{
  destination->registers = source->registers;
  destination->primitiveRegister =
    source->primitiveRegister;
  destination->colorRegister = source->colorRegister;
  destination->vertexRegister = source->vertexRegister;
  destination->textureCoordinateRegister =
    source->textureCoordinateRegister;
  destination->primitiveVertices =
    source->primitiveVertices;
  destination->primitiveColors = source->primitiveColors;
  destination->primitiveTextureCoordinates =
    source->primitiveTextureCoordinates;
  destination->primitiveVertexCount =
    source->primitiveVertexCount;
  destination->renderedPoints = source->renderedPoints;
  destination->renderedLines = source->renderedLines;
  destination->renderedSprites = source->renderedSprites;
  destination->renderedTriangles =
    source->renderedTriangles;
  destination->writtenPixels = source->writtenPixels;
  destination->contexts = source->contexts;
  destination->transfer = source->transfer;
  destination->reverseHostInterface =
    source->reverseHostInterface;
  destination->perPixelAlphaBlending =
    source->perPixelAlphaBlending;
  destination->localMemory.swap(source->localMemory);
}

void NekoSaveStateCodec::writeDMAC(
  SaveStateWriter *writer,
  const GIFDMACChannel &dmac)
{
  writer->writeU32(dmac.channelControlRegister);
  writer->writeU32(dmac.memoryAddressRegister);
  writer->writeU32(dmac.quadwordCountRegister);
  writer->writeU32(dmac.tagAddressRegister);
  for (std::uint32_t value : dmac.addressStackRegisters)
  {
    writer->writeU32(value);
  }
  writer->writeU32(dmac.globalControlRegister);
  writer->writeU32(dmac.statusRegister);
  writer->writeU32(dmac.statusMaskRegister);
  writer->writeBool(dmac.terminateAfterPacket);
  writer->writeBool(dmac.path3Stalled);
  writer->writeU8(dmac.addressStackDepth);
  writer->writeU64(dmac.transferredQuadwords);
}

void NekoSaveStateCodec::readDMAC(
  SaveStateReader *reader,
  GIFDMACChannel *dmac)
{
  dmac->channelControlRegister = reader->readU32();
  dmac->memoryAddressRegister = reader->readU32();
  dmac->quadwordCountRegister = reader->readU32();
  dmac->tagAddressRegister = reader->readU32();
  for (std::uint32_t &value : dmac->addressStackRegisters)
  {
    value = reader->readU32();
  }
  dmac->globalControlRegister = reader->readU32();
  dmac->statusRegister = reader->readU32();
  dmac->statusMaskRegister = reader->readU32();
  dmac->terminateAfterPacket =
    reader->readBool("GIF DMAC termination flag");
  dmac->path3Stalled =
    reader->readBool("GIF DMAC PATH3 stall flag");
  dmac->addressStackDepth = reader->readU8();
  dmac->transferredQuadwords = reader->readU64();

  const std::uint32_t writableControl =
    GIFDMACChannelControl::FROM_MEMORY |
    GIFDMACChannelControl::MODE_MASK |
    GIFDMACChannelControl::ADDRESS_STACK_MASK |
    GIFDMACChannelControl::TAG_TRANSFER_ENABLE |
    GIFDMACChannelControl::TAG_INTERRUPT_ENABLE |
    GIFDMACChannelControl::START |
    GIFDMACChannelControl::TAG_MASK;
  const std::uint32_t mode =
    dmac->channelControlRegister &
    GIFDMACChannelControl::MODE_MASK;
  require(
    (dmac->channelControlRegister & ~writableControl) == 0 &&
    (mode == 0 || mode == GIFDMACChannelControl::CHAIN_MODE),
    "GIF DMAC channel control is invalid");
  require(
    dmac->quadwordCountRegister <= 0xffff,
    "GIF DMAC qword count is invalid");
  const std::uint32_t addresses[] = {
    dmac->memoryAddressRegister,
    dmac->tagAddressRegister,
    dmac->addressStackRegisters[0],
    dmac->addressStackRegisters[1]
  };
  for (std::uint32_t address : addresses)
  {
    require(
      (address & UINT32_C(0x8000000f)) == 0,
      "GIF DMAC address is invalid");
  }
  require(
    dmac->globalControlRegister <=
      GIFDMACControl::DMA_ENABLE,
    "GIF DMAC global control is invalid");
  require(
    (dmac->statusRegister &
     ~(GIFDMACStatus::CHANNEL_1 |
       GIFDMACStatus::CHANNEL_2)) == 0 &&
    (dmac->statusMaskRegister &
     ~(GIFDMACStatus::CHANNEL_1_MASK |
       GIFDMACStatus::CHANNEL_2_MASK)) == 0,
    "GIF DMAC status is invalid");
  require(
    dmac->addressStackDepth <=
      dmac->addressStackRegisters.size() &&
    ((dmac->channelControlRegister &
      GIFDMACChannelControl::ADDRESS_STACK_MASK) >> 4) ==
      dmac->addressStackDepth,
    "GIF DMAC address-stack state is invalid");
}

void NekoSaveStateCodec::writeVIF1DMAC(
  SaveStateWriter *writer,
  const VIF1DMACChannel &dmac)
{
  writer->writeU32(dmac.channelControlRegister);
  writer->writeU32(dmac.memoryAddressRegister);
  writer->writeU32(dmac.quadwordCountRegister);
  writer->writeU32(dmac.tagAddressRegister);
  for (std::uint32_t address : dmac.addressStackRegisters)
  {
    writer->writeU32(address);
  }
  writer->writeBool(dmac.terminateAfterPacket);
  writer->writeBool(dmac.vif1Stalled);
  writer->writeU8(dmac.addressStackDepth);
  writer->writeU64(dmac.transferredQuadwords);
}

void NekoSaveStateCodec::readVIF1DMAC(
  SaveStateReader *reader,
  VIF1DMACChannel *dmac)
{
  dmac->channelControlRegister = reader->readU32();
  dmac->memoryAddressRegister = reader->readU32();
  dmac->quadwordCountRegister = reader->readU32();
  dmac->tagAddressRegister = reader->readU32();
  for (std::uint32_t &address : dmac->addressStackRegisters)
  {
    address = reader->readU32();
  }
  dmac->terminateAfterPacket =
    reader->readBool("VIF1 DMAC termination flag");
  dmac->vif1Stalled =
    reader->readBool("VIF1 DMAC stall flag");
  dmac->addressStackDepth = reader->readU8();
  dmac->transferredQuadwords = reader->readU64();

  const std::uint32_t writableControl =
    GIFDMACChannelControl::FROM_MEMORY |
    GIFDMACChannelControl::MODE_MASK |
    GIFDMACChannelControl::ADDRESS_STACK_MASK |
    GIFDMACChannelControl::TAG_TRANSFER_ENABLE |
    GIFDMACChannelControl::TAG_INTERRUPT_ENABLE |
    GIFDMACChannelControl::START |
    GIFDMACChannelControl::TAG_MASK;
  const std::uint32_t mode =
    dmac->channelControlRegister &
    GIFDMACChannelControl::MODE_MASK;
  require(
    (dmac->channelControlRegister & ~writableControl) == 0 &&
    ((dmac->channelControlRegister &
      GIFDMACChannelControl::FROM_MEMORY) != 0 ||
     (dmac->channelControlRegister &
      GIFDMACChannelControl::START) == 0) &&
    (mode == 0 || mode == GIFDMACChannelControl::CHAIN_MODE),
    "VIF1 DMAC channel control is invalid");
  require(
    dmac->quadwordCountRegister <= 0xffff,
    "VIF1 DMAC qword count is invalid");
  const std::uint32_t addresses[] = {
    dmac->memoryAddressRegister,
    dmac->tagAddressRegister,
    dmac->addressStackRegisters[0],
    dmac->addressStackRegisters[1]
  };
  for (std::uint32_t address : addresses)
  {
    require(
      (address & ~UINT32_C(0x7ffffff0)) == 0,
      "VIF1 DMAC address is invalid");
  }
  require(
    dmac->addressStackDepth <=
      dmac->addressStackRegisters.size() &&
    ((dmac->channelControlRegister &
      GIFDMACChannelControl::ADDRESS_STACK_MASK) >> 4) ==
      dmac->addressStackDepth,
    "VIF1 DMAC address-stack state is invalid");
}

void NekoSaveStateCodec::writeGSDisplay(
  SaveStateWriter *writer,
  const GSDisplay &display)
{
  writer->writeSize(display.circuits.size());
  for (const GSDisplay::Circuit &circuit : display.circuits)
  {
    writer->writeU16(circuit.basePointer);
    writer->writeU8(circuit.bufferWidth);
    writer->writeU8(circuit.pixelStorageMode);
    writer->writeU16(circuit.sourceX);
    writer->writeU16(circuit.sourceY);
    writer->writeU8(circuit.horizontalMagnification);
    writer->writeU8(circuit.verticalMagnification);
    writer->writeU16(circuit.displayWidth);
    writer->writeU16(circuit.displayHeight);
  }
  writer->writeU64(display.videoTiming.activeCycles);
  writer->writeU64(display.videoTiming.totalCycles);
  writer->writeU64(display.modeRegister);
  writer->writeU64(display.syncModeRegister);
  writer->writeU64(display.backgroundColor);
  writer->writeU64(display.interruptMaskRegister);
  writer->writeU64(display.cycleInFrame);
  writer->writeU64(display.frameBoundaries);
  writer->writeBool(display.verticalBlank);
  writer->writeBool(display.oddField);
  writer->writeBool(display.vsyncInterrupt);
  writer->writeBool(display.verticalBlankStarted);
  writer->writeBool(display.verticalBlankEnded);
}

void NekoSaveStateCodec::readGSDisplay(
  SaveStateReader *reader,
  GSDisplay *display)
{
  const std::uint32_t circuitCount = reader->readU32();
  require(
    circuitCount == display->circuits.size(),
    "GS display-circuit count is invalid");
  for (GSDisplay::Circuit &circuit : display->circuits)
  {
    circuit.basePointer = reader->readU16();
    circuit.bufferWidth = reader->readU8();
    circuit.pixelStorageMode = reader->readU8();
    circuit.sourceX = reader->readU16();
    circuit.sourceY = reader->readU16();
    circuit.horizontalMagnification = reader->readU8();
    circuit.verticalMagnification = reader->readU8();
    circuit.displayWidth = reader->readU16();
    circuit.displayHeight = reader->readU16();
    require(
      circuit.horizontalMagnification >= 1 &&
      circuit.horizontalMagnification <= 16 &&
      circuit.verticalMagnification >= 1 &&
      circuit.verticalMagnification <= 4,
      "GS display-circuit geometry is invalid");
  }
  display->videoTiming.activeCycles = reader->readU64();
  display->videoTiming.totalCycles = reader->readU64();
  display->modeRegister = reader->readU64();
  display->syncModeRegister = reader->readU64();
  display->backgroundColor = reader->readU64();
  display->interruptMaskRegister = reader->readU64();
  display->cycleInFrame = reader->readU64();
  display->frameBoundaries = reader->readU64();
  display->verticalBlank =
    reader->readBool("GS vertical-blank flag");
  display->oddField =
    reader->readBool("GS odd-field flag");
  display->vsyncInterrupt =
    reader->readBool("GS VSYNC interrupt flag");
  display->verticalBlankStarted =
    reader->readBool("GS vertical-blank-start event");
  display->verticalBlankEnded =
    reader->readBool("GS vertical-blank-end event");
  require(
    display->videoTiming.activeCycles != 0 &&
    display->videoTiming.activeCycles <
      display->videoTiming.totalCycles,
    "GS display timing is invalid");
  require(
    display->cycleInFrame <
      display->videoTiming.totalCycles,
    "GS display cycle is invalid");
  require(
    (display->modeRegister & ~UINT64_C(0xffff)) == 0 &&
    (display->syncModeRegister & ~UINT64_C(0x0f)) == 0 &&
    (display->backgroundColor &
     ~UINT64_C(0x00ffffff)) == 0 &&
    (display->interruptMaskRegister &
     ~GSInterruptMask::ALL) == 0,
    "GS display register state is invalid");
}

std::vector<std::uint8_t> NekoSystem::saveState() const
{
  return NekoSaveStateCodec::save(*this);
}

void NekoSystem::loadState(
  const std::vector<std::uint8_t> &state)
{
  NekoSaveStateCodec::load(this, state);
}
