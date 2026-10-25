#include "save_state_internal.hpp"

std::uint64_t EETimers::stateHash(std::uint64_t masterCycle) const
{
  return NekoSaveStateCodec::hashEETimers(*this, masterCycle);
}

std::uint64_t NekoSaveStateCodec::hashEETimers(
  const EETimers &timers, std::uint64_t masterCycle)
{
  SaveStateHashWriter writer;
  writeEETimersState(&writer, timers);
  std::uint64_t period = 1;
  for (const auto &timer : timers.timers)
  {
    if ((timer.mode & EETimerMode::COUNT_ENABLE) != 0)
    {
      period = std::max(period,
        EETimers::clockPeriod(timer.mode & EETimerMode::CLOCK));
    }
  }
  writer.writeFieldU64("clockPhase", masterCycle % period);
  return writer.value();
}

void NekoSaveStateCodec::writeEETimers(
  SaveStateWriter *writer, const EETimers &timers)
{
  writeEETimersState(writer, timers);
}

template<typename Writer>
void NekoSaveStateCodec::writeEETimersState(
  Writer *writer, const EETimers &timers)
{
  writer->writeFieldBool("previousHBlank", timers.previousLevels.hBlank);
  writer->writeFieldBool("previousVBlank", timers.previousLevels.vBlank);
  auto counters = writer->scope("timers");
  for (std::size_t index = 0; index < timers.timers.size(); ++index)
  {
    auto counter = writer->element(index);
    const auto &timer = timers.timers[index];
    writer->writeFieldU16("count", timer.count);
    writer->writeFieldU16("mode", timer.mode);
    writer->writeFieldU16("compare", timer.compare);
    if (index < 2)
    {
      writer->writeFieldU16("hold", timer.hold);
    }
  }
}

void NekoSaveStateCodec::readEETimers(
  SaveStateReader *reader, EETimers *timers)
{
  timers->previousLevels.hBlank = reader->readFieldBool("previousHBlank");
  timers->previousLevels.vBlank = reader->readFieldBool("previousVBlank");
  auto counters = reader->scope("timers");
  for (std::size_t index = 0; index < timers->timers.size(); ++index)
  {
    auto counter = reader->element(index);
    auto &timer = timers->timers[index];
    timer.count = reader->readFieldU16("count");
    timer.mode = reader->readFieldU16("mode");
    reader->requireField(
      (timer.mode & ~(EETimerMode::CONTROLS | EETimerMode::FLAGS)) == 0,
      "EE timer mode contains reserved bits");
    timer.compare = reader->readFieldU16("compare");
    timer.hold = index < 2 ? reader->readFieldU16("hold") : 0;
  }
}
