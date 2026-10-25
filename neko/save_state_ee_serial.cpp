#include "save_state_internal.hpp"

std::uint64_t EESerial::stateHash() const
{
  return NekoSaveStateCodec::hashEESerial(*this);
}

std::uint64_t NekoSaveStateCodec::hashEESerial(const EESerial &serial)
{
  SaveStateHashWriter writer;
  writeEESerialState(&writer, serial);
  return writer.value();
}

template<typename Writer>
void NekoSaveStateCodec::writeEESerialState(
  Writer *writer, const EESerial &serial)
{
  writer->writeFieldU16("remainingCycles", serial.remainingCycles);
  writer->writeFieldU8("pendingByte", serial.pendingByte);
  writer->writeFieldBool("resetHeld", serial.resetHeld);
}

void NekoSaveStateCodec::writeEESerial(
  SaveStateWriter *writer, const EESerial &serial)
{
  writeEESerialState(writer, serial);
}

void NekoSaveStateCodec::readEESerial(
  SaveStateReader *reader, EESerial *serial)
{
  serial->remainingCycles = reader->readFieldU16("remainingCycles");
  serial->pendingByte = reader->readFieldU8("pendingByte");
  serial->resetHeld = reader->readFieldBool("resetHeld");
  reader->requireField(
    serial->remainingCycles <= EESerial::TRANSMIT_FRAME_CYCLES,
    "EE serial frame time exceeds the supported profile");
  reader->requireField(
    serial->remainingCycles != 0 || serial->pendingByte == 0,
    "EE serial idle state contains a pending byte");
  reader->requireField(
    !serial->resetHeld || serial->remainingCycles == 0,
    "EE serial reset-held state contains an active frame");
}
