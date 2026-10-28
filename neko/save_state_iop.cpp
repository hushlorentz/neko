#include "save_state_internal.hpp"

#include <type_traits>

namespace
{
  constexpr std::uint32_t IOP_CACHE_PROFILE = 1;
  using PS2Clock::IOP_CLOCK_PERIOD;
  using PS2Clock::IOP_CLOCK_PHASE;

}

std::uint64_t NekoSystem::iopStateHash() const
{
  return NekoSaveStateCodec::hashIOP(*this);
}

std::uint64_t NekoSaveStateCodec::hashIOP(const NekoSystem &system)
{
  SaveStateHashWriter writer;
  writeIOPState(&writer, system);
  writer.writeFieldU64(
    "masterClockPhase", system.masterClock.currentCycle() % IOP_CLOCK_PERIOD);
  return writer.value();
}

void NekoSaveStateCodec::writeIOP(
  SaveStateWriter *writer, const NekoSystem &system)
{
  writeIOPState(writer, system);
}

template<typename Writer>
void NekoSaveStateCodec::writeIOPState(
  Writer *writer, const NekoSystem &system)
{
  const IOPCore &core = system.iopCoreComponent;
  const IOPBus &bus = system.iopBusComponent;
  if (core.bus != &bus ||
      bus.interruptController != &system.iopInterruptControllerComponent ||
      bus.timers != &system.iopTimersComponent ||
      bus.cdvd != &system.cdvdControllerComponent ||
      bus.dmaController != &system.iopDMAControllerComponent ||
      bus.sif1DMA != &system.iopSIF1DMAComponent ||
      bus.sif0DMA != &system.iopSIF0DMAComponent ||
      &system.iopDMAControllerComponent.interrupts !=
        &system.iopInterruptControllerComponent)
  {
    throw std::logic_error("Cannot persist an IOP with foreign machine wiring.");
  }
  writer->writeFieldU64("clockPeriod", IOP_CLOCK_PERIOD);
  writer->writeFieldU64("clockPhase", IOP_CLOCK_PHASE);
  {
    auto scope = writer->scope("core");
    writer->writeFieldU32("programCounter", core.pc);
    {
      auto registers = writer->scope("generalRegisters");
      for (std::size_t index = 0; index < core.writableGeneralRegisters.size();
           ++index)
      {
        auto reg = writer->element(index + 1);
        writer->writeFieldU32("value", core.writableGeneralRegisters[index]);
      }
    }
    writer->writeFieldU32("hi", core.hiRegister);
    writer->writeFieldU32("lo", core.loRegister);
    writer->writeFieldU8(
      "hiWriteHazardInstructions", core.hiLoState.hiWriteHazardInstructions);
    writer->writeFieldU8(
      "loWriteHazardInstructions", core.hiLoState.loWriteHazardInstructions);
    writer->writeFieldBool(
      "unreadMultiplyDivideHI", core.hiLoState.unreadMultiplyDivideHI);
    writer->writeFieldBool(
      "unreadMultiplyDivideLO", core.hiLoState.unreadMultiplyDivideLO);
    writer->writeFieldU32("badVirtualAddress", core.cop0.badVirtualAddress);
    writer->writeFieldU32("status", core.cop0.status);
    writer->writeFieldU32("cause", core.cop0.cause);
    writer->writeFieldU32(
      "exceptionProgramCounter", core.cop0.exceptionProgramCounter);
    writer->writeFieldU32("directInterruptInputs", core.directInterruptInputs);
    {
      auto branch = writer->scope("branch");
      writer->writeFieldBool("active", core.branch.active);
      writer->writeFieldU32("instructionAddress", core.branch.instructionAddress);
      writer->writeFieldU32("targetAddress", core.branch.targetAddress);
    }
    {
      auto delayed = writer->scope("delayedResult");
      writer->writeFieldU8(
        "source", static_cast<std::uint8_t>(core.delayedResult.source));
      writer->writeFieldU8("destination", core.delayedResult.destination);
      writer->writeFieldU32("value", core.delayedResult.value);
    }
    {
      auto exception = writer->scope("exception");
      writer->writeFieldU8(
        "kind", static_cast<std::uint8_t>(core.enteredException.kind));
      writer->writeFieldU32(
        "badVirtualAddress", core.enteredException.badVirtualAddress);
      writer->writeFieldBool(
        "inBranchDelaySlot", core.enteredException.inBranchDelaySlot);
      writer->writeFieldU8("coprocessor", core.enteredException.coprocessor);
    }
    {
      auto store = writer->scope("storeFailure");
      const auto &failure = core.rejectedStore;
      writer->writeFieldU8("width", static_cast<std::uint8_t>(failure.width));
      writer->writeFieldU32("value", failure.value);
      writer->writeFieldU8(
        "outcome", static_cast<std::uint8_t>(failure.access.outcome));
      writer->writeFieldU8(
        "exception", static_cast<std::uint8_t>(failure.access.exception));
      writer->writeFieldU32("virtualAddress", failure.access.virtualAddress);
      writer->writeFieldU32("physicalAddress", failure.access.physicalAddress);
      writer->writeFieldU8(
        "cacheRoute", static_cast<std::uint8_t>(failure.access.cacheRoute));
      writer->writeFieldU8("mergeByteEnable", failure.mergeByteEnable);
    }
    writer->writeFieldU8("state", static_cast<std::uint8_t>(core.state));
    writer->writeFieldU8("stopReason", static_cast<std::uint8_t>(core.haltReason));
    writer->writeFieldU64("elapsedCycles", core.cycles);
    writer->writeFieldU64("retiredInstructions", core.retiredInstructionTotal);
    writer->writeFieldBool(
      "cacheRomSynchronized", core.cacheRomIdentity == bus.rom);
    {
      auto cache = writer->scope("cache");
      writer->writeFieldU32("profile", IOP_CACHE_PROFILE);
      writer->writeFieldSize(
        "instructionLineCount", IOPCache::INSTRUCTION_LINE_COUNT);
      writer->writeFieldSize(
        "instructionLineWords", IOPCache::INSTRUCTION_LINE_WORDS);
      writer->writeFieldSize("dataLineCount", IOPCache::DATA_LINE_COUNT);
      writer->writeFieldSize("dataLineWords", IOPCache::DATA_LINE_WORDS);
      const auto writeBank = [&](const char *name, const auto &lines)
      {
        auto bank = writer->scope(name);
        for (std::size_t index = 0; index < lines.size(); ++index)
        {
          auto line = writer->element(index);
          writer->writeFieldU32("physicalTag", lines[index].physicalTag);
          writer->writeFieldBool("valid", lines[index].valid);
          auto words = writer->scope("words");
          for (std::size_t word = 0; word < lines[index].words.size(); ++word)
          {
            auto payload = writer->element(word);
            writer->writeFieldU32("value", lines[index].words[word]);
          }
        }
      };
      writeBank("instructionLines", core.caches.instructionLines);
      writeBank("dataLines", core.caches.dataLines);
    }
  }
  {
    auto scope = writer->scope("bus");
    writer->writeFieldBool("romPresent", bus.rom != nullptr);
    writer->writeFieldU64(
      "romFingerprint", bus.rom != nullptr ? bus.rom->fingerprint() : 0);
    writer->writeFieldU32("ssbusConfiguration", bus.ssbusConfiguration.romDelay);
    writer->writeFieldU32("ssbusCommonDelay", bus.ssbusConfiguration.commonDelay);
    writer->writeFieldU32("ssbusPioDelay", bus.ssbusConfiguration.pioDelay);
    writer->writeFieldU32("ssbusSpdAddress", bus.ssbusConfiguration.spdAddress);
    writer->writeFieldU32("ssbusPioAddress", bus.ssbusConfiguration.pioAddress);
    writer->writeFieldU32("ssbusSpdDelay", bus.ssbusConfiguration.spdDelay);
    writer->writeFieldU32("ssbusDev1Delay", bus.ssbusConfiguration.dev1Delay);
    writer->writeFieldU32("ssbusSpuDelay", bus.ssbusConfiguration.spuDelay);
    writer->writeFieldU32("ssbusDev5Delay", bus.ssbusConfiguration.dev5Delay);
    writer->writeFieldU32("ssbusDev1Address", bus.ssbusConfiguration.dev1Address);
    writer->writeFieldU32("ssbusSpuAddress", bus.ssbusConfiguration.spuAddress);
    writer->writeFieldU32("ssbusDev5Address", bus.ssbusConfiguration.dev5Address);
    writer->writeFieldU32("ssbusSpu1Address", bus.ssbusConfiguration.spu1Address);
    writer->writeFieldU32("ssbusDev9Address3", bus.ssbusConfiguration.dev9Address3);
    writer->writeFieldU32("ssbusSpu1Delay", bus.ssbusConfiguration.spu1Delay);
    writer->writeFieldU32("ssbusDev9Delay2", bus.ssbusConfiguration.dev9Delay2);
    writer->writeFieldU32("ssbusDev9Delay3", bus.ssbusConfiguration.dev9Delay3);
    writer->writeFieldU32("ssbusDev9Delay1", bus.ssbusConfiguration.dev9Delay1);
    writer->writeFieldU32("cacheInitializationControl", bus.cacheInitializationControl);
    writer->writeFieldByteVector("ram", bus.ram);
    writer->writeFieldBytes(
      "scratchpad", bus.scratchpad.data(), bus.scratchpad.size());
  }
  {
    auto scope = writer->scope("interruptController");
    const auto &interrupts = system.iopInterruptControllerComponent;
    writer->writeFieldU32("status", interrupts.statusRegister);
    writer->writeFieldU32("mask", interrupts.maskRegister);
    writer->writeFieldU32("control", interrupts.controlRegister);
  }
  {
    auto scope = writer->scope("timers");
    const auto &timers = system.iopTimersComponent;
    writer->writeFieldBool("horizontalBlank", timers.blanking.horizontalBlank);
    writer->writeFieldBool("verticalBlank", timers.blanking.verticalBlank);
    auto counters = writer->scope("counters");
    for (std::size_t index = 0; index < timers.counters.size(); ++index)
    {
      auto counter = writer->element(index);
      const auto &state = timers.counters[index];
      writer->writeFieldU32("count", state.count);
      writer->writeFieldU32("mode", state.mode);
      writer->writeFieldU32("target", state.target);
      writer->writeFieldU16("prescalerRemainder", state.prescalerRemainder);
      writer->writeFieldBool("gateWaiting", state.gateWaiting);
      writer->writeFieldBool("oneShotConsumed", state.oneShotConsumed);
      writer->writeFieldBool("pulseReleasePending", state.pulseReleasePending);
    }
  }
  {
    auto scope = writer->scope("cdvd");
    writeCDVDState(writer, system.cdvdControllerComponent);
  }
  {
    auto scope = writer->scope("timing");
    writer->writeFieldU16("pixelPhase", system.iopTimerTimingComponent.phase);
  }
  {
    auto scope = writer->scope("dmaController");
    writer->writeFieldU8("sifNextChannel",
      static_cast<std::uint8_t>(system.iopDMAControllerComponent.sifNextChannel));
    writer->writeFieldU32("auxiliaryControl",
      system.iopDMAControllerComponent.auxiliaryControlRegister);
    auto banks = writer->scope("banks");
    for (std::size_t index = 0;
         index < system.iopDMAControllerComponent.banks.size(); ++index)
    {
      auto bank = writer->element(index);
      const auto &state = system.iopDMAControllerComponent.banks[index];
      writer->writeFieldU32("priority", state.priority);
      writer->writeFieldU32("control", state.control);
      writer->writeFieldU8("flags", state.flags);
    }
  }
  {
    auto scope = writer->scope("sif1DMA");
    const auto &channel = system.iopSIF1DMAComponent;
    writer->writeFieldU32("memoryAddress", channel.registers.memoryAddressRegister);
    writer->writeFieldU32("blockControl", channel.registers.blockControlRegister);
    writer->writeFieldU32("channelControl", channel.registers.channelControlRegister);
    writer->writeFieldU32("remainingWords", channel.packetWordsRemaining);
    writer->writeFieldBool("terminal", channel.packetTerminal);
  }
  {
    auto scope = writer->scope("sif0DMA");
    const auto &channel = system.iopSIF0DMAComponent;
    writer->writeFieldU32("memoryAddress", channel.registers.memoryAddressRegister);
    writer->writeFieldU32("blockControl", channel.registers.blockControlRegister);
    writer->writeFieldU32("channelControl", channel.registers.channelControlRegister);
    writer->writeFieldU32("tagAddress", channel.tagAddressRegister);
    writer->writeFieldU32("remainingWords", channel.packetWordsRemaining);
    writer->writeFieldBool("terminal", channel.packetTerminal);
  }
}

