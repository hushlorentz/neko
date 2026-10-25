#include <cstdint>
#include <stdexcept>
#include <vector>

#include "catch.hpp"
#include "neko_system.hpp"
#include "save_state_mutation.hpp"

namespace
{
  constexpr std::uint32_t TX = 0x1000f180;
  constexpr auto COMPLETION = NekoTraceEventType::SerialTransmitCompleted;

  std::vector<NekoTraceEvent> completions(const NekoSystem &system)
  {
    std::vector<NekoTraceEvent> result;
    for (const auto &event : system.trace())
    {
      if (event.type == COMPLETION)
      {
        result.push_back(event);
      }
    }
    return result;
  }

  void startStore(NekoSystem &system, std::uint32_t address,
    std::uint32_t value, std::uint8_t opcode)
  {
    system.eeBus().write32(0,
      (std::uint32_t(opcode) << 26) | (1u << 21) | (2u << 16));
    system.eeCore().setGeneralRegister(1, {address, 0});
    system.eeCore().setGeneralRegister(2, {value, 0});
    system.eeCore().setCOP0Register(EECOP0Register::Status, 0);
    system.eeCore().startExecution(0xa0000000);
  }
}

TEST_CASE("EE serial MMIO admits exactly one byte and delegates reset control",
  "[ee][bus][serial-integration]")
{
  REQUIRE(EEMemoryMap::SIO_TXFIFO == TX);
  REQUIRE(static_cast<unsigned>(COMPLETION) == 22);
  for (const auto alias : {0u, 0x80000000u, 0xa0000000u})
  {
    NekoSystem system;
    auto &bus = system.eeBus();
    const auto initial = system.eeStateHash();
    const auto iop = system.iopStateHash();
    const auto sif = system.sifStateHash();
    REQUIRE(bus.writeData8(TX, 0));
    REQUIRE(system.eeStateHash() != initial);
    REQUIRE(bus.read32(EEMemoryMap::SIO_ISR | alias) == 0x8000);
    const auto pending = system.eeStateHash();
    REQUIRE_FALSE(bus.writeData8(TX, 0x41));
    REQUIRE_THROWS_AS(bus.write8(TX | alias, 0x41), std::out_of_range);
    REQUIRE(bus.writeData32(EEMemoryMap::SIO_LCR, 0x20));
    REQUIRE(bus.writeData32(EEMemoryMap::SIO_IER, 0));
    REQUIRE(bus.writeData32(EEMemoryMap::SIO_BGR, 0));
    REQUIRE(bus.writeData32(EEMemoryMap::SIO_FCR, 0));
    REQUIRE_FALSE(bus.writeData32(EEMemoryMap::SIO_LCR, 0x21));
    REQUIRE_FALSE(bus.writeData32(EEMemoryMap::SIO_IER, 1));
    REQUIRE_FALSE(bus.writeData32(EEMemoryMap::SIO_BGR, 1));
    REQUIRE_FALSE(bus.writeData32(EEMemoryMap::SIO_FCR, 1));
    REQUIRE(system.eeStateHash() == pending);
    bus.write32(EEMemoryMap::SIO_FCR | alias, 7);
    REQUIRE(bus.read32(EEMemoryMap::SIO_ISR | alias) == 0);
    REQUIRE(system.eeStateHash() != initial);
    const auto held = system.eeStateHash();
    REQUIRE_FALSE(bus.writeData8(TX, 0xff));
    REQUIRE(system.eeStateHash() == held);
    bus.write32(EEMemoryMap::SIO_FCR | alias, 0);
    REQUIRE(system.eeStateHash() == initial);
    REQUIRE_NOTHROW(bus.write8(TX | alias, 0xff));
    REQUIRE(bus.read32(EEMemoryMap::SIO_ISR | alias) == 0x8000);
    REQUIRE(system.iopStateHash() == iop);
    REQUIRE(system.sifStateHash() == sif);
    REQUIRE(system.interruptController().status() == 0);
    REQUIRE(system.iopInterruptController().status() == 0);
  }
}

