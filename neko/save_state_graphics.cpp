#include "save_state_internal.hpp"

namespace
{
  constexpr std::uint32_t MAX_GIF_REGISTER_VALUES =
    0x7fffu * 16u;

  void writeGIFTag(
    SaveStateWriter *writer,
    const GIFTag &tag)
  {
    writer->writeFieldU16("loopCount", tag.loopCount);
    writer->writeFieldBool("endOfPacket", tag.endOfPacket);
    writer->writeFieldBool(
      "primitiveEnabled",
      tag.primitiveEnabled);
    writer->writeFieldU16("primitive", tag.primitive);
    writer->writeFieldU8(
      "format",
      static_cast<std::uint8_t>(tag.format));
    writer->writeFieldU8("registerCount", tag.registerCount);
    writer->writeFieldU64("registers", tag.registers);
  }

  GIFTag readGIFTag(SaveStateReader *reader)
  {
    const auto require =
      [reader](bool condition, const std::string &detail)
      {
        reader->requireField(condition, detail);
      };
    GIFTag tag;
    tag.loopCount = reader->readFieldU16("loopCount");
    tag.endOfPacket =
      reader->readFieldBool("endOfPacket");
    tag.primitiveEnabled =
      reader->readFieldBool("primitiveEnabled");
    tag.primitive = reader->readFieldU16("primitive");
    tag.format = readEnum<GIFDataFormat>(
      reader,
      static_cast<std::uint8_t>(GIFDataFormat::Disabled),
      "format");
    tag.registerCount = reader->readFieldU8("registerCount");
    tag.registers = reader->readFieldU64("registers");
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
    {
      auto tag = writer->scope("tag");
      writeGIFTag(writer, state.tag);
    }
    writer->writeFieldBool("waitingForTag", state.waitingForTag);
    writer->writeFieldBool("activePacket", state.activePacket);
    writer->writeFieldU32(
      "remainingQuadwords",
      state.remainingQuadwords);
    writer->writeFieldU32(
      "remainingRegisterValues",
      state.remainingRegisterValues);
    writer->writeFieldU16("currentLoop", state.currentLoop);
    writer->writeFieldU8("currentRegister", state.currentRegister);
    writer->writeFieldU32("qValue", state.qValue);
  }

  GIFDecoderState readGIFDecoderState(
    SaveStateReader *reader,
    const char *name)
  {
    const auto require =
      [reader](bool condition, const std::string &detail)
      {
        reader->requireField(condition, detail);
      };
    GIFDecoderState state;
    {
      auto tag = reader->scope("tag");
      state.tag = readGIFTag(reader);
    }
    state.waitingForTag =
      reader->readFieldBool("waitingForTag");
    state.activePacket =
      reader->readFieldBool("activePacket");
    state.remainingQuadwords =
      reader->readFieldU32("remainingQuadwords");
    state.remainingRegisterValues =
      reader->readFieldU32("remainingRegisterValues");
    state.currentLoop = reader->readFieldU16("currentLoop");
    state.currentRegister =
      reader->readFieldU8("currentRegister");
    state.qValue = reader->readFieldU32("qValue");
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
    writer->writeFieldU8(
      "type",
      static_cast<std::uint8_t>(primitive.type));
    writer->writeFieldBool(
      "gouraudShading",
      primitive.gouraudShading);
    writer->writeFieldBool(
      "textureMapping",
      primitive.textureMapping);
    writer->writeFieldBool("fogging", primitive.fogging);
    writer->writeFieldBool(
      "alphaBlending",
      primitive.alphaBlending);
    writer->writeFieldBool("antialiasing", primitive.antialiasing);
    writer->writeFieldBool(
      "fixedTextureCoordinates",
      primitive.fixedTextureCoordinates);
    writer->writeFieldU8("context", primitive.context);
    writer->writeFieldBool(
      "fixedFragmentValue",
      primitive.fixedFragmentValue);
  }

  GSPrimitive readGSPrimitive(SaveStateReader *reader)
  {
    const auto require =
      [reader](bool condition, const std::string &detail)
      {
        reader->requireField(condition, detail);
      };
    GSPrimitive primitive;
    primitive.type = readEnum<GSPrimitiveType>(
      reader,
      static_cast<std::uint8_t>(GSPrimitiveType::Sprite),
      "type");
    primitive.gouraudShading =
      reader->readFieldBool("gouraudShading");
    primitive.textureMapping =
      reader->readFieldBool("textureMapping");
    primitive.fogging = reader->readFieldBool("fogging");
    primitive.alphaBlending =
      reader->readFieldBool("alphaBlending");
    primitive.antialiasing =
      reader->readFieldBool("antialiasing");
    primitive.fixedTextureCoordinates =
      reader->readFieldBool("fixedTextureCoordinates");
    primitive.context = reader->readFieldU8("context");
    primitive.fixedFragmentValue =
      reader->readFieldBool("fixedFragmentValue");
    require(primitive.context < 2, "GS context is invalid");
    return primitive;
  }

  void writeGSColor(
    SaveStateWriter *writer,
    const GSColor &color)
  {
    writer->writeFieldU8("red", color.red);
    writer->writeFieldU8("green", color.green);
    writer->writeFieldU8("blue", color.blue);
    writer->writeFieldU8("alpha", color.alpha);
    writer->writeFieldU32("q", color.q);
  }

  GSColor readGSColor(SaveStateReader *reader)
  {
    GSColor color;
    color.red = reader->readFieldU8("red");
    color.green = reader->readFieldU8("green");
    color.blue = reader->readFieldU8("blue");
    color.alpha = reader->readFieldU8("alpha");
    color.q = reader->readFieldU32("q");
    return color;
  }

  void writeGSVertex(
    SaveStateWriter *writer,
    const GSVertexCoordinate &vertex)
  {
    writer->writeFieldU16("x", vertex.x);
    writer->writeFieldU16("y", vertex.y);
    writer->writeFieldU32("z", vertex.z);
  }