void NekoSaveStateCodec::readIOP(
  SaveStateReader *reader, NekoSystem *system, IOPConfiguration *configuration)
{
  reader->requireField(
    reader->readFieldU64("clockPeriod") == IOP_CLOCK_PERIOD,
    "IOP clock period is incompatible");
  reader->requireField(
    reader->readFieldU64("clockPhase") == IOP_CLOCK_PHASE,
    "IOP clock phase is incompatible");
  {
    auto scope = reader->scope("core");
    readIOPCore(
      reader, &system->iopCoreComponent, &configuration->cacheRomSynchronized);
  }
  {
    auto scope = reader->scope("bus");
    auto &bus = system->iopBusComponent;
    configuration->rom = readROMConfiguration(reader);
    reader->requireField(
      configuration->rom.present || configuration->cacheRomSynchronized,
      "IOP cache has a pending ROM replacement without an external image");
    bus.ssbusConfiguration.romDelay = reader->readFieldU32("ssbusConfiguration");
    bus.ssbusConfiguration.commonDelay = reader->readFieldU32("ssbusCommonDelay");
    bus.ssbusConfiguration.pioDelay = reader->readFieldU32("ssbusPioDelay");
    bus.ssbusConfiguration.spdAddress = reader->readFieldU32("ssbusSpdAddress");
    bus.ssbusConfiguration.pioAddress = reader->readFieldU32("ssbusPioAddress");
    bus.ssbusConfiguration.spdDelay = reader->readFieldU32("ssbusSpdDelay");
    bus.ssbusConfiguration.dev1Delay = reader->readFieldU32("ssbusDev1Delay");
    bus.ssbusConfiguration.spuDelay = reader->readFieldU32("ssbusSpuDelay");
    bus.ssbusConfiguration.dev5Delay = reader->readFieldU32("ssbusDev5Delay");
    bus.ssbusConfiguration.dev1Address = reader->readFieldU32("ssbusDev1Address");
    bus.ssbusConfiguration.spuAddress = reader->readFieldU32("ssbusSpuAddress");
    bus.ssbusConfiguration.dev5Address = reader->readFieldU32("ssbusDev5Address");
    bus.ssbusConfiguration.spu1Address = reader->readFieldU32("ssbusSpu1Address");
    bus.ssbusConfiguration.dev9Address3 = reader->readFieldU32("ssbusDev9Address3");
    bus.ssbusConfiguration.spu1Delay = reader->readFieldU32("ssbusSpu1Delay");
    bus.ssbusConfiguration.dev9Delay2 = reader->readFieldU32("ssbusDev9Delay2");
    bus.ssbusConfiguration.dev9Delay3 = reader->readFieldU32("ssbusDev9Delay3");
    bus.ssbusConfiguration.dev9Delay1 = reader->readFieldU32("ssbusDev9Delay1");
    bus.cacheInitializationControl = reader->readFieldU32("cacheInitializationControl");
    bus.ram = reader->readFieldByteVector("ram", IOPBus::RAM_SIZE);
    reader->readFieldBytes(
      "scratchpad", bus.scratchpad.data(), bus.scratchpad.size());
  }
  {
    auto scope = reader->scope("interruptController");
    auto &interrupts = system->iopInterruptControllerComponent;
    interrupts.statusRegister = reader->readFieldU32("status");
    reader->requireField(
      (interrupts.statusRegister & ~IOPInterruptController::SOURCE_MASK) == 0,
      "IOP interrupt status contains unsupported causes");
    interrupts.maskRegister = reader->readFieldU32("mask");
    reader->requireField(
      (interrupts.maskRegister & ~IOPInterruptController::SOURCE_MASK) == 0,
      "IOP interrupt mask contains unsupported causes");
    interrupts.controlRegister = reader->readFieldU32("control");
    reader->requireField(
      interrupts.controlRegister <= 1, "IOP interrupt master control is invalid");
  }
  {
    auto scope = reader->scope("timers");
    readIOPTimers(reader, &system->iopTimersComponent);
  }
  {
    auto scope = reader->scope("cdvd");
    readCDVD(reader, &system->cdvdControllerComponent);
  }
  {
    auto scope = reader->scope("timing");
    system->iopTimerTimingComponent.phase = reader->readFieldU16("pixelPhase");
    reader->requireField(
      system->iopTimerTimingComponent.phase < PS2Clock::IOP_PIXEL_PERIOD,
      "IOP pixel phase is outside its rational period");
  }
  {
    auto scope = reader->scope("dmaController");
    const auto nextChannel = reader->readFieldU8("sifNextChannel");
    reader->requireField(
      nextChannel == static_cast<std::uint8_t>(IOPDMAChannel::SIF0) ||
      nextChannel == static_cast<std::uint8_t>(IOPDMAChannel::SIF1),
      "IOP SIF arbitration cursor is invalid");
    system->iopDMAControllerComponent.sifNextChannel = static_cast<IOPDMAChannel>(nextChannel);
    system->iopDMAControllerComponent.auxiliaryControlRegister =
      reader->readFieldU32("auxiliaryControl");
    auto banks = reader->scope("banks");
    for (std::size_t index = 0;
         index < system->iopDMAControllerComponent.banks.size(); ++index)
    {
      auto bank = reader->element(index);
      auto &state = system->iopDMAControllerComponent.banks[index];
      state.priority = reader->readFieldU32("priority");
      state.control = reader->readFieldU32("control");
      reader->requireField(
        (state.control & 0xff000000) == 0,
        "IOP DMA interrupt control contains non-control bits");
      state.flags = reader->readFieldU8("flags");
      const auto implemented = index == 1
        ? (1 << IOPDMAController::channelSlot(IOPDMAChannel::SIF0)) |
          (1 << IOPDMAController::channelSlot(IOPDMAChannel::SIF1)) : 0;
      reader->requireField(
        (state.flags & ~implemented) == 0,
        "IOP DMA completion flags contain unimplemented channels");
    }
  }
  {
    auto scope = reader->scope("sif1DMA");
    auto &channel = system->iopSIF1DMAComponent;
    channel.registers.memoryAddressRegister = reader->readFieldU32("memoryAddress");
    reader->requireField(
      IOPSIF1DMAChannel::validMemoryAddress(channel.registers.memoryAddressRegister),
      "IOP SIF1 DMA address is unaligned or exceeds 24 bits");
    channel.registers.blockControlRegister = reader->readFieldU32("blockControl");
    channel.registers.channelControlRegister = reader->readFieldU32("channelControl");
    reader->requireField(
      IOPSIF1DMAChannel::validChannelControl(channel.registers.channelControlRegister),
      "IOP SIF1 DMA channel control is outside the supported profile");
    channel.packetWordsRemaining = reader->readFieldU32("remainingWords");
    channel.packetTerminal = reader->readFieldBool("terminal");
    reader->requireField(channel.validContinuation(),
      "IOP SIF1 DMA packet continuation is incompatible with channel state");
  }
  {
    auto scope = reader->scope("sif0DMA");
    auto &channel = system->iopSIF0DMAComponent;
    channel.registers.memoryAddressRegister = reader->readFieldU32("memoryAddress");
    reader->requireField(
      IOPDMAChannelState::validMemoryAddress(channel.registers.memoryAddressRegister),
      "IOP SIF0 DMA address is unaligned or exceeds 24 bits");
    channel.registers.blockControlRegister = reader->readFieldU32("blockControl");
    channel.registers.channelControlRegister = reader->readFieldU32("channelControl");
    reader->requireField(IOPDMAChannelState::validChannelControl(
      channel.registers.channelControlRegister, IOPDMAChannelProfile::SIF0Source),
      "IOP SIF0 DMA channel control is outside the supported profile");
    channel.tagAddressRegister = reader->readFieldU32("tagAddress");
    reader->requireField(IOPDMAChannelState::validMemoryAddress(channel.tagAddressRegister),
      "IOP SIF0 DMA tag address is unaligned or exceeds 24 bits");
    channel.packetWordsRemaining = reader->readFieldU32("remainingWords");
    channel.packetTerminal = reader->readFieldBool("terminal");
    reader->requireField(channel.validContinuation(),
      "IOP SIF0 DMA packet continuation is incompatible with channel state");
  }
}

