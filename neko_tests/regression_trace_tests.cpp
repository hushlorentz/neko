#include <cstdint>

#include "catch.hpp"
#include "ee_bus.hpp"
#include "neko_system.hpp"
#include "regression_trace.hpp"

namespace
{
  std::uint32_t immediateInstruction(
    std::uint8_t opcode,
    std::uint8_t source,
    std::uint8_t target,
    std::uint16_t immediate)
  {
    return
      (static_cast<std::uint32_t>(opcode) << 26) |
      (static_cast<std::uint32_t>(source) << 21) |
      (static_cast<std::uint32_t>(target) << 16) |
      immediate;
  }

  std::vector<NekoTraceEvent> eeTrace(
    const NekoSystem &system)
  {
    std::vector<NekoTraceEvent> events;
    for (const NekoTraceEvent &event : system.trace())
    {
      if (event.subsystem == NekoTraceSubsystem::EE)
      {
        events.push_back(event);
      }
    }
    return events;
  }

  GIFQuadword gifTag()
  {
    const std::uint64_t low =
      UINT64_C(1) |
      (UINT64_C(1) << 15) |
      (static_cast<std::uint64_t>(GIFDataFormat::Packed) << 58) |
      (UINT64_C(1) << 60);
    return GIFQuadword{{
      static_cast<std::uint32_t>(low),
      static_cast<std::uint32_t>(low >> 32),
      GIFRegisterDescriptor::AD,
      0
    }};
  }

  GIFQuadword primitiveWrite()
  {
    return GIFQuadword{{
      static_cast<std::uint8_t>(GSPrimitiveType::Sprite),
      0,
      GSRegisterAddress::PRIM,
      0
    }};
  }

  void prepareRegressionFrame(NekoSystem *system)
  {
    system->gsDisplay().configureTiming({4, 6});
    system->gs().writeDisplayPSMCT32(
      0, 1, 0, 0, 0xff332211);
    system->eeBus().write64(
      EEMemoryMap::GS_DISPFB1,
      UINT64_C(1) << 9);
    system->eeBus().write64(EEMemoryMap::GS_DISPLAY1, 0);
    system->eeBus().write64(
      EEMemoryMap::GS_PMODE,
      GSDisplayMode::ENABLE_CIRCUIT_1);
    REQUIRE(system->eeBus().writeQuadword(0x1000, gifTag()));
    REQUIRE(system->eeBus().writeQuadword(
      0x1010,
      primitiveWrite()));
    system->eeBus().write32(
      EEMemoryMap::D_CTRL,
      GIFDMACControl::DMA_ENABLE);
    system->eeBus().write32(EEMemoryMap::D2_MADR, 0x1000);
    system->eeBus().write32(EEMemoryMap::D2_QWC, 2);
    system->eeBus().write32(
      EEMemoryMap::D2_CHCR,
      GIFDMACChannelControl::START);
  }
}

TEST_CASE("Neko Frame Hash Tests")
{
  GSPresentation presentation;
  presentation.width = 1;
  presentation.height = 1;
  presentation.rgba = {1, 2, 3, 4};

  const std::uint64_t hash = nekoFrameHash(presentation);
  REQUIRE(nekoFrameHash(presentation) == hash);

  presentation.rgba[2] = 4;
  REQUIRE(nekoFrameHash(presentation) != hash);

  std::vector<NekoTraceEvent> events(1);
  const std::uint64_t traceHash = nekoTraceHash(events);
  events[0].value3 = 1;
  REQUIRE(nekoTraceHash(events) != traceHash);
}

TEST_CASE("Neko Subsystem Regression Trace Tests")
{
  NekoSystem first;
  NekoSystem second;
  prepareRegressionFrame(&first);
  prepareRegressionFrame(&second);
  first.startTrace();
  second.startTrace();
  NekoInputState input;
  input.buttons = NekoButton::START;
  first.setInput(input);
  second.setInput(input);

  const NekoFrameResult firstFrame = first.runFrame();
  const NekoFrameResult secondFrame = second.runFrame();

  REQUIRE(firstFrame.videoHash == secondFrame.videoHash);
  REQUIRE(firstFrame.videoHash == nekoFrameHash(firstFrame.video));
  REQUIRE(first.traceHash() == second.traceHash());
  REQUIRE(first.trace().size() == second.trace().size());
  REQUIRE(first.trace().size() >= 6);
  REQUIRE(
    first.trace().front().subsystem ==
    NekoTraceSubsystem::Input);
  REQUIRE(
    first.trace()[1].subsystem ==
    NekoTraceSubsystem::GIF);
  REQUIRE(
    first.trace()[2].subsystem ==
    NekoTraceSubsystem::GIFDMAC);
  REQUIRE(
    first.trace().back().type ==
    NekoTraceEventType::PresentationBoundary);
}

TEST_CASE("Save States Continue Identical Regression Traces")
{
  NekoSystem original;
  prepareRegressionFrame(&original);
  original.runMasterCycles(1);
  const std::vector<std::uint8_t> state = original.saveState();

  NekoSystem restored;
  restored.loadState(state);
  original.startTrace();
  restored.startTrace();

  const NekoFrameResult originalFrame = original.runFrame();
  const NekoFrameResult restoredFrame = restored.runFrame();

  REQUIRE(originalFrame.videoHash == restoredFrame.videoHash);
  REQUIRE(original.traceHash() == restored.traceHash());

  original.clearTrace();
  original.loadState(state);
  REQUIRE(original.traceEnabled());
  REQUIRE(original.trace().empty());
}

