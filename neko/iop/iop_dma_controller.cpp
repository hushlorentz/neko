#include "iop_dma_controller.hpp"

#include <cassert>
#include <stdexcept>
#include <string>

#include "iop_sif1_dma_channel.hpp"
#include "iop_sif0_dma_channel.hpp"
#include "spu2_controller.hpp"
#include "sio2_controller.hpp"

namespace
{
  constexpr IOPWord SPU_START = 0x01000201;
  constexpr IOPWord SPU_STOP = 0x201;
  constexpr IOPWord SPU_BLOCK_SIZE = 16;
  constexpr IOPWord SIO2_INPUT_START = 0x01000201;
  constexpr IOPWord SIO2_INPUT_BCR = 0x10024;
  constexpr IOPWord SIO2_INPUT_WORDS = SIO2Controller::DMA_INPUT_BYTES / 4;
  constexpr std::array<IOPDMAChannel, 5> GRANT_CHANNELS = {{
    IOPDMAChannel::SPU0, IOPDMAChannel::SPU1, IOPDMAChannel::SIF0,
    IOPDMAChannel::SIF1, IOPDMAChannel::SIO2Input
  }};

  const char *channelDescription(IOPDMAChannel channel)
  {
    switch (channel)
    {
      case IOPDMAChannel::SPU0: return "SPU0";
      case IOPDMAChannel::SPU1: return "SPU1";
      case IOPDMAChannel::SIF0: return "SIF0";
      case IOPDMAChannel::SIF1: return "SIF1";
      case IOPDMAChannel::SIO2Input: return "SIO2 input";
    }
    throw std::out_of_range("Unsupported IOP DMA channel.");
  }

  const char *failureDescription(IOPDMAFailure failure)
  {
    switch (failure)
    {
      case IOPDMAFailure::None: return "unspecified transfer";
      case IOPDMAFailure::UnalignedHeader: return "unaligned header";
      case IOPDMAFailure::DestinationWrite: return "physical destination write";
      case IOPDMAFailure::DescriptorRead: return "physical descriptor read";
      case IOPDMAFailure::SourceRead: return "physical source read";
    }
    throw std::logic_error("Unknown IOP DMA failure.");
  }
}

IOPDMATransferError::IOPDMATransferError(const IOPDMABeatResult &result) :
  std::runtime_error(std::string("IOP ") +
    channelDescription(result.channel) + " DMA rejected " +
    failureDescription(result.failure) +
    " at address " + std::to_string(result.address) +
    " with value " + std::to_string(result.value) +
    " (bus status " + std::to_string(static_cast<unsigned>(result.status)) + ")."),
  rejectedBeat(result)
{
}

const IOPDMABeatResult &IOPDMATransferError::result() const
{
  return rejectedBeat;
}

IOPDMAController::IOPDMAController(IOPInterruptController &controller) :
  interrupts(controller)
{
}

std::size_t IOPDMAController::bankIndex(IOPDMABank bank)
{
  switch (bank)
  {
    case IOPDMABank::Primary:
      return 0;
    case IOPDMABank::Secondary:
      return 1;
  }
  throw std::out_of_range("Unsupported IOP DMA bank.");
}

IOPDMABank IOPDMAController::channelBank(IOPDMAChannel channel)
{
  switch (channel)
  {
    case IOPDMAChannel::SPU0: return IOPDMABank::Primary;
    case IOPDMAChannel::SPU1:
    case IOPDMAChannel::SIF0:
    case IOPDMAChannel::SIF1:
    case IOPDMAChannel::SIO2Input: return IOPDMABank::Secondary;
  }
  throw std::out_of_range("Unsupported IOP DMA channel.");
}

std::uint8_t IOPDMAController::channelSlot(IOPDMAChannel channel)
{
  switch (channel)
  {
    case IOPDMAChannel::SPU0:
      return 4;
    case IOPDMAChannel::SPU1:
      return 0;
    case IOPDMAChannel::SIF0:
      return 2;
    case IOPDMAChannel::SIF1:
      return 3;
    case IOPDMAChannel::SIO2Input:
      return 4;
  }
  throw std::out_of_range("Unsupported IOP DMA channel.");
}

