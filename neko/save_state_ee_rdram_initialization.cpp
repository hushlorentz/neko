#include "save_state_internal.hpp"

#include <array>

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
  std::array<std::uint8_t,
    EERDRAMInitializationState::MCH_WINDOW_WORD_COUNT * sizeof(std::uint32_t)>
    windowBytes{};
  for (std::size_t index = 0; index < state.mchWindowWords.size(); ++index)
  {
    const auto word = state.mchWindowWords[index];
    for (std::size_t byte = 0; byte < sizeof(word); ++byte)
    {
      windowBytes[index * sizeof(word) + byte] =
        static_cast<std::uint8_t>(word >> (byte * 8));
    }
  }
  writer->writeFieldBytes(
    "mchWindowWords", windowBytes.data(), windowBytes.size());
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
  std::array<std::uint8_t,
    EERDRAMInitializationState::MCH_WINDOW_WORD_COUNT * sizeof(std::uint32_t)>
    windowBytes{};
  reader->readFieldBytes(
    "mchWindowWords", windowBytes.data(), windowBytes.size());
  for (std::size_t index = 0; index < state.mchWindowWords.size(); ++index)
  {
    std::uint32_t word = 0;
    for (std::size_t byte = 0; byte < sizeof(word); ++byte)
    {
      word |= static_cast<std::uint32_t>(
        windowBytes[index * sizeof(word) + byte]) << (byte * 8);
    }
    state.mchWindowWords[index] = word;
  }
  reader->requireField(
    EERDRAMInitialization::validState(state),
    "EE RDRAM initialization enumeration continuation is invalid");
  initialization->registers = state;
}
