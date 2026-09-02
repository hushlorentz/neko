#include <algorithm>
#include <stdexcept>

#include "vif.hpp"
#include "vpu.hpp"

namespace
{
  constexpr std::uint16_t VIF_STMOD_MASK = 0x0003;
  constexpr std::uint32_t VIF_BITS_PER_WORD = 32;
  constexpr std::uint32_t VIF_MPG_WORDS_PER_INSTRUCTION = 2;
  constexpr std::uint32_t VIF_DIRECT_WORDS_PER_QUADWORD = 4;
  constexpr std::uint32_t VIF_STMASK_PAYLOAD_WORDS = 1;
  constexpr std::uint32_t VIF_VECTOR_REGISTER_WORDS = 4;
  constexpr std::uint32_t VIF_ZERO_DIRECT_COUNT = 65536;
  constexpr std::uint8_t VIF_UNPACK_VN_SHIFT = 2;
  constexpr std::uint8_t VIF_UNPACK_FIELD_MASK = 0x03;
  constexpr std::uint8_t VIF_UNPACK_V4_5_ELEMENT_BITS = 16;
  constexpr std::uint8_t VIF_UNPACK_V4_5_RGB_BITS = 5;
  constexpr std::uint8_t VIF_UNPACK_V4_5_ALPHA_BITS = 1;
  constexpr std::uint8_t VIF_UNPACK_V4_5_RGB_SHIFT = 3;
  constexpr std::uint8_t VIF_UNPACK_V4_5_ALPHA_SHIFT = 7;
  constexpr std::uint32_t VIF_QUADWORD_BYTES = 16;
  constexpr std::uint32_t VIF_MASK_BITS_PER_LANE = 2;
  constexpr std::uint32_t VIF_MASK_LANES_PER_CYCLE = 4;
  constexpr std::uint32_t VIF_MASK_CYCLE_LIMIT = 3;
  constexpr std::uint32_t VIF_MASK_INPUT = 0;
  constexpr std::uint32_t VIF_MASK_ROW = 1;
  constexpr std::uint32_t VIF_MASK_COLUMN = 2;
  constexpr std::uint32_t VIF_MASK_PROTECT = 3;
  constexpr std::uint8_t VIF_MODE_NORMAL = 0;
  constexpr std::uint8_t VIF_MODE_OFFSET = 1;
  constexpr std::uint8_t VIF_MODE_DIFFERENCE = 2;
  constexpr std::uint64_t VIF_MPG_ALIGNMENT_WORDS = 2;
  constexpr std::uint64_t VIF_DIRECT_ALIGNMENT_WORDS = 4;

  bool ownsPayload(VIFCommandKind kind)
  {
    switch (kind)
    {
      case VIFCommandKind::STMASK:
      case VIFCommandKind::STROW:
      case VIFCommandKind::STCOL:
      case VIFCommandKind::MPG:
      case VIFCommandKind::DIRECT:
      case VIFCommandKind::DIRECTHL:
      case VIFCommandKind::UNPACK:
        return true;
      default:
        return false;
    }
  }
}

VIF::VIF(VIFType type) : type(type)
{
}

VIFType VIF::unitType() const
{
  return type;
}

void VIF::attachVPU(VPU *attachedVPU)
{
  if (awaitingPayload())
  {
    throw std::runtime_error(
      "Cannot attach a VPU while a VIF payload is in progress.");
  }

  if (attachedVPU == nullptr)
  {
    throw std::invalid_argument("Cannot attach a null VPU.");
  }

  const bool unitMatches =
    (type == VIFType::VIF0 &&
     attachedVPU->unitType() == VPUType::VU0) ||
    (type == VIFType::VIF1 &&
     attachedVPU->unitType() == VPUType::VU1);
  if (!unitMatches)
  {
    throw std::invalid_argument(
      "VIF and VPU unit types must match.");
  }

  vpu = attachedVPU;
  vpu->setVIFRegisterSource(this);
}