IOPWord IOPDMAController::priority(IOPDMABank bank) const
{
  return banks[bankIndex(bank)].priority;
}

void IOPDMAController::writePriority(IOPDMABank bank, IOPWord value)
{
  banks[bankIndex(bank)].priority = value;
}

std::uint8_t IOPDMAController::activeFlags(std::size_t index) const
{
  const auto &bank = banks[index];
  const auto mask = static_cast<std::uint8_t>((bank.control >> 16) & 0x7f);
  // Selected SIF slots bypass their stored masks in the provisional profile.
  const auto effectiveMask = index == 1 ? mask | 0x0c : mask;
  return static_cast<std::uint8_t>(bank.flags & effectiveMask);
}

IOPWord IOPDMAController::interruptStatus(IOPDMABank selected) const
{
  const auto index = bankIndex(selected);
  const auto &bank = banks[index];
  const bool summary = (bank.control & 0x8000) != 0 ||
    ((bank.control & 0x800000) != 0 && activeFlags(index) != 0);
  return bank.control | (static_cast<IOPWord>(bank.flags) << 24) |
    (summary ? UINT32_C(0x80000000) : 0);
}

void IOPDMAController::writeInterrupt(IOPDMABank selected, IOPWord value)
{
  auto &bank = banks[bankIndex(selected)];
  bank.control = value & 0x00ffffff;
  bank.flags &= static_cast<std::uint8_t>(~((value >> 24) & 0x7f));
  publishInterrupt();
}

IOPWord IOPDMAController::auxiliaryControl() const
{
  return auxiliaryControlRegister;
}

void IOPDMAController::writeAuxiliaryControl(IOPWord value)
{
  auxiliaryControlRegister = value;
}

IOPWord IOPDMAController::sio2MemoryAddress(IOPSIO2DMAChannel channel) const
{
  return sio2Channels.at(static_cast<std::size_t>(channel)).memoryAddressRegister;
}

IOPDMAWriteOutcome IOPDMAController::writeSIO2MemoryAddress(
  IOPSIO2DMAChannel channel, IOPWord value)
{
  auto &registers = sio2Channels.at(static_cast<std::size_t>(channel));
  return registers.writeAddress(registers.memoryAddressRegister, value);
}

IOPWord IOPDMAController::sio2BlockControl(IOPSIO2DMAChannel channel) const
{
  return sio2Channels.at(static_cast<std::size_t>(channel)).blockControlRegister;
}

IOPDMAWriteOutcome IOPDMAController::writeSIO2BlockControl(
  IOPSIO2DMAChannel channel, IOPWord value)
{
  return sio2Channels.at(static_cast<std::size_t>(channel)).writeBlockControl(value);
}

std::uint16_t IOPDMAController::sio2BlockSize(IOPSIO2DMAChannel channel) const
{
  return static_cast<std::uint16_t>(sio2BlockControl(channel));
}

IOPDMAWriteOutcome IOPDMAController::writeSIO2BlockSize(
  IOPSIO2DMAChannel channel, std::uint16_t value)
{
  return writeSIO2BlockControl(channel, (sio2BlockControl(channel) & 0xffff0000) | value);
}

std::uint16_t IOPDMAController::sio2BlockCount(IOPSIO2DMAChannel channel) const
{
  return static_cast<std::uint16_t>(sio2BlockControl(channel) >> 16);
}

IOPDMAWriteOutcome IOPDMAController::writeSIO2BlockCount(
  IOPSIO2DMAChannel channel, std::uint16_t value)
{
  return writeSIO2BlockControl(channel,
    (sio2BlockControl(channel) & 0xffff) | (IOPWord{value} << 16));
}

bool IOPDMAController::validSIO2StoppedControl(IOPSIO2DMAChannel channel, IOPWord value)
{
  if (channel == IOPSIO2DMAChannel::Output)
    value &= ~IOPWord{0x40000000};
  return value == 0 || value == 0x200 || value == 0x201;
}

IOPWord IOPDMAController::sio2ChannelControl(IOPSIO2DMAChannel channel) const
{
  return sio2Channels.at(static_cast<std::size_t>(channel)).channelControlRegister;
}