TEST_CASE("EE regression traces describe issued work")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setGeneralRegister(
    1,
    {0, 0});
  core.setGeneralRegister(
    2,
    {UINT64_C(0x11223344), 0});
  core.setCOP0Register(EECOP0Register::Status, 0);
  system.eeBus().write32(
    0,
    immediateInstruction(0x2b, 1, 2, 0x100));
  system.eeBus().write32(
    4,
    immediateInstruction(0x04, 0, 0, 1));
  system.eeBus().write32(8, 0);
  system.eeBus().write32(12, UINT32_C(0x0000000c));
  core.startExecution(0);
  system.startTrace();

  system.runMasterCycles(4);

  const std::vector<NekoTraceEvent> events = eeTrace(system);
  REQUIRE(events.size() == 7);

  REQUIRE(events[0].masterCycle == 1);
  REQUIRE(
    events[0].type ==
    NekoTraceEventType::InstructionIssued);
  REQUIRE(events[0].value0 == 0);
  REQUIRE(
    events[0].value1 ==
    immediateInstruction(0x2b, 1, 2, 0x100));
  REQUIRE(events[0].value3 == 0);

  REQUIRE(events[1].masterCycle == 1);
  REQUIRE(events[1].type == NekoTraceEventType::MemoryAccess);
  REQUIRE(events[1].value0 == 0x100);
  REQUIRE(events[1].value1 == UINT64_C(0x11223344));
  REQUIRE(events[1].value2 == 0);
  REQUIRE(
    events[1].value3 ==
    (UINT64_C(4) |
     NekoEETraceMemory::WRITE |
     NekoEETraceMemory::SUCCEEDED));

  REQUIRE(events[2].masterCycle == 2);
  REQUIRE(
    events[2].type ==
    NekoTraceEventType::InstructionIssued);
  REQUIRE(events[2].value0 == 4);
  REQUIRE(
    events[3].type ==
    NekoTraceEventType::BranchScheduled);
  REQUIRE(events[3].value0 == 4);
  REQUIRE(events[3].value1 == 12);
  REQUIRE(events[3].value2 == NekoEETraceBranch::TAKEN);

  REQUIRE(events[4].masterCycle == 3);
  REQUIRE(
    events[4].type ==
    NekoTraceEventType::InstructionIssued);
  REQUIRE(events[4].value0 == 8);
  REQUIRE(events[4].value3 == 1);

  REQUIRE(events[5].masterCycle == 4);
  REQUIRE(
    events[5].type ==
    NekoTraceEventType::InstructionIssued);
  REQUIRE(events[5].value0 == 12);
  REQUIRE(
    events[6].type ==
    NekoTraceEventType::ExceptionEntered);
  REQUIRE(
    events[6].value0 ==
    static_cast<std::uint8_t>(EEException::SystemCall));
  REQUIRE(events[6].value1 == 12);
  REQUIRE(events[6].value2 == EEExceptionVector::GENERAL);
}

TEST_CASE("EE regression traces identify interrupt delivery")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  system.interruptController().setSource(
    EEInterruptSource::VIF0,
    true);
  system.interruptController().toggleMask(
    EEInterruptSource::mask(EEInterruptSource::VIF0));
  core.setCOP0Register(
    EECOP0Register::Status,
    EECOP0Status::INTERRUPT_ENABLE |
      EECOP0Status::MASTER_INTERRUPT_ENABLE |
      EECOP0Status::INTC_MASK);
  system.eeBus().write32(0, 0);
  core.startExecution(0);
  system.startTrace();

  system.clockMasterCycle();

  const std::vector<NekoTraceEvent> events = eeTrace(system);
  REQUIRE(events.size() == 2);
  REQUIRE(events[0].masterCycle == 1);
  REQUIRE(
    events[0].type ==
    NekoTraceEventType::InterruptDelivered);
  REQUIRE(events[0].value0 == 0);
  REQUIRE(events[0].value3 == EEExceptionVector::INTERRUPT);
  REQUIRE(
    events[1].type ==
    NekoTraceEventType::ExceptionEntered);
  REQUIRE(
    events[1].value0 ==
    static_cast<std::uint8_t>(EEException::Interrupt));
  REQUIRE(events[1].value2 == EEExceptionVector::INTERRUPT);
}

TEST_CASE("EE regression traces retain failed memory attempts")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(EECOP0Register::Status, 0);
  core.setGeneralRegister(
    1,
    {EEMemoryMap::MAIN_MEMORY_SIZE, 0});
  system.eeBus().write32(
    0,
    immediateInstruction(0x23, 1, 2, 0));
  core.startExecution(0);
  system.startTrace();

  system.clockMasterCycle();

  const std::vector<NekoTraceEvent> events = eeTrace(system);
  REQUIRE(events.size() == 3);
  REQUIRE(
    events[0].type ==
    NekoTraceEventType::InstructionIssued);
  REQUIRE(events[1].type == NekoTraceEventType::MemoryAccess);
  REQUIRE(
    events[1].value0 ==
    EEMemoryMap::MAIN_MEMORY_SIZE);
  REQUIRE(events[1].value1 == 0);
  REQUIRE(events[1].value3 == 4);
  REQUIRE(
    events[2].type ==
    NekoTraceEventType::ExceptionEntered);
  REQUIRE(
    events[2].value0 ==
    static_cast<std::uint8_t>(EEException::DataBusErrorLoad));
}