void NekoSaveStateCodec::readIOPCache(
  SaveStateReader *reader, IOPCache *cache)
{
  reader->requireField(
    reader->readFieldU32("profile") == IOP_CACHE_PROFILE,
    "IOP cache profile is incompatible");
  const auto dimension = [&](const char *name, std::size_t expected)
  {
    reader->requireField(
      reader->readFieldU32(name) == expected,
      "IOP cache geometry is incompatible");
  };
  dimension("instructionLineCount", IOPCache::INSTRUCTION_LINE_COUNT);
  dimension("instructionLineWords", IOPCache::INSTRUCTION_LINE_WORDS);
  dimension("dataLineCount", IOPCache::DATA_LINE_COUNT);
  dimension("dataLineWords", IOPCache::DATA_LINE_WORDS);
  const auto readBank = [&](const char *name, auto &lines, std::size_t size)
  {
    auto bank = reader->scope(name);
    for (std::size_t index = 0; index < lines.size(); ++index)
    {
      auto line = reader->element(index);
      lines[index].physicalTag = reader->readFieldU32("physicalTag");
      reader->requireField(
        (lines[index].physicalTag & (size - 1)) == 0,
        "IOP cache tag contains index or offset bits");
      lines[index].valid = reader->readFieldBool("valid");
      auto words = reader->scope("words");
      for (std::size_t word = 0; word < lines[index].words.size(); ++word)
      {
        auto payload = reader->element(word);
        lines[index].words[word] = reader->readFieldU32("value");
      }
    }
  };
  readBank("instructionLines", cache->instructionLines, IOPCache::INSTRUCTION_SIZE);
  readBank("dataLines", cache->dataLines, IOPCache::DATA_SIZE);
}

