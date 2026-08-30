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
}

VIFCommand VIF::processCode(std::uint32_t code)
{
  if (awaitingPayload())
  {
    throw std::runtime_error(
      "Cannot process a VIFcode while a payload is in progress.");
  }

  const VIFCommand command = decodeVIFCommand(code, type);
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
      break;
    case VIFCommandKind::MARK:
      markRegister = command.immediate;
      markFlag = true;
      break;
    default:
      throw std::runtime_error(
        "VIF command execution requires its owning subsystem.");
  }

  return command;
}

VIFStreamWord VIF::ingestWord(std::uint32_t word)
{
  VIFStreamWord streamWord;
  streamWord.raw = word;

  if (awaitingPayload())
  {
    consumePayloadWord(word);

    streamWord.kind = VIFStreamWordKind::Payload;
    streamWord.command = streamCommand;
    streamWord.payloadWordCount = streamPayloadWordCount;
    streamWord.payloadIndex =
      streamPayloadWordCount - streamPayloadWordsRemaining;

    --streamPayloadWordsRemaining;
    ++streamWordsIngested;
    streamWord.packetComplete = !awaitingPayload();
    return streamWord;
  }

  const VIFCommand command = decodeVIFCommand(word, type);
  validatePayloadAlignment(command);

  const std::uint32_t commandPayloadWordCount =
    payloadWordCount(command);
  preparePayload(command);

  if (!ownsPayload(command.kind))
  {
    processCode(word);
  }

  streamCommand = command;
  streamPayloadWordCount = commandPayloadWordCount;
  streamPayloadWordsRemaining = streamPayloadWordCount;
  ++streamWordsIngested;

  streamWord.kind = VIFStreamWordKind::Command;
  streamWord.command = streamCommand;
  streamWord.payloadWordCount = streamPayloadWordCount;
  streamWord.packetComplete = !awaitingPayload();

  if (ownsPayload(command.kind))
  {
    codeRegister = word;
  }

  return streamWord;
}

void VIF::preparePayload(const VIFCommand &command)
{
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

void VIF::consumePayloadWord(std::uint32_t word)
{
  if (streamCommand.kind != VIFCommandKind::MPG)
  {
    return;
  }

  if (!mpgLowerInstructionPending)
  {
    mpgLowerInstruction = word;
    mpgLowerInstructionPending = true;
    return;
  }

  constexpr std::uint32_t MPG_WORDS_PER_INSTRUCTION = 2;
  const std::uint32_t payloadWordIndex =
    streamPayloadWordCount - streamPayloadWordsRemaining;
  const std::uint32_t instructionIndex =
    payloadWordIndex / MPG_WORDS_PER_INSTRUCTION;
  vpu->writeMicroInstruction(
    streamCommand.immediate + instructionIndex,
    mpgLowerInstruction,
    word);
  mpgLowerInstructionPending = false;
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

std::uint32_t VIF::payloadWordsRemaining() const
{
  return streamPayloadWordsRemaining;
}

std::uint64_t VIF::wordsIngested() const
{
  return streamWordsIngested;
}