IOPDMAWriteOutcome IOPDMAController::writeSIO2ChannelControl(
  IOPSIO2DMAChannel channel, IOPWord value)
{
  auto &registers = sio2Channels.at(static_cast<std::size_t>(channel));
  if (!validSIO2StoppedControl(channel, value))
    return IOPDMAWriteOutcome::UnsupportedControl;
  if (registers.active())
    return IOPDMAWriteOutcome::ActiveTransfer;
  registers.channelControlRegister = value;
  return IOPDMAWriteOutcome::Completed;
}

IOPDMAWriteOutcome IOPDMAController::writeSIO2ChannelControl(
  IOPSIO2DMAChannel channel, IOPWord value, const IOPBus &bus, SIO2Controller &sio2)
{
  validateSIO2Wiring(bus, sio2);
  auto &programming = sio2Channels.at(static_cast<std::size_t>(channel));
  if (channel == IOPSIO2DMAChannel::Input && value == SIO2_INPUT_START)
  {
    if (programming.active())
      return IOPDMAWriteOutcome::Completed;
    if (programming.blockControlRegister != SIO2_INPUT_BCR || !sio2.beginDMAInput())
      return IOPDMAWriteOutcome::UnsupportedControl;
    sio2InputWordsTransferred = 0;
    programming.channelControlRegister = value;
    return IOPDMAWriteOutcome::Completed;
  }
  if (!validSIO2StoppedControl(channel, value))
  {
    return IOPDMAWriteOutcome::UnsupportedControl;
  }
  if (programming.active())
  {
    sio2.cancelDMAInput();
    sio2InputWordsTransferred = 0;
  }
  programming.channelControlRegister = value;
  return IOPDMAWriteOutcome::Completed;
}

bool IOPDMAController::sio2InputActive() const
{
  return sio2Channels[0].active();
}

IOPWord IOPDMAController::sio2InputRemainingWords() const
{
  return sio2InputActive() ? SIO2_INPUT_WORDS - sio2InputWordsTransferred : 0;
}

bool IOPDMAController::validSIO2Continuation(const SIO2Controller &sio2) const
{
  for (const auto &channel : sio2Channels)
    if (!IOPDMAChannelState::validMemoryAddress(channel.memoryAddressRegister))
      return false;
  if (!validSIO2StoppedControl(IOPSIO2DMAChannel::Output,
        sio2Channels[1].channelControlRegister) ||
      !sio2.hasValidTransferState())
    return false;
  if (!sio2InputActive())
    return validSIO2StoppedControl(IOPSIO2DMAChannel::Input,
        sio2Channels[0].channelControlRegister) &&
      sio2InputWordsTransferred == 0 && sio2.ingressPhase != SIO2IngressPhase::DMAOpen;
  return sio2Channels[0].channelControlRegister == SIO2_INPUT_START &&
    sio2Channels[0].blockControlRegister == SIO2_INPUT_BCR &&
    sio2InputWordsTransferred < SIO2_INPUT_WORDS &&
    sio2.ingressPhase == SIO2IngressPhase::DMAOpen &&
    sio2.transmitInputCount == sio2InputWordsTransferred * 4;
}

void IOPDMAController::validateSIO2Wiring(
  const IOPBus &bus, const SIO2Controller &sio2) const
{
  if (bus.dmaController != this || bus.sio2 != &sio2)
    throw std::invalid_argument("IOP SIO2 DMA grant requires the attached owners.");
}

IOPDMABeatResult IOPDMAController::inspectSIO2Input(
  const IOPBus &bus, const SIO2Controller &sio2) const
{
  validateSIO2Wiring(bus, sio2);
  if (!validSIO2Continuation(sio2))
    throw std::logic_error("IOP SIO2 DMA continuation is outside the bounded input profile.");
  IOPDMABeatResult result;
  result.channel = IOPDMAChannel::SIO2Input;
  result.address = sio2MemoryAddress(IOPSIO2DMAChannel::Input);
  if (!sio2InputActive() || !channelEnabled(result.channel) || !sio2.canAcceptDMAWord())
    return result;
  const auto read = bus.readDMAC32(result.address);
  result.value = read.value;
  result.status = read.status;
  if (read.status != IOPBusStatus::Completed)
  {
    result.outcome = IOPDMABeatOutcome::Rejected;
    result.failure = IOPDMAFailure::SourceRead;
  }
  else
    result.outcome = sio2InputRemainingWords() == 1 ?
      IOPDMABeatOutcome::Completed : IOPDMABeatOutcome::Accepted;
  return result;
}