  GSVertexCoordinate readGSVertex(
    SaveStateReader *reader)
  {
    GSVertexCoordinate vertex;
    vertex.x = reader->readFieldU16("x");
    vertex.y = reader->readFieldU16("y");
    vertex.z = reader->readFieldU32("z");
    return vertex;
  }

  void writeGSTextureCoordinate(
    SaveStateWriter *writer,
    const GSTextureCoordinate &coordinate)
  {
    writer->writeFieldU32("s", coordinate.s);
    writer->writeFieldU32("t", coordinate.t);
    writer->writeFieldU16("u", coordinate.u);
    writer->writeFieldU16("v", coordinate.v);
  }

  GSTextureCoordinate readGSTextureCoordinate(
    SaveStateReader *reader)
  {
    GSTextureCoordinate coordinate;
    coordinate.s = reader->readFieldU32("s");
    coordinate.t = reader->readFieldU32("t");
    coordinate.u = reader->readFieldU16("u");
    coordinate.v = reader->readFieldU16("v");
    return coordinate;
  }

  void writeGSFrame(
    SaveStateWriter *writer,
    const GSFrame &frame)
  {
    writer->writeFieldU16("basePointer", frame.basePointer);
    writer->writeFieldU8("width", frame.width);
    writer->writeFieldU8(
      "pixelStorageMode",
      frame.pixelStorageMode);
    writer->writeFieldU32("drawingMask", frame.drawingMask);
  }

  GSFrame readGSFrame(SaveStateReader *reader)
  {
    GSFrame frame;
    frame.basePointer = reader->readFieldU16("basePointer");
    frame.width = reader->readFieldU8("width");
    frame.pixelStorageMode =
      reader->readFieldU8("pixelStorageMode");
    frame.drawingMask = reader->readFieldU32("drawingMask");
    return frame;
  }

  void writeGSScissor(
    SaveStateWriter *writer,
    const GSScissor &scissor)
  {
    writer->writeFieldU16("x0", scissor.x0);
    writer->writeFieldU16("x1", scissor.x1);
    writer->writeFieldU16("y0", scissor.y0);
    writer->writeFieldU16("y1", scissor.y1);
  }

  GSScissor readGSScissor(SaveStateReader *reader)
  {
    GSScissor scissor;
    scissor.x0 = reader->readFieldU16("x0");
    scissor.x1 = reader->readFieldU16("x1");
    scissor.y0 = reader->readFieldU16("y0");
    scissor.y1 = reader->readFieldU16("y1");
    return scissor;
  }

  void writeGSXYOffset(
    SaveStateWriter *writer,
    const GSXYOffset &offset)
  {
    writer->writeFieldU16("x", offset.x);
    writer->writeFieldU16("y", offset.y);
  }

  GSXYOffset readGSXYOffset(SaveStateReader *reader)
  {
    GSXYOffset offset;
    offset.x = reader->readFieldU16("x");
    offset.y = reader->readFieldU16("y");
    return offset;
  }

  void writeGSTest(
    SaveStateWriter *writer,
    const GSTest &test)
  {
    writer->writeFieldBool(
      "alphaTestEnabled",
      test.alphaTestEnabled);
    writer->writeFieldU8("alphaTest", test.alphaTest);
    writer->writeFieldU8("alphaReference", test.alphaReference);
    writer->writeFieldU8("alphaFail", test.alphaFail);
    writer->writeFieldBool(
      "destinationAlphaTestEnabled",
      test.destinationAlphaTestEnabled);
    writer->writeFieldBool(
      "destinationAlphaMode",
      test.destinationAlphaMode);
    writer->writeFieldBool(
      "depthTestEnabled",
      test.depthTestEnabled);
    writer->writeFieldU8("depthTest", test.depthTest);
  }

  GSTest readGSTest(SaveStateReader *reader)
  {
    const auto require =
      [reader](bool condition, const std::string &detail)
      {
        reader->requireField(condition, detail);
      };
    GSTest test;
    test.alphaTestEnabled =
      reader->readFieldBool("alphaTestEnabled");
    test.alphaTest = reader->readFieldU8("alphaTest");
    test.alphaReference =
      reader->readFieldU8("alphaReference");
    test.alphaFail = reader->readFieldU8("alphaFail");
    test.destinationAlphaTestEnabled =
      reader->readFieldBool("destinationAlphaTestEnabled");
    test.destinationAlphaMode =
      reader->readFieldBool("destinationAlphaMode");
    test.depthTestEnabled =
      reader->readFieldBool("depthTestEnabled");
    test.depthTest = reader->readFieldU8("depthTest");
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
    writer->writeFieldU8("source", alpha.source);
    writer->writeFieldU8("destination", alpha.destination);
    writer->writeFieldU8("alpha", alpha.alpha);
    writer->writeFieldU8("result", alpha.result);
    writer->writeFieldU8("fixedAlpha", alpha.fixedAlpha);
  }

  GSAlpha readGSAlpha(SaveStateReader *reader)
  {
    const auto require =
      [reader](bool condition, const std::string &detail)
      {
        reader->requireField(condition, detail);
      };
    GSAlpha alpha;
    alpha.source = reader->readFieldU8("source");
    alpha.destination = reader->readFieldU8("destination");
    alpha.alpha = reader->readFieldU8("alpha");
    alpha.result = reader->readFieldU8("result");
    alpha.fixedAlpha = reader->readFieldU8("fixedAlpha");
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
    writer->writeFieldU16("basePointer", depth.basePointer);
    writer->writeFieldU8(
      "pixelStorageMode",
      depth.pixelStorageMode);
    writer->writeFieldBool("drawingMasked", depth.drawingMasked);
  }

  GSDepthBuffer readGSDepthBuffer(SaveStateReader *reader)
  {
    GSDepthBuffer depth;
    depth.basePointer = reader->readFieldU16("basePointer");
    depth.pixelStorageMode =
      reader->readFieldU8("pixelStorageMode");
    depth.drawingMasked =
      reader->readFieldBool("drawingMasked");
    return depth;
  }