void VIF::attachGIFDecoder(GIFDecoder *attachedGIFDecoder)
{
  if (awaitingPayload())
  {
    throw std::runtime_error(
      "Cannot attach a GIF decoder while a VIF payload is in progress.");
  }
  if (attachedGIFDecoder == nullptr)
  {
    throw std::invalid_argument("Cannot attach a null GIF decoder.");
  }
  if (type != VIFType::VIF1)
  {
    throw std::runtime_error(
      "Only VIF1 can attach to GIF PATH2.");
  }
  if (gifPathArbiter != nullptr &&
      !gifPathArbiter->pathsIdle())
  {
    throw std::runtime_error(
      "Cannot replace GIF routing during a transfer.");
  }

  ownedGIFPathArbiter.reset(
    new GIFPathArbiter(attachedGIFDecoder));
  gifPathArbiter = ownedGIFPathArbiter.get();
  gifPathArbiter->setPath3MaskedByVIF(path3Mask);
}

void VIF::attachGIFPathArbiter(
  GIFPathArbiter *attachedArbiter)
{
  if (awaitingPayload())
  {
    throw std::runtime_error(
      "Cannot attach a GIF path arbiter while a VIF payload is in progress.");
  }
  if (attachedArbiter == nullptr)
  {
    throw std::invalid_argument(
      "Cannot attach a null GIF path arbiter.");
  }
  if (type != VIFType::VIF1)
  {
    throw std::runtime_error(
      "Only VIF1 can attach to GIF PATH2.");
  }
  if (gifPathArbiter != nullptr &&
      !gifPathArbiter->pathsIdle())
  {
    throw std::runtime_error(
      "Cannot replace GIF routing during a transfer.");
  }

  ownedGIFPathArbiter.reset();
  gifPathArbiter = attachedArbiter;
  gifPathArbiter->setPath3MaskedByVIF(path3Mask);
}

VIFCommand VIF::processCode(std::uint32_t code)
{
  if (awaitingPayload())
  {
    throw std::runtime_error(
      "Cannot process a VIFcode while a payload is in progress.");
  }

  const VIFCommand command = decodeVIFCommand(code, type);
  if (interruptFlag &&
      command.kind != VIFCommandKind::MARK)
  {
    throw std::runtime_error(
      "VIF command processing is stalled by an interrupt.");
  }
  if (!commandReady(command))
  {
    throw std::runtime_error(
      "VIF command is waiting for synchronization.");
  }
  codeRegister = code;

  switch (command.kind)
  {
    case VIFCommandKind::NOP:
      break;
    case VIFCommandKind::STCYCL:
      cycleRegister = command.immediate;
      break;
    case VIFCommandKind::OFFSET:
      offsetRegister =
        command.immediate & VIFImmediateEncoding::AddressMask;
      dbf = false;
      topsRegister = baseRegister;
      break;
    case VIFCommandKind::BASE:
      baseRegister =
        command.immediate & VIFImmediateEncoding::AddressMask;
      break;
    case VIFCommandKind::ITOP:
      itopsRegister =
        command.immediate & VIFImmediateEncoding::AddressMask;
      break;
    case VIFCommandKind::STMOD:
      modeRegister = command.immediate & VIF_STMOD_MASK;
      break;
    case VIFCommandKind::MSKPATH3:
      path3Mask =
        (command.immediate & VIFImmediateEncoding::MSKPATH3Mask) != 0;
      if (gifPathArbiter != nullptr)
      {
        gifPathArbiter->setPath3MaskedByVIF(path3Mask);
      }
      break;
    case VIFCommandKind::MARK:
      markRegister = command.immediate;
      markFlag = true;
      break;
    case VIFCommandKind::FLUSHE:
    case VIFCommandKind::FLUSH:
    case VIFCommandKind::FLUSHA:
      if (vpu == nullptr)
      {
        throw std::runtime_error(
          "VIF synchronization requires an attached VPU.");
      }
      break;
    case VIFCommandKind::MSCAL:
    case VIFCommandKind::MSCALF:
    case VIFCommandKind::MSCNT:
      startMicroProgram(command);
      break;
    default:
      throw std::runtime_error(
        "VIF command execution requires its owning subsystem.");
  }

  if (command.interrupt)
  {
    interruptFlag = true;
  }
  return command;
}

