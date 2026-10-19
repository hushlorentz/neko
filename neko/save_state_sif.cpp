#include "save_state_internal.hpp"

#include <cassert>
#include <type_traits>

std::uint64_t NekoSystem::sifStateHash() const
{
  return NekoSaveStateCodec::hashSIF(*this);
}

std::uint64_t NekoSaveStateCodec::hashSIF(const NekoSystem &system)
{
  SaveStateHashWriter writer;
  writeSIFState(&writer, system.sifComponent);
  return writer.value();
}

template<typename Writer>
void NekoSaveStateCodec::writeSIFState(Writer *writer, const SIF &sif)
{
  writer->writeFieldU32("mscom", sif.mscom);
  writer->writeFieldU32("smcom", sif.smcom);
  writer->writeFieldU32("msflag", sif.msflag);
  writer->writeFieldU32("smflag", sif.smflag);
  writer->writeFieldU32("control", sif.controlRegister);
  writer->writeFieldU32("iopDoorbell", sif.iopDoorbellRegister);
  for (const auto direction : {SIFDirection::SIF0, SIFDirection::SIF1})
  {
    auto stream = writer->scope(
      direction == SIFDirection::SIF0 ? "sif0" : "sif1");
    const auto count = sif.wordCount(direction);
    writer->writeFieldU8("wordCount", static_cast<std::uint8_t>(count));
    auto words = writer->scope("words");
    for (std::size_t index = 0; index < count; ++index)
    {
      auto word = writer->element(index);
      writer->writeFieldU32("value", sif.wordAt(direction, index));
    }
  }
  {
    auto latch = writer->scope("packingLatch");
    for (std::size_t index = 0; index < sif.packingLatch.size(); ++index)
    {
      auto word = writer->element(index);
      writer->writeFieldU32("value", sif.packingLatch[index]);
    }
  }
  writer->writeFieldU8("packingCursor", sif.packingCursor);
}

void NekoSaveStateCodec::writeSIF(
  SaveStateWriter *writer, const NekoSystem &system)
{
  if (system.eeBusComponent.sifComponent != &system.sifComponent ||
      system.iopBusComponent.sifComponent != &system.sifComponent ||
      system.eeBusComponent.sifSignals !=
        &system.sifInterruptRouterComponent ||
      system.iopBusComponent.sifSignals !=
        &system.sifInterruptRouterComponent)
  {
    throw std::logic_error("Cannot persist SIF with foreign machine wiring.");
  }
  writeSIFState(writer, system.sifComponent);
}

void NekoSaveStateCodec::readSIF(SaveStateReader *reader, SIF *sif)
{
  sif->reset();
  sif->mscom = reader->readFieldU32("mscom");
  sif->smcom = reader->readFieldU32("smcom");
  sif->msflag = reader->readFieldU32("msflag");
  sif->smflag = reader->readFieldU32("smflag");
  sif->controlRegister = reader->readFieldU32("control");
  reader->requireField((sif->controlRegister & ~SIF::CONTROL_STATE_MASK) == 0,
    "SIF control contains unsupported bits");
  sif->iopDoorbellRegister = reader->readFieldU32("iopDoorbell");
  for (const auto direction : {SIFDirection::SIF0, SIFDirection::SIF1})
  {
    auto stream = reader->scope(
      direction == SIFDirection::SIF0 ? "sif0" : "sif1");
    const auto count = reader->readFieldU8("wordCount");
    reader->requireField(count <= sif->freeWordCount(),
      "SIF streams exceed shared word capacity");
    auto words = reader->scope("words");
    for (std::size_t index = 0; index < count; ++index)
    {
      auto word = reader->element(index);
      const SIFQuadword beat = {{reader->readFieldU32("value"), 0, 0, 0}};
      const auto outcome = sif->pushWords(direction, beat, 1);
      assert(outcome == SIFTransferOutcome::Completed);
      (void)outcome;
    }
  }
  {
    auto latch = reader->scope("packingLatch");
    for (std::size_t index = 0; index < sif->packingLatch.size(); ++index)
    {
      auto word = reader->element(index);
      sif->packingLatch[index] = reader->readFieldU32("value");
    }
  }
  sif->packingCursor = reader->readFieldU8("packingCursor");
  reader->requireField(sif->packingCursor < sif->packingLatch.size(),
    "SIF0 packing cursor is invalid");
}