TEST_CASE("EE serial transmit endpoint excludes reads widths lanes and DMA",
  "[ee][bus][serial-integration]")
{
  NekoSystem system;
  auto &bus = system.eeBus();
  const auto initial = system.eeStateHash();
  std::uint8_t byte = 0x12;
  std::uint16_t half = 0x1234;
  std::uint32_t word = 0x12345678;
  std::uint64_t wide = UINT64_C(0x123456789abcdef0);
  EEQuadword quad = {wide, wide};
  for (const auto address : {TX - 1, TX, TX + 1, TX + 2, TX + 3, TX + 4})
  {
    REQUIRE_FALSE(bus.readData8(address, &byte));
    if (address != TX)
    {
      REQUIRE_FALSE(bus.writeData8(address, 0x41));
    }
  }
  REQUIRE_FALSE(bus.readData16(TX, &half));
  REQUIRE_FALSE(bus.writeData16(TX, 0x4141));
  REQUIRE_FALSE(bus.readData32(TX, &word));
  REQUIRE_FALSE(bus.writeData32(TX, 0x41414141));
  REQUIRE_FALSE(bus.readInstruction32(TX, &word));
  REQUIRE_FALSE(bus.readData64(TX, &wide));
  REQUIRE_FALSE(bus.writeData64(TX, wide));
  REQUIRE_FALSE(bus.readData128(TX, &quad));
  REQUIRE_FALSE(bus.writeData128(TX, quad));
  REQUIRE_FALSE(bus.readDMAC128(TX, &quad));
  REQUIRE_FALSE(bus.writeDMAC128(TX, quad));
  REQUIRE_FALSE(bus.isCacheFillRange(TX, 4));
  REQUIRE_FALSE(bus.writeData8(0x9000f180, 0x41));
  REQUIRE_FALSE(bus.writeData8(0xb000f180, 0x41));
  REQUIRE(byte == 0x12);
  REQUIRE(half == 0x1234);
  REQUIRE(word == 0x12345678);
  REQUIRE(wide == UINT64_C(0x123456789abcdef0));
  REQUIRE(quad.low == wide);
  REQUIRE(quad.high == wide);
  REQUIRE(system.eeStateHash() == initial);
  REQUIRE(bus.writeData8(TX, 0x41));
}

TEST_CASE("EE serial completion occurs once at the exact master tick",
  "[ee][serial-integration][trace]")
{
  for (const std::uint8_t byte : {0x00, 0x0a, 0x0d, 0xff})
  {
    NekoSystem system;
    system.startTrace(100);
    REQUIRE(system.eeBus().writeData8(TX, byte));
    REQUIRE(completions(system).empty());
    system.runMasterCycles(2559);
    REQUIRE(system.eeBus().read32(EEMemoryMap::SIO_ISR) == 0x8000);
    REQUIRE(completions(system).empty());
    system.clockMasterCycle();
    REQUIRE(system.eeBus().read32(EEMemoryMap::SIO_ISR) == 0);
    const auto events = completions(system);
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].masterCycle == 2560);
    REQUIRE(events[0].subsystem == NekoTraceSubsystem::EE);
    REQUIRE(events[0].value0 == byte);
    REQUIRE(events[0].value1 == TX);
    REQUIRE(events[0].value2 == 0);
    REQUIRE(events[0].value3 == 0);
    system.runMasterCycles(2560);
    REQUIRE(completions(system).size() == 1);
    REQUIRE_FALSE(system.traceTruncated());
  }
}

TEST_CASE("EE serial completion precedes same-tick guest transmit or reset",
  "[ee][serial-integration][trace]")
{
  for (const bool reset : {false, true})
  {
    NekoSystem system;
    REQUIRE(system.eeBus().writeData8(TX, 0x23));
    system.runMasterCycles(2559);
    startStore(system, reset ? 0xb000f140 : 0xb000f180,
      reset ? 7 : 0x41, reset ? 0x2b : 0x28);
    system.startTrace(100);
    system.clockMasterCycle();
    REQUIRE(system.eeCore().pendingException() == EEException::None);
    REQUIRE(system.eeBus().read32(EEMemoryMap::SIO_ISR) == (reset ? 0 : 0x8000));
    const auto events = completions(system);
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].masterCycle == 2560);
    REQUIRE(events[0].value0 == 0x23);
    bool completed = false;
    std::size_t stores = 0;
    for (const auto &event : system.trace())
    {
      if (event.type == COMPLETION)
      {
        completed = true;
      }
      if (event.subsystem == NekoTraceSubsystem::EE &&
          event.type == NekoTraceEventType::MemoryAccess &&
          event.value0 == (reset ? 0xb000f140u : 0xb000f180u))
      {
        REQUIRE(completed);
        REQUIRE(event.masterCycle == 2560);
        ++stores;
      }
    }
    REQUIRE(stores == 1);
    system.eeCore().haltExecution();
    system.runMasterCycles(2559);
    REQUIRE(completions(system).size() == 1);
    system.clockMasterCycle();
    REQUIRE(completions(system).size() == (reset ? 1 : 2));
    if (!reset)
    {
      REQUIRE(completions(system)[1].masterCycle == 5120);
      REQUIRE(completions(system)[1].value0 == 0x41);
    }
    REQUIRE_FALSE(system.traceTruncated());
  }
}