void VIF::startMicroProgram(const VIFCommand &command)
{
  constexpr std::uint16_t MICRO_INSTRUCTION_SIZE = 8;

  if (vpu == nullptr)
  {
    throw std::runtime_error(
      "VIF microprogram execution requires an attached VPU.");
  }
  if (vpu->getState() == VPU_STATE_RUN)
  {
    throw std::runtime_error(
      "VIF microprogram execution is waiting for the VPU.");
  }

  std::uint16_t startAddress = 0;
  if (command.kind == VIFCommandKind::MSCNT)
  {
    if (!vpu->hasTerminationPosition())
    {
      throw std::runtime_error(
        "VIF MSCNT requires a previously completed microprogram.");
    }
    startAddress = vpu->programCounter();
  }
  else
  {
    const std::size_t instructionCapacity =
      vpu->microMemorySize() / MICRO_INSTRUCTION_SIZE;
    if (command.immediate >= instructionCapacity)
    {
      throw std::out_of_range(
        "VIF microprogram start is outside VU micro memory.");
    }
    startAddress = command.immediate * MICRO_INSTRUCTION_SIZE;
  }

  vpu->startMicroMode(startAddress);
  updateProgramStartRegisters();
}

void VIF::updateProgramStartRegisters()
{
  itopRegister = itopsRegister;
  if (type != VIFType::VIF1)
  {
    return;
  }

  topRegister = topsRegister;
  dbf = !dbf;
  topsRegister =
    (baseRegister + (dbf ? offsetRegister : 0)) &
    VIFImmediateEncoding::AddressMask;
}

VIFStreamWord VIF::ingestWord(std::uint32_t word)
{
  VIFStreamWord streamWord;
  streamWord.raw = word;

  if (awaitingPayload())
  {
    streamWord.kind = VIFStreamWordKind::Payload;
    streamWord.command = streamCommand;
    streamWord.payloadWordCount = streamPayloadWordCount;
    streamWord.payloadIndex =
      streamPayloadWordCount - streamPayloadWordsRemaining;
    if (!consumePayloadWord(word, &streamWord))
    {
      streamWord.stalled = true;
      return streamWord;
    }

    --streamPayloadWordsRemaining;
    ++streamWordsIngested;
    streamWord.packetComplete = !awaitingPayload();
    if (streamWord.packetComplete)
    {
      completePayloadCommand();
    }
    return streamWord;
  }

  const VIFCommand command = decodeVIFCommand(word, type);
  streamWord.kind = VIFStreamWordKind::Command;
  streamWord.command = command;
  if ((interruptFlag &&
       command.kind != VIFCommandKind::MARK) ||
      !commandReady(command))
  {
    streamWord.stalled = true;
    return streamWord;
  }

  validatePayloadAlignment(command);
  preparePayload(command);

  const std::uint32_t commandPayloadWordCount =
    payloadWordCount(command);

  if (!ownsPayload(command.kind))
  {
    processCode(word);
  }

  streamCommand = command;
  streamPayloadWordCount = commandPayloadWordCount;
  streamPayloadWordsRemaining = streamPayloadWordCount;
  ++streamWordsIngested;

  streamWord.command = streamCommand;
  streamWord.payloadWordCount = streamPayloadWordCount;
  streamWord.packetComplete = !awaitingPayload();

  if (ownsPayload(command.kind))
  {
    codeRegister = word;
  }
  if (command.kind == VIFCommandKind::UNPACK &&
      commandPayloadWordCount == 0)
  {
    executeUNPACK();
    completePayloadCommand();
  }

  return streamWord;
}

bool VIF::submitQuadword(const GIFQuadword &quadword)
{
  const std::size_t capacityWords = fifoCapacity() * 4;
  if (fifoWords.size() > capacityWords - quadword.size())
  {
    return false;
  }
  fifoWords.insert(
    fifoWords.end(),
    quadword.begin(),
    quadword.end());
  return true;
}