  void writeGSTexture(
    SaveStateWriter *writer,
    const GSTexture &texture)
  {
    writer->writeFieldU16("basePointer", texture.basePointer);
    writer->writeFieldU8("bufferWidth", texture.bufferWidth);
    writer->writeFieldU8(
      "pixelStorageMode",
      texture.pixelStorageMode);
    writer->writeFieldU8("widthExponent", texture.widthExponent);
    writer->writeFieldU8(
      "heightExponent",
      texture.heightExponent);
    writer->writeFieldBool("rgba", texture.rgba);
    writer->writeFieldU8("function", texture.function);
    writer->writeFieldU8(
      "maximumMipLevel",
      texture.maximumMipLevel);
    writer->writeFieldBool(
      "magnificationLinear",
      texture.magnificationLinear);
    writer->writeFieldU8(
      "minificationFilter",
      texture.minificationFilter);
  }

  GSTexture readGSTexture(SaveStateReader *reader)
  {
    const auto require =
      [reader](bool condition, const std::string &detail)
      {
        reader->requireField(condition, detail);
      };
    GSTexture texture;
    texture.basePointer = reader->readFieldU16("basePointer");
    texture.bufferWidth = reader->readFieldU8("bufferWidth");
    texture.pixelStorageMode =
      reader->readFieldU8("pixelStorageMode");
    texture.widthExponent =
      reader->readFieldU8("widthExponent");
    texture.heightExponent =
      reader->readFieldU8("heightExponent");
    texture.rgba = reader->readFieldBool("rgba");
    texture.function = reader->readFieldU8("function");
    texture.maximumMipLevel =
      reader->readFieldU8("maximumMipLevel");
    texture.magnificationLinear =
      reader->readFieldBool("magnificationLinear");
    texture.minificationFilter =
      reader->readFieldU8("minificationFilter");
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
    writer->writeFieldU8(
      "horizontal",
      static_cast<std::uint8_t>(clamp.horizontal));
    writer->writeFieldU8(
      "vertical",
      static_cast<std::uint8_t>(clamp.vertical));
    writer->writeFieldU16("minimumU", clamp.minimumU);
    writer->writeFieldU16("maximumU", clamp.maximumU);
    writer->writeFieldU16("minimumV", clamp.minimumV);
    writer->writeFieldU16("maximumV", clamp.maximumV);
  }

  GSTextureClamp readGSTextureClamp(
    SaveStateReader *reader)
  {
    GSTextureClamp clamp;
    clamp.horizontal = readEnum<GSTextureWrapMode>(
      reader,
      static_cast<std::uint8_t>(
        GSTextureWrapMode::RegionRepeat),
      "horizontal");
    clamp.vertical = readEnum<GSTextureWrapMode>(
      reader,
      static_cast<std::uint8_t>(
        GSTextureWrapMode::RegionRepeat),
      "vertical");
    clamp.minimumU = reader->readFieldU16("minimumU");
    clamp.maximumU = reader->readFieldU16("maximumU");
    clamp.minimumV = reader->readFieldU16("minimumV");
    clamp.maximumV = reader->readFieldU16("maximumV");
    return clamp;
  }

  void writeGSContext(
    SaveStateWriter *writer,
    const GSContext &context)
  {
    {
      auto field = writer->scope("frame");
      writeGSFrame(writer, context.frame);
    }
    {
      auto field = writer->scope("scissor");
      writeGSScissor(writer, context.scissor);
    }
    {
      auto field = writer->scope("offset");
      writeGSXYOffset(writer, context.offset);
    }
    {
      auto field = writer->scope("test");
      writeGSTest(writer, context.test);
    }
    {
      auto field = writer->scope("alpha");
      writeGSAlpha(writer, context.alpha);
    }
    writer->writeFieldBool("forceAlphaBit", context.forceAlphaBit);
    {
      auto field = writer->scope("depthBuffer");
      writeGSDepthBuffer(writer, context.depthBuffer);
    }
    {
      auto field = writer->scope("texture");
      writeGSTexture(writer, context.texture);
    }
    {
      auto field = writer->scope("textureClamp");
      writeGSTextureClamp(writer, context.textureClamp);
    }
  }

  GSContext readGSContext(SaveStateReader *reader)
  {
    GSContext context;
    {
      auto field = reader->scope("frame");
      context.frame = readGSFrame(reader);
    }
    {
      auto field = reader->scope("scissor");
      context.scissor = readGSScissor(reader);
    }
    {
      auto field = reader->scope("offset");
      context.offset = readGSXYOffset(reader);
    }
    {
      auto field = reader->scope("test");
      context.test = readGSTest(reader);
    }
    {
      auto field = reader->scope("alpha");
      context.alpha = readGSAlpha(reader);
    }
    context.forceAlphaBit =
      reader->readFieldBool("forceAlphaBit");
    {
      auto field = reader->scope("depthBuffer");
      context.depthBuffer = readGSDepthBuffer(reader);
    }
    {
      auto field = reader->scope("texture");
      context.texture = readGSTexture(reader);
    }
    {
      auto field = reader->scope("textureClamp");
      context.textureClamp = readGSTextureClamp(reader);
    }
    return context;
  }

  void writeGSImageTransfer(
    SaveStateWriter *writer,
    const GSImageTransfer &transfer)
  {
    writer->writeFieldU16(
      "sourceBasePointer",
      transfer.sourceBasePointer);
    writer->writeFieldU8(
      "sourceBufferWidth",
      transfer.sourceBufferWidth);
    writer->writeFieldU8(
      "sourcePixelStorageMode",
      transfer.sourcePixelStorageMode);
    writer->writeFieldU16(
      "destinationBasePointer",
      transfer.destinationBasePointer);
    writer->writeFieldU8(
      "destinationBufferWidth",
      transfer.destinationBufferWidth);
    writer->writeFieldU8(
      "destinationPixelStorageMode",
      transfer.destinationPixelStorageMode);
    writer->writeFieldU16("sourceX", transfer.sourceX);
    writer->writeFieldU16("sourceY", transfer.sourceY);
    writer->writeFieldU16("destinationX", transfer.destinationX);
    writer->writeFieldU16("destinationY", transfer.destinationY);
    writer->writeFieldU16("width", transfer.width);
    writer->writeFieldU16("height", transfer.height);
    writer->writeFieldU32(
      "transferredPixels",
      transfer.transferredPixels);
    writer->writeFieldU8(
      "direction",
      static_cast<std::uint8_t>(transfer.direction));
    writer->writeFieldBool("active", transfer.active);
  }