IOPDMABeatResult IOPDMAController::clockSIO2Input(
  const IOPBus &bus, SIO2Controller &sio2)
{
  const auto result = inspectSIO2Input(bus, sio2);
  if (result.outcome == IOPDMABeatOutcome::Wait ||
      result.outcome == IOPDMABeatOutcome::Rejected)
    return result;
  sio2.acceptDMAWord(result.value);
  auto &channel = sio2Channels[0];
  channel.memoryAddressRegister = (channel.memoryAddressRegister + 4) & 0x00ffffff;
  ++sio2InputWordsTransferred;
  if (result.outcome == IOPDMABeatOutcome::Completed)
  {
    sio2.finishDMAInput();
    channel.blockControlRegister -= IOPWord{1} << 16;
    channel.channelControlRegister = 0x201;
    sio2InputWordsTransferred = 0;
    signalChannelCompletion(result.channel);
  }
  advanceGrantCursor(result.channel);
  return result;
}

IOPWord IOPDMAController::spuMemoryAddress(std::size_t core) const
{
  return spuChannels.at(core).registers.memoryAddressRegister;
}

IOPDMAWriteOutcome IOPDMAController::writeSPUMemoryAddress(
  std::size_t core, IOPWord value)
{
  auto &registers = spuChannels.at(core).registers;
  return registers.writeAddress(registers.memoryAddressRegister, value);
}

IOPWord IOPDMAController::spuBlockControl(std::size_t core) const
{
  return spuChannels.at(core).registers.blockControlRegister;
}

IOPDMAWriteOutcome IOPDMAController::writeSPUBlockControl(std::size_t core, IOPWord value)
{
  return spuChannels.at(core).registers.writeBlockControl(value);
}

std::uint16_t IOPDMAController::spuBlockSize(std::size_t core) const
{
  return static_cast<std::uint16_t>(spuBlockControl(core));
}

IOPDMAWriteOutcome IOPDMAController::writeSPUBlockSize(std::size_t core, std::uint16_t value)
{
  return writeSPUBlockControl(core, (spuBlockControl(core) & 0xffff0000) | value);
}

std::uint16_t IOPDMAController::spuBlockCount(std::size_t core) const
{
  return static_cast<std::uint16_t>(spuBlockControl(core) >> 16);
}

IOPDMAWriteOutcome IOPDMAController::writeSPUBlockCount(std::size_t core, std::uint16_t value)
{
  return writeSPUBlockControl(core, (spuBlockControl(core) & 0xffff) | (IOPWord{value} << 16));
}

IOPDMAChannel IOPDMAController::spuChannel(std::size_t core)
{
  switch (core)
  {
    case 0: return IOPDMAChannel::SPU0;
    case 1: return IOPDMAChannel::SPU1;
  }
  throw std::out_of_range("Unsupported IOP SPU DMA core.");
}

IOPWord IOPDMAController::spuChannelControl(std::size_t core) const
{
  return spuChannels.at(core).registers.channelControlRegister;
}

IOPDMAWriteOutcome IOPDMAController::writeSPUChannelControl(
  std::size_t core, IOPWord value, const IOPBus &bus, SPU2Controller &spu)
{
  validateSPUWiring(bus, spu);
  auto &registers = spuChannels.at(core).registers;
  if (value == SPU_START)
  {
    return startSPUTransfer(core, bus, spu);
  }
  if (value != 0 && value != SPU_STOP)
  {
    return IOPDMAWriteOutcome::UnsupportedControl;
  }
  stopSPUTransfer(core, bus, spu);
  registers.channelControlRegister = value;
  return IOPDMAWriteOutcome::Completed;
}

bool IOPDMAController::spuActive(std::size_t core) const
{
  return spuChannels.at(core).registers.active();
}