void VIF::advanceFIFO()
{
  while (!fifoWords.empty())
  {
    const VIFStreamWord result = ingestWord(fifoWords.front());
    if (result.stalled)
    {
      return;
    }
    fifoWords.pop_front();
  }
}

std::size_t VIF::fifoQuadwordCount() const
{
  return (fifoWords.size() + 3) / 4;
}

std::size_t VIF::fifoCapacity() const
{
  return type == VIFType::VIF0 ? 8 : 16;
}

void VIF::preparePayload(const VIFCommand &command)
{
  if (command.kind == VIFCommandKind::DIRECT ||
      command.kind == VIFCommandKind::DIRECTHL)
  {
    if (gifPathArbiter == nullptr)
    {
      throw std::runtime_error(
        "VIF DIRECT requires an attached GIF decoder.");
    }
    directQuadword.fill(0);
    return;
  }

  if (command.kind == VIFCommandKind::UNPACK)
  {
    if (vpu == nullptr)
    {
      throw std::runtime_error(
        "VIF UNPACK requires an attached VPU.");
    }
    if (writeLength() == 0)
    {
      throw std::runtime_error(
        "VIF UNPACK requires a nonzero write length.");
    }
    unpackPayload.clear();
    return;
  }

  if (command.kind != VIFCommandKind::MPG)
  {
    return;
  }

  constexpr std::size_t MICRO_INSTRUCTION_SIZE = 8;

  if (vpu == nullptr)
  {
    throw std::runtime_error(
      "VIF MPG requires an attached VPU.");
  }
  if (vpu->getState() == VPU_STATE_RUN)
  {
    throw std::runtime_error(
      "VIF MPG cannot upload while the VPU is running.");
  }

  const std::size_t instructionCapacity =
    vpu->microMemorySize() / MICRO_INSTRUCTION_SIZE;
  if (command.immediate > instructionCapacity ||
      command.count > instructionCapacity - command.immediate)
  {
    throw std::out_of_range(
      "VIF MPG transfer exceeds VU micro memory.");
  }

  mpgLowerInstructionPending = false;
}

bool VIF::consumePayloadWord(
  std::uint32_t word,
  VIFStreamWord *streamWord)
{
  const std::uint32_t payloadWordIndex =
    streamPayloadWordCount - streamPayloadWordsRemaining;

  switch (streamCommand.kind)
  {
    case VIFCommandKind::STMASK:
      maskRegister = word;
      return true;
    case VIFCommandKind::STROW:
      rowRegisters[payloadWordIndex] = word;
      return true;
    case VIFCommandKind::STCOL:
      columnRegisters[payloadWordIndex] = word;
      return true;
    case VIFCommandKind::UNPACK:
      unpackPayload.push_back(word);
      if (streamPayloadWordsRemaining == 1)
      {
        executeUNPACK();
      }
      return true;
    case VIFCommandKind::MPG:
      break;
    case VIFCommandKind::DIRECT:
    case VIFCommandKind::DIRECTHL:
    {
      const std::uint32_t quadwordWordIndex =
        payloadWordIndex % VIF_DIRECT_WORDS_PER_QUADWORD;
      directQuadword[quadwordWordIndex] = word;
      if (quadwordWordIndex ==
          VIF_DIRECT_WORDS_PER_QUADWORD - 1)
      {
        const GIFPathTransferResult transfer =
          gifPathArbiter->transferQuadword(
            GIFPath::Path2,
            directQuadword,
            streamCommand.kind != VIFCommandKind::DIRECTHL);
        if (!transfer.accepted)
        {
          return false;
        }
        streamWord->gifResult = transfer.decodeResult;
        streamWord->gifQuadwordDecoded = true;
      }
      return true;
    }
    default:
      return true;
  }

  if (!mpgLowerInstructionPending)
  {
    mpgLowerInstruction = word;
    mpgLowerInstructionPending = true;
    return true;
  }

  constexpr std::uint32_t MPG_WORDS_PER_INSTRUCTION = 2;
  const std::uint32_t instructionIndex =
    payloadWordIndex / MPG_WORDS_PER_INSTRUCTION;
  vpu->writeMicroInstruction(
    streamCommand.immediate + instructionIndex,
    mpgLowerInstruction,
    word);
  mpgLowerInstructionPending = false;
  return true;
}

