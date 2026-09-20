#include "save_state_internal.hpp"

namespace
{
  constexpr std::uint32_t MAX_GIF_REGISTER_VALUES =
    0x7fffu * 16u;

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
    arbiter.queuedPath2Interruption ==
    GIFPath3InterruptionPolicy::Interrupt);
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
  arbiter->queuedPath2Interruption =
    reader->readBool("GIF PATH2 interruption flag") ?
      GIFPath3InterruptionPolicy::Interrupt :
      GIFPath3InterruptionPolicy::Defer;
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
  const GIFDMACChannel &channel,
  const DMACController &controller)
{
  const DMACChannelState &state = channel.channelState;
  writer->writeU32(state.channelControlRegister);
  writer->writeU32(state.memoryAddressRegister);
  writer->writeU32(state.quadwordCountRegister);
  writer->writeU32(state.tagAddressRegister);
  for (std::uint32_t value : state.addressStackRegisters)
  {
    writer->writeU32(value);
  }
  writer->writeU32(controller.controlRegister);
  writer->writeU32(controller.statusRegister);
  writer->writeU32(controller.statusMaskRegister);
  writer->writeBool(state.terminateAfterPacket);
  writer->writeBool(channel.path3Stalled);
  writer->writeU8(state.addressStackDepth);
  writer->writeU64(channel.transferredQuadwords);
}

void NekoSaveStateCodec::readDMAC(
  SaveStateReader *reader,
  GIFDMACChannel *channel,
  DMACController *controller)
{
  DMACChannelState &state = channel->channelState;
  state.channelControlRegister = reader->readU32();
  state.memoryAddressRegister = reader->readU32();
  state.quadwordCountRegister = reader->readU32();
  state.tagAddressRegister = reader->readU32();
  for (std::uint32_t &value : state.addressStackRegisters)
  {
    value = reader->readU32();
  }
  controller->controlRegister = reader->readU32();
  controller->statusRegister = reader->readU32();
  controller->statusMaskRegister = reader->readU32();
  state.terminateAfterPacket =
    reader->readBool("GIF DMAC termination flag");
  channel->path3Stalled =
    reader->readBool("GIF DMAC PATH3 stall flag");
  state.addressStackDepth = reader->readU8();
  channel->transferredQuadwords = reader->readU64();

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
       DMACStatus::CHANNEL_2)) == 0 &&
    (controller->statusMaskRegister &
     ~(DMACStatus::CHANNEL_1_MASK |
       DMACStatus::CHANNEL_2_MASK)) == 0,
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
  writer->writeU32(state.channelControlRegister);
  writer->writeU32(state.memoryAddressRegister);
  writer->writeU32(state.quadwordCountRegister);
  writer->writeU32(state.tagAddressRegister);
  for (std::uint32_t address : state.addressStackRegisters)
  {
    writer->writeU32(address);
  }
  writer->writeBool(state.terminateAfterPacket);
  writer->writeBool(dmac.vif1Stalled);
  writer->writeU8(state.addressStackDepth);
  writer->writeU64(dmac.transferredQuadwords);
}

void NekoSaveStateCodec::readVIF1DMAC(
  SaveStateReader *reader,
  VIF1DMACChannel *dmac)
{
  DMACChannelState &state = dmac->channelState;
  state.channelControlRegister = reader->readU32();
  state.memoryAddressRegister = reader->readU32();
  state.quadwordCountRegister = reader->readU32();
  state.tagAddressRegister = reader->readU32();
  for (std::uint32_t &address : state.addressStackRegisters)
  {
    address = reader->readU32();
  }
  state.terminateAfterPacket =
    reader->readBool("VIF1 DMAC termination flag");
  dmac->vif1Stalled =
    reader->readBool("VIF1 DMAC stall flag");
  state.addressStackDepth = reader->readU8();
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