IOPWord IOPDMAController::spuRemainingWords(std::size_t core) const
{
  return spuActive(core) ?
    spuBlockCount(core) * SPU_BLOCK_SIZE - spuChannels[core].blockWordsTransferred : 0;
}

bool IOPDMAController::validSPUContinuation(
  std::size_t core, const SPU2Controller &spu) const
{
  const auto &channel = spuChannels.at(core);
  const auto control = spuChannelControl(core);
  if (!IOPDMAChannelState::validMemoryAddress(spuMemoryAddress(core)) ||
      channel.blockWordsTransferred >= SPU_BLOCK_SIZE)
  {
    return false;
  }
  if (!spuActive(core))
  {
    return (control == 0 || control == SPU_STOP) && channel.blockWordsTransferred == 0;
  }
  const auto &input = spu.inputStates[core];
  return control == SPU_START && spuBlockSize(core) == SPU_BLOCK_SIZE &&
    spuBlockCount(core) != 0 && input.writeOpen &&
    spu.hasValidInputState() &&
    (spuRemainingWords(core) + input.fillWords) % SPU2Controller::INPUT_HALF_WORD_COUNT == 0;
}

void IOPDMAController::validateSPUWiring(
  const IOPBus &bus, const SPU2Controller &spu) const
{
  if (bus.dmaController != this || bus.spu2 != &spu)
  {
    throw std::invalid_argument("IOP SPU DMA grant requires the attached owners.");
  }
}

IOPDMAWriteOutcome IOPDMAController::startSPUTransfer(
  std::size_t core, const IOPBus &bus, SPU2Controller &spu)
{
  validateSPUWiring(bus, spu);
  auto &channel = spuChannels.at(core);
  if (channel.registers.active())
  {
    return IOPDMAWriteOutcome::Completed;
  }
  if (spuBlockSize(core) != SPU_BLOCK_SIZE || spuBlockCount(core) == 0 ||
      spuBlockCount(core) % 16 != 0 || spu.readAutoDMAWriteSelection(core) == 0)
  {
    return IOPDMAWriteOutcome::UnsupportedControl;
  }
  const bool opened = spu.beginInputWrite(core);
  assert(opened);
  (void)opened;
  channel.blockWordsTransferred = 0;
  channel.registers.channelControlRegister = SPU_START;
  return IOPDMAWriteOutcome::Completed;
}

void IOPDMAController::stopSPUTransfer(
  std::size_t core, const IOPBus &bus, SPU2Controller &spu)
{
  validateSPUWiring(bus, spu);
  auto &channel = spuChannels.at(core);
  channel.registers.channelControlRegister = SPU_STOP;
  channel.blockWordsTransferred = 0;
  spu.cancelInputWrite(core);
}

IOPDMABeatResult IOPDMAController::inspectSPU(
  std::size_t core, const IOPBus &bus, const SPU2Controller &spu) const
{
  validateSPUWiring(bus, spu);
  IOPDMABeatResult result;
  result.channel = spuChannel(core);
  if (!validSPUContinuation(core, spu))
  {
    throw std::logic_error("IOP SPU DMA continuation is outside the bounded profile.");
  }
  if (!channelEnabled(result.channel) || !spuActive(core) ||
      !spu.inputWriteRequested(core))
  {
    return result;
  }
  result.address = spuMemoryAddress(core);
  const auto read = bus.readDMAC32(result.address);
  result.value = read.value;
  result.status = read.status;
  if (read.status != IOPBusStatus::Completed)
  {
    result.outcome = IOPDMABeatOutcome::Rejected;
    result.failure = IOPDMAFailure::SourceRead;
  }
  else
  {
    result.outcome = spuRemainingWords(core) == 1 ?
      IOPDMABeatOutcome::Completed : IOPDMABeatOutcome::Accepted;
  }
  return result;
}