void NekoSaveStateCodec::readIOPCore(
  SaveStateReader *reader, IOPCore *core, bool *cacheRomSynchronized)
{
  core->pc = reader->readFieldU32("programCounter");
  {
    auto registers = reader->scope("generalRegisters");
    for (std::size_t index = 0; index < core->writableGeneralRegisters.size();
         ++index)
    {
      auto reg = reader->element(index + 1);
      core->writableGeneralRegisters[index] = reader->readFieldU32("value");
    }
  }
  core->hiRegister = reader->readFieldU32("hi");
  core->loRegister = reader->readFieldU32("lo");
  core->hiLoState.hiWriteHazardInstructions =
    reader->readFieldU8("hiWriteHazardInstructions");
  reader->requireField(
    core->hiLoState.hiWriteHazardInstructions <= 2, "IOP HI hazard is invalid");
  core->hiLoState.loWriteHazardInstructions =
    reader->readFieldU8("loWriteHazardInstructions");
  reader->requireField(
    core->hiLoState.loWriteHazardInstructions <= 2, "IOP LO hazard is invalid");
  core->hiLoState.unreadMultiplyDivideHI =
    reader->readFieldBool("unreadMultiplyDivideHI");
  core->hiLoState.unreadMultiplyDivideLO =
    reader->readFieldBool("unreadMultiplyDivideLO");
  core->cop0.badVirtualAddress = reader->readFieldU32("badVirtualAddress");
  core->cop0.status = reader->readFieldU32("status");
  reader->requireField(
    (core->cop0.status & ~IOPCOP0Status::READ_MASK) == 0,
    "IOP status contains unsupported bits");
  core->cop0.cause = reader->readFieldU32("cause");
  reader->requireField(
    (core->cop0.cause & ~IOPCOP0Cause::READ_MASK) == 0,
    "IOP cause contains unsupported bits");
  core->cop0.exceptionProgramCounter =
    reader->readFieldU32("exceptionProgramCounter");
  core->directInterruptInputs = reader->readFieldU32("directInterruptInputs");
  reader->requireField(
    (core->directInterruptInputs & ~IOPCOP0Cause::HARDWARE_INTERRUPT_PENDING) == 0,
    "IOP direct interrupt inputs are invalid");
  {
    auto branch = reader->scope("branch");
    core->branch.active = reader->readFieldBool("active");
    core->branch.instructionAddress = reader->readFieldU32("instructionAddress");
    core->branch.targetAddress = reader->readFieldU32("targetAddress");
    reader->requireField(
      core->branch.active
        ? (core->branch.instructionAddress & 3) == 0 &&
          core->pc == core->branch.instructionAddress + IOPWord(4)
        : core->branch.instructionAddress == 0 && core->branch.targetAddress == 0,
      "IOP branch continuation is inconsistent");
  }
  {
    auto delayed = reader->scope("delayedResult");
    core->delayedResult.source = readEnum<IOPDelayedResultSource>(
      reader, static_cast<std::uint8_t>(IOPDelayedResultSource::COP0Transfer),
      "source");
    core->delayedResult.destination = reader->readFieldU8("destination");
    reader->requireField(
      core->delayedResult.destination < IOPCore::GENERAL_REGISTER_COUNT,
      "IOP delayed destination is invalid");
    core->delayedResult.value = reader->readFieldU32("value");
    reader->requireField(
      core->delayedResult.source != IOPDelayedResultSource::None ||
        (core->delayedResult.destination == 0 && core->delayedResult.value == 0),
      "inactive IOP delayed result contains payload");
  }
  {
    auto exception = reader->scope("exception");
    core->enteredException.kind = readEnum<IOPException>(
      reader, static_cast<std::uint8_t>(IOPException::ArithmeticOverflow), "kind");
    core->enteredException.badVirtualAddress =
      reader->readFieldU32("badVirtualAddress");
    core->enteredException.inBranchDelaySlot =
      reader->readFieldBool("inBranchDelaySlot");
    core->enteredException.coprocessor = reader->readFieldU8("coprocessor");
    reader->requireField(
      core->enteredException.coprocessor <= 3,
      "IOP exception coprocessor is invalid");
    reader->requireField(
      core->enteredException.kind != IOPException::None ||
        (core->enteredException.badVirtualAddress == 0 &&
         !core->enteredException.inBranchDelaySlot &&
         core->enteredException.coprocessor == 0),
      "inactive IOP exception contains payload");
  }
  {
    auto store = reader->scope("storeFailure");
    auto &failure = core->rejectedStore;
    const auto width = reader->readFieldU8("width");
    reader->requireField(
      width == 0 || width == 1 || width == 2 || width == 4,
      "IOP rejected-store width is invalid");
    failure.width = static_cast<IOPStoreWidth>(width);
    failure.value = reader->readFieldU32("value");
    failure.access.outcome = readEnum<IOPAccessOutcome>(
      reader, static_cast<std::uint8_t>(IOPAccessOutcome::Unmapped), "outcome");
    failure.access.exception = readEnum<IOPException>(
      reader, static_cast<std::uint8_t>(IOPException::ArithmeticOverflow),
      "exception");
    failure.access.virtualAddress = reader->readFieldU32("virtualAddress");
    failure.access.physicalAddress = reader->readFieldU32("physicalAddress");
    failure.access.cacheRoute = readEnum<IOPCacheRoute>(
      reader, static_cast<std::uint8_t>(IOPCacheRoute::Uncached), "cacheRoute");
    failure.mergeByteEnable = reader->readFieldU8("mergeByteEnable");
    reader->requireField(
      (failure.mergeByteEnable & ~0xf) == 0 &&
        (failure.mergeByteEnable == 0 || failure.width == IOPStoreWidth::Word),
      "IOP rejected-store byte enable is invalid");
    reader->requireField(
      failure.access.exception == IOPException::None &&
      (width == 0
        ? failure.value == 0 &&
          failure.access.outcome == IOPAccessOutcome::Unmapped &&
          failure.access.virtualAddress == 0 &&
          failure.access.physicalAddress == 0 &&
          failure.access.cacheRoute == IOPCacheRoute::None &&
          failure.mergeByteEnable == 0
        : (failure.access.outcome == IOPAccessOutcome::ReadOnly ||
           failure.access.outcome == IOPAccessOutcome::Unmapped) &&
          failure.access.cacheRoute != IOPCacheRoute::None),
      "IOP rejected-store record is inconsistent");
  }
  core->state = readEnum<IOPExecutionState>(
    reader, static_cast<std::uint8_t>(IOPExecutionState::Running), "state");
  core->haltReason = readEnum<IOPStopReason>(
    reader, static_cast<std::uint8_t>(IOPStopReason::StoreFailure), "stopReason");
  reader->requireField(
    (core->state != IOPExecutionState::Running ||
      core->haltReason == IOPStopReason::None) &&
    (core->rejectedStore.width == IOPStoreWidth::None ||
      core->state == IOPExecutionState::Halted) &&
    (core->haltReason != IOPStopReason::StoreFailure ||
      core->rejectedStore.width != IOPStoreWidth::None),
    "IOP execution and stop state disagree");
  core->cycles = reader->readFieldU64("elapsedCycles");
  core->retiredInstructionTotal = reader->readFieldU64("retiredInstructions");
  reader->requireField(
    core->retiredInstructionTotal <= core->cycles,
    "IOP retirement exceeds elapsed cycles");
  *cacheRomSynchronized = reader->readFieldBool("cacheRomSynchronized");
  {
    auto cache = reader->scope("cache");
    readIOPCache(reader, &core->caches);
  }
}

