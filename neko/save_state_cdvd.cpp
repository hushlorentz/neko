#include "save_state_internal.hpp"

#include <algorithm>

std::uint64_t CDVDController::stateHash() const
{
  return NekoSaveStateCodec::hashCDVD(*this);
}

std::uint64_t NekoSaveStateCodec::hashCDVD(const CDVDController &controller)
{
  SaveStateHashWriter writer;
  writeCDVDState(&writer, controller);
  return writer.value();
}

template<typename Writer>
void NekoSaveStateCodec::writeCDVDState(
  Writer *writer, const CDVDController &controller)
{
  writer->writeFieldU64("rtcTicks", controller.rtcTicks);
  writer->writeFieldBool("normalPending", controller.normalPending);
  writer->writeFieldU8("deviceCause", controller.deviceCause);
  writer->writeFieldU8("lastSubcommand", controller.lastSubcommand);
  writer->writeFieldU8(
    "subcommandPhase", static_cast<std::uint8_t>(controller.subcommandPhase));
  writer->writeFieldBytes("result", controller.result.data(), controller.result.size());
  writer->writeFieldU8("resultCursor", controller.resultCursor);
}

template void NekoSaveStateCodec::writeCDVDState(
  SaveStateWriter *, const CDVDController &);
template void NekoSaveStateCodec::writeCDVDState(
  SaveStateHashWriter *, const CDVDController &);

void NekoSaveStateCodec::readCDVD(
  SaveStateReader *reader, CDVDController *controller)
{
  controller->rtcTicks = reader->readFieldU64("rtcTicks");
  reader->requireField(controller->rtcTicks < CDVDController::RTC_PERIOD_TICKS,
    "CD/DVD RTC is outside its century");
  controller->normalPending = reader->readFieldBool("normalPending");
  controller->deviceCause = reader->readFieldU8("deviceCause");
  reader->requireField(controller->deviceCause <= 1,
    "CD/DVD device cause contains unsupported bits");
  controller->lastSubcommand = reader->readFieldU8("lastSubcommand");
  reader->requireField(
    controller->lastSubcommand == 0 || controller->lastSubcommand == 8,
    "CD/DVD last subcommand is unsupported");
  const auto phase = reader->readFieldU8("subcommandPhase");
  using Phase = CDVDController::SubcommandPhase;
  reader->requireField(phase <= static_cast<std::uint8_t>(Phase::Result),
    "CD/DVD subcommand phase is invalid");
  controller->subcommandPhase = static_cast<Phase>(phase);
  reader->requireField(
    controller->subcommandPhase == Phase::Idle || controller->lastSubcommand == 8,
    "CD/DVD active subcommand has no ReadClock command");
  reader->readFieldBytes(
    "result", controller->result.data(), controller->result.size());
  controller->resultCursor = reader->readFieldU8("resultCursor");
  if (controller->subcommandPhase == Phase::Result)
  {
    reader->requireField(controller->resultCursor < controller->result.size(),
      "CD/DVD result cursor is outside the snapshot");
    reader->requireField(CDVDController::isValidClockResult(controller->result),
      "CD/DVD clock result is not a valid Gregorian BCD snapshot");
  }
  else
  {
    reader->requireField(controller->resultCursor == 0 &&
      std::all_of(controller->result.begin(), controller->result.end(),
        [](std::uint8_t byte) { return byte == 0; }),
      "CD/DVD inactive result storage is not empty");
  }
}