IOPDMABeatResult IOPDMAController::clockSPU(
  std::size_t core, const IOPBus &bus, SPU2Controller &spu)
{
  const auto result = inspectSPU(core, bus, spu);
  if (result.outcome == IOPDMABeatOutcome::Wait ||
      result.outcome == IOPDMABeatOutcome::Rejected)
  {
    return result;
  }
  const bool accepted = spu.writeInputWord(core, result.value);
  assert(accepted);
  (void)accepted;
  auto &channel = spuChannels[core];
  auto &registers = channel.registers;
  registers.memoryAddressRegister = (registers.memoryAddressRegister + 4) & 0x00ffffff;
  if (++channel.blockWordsTransferred == SPU_BLOCK_SIZE)
  {
    channel.blockWordsTransferred = 0;
    registers.blockControlRegister -= IOPWord{1} << 16;
  }
  if (result.outcome == IOPDMABeatOutcome::Completed)
  {
    stopSPUTransfer(core, bus, spu);
    signalChannelCompletion(result.channel);
  }
  advanceGrantCursor(result.channel);
  return result;
}

IOPDMAWriteOutcome IOPDMAController::writeDPCR3Initialization(IOPWord value) const
{
  return value == 0x777 ? IOPDMAWriteOutcome::Completed :
    IOPDMAWriteOutcome::UnsupportedControl;
}

IOPWord IOPDMAController::channelRegisterInitialization() const
{
  return 0;
}

IOPDMAWriteOutcome IOPDMAController::writeChannelRegisterInitialization(
  IOPWord value) const
{
  return value == 0 ? IOPDMAWriteOutcome::Completed :
    IOPDMAWriteOutcome::UnsupportedControl;
}

IOPDMAWriteOutcome IOPDMAController::writeSupplementalInitialization(
  IOPWord value) const
{
  return value == 0 ? IOPDMAWriteOutcome::Completed :
    IOPDMAWriteOutcome::UnsupportedControl;
}

bool IOPDMAController::channelEnabled(IOPDMAChannel channel) const
{
  return (priority(channelBank(channel)) & (IOPWord{8} << (4 * channelSlot(channel)))) != 0;
}

std::uint8_t IOPDMAController::channelPriority(IOPDMAChannel channel) const
{
  return static_cast<std::uint8_t>(
    (priority(channelBank(channel)) >> (4 * channelSlot(channel))) & 7);
}

std::size_t IOPDMAController::grantChannelIndex(IOPDMAChannel channel)
{
  switch (channel)
  {
    case IOPDMAChannel::SPU0: return 0;
    case IOPDMAChannel::SPU1: return 1;
    case IOPDMAChannel::SIF0: return 2;
    case IOPDMAChannel::SIF1: return 3;
    case IOPDMAChannel::SIO2Input: return 4;
  }
  throw std::out_of_range("Unsupported IOP DMA grant channel.");
}

void IOPDMAController::advanceGrantCursor(IOPDMAChannel channel)
{
  nextChannel = GRANT_CHANNELS[(grantChannelIndex(channel) + 1) % GRANT_CHANNELS.size()];
}

std::array<IOPDMAChannel, 5> IOPDMAController::grantOrder() const
{
  std::array<IOPDMAChannel, 5> order;
  const auto start = grantChannelIndex(nextChannel);
  for (std::size_t index = 0; index < order.size(); ++index)
  {
    order[index] = GRANT_CHANNELS[(start + index) % order.size()];
  }
  // Stable insertion sort keeps cyclic tie order without allocating on a grant.
  for (std::size_t index = 1; index < order.size(); ++index)
  {
    const auto channel = order[index];
    const auto priority = channelPriority(channel);
    auto destination = index;
    while (destination != 0 && channelPriority(order[destination - 1]) > priority)
    {
      order[destination] = order[destination - 1];
      --destination;
    }
    order[destination] = channel;
  }
  return order;
}

IOPDMAChannel IOPDMAController::tieBreakChannel() const
{
  return nextChannel;
}

std::array<IOPDMAChannel, 2> IOPDMAController::sifGrantOrder() const
{
  std::array<IOPDMAChannel, 2> order;
  std::size_t index = 0;
  for (const auto channel : grantOrder())
  {
    if (channel == IOPDMAChannel::SIF0 || channel == IOPDMAChannel::SIF1)
    {
      order[index++] = channel;
    }
  }
  return order;
}

IOPDMAChannel IOPDMAController::sifTieBreakChannel() const
{
  return nextChannel == IOPDMAChannel::SIF1 ? IOPDMAChannel::SIF1 : IOPDMAChannel::SIF0;
}