void NekoSaveStateCodec::readIOPTimers(
  SaveStateReader *reader, IOPTimers *timers)
{
  timers->blanking.horizontalBlank = reader->readFieldBool("horizontalBlank");
  timers->blanking.verticalBlank = reader->readFieldBool("verticalBlank");
  auto counters = reader->scope("counters");
  for (std::size_t index = 0; index < timers->counters.size(); ++index)
  {
    auto counter = reader->element(index);
    auto &state = timers->counters[index];
    const auto mask = UINT32_C(0xffffffff) >>
      (32 - IOPTimers::capabilities(index).counterBits);
    state.count = reader->readFieldU32("count");
    reader->requireField(
      (state.count & ~mask) == 0, "IOP timer count exceeds counter width");
    state.mode = reader->readFieldU32("mode");
    reader->requireField(
      (state.mode & ~(IOPTimerMode::WRITABLE_MASK |
        IOPTimerMode::REQUEST_IDLE | IOPTimerMode::EVENT_MASK)) == 0,
      "IOP timer mode contains unsupported bits");
    state.target = reader->readFieldU32("target");
    reader->requireField(
      (state.target & ~mask) == 0, "IOP timer target exceeds counter width");
    const auto selected = timers->configuration(index);
    state.prescalerRemainder = reader->readFieldU16("prescalerRemainder");
    reader->requireField(
      state.prescalerRemainder < selected.divisor,
      "IOP timer prescaler remainder is invalid");
    state.gateWaiting = reader->readFieldBool("gateWaiting");
    reader->requireField(
      !state.gateWaiting ||
        selected.gateMode == IOPTimerGateMode::CountInBlank ||
        selected.gateMode == IOPTimerGateMode::StartOnBlankEnd,
      "IOP timer gate wait is incompatible with its mode");
    state.oneShotConsumed = reader->readFieldBool("oneShotConsumed");
    const bool repeat = (state.mode & IOPTimerMode::REPEAT_INTERRUPT) != 0;
    const bool interruptEnabled =
      (state.mode & (IOPTimerMode::INTERRUPT_ON_TARGET |
        IOPTimerMode::INTERRUPT_ON_OVERFLOW)) != 0;
    reader->requireField(
      !state.oneShotConsumed || (!repeat && interruptEnabled),
      "IOP timer one-shot state is incompatible with its mode");
    state.pulseReleasePending = reader->readFieldBool("pulseReleasePending");
    reader->requireField(
      !state.pulseReleasePending ||
        (interruptEnabled && (repeat || state.oneShotConsumed) &&
         (state.mode & (IOPTimerMode::TOGGLE_INTERRUPT |
           IOPTimerMode::REQUEST_IDLE)) == 0),
      "IOP timer pulse-release state is incompatible with its mode");
  }
}