TEST_CASE("EE serial canonical hash covers each continuation field",
  "[ee][serial-integration]")
{
  EESerial serial;
  EESerial other;
  const auto idle = serial.stateHash();
  REQUIRE(serial.transmitByte(0) == EESerialTransmitResult::Accepted);
  const auto zero = serial.stateHash();
  REQUIRE(zero != idle);
  REQUIRE(other.transmitByte(1) == EESerialTransmitResult::Accepted);
  REQUIRE(other.stateHash() != zero);
  REQUIRE_FALSE(serial.advance().completed);
  REQUIRE(serial.stateHash() != zero);
  serial.reset();
  REQUIRE(serial.stateHash() == idle);
  REQUIRE(serial.writeFIFOControl(7));
  REQUIRE(serial.stateHash() != idle);
  serial.reset();
  REQUIRE(serial.stateHash() == idle);
}

TEST_CASE("EE serial canceled output and trace capacity do not alter progress",
  "[ee][serial-integration][trace]")
{
  NekoSystem traced;
  NekoSystem plain;
  traced.startTrace(0);
  for (NekoSystem *system : {&traced, &plain})
  {
    REQUIRE(system->eeBus().writeData8(TX, 0x41));
    system->runMasterCycles(2559);
    REQUIRE(system->eeBus().writeData32(EEMemoryMap::SIO_FCR, 7));
    system->runMasterCycles(2560);
    REQUIRE(system->eeBus().read32(EEMemoryMap::SIO_ISR) == 0);
    REQUIRE_FALSE(system->eeBus().writeData8(TX, 0x42));
    REQUIRE(system->eeBus().writeData32(EEMemoryMap::SIO_FCR, 0));
    REQUIRE(system->eeBus().writeData8(TX, 0x00));
    system->runMasterCycles(2560);
    REQUIRE(system->eeBus().read32(EEMemoryMap::SIO_ISR) == 0);
  }
  REQUIRE(traced.trace().empty());
  REQUIRE(traced.traceTruncated());
  REQUIRE(traced.eeStateHash() == plain.eeStateHash());
  REQUIRE(traced.saveState() == plain.saveState());
}

TEST_CASE("EE serial full and reset-held guest byte stores enter DBE",
  "[ee][serial-integration][trace]")
{
  for (const bool resetHeld : {false, true})
  {
    NekoSystem system;
    REQUIRE(system.eeBus().writeData8(TX, 0x23));
    if (resetHeld)
    {
      REQUIRE(system.eeBus().writeData32(EEMemoryMap::SIO_FCR, 7));
    }
    startStore(system, 0xb000f180, 0x41, 0x28);
    system.startTrace(100);
    const auto result = system.stepEEInstruction(16);
    REQUIRE(result.pendingException == EEException::DataBusErrorStore);
    REQUIRE(result.exceptionAddress == 0xb000f180);
    REQUIRE_FALSE(result.cycleLimitReached);
    REQUIRE(system.eeBus().read32(EEMemoryMap::SIO_ISR) ==
      (resetHeld ? 0 : 0x8000));
    std::size_t stores = 0;
    for (const auto &event : system.trace())
    {
      if (event.subsystem == NekoTraceSubsystem::EE &&
          event.type == NekoTraceEventType::MemoryAccess &&
          event.value0 == 0xb000f180)
      {
        ++stores;
        REQUIRE(event.value1 == 0x41);
        REQUIRE(event.value2 == 0);
        REQUIRE(event.value3 == nekoPackEEMemoryTraceMetadata({
          TX, 1, NekoEETraceMemory::AccessKind::DataStore,
          NekoEETraceMemory::TranslationOutcome::Translated,
          NekoEETraceMemory::CacheRoute::Uncached, 2, false, true,
          NekoEETraceMemory::CacheAccess::Bypassed,
          NekoEETraceMemory::TransferOutcome::PhysicalBusError,
          NekoEETraceMemory::FailurePhase::Direct
        }));
      }
    }
    REQUIRE(stores == 1);
    system.eeCore().haltExecution();
    system.runMasterCycles(2560 - system.masterClockScheduler().currentCycle());
    const auto events = completions(system);
    REQUIRE(events.size() == (resetHeld ? 0 : 1));
    if (!resetHeld)
    {
      REQUIRE(events[0].value0 == 0x23);
      REQUIRE(events[0].masterCycle == 2560);
    }
    REQUIRE_FALSE(system.traceTruncated());
  }
}