void IOPDMAController::validateSIFWiring(
  const IOPSIF1DMAChannel &channel, const IOPBus &bus, const SIF &sif) const
{
  if (bus.dmaController != this || bus.sif1DMA != &channel ||
      bus.sifComponent != &sif)
  {
    throw std::invalid_argument("IOP SIF DMA grant requires the attached owners.");
  }
}

IOPDMABeatResult IOPDMAController::inspectSIF(
  const IOPSIF1DMAChannel &channel, const IOPBus &bus, const SIF &sif) const
{
  validateSIFWiring(channel, bus, sif);
  if (!channelEnabled(IOPDMAChannel::SIF1) || !channel.active())
  {
    return {};
  }
  return channel.prepareBeat(bus, sif).result;
}

IOPDMABeatResult IOPDMAController::clockSIF(
  IOPSIF1DMAChannel &channel, IOPBus &bus, SIF &sif)
{
  validateSIFWiring(channel, bus, sif);
  if (!channelEnabled(IOPDMAChannel::SIF1) || !channel.active())
  {
    return {};
  }
  const auto result = channel.transferBeat(bus, sif);
  if (result.outcome == IOPDMABeatOutcome::Accepted ||
      result.outcome == IOPDMABeatOutcome::Completed)
  {
    advanceGrantCursor(result.channel);
  }
  if (result.outcome == IOPDMABeatOutcome::Completed)
  {
    signalChannelCompletion(IOPDMAChannel::SIF1);
  }
  return result;
}

void IOPDMAController::signalChannelCompletion(IOPDMAChannel channel)
{
  auto &bank = banks[bankIndex(channelBank(channel))];
  const auto flag = static_cast<std::uint8_t>(1 << channelSlot(channel));
  if ((channel == IOPDMAChannel::SPU0 || channel == IOPDMAChannel::SPU1 ||
       channel == IOPDMAChannel::SIO2Input) &&
      (bank.control & (IOPWord{flag} << 16)) == 0)
  {
    return;
  }
  bank.flags |= flag;
  publishInterrupt();
}

void IOPDMAController::validateSIFWiring(
  const IOPSIF0DMAChannel &channel, const IOPBus &bus, const SIF &sif) const
{
  if (bus.dmaController != this || bus.sif0DMA != &channel ||
      bus.sifComponent != &sif)
  {
    throw std::invalid_argument("IOP SIF DMA grant requires the attached owners.");
  }
}

IOPDMABeatResult IOPDMAController::inspectSIF(
  const IOPSIF0DMAChannel &channel, const IOPBus &bus, const SIF &sif) const
{
  validateSIFWiring(channel, bus, sif);
  if (!channelEnabled(IOPDMAChannel::SIF0) || !channel.active())
  {
    IOPDMABeatResult result;
    result.channel = IOPDMAChannel::SIF0;
    return result;
  }
  return channel.prepareBeat(bus, sif).result;
}

IOPDMABeatResult IOPDMAController::clockSIF(
  IOPSIF0DMAChannel &channel, IOPBus &bus, SIF &sif)
{
  validateSIFWiring(channel, bus, sif);
  if (!channelEnabled(IOPDMAChannel::SIF0) || !channel.active())
  {
    IOPDMABeatResult result;
    result.channel = IOPDMAChannel::SIF0;
    return result;
  }
  const auto result = channel.transferBeat(bus, sif);
  if (result.outcome == IOPDMABeatOutcome::Accepted ||
      result.outcome == IOPDMABeatOutcome::Completed)
  {
    advanceGrantCursor(result.channel);
  }
  if (result.outcome == IOPDMABeatOutcome::Completed)
  {
    signalChannelCompletion(IOPDMAChannel::SIF0);
  }
  return result;
}

bool IOPDMAController::interruptPending() const
{
  for (std::size_t index = 0; index < banks.size(); ++index)
  {
    if ((banks[index].control & 0x8000) != 0 || activeFlags(index) != 0)
    {
      return true;
    }
  }
  return false;
}

void IOPDMAController::publishInterrupt()
{
  if (interruptPending())
  {
    interrupts.assertSource(IOPInterruptSource::DMAC);
  }
}
