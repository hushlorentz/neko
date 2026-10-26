#include "save_state_internal.hpp"

std::uint64_t EERDRAMInitialization::stateHash() const
{
  return NekoSaveStateCodec::hashEERDRAMInitialization(*this);
}

std::uint64_t NekoSaveStateCodec::hashEERDRAMInitialization(
  const EERDRAMInitialization &initialization)
{
  SaveStateHashWriter writer;
  writeEERDRAMInitializationState(&writer, initialization);
  return writer.value();
}

template<typename Writer>
void NekoSaveStateCodec::writeEERDRAMInitializationState(
  Writer *writer, const EERDRAMInitialization &initialization)
{
  const auto &state = initialization.registers;
  writer->writeFieldU32("command", state.command);
  writer->writeFieldU32("inputData", state.inputData);
  writer->writeFieldU8("enumeratedDevices", state.enumeratedDevices);
}

void NekoSaveStateCodec::writeEERDRAMInitialization(
  SaveStateWriter *writer, const EERDRAMInitialization &initialization)
{
  writeEERDRAMInitializationState(writer, initialization);
}

void NekoSaveStateCodec::readEERDRAMInitialization(
  SaveStateReader *reader, EERDRAMInitialization *initialization)
{
  EERDRAMInitializationState state;
  state.command = reader->readFieldU32("command");
  reader->requireField(
    EERDRAMInitialization::validState(state),
    "EE RDRAM initialization retained command is unsupported");
  state.inputData = reader->readFieldU32("inputData");
  state.enumeratedDevices = reader->readFieldU8("enumeratedDevices");
  reader->requireField(
    EERDRAMInitialization::validState(state),
    "EE RDRAM initialization enumeration continuation is invalid");
  initialization->registers = state;
}