void NekoSaveStateCodec::commitIOP(
  NekoSystem *destination, NekoSystem *source) noexcept
{
  static_assert(
    std::is_nothrow_copy_assignable<IOPBus::SSBUSConfigurationState>::value,
    "Transactional IOP SSBUS commit must not throw.");
  static_assert(std::is_nothrow_copy_assignable<IOPCore>::value,
    "Transactional IOP core commit must not throw.");
  static_assert(std::is_nothrow_copy_assignable<IOPTimers>::value,
    "Transactional IOP timer commit must not throw.");
  static_assert(std::is_nothrow_copy_assignable<CDVDController>::value,
    "Transactional CD/DVD commit must not throw.");
  static_assert(std::is_nothrow_copy_assignable<IOPSIF1DMAChannel>::value,
    "Transactional IOP SIF1 DMA commit must not throw.");
  static_assert(std::is_nothrow_copy_assignable<IOPSIF0DMAChannel>::value,
    "Transactional IOP SIF0 DMA commit must not throw.");
  destination->iopBusComponent.ram.swap(source->iopBusComponent.ram);
  destination->iopBusComponent.scratchpad = source->iopBusComponent.scratchpad;
  destination->iopBusComponent.ssbusConfiguration =
    source->iopBusComponent.ssbusConfiguration;
  destination->iopBusComponent.cacheInitializationControl =
    source->iopBusComponent.cacheInitializationControl;
  const bool observationsEnabled = destination->iopCoreComponent.observationsEnabled;
  auto *const observationOrder = destination->iopCoreComponent.observationOrder;
  destination->iopCoreComponent = source->iopCoreComponent;
  destination->iopCoreComponent.bus = &destination->iopBusComponent;
  destination->iopCoreComponent.observationsEnabled = observationsEnabled;
  destination->iopCoreComponent.observationOrder = observationOrder;
  destination->iopCoreComponent.clearObservations();
  destination->iopBusComponent.interruptController =
    &destination->iopInterruptControllerComponent;
  destination->iopBusComponent.timers = &destination->iopTimersComponent;
  destination->iopBusComponent.cdvd = &destination->cdvdControllerComponent;
  destination->cdvdControllerComponent = source->cdvdControllerComponent;
  destination->iopBusComponent.dmaController =
    &destination->iopDMAControllerComponent;
  destination->iopBusComponent.sif1DMA = &destination->iopSIF1DMAComponent;
  destination->iopBusComponent.sif0DMA = &destination->iopSIF0DMAComponent;
  const bool interruptObservationsEnabled =
    destination->iopInterruptControllerComponent.observationsEnabled;
  const bool timerObservationsEnabled =
    destination->iopTimersComponent.observationsEnabled;
  auto *const interruptObservationOrder =
    destination->iopInterruptControllerComponent.observationOrder;
  auto *const timerObservationOrder = destination->iopTimersComponent.observationOrder;
  destination->iopInterruptControllerComponent =
    source->iopInterruptControllerComponent;
  destination->iopInterruptControllerComponent.observationsEnabled =
    interruptObservationsEnabled;
  destination->iopInterruptControllerComponent.observationOrder =
    interruptObservationOrder;
  destination->iopInterruptControllerComponent.clearObservations();
  destination->iopTimersComponent = source->iopTimersComponent;
  destination->iopTimersComponent.observationsEnabled = timerObservationsEnabled;
  destination->iopTimersComponent.observationOrder = timerObservationOrder;
  destination->iopTimersComponent.clearObservations();
  destination->iopTimerTimingComponent = source->iopTimerTimingComponent;
  destination->iopDMAControllerComponent.banks =
    source->iopDMAControllerComponent.banks;
  destination->iopDMAControllerComponent.auxiliaryControlRegister =
    source->iopDMAControllerComponent.auxiliaryControlRegister;
  destination->iopDMAControllerComponent.sifNextChannel =
    source->iopDMAControllerComponent.sifNextChannel;
  destination->iopSIF1DMAComponent = source->iopSIF1DMAComponent;
  destination->iopSIF0DMAComponent = source->iopSIF0DMAComponent;
}