TEST_CASE("EE serial restores exact in-flight time across trace modes",
  "[ee][serial-integration][save-state]")
{
  NekoSystem system;
  REQUIRE(system.eeBus().writeData8(TX, 0));
  NekoSystem restored;
  const auto accepted = system.saveState();
  restored.loadState(accepted);
  REQUIRE(restored.saveState() == accepted);
  system.runMasterCycles(1279);
  const auto saved = system.saveState();
  const auto hash = system.eeStateHash();
  restored.loadState(saved);
  REQUIRE(restored.saveState() == saved);
  REQUIRE(restored.eeStateHash() == hash);
  for (NekoSystem *machine : {&system, &restored})
  {
    machine->startTrace(100);
    machine->runMasterCycles(1280);
    REQUIRE(machine->eeBus().read32(EEMemoryMap::SIO_ISR) == 0x8000);
    REQUIRE(completions(*machine).empty());
    machine->clockMasterCycle();
    REQUIRE(machine->eeBus().read32(EEMemoryMap::SIO_ISR) == 0);
    REQUIRE(completions(*machine).size() == 1);
    REQUIRE(completions(*machine)[0].value0 == 0);
    REQUIRE(completions(*machine)[0].masterCycle == 2560);
  }
  REQUIRE(system.traceHash() == restored.traceHash());
  const auto ending = system.saveState();
  REQUIRE(restored.saveState() == ending);
  restored.stopTrace();
  restored.loadState(saved);
  restored.runMasterCycles(1281);
  REQUIRE_FALSE(restored.traceEnabled());
  REQUIRE(restored.saveState() == ending);
  restored.loadState(ending);
  restored.startTrace(100);
  restored.runMasterCycles(2560);
  REQUIRE(completions(restored).empty());
  REQUIRE(restored.eeBus().writeData8(TX, 0x41));
  restored.reset();
  NekoSystem fresh;
  REQUIRE(restored.saveState() == fresh.saveState());
  REQUIRE(restored.eeBus().writeData8(TX, 0x42));
}

TEST_CASE("EE serial held reset restores and keeps destination bus attached",
  "[ee][serial-integration][save-state]")
{
  NekoSystem system;
  REQUIRE(system.eeBus().writeData32(EEMemoryMap::SIO_FCR, 7));
  const auto saved = system.saveState();
  NekoSystem restored;
  restored.loadState(saved);
  REQUIRE(restored.saveState() == saved);
  REQUIRE(restored.eeStateHash() == system.eeStateHash());
  REQUIRE_FALSE(restored.eeBus().writeData8(TX, 0x41));
  restored.runMasterCycles(2560);
  REQUIRE_FALSE(restored.eeBus().writeData8(TX, 0x41));
  REQUIRE(restored.eeBus().writeData32(EEMemoryMap::SIO_FCR, 0));
  REQUIRE(restored.eeBus().writeData8(TX, 0x41));
  REQUIRE_FALSE(system.eeBus().writeData8(TX, 0x42));
}

TEST_CASE("EE serial malformed state rejects atomically",
  "[ee][serial-integration][save-state]")
{
  NekoSystem system;
  REQUIRE(system.eeBus().writeData8(TX, 0x41));
  system.runMasterCycles(17);
  const auto original = system.saveState();
  const auto hash = system.eeStateHash();
  for (unsigned scenario = 0; scenario < 5; ++scenario)
  {
    CAPTURE(scenario);
    SaveStateMutation invalid(original);
    if (scenario == 0)
    {
      invalid.writeScalar("system.eeSerial.remainingCycles", 2561);
    }
    else if (scenario == 1)
    {
      invalid.writeScalar("system.eeSerial.remainingCycles", 0);
    }
    else if (scenario == 2)
    {
      invalid.writeScalar("system.eeSerial.resetHeld", 1);
    }
    else if (scenario == 3)
    {
      invalid.writeScalar("system.eeSerial.resetHeld", 2);
    }
    else
    {
      invalid.writeScalar("system.eeSerial.remainingCycles", 0);
      invalid.writeScalar("system.eeSerial.resetHeld", 1);
    }
    invalid.refreshChecksum();
    REQUIRE_THROWS_AS(system.loadState(invalid), std::invalid_argument);
    REQUIRE(system.eeStateHash() == hash);
    REQUIRE(system.saveState() == original);
  }
}