bool VIF::commandReady(const VIFCommand &command) const
{
  const bool microprogramEnded =
    vpu == nullptr || vpu->getState() != VPU_STATE_RUN;
  const bool path1And2Ended =
    gifPathArbiter == nullptr ||
    gifPathArbiter->pathsIdle(false);
  const bool allPathsEnded =
    gifPathArbiter == nullptr ||
    gifPathArbiter->pathsIdle(true);

  switch (command.kind)
  {
    case VIFCommandKind::FLUSHE:
    case VIFCommandKind::MSCAL:
    case VIFCommandKind::MSCNT:
    case VIFCommandKind::MPG:
      return microprogramEnded;
    case VIFCommandKind::FLUSH:
    case VIFCommandKind::MSCALF:
      return microprogramEnded && path1And2Ended;
    case VIFCommandKind::FLUSHA:
      return microprogramEnded && allPathsEnded;
    default:
      return true;
  }
}

void VIF::completePayloadCommand()
{
  if (streamCommand.interrupt)
  {
    interruptFlag = true;
  }
}

void VIF::executeUNPACK()
{
  const std::uint32_t componentCount =
    ((streamCommand.command >> VIF_UNPACK_VN_SHIFT) &
      VIF_UNPACK_FIELD_MASK) + 1;
  const std::uint32_t definedLaneCount =
    componentCount == 1
      ? VIF_MASK_LANES_PER_CYCLE
      : componentCount;
  const bool filling = writeLength() > cycleLength();
  const std::uint32_t memoryQuadwords =
    vpu->dataMemorySize() / VIF_QUADWORD_BYTES;
  const std::uint32_t baseAddress =
    streamCommand.address +
    (streamCommand.addTops ? topsRegister : 0);
  std::uint32_t inputVector = 0;

  for (std::uint32_t outputVector = 0;
       outputVector < streamCommand.count;
       ++outputVector)
  {
    const std::uint32_t cycleIndex =
      outputVector % writeLength();
    const bool consumesInput =
      !filling || cycleIndex < cycleLength();
    std::array<std::uint32_t, 4> input = {};
    if (consumesInput)
    {
      input = unpackInputVector(inputVector);
      ++inputVector;
    }

    std::uint32_t destinationOffset = outputVector;
    if (!filling)
    {
      destinationOffset =
        cycleLength() * (outputVector / writeLength()) +
        cycleIndex;
    }
    const std::uint32_t destination =
      (baseAddress + destinationOffset) % memoryQuadwords;
    std::array<std::uint32_t, 4> result =
      vpu->readDataQuadword(destination);
    const std::uint32_t maskCycle =
      std::min(cycleIndex, VIF_MASK_CYCLE_LIMIT);

    for (std::uint32_t lane = 0;
         lane < VIF_MASK_LANES_PER_CYCLE;
         ++lane)
    {
      std::uint32_t mask = VIF_MASK_INPUT;
      if (streamCommand.masked)
      {
        const std::uint32_t maskIndex =
          maskCycle * VIF_MASK_LANES_PER_CYCLE + lane;
        mask =
          (maskRegister >>
           (maskIndex * VIF_MASK_BITS_PER_LANE)) &
          VIF_UNPACK_FIELD_MASK;
      }

      switch (mask)
      {
        case VIF_MASK_INPUT:
        {
          std::uint32_t value =
            lane < definedLaneCount ? input[lane] : 0;
          if (modeRegister == VIF_MODE_OFFSET ||
              modeRegister == VIF_MODE_DIFFERENCE)
          {
            value += rowRegisters[lane];
          }
          result[lane] = value;
          if (modeRegister == VIF_MODE_DIFFERENCE)
          {
            rowRegisters[lane] = value;
          }
          break;
        }
        case VIF_MASK_ROW:
          result[lane] = rowRegisters[lane];
          break;
        case VIF_MASK_COLUMN:
          result[lane] = columnRegisters[maskCycle];
          break;
        case VIF_MASK_PROTECT:
          break;
      }
    }

    vpu->writeDataQuadword(destination, result);
  }
}