void NekoSaveStateCodec::commitSIF(SIF *destination, const SIF &source) noexcept
{
  static_assert(std::is_nothrow_copy_assignable<SIF>::value,
    "Transactional SIF commit must not throw.");
  auto *const order = destination->observationOrder;
  *destination = source;
  destination->observationOrder = order;
  destination->observationCount = 0;
}

std::uint64_t SIF1DMACChannel::stateHash() const
{
  return NekoSaveStateCodec::hashSIF1DMAC(*this);
}

std::uint64_t NekoSaveStateCodec::hashSIF1DMAC(const SIF1DMACChannel &channel)
{
  SaveStateHashWriter writer;
  writeSIF1DMACState(&writer, channel);
  return writer.value();
}

template<typename Writer>
void NekoSaveStateCodec::writeSIF1DMACState(
  Writer *writer, const SIF1DMACChannel &channel)
{
  const auto &state = channel.channelState;
  writer->writeFieldU32("channelControlRegister", state.channelControlRegister);
  writer->writeFieldU32("memoryAddressRegister", state.memoryAddressRegister);
  writer->writeFieldU32("quadwordCountRegister", state.quadwordCountRegister);
  writer->writeFieldU32("tagAddressRegister", state.tagAddressRegister);
  writer->writeFieldBool("terminateAfterPacket", state.terminateAfterPacket);
}

void NekoSaveStateCodec::writeSIF1DMAC(
  SaveStateWriter *writer, const NekoSystem &system)
{
  const auto &channel = system.sif1DMACComponent;
  if (system.eeBusComponent.sif1DMACChannel != &channel ||
      &channel.dmacController != &system.dmacControllerComponent ||
      &channel.sifComponent != &system.sifComponent)
  {
    throw std::logic_error("Cannot persist SIF1 DMA with foreign machine wiring.");
  }
  writeSIF1DMACState(writer, channel);
}

void NekoSaveStateCodec::readSIF1DMAC(
  SaveStateReader *reader, SIF1DMACChannel *channel)
{
  auto &state = channel->channelState;
  state.channelControlRegister = reader->readFieldU32("channelControlRegister");
  reader->requireField(
    DMACChannelControl::validSIF1Control(state.channelControlRegister),
    "SIF1 DMAC channel control or active TTE is invalid");
  state.memoryAddressRegister = reader->readFieldU32("memoryAddressRegister");
  reader->requireField((state.memoryAddressRegister & 0xf) == 0,
    "SIF1 DMAC memory address is unaligned");
  state.quadwordCountRegister = reader->readFieldU32("quadwordCountRegister");
  reader->requireField(state.quadwordCountRegister <= 0xffff,
    "SIF1 DMAC qword count is invalid");
  state.tagAddressRegister = reader->readFieldU32("tagAddressRegister");
  reader->requireField((state.tagAddressRegister & 0xf) == 0,
    "SIF1 DMAC tag address is unaligned");
  state.terminateAfterPacket = reader->readFieldBool("terminateAfterPacket");
  reader->requireField(!state.terminateAfterPacket,
    "SIF1 DMAC tag continuation requires an implemented sender engine");
}

void NekoSaveStateCodec::commitSIF1DMAC(
  SIF1DMACChannel *destination, const SIF1DMACChannel &source) noexcept
{
  auto &state = destination->channelState;
  const auto &sourceState = source.channelState;
  state.channelControlRegister = sourceState.channelControlRegister;
  state.memoryAddressRegister = sourceState.memoryAddressRegister;
  state.quadwordCountRegister = sourceState.quadwordCountRegister;
  state.tagAddressRegister = sourceState.tagAddressRegister;
  state.terminateAfterPacket = sourceState.terminateAfterPacket;
}
