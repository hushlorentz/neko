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
  reader->requireField((sif->controlRegister & ~UINT32_C(0x21f0)) == 0,
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