std::uint32_t VIF::unpackElement(
  std::uint32_t bitOffset,
  std::uint8_t bitCount) const
{
  const std::uint32_t wordIndex = bitOffset / VIF_BITS_PER_WORD;
  const std::uint32_t wordBit = bitOffset % VIF_BITS_PER_WORD;
  std::uint64_t packed = unpackPayload[wordIndex];
  if (wordBit + bitCount > VIF_BITS_PER_WORD)
  {
    packed |=
      static_cast<std::uint64_t>(unpackPayload[wordIndex + 1]) <<
      VIF_BITS_PER_WORD;
  }
  packed >>= wordBit;

  if (bitCount == VIF_BITS_PER_WORD)
  {
    return static_cast<std::uint32_t>(packed);
  }

  const std::uint32_t mask =
    (UINT32_C(1) << bitCount) - 1;
  const std::uint32_t value = packed & mask;
  if (streamCommand.unpackFormat == VIFUnpackFormat::V4_5 ||
      streamCommand.unsignedData ||
      (value & (UINT32_C(1) << (bitCount - 1))) == 0)
  {
    return value;
  }
  return value | ~mask;
}

std::array<std::uint32_t, 4> VIF::unpackInputVector(
  std::uint32_t inputVectorIndex) const
{
  const std::uint8_t format =
    streamCommand.command & VIF_UNPACK_FIELD_MASK;
  const std::uint32_t componentCount =
    ((streamCommand.command >> VIF_UNPACK_VN_SHIFT) &
      VIF_UNPACK_FIELD_MASK) + 1;
  std::array<std::uint32_t, 4> input = {};

  if (streamCommand.unpackFormat == VIFUnpackFormat::V4_5)
  {
    const std::uint32_t vectorBit =
      inputVectorIndex * VIF_UNPACK_V4_5_ELEMENT_BITS;
    input[0] =
      unpackElement(vectorBit, VIF_UNPACK_V4_5_RGB_BITS) <<
      VIF_UNPACK_V4_5_RGB_SHIFT;
    input[1] =
      unpackElement(
        vectorBit + VIF_UNPACK_V4_5_RGB_BITS,
        VIF_UNPACK_V4_5_RGB_BITS) <<
      VIF_UNPACK_V4_5_RGB_SHIFT;
    input[2] =
      unpackElement(
        vectorBit + VIF_UNPACK_V4_5_RGB_BITS * 2,
        VIF_UNPACK_V4_5_RGB_BITS) <<
      VIF_UNPACK_V4_5_RGB_SHIFT;
    input[3] =
      unpackElement(
        vectorBit + VIF_UNPACK_V4_5_RGB_BITS * 3,
        VIF_UNPACK_V4_5_ALPHA_BITS) <<
      VIF_UNPACK_V4_5_ALPHA_SHIFT;
    return input;
  }

  const std::uint8_t componentBits =
    VIF_BITS_PER_WORD >> format;
  const std::uint32_t vectorBit =
    inputVectorIndex * componentCount * componentBits;
  for (std::uint32_t component = 0;
       component < componentCount;
       ++component)
  {
    input[component] = unpackElement(
      vectorBit + component * componentBits,
      componentBits);
  }

  if (componentCount == 1)
  {
    input.fill(input[0]);
  }
  return input;
}