  GSImageTransfer readGSImageTransfer(
    SaveStateReader *reader)
  {
    const auto require =
      [reader](bool condition, const std::string &detail)
      {
        reader->requireField(condition, detail);
      };
    GSImageTransfer transfer;
    transfer.sourceBasePointer =
      reader->readFieldU16("sourceBasePointer");
    transfer.sourceBufferWidth =
      reader->readFieldU8("sourceBufferWidth");
    transfer.sourcePixelStorageMode =
      reader->readFieldU8("sourcePixelStorageMode");
    transfer.destinationBasePointer =
      reader->readFieldU16("destinationBasePointer");
    transfer.destinationBufferWidth =
      reader->readFieldU8("destinationBufferWidth");
    transfer.destinationPixelStorageMode =
      reader->readFieldU8("destinationPixelStorageMode");
    transfer.sourceX = reader->readFieldU16("sourceX");
    transfer.sourceY = reader->readFieldU16("sourceY");
    transfer.destinationX =
      reader->readFieldU16("destinationX");
    transfer.destinationY =
      reader->readFieldU16("destinationY");
    transfer.width = reader->readFieldU16("width");
    transfer.height = reader->readFieldU16("height");
    transfer.transferredPixels =
      reader->readFieldU32("transferredPixels");
    transfer.direction = readEnum<GSImageTransferDirection>(
      reader,
      static_cast<std::uint8_t>(
        GSImageTransferDirection::Deactivated),
      "direction");
    transfer.active =
      reader->readFieldBool("active");
    const std::uint64_t pixelCount =
      static_cast<std::uint64_t>(transfer.width) *
      transfer.height;
    require(
      transfer.transferredPixels <= pixelCount,
      "GS image-transfer progress is invalid");
    return transfer;
  }}

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
  writer->writeFieldU8(
    "currentPath",
    static_cast<std::uint8_t>(arbiter.currentPath));
  {
    auto paths = writer->scope("queuedPaths");
    for (std::size_t index = 0;
         index < arbiter.queuedPaths.size();
         ++index)
    {
      auto element = writer->element(index);
      writer->writeFieldBool("queued", arbiter.queuedPaths[index]);
    }
  }
  writer->writeFieldBool("vifPath3Mask", arbiter.vifPath3Mask);
  writer->writeFieldBool("modePath3Mask", arbiter.modePath3Mask);
  writer->writeFieldBool(
    "intermittentPath3",
    arbiter.intermittentPath3);
  writer->writeFieldBool("timedTransfers", arbiter.timedTransfers);
  writer->writeFieldBool(
    "interruptedPath3",
    arbiter.interruptedPath3);
  writer->writeFieldBool(
    "interruptQueuedPath2",
    arbiter.queuedPath2Interruption ==
    GIFPath3InterruptionPolicy::Interrupt);
  writer->writeFieldU8(
    "path3ImageSliceQuadwords",
    arbiter.path3ImageSliceQuadwords);
  writer->writeFieldU16("path3Count", arbiter.path3Count);
  writer->writeFieldU16("path3Tag", arbiter.path3Tag);
  writer->writeFieldU8(
    "remainingIdleCycles",
    arbiter.remainingIdleCycles);
  {
    auto state = writer->scope("suspendedPath3State");
    writeGIFDecoderState(writer, arbiter.suspendedPath3State);
  }
}

void NekoSaveStateCodec::readGIFArbiter(
  SaveStateReader *reader,
  GIFPathArbiter *arbiter)
{
  const auto require =
    [reader](bool condition, const std::string &detail)
    {
      reader->requireField(condition, detail);
    };
  arbiter->currentPath = readEnum<GIFPath>(
    reader,
    static_cast<std::uint8_t>(GIFPath::Path3),
    "currentPath");
  {
    auto paths = reader->scope("queuedPaths");
    for (std::size_t index = 0;
         index < arbiter->queuedPaths.size();
         ++index)
    {
      auto element = reader->element(index);
      arbiter->queuedPaths[index] =
        reader->readFieldBool("queued");
    }
  }
  arbiter->vifPath3Mask =
    reader->readFieldBool("vifPath3Mask");
  arbiter->modePath3Mask =
    reader->readFieldBool("modePath3Mask");
  arbiter->intermittentPath3 =
    reader->readFieldBool("intermittentPath3");
  arbiter->timedTransfers =
    reader->readFieldBool("timedTransfers");
  arbiter->interruptedPath3 =
    reader->readFieldBool("interruptedPath3");
  arbiter->queuedPath2Interruption =
    reader->readFieldBool("interruptQueuedPath2") ?
      GIFPath3InterruptionPolicy::Interrupt :
      GIFPath3InterruptionPolicy::Defer;
  arbiter->path3ImageSliceQuadwords =
    reader->readFieldU8("path3ImageSliceQuadwords");
  arbiter->path3Count = reader->readFieldU16("path3Count");
  arbiter->path3Tag = reader->readFieldU16("path3Tag");
  arbiter->remainingIdleCycles =
    reader->readFieldU8("remainingIdleCycles");
  {
    auto state = reader->scope("suspendedPath3State");
    arbiter->suspendedPath3State =
      readGIFDecoderState(reader, "suspended GIF PATH3 decoder");
  }
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
  writer->writeFieldBool("active", path.active);
  writer->writeFieldU16("qwordAddress", path.qwordAddress);
  writer->writeFieldU64(
    "transferredQuadwords",
    path.transferredQuadwords);
}

void NekoSaveStateCodec::readGIFPath1(
  SaveStateReader *reader,
  GIFPath1Transfer *path)
{
  const auto require =
    [reader](bool condition, const std::string &detail)
    {
      reader->requireField(condition, detail);
    };
  path->active =
    reader->readFieldBool("active");
  path->qwordAddress = reader->readFieldU16("qwordAddress");
  path->transferredQuadwords =
    reader->readFieldU64("transferredQuadwords");
  require(
    path->qwordAddress <
      path->vpu->dataMemorySize() / 16,
    "GIF PATH1 qword address is invalid");
}

void NekoSaveStateCodec::writeGIFPath3(
  SaveStateWriter *writer,
  const GIFPath3Transfer &path)
{
  writer->writeFieldU64(
    "submissionAttempts",
    path.submissionAttempts);
  writer->writeFieldU64(
    "transferredQuadwords",
    path.transferredQuadwords);
  writer->writeFieldU64("completedPackets", path.completedPackets);
  writer->writeFieldSize("fifoCount", path.guestFIFO.size());
  {
    auto fifo = writer->scope("guestFIFO");
    std::size_t quadwordIndex = 0;
    for (const GIFQuadword &quadword : path.guestFIFO)
    {
      auto quadwordElement = writer->element(quadwordIndex++);
      for (std::size_t wordIndex = 0;
           wordIndex < quadword.size();
           ++wordIndex)
      {
        auto wordElement = writer->element(wordIndex);
        writer->writeFieldU32("value", quadword[wordIndex]);
      }
    }
  }
}