std::uint32_t VIF::payloadWordCount(const VIFCommand &command) const
{
  switch (command.kind)
  {
    case VIFCommandKind::STMASK:
      return VIF_STMASK_PAYLOAD_WORDS;
    case VIFCommandKind::STROW:
    case VIFCommandKind::STCOL:
      return VIF_VECTOR_REGISTER_WORDS;
    case VIFCommandKind::MPG:
      return command.count * VIF_MPG_WORDS_PER_INSTRUCTION;
    case VIFCommandKind::DIRECT:
    case VIFCommandKind::DIRECTHL:
    {
      const std::uint32_t quadwordCount =
        command.immediate == 0
          ? VIF_ZERO_DIRECT_COUNT
          : command.immediate;
      return quadwordCount * VIF_DIRECT_WORDS_PER_QUADWORD;
    }
    case VIFCommandKind::UNPACK:
    {
      const std::uint8_t format = command.command &
        VIF_UNPACK_FIELD_MASK;
      const std::uint32_t componentCount =
        ((command.command >> VIF_UNPACK_VN_SHIFT) &
          VIF_UNPACK_FIELD_MASK) + 1;
      const std::uint32_t componentBits =
        VIF_BITS_PER_WORD >> format;

      std::uint32_t inputVectorCount = command.count;
      if (writeLength() > cycleLength())
      {
        const std::uint32_t completeBlocks =
          command.count / writeLength();
        const std::uint32_t partialBlock =
          command.count % writeLength();
        inputVectorCount =
          cycleLength() * completeBlocks +
          std::min<std::uint32_t>(partialBlock, cycleLength());
      }

      const std::uint32_t payloadBits =
        inputVectorCount * componentCount * componentBits;
      return
        (payloadBits + VIF_BITS_PER_WORD - 1) /
        VIF_BITS_PER_WORD;
    }
    default:
      return 0;
  }
}

void VIF::validatePayloadAlignment(const VIFCommand &command) const
{
  if (command.kind == VIFCommandKind::MPG &&
      (streamWordsIngested + 1) % VIF_MPG_ALIGNMENT_WORDS != 0)
  {
    throw std::runtime_error(
      "VIF MPG payload is not 64-bit aligned.");
  }

  if ((command.kind == VIFCommandKind::DIRECT ||
       command.kind == VIFCommandKind::DIRECTHL) &&
      (streamWordsIngested + 1) % VIF_DIRECT_ALIGNMENT_WORDS != 0)
  {
    throw std::runtime_error(
      "VIF DIRECT payload is not 128-bit aligned.");
  }
}

std::uint16_t VIF::cycle() const
{
  return cycleRegister;
}

std::uint8_t VIF::cycleLength() const
{
  return cycleRegister & 0xff;
}

std::uint8_t VIF::writeLength() const
{
  return cycleRegister >> 8;
}

std::uint8_t VIF::mode() const
{
  return modeRegister;
}

std::uint32_t VIF::mask() const
{
  return maskRegister;
}

std::uint32_t VIF::row(std::size_t index) const
{
  if (index >= rowRegisters.size())
  {
    throw std::out_of_range("VIF row register index is outside range.");
  }
  return rowRegisters[index];
}

std::uint32_t VIF::column(std::size_t index) const
{
  if (index >= columnRegisters.size())
  {
    throw std::out_of_range(
      "VIF column register index is outside range.");
  }
  return columnRegisters[index];
}

std::uint16_t VIF::top() const
{
  return topRegister;
}

std::uint16_t VIF::itop() const
{
  return itopRegister;
}

std::uint16_t VIF::itops() const
{
  return itopsRegister;
}

std::uint16_t VIF::base() const
{
  return baseRegister;
}

std::uint16_t VIF::offset() const
{
  return offsetRegister;
}

std::uint16_t VIF::tops() const
{
  return topsRegister;
}

std::uint16_t VIF::mark() const
{
  return markRegister;
}

bool VIF::doubleBufferFlag() const
{
  return dbf;
}

bool VIF::path3Masked() const
{
  return path3Mask;
}

bool VIF::markDetected() const
{
  return markFlag;
}

std::uint32_t VIF::lastCode() const
{
  return codeRegister;
}

bool VIF::awaitingPayload() const
{
  return streamPayloadWordsRemaining != 0;
}

bool VIF::interruptPending() const
{
  return interruptFlag;
}

void VIF::clearInterrupt()
{
  interruptFlag = false;
}

std::uint32_t VIF::payloadWordsRemaining() const
{
  return streamPayloadWordsRemaining;
}

std::uint64_t VIF::wordsIngested() const
{
  return streamWordsIngested;
}