void NekoSaveStateCodec::readGIFPath3(
  SaveStateReader *reader,
  GIFPath3Transfer *path)
{
  const auto require =
    [reader](bool condition, const std::string &detail)
    {
      reader->requireField(condition, detail);
    };
  path->submissionAttempts =
    reader->readFieldU64("submissionAttempts");
  path->transferredQuadwords =
    reader->readFieldU64("transferredQuadwords");
  path->completedPackets =
    reader->readFieldU64("completedPackets");
  const std::uint32_t fifoCount =
    reader->readFieldU32("fifoCount");
  require(fifoCount <= 16, "GIF FIFO size is invalid");
  std::deque<GIFQuadword> guestFIFO;
  {
    auto fifo = reader->scope("guestFIFO");
    for (std::uint32_t index = 0; index < fifoCount; ++index)
    {
      auto quadwordElement = reader->element(index);
      GIFQuadword quadword = {};
      for (std::size_t wordIndex = 0;
           wordIndex < quadword.size();
           ++wordIndex)
      {
        auto wordElement = reader->element(wordIndex);
        quadword[wordIndex] = reader->readFieldU32("value");
      }
      guestFIFO.push_back(quadword);
    }
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
  writer->writeFieldSize("registerCount", gs.registers.size());
  {
    auto registers = writer->scope("registers");
    for (std::size_t index = 0;
         index < gs.registers.size();
         ++index)
    {
      auto element = writer->element(index);
      writer->writeFieldU64("value", gs.registers[index]);
    }
  }
  {
    auto field = writer->scope("primitiveRegister");
    writeGSPrimitive(writer, gs.primitiveRegister);
  }
  {
    auto field = writer->scope("colorRegister");
    writeGSColor(writer, gs.colorRegister);
  }
  {
    auto field = writer->scope("vertexRegister");
    writeGSVertex(writer, gs.vertexRegister);
  }
  {
    auto field = writer->scope("textureCoordinateRegister");
    writeGSTextureCoordinate(
      writer,
      gs.textureCoordinateRegister);
  }
  {
    auto vertices = writer->scope("primitiveVertices");
    for (std::size_t index = 0;
         index < gs.primitiveVertices.size();
         ++index)
    {
      auto element = writer->element(index);
      writeGSVertex(writer, gs.primitiveVertices[index]);
    }
  }
  {
    auto colors = writer->scope("primitiveColors");
    for (std::size_t index = 0;
         index < gs.primitiveColors.size();
         ++index)
    {
      auto element = writer->element(index);
      writeGSColor(writer, gs.primitiveColors[index]);
    }
  }
  {
    auto coordinates =
      writer->scope("primitiveTextureCoordinates");
    for (std::size_t index = 0;
         index < gs.primitiveTextureCoordinates.size();
         ++index)
    {
      auto element = writer->element(index);
      writeGSTextureCoordinate(
        writer,
        gs.primitiveTextureCoordinates[index]);
    }
  }
  writer->writeFieldU64(
    "primitiveVertexCount",
    gs.primitiveVertexCount);
  writer->writeFieldU64("renderedPoints", gs.renderedPoints);
  writer->writeFieldU64("renderedLines", gs.renderedLines);
  writer->writeFieldU64("renderedSprites", gs.renderedSprites);
  writer->writeFieldU64(
    "renderedTriangles",
    gs.renderedTriangles);
  writer->writeFieldU64("writtenPixels", gs.writtenPixels);
  {
    auto contexts = writer->scope("contexts");
    for (std::size_t index = 0;
         index < gs.contexts.size();
         ++index)
    {
      auto element = writer->element(index);
      writeGSContext(writer, gs.contexts[index]);
    }
  }
  {
    auto transfer = writer->scope("transfer");
    writeGSImageTransfer(writer, gs.transfer);
  }
  writer->writeFieldBool(
    "reverseHostInterface",
    gs.reverseHostInterface);
  writer->writeFieldBool(
    "perPixelAlphaBlending",
    gs.perPixelAlphaBlending);
  writer->writeFieldSize("localMemorySize", gs.localMemory.size());
  writer->writeFieldRange(
    "localMemory",
    [&]()
    {
      for (std::uint32_t value : gs.localMemory)
      {
        writer->writeU32(value);
      }
    });
}

void NekoSaveStateCodec::readGS(
  SaveStateReader *reader,
  GS *gs)
{
  const auto require =
    [reader](bool condition, const std::string &detail)
    {
      reader->requireField(condition, detail);
    };
  const std::uint32_t registerCount =
    reader->readFieldU32("registerCount");
  require(
    registerCount == gs->registers.size(),
    "GS register-file size is invalid");
  {
    auto registers = reader->scope("registers");
    for (std::size_t index = 0;
         index < gs->registers.size();
         ++index)
    {
      auto element = reader->element(index);
      gs->registers[index] = reader->readFieldU64("value");
    }
  }
  {
    auto field = reader->scope("primitiveRegister");
    gs->primitiveRegister = readGSPrimitive(reader);
  }
  {
    auto field = reader->scope("colorRegister");
    gs->colorRegister = readGSColor(reader);
  }
  {
    auto field = reader->scope("vertexRegister");
    gs->vertexRegister = readGSVertex(reader);
  }
  {
    auto field = reader->scope("textureCoordinateRegister");
    gs->textureCoordinateRegister =
      readGSTextureCoordinate(reader);
  }
  {
    auto vertices = reader->scope("primitiveVertices");
    for (std::size_t index = 0;
         index < gs->primitiveVertices.size();
         ++index)
    {
      auto element = reader->element(index);
      gs->primitiveVertices[index] = readGSVertex(reader);
    }
  }
  {
    auto colors = reader->scope("primitiveColors");
    for (std::size_t index = 0;
         index < gs->primitiveColors.size();
         ++index)
    {
      auto element = reader->element(index);
      gs->primitiveColors[index] = readGSColor(reader);
    }
  }
  {
    auto coordinates =
      reader->scope("primitiveTextureCoordinates");
    for (std::size_t index = 0;
         index < gs->primitiveTextureCoordinates.size();
         ++index)
    {
      auto element = reader->element(index);
      gs->primitiveTextureCoordinates[index] =
        readGSTextureCoordinate(reader);
    }
  }
  const std::uint64_t primitiveVertexCount =
    reader->readFieldU64("primitiveVertexCount");
  require(
    primitiveVertexCount <= gs->primitiveVertices.size(),
    "GS primitive queue size is invalid");
  gs->primitiveVertexCount =
    static_cast<std::size_t>(primitiveVertexCount);
  gs->renderedPoints = reader->readFieldU64("renderedPoints");
  gs->renderedLines = reader->readFieldU64("renderedLines");
  gs->renderedSprites =
    reader->readFieldU64("renderedSprites");
  gs->renderedTriangles =
    reader->readFieldU64("renderedTriangles");
  gs->writtenPixels = reader->readFieldU64("writtenPixels");
  {
    auto contexts = reader->scope("contexts");
    for (std::size_t index = 0;
         index < gs->contexts.size();
         ++index)
    {
      auto element = reader->element(index);
      gs->contexts[index] = readGSContext(reader);
    }
  }
  {
    auto transfer = reader->scope("transfer");
    gs->transfer = readGSImageTransfer(reader);
  }
  gs->reverseHostInterface =
    reader->readFieldBool("reverseHostInterface");
  gs->perPixelAlphaBlending =
    reader->readFieldBool("perPixelAlphaBlending");
  const std::uint32_t localMemorySize =
    reader->readFieldU32("localMemorySize");
  require(
    localMemorySize == gs->localMemory.size(),
    "GS local-memory size is invalid");
  std::vector<std::uint32_t> localMemory;
  localMemory.reserve(localMemorySize);
  reader->readFieldRange(
    "localMemory",
    static_cast<std::size_t>(localMemorySize) *
      sizeof(std::uint32_t),
    [&]()
    {
      for (std::uint32_t index = 0;
           index < localMemorySize;
           ++index)
      {
        localMemory.push_back(reader->readU32());
      }
    });
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
  const GIFDMACChannel &channel,
  const DMACController &controller)
{
  const DMACChannelState &state = channel.channelState;
  writer->writeFieldU32(
    "channelControlRegister",
    state.channelControlRegister);
  writer->writeFieldU32(
    "memoryAddressRegister",
    state.memoryAddressRegister);
  writer->writeFieldU32(
    "quadwordCountRegister",
    state.quadwordCountRegister);
  writer->writeFieldU32(
    "tagAddressRegister",
    state.tagAddressRegister);
  {
    auto addresses = writer->scope("addressStackRegisters");
    for (std::size_t index = 0;
         index < state.addressStackRegisters.size();
         ++index)
    {
      auto element = writer->element(index);
      writer->writeFieldU32(
        "value",
        state.addressStackRegisters[index]);
    }
  }
  writer->writeFieldU32(
    "controllerControlRegister",
    controller.controlRegister);
  writer->writeFieldU32(
    "controllerStatusRegister",
    controller.statusRegister);
  writer->writeFieldU32(
    "controllerStatusMaskRegister",
    controller.statusMaskRegister);
  writer->writeFieldBool(
    "terminateAfterPacket",
    state.terminateAfterPacket);
  writer->writeFieldBool("path3Stalled", channel.path3Stalled);
  writer->writeFieldU8(
    "addressStackDepth",
    state.addressStackDepth);
  writer->writeFieldU64(
    "transferredQuadwords",
    channel.transferredQuadwords);
}

void NekoSaveStateCodec::readDMAC(
  SaveStateReader *reader,
  GIFDMACChannel *channel,
  DMACController *controller)
{
  const auto require =
    [reader](bool condition, const std::string &detail)
    {
      reader->requireField(condition, detail);
    };
  DMACChannelState &state = channel->channelState;
  state.channelControlRegister =
    reader->readFieldU32("channelControlRegister");
  state.memoryAddressRegister =
    reader->readFieldU32("memoryAddressRegister");
  state.quadwordCountRegister =
    reader->readFieldU32("quadwordCountRegister");
  state.tagAddressRegister =
    reader->readFieldU32("tagAddressRegister");
  {
    auto addresses = reader->scope("addressStackRegisters");
    for (std::size_t index = 0;
         index < state.addressStackRegisters.size();
         ++index)
    {
      auto element = reader->element(index);
      state.addressStackRegisters[index] =
        reader->readFieldU32("value");
    }
  }
  controller->controlRegister =
    reader->readFieldU32("controllerControlRegister");
  controller->statusRegister =
    reader->readFieldU32("controllerStatusRegister");
  controller->statusMaskRegister =
    reader->readFieldU32("controllerStatusMaskRegister");
  state.terminateAfterPacket =
    reader->readFieldBool("terminateAfterPacket");
  channel->path3Stalled =
    reader->readFieldBool("path3Stalled");
  state.addressStackDepth =
    reader->readFieldU8("addressStackDepth");
  channel->transferredQuadwords =
    reader->readFieldU64("transferredQuadwords");

  const std::uint32_t writableControl =
    GIFDMACChannelControl::FROM_MEMORY |
    GIFDMACChannelControl::MODE_MASK |
    GIFDMACChannelControl::ADDRESS_STACK_MASK |
    GIFDMACChannelControl::TAG_TRANSFER_ENABLE |
    GIFDMACChannelControl::TAG_INTERRUPT_ENABLE |
    GIFDMACChannelControl::START |
    GIFDMACChannelControl::TAG_MASK;
  const std::uint32_t mode =
    state.channelControlRegister &
    GIFDMACChannelControl::MODE_MASK;
  require(
    (state.channelControlRegister & ~writableControl) == 0 &&
    (mode == 0 || mode == GIFDMACChannelControl::CHAIN_MODE),
    "GIF DMAC channel control is invalid");
  require(
    state.quadwordCountRegister <= 0xffff,
    "GIF DMAC qword count is invalid");
  const std::uint32_t addresses[] = {
    state.memoryAddressRegister,
    state.tagAddressRegister,
    state.addressStackRegisters[0],
    state.addressStackRegisters[1]
  };
  for (std::uint32_t address : addresses)
  {
    require(
      (address & UINT32_C(0x8000000f)) == 0,
      "GIF DMAC address is invalid");
  }
  require(
    controller->controlRegister <=
      DMACControl::DMA_ENABLE,
    "DMAC global control is invalid");
  require(
    (controller->statusRegister &
     ~(DMACStatus::CHANNEL_1 |
       DMACStatus::CHANNEL_2 |
       DMACStatus::CHANNEL_8 |
       DMACStatus::CHANNEL_9)) == 0 &&
    (controller->statusMaskRegister &
     ~(DMACStatus::CHANNEL_1_MASK |
       DMACStatus::CHANNEL_2_MASK |
       DMACStatus::CHANNEL_8_MASK |
       DMACStatus::CHANNEL_9_MASK)) == 0,
    "DMAC status is invalid");
  require(
    state.addressStackDepth <=
      state.addressStackRegisters.size() &&
    ((state.channelControlRegister &
      GIFDMACChannelControl::ADDRESS_STACK_MASK) >> 4) ==
      state.addressStackDepth,
    "GIF DMAC address-stack state is invalid");
}

void NekoSaveStateCodec::writeVIF1DMAC(
  SaveStateWriter *writer,
  const VIF1DMACChannel &dmac)
{
  const DMACChannelState &state = dmac.channelState;
  writer->writeFieldU32(
    "channelControlRegister",
    state.channelControlRegister);
  writer->writeFieldU32(
    "memoryAddressRegister",
    state.memoryAddressRegister);
  writer->writeFieldU32(
    "quadwordCountRegister",
    state.quadwordCountRegister);
  writer->writeFieldU32(
    "tagAddressRegister",
    state.tagAddressRegister);
  {
    auto addresses = writer->scope("addressStackRegisters");
    for (std::size_t index = 0;
         index < state.addressStackRegisters.size();
         ++index)
    {
      auto element = writer->element(index);
      writer->writeFieldU32(
        "value",
        state.addressStackRegisters[index]);
    }
  }
  writer->writeFieldBool(
    "terminateAfterPacket",
    state.terminateAfterPacket);
  writer->writeFieldBool("vif1Stalled", dmac.vif1Stalled);
  writer->writeFieldU8(
    "addressStackDepth",
    state.addressStackDepth);
  writer->writeFieldU64(
    "transferredQuadwords",
    dmac.transferredQuadwords);
}

void NekoSaveStateCodec::readVIF1DMAC(
  SaveStateReader *reader,
  VIF1DMACChannel *dmac)
{
  const auto require =
    [reader](bool condition, const std::string &detail)
    {
      reader->requireField(condition, detail);
    };
  DMACChannelState &state = dmac->channelState;
  state.channelControlRegister =
    reader->readFieldU32("channelControlRegister");
  state.memoryAddressRegister =
    reader->readFieldU32("memoryAddressRegister");
  state.quadwordCountRegister =
    reader->readFieldU32("quadwordCountRegister");
  state.tagAddressRegister =
    reader->readFieldU32("tagAddressRegister");
  {
    auto addresses = reader->scope("addressStackRegisters");
    for (std::size_t index = 0;
         index < state.addressStackRegisters.size();
         ++index)
    {
      auto element = reader->element(index);
      state.addressStackRegisters[index] =
        reader->readFieldU32("value");
    }
  }
  state.terminateAfterPacket =
    reader->readFieldBool("terminateAfterPacket");
  dmac->vif1Stalled =
    reader->readFieldBool("vif1Stalled");
  state.addressStackDepth =
    reader->readFieldU8("addressStackDepth");
  dmac->transferredQuadwords =
    reader->readFieldU64("transferredQuadwords");

  const std::uint32_t writableControl =
    GIFDMACChannelControl::FROM_MEMORY |
    GIFDMACChannelControl::MODE_MASK |
    GIFDMACChannelControl::ADDRESS_STACK_MASK |
    GIFDMACChannelControl::TAG_TRANSFER_ENABLE |
    GIFDMACChannelControl::TAG_INTERRUPT_ENABLE |
    GIFDMACChannelControl::START |
    GIFDMACChannelControl::TAG_MASK;
  const std::uint32_t mode =
    state.channelControlRegister &
    GIFDMACChannelControl::MODE_MASK;
  require(
    (state.channelControlRegister & ~writableControl) == 0 &&
    ((state.channelControlRegister &
      GIFDMACChannelControl::FROM_MEMORY) != 0 ||
     (state.channelControlRegister &
      GIFDMACChannelControl::START) == 0) &&
    (mode == 0 || mode == GIFDMACChannelControl::CHAIN_MODE),
    "VIF1 DMAC channel control is invalid");
  require(
    state.quadwordCountRegister <= 0xffff,
    "VIF1 DMAC qword count is invalid");
  const std::uint32_t addresses[] = {
    state.memoryAddressRegister,
    state.tagAddressRegister,
    state.addressStackRegisters[0],
    state.addressStackRegisters[1]
  };
  for (std::uint32_t address : addresses)
  {
    require(
      (address & ~UINT32_C(0x7ffffff0)) == 0,
      "VIF1 DMAC address is invalid");
  }
  require(
    state.addressStackDepth <=
      state.addressStackRegisters.size() &&
    ((state.channelControlRegister &
      GIFDMACChannelControl::ADDRESS_STACK_MASK) >> 4) ==
      state.addressStackDepth,
    "VIF1 DMAC address-stack state is invalid");
}

void NekoSaveStateCodec::writeGSDisplay(
  SaveStateWriter *writer,
  const GSDisplay &display)
{
  writer->writeFieldSize("circuitCount", display.circuits.size());
  {
    auto circuits = writer->scope("circuits");
    for (std::size_t index = 0;
         index < display.circuits.size();
         ++index)
    {
      auto element = writer->element(index);
      const GSDisplay::Circuit &circuit =
        display.circuits[index];
      writer->writeFieldU16("basePointer", circuit.basePointer);
      writer->writeFieldU8("bufferWidth", circuit.bufferWidth);
      writer->writeFieldU8(
        "pixelStorageMode",
        circuit.pixelStorageMode);
      writer->writeFieldU16("sourceX", circuit.sourceX);
      writer->writeFieldU16("sourceY", circuit.sourceY);
      writer->writeFieldU8(
        "horizontalMagnification",
        circuit.horizontalMagnification);
      writer->writeFieldU8(
        "verticalMagnification",
        circuit.verticalMagnification);
      writer->writeFieldU16("displayWidth", circuit.displayWidth);
      writer->writeFieldU16(
        "displayHeight",
        circuit.displayHeight);
    }
  }
  writer->writeFieldU64(
    "activeCycles",
    display.videoTiming.activeCycles);
  writer->writeFieldU64(
    "totalCycles",
    display.videoTiming.totalCycles);
  writer->writeFieldU64("modeRegister", display.modeRegister);
  writer->writeFieldU64(
    "syncModeRegister",
    display.syncModeRegister);
  writer->writeFieldU64(
    "backgroundColor",
    display.backgroundColor);
  writer->writeFieldU64(
    "interruptMaskRegister",
    display.interruptMaskRegister);
  writer->writeFieldU64("cycleInFrame", display.cycleInFrame);
  writer->writeFieldU64(
    "frameBoundaries",
    display.frameBoundaries);
  writer->writeFieldBool("verticalBlank", display.verticalBlank);
  writer->writeFieldBool("oddField", display.oddField);
  writer->writeFieldBool(
    "vsyncInterrupt",
    display.vsyncInterrupt);
  writer->writeFieldBool(
    "verticalBlankStarted",
    display.verticalBlankStarted);
  writer->writeFieldBool(
    "verticalBlankEnded",
    display.verticalBlankEnded);
}

void NekoSaveStateCodec::readGSDisplay(
  SaveStateReader *reader,
  GSDisplay *display)
{
  const auto require =
    [reader](bool condition, const std::string &detail)
    {
      reader->requireField(condition, detail);
    };
  const std::uint32_t circuitCount =
    reader->readFieldU32("circuitCount");
  require(
    circuitCount == display->circuits.size(),
    "GS display-circuit count is invalid");
  {
    auto circuits = reader->scope("circuits");
    for (std::size_t index = 0;
         index < display->circuits.size();
         ++index)
    {
      auto element = reader->element(index);
      GSDisplay::Circuit &circuit = display->circuits[index];
      circuit.basePointer =
        reader->readFieldU16("basePointer");
      circuit.bufferWidth =
        reader->readFieldU8("bufferWidth");
      circuit.pixelStorageMode =
        reader->readFieldU8("pixelStorageMode");
      circuit.sourceX = reader->readFieldU16("sourceX");
      circuit.sourceY = reader->readFieldU16("sourceY");
      circuit.horizontalMagnification =
        reader->readFieldU8("horizontalMagnification");
      circuit.verticalMagnification =
        reader->readFieldU8("verticalMagnification");
      circuit.displayWidth =
        reader->readFieldU16("displayWidth");
      circuit.displayHeight =
        reader->readFieldU16("displayHeight");
      require(
        circuit.horizontalMagnification >= 1 &&
        circuit.horizontalMagnification <= 16 &&
        circuit.verticalMagnification >= 1 &&
        circuit.verticalMagnification <= 4,
        "GS display-circuit geometry is invalid");
    }
  }
  display->videoTiming.activeCycles =
    reader->readFieldU64("activeCycles");
  display->videoTiming.totalCycles =
    reader->readFieldU64("totalCycles");
  display->modeRegister = reader->readFieldU64("modeRegister");
  display->syncModeRegister =
    reader->readFieldU64("syncModeRegister");
  display->backgroundColor =
    reader->readFieldU64("backgroundColor");
  display->interruptMaskRegister =
    reader->readFieldU64("interruptMaskRegister");
  display->cycleInFrame =
    reader->readFieldU64("cycleInFrame");
  display->frameBoundaries =
    reader->readFieldU64("frameBoundaries");
  display->verticalBlank =
    reader->readFieldBool("verticalBlank");
  display->oddField =
    reader->readFieldBool("oddField");
  display->vsyncInterrupt =
    reader->readFieldBool("vsyncInterrupt");
  display->verticalBlankStarted =
    reader->readFieldBool("verticalBlankStarted");
  display->verticalBlankEnded =
    reader->readFieldBool("verticalBlankEnded");
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
