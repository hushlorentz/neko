#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "catch.hpp"
#include "iop_trace_test_utils.hpp"
#include "neko_system.hpp"
#include "save_state_mutation.hpp"
#include "spu2_input_test_utils.hpp"

namespace
{
  constexpr std::array<IOPAddress, 5> configurationAddresses = {{
    0x1f9007c6, 0x1f9007c8, 0x1f9007cc, 0x1f9007ca, 0x1f9007c0
  }};
  constexpr std::array<std::uint16_t, 5> configurationValues = {{
    0x0900, 0xa55a, 0x8001, 0x0008, 0xc032
  }};
  constexpr std::array<IOPAddress, 4> masterVolumeAddresses = {{
    0x1f900760, 0x1f900762, 0x1f900788, 0x1f90078a
  }};
  constexpr std::array<std::uint16_t, 4> masterVolumeValues = {{
    0x1234, 0x3fff, 0x4000, 0x7fff
  }};
  constexpr std::size_t voiceVolumeCount = 96;

  IOPAddress voiceVolumeAddress(std::size_t index)
  {
    return static_cast<IOPAddress>(
      0x1f900000 + (index / 48) * 0x400 + ((index / 2) % 24) * 0x10 +
      (index % 2) * 2);
  }

  std::uint16_t voiceVolumeValue(std::size_t index)
  {
    return static_cast<std::uint16_t>((index + 1) * 0x101);
  }

  void programVoiceVolumes(NekoSystem &system)
  {
    for (std::size_t index = 0; index < voiceVolumeCount; ++index)
    {
      REQUIRE(system.iopBus().write16(
        voiceVolumeAddress(index), voiceVolumeValue(index)).status ==
        IOPBusStatus::Completed);
    }
  }

  constexpr std::size_t voicePitchCount = 48;

  IOPAddress voicePitchAddress(std::size_t index)
  {
    return static_cast<IOPAddress>(
      0x1f900004 + (index / 24) * 0x400 + (index % 24) * 0x10);
  }

  std::uint16_t voicePitchValue(std::size_t index)
  {
    return static_cast<std::uint16_t>(0x8000 | ((index + 1) * 0x101));
  }

  void programVoicePitches(NekoSystem &system)
  {
    for (std::size_t index = 0; index < voicePitchCount; ++index)
    {
      REQUIRE(system.iopBus().write16(
        voicePitchAddress(index), voicePitchValue(index)).status ==
        IOPBusStatus::Completed);
    }
  }

  constexpr std::size_t voiceADSRCount = 96;

  IOPAddress voiceADSRAddress(std::size_t index)
  {
    return static_cast<IOPAddress>(
      0x1f900006 + (index / 48) * 0x400 + ((index / 2) % 24) * 0x10 +
      (index % 2) * 2);
  }

  std::uint16_t voiceADSRValue(std::size_t index)
  {
    return static_cast<std::uint16_t>(0x2000 | ((index + 1) * 0x101));
  }

  void programVoiceADSR(NekoSystem &system)
  {
    for (std::size_t index = 0; index < voiceADSRCount; ++index)
    {
      REQUIRE(system.iopBus().write16(
        voiceADSRAddress(index), voiceADSRValue(index)).status ==
        IOPBusStatus::Completed);
    }
  }

  constexpr std::size_t voiceStartAddressCount = 96;

  IOPAddress voiceStartAddress(std::size_t index)
  {
    return static_cast<IOPAddress>(
      0x1f9001c0 + (index / 48) * 0x400 + ((index / 2) % 24) * 0xc +
      (index % 2) * 2);
  }

  std::uint16_t voiceStartValue(std::size_t index)
  {
    return static_cast<std::uint16_t>(
      index % 2 == 0 ? (index / 2) % 15 + 1 : 0x8000 | ((index + 1) * 0x108));
  }

  void programVoiceStartAddresses(NekoSystem &system)
  {
    for (std::size_t index = 0; index < voiceStartAddressCount; ++index)
    {
      REQUIRE(system.iopBus().write16(
        voiceStartAddress(index), voiceStartValue(index)).status ==
        IOPBusStatus::Completed);
    }
  }

  constexpr std::array<IOPAddress, 16> voiceMixAddresses = {{
    0x1f900188, 0x1f90018a, 0x1f90018c, 0x1f90018e,
    0x1f900190, 0x1f900192, 0x1f900194, 0x1f900196,
    0x1f900588, 0x1f90058a, 0x1f90058c, 0x1f90058e,
    0x1f900590, 0x1f900592, 0x1f900594, 0x1f900596
  }};
  constexpr std::array<std::uint16_t, 16> voiceMixValues = {{
    0x1357, 0x8001, 0x1234, 0xabcd, 0x2345, 0xbcde, 0x3456, 0xcdef,
    0x4567, 0xdef0, 0x5678, 0xef01, 0x6789, 0xf012, 0x789a, 0x8123
  }};
  constexpr std::array<IOPAddress, 8> voiceModeAddresses = {{
    0x1f900180, 0x1f900182, 0x1f900184, 0x1f900186,
    0x1f900580, 0x1f900582, 0x1f900584, 0x1f900586
  }};
  constexpr std::array<std::uint16_t, 8> voiceModeValues = {{
    0x0001, 0xff80, 0xffff, 0x8001, 0x1357, 0xabcd, 0x2468, 0xff00
  }};
  constexpr std::array<IOPAddress, 2> mainMixAddresses = {{
    0x1f900198, 0x1f900598
  }};
  constexpr std::array<std::uint16_t, 2> mainMixValues = {{
    0x1357, 0x8abc
  }};
  constexpr std::array<IOPAddress, 12> effectInputVolumeAddresses = {{
    0x1f900764, 0x1f900766, 0x1f900768, 0x1f90076a, 0x1f90076c, 0x1f90076e,
    0x1f90078c, 0x1f90078e, 0x1f900790, 0x1f900792, 0x1f900794, 0x1f900796
  }};
  constexpr std::array<std::uint16_t, 12> effectInputVolumeValues = {{
    0x1357, 0x8000, 0xffff, 0x8abc, 0x2468, 0x7fff,
    0x9753, 0x1234, 0xabcd, 0x5678, 0xcdef, 0x4321
  }};
  constexpr std::array<IOPAddress, 2> effectEndAddressHighAddresses = {{
    0x1f90033c, 0x1f90073c
  }};
  constexpr std::array<std::uint16_t, 2> effectEndAddressHighValues = {{
    0x800e, 0xf123
  }};
  constexpr std::array<IOPAddress, 4> transferStartAddressAddresses = {{
    0x1f9001a8, 0x1f9001aa, 0x1f9005a8, 0x1f9005aa
  }};
  constexpr std::array<std::uint16_t, 4> transferStartAddressValues = {{
    0x8001, 0x1357, 0xfedc, 0x2468
  }};
  constexpr std::array<IOPAddress, 2> transferDataAddresses = {{
    0x1f9001ac, 0x1f9005ac
  }};
  constexpr std::array<IOPAddress, 2> unknownInitializationAddresses = {{
    0x1f900b60, 0x1f900b62
  }};
  constexpr std::array<IOPAddress, 2> coreAttributeAddresses = {{
    0x1f90019a, 0x1f90059a
  }};
  constexpr std::array<std::uint16_t, 8> coreAttributeValues = {{
    0, 1, 0x4000, 0x4001, 0x8000, 0x8001, 0xc000, 0xc001
  }};
  constexpr std::array<IOPAddress, 2> autoDMAAddresses = {{
    0x1f9001b0, 0x1f9005b0
  }};
  constexpr std::array<std::uint16_t, 2> autoDMAWriteSelections = {{
    1, 2
  }};
  constexpr std::array<IOPAddress, 2> coreStatusAddresses = {{
    0x1f900344, 0x1f900744
  }};
  constexpr std::array<IOPAddress, 2> keyOffLowAddresses = {{
    0x1f9001a4, 0x1f9005a4
  }};
  constexpr std::array<IOPAddress, 2> keyOffHighAddresses = {{
    0x1f9001a6, 0x1f9005a6
  }};
  struct HalfwordProgramming
  {
    IOPAddress address;
    std::uint16_t value;
    IOPWord signedLoad;
  };
  constexpr std::array<HalfwordProgramming, 53> halfwordProgramming = {{
    {0x1f9007c6, 0x8900, 0xffff8900},
    {0x1f9007c8, 0x8900, 0xffff8900},
    {0x1f9007cc, 0x8900, 0xffff8900},
    {0x1f9007ca, 0x8900, 0xffff8900},
    {0x1f9007c0, 0xc032, 0xffffc032},
    {0x1f900760, 0x4900, 0x4900},
    {0x1f900762, 0x4900, 0x4900},
    {0x1f900788, 0x4900, 0x4900},
    {0x1f90078a, 0x4900, 0x4900},
    {0x1f900188, 0xffff, 0xffffffff},
    {0x1f90018a, 0x8900, 0xffff8900},
    {0x1f90018c, 0x4900, 0x4900},
    {0x1f90018e, 0x8900, 0xffff8900},
    {0x1f900190, 0x4900, 0x4900},
    {0x1f900192, 0x8900, 0xffff8900},
    {0x1f900194, 0x4900, 0x4900},
    {0x1f900196, 0x8900, 0xffff8900},
    {0x1f900588, 0x4900, 0x4900},
    {0x1f90058a, 0x8900, 0xffff8900},
    {0x1f90058c, 0x4900, 0x4900},
    {0x1f90058e, 0x8900, 0xffff8900},
    {0x1f900590, 0x4900, 0x4900},
    {0x1f900592, 0x8900, 0xffff8900},
    {0x1f900594, 0x4900, 0x4900},
    {0x1f900596, 0x8900, 0xffff8900},
    {0x1f900198, 0x0ff0, 0x0ff0},
    {0x1f900598, 0xf00f, 0xfffff00f},
    {0x1f900764, 0x8000, 0xffff8000},
    {0x1f900766, 0xffff, 0xffffffff},
    {0x1f900768, 0x8900, 0xffff8900},
    {0x1f90076a, 0x4900, 0x4900},
    {0x1f90076c, 0xffff, 0xffffffff},
    {0x1f90076e, 0x8000, 0xffff8000},
    {0x1f90078c, 0x4900, 0x4900},
    {0x1f90078e, 0x8000, 0xffff8000},
    {0x1f900790, 0xffff, 0xffffffff},
    {0x1f900792, 0x8900, 0xffff8900},
    {0x1f900794, 0x7fff, 0x7fff},
    {0x1f900796, 0, 0},
    {0x1f90033c, 0x000e, 0x000e},
    {0x1f90073c, 0xffff, 0xffffffff},
    {0x1f9001a8, 0x8001, 0xffff8001},
    {0x1f9001aa, 0xffff, 0xffffffff},
    {0x1f9005a8, 0x000f, 0x000f},
    {0x1f9005aa, 0x1357, 0x1357},
    {0x1f900180, 0xffff, 0xffffffff},
    {0x1f900182, 0xff80, 0xffffff80},
    {0x1f900184, 0x4900, 0x4900},
    {0x1f900186, 0x8900, 0xffff8900},
    {0x1f900580, 0x4901, 0x4901},
    {0x1f900582, 0xffff, 0xffffffff},
    {0x1f900584, 0x8000, 0xffff8000},
    {0x1f900586, 0xff00, 0xffffff00}
  }};

  void programVoiceModes(NekoSystem &system)
  {
    for (std::size_t index = 0; index < voiceModeAddresses.size(); ++index)
    {
      REQUIRE(system.iopBus().write16(
        voiceModeAddresses[index], voiceModeValues[index]).status ==
        IOPBusStatus::Completed);
    }
  }

  void programMasterVolumes(NekoSystem &system)
  {
    for (std::size_t index = 0; index < masterVolumeAddresses.size(); ++index)
    {
      REQUIRE(system.iopBus().write16(
        masterVolumeAddresses[index], masterVolumeValues[index]).status ==
        IOPBusStatus::Completed);
    }
  }

  void programVoiceMix(NekoSystem &system)
  {
    for (std::size_t index = 0; index < voiceMixAddresses.size(); ++index)
    {
      REQUIRE(system.iopBus().write16(
        voiceMixAddresses[index], voiceMixValues[index]).status ==
        IOPBusStatus::Completed);
    }
  }

  void programMainMix(NekoSystem &system)
  {
    for (std::size_t core = 0; core < mainMixAddresses.size(); ++core)
    {
      REQUIRE(system.iopBus().write16(
        mainMixAddresses[core], mainMixValues[core]).status ==
        IOPBusStatus::Completed);
    }
  }

  void programEffectInputVolumes(NekoSystem &system)
  {
    for (std::size_t index = 0; index < effectInputVolumeAddresses.size(); ++index)
    {
      REQUIRE(system.iopBus().write16(
        effectInputVolumeAddresses[index], effectInputVolumeValues[index]).status ==
        IOPBusStatus::Completed);
    }
  }

  void programEffectEndAddresses(NekoSystem &system)
  {
    for (std::size_t core = 0; core < effectEndAddressHighAddresses.size(); ++core)
    {
      REQUIRE(system.iopBus().write16(
        effectEndAddressHighAddresses[core], effectEndAddressHighValues[core]).status ==
        IOPBusStatus::Completed);
    }
  }

  void programTransferStartAddresses(NekoSystem &system)
  {
    for (std::size_t index = 0; index < transferStartAddressAddresses.size(); ++index)
    {
      REQUIRE(system.iopBus().write16(
        transferStartAddressAddresses[index], transferStartAddressValues[index]).status ==
        IOPBusStatus::Completed);
    }
  }

  void programConfiguration(NekoSystem &system)
  {
    for (std::size_t index = 0; index < configurationAddresses.size(); ++index)
    {
      REQUIRE(system.iopBus().write16(
        configurationAddresses[index], configurationValues[index]).status ==
        IOPBusStatus::Completed);
    }
  }

  void installConfigurationProgram(
    NekoSystem &system, IOPAddress address, std::uint16_t value)
  {
    const std::array<IOPWord, 13> words = {{
      0x3c010000 | (address >> 16), 0x34210000 | (address & 0xffff),
      0x34020000u | value, 0x10000002, 0xa4220000, 0xffffffff,
      0x84230000, 0xac030100, 0xac030104,
      0x94240000, 0xac040108, 0xac04010c, 0x24051234
    }};
    for (std::size_t index = 0; index < words.size(); ++index)
    {
      REQUIRE(system.iopBus().write32(static_cast<IOPAddress>(index * 4),
        words[index]).status == IOPBusStatus::Completed);
    }
    system.iopCore().startExecution(0xa0000000);
  }

  void requireConfigurationResult(
    NekoSystem &system, IOPAddress physical, IOPWord signedLoad)
  {
    REQUIRE(system.iopCore().retiredInstructions() == 12);
    REQUIRE(system.iopCore().programCounter() == 0xa0000034);
    REQUIRE(system.iopCore().exception().kind == IOPException::None);
    REQUIRE(system.iopCore().executionState() == IOPExecutionState::Running);
    REQUIRE_FALSE(system.iopCore().branchContinuation().active);
    REQUIRE(system.iopCore().delayedResultContinuation().source ==
      IOPDelayedResultSource::None);
    const auto value = signedLoad & 0xffff;
    REQUIRE(system.iopCore().generalRegister(3) == signedLoad);
    REQUIRE(system.iopCore().generalRegister(4) == value);
    REQUIRE(system.iopCore().generalRegister(5) == 0x1234);
    REQUIRE(system.iopBus().read32(0x100).value == 0);
    REQUIRE(system.iopBus().read32(0x104).value == signedLoad);
    REQUIRE(system.iopBus().read32(0x108).value == 0);
    REQUIRE(system.iopBus().read32(0x10c).value == value);
    for (std::size_t index = 0; index < configurationAddresses.size(); ++index)
    {
      REQUIRE(system.iopBus().read16(configurationAddresses[index]).value ==
        (configurationAddresses[index] == physical ? value :
          configurationValues[index]));
    }
    for (std::size_t index = 0; index < masterVolumeAddresses.size(); ++index)
    {
      REQUIRE(system.iopBus().read16(masterVolumeAddresses[index]).value ==
        (masterVolumeAddresses[index] == physical ? value :
          masterVolumeValues[index]));
    }
    for (std::size_t index = 0; index < voiceMixAddresses.size(); ++index)
    {
      REQUIRE(system.iopBus().read16(voiceMixAddresses[index]).value ==
        (voiceMixAddresses[index] == physical ? value : voiceMixValues[index]));
    }
    for (std::size_t index = 0; index < voiceModeAddresses.size(); ++index)
    {
      REQUIRE(system.iopBus().read16(voiceModeAddresses[index]).value ==
        (voiceModeAddresses[index] == physical ? value : voiceModeValues[index]));
    }
    for (std::size_t core = 0; core < mainMixAddresses.size(); ++core)
    {
      REQUIRE(system.iopBus().read16(mainMixAddresses[core]).value ==
        (mainMixAddresses[core] == physical ? value : mainMixValues[core]));
    }
    for (std::size_t index = 0; index < effectInputVolumeAddresses.size(); ++index)
    {
      REQUIRE(system.iopBus().read16(effectInputVolumeAddresses[index]).value ==
        (effectInputVolumeAddresses[index] == physical ? value :
        effectInputVolumeValues[index]));
    }
    for (std::size_t core = 0; core < effectEndAddressHighAddresses.size(); ++core)
    {
      REQUIRE(system.iopBus().read16(effectEndAddressHighAddresses[core]).value ==
        (effectEndAddressHighAddresses[core] == physical ? value :
        effectEndAddressHighValues[core]));
    }
    for (std::size_t index = 0; index < transferStartAddressAddresses.size(); ++index)
    {
      REQUIRE(system.iopBus().read16(transferStartAddressAddresses[index]).value ==
        (transferStartAddressAddresses[index] == physical ? value :
        transferStartAddressValues[index]));
    }
    REQUIRE(system.iopInterruptController().status() == 0);
  }

  void installInitializationStoreProgram(NekoSystem &system, IOPAddress address,
    std::uint16_t value)
  {
    const std::array<IOPWord, 7> words = {{
      0x3c010000 | (address >> 16), 0x34210000 | (address & 0xffff),
      0x34020000u | value, 0x10000002, 0xa4220000, 0xffffffff, 0x24031234
    }};
    for (std::size_t index = 0; index < words.size(); ++index)
    {
      REQUIRE(system.iopBus().write32(static_cast<IOPAddress>(index * 4),
        words[index]).status == IOPBusStatus::Completed);
    }
    system.iopCore().startExecution(0xa0000000);
  }

  void installHalfwordReadProgram(
    NekoSystem &system, IOPAddress address, IOPWord load)
  {
    const std::array<IOPWord, 9> words = {{
      0x3c010000 | (address >> 16), 0x34210000 | (address & 0xffff),
      0x2402ffff, 0x10000002, load, 0xffffffff,
      0x00401821, 0x00402021, 0x24051234
    }};
    for (std::size_t index = 0; index < words.size(); ++index)
    {
      REQUIRE(system.iopBus().write32(static_cast<IOPAddress>(index * 4),
        words[index]).status == IOPBusStatus::Completed);
    }
    system.iopCore().startExecution(0xa0000000);
  }

  void prepareInactiveInputs(NekoSystem &system)
  {
    auto &spu = system.spu2Controller();
    SPU2InputTest::programProfile(spu);
    for (std::size_t core = 0; core < coreStatusAddresses.size(); ++core)
    {
      REQUIRE(system.iopBus().write16(autoDMAAddresses[core],
        autoDMAWriteSelections[core]).status == IOPBusStatus::Completed);
      REQUIRE(spu.writeInputWord(core, 0x12345678));
      REQUIRE(system.iopBus().write16(autoDMAAddresses[core], 0).status ==
        IOPBusStatus::Completed);
    }
  }
}

TEST_CASE("SPU2 inactive status follows each core's input lifecycle",
  "[iop][spu2][spu2-input][spu2-statx]")
{
  for (std::size_t core = 0; core < coreStatusAddresses.size(); ++core)
  {
    CAPTURE(core);
    NekoSystem system;
    auto &bus = system.iopBus();
    auto &spu = system.spu2Controller();
    const auto peer = 1 - core;
    REQUIRE(bus.write16(autoDMAAddresses[core], autoDMAWriteSelections[core]).status ==
      IOPBusStatus::Unmapped);
    SPU2InputTest::programProfile(spu);
    REQUIRE(bus.write16(autoDMAAddresses[peer], autoDMAWriteSelections[peer]).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.read16(coreStatusAddresses[core]).status == IOPBusStatus::Completed);
    REQUIRE(bus.write16(autoDMAAddresses[core], autoDMAWriteSelections[core]).status ==
      IOPBusStatus::Completed);
    REQUIRE(spu.writeInputWord(core, 0x24681357));
    REQUIRE(bus.read16(coreStatusAddresses[core]).status == IOPBusStatus::Unmapped);
    spu.cancelInputWrite(core);
    REQUIRE(bus.read16(coreStatusAddresses[core]).status == IOPBusStatus::Unmapped);
    bus.reset();
    system.iopCore().reset();
    system.runMasterCycles(80);
    REQUIRE(bus.read16(coreStatusAddresses[core]).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write16(autoDMAAddresses[core], 0).status == IOPBusStatus::Completed);
    const auto before = system.iopStateHash();
    for (unsigned repeat = 0; repeat < 3; ++repeat)
    {
      const auto read = bus.read16(coreStatusAddresses[core]);
      REQUIRE(read.status == IOPBusStatus::Completed);
      REQUIRE(read.value == 0);
      REQUIRE(bus.read16(coreStatusAddresses[peer]).status == IOPBusStatus::Unmapped);
    }
    REQUIRE(system.iopStateHash() == before);
    REQUIRE(spu.readSoundRAMHalfword(0x2100 + core * 0x400) == 0x1357);
    REQUIRE(bus.write16(autoDMAAddresses[core], autoDMAWriteSelections[core]).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.read16(coreStatusAddresses[core]).status == IOPBusStatus::Unmapped);
    system.reset();
    REQUIRE(bus.read16(coreStatusAddresses[core]).status == IOPBusStatus::Completed);
  }
}

TEST_CASE("SPU2 PCM input clock starts on the 768th IOP pulse",
  "[iop][spu2][spu2-input]")
{
  NekoSystem system;
  system.runMasterCycles(6136);
  auto fields = inspectSaveState(system.saveState());
  const auto *phase = fields.find("system.iop.spu2.inputClockPhase");
  const auto *position = fields.find("system.iop.spu2.inputReadPosition");
  REQUIRE(phase != nullptr);
  REQUIRE(position != nullptr);
  REQUIRE(phase->scalarValue == 767);
  REQUIRE(position->scalarValue == 0);
  system.runMasterCycles(1);
  fields = inspectSaveState(system.saveState());
  REQUIRE(fields.find("system.iop.spu2.inputClockPhase")->scalarValue == 0);
  REQUIRE(fields.find("system.iop.spu2.inputReadPosition")->scalarValue == 1);
  system.runMasterCycles(6143);
  fields = inspectSaveState(system.saveState());
  REQUIRE(fields.find("system.iop.spu2.inputReadPosition")->scalarValue == 1);
  system.runMasterCycles(1);
  fields = inspectSaveState(system.saveState());
  REQUIRE(fields.find("system.iop.spu2.inputReadPosition")->scalarValue == 2);
}

TEST_CASE("SPU2 PCM input halves use planar words and real shared capacity",
  "[iop][spu2][spu2-input][spu2-admas-read]")
{
  SPU2Controller spu;
  IOPBus bus;
  bus.attachSPU2(&spu);
  SPU2InputTest::programProfile(spu);
  for (std::size_t core = 0; core < 2; ++core)
  {
    REQUIRE_FALSE(spu.beginInputWrite(core));
    REQUIRE_FALSE(spu.writeInputWord(core, 0xffffffff));
    REQUIRE(spu.writeAutoDMAWriteSelection(core, 1u << core));
    for (std::uint32_t word = 0; word < 256; ++word)
    {
      REQUIRE(spu.inputWriteRequested(core));
      REQUIRE(spu.writeInputWord(core, 0x80010000u + word));
      const auto address = static_cast<std::uint32_t>(0x2100 + core * 0x400 +
        (word / 128) * 0x200 + (word % 128) * 2);
      REQUIRE(spu.readSoundRAMHalfword(address) == word);
      REQUIRE(spu.readSoundRAMHalfword(address + 1) == 0x8001);
    }
    REQUIRE_FALSE(spu.inputWriteRequested(core));
    REQUIRE_FALSE(spu.writeInputWord(core, 0xffffffff));
    const auto read = bus.read16(autoDMAAddresses[core]);
    REQUIRE(read.status == IOPBusStatus::Completed);
    REQUIRE(read.value == autoDMAWriteSelections[core]);
    REQUIRE_FALSE(spu.inputWriteRequested(core));
  }
  auto tick = SPU2InputTest::advanceSamples(spu, 255);
  REQUIRE(tick.sampleTick);
  REQUIRE(tick.samples[0].consumed);
  REQUIRE(tick.samples[0].left == 0);
  REQUIRE_FALSE(spu.inputWriteRequested(0));
  tick = SPU2InputTest::advanceSamples(spu, 1);
  REQUIRE(spu.inputWriteRequested(0));
  REQUIRE(spu.inputWriteRequested(1));
  for (std::size_t core = 0; core < 2; ++core)
  {
    REQUIRE(bus.read16(autoDMAAddresses[core]).value == autoDMAWriteSelections[core]);
    REQUIRE(spu.inputWriteRequested(core));
  }
  tick = SPU2InputTest::advanceSamples(spu, 1);
  for (std::size_t core = 0; core < 2; ++core)
  {
    REQUIRE(tick.samples[core].consumed);
    REQUIRE(tick.samples[core].left == 0);
    REQUIRE(tick.samples[core].right == 128);
    REQUIRE(spu.writeInputWord(core, 0xfedcba98));
    REQUIRE(spu.readSoundRAMHalfword(0x2000 + core * 0x400) == 0xba98);
    REQUIRE(spu.readSoundRAMHalfword(0x2001 + core * 0x400) == 0xfedc);
  }
}

TEST_CASE("SPU2 PCM starvation preserves partial targets and cancellation preserves phase",
  "[iop][spu2][spu2-input]")
{
  SPU2Controller spu;
  SPU2InputTest::programProfile(spu);
  SPU2InputTest::advanceSamples(spu, 255);
  REQUIRE(spu.writeAutoDMAWriteSelection(0, 1));
  REQUIRE(spu.writeInputWord(0, 0x24681357));
  SPU2InputTest::advanceSamples(spu, 1);
  REQUIRE_FALSE(spu.inputWriteRequested(0));
  REQUIRE_FALSE(spu.writeInputWord(0, 0xffffffff));
  const auto tick = SPU2InputTest::advanceSamples(spu, 1);
  REQUIRE(tick.samples[0].left == 0x1357);
  REQUIRE(tick.samples[0].right == 0);
  REQUIRE(tick.samples[1].consumed == false);
  SPU2InputTest::advanceSamples(spu, 255);
  REQUIRE(spu.inputWriteRequested(0));
  REQUIRE(spu.writeInputWord(0, 0x89abcdef));
  REQUIRE(spu.readSoundRAMHalfword(0x2100) == 0x1357);
  REQUIRE(spu.readSoundRAMHalfword(0x2102) == 0xcdef);
  spu.cancelInputWrite(0);
  REQUIRE_FALSE(spu.inputWriteRequested(0));
  REQUIRE(spu.writeAutoDMAWriteSelection(0, 1));
  REQUIRE_FALSE(spu.inputWriteRequested(0));
  SPU2InputTest::advanceSamples(spu, 256);
  REQUIRE(spu.beginInputWrite(0));
  REQUIRE(spu.writeInputWord(0, 0x12345678));
  REQUIRE(spu.readSoundRAMHalfword(0x2000) == 0x5678);
  REQUIRE(spu.readSoundRAMHalfword(0x2100) == 0x1357);
  REQUIRE(spu.writeAutoDMAWriteSelection(0, 0));
  REQUIRE_FALSE(spu.inputWriteRequested(0));
  REQUIRE(spu.readCoreStatus(0).status == SPU2AccessStatus::Accepted);
  REQUIRE(spu.readCoreStatus(0).value == 0);
  SPU2InputTest::advanceSamples(spu, 256);
  REQUIRE(spu.writeAutoDMAWriteSelection(0, 1));
  REQUIRE(spu.writeInputWord(0, 0x76543210));
  REQUIRE(spu.readSoundRAMHalfword(0x2100) == 0x3210);
  spu.reset();
  REQUIRE(spu.readCoreStatus(0).status == SPU2AccessStatus::Accepted);
  REQUIRE(spu.readCoreStatus(0).value == 0);
  REQUIRE_FALSE(spu.inputWriteRequested(0));
  REQUIRE(spu.readSoundRAMHalfword(0x2100) == 0);
}

TEST_CASE("SPU2 selected PCM input guards only its live profile",
  "[iop][spu2][spu2-input]")
{
  NekoSystem system;
  auto &spu = system.spu2Controller();
  SPU2InputTest::programProfile(spu);
  REQUIRE(system.iopBus().write16(autoDMAAddresses[1], 2).status ==
    IOPBusStatus::Completed);
  const auto before = system.saveState();
  for (std::size_t index = 0; index < configurationAddresses.size(); ++index)
  {
    const auto value = SPU2InputTest::configuration[index];
    REQUIRE(system.iopBus().write16(configurationAddresses[index], value).status ==
      IOPBusStatus::Completed);
    REQUIRE(system.iopBus().write16(configurationAddresses[index], value ^ 1).status ==
      IOPBusStatus::Unmapped);
  }
  REQUIRE(system.iopBus().write16(coreAttributeAddresses[1], 0xc001).status ==
    IOPBusStatus::Completed);
  REQUIRE(system.iopBus().write16(coreAttributeAddresses[1], 0xc000).status ==
    IOPBusStatus::Unmapped);
  REQUIRE(system.saveState() == before);
  REQUIRE(system.iopBus().write16(coreAttributeAddresses[0], 0).status ==
    IOPBusStatus::Completed);
  REQUIRE(system.iopBus().read16(coreStatusAddresses[0]).status ==
    IOPBusStatus::Completed);
  REQUIRE(system.iopBus().read16(coreStatusAddresses[1]).status ==
    IOPBusStatus::Unmapped);
  REQUIRE(system.iopBus().write16(autoDMAAddresses[1], 0).status ==
    IOPBusStatus::Completed);
  REQUIRE(system.iopBus().write16(configurationAddresses[0], 0).status ==
    IOPBusStatus::Completed);
  REQUIRE(system.iopBus().read16(coreStatusAddresses[1]).status ==
    IOPBusStatus::Completed);
  REQUIRE(system.iopBus().write32(0x1f801508, 0x01000201).status ==
    IOPBusStatus::Unmapped);
}

TEST_CASE("SPU2 PCM partial and full buffers restore across sample boundaries",
  "[iop][spu2][spu2-input][save-state][trace]")
{
  for (const auto cycles : {0u, 1572856u, 1572857u})
  {
    CAPTURE(cycles);
    NekoSystem source;
    auto &spu = source.spu2Controller();
    SPU2InputTest::programProfile(spu);
    REQUIRE(spu.writeAutoDMAWriteSelection(0, 1));
    REQUIRE(spu.writeAutoDMAWriteSelection(1, 2));
    REQUIRE(spu.writeInputWord(0, 0x12345678));
    for (unsigned word = 0; word < 256; ++word)
    {
      REQUIRE(spu.writeInputWord(1, 0x80000000u + word));
    }
    source.runMasterCycles(cycles);
    const auto checkpoint = source.saveState();
    NekoSystem restored;
    NekoSystem plain;
    restored.loadState(checkpoint);
    plain.loadState(checkpoint);
    source.startTrace();
    restored.startTrace();
    source.runMasterCycles(17);
    restored.runMasterCycles(17);
    plain.runMasterCycles(17);
    REQUIRE(source.saveState() == restored.saveState());
    REQUIRE(source.saveState() == plain.saveState());
    IOPTraceTest::requireEvents(source.trace(), restored.trace());
    REQUIRE(spu.inputWriteRequested(0) == (cycles == 0));
    REQUIRE(spu.inputWriteRequested(1) == (cycles != 0));
    for (auto *system : {&source, &restored, &plain})
    {
      auto &owner = system->spu2Controller();
      owner.cancelInputWrite(0);
      REQUIRE_FALSE(owner.inputWriteRequested(0));
      REQUIRE(owner.beginInputWrite(0));
      REQUIRE(owner.writeInputWord(0, 0x89abcdef));
    }
    REQUIRE(source.saveState() == restored.saveState());
    REQUIRE(source.saveState() == plain.saveState());
  }
}

TEST_CASE("SPU2 PCM invalid progress rejects transactionally",
  "[iop][spu2][spu2-input][save-state]")
{
  NekoSystem source;
  auto &spu = source.spu2Controller();
  SPU2InputTest::programProfile(spu);
  REQUIRE(spu.writeAutoDMAWriteSelection(0, 1));
  REQUIRE(spu.writeInputWord(0, 0x12345678));
  const auto checkpoint = source.saveState();
  NekoSystem destination;
  destination.runMasterCycles(9);
  destination.startTrace();
  const auto before = destination.saveState();
  const auto traceHash = destination.traceHash();
  struct InvalidField
  {
    const char *path;
    std::uint64_t value;
  };
  for (const auto field : {
      InvalidField{"system.iop.spu2.inputClockPhase", 768},
      InvalidField{"system.iop.spu2.inputReadPosition", 512},
      InvalidField{"system.iop.spu2.inputStates[0].everSelected", 0},
      InvalidField{"system.iop.spu2.inputStates[0].writeOpen", 0},
      InvalidField{"system.iop.spu2.inputStates[0].fillHalf", 2},
      InvalidField{"system.iop.spu2.inputStates[0].fillWords", 257},
      InvalidField{"system.iop.spu2.inputStates[1].writeOpen", 1},
      InvalidField{"system.iop.spu2.spdifConfiguration[4].value", 0},
      InvalidField{"system.iop.spu2.coreAttributes[0].value", 0}})
  {
    CAPTURE(field.path);
    SaveStateMutation invalid(checkpoint);
    invalid.writeScalar(field.path, field.value);
    invalid.refreshChecksum();
    REQUIRE_THROWS_AS(destination.loadState(invalid), std::invalid_argument);
    REQUIRE(destination.saveState() == before);
    REQUIRE(destination.traceEnabled());
    REQUIRE(destination.traceHash() == traceHash);
  }
  SaveStateMutation invalidFull(checkpoint);
  invalidFull.writeScalar("system.iop.spu2.inputStates[0].fillWords", 256);
  invalidFull.writeScalar("system.iop.spu2.inputStates[0].fillHalf", 0);
  invalidFull.refreshChecksum();
  REQUIRE_THROWS_AS(destination.loadState(invalidFull), std::invalid_argument);
  REQUIRE(destination.saveState() == before);
}

TEST_CASE("SPU2 sound RAM has a fixed canonical byte region",
  "[iop][spu2][sound-ram][save-state]")
{
  NekoSystem system;
  const auto state = system.saveState();
  const auto snapshot = inspectSaveState(state);
  const auto *memory = snapshot.find("system.iop.spu2.soundRAM");
  REQUIRE(memory != nullptr);
  REQUIRE(memory->kind == SaveStateFieldKind::Bytes);
  REQUIRE(memory->size == 2 * 1024 * 1024);
  REQUIRE(memory->containerOffset + memory->size <= state.size());
  const auto begin = state.begin() + memory->containerOffset;
  REQUIRE(std::all_of(begin, begin + memory->size,
    [](std::uint8_t value) { return value == 0; }));
}

TEST_CASE("SPU2 sound RAM uses checked halfword addresses without wrapping",
  "[iop][spu2][sound-ram]")
{
  SPU2Controller spu2;
  for (const std::uint32_t address : {0u, 0x2000u, 0x2200u, 0x2400u, 0xfffffu})
  {
    REQUIRE(spu2.readSoundRAMHalfword(address) == 0);
    spu2.writeSoundRAMHalfword(address, 0x8123);
    REQUIRE(spu2.readSoundRAMHalfword(address) == 0x8123);
  }
  spu2.writeSoundRAMHalfword(0x2001, 0x4567);
  REQUIRE(spu2.readSoundRAMHalfword(0x2000) == 0x8123);
  REQUIRE(spu2.readSoundRAMHalfword(0x2001) == 0x4567);
  REQUIRE(spu2.readSoundRAMHalfword(0x2002) == 0);
  for (const std::uint32_t address : {0x100000u, 0xffffffffu})
  {
    REQUIRE_THROWS_AS(spu2.readSoundRAMHalfword(address), std::out_of_range);
    REQUIRE_THROWS_AS(spu2.writeSoundRAMHalfword(address, 0xffff),
      std::out_of_range);
  }
  REQUIRE(spu2.readSoundRAMHalfword(0) == 0x8123);
  REQUIRE(spu2.readSoundRAMHalfword(0xfffff) == 0x8123);
}

TEST_CASE("SPU2 sound RAM survives local resets and clears on owner or machine reset",
  "[iop][spu2][sound-ram]")
{
  NekoSystem system;
  auto &spu2 = system.spu2Controller();
  for (const std::uint32_t address : {0u, 0x2000u, 0x2400u, 0xfffffu})
  {
    spu2.writeSoundRAMHalfword(address, 0xabcd);
  }
  system.runMasterCycles(80);
  system.iopBus().reset();
  system.iopCore().reset();
  for (const std::uint32_t address : {0u, 0x2000u, 0x2400u, 0xfffffu})
  {
    REQUIRE(spu2.readSoundRAMHalfword(address) == 0xabcd);
  }
  spu2.reset();
  for (const std::uint32_t address : {0u, 0x2000u, 0x2400u, 0xfffffu})
  {
    REQUIRE(spu2.readSoundRAMHalfword(address) == 0);
    spu2.writeSoundRAMHalfword(address, 0x1234);
  }
  system.reset();
  for (const std::uint32_t address : {0u, 0x2000u, 0x2400u, 0xfffffu})
  {
    REQUIRE(spu2.readSoundRAMHalfword(address) == 0);
  }
}

TEST_CASE("SPU2 sound RAM restores little endian bytes without aliasing or trace effects",
  "[iop][spu2][sound-ram][save-state]")
{
  NekoSystem source;
  auto previousHash = source.iopStateHash();
  const std::array<std::uint32_t, 4> addresses = {{0, 0x2000, 0x2400, 0xfffff}};
  for (std::size_t index = 0; index < addresses.size(); ++index)
  {
    source.spu2Controller().writeSoundRAMHalfword(addresses[index],
      static_cast<std::uint16_t>(0x8123 + index));
    REQUIRE(source.iopStateHash() != previousHash);
    previousHash = source.iopStateHash();
  }
  const auto state = source.saveState();
  const auto snapshot = inspectSaveState(state);
  const auto *memory = snapshot.find("system.iop.spu2.soundRAM");
  REQUIRE(memory != nullptr);
  REQUIRE(memory->size == SPU2Controller::SOUND_RAM_SIZE);
  for (std::size_t index = 0; index < addresses.size(); ++index)
  {
    const auto offset = memory->containerOffset + addresses[index] * 2;
    REQUIRE(state[offset] == 0x23 + index);
    REQUIRE(state[offset + 1] == 0x81);
  }
  NekoSystem restored;
  restored.spu2Controller().writeSoundRAMHalfword(1, 0xffff);
  restored.loadState(state);
  REQUIRE(restored.iopStateHash() == previousHash);
  REQUIRE(restored.spu2Controller().readSoundRAMHalfword(1) == 0);
  REQUIRE(restored.saveState() == state);
  NekoSystem plain;
  plain.loadState(state);
  source.startTrace();
  restored.startTrace();
  source.runMasterCycles(32);
  restored.runMasterCycles(32);
  plain.runMasterCycles(32);
  IOPTraceTest::requireEvents(restored.trace(), source.trace());
  IOPTraceTest::requireMachine(restored, source);
  IOPTraceTest::requireMachine(plain, source);
  source.spu2Controller().reset();
  restored.spu2Controller().writeSoundRAMHalfword(0x2000, 0x4567);
  REQUIRE(source.spu2Controller().readSoundRAMHalfword(0x2000) == 0);
  REQUIRE(restored.spu2Controller().readSoundRAMHalfword(0x2000) == 0x4567);
  REQUIRE(plain.spu2Controller().readSoundRAMHalfword(0x2000) == 0x8124);
}

TEST_CASE("SPU2 Auto DMA write selection admits each core bit without starting DMA",
  "[iop][spu2][spu2-autodma]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  programConfiguration(system);
  programMasterVolumes(system);
  programTransferStartAddresses(system);
  SPU2InputTest::programProfile(system.spu2Controller());
  system.iopInterruptController().assertSource(IOPInterruptSource::SPU);
  system.iopDMAController().writeAuxiliaryControl(0x12345678);
  const auto before = system.iopStateHash();
  for (std::size_t core = 0; core < autoDMAAddresses.size(); ++core)
  {
    CAPTURE(core);
    REQUIRE(system.spu2Controller().readAutoDMAWriteSelection(core) == 0);
    REQUIRE(bus.write16(autoDMAAddresses[core], autoDMAWriteSelections[core]).status ==
      IOPBusStatus::Completed);
    REQUIRE(system.spu2Controller().readAutoDMAWriteSelection(core) ==
      autoDMAWriteSelections[core]);
    REQUIRE(system.spu2Controller().readAutoDMAWriteSelection(1 - core) == 0);
    const auto enabled = system.iopStateHash();
    REQUIRE(bus.write16(autoDMAAddresses[core], autoDMAWriteSelections[core]).status ==
      IOPBusStatus::Completed);
    REQUIRE(system.iopStateHash() == enabled);
    for (std::uint32_t value = 1; value <= 0xffff; ++value)
    {
      if (value == autoDMAWriteSelections[core])
      {
        continue;
      }
      const auto image = static_cast<std::uint16_t>(value);
      if (system.spu2Controller().writeAutoDMAWriteSelection(core, image) ||
          bus.write16(autoDMAAddresses[core], image).status != IOPBusStatus::Unmapped ||
          system.spu2Controller().readAutoDMAWriteSelection(core) !=
            autoDMAWriteSelections[core] ||
          system.spu2Controller().readAutoDMAWriteSelection(1 - core) != 0)
      {
        FAIL("Unsupported Auto DMA selection changed programming: " << value);
      }
    }
    REQUIRE(system.iopStateHash() == enabled);
    REQUIRE(bus.read16(autoDMAAddresses[core]).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(autoDMAAddresses[core]).value == autoDMAWriteSelections[core]);
    for (IOPAddress address : {
        0x1f9001acu, 0x1f9005acu, 0x1f8010c0u, 0x1f8010c8u,
        0x1f801500u, 0x1f801508u})
    {
      REQUIRE(bus.write16(address, 1).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write32(address, 0x01000001).status == IOPBusStatus::Unmapped);
    }
    for (std::size_t index = 0; index < transferStartAddressAddresses.size(); ++index)
    {
      REQUIRE(bus.read16(transferStartAddressAddresses[index]).value ==
        transferStartAddressValues[index]);
    }
    for (std::size_t statusCore = 0; statusCore < 2; ++statusCore)
    {
      CAPTURE(statusCore);
      REQUIRE(bus.read16(coreStatusAddresses[statusCore]).status ==
        (statusCore == core ? IOPBusStatus::Unmapped : IOPBusStatus::Completed));
    }
    REQUIRE(system.iopInterruptController().status() == (1u << 9));
    REQUIRE(system.iopDMAController().auxiliaryControl() == 0x12345678);
    REQUIRE(bus.write16(autoDMAAddresses[core], 0).status == IOPBusStatus::Completed);
    REQUIRE(system.spu2Controller().readAutoDMAWriteSelection(core) == 0);
    REQUIRE(system.iopStateHash() != before);
  }
  for (std::size_t core = 0; core < autoDMAAddresses.size(); ++core)
  {
    REQUIRE(bus.write16(autoDMAAddresses[core], autoDMAWriteSelections[core]).status ==
      IOPBusStatus::Completed);
  }
  REQUIRE(bus.write16(autoDMAAddresses[0], 0).status == IOPBusStatus::Completed);
  REQUIRE(system.spu2Controller().readAutoDMAWriteSelection(0) == 0);
  REQUIRE(system.spu2Controller().readAutoDMAWriteSelection(1) == 2);
  REQUIRE(bus.write16(autoDMAAddresses[0], 1).status == IOPBusStatus::Completed);
  system.runMasterCycles(80);
  bus.reset();
  system.iopCore().reset();
  for (std::size_t core = 0; core < autoDMAAddresses.size(); ++core)
  {
    REQUIRE(system.spu2Controller().readAutoDMAWriteSelection(core) ==
      autoDMAWriteSelections[core]);
  }
  REQUIRE(system.iopInterruptController().status() == (1u << 9));
  REQUIRE(system.iopDMAController().auxiliaryControl() == 0x12345678);
  system.spu2Controller().reset();
  SPU2InputTest::programProfile(system.spu2Controller());
  for (std::size_t core = 0; core < autoDMAAddresses.size(); ++core)
  {
    REQUIRE(system.spu2Controller().readAutoDMAWriteSelection(core) == 0);
    REQUIRE(bus.write16(autoDMAAddresses[core], autoDMAWriteSelections[core]).status ==
      IOPBusStatus::Completed);
  }
  system.reset();
  for (std::size_t core = 0; core < autoDMAAddresses.size(); ++core)
  {
    REQUIRE(system.spu2Controller().readAutoDMAWriteSelection(core) == 0);
  }
}

TEST_CASE("SPU2 transfer start addresses retain raw halfwords without transfers",
  "[iop][spu2][spu2-tsa]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  for (const auto address : transferStartAddressAddresses)
  {
    REQUIRE(bus.read16(address).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(address).value == 0);
  }
  programConfiguration(system);
  programMasterVolumes(system);
  programVoiceMix(system);
  programMainMix(system);
  programEffectInputVolumes(system);
  programEffectEndAddresses(system);
  for (const auto address : coreAttributeAddresses)
  {
    REQUIRE(bus.write16(address, 0xc001).status == IOPBusStatus::Completed);
    REQUIRE(bus.write16(address, 0xc031).status == IOPBusStatus::Unmapped);
  }
  system.iopInterruptController().assertSource(IOPInterruptSource::SPU);
  system.iopDMAController().writeAuxiliaryControl(0x12345678);
  const auto before = system.iopStateHash();
  for (std::size_t selected = 0; selected < transferStartAddressAddresses.size(); ++selected)
  {
    const auto address = transferStartAddressAddresses[selected];
    CAPTURE(address);
    for (std::uint32_t value = 0; value <= 0xffff; ++value)
    {
      const auto write = bus.write16(address, static_cast<std::uint16_t>(value));
      const auto read = bus.read16(address);
      if (write.status != IOPBusStatus::Completed ||
          read.status != IOPBusStatus::Completed || read.value != value)
      {
        FAIL("Transfer-start image did not round-trip: " << value);
      }
    }
    for (std::size_t index = 0; index < transferStartAddressAddresses.size(); ++index)
    {
      REQUIRE(bus.read16(transferStartAddressAddresses[index]).value ==
        (index == selected ? 0xffff : 0));
    }
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Completed);
  }
  REQUIRE(system.iopStateHash() == before);
  for (std::size_t index = 0; index < transferStartAddressAddresses.size(); ++index)
  {
    REQUIRE(bus.write16(transferStartAddressAddresses[index],
      transferStartAddressValues[index]).status == IOPBusStatus::Completed);
  }
  const auto programmed = system.iopStateHash();
  for (const auto address : coreStatusAddresses)
  {
    REQUIRE(bus.read16(address).value == 0);
  }
  for (const auto address : autoDMAAddresses)
  {
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Completed);
  }
  REQUIRE(system.iopStateHash() == programmed);
  system.runMasterCycles(80);
  bus.reset();
  system.iopCore().reset();
  for (std::size_t index = 0; index < transferStartAddressAddresses.size(); ++index)
  {
    REQUIRE(bus.read16(transferStartAddressAddresses[index]).value ==
      transferStartAddressValues[index]);
  }
  REQUIRE(system.iopInterruptController().status() == (1u << 9));
  REQUIRE(system.iopDMAController().auxiliaryControl() == 0x12345678);
  system.spu2Controller().reset();
  for (const auto address : transferStartAddressAddresses)
  {
    REQUIRE(bus.read16(address).value == 0);
    REQUIRE(bus.write16(address, 0xffff).status == IOPBusStatus::Completed);
  }
  system.reset();
  for (const auto address : transferStartAddressAddresses)
  {
    REQUIRE(bus.read16(address).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(address).value == 0);
  }
}

TEST_CASE("SPU2 CPU data ports advance independent wrapping cursors in shared RAM",
  "[iop][spu2][spu2-pio]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  auto &spu = system.spu2Controller();
  for (std::size_t core = 0; core < 2; ++core)
  {
    REQUIRE(bus.write16(transferStartAddressAddresses[core * 2], 0xffff).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.write16(transferStartAddressAddresses[core * 2 + 1], 0xffff).status ==
      IOPBusStatus::Completed);
  }
  REQUIRE(bus.write16(transferDataAddresses[0], 0x0707).status == IOPBusStatus::Completed);
  REQUIRE(spu.readSoundRAMHalfword(0xfffff) == 0x0707);
  REQUIRE(bus.write16(transferDataAddresses[0], 0xabcd).status == IOPBusStatus::Completed);
  REQUIRE(spu.readSoundRAMHalfword(0) == 0xabcd);
  REQUIRE(bus.write16(transferDataAddresses[1], 0x1234).status == IOPBusStatus::Completed);
  REQUIRE(spu.readSoundRAMHalfword(0xfffff) == 0x1234);
  REQUIRE(spu.readSoundRAMHalfword(0) == 0xabcd);
  REQUIRE(bus.write16(transferDataAddresses[1], 0xffff).status == IOPBusStatus::Completed);
  REQUIRE(spu.readSoundRAMHalfword(0) == 0xffff);
  REQUIRE(bus.write16(transferDataAddresses[0], 0).status == IOPBusStatus::Completed);
  REQUIRE(spu.readSoundRAMHalfword(1) == 0);
  for (const auto address : transferStartAddressAddresses)
  {
    REQUIRE(bus.read16(address).value == 0xffff);
  }
  REQUIRE(bus.write16(transferStartAddressAddresses[0], 0x8000).status ==
    IOPBusStatus::Completed);
  REQUIRE(bus.write16(transferDataAddresses[0], 0x5678).status == IOPBusStatus::Completed);
  REQUIRE(spu.readSoundRAMHalfword(0xffff) == 0x5678);
  REQUIRE(bus.write16(transferStartAddressAddresses[1], 0x2800).status ==
    IOPBusStatus::Completed);
  REQUIRE(bus.write16(transferDataAddresses[0], 0x0707).status == IOPBusStatus::Completed);
  REQUIRE(spu.readSoundRAMHalfword(0x2800) == 0x0707);
  REQUIRE(bus.write16(transferDataAddresses[0], 0x0707).status == IOPBusStatus::Completed);
  REQUIRE(spu.readSoundRAMHalfword(0x2801) == 0x0707);
  system.runMasterCycles(80);
  bus.reset();
  system.iopCore().reset();
  REQUIRE(bus.write16(transferDataAddresses[0], 0x9abc).status == IOPBusStatus::Completed);
  REQUIRE(spu.readSoundRAMHalfword(0x2802) == 0x9abc);
  REQUIRE(bus.read16(transferStartAddressAddresses[0]).value == 0x8000);
  REQUIRE(bus.read16(transferStartAddressAddresses[1]).value == 0x2800);
  spu.reset();
  REQUIRE(spu.readSoundRAMHalfword(0x2802) == 0);
  REQUIRE(bus.write16(transferDataAddresses[0], 0x4321).status == IOPBusStatus::Completed);
  REQUIRE(spu.readSoundRAMHalfword(0) == 0x4321);
  system.reset();
  REQUIRE(bus.write16(transferDataAddresses[1], 0x9876).status == IOPBusStatus::Completed);
  REQUIRE(spu.readSoundRAMHalfword(0) == 0x9876);
  REQUIRE(spu.readSoundRAMHalfword(1) == 0);
}

TEST_CASE("SPU2 CPU data ports reject unsupported accesses without mutation",
  "[iop][spu2][spu2-pio]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  const auto before = system.iopStateHash();
  for (const auto address : transferDataAddresses)
  {
    CAPTURE(address);
    REQUIRE(bus.read16(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read8(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read8(address + 1).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(address, 0xff).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(address + 1, 0xff).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read16(address + 1).status == IOPBusStatus::Misaligned);
    REQUIRE(bus.write16(address + 1, 0xffff).status == IOPBusStatus::Misaligned);
    REQUIRE(bus.read32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(address, 0xffffffff).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.readInstruction32(address).status == IOPBusStatus::Unmapped);
    for (std::uint8_t mask = 1; mask <= 0xf; ++mask)
    {
      REQUIRE(bus.writeMasked32(address, 0xffffffff, mask).status ==
        IOPBusStatus::Unmapped);
    }
    REQUIRE(bus.readDMAC32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.dmaWriteStatus(address) == IOPBusStatus::Unmapped);
    REQUIRE(bus.writeDMAC32(address, 0xffffffff).status == IOPBusStatus::Unmapped);
    std::array<std::uint8_t, 2> bytes = {{0x12, 0x34}};
    REQUIRE(bus.loadPhysical(address, bytes.data(), bytes.size()) == IOPBusStatus::Unmapped);
    REQUIRE(bus.inspectPhysical(address, bytes.data(), bytes.size()) == IOPBusStatus::Unmapped);
    REQUIRE(bytes == (std::array<std::uint8_t, 2>{{0x12, 0x34}}));
    std::uint16_t value = 0x4321;
    REQUIRE_FALSE(system.eeBus().readData16(address, &value));
    REQUIRE(value == 0x4321);
    REQUIRE_FALSE(system.eeBus().writeData16(address, 0xffff));
    IOPBus unattached;
    REQUIRE(unattached.write16(address, 0xffff).status == IOPBusStatus::Unmapped);
    for (IOPAddress other : {address + 2, address + 0x800,
        address | 0x80000000u, address | 0xa0000000u})
    {
      REQUIRE(bus.read16(other).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write16(other, 0xffff).status == IOPBusStatus::Unmapped);
    }
  }
  REQUIRE(system.iopStateHash() == before);
}

TEST_CASE("SPU2 CPU data ports reject either selected input atomically",
  "[iop][spu2][spu2-pio]")
{
  for (std::size_t selectedCore = 0; selectedCore < 2; ++selectedCore)
  for (bool open : {false, true})
  {
    NekoSystem system;
    auto &spu = system.spu2Controller();
    SPU2InputTest::programProfile(spu);
    REQUIRE(spu.writeAutoDMAWriteSelection(
      selectedCore, autoDMAWriteSelections[selectedCore]));
    if (!open)
    {
      spu.cancelInputWrite(selectedCore);
    }
    system.iopInterruptController().assertSource(IOPInterruptSource::SPU);
    system.iopDMAController().writeAuxiliaryControl(0x12345678);
    const auto before = system.iopStateHash();
    for (const auto address : transferDataAddresses)
    {
      REQUIRE(system.iopBus().write16(address, 0x0707).status == IOPBusStatus::Unmapped);
    }
    REQUIRE(system.iopStateHash() == before);
    REQUIRE(spu.writeAutoDMAWriteSelection(selectedCore, 0));
    for (const auto address : transferDataAddresses)
    {
      REQUIRE(system.iopBus().write16(address, 0x0707).status == IOPBusStatus::Completed);
    }
    REQUIRE(spu.readSoundRAMHalfword(0) == 0x0707);
    REQUIRE(spu.readSoundRAMHalfword(1) == 0);
    REQUIRE(system.iopInterruptController().status() == (1u << 9));
    REQUIRE(system.iopDMAController().auxiliaryControl() == 0x12345678);
  }
}

TEST_CASE("SPU2 CPU data stores retain alias and delay-slot retirement",
  "[iop][spu2][spu2-pio][trace]")
{
  for (std::size_t core = 0; core < 2; ++core)
  for (IOPAddress segment : {0u, 0x80000000u, 0xa0000000u})
  for (bool delaySlot : {false, true})
  {
    NekoSystem system;
    REQUIRE(system.iopBus().write16(transferStartAddressAddresses[core * 2 + 1],
      0x2800).status == IOPBusStatus::Completed);
    const auto address = transferDataAddresses[core] | segment;
    installInitializationStoreProgram(system, address, 0x0707);
    if (!delaySlot)
    {
      REQUIRE(system.iopBus().write32(0xc, 0).status == IOPBusStatus::Completed);
      REQUIRE(system.iopBus().write32(0x14, 0).status == IOPBusStatus::Completed);
    }
    system.startTrace();
    system.runMasterCycles(delaySlot ? 48 : 56);
    REQUIRE(system.iopCore().executionState() == IOPExecutionState::Running);
    REQUIRE(system.iopCore().exception().kind == IOPException::None);
    REQUIRE(system.iopCore().retiredInstructions() == (delaySlot ? 6 : 7));
    REQUIRE(system.iopCore().programCounter() == 0xa000001c);
    REQUIRE(system.iopCore().generalRegister(3) == 0x1234);
    REQUIRE(system.spu2Controller().readSoundRAMHalfword(0x2800) == 0x0707);
    IOPTraceTest::Events actual;
    for (const auto &event : system.trace())
    {
      if (event.subsystem == NekoTraceSubsystem::IOP &&
          event.type == NekoTraceEventType::MemoryAccess && event.value0 == address)
      {
        actual.push_back(event);
      }
    }
    namespace Memory = NekoIOPTraceMemory;
    const auto metadata = nekoPackIOPMemoryTraceMetadata({
      transferDataAddresses[core], 2, Memory::AccessKind::DataStore,
      Memory::Outcome::Completed, segment == 0xa0000000
        ? Memory::CacheRoute::Uncached : Memory::CacheRoute::Cached, true, 3});
    IOPTraceTest::requireEvents(actual, {
      {33, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, 0x0707, 0, metadata}
    });
  }
}

TEST_CASE("SPU2 CPU data cursors restore pending guest stores deterministically",
  "[iop][spu2][spu2-pio][save-state][trace]")
{
  NekoSystem source;
  for (std::size_t core = 0; core < 2; ++core)
  {
    REQUIRE(source.iopBus().write16(transferStartAddressAddresses[core * 2 + 1],
      static_cast<std::uint16_t>(0x2800 + core)).status == IOPBusStatus::Completed);
    REQUIRE(source.iopBus().write16(transferDataAddresses[core], 0x1234).status ==
      IOPBusStatus::Completed);
  }
  installInitializationStoreProgram(source, 0xbf9005ac, 0x0707);
  source.runMasterCycles(32);
  const auto saved = source.saveState();
  const auto fields = inspectSaveState(saved);
  for (std::size_t core = 0; core < 2; ++core)
  {
    const auto *cursor = fields.find(
      "system.iop.spu2.transferCursor[" + std::to_string(core) + "].value");
    REQUIRE(cursor != nullptr);
    REQUIRE(cursor->scalarValue == 0x2801 + core);
  }
  NekoSystem restored;
  restored.loadState(saved);
  REQUIRE(restored.saveState() == saved);
  source.startTrace();
  restored.startTrace();
  source.runMasterCycles(16);
  restored.runMasterCycles(16);
  IOPTraceTest::requireEvents(restored.trace(), source.trace());
  REQUIRE(restored.iopCore().retiredInstructions() == 6);
  REQUIRE(restored.spu2Controller().readSoundRAMHalfword(0x2802) == 0x0707);
  REQUIRE(restored.iopStateHash() == source.iopStateHash());
  REQUIRE(restored.saveState() == source.saveState());
  REQUIRE(restored.iopBus().write16(transferDataAddresses[0], 0xabcd).status ==
    IOPBusStatus::Completed);
  REQUIRE(restored.spu2Controller().readSoundRAMHalfword(0x2801) == 0xabcd);
  REQUIRE(source.spu2Controller().readSoundRAMHalfword(0x2801) == 0x1234);
  for (std::size_t core = 0; core < 2; ++core)
  {
    const auto path =
      "system.iop.spu2.transferCursor[" + std::to_string(core) + "].value";
    const auto before = restored.saveState();
    for (std::uint32_t value : {0x100000u, 0xffffffffu})
    {
      SaveStateMutation invalid(saved);
      invalid.writeScalar(path, value);
      invalid.refreshChecksum();
      REQUIRE_THROWS_AS(restored.loadState(invalid), std::invalid_argument);
      REQUIRE(restored.saveState() == before);
    }
    SaveStateMutation valid(saved);
    valid.writeScalar(path, 0xfffff);
    valid.refreshChecksum();
    restored.loadState(valid);
    REQUIRE(restored.iopStateHash() != source.iopStateHash());
    REQUIRE(restored.iopBus().write16(transferDataAddresses[core], 0x9876).status ==
      IOPBusStatus::Completed);
    REQUIRE(restored.spu2Controller().readSoundRAMHalfword(0xfffff) == 0x9876);
  }
}

TEST_CASE("SPU2 effect end addresses retain raw high halfwords without active effects",
  "[iop][spu2][spu2-eea]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  for (const auto address : effectEndAddressHighAddresses)
  {
    REQUIRE(bus.read16(address).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(address).value == 0);
  }
  programConfiguration(system);
  programMasterVolumes(system);
  programVoiceMix(system);
  programMainMix(system);
  programEffectInputVolumes(system);
  programEffectEndAddresses(system);
  for (const auto address : coreAttributeAddresses)
  {
    REQUIRE(bus.write16(address, 0xc001).status == IOPBusStatus::Completed);
    REQUIRE(bus.write16(address, 0xc081).status == IOPBusStatus::Unmapped);
  }
  system.iopInterruptController().assertSource(IOPInterruptSource::SPU);
  system.iopDMAController().writeAuxiliaryControl(0x12345678);
  const auto programmed = system.iopStateHash();
  for (std::size_t selected = 0; selected < effectEndAddressHighAddresses.size(); ++selected)
  {
    const auto address = effectEndAddressHighAddresses[selected];
    CAPTURE(address);
    for (std::uint32_t value = 0; value <= 0xffff; ++value)
    {
      const auto write = bus.write16(address, static_cast<std::uint16_t>(value));
      const auto read = bus.read16(address);
      if (write.status != IOPBusStatus::Completed ||
          read.status != IOPBusStatus::Completed || read.value != value)
      {
        FAIL("Effect-end high image did not round-trip: " << value);
      }
    }
    for (std::size_t core = 0; core < effectEndAddressHighAddresses.size(); ++core)
    {
      REQUIRE(bus.read16(effectEndAddressHighAddresses[core]).value ==
        (core == selected ? 0xffff : effectEndAddressHighValues[core]));
    }
    REQUIRE(bus.write16(address, effectEndAddressHighValues[selected]).status ==
      IOPBusStatus::Completed);
  }
  REQUIRE(system.iopStateHash() == programmed);
  for (const auto address : coreStatusAddresses)
  {
    REQUIRE(bus.read16(address).value == 0);
  }
  for (const auto &addresses : {keyOffLowAddresses, keyOffHighAddresses})
  for (const auto address : addresses)
  {
    REQUIRE(bus.write16(address, 0xffff).status == IOPBusStatus::Completed);
  }
  for (const auto address : autoDMAAddresses)
  {
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Completed);
  }
  REQUIRE(system.iopStateHash() == programmed);
  system.runMasterCycles(80);
  bus.reset();
  system.iopCore().reset();
  for (std::size_t core = 0; core < effectEndAddressHighAddresses.size(); ++core)
  {
    REQUIRE(bus.read16(effectEndAddressHighAddresses[core]).value ==
      effectEndAddressHighValues[core]);
    REQUIRE(bus.read16(coreAttributeAddresses[core]).value == 0xc001);
  }
  for (std::size_t index = 0; index < configurationAddresses.size(); ++index)
  {
    REQUIRE(bus.read16(configurationAddresses[index]).value == configurationValues[index]);
  }
  for (std::size_t index = 0; index < masterVolumeAddresses.size(); ++index)
  {
    REQUIRE(bus.read16(masterVolumeAddresses[index]).value == masterVolumeValues[index]);
  }
  for (std::size_t index = 0; index < voiceMixAddresses.size(); ++index)
  {
    REQUIRE(bus.read16(voiceMixAddresses[index]).value == voiceMixValues[index]);
  }
  for (std::size_t core = 0; core < mainMixAddresses.size(); ++core)
  {
    REQUIRE(bus.read16(mainMixAddresses[core]).value == mainMixValues[core]);
  }
  for (std::size_t index = 0; index < effectInputVolumeAddresses.size(); ++index)
  {
    REQUIRE(bus.read16(effectInputVolumeAddresses[index]).value ==
      effectInputVolumeValues[index]);
  }
  REQUIRE(system.iopInterruptController().status() == (1u << 9));
  REQUIRE(system.iopDMAController().auxiliaryControl() == 0x12345678);
  system.spu2Controller().reset();
  for (const auto address : effectEndAddressHighAddresses)
  {
    REQUIRE(bus.read16(address).value == 0);
  }
  programEffectEndAddresses(system);
  system.reset();
  for (const auto address : effectEndAddressHighAddresses)
  {
    REQUIRE(bus.read16(address).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(address).value == 0);
  }
}

TEST_CASE("SPU2 effect and input volumes retain full halfwords without active effects",
  "[iop][spu2][spu2-effect-input]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  for (const auto address : effectInputVolumeAddresses)
  {
    REQUIRE(bus.read16(address).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(address).value == 0);
  }
  programConfiguration(system);
  programMasterVolumes(system);
  programVoiceMix(system);
  programMainMix(system);
  programEffectInputVolumes(system);
  for (const auto address : coreAttributeAddresses)
  {
    REQUIRE(bus.write16(address, 0xc001).status == IOPBusStatus::Completed);
  }
  system.iopInterruptController().assertSource(IOPInterruptSource::SPU);
  system.iopDMAController().writeAuxiliaryControl(0x12345678);
  const auto programmed = system.iopStateHash();
  for (std::size_t selected = 0; selected < effectInputVolumeAddresses.size(); ++selected)
  {
    const auto address = effectInputVolumeAddresses[selected];
    CAPTURE(address);
    for (std::uint32_t value = 0; value <= 0xffff; ++value)
    {
      const auto write = bus.write16(address, static_cast<std::uint16_t>(value));
      const auto read = bus.read16(address);
      if (write.status != IOPBusStatus::Completed ||
          read.status != IOPBusStatus::Completed || read.value != value)
      {
        FAIL("Effect/input volume did not round-trip: " << value);
      }
    }
    for (std::size_t index = 0; index < effectInputVolumeAddresses.size(); ++index)
    {
      REQUIRE(bus.read16(effectInputVolumeAddresses[index]).value ==
        (index == selected ? 0xffff : effectInputVolumeValues[index]));
    }
    REQUIRE(bus.write16(address, effectInputVolumeValues[selected]).status ==
      IOPBusStatus::Completed);
  }
  REQUIRE(system.iopStateHash() == programmed);
  for (const auto address : coreStatusAddresses)
  {
    REQUIRE(bus.read16(address).value == 0);
  }
  for (const auto &addresses : {keyOffLowAddresses, keyOffHighAddresses})
  for (const auto address : addresses)
  {
    REQUIRE(bus.write16(address, 0xffff).status == IOPBusStatus::Completed);
  }
  for (const auto address : autoDMAAddresses)
  {
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Completed);
  }
  REQUIRE(system.iopStateHash() == programmed);
  system.runMasterCycles(80);
  bus.reset();
  system.iopCore().reset();
  for (std::size_t index = 0; index < effectInputVolumeAddresses.size(); ++index)
  {
    REQUIRE(bus.read16(effectInputVolumeAddresses[index]).value ==
      effectInputVolumeValues[index]);
  }
  for (std::size_t index = 0; index < configurationAddresses.size(); ++index)
  {
    REQUIRE(bus.read16(configurationAddresses[index]).value == configurationValues[index]);
  }
  for (std::size_t index = 0; index < masterVolumeAddresses.size(); ++index)
  {
    REQUIRE(bus.read16(masterVolumeAddresses[index]).value == masterVolumeValues[index]);
  }
  for (std::size_t index = 0; index < voiceMixAddresses.size(); ++index)
  {
    REQUIRE(bus.read16(voiceMixAddresses[index]).value == voiceMixValues[index]);
  }
  for (std::size_t core = 0; core < mainMixAddresses.size(); ++core)
  {
    REQUIRE(bus.read16(mainMixAddresses[core]).value == mainMixValues[core]);
    REQUIRE(bus.read16(coreAttributeAddresses[core]).value == 0xc001);
  }
  REQUIRE(system.iopInterruptController().status() == (1u << 9));
  REQUIRE(system.iopDMAController().auxiliaryControl() == 0x12345678);
  system.spu2Controller().reset();
  for (const auto address : effectInputVolumeAddresses)
  {
    REQUIRE(bus.read16(address).value == 0);
  }
  programEffectInputVolumes(system);
  system.reset();
  for (const auto address : effectInputVolumeAddresses)
  {
    REQUIRE(bus.read16(address).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(address).value == 0);
  }
}

TEST_CASE("SPU2 main mix retains independent raw halfwords without active effects",
  "[iop][spu2][spu2-mmix]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  for (const auto address : mainMixAddresses)
  {
    REQUIRE(bus.read16(address).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(address).value == 0);
  }
  programConfiguration(system);
  programMasterVolumes(system);
  programVoiceMix(system);
  programMainMix(system);
  for (const auto address : coreAttributeAddresses)
  {
    REQUIRE(bus.write16(address, 0xc001).status == IOPBusStatus::Completed);
  }
  system.iopInterruptController().assertSource(IOPInterruptSource::SPU);
  system.iopDMAController().writeAuxiliaryControl(0x12345678);
  const auto programmed = system.iopStateHash();
  for (std::size_t selected = 0; selected < mainMixAddresses.size(); ++selected)
  {
    const auto address = mainMixAddresses[selected];
    CAPTURE(address);
    for (std::uint32_t value = 0; value <= 0xffff; ++value)
    {
      const auto write = bus.write16(address, static_cast<std::uint16_t>(value));
      const auto read = bus.read16(address);
      if (write.status != IOPBusStatus::Completed ||
          read.status != IOPBusStatus::Completed || read.value != value)
      {
        FAIL("Main-mix image did not round-trip: " << value);
      }
    }
    for (std::size_t core = 0; core < mainMixAddresses.size(); ++core)
    {
      REQUIRE(bus.read16(mainMixAddresses[core]).value ==
        (core == selected ? 0xffff : mainMixValues[core]));
    }
    REQUIRE(bus.write16(address, mainMixValues[selected]).status ==
      IOPBusStatus::Completed);
  }
  REQUIRE(system.iopStateHash() == programmed);
  for (const auto address : coreStatusAddresses)
  {
    REQUIRE(bus.read16(address).value == 0);
  }
  for (const auto &addresses : {keyOffLowAddresses, keyOffHighAddresses})
  for (const auto address : addresses)
  {
    REQUIRE(bus.write16(address, 0xffff).status == IOPBusStatus::Completed);
  }
  for (const auto address : autoDMAAddresses)
  {
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Completed);
  }
  REQUIRE(system.iopStateHash() == programmed);
  system.runMasterCycles(80);
  bus.reset();
  system.iopCore().reset();
  for (std::size_t core = 0; core < mainMixAddresses.size(); ++core)
  {
    REQUIRE(bus.read16(mainMixAddresses[core]).value == mainMixValues[core]);
  }
  for (std::size_t index = 0; index < voiceMixAddresses.size(); ++index)
  {
    REQUIRE(bus.read16(voiceMixAddresses[index]).value == voiceMixValues[index]);
  }
  for (std::size_t index = 0; index < configurationAddresses.size(); ++index)
  {
    REQUIRE(bus.read16(configurationAddresses[index]).value ==
      configurationValues[index]);
  }
  for (std::size_t index = 0; index < masterVolumeAddresses.size(); ++index)
  {
    REQUIRE(bus.read16(masterVolumeAddresses[index]).value ==
      masterVolumeValues[index]);
  }
  for (const auto address : coreAttributeAddresses)
  {
    REQUIRE(bus.read16(address).value == 0xc001);
  }
  REQUIRE(system.iopInterruptController().status() == (1u << 9));
  REQUIRE(system.iopDMAController().auxiliaryControl() == 0x12345678);
  system.spu2Controller().reset();
  for (const auto address : mainMixAddresses)
  {
    REQUIRE(bus.read16(address).value == 0);
  }
  programMainMix(system);
  system.reset();
  for (const auto address : mainMixAddresses)
  {
    REQUIRE(bus.read16(address).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(address).value == 0);
  }
}

TEST_CASE("SPU2 voice mix retains independent raw halfwords without active effects",
  "[iop][spu2][spu2-vmix]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  REQUIRE(bus.write16(0x1f900188, 0xffff).status == IOPBusStatus::Completed);
  REQUIRE(bus.read16(0x1f900188).value == 0xffff);
  system.spu2Controller().reset();
  for (const auto address : voiceMixAddresses)
  {
    REQUIRE(bus.read16(address).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(address).value == 0);
  }
  programConfiguration(system);
  programMasterVolumes(system);
  for (const auto address : coreAttributeAddresses)
  {
    REQUIRE(bus.write16(address, 0xc001).status == IOPBusStatus::Completed);
  }
  programVoiceMix(system);
  system.iopInterruptController().assertSource(IOPInterruptSource::SPU);
  system.iopDMAController().writeAuxiliaryControl(0x12345678);
  const auto programmed = system.iopStateHash();
  for (std::size_t selected = 0; selected < voiceMixAddresses.size(); ++selected)
  {
    const auto address = voiceMixAddresses[selected];
    CAPTURE(address);
    for (std::uint32_t value = 0; value <= 0xffff; ++value)
    {
      const auto write = bus.write16(address, static_cast<std::uint16_t>(value));
      const auto read = bus.read16(address);
      if (write.status != IOPBusStatus::Completed ||
          read.status != IOPBusStatus::Completed || read.value != value)
      {
        FAIL("Voice-mix image did not round-trip: " << value);
      }
    }
    for (std::size_t index = 0; index < voiceMixAddresses.size(); ++index)
    {
      REQUIRE(bus.read16(voiceMixAddresses[index]).value ==
        (index == selected ? 0xffff : voiceMixValues[index]));
    }
    REQUIRE(bus.write16(address, voiceMixValues[selected]).status ==
      IOPBusStatus::Completed);
  }
  REQUIRE(system.iopStateHash() == programmed);
  for (const auto address : coreStatusAddresses)
  {
    REQUIRE(bus.read16(address).value == 0);
  }
  for (const auto &addresses : {keyOffLowAddresses, keyOffHighAddresses})
  for (const auto address : addresses)
  {
    REQUIRE(bus.write16(address, 0xffff).status == IOPBusStatus::Completed);
  }
  for (const auto address : autoDMAAddresses)
  {
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Completed);
  }
  REQUIRE(system.iopStateHash() == programmed);
  system.runMasterCycles(80);
  bus.reset();
  system.iopCore().reset();
  for (std::size_t index = 0; index < voiceMixAddresses.size(); ++index)
  {
    REQUIRE(bus.read16(voiceMixAddresses[index]).value == voiceMixValues[index]);
  }
  for (std::size_t index = 0; index < configurationAddresses.size(); ++index)
  {
    REQUIRE(bus.read16(configurationAddresses[index]).value ==
      configurationValues[index]);
  }
  for (std::size_t index = 0; index < masterVolumeAddresses.size(); ++index)
  {
    REQUIRE(bus.read16(masterVolumeAddresses[index]).value ==
      masterVolumeValues[index]);
  }
  for (const auto address : coreAttributeAddresses)
  {
    REQUIRE(bus.read16(address).value == 0xc001);
  }
  REQUIRE(system.iopInterruptController().status() == (1u << 9));
  REQUIRE(system.iopDMAController().auxiliaryControl() == 0x12345678);
  system.spu2Controller().reset();
  for (const auto address : voiceMixAddresses)
  {
    REQUIRE(bus.read16(address).value == 0);
  }
  programVoiceMix(system);
  system.reset();
  for (const auto address : voiceMixAddresses)
  {
    REQUIRE(bus.read16(address).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(address).value == 0);
  }
}

TEST_CASE("SPU2 voice modes retain raw masks without input or voice effects",
  "[iop][spu2][spu2-mode]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  for (const auto address : voiceModeAddresses)
  {
    REQUIRE(bus.read16(address).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(address).value == 0);
  }
  SPU2InputTest::programProfile(system.spu2Controller());
  for (std::size_t core = 0; core < autoDMAAddresses.size(); ++core)
  {
    REQUIRE(bus.write16(autoDMAAddresses[core],
      autoDMAWriteSelections[core]).status == IOPBusStatus::Completed);
    REQUIRE(system.spu2Controller().writeInputWord(core, 0x12345678));
  }
  system.iopInterruptController().assertSource(IOPInterruptSource::SPU);
  system.iopDMAController().writeAuxiliaryControl(0x12345678);
  const auto before = system.iopStateHash();
  programVoiceModes(system);
  for (std::size_t selected = 0; selected < voiceModeAddresses.size(); ++selected)
  {
    CAPTURE(selected);
    for (std::uint16_t value : {0, 1, 0x80, 0x100, 0x8000, 0xffff})
    {
      REQUIRE(bus.write16(voiceModeAddresses[selected], value).status ==
        IOPBusStatus::Completed);
      for (std::size_t index = 0; index < voiceModeAddresses.size(); ++index)
      {
        REQUIRE(bus.read16(voiceModeAddresses[index]).status == IOPBusStatus::Completed);
        REQUIRE(bus.read16(voiceModeAddresses[index]).value ==
          (index == selected ? value : voiceModeValues[index]));
      }
    }
    REQUIRE(bus.write16(voiceModeAddresses[selected], voiceModeValues[selected]).status ==
      IOPBusStatus::Completed);
  }
  for (const auto address : voiceModeAddresses)
  {
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Completed);
  }
  REQUIRE(system.iopStateHash() == before);
  programVoiceModes(system);
  for (const auto address : autoDMAAddresses)
  {
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Completed);
  }
  const auto inactive = system.iopStateHash();
  for (const auto address : coreStatusAddresses)
  {
    REQUIRE(bus.read16(address).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(address).value == 0);
  }
  for (const auto &addresses : {keyOffLowAddresses, keyOffHighAddresses})
  for (const auto address : addresses)
  {
    REQUIRE(bus.write16(address, 0xffff).status == IOPBusStatus::Completed);
  }
  REQUIRE(system.iopStateHash() == inactive);
  system.runMasterCycles(80);
  bus.reset();
  system.iopCore().reset();
  for (std::size_t index = 0; index < voiceModeAddresses.size(); ++index)
  {
    REQUIRE(bus.read16(voiceModeAddresses[index]).value == voiceModeValues[index]);
  }
  system.spu2Controller().reset();
  for (const auto address : voiceModeAddresses)
  {
    REQUIRE(bus.read16(address).value == 0);
  }
  programVoiceModes(system);
  system.reset();
  for (const auto address : voiceModeAddresses)
  {
    REQUIRE(bus.read16(address).value == 0);
  }
}

TEST_CASE("SPU2 inactive key-off accepts all masks without programming or signals",
  "[iop][spu2][spu2-koff]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  programConfiguration(system);
  programMasterVolumes(system);
  system.iopInterruptController().assertSource(IOPInterruptSource::SPU);
  system.iopDMAController().writeAuxiliaryControl(0x12345678);
  for (const auto first : coreAttributeValues)
  for (const auto second : coreAttributeValues)
  {
    REQUIRE(bus.write16(coreAttributeAddresses[0], first).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.write16(coreAttributeAddresses[1], second).status ==
      IOPBusStatus::Completed);
    const auto before = system.iopStateHash();
    for (const auto &addresses : {keyOffLowAddresses, keyOffHighAddresses})
    for (const auto address : addresses)
    {
      CAPTURE(address);
      REQUIRE(bus.write16(address, 0xffff).status == IOPBusStatus::Completed);
      for (std::uint32_t value = 0; value <= 0xffff; ++value)
      {
        if (bus.write16(address, static_cast<std::uint16_t>(value)).status !=
            IOPBusStatus::Completed)
        {
          FAIL("Inactive key-off mask rejected: " << value);
        }
      }
      REQUIRE(bus.write16(address, 0xffff).status == IOPBusStatus::Completed);
      REQUIRE(bus.read16(address).status == IOPBusStatus::Unmapped);
      REQUIRE(system.iopStateHash() == before);
    }
    for (const auto address : coreStatusAddresses)
    {
      REQUIRE(bus.read16(address).status == IOPBusStatus::Completed);
      REQUIRE(bus.read16(address).value == 0);
    }
  }
  system.runMasterCycles(80);
  bus.reset();
  system.iopCore().reset();
  for (const auto &addresses : {keyOffLowAddresses, keyOffHighAddresses})
  for (const auto address : addresses)
  {
    REQUIRE(bus.write16(address, 0xffff).status == IOPBusStatus::Completed);
  }
  REQUIRE(system.iopInterruptController().status() == (1u << 9));
  REQUIRE(system.iopDMAController().auxiliaryControl() == 0x12345678);
  system.spu2Controller().reset();
  system.reset();
  for (const auto &addresses : {keyOffLowAddresses, keyOffHighAddresses})
  for (const auto address : addresses)
  {
    REQUIRE(bus.write16(address, 0xffff).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(address).status == IOPBusStatus::Unmapped);
  }
}

TEST_CASE("SPU2 inactive key-off rejects readback neighbors and other transports",
  "[iop][spu2][spu2-koff]")
{
  for (const auto &addresses : {keyOffLowAddresses, keyOffHighAddresses})
  for (const auto address : addresses)
  {
    CAPTURE(address);
    NekoSystem system;
    auto &bus = system.iopBus();
    programConfiguration(system);
    programMasterVolumes(system);
    programTransferStartAddresses(system);
    const auto before = system.iopStateHash();
    REQUIRE(bus.write16(address, 0xffff).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(address).status == IOPBusStatus::Unmapped);
    for (const auto byteAddress : {address, address + 1})
    {
      REQUIRE(bus.read8(byteAddress).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write8(byteAddress, 0xff).status == IOPBusStatus::Unmapped);
    }
    REQUIRE(bus.read16(address + 1).status == IOPBusStatus::Misaligned);
    REQUIRE(bus.write16(address + 1, 0).status == IOPBusStatus::Misaligned);
    const auto wordAddress = address & ~IOPAddress(3);
    REQUIRE(bus.read32(wordAddress).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(wordAddress, 0xffffffff).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.readInstruction32(wordAddress).status == IOPBusStatus::Unmapped);
    for (std::uint8_t mask = 1; mask <= 0xf; ++mask)
    {
      REQUIRE(bus.writeMasked32(wordAddress, 0xffffffff, mask).status ==
        IOPBusStatus::Unmapped);
    }
    REQUIRE(bus.readDMAC32(wordAddress).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.writeDMAC32(wordAddress, 0xffffffff).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.dmaWriteStatus(wordAddress) == IOPBusStatus::Unmapped);
    std::array<std::uint8_t, 2> bytes = {{0x12, 0x34}};
    REQUIRE(bus.loadPhysical(address, bytes.data(), bytes.size()) ==
      IOPBusStatus::Unmapped);
    REQUIRE(bus.inspectPhysical(address, bytes.data(), bytes.size()) ==
      IOPBusStatus::Unmapped);
    REQUIRE(bytes == (std::array<std::uint8_t, 2>{{0x12, 0x34}}));
    const auto coreBase = address & ~IOPAddress(0x3ff);
    for (IOPAddress offset : {0xau, 0xcu, 0x19cu, 0x19eu,
        0x1a0u, 0x1a2u, 0x1aeu, 0x340u, 0x342u})
    {
      REQUIRE(bus.read16(coreBase + offset).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write16(coreBase + offset, 0xffff).status == IOPBusStatus::Unmapped);
    }
    for (IOPAddress other : {address + 0x800,
        address | 0x80000000u, address | 0xa0000000u})
    {
      REQUIRE(bus.read16(other).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write16(other, 0xffff).status == IOPBusStatus::Unmapped);
    }
    std::uint16_t eeValue = 0x4321;
    REQUIRE_FALSE(system.eeBus().readData16(address, &eeValue));
    REQUIRE(eeValue == 0x4321);
    REQUIRE_FALSE(system.eeBus().writeData16(address, 0xffff));
    IOPBus unattached;
    REQUIRE(unattached.write16(address, 0xffff).status == IOPBusStatus::Unmapped);
    REQUIRE(system.iopStateHash() == before);
  }
}

TEST_CASE("SPU2 inactive status reads preserve programming and pending signals",
  "[iop][spu2][spu2-statx]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  for (const auto address : coreStatusAddresses)
  {
    REQUIRE(bus.read16(address).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(address).value == 0);
  }
  prepareInactiveInputs(system);
  programConfiguration(system);
  programMasterVolumes(system);
  system.iopInterruptController().assertSource(IOPInterruptSource::SPU);
  system.iopDMAController().writeAuxiliaryControl(0x12345678);
  for (const auto first : coreAttributeValues)
  for (const auto second : coreAttributeValues)
  {
    REQUIRE(bus.write16(coreAttributeAddresses[0], first).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.write16(coreAttributeAddresses[1], second).status ==
      IOPBusStatus::Completed);
    const auto before = system.iopStateHash();
    for (unsigned repeat = 0; repeat < 3; ++repeat)
    for (const auto address : coreStatusAddresses)
    {
      const auto read = bus.read16(address);
      REQUIRE(read.status == IOPBusStatus::Completed);
      REQUIRE(read.value == 0);
    }
    REQUIRE(system.iopStateHash() == before);
  }
  system.runMasterCycles(80);
  bus.reset();
  system.iopCore().reset();
  for (const auto address : coreStatusAddresses)
  {
    REQUIRE(bus.read16(address).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(address).value == 0);
  }
  REQUIRE(system.iopInterruptController().status() == (1u << 9));
  REQUIRE(system.iopDMAController().auxiliaryControl() == 0x12345678);
  system.spu2Controller().reset();
  system.reset();
  for (const auto address : coreStatusAddresses)
  {
    REQUIRE(bus.read16(address).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(address).value == 0);
  }
}

TEST_CASE("SPU2 inactive status excludes writes neighbors and non-CPU transports",
  "[iop][spu2][spu2-statx]")
{
  for (const auto address : coreStatusAddresses)
  for (const auto attribute : {0x8000, 0x8010})
  {
    CAPTURE(address);
    CAPTURE(attribute);
    NekoSystem system;
    auto &bus = system.iopBus();
    prepareInactiveInputs(system);
    programConfiguration(system);
    programMasterVolumes(system);
    for (const auto target : coreAttributeAddresses)
    {
      REQUIRE(bus.write16(target, attribute).status == IOPBusStatus::Completed);
    }
    const auto before = system.iopStateHash();
    REQUIRE(bus.read8(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read8(address + 1).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(address + 1, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read16(address + 1).status == IOPBusStatus::Misaligned);
    REQUIRE(bus.write16(address + 1, 0).status == IOPBusStatus::Misaligned);
    for (std::uint16_t value : {0, 0x80, 0x400, 0xffff})
    {
      REQUIRE(bus.write16(address, value).status == IOPBusStatus::Unmapped);
    }
    REQUIRE(bus.read32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.readInstruction32(address).status == IOPBusStatus::Unmapped);
    for (std::uint8_t mask = 1; mask <= 0xf; ++mask)
    {
      REQUIRE(bus.writeMasked32(address, 0, mask).status == IOPBusStatus::Unmapped);
    }
    REQUIRE(bus.readDMAC32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.writeDMAC32(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.dmaWriteStatus(address) == IOPBusStatus::Unmapped);
    std::array<std::uint8_t, 2> bytes = {{0x12, 0x34}};
    REQUIRE(bus.loadPhysical(address, bytes.data(), bytes.size()) ==
      IOPBusStatus::Unmapped);
    REQUIRE(bus.inspectPhysical(address, bytes.data(), bytes.size()) ==
      IOPBusStatus::Unmapped);
    REQUIRE(bytes == (std::array<std::uint8_t, 2>{{0x12, 0x34}}));
    for (IOPAddress other : {address - 0x10, address - 4, address - 2,
        address + 2, address + 0x800,
        address | 0x80000000u, address | 0xa0000000u})
    {
      REQUIRE(bus.read16(other).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write16(other, 0).status == IOPBusStatus::Unmapped);
    }
    std::uint16_t eeValue = 0x4321;
    REQUIRE_FALSE(system.eeBus().readData16(address, &eeValue));
    REQUIRE(eeValue == 0x4321);
    REQUIRE_FALSE(system.eeBus().writeData16(address, 0));
    bus.attachSPU2(nullptr);
    REQUIRE(bus.read16(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Unmapped);
    bus.attachSPU2(&system.spu2Controller());
    REQUIRE(system.iopStateHash() == before);
    REQUIRE(bus.read16(address).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(address).value == (attribute == 0x8010 ? 0x80 : 0));
  }
}

TEST_CASE("SPU2 inactive status guests preserve aliases and branch load delays",
  "[iop][spu2][spu2-statx][trace]")
{
  for (const auto physical : coreStatusAddresses)
  for (auto segment : {0u, 0x80000000u, 0xa0000000u})
  for (IOPWord load : {0x84220000u, 0x94220000u})
  for (const auto attribute : {0x8000, 0x8010})
  {
    const auto address = physical | segment;
    CAPTURE(address);
    CAPTURE(load);
    CAPTURE(attribute);
    const IOPWord expected = attribute == 0x8010 ? 0x80 : 0;
    NekoSystem system;
    prepareInactiveInputs(system);
    REQUIRE(system.iopBus().write16(coreAttributeAddresses[0], attribute).status ==
      IOPBusStatus::Completed);
    REQUIRE(system.iopBus().write16(coreAttributeAddresses[1], attribute).status ==
      IOPBusStatus::Completed);
    installHalfwordReadProgram(system, address, load);
    system.startTrace();
    system.runMasterCycles(64);
    REQUIRE(system.iopCore().executionState() == IOPExecutionState::Running);
    REQUIRE(system.iopCore().exception().kind == IOPException::None);
    REQUIRE(system.iopCore().retiredInstructions() == 8);
    REQUIRE(system.iopCore().programCounter() == 0xa0000024);
    REQUIRE(system.iopCore().generalRegister(2) == expected);
    REQUIRE(system.iopCore().generalRegister(3) == 0xffffffff);
    REQUIRE(system.iopCore().generalRegister(4) == expected);
    REQUIRE(system.iopCore().generalRegister(5) == 0x1234);
    REQUIRE_FALSE(system.iopCore().branchContinuation().active);
    REQUIRE(system.iopCore().delayedResultContinuation().source ==
      IOPDelayedResultSource::None);
    IOPTraceTest::Events actual;
    for (const auto &event : system.trace())
    {
      if (event.subsystem == NekoTraceSubsystem::IOP &&
          event.type == NekoTraceEventType::MemoryAccess &&
          event.value0 == address)
      {
        actual.push_back(event);
      }
    }
    namespace Memory = NekoIOPTraceMemory;
    const auto route = segment == 0xa0000000
      ? Memory::CacheRoute::Uncached : Memory::CacheRoute::Cached;
    const auto metadata = nekoPackIOPMemoryTraceMetadata({
      physical, 2, Memory::AccessKind::DataLoad,
      Memory::Outcome::Completed, route, true, 0});
    IOPTraceTest::requireEvents(actual, {
      {33, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, expected, 0, metadata}
    });
  }
}

TEST_CASE("SPU2 inactive status loads restore across branch and load continuations",
  "[iop][spu2][spu2-statx][save-state][trace]")
{
  for (const auto address : coreStatusAddresses)
  for (unsigned cut : {32u, 40u, 48u})
  {
    CAPTURE(address);
    CAPTURE(cut);
    NekoSystem original;
    prepareInactiveInputs(original);
    programConfiguration(original);
    programMasterVolumes(original);
    REQUIRE(original.iopBus().write16(coreAttributeAddresses[0], 0x8000).status ==
      IOPBusStatus::Completed);
    original.iopInterruptController().assertSource(IOPInterruptSource::SPU);
    original.iopDMAController().writeAuxiliaryControl(0x12345678);
    installHalfwordReadProgram(original, address | 0xa0000000u, 0x94220000);
    original.runMasterCycles(cut);
    REQUIRE(original.iopCore().branchContinuation().active == (cut == 32));
    REQUIRE((original.iopCore().delayedResultContinuation().source ==
      IOPDelayedResultSource::Load) == (cut == 40));
    const auto checkpoint = original.saveState();
    NekoSystem restored;
    NekoSystem plain;
    restored.loadState(checkpoint);
    plain.loadState(checkpoint);
    REQUIRE(plain.iopBus().read16(address).status == IOPBusStatus::Completed);
    REQUIRE(plain.iopBus().read16(address).value == 0);
    REQUIRE(plain.saveState() == checkpoint);
    original.startTrace();
    restored.startTrace();
    original.runMasterCycles(64 - cut);
    restored.runMasterCycles(64 - cut);
    plain.runMasterCycles(64 - cut);
    REQUIRE(original.iopCore().exception().kind == IOPException::None);
    REQUIRE(original.iopCore().retiredInstructions() == 8);
    REQUIRE(original.iopCore().generalRegister(3) == 0xffffffff);
    REQUIRE(original.iopCore().generalRegister(4) == 0);
    REQUIRE(original.iopCore().generalRegister(5) == 0x1234);
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireMachine(plain, original);
  }
}

TEST_CASE("SPU2 I/O status derives idle readiness without transfer side effects",
  "[iop][spu2][spu2-statx][spu2-io-status]")
{
  for (std::size_t core = 0; core < 2; ++core)
  {
    CAPTURE(core);
    NekoSystem system;
    auto &bus = system.iopBus();
    auto &spu = system.spu2Controller();
    system.iopInterruptController().assertSource(IOPInterruptSource::SPU);
    system.iopDMAController().writeAuxiliaryControl(0x12345678);
    REQUIRE(bus.write16(transferStartAddressAddresses[core * 2 + 1], 0xffff).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.write16(transferDataAddresses[core], 0x0707).status ==
      IOPBusStatus::Completed);
    std::uint32_t cursor = 0x10000;
    for (const auto base : coreAttributeValues)
    {
      REQUIRE(bus.write16(coreAttributeAddresses[core], base | 0x10).status ==
        IOPBusStatus::Completed);
      const auto before = system.iopStateHash();
      for (unsigned repeat = 0; repeat < 3; ++repeat)
      {
        const auto status = bus.read16(coreStatusAddresses[core]);
        REQUIRE(status.status == IOPBusStatus::Completed);
        REQUIRE(status.value == 0x80);
        REQUIRE(bus.read16(coreStatusAddresses[1 - core]).value == 0);
      }
      REQUIRE(system.iopStateHash() == before);
      REQUIRE(bus.write16(transferDataAddresses[core], base).status ==
        IOPBusStatus::Completed);
      REQUIRE(spu.readSoundRAMHalfword(cursor++) == base);
      REQUIRE(bus.read16(coreStatusAddresses[core]).value == 0x80);
      REQUIRE(bus.write16(coreAttributeAddresses[core], base).status ==
        IOPBusStatus::Completed);
      REQUIRE(bus.read16(coreStatusAddresses[core]).value == 0);
    }
    REQUIRE(bus.write16(coreAttributeAddresses[0], 0x8010).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.write16(coreAttributeAddresses[1], 0x10).status ==
      IOPBusStatus::Completed);
    system.runMasterCycles(80);
    bus.reset();
    system.iopCore().reset();
    for (const auto address : coreStatusAddresses)
    {
      REQUIRE(bus.read16(address).status == IOPBusStatus::Completed);
      REQUIRE(bus.read16(address).value == 0x80);
    }
    REQUIRE(bus.write16(transferDataAddresses[core], 0xabcd).status ==
      IOPBusStatus::Completed);
    REQUIRE(spu.readSoundRAMHalfword(cursor) == 0xabcd);
    REQUIRE(spu.readSoundRAMHalfword(0xffff) == 0x0707);
    REQUIRE(bus.read16(transferStartAddressAddresses[core * 2 + 1]).value == 0xffff);
    REQUIRE(system.iopInterruptController().status() == (1u << 9));
    REQUIRE(system.iopDMAController().auxiliaryControl() == 0x12345678);
    spu.reset();
    REQUIRE(bus.read16(coreStatusAddresses[core]).value == 0);
    system.reset();
    REQUIRE(bus.read16(coreStatusAddresses[core]).value == 0);
  }
}

TEST_CASE("SPU2 I/O status excludes peer PCM activity without changing mode00",
  "[iop][spu2][spu2-statx][spu2-io-status][spu2-input]")
{
  for (std::size_t core = 0; core < 2; ++core)
  {
    CAPTURE(core);
    NekoSystem system;
    auto &bus = system.iopBus();
    auto &spu = system.spu2Controller();
    const auto peer = 1 - core;
    SPU2InputTest::programProfile(spu);
    REQUIRE(bus.write16(coreAttributeAddresses[core], 0xc011).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.read16(coreStatusAddresses[core]).status == IOPBusStatus::Completed);
    REQUIRE(bus.write16(autoDMAAddresses[peer], autoDMAWriteSelections[peer]).status ==
      IOPBusStatus::Completed);
    REQUIRE(spu.writeInputWord(peer, 0x12345678));
    const auto before = system.iopStateHash();
    REQUIRE(bus.read16(coreStatusAddresses[core]).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read16(coreStatusAddresses[peer]).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write16(transferDataAddresses[core], 0x0707).status ==
      IOPBusStatus::Unmapped);
    REQUIRE(system.iopStateHash() == before);
    spu.cancelInputWrite(peer);
    REQUIRE(bus.read16(coreStatusAddresses[core]).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write16(coreAttributeAddresses[core], 0xc001).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.read16(coreStatusAddresses[core]).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(coreStatusAddresses[core]).value == 0);
    REQUIRE(bus.write16(coreAttributeAddresses[core], 0xc011).status ==
      IOPBusStatus::Completed);
    bus.reset();
    system.iopCore().reset();
    system.runMasterCycles(80);
    REQUIRE(bus.read16(coreStatusAddresses[core]).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write16(autoDMAAddresses[peer], 0).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(coreStatusAddresses[core]).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(coreStatusAddresses[core]).value == 0x80);
  }
}

TEST_CASE("SPU2 I/O status loads restore across branch and load continuations",
  "[iop][spu2][spu2-statx][spu2-io-status][save-state][trace]")
{
  for (std::size_t core = 0; core < 2; ++core)
  for (unsigned cut : {32u, 40u, 48u})
  {
    CAPTURE(core);
    CAPTURE(cut);
    NekoSystem original;
    REQUIRE(original.iopBus().write16(coreAttributeAddresses[core], 0x8010).status ==
      IOPBusStatus::Completed);
    REQUIRE(original.iopBus().write16(coreAttributeAddresses[1 - core], 0x10).status ==
      IOPBusStatus::Completed);
    original.iopInterruptController().assertSource(IOPInterruptSource::SPU);
    original.iopDMAController().writeAuxiliaryControl(0x12345678);
    installHalfwordReadProgram(
      original, coreStatusAddresses[core] | 0xa0000000u, 0x94220000);
    original.runMasterCycles(cut);
    const auto checkpoint = original.saveState();
    NekoSystem restored;
    NekoSystem plain;
    restored.loadState(checkpoint);
    plain.loadState(checkpoint);
    const auto status = plain.iopBus().read16(coreStatusAddresses[core]);
    REQUIRE(status.status == IOPBusStatus::Completed);
    REQUIRE(status.value == 0x80);
    REQUIRE(plain.saveState() == checkpoint);
    original.startTrace();
    restored.startTrace();
    original.runMasterCycles(64 - cut);
    restored.runMasterCycles(64 - cut);
    plain.runMasterCycles(64 - cut);
    REQUIRE(original.iopCore().exception().kind == IOPException::None);
    REQUIRE(original.iopCore().retiredInstructions() == 8);
    REQUIRE(original.iopCore().generalRegister(3) == 0xffffffff);
    REQUIRE(original.iopCore().generalRegister(4) == 0x80);
    REQUIRE(original.iopCore().generalRegister(5) == 0x1234);
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireMachine(plain, original);
    const auto before = restored.saveState();
    SaveStateMutation oldVersion(before);
    oldVersion.writeScalar("container.version", 110);
    oldVersion.refreshChecksum();
    REQUIRE_THROWS_AS(restored.loadState(oldVersion), std::invalid_argument);
    REQUIRE(restored.saveState() == before);
  }
}

TEST_CASE("SPU2 core attributes retain independent bit0 and bits14-15 images",
  "[iop][spu2][spu2-attr]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  for (const auto address : coreAttributeAddresses)
  {
    CAPTURE(address);
    const auto cold = bus.read16(address);
    REQUIRE(cold.status == IOPBusStatus::Completed);
    REQUIRE(cold.value == 0);
  }
  programConfiguration(system);
  programMasterVolumes(system);
  system.iopInterruptController().assertSource(IOPInterruptSource::SPU);
  system.iopDMAController().writeAuxiliaryControl(0x12345678);
  const auto before = system.iopStateHash();
  for (std::size_t selected = 0; selected < coreAttributeAddresses.size(); ++selected)
  {
    const auto address = coreAttributeAddresses[selected];
    CAPTURE(address);
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Completed);
    REQUIRE(system.iopStateHash() == before);
    const auto peerAddress = coreAttributeAddresses[1 - selected];
    for (const auto peer : coreAttributeValues)
    {
      REQUIRE(bus.write16(peerAddress, peer).status == IOPBusStatus::Completed);
      for (const auto from : coreAttributeValues)
      for (const auto to : coreAttributeValues)
      {
        CAPTURE(peer);
        CAPTURE(from);
        CAPTURE(to);
        REQUIRE(bus.write16(address, from).status == IOPBusStatus::Completed);
        REQUIRE(bus.write16(address, to).status == IOPBusStatus::Completed);
        REQUIRE(bus.read16(address).status == IOPBusStatus::Completed);
        REQUIRE(bus.read16(address).value == to);
        REQUIRE(bus.read16(peerAddress).value == peer);
      }
    }
    REQUIRE(bus.write16(peerAddress, 0x4001).status == IOPBusStatus::Completed);
    REQUIRE(bus.write16(address, 0xc001).status == IOPBusStatus::Completed);
    const auto programmed = system.iopStateHash();
    REQUIRE(programmed != before);
    REQUIRE(bus.write16(address, 0xc001).status == IOPBusStatus::Completed);
    REQUIRE(system.iopStateHash() == programmed);
    for (std::uint32_t value = 1; value <= 0xffff; ++value)
    {
      if ((value & 0x3fee) != 0 &&
          (bus.write16(address, static_cast<std::uint16_t>(value)).status !=
            IOPBusStatus::Unmapped ||
           system.spu2Controller().readCoreAttribute(selected) != 0xc001))
      {
        FAIL("Unsupported core ATTR write admitted: " << value);
      }
    }
    REQUIRE(system.iopStateHash() == programmed);
    REQUIRE(bus.read16(address).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(address).value == 0xc001);
    REQUIRE(bus.read16(peerAddress).value == 0x4001);
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Completed);
    REQUIRE(bus.write16(peerAddress, 0).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(address).value == 0);
    REQUIRE(bus.read16(coreAttributeAddresses[1 - selected]).value == 0);
    REQUIRE(system.iopStateHash() == before);
  }
  REQUIRE(system.iopStateHash() == before);
  REQUIRE(system.iopDMAController().auxiliaryControl() == 0x12345678);
  for (std::size_t index = 0; index < configurationAddresses.size(); ++index)
  {
    REQUIRE(bus.read16(configurationAddresses[index]).value ==
      configurationValues[index]);
  }
  for (std::size_t index = 0; index < masterVolumeAddresses.size(); ++index)
  {
    REQUIRE(bus.read16(masterVolumeAddresses[index]).value ==
      masterVolumeValues[index]);
  }
  for (const auto value : coreAttributeValues)
  {
    for (const auto address : coreAttributeAddresses)
    {
      REQUIRE(bus.write16(address, value).status == IOPBusStatus::Completed);
    }
    system.runMasterCycles(80);
    bus.reset();
    system.iopCore().reset();
    for (const auto address : coreAttributeAddresses)
    {
      REQUIRE(bus.read16(address).value == value);
    }
    REQUIRE(system.iopInterruptController().status() == (1u << 9));
    REQUIRE(system.iopDMAController().auxiliaryControl() == 0x12345678);
    system.spu2Controller().reset();
    for (const auto address : coreAttributeAddresses)
    {
      REQUIRE(bus.read16(address).status == IOPBusStatus::Completed);
      REQUIRE(bus.read16(address).value == 0);
    }
  }
  for (const auto address : coreAttributeAddresses)
  {
    REQUIRE(bus.write16(address, 0xc001).status == IOPBusStatus::Completed);
  }
  system.reset();
  for (const auto address : coreAttributeAddresses)
  {
    REQUIRE(bus.read16(address).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(address).value == 0);
  }
}

TEST_CASE("SPU2 I/O mode retains attributes without rewinding or signaling",
  "[iop][spu2][spu2-io-mode]")
{
  for (std::size_t core = 0; core < 2; ++core)
  {
    CAPTURE(core);
    NekoSystem system;
    auto &bus = system.iopBus();
    auto &spu = system.spu2Controller();
    system.iopInterruptController().assertSource(IOPInterruptSource::SPU);
    system.iopDMAController().writeAuxiliaryControl(0x12345678);
    REQUIRE(bus.write16(transferStartAddressAddresses[core * 2 + 1], 0xffff).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.write16(transferDataAddresses[core], 0x0707).status ==
      IOPBusStatus::Completed);
    std::uint32_t cursor = 0x10000;
    for (const auto base : coreAttributeValues)
    {
      const auto mode = static_cast<std::uint16_t>(base | 0x10);
      REQUIRE(bus.write16(coreAttributeAddresses[core], mode).status ==
        IOPBusStatus::Completed);
      const auto before = system.iopStateHash();
      REQUIRE(bus.write16(coreAttributeAddresses[core], mode).status ==
        IOPBusStatus::Completed);
      REQUIRE(bus.read16(coreAttributeAddresses[core]).value == mode);
      REQUIRE(bus.read16(coreAttributeAddresses[1 - core]).value == 0);
      REQUIRE(bus.read16(coreStatusAddresses[core]).status == IOPBusStatus::Completed);
      REQUIRE(bus.read16(coreStatusAddresses[core]).value == 0x80);
      REQUIRE(bus.read16(coreStatusAddresses[1 - core]).status ==
        IOPBusStatus::Completed);
      REQUIRE(system.iopStateHash() == before);
      REQUIRE(bus.write16(transferDataAddresses[core], mode).status ==
        IOPBusStatus::Completed);
      REQUIRE(spu.readSoundRAMHalfword(cursor++) == mode);
      REQUIRE(bus.write16(coreAttributeAddresses[core], base).status ==
        IOPBusStatus::Completed);
      REQUIRE(bus.read16(coreStatusAddresses[core]).status == IOPBusStatus::Completed);
      REQUIRE(bus.read16(coreStatusAddresses[core]).value == 0);
    }
    REQUIRE(spu.readSoundRAMHalfword(0xffff) == 0x0707);
    REQUIRE(bus.read16(transferStartAddressAddresses[core * 2 + 1]).value == 0xffff);
    REQUIRE(bus.write16(coreAttributeAddresses[core], 0x8010).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.write16(coreAttributeAddresses[1 - core], 0x10).status ==
      IOPBusStatus::Completed);
    system.runMasterCycles(80);
    bus.reset();
    system.iopCore().reset();
    REQUIRE(bus.read16(coreAttributeAddresses[core]).value == 0x8010);
    REQUIRE(bus.read16(coreStatusAddresses[core]).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(coreStatusAddresses[core]).value == 0x80);
    REQUIRE(bus.write16(transferDataAddresses[core], 0xabcd).status ==
      IOPBusStatus::Completed);
    REQUIRE(spu.readSoundRAMHalfword(cursor) == 0xabcd);
    REQUIRE(system.iopInterruptController().status() == (1u << 9));
    REQUIRE(system.iopDMAController().auxiliaryControl() == 0x12345678);
    spu.reset();
    REQUIRE(bus.read16(coreAttributeAddresses[core]).value == 0);
    REQUIRE(bus.read16(coreStatusAddresses[core]).status == IOPBusStatus::Completed);
  }
}

TEST_CASE("SPU2 I/O mode preserves selected input and PIO exclusions",
  "[iop][spu2][spu2-io-mode]")
{
  for (std::size_t core = 0; core < 2; ++core)
  {
    NekoSystem system;
    auto &bus = system.iopBus();
    SPU2InputTest::programProfile(system.spu2Controller());
    REQUIRE(bus.write16(autoDMAAddresses[core], autoDMAWriteSelections[core]).status ==
      IOPBusStatus::Completed);
    const auto selected = system.iopStateHash();
    REQUIRE(bus.write16(coreAttributeAddresses[core], 0xc011).status ==
      IOPBusStatus::Unmapped);
    REQUIRE(system.iopStateHash() == selected);
    REQUIRE(bus.write16(coreAttributeAddresses[1 - core], 0xc011).status ==
      IOPBusStatus::Completed);
    const auto before = system.iopStateHash();
    for (const auto address : transferDataAddresses)
    {
      REQUIRE(bus.write16(address, 0x0707).status == IOPBusStatus::Unmapped);
    }
    REQUIRE(bus.write16(autoDMAAddresses[1 - core],
      autoDMAWriteSelections[1 - core]).status == IOPBusStatus::Unmapped);
    REQUIRE(system.iopStateHash() == before);
    const auto checkpoint = system.saveState();
    SaveStateMutation invalid(checkpoint);
    invalid.writeScalar(
      "system.iop.spu2.coreAttributes[" + std::to_string(core) + "].value", 0xc011);
    invalid.refreshChecksum();
    REQUIRE_THROWS_AS(system.loadState(invalid), std::invalid_argument);
    REQUIRE(system.saveState() == checkpoint);
  }
}

TEST_CASE("SPU2 I/O mode restores guest stores and cursor status boundaries",
  "[iop][spu2][spu2-io-mode][save-state][trace]")
{
  for (std::size_t core = 0; core < 2; ++core)
  for (unsigned cut : {32u, 40u})
  {
    CAPTURE(core);
    CAPTURE(cut);
    NekoSystem original;
    REQUIRE(original.iopBus().write16(
      transferStartAddressAddresses[core * 2 + 1], 0x2800).status ==
      IOPBusStatus::Completed);
    REQUIRE(original.iopBus().write16(transferDataAddresses[core], 0x0707).status ==
      IOPBusStatus::Completed);
    installInitializationStoreProgram(original,
      coreAttributeAddresses[core] | 0xa0000000u, 0x8010);
    original.runMasterCycles(cut);
    REQUIRE(original.iopCore().executionState() == IOPExecutionState::Running);
    const auto checkpoint = original.saveState();
    NekoSystem restored;
    NekoSystem plain;
    restored.loadState(checkpoint);
    plain.loadState(checkpoint);
    REQUIRE(restored.saveState() == checkpoint);
    original.startTrace();
    restored.startTrace();
    original.runMasterCycles(48 - cut);
    restored.runMasterCycles(48 - cut);
    plain.runMasterCycles(48 - cut);
    REQUIRE(original.iopCore().retiredInstructions() == 6);
    REQUIRE(original.iopCore().generalRegister(3) == 0x1234);
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireMachine(plain, original);
    for (auto *system : {&original, &restored, &plain})
    {
      REQUIRE(system->iopBus().read16(coreAttributeAddresses[core]).value == 0x8010);
      REQUIRE(system->iopBus().read16(coreStatusAddresses[core]).status ==
        IOPBusStatus::Completed);
      REQUIRE(system->iopBus().read16(coreStatusAddresses[core]).value == 0x80);
      REQUIRE(system->iopBus().write16(transferDataAddresses[core], 0x1234).status ==
        IOPBusStatus::Completed);
      REQUIRE(system->spu2Controller().readSoundRAMHalfword(0x2800) == 0x0707);
      REQUIRE(system->spu2Controller().readSoundRAMHalfword(0x2801) == 0x1234);
    }
    REQUIRE(restored.saveState() == original.saveState());
    REQUIRE(plain.saveState() == original.saveState());
    const auto before = restored.saveState();
    SaveStateMutation previousVersion(before);
    previousVersion.writeScalar("container.version", 109);
    previousVersion.refreshChecksum();
    REQUIRE_THROWS_AS(restored.loadState(previousVersion), std::invalid_argument);
    REQUIRE(restored.saveState() == before);
    for (const auto value : {0x8020u, 0x8030u, 0x8012u})
    {
      SaveStateMutation invalid(before);
      invalid.writeScalar(
        "system.iop.spu2.coreAttributes[" + std::to_string(core) + "].value", value);
      invalid.refreshChecksum();
      REQUIRE_THROWS_AS(restored.loadState(invalid), std::invalid_argument);
      REQUIRE(restored.saveState() == before);
    }
  }
}

TEST_CASE("SPU2 core attributes reject neighbors and other transports",
  "[iop][spu2][spu2-attr]")
{
  for (const auto address : coreAttributeAddresses)
  {
    CAPTURE(address);
    NekoSystem system;
    auto &bus = system.iopBus();
    REQUIRE(bus.write16(address, 0xc001).status == IOPBusStatus::Completed);
    const auto before = system.iopStateHash();
    REQUIRE(bus.read8(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read8(address + 1).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(address + 1, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read16(address + 1).status == IOPBusStatus::Misaligned);
    REQUIRE(bus.write16(address + 1, 0).status == IOPBusStatus::Misaligned);
    REQUIRE(bus.read32(address).status == IOPBusStatus::Misaligned);
    REQUIRE(bus.write32(address, 0).status == IOPBusStatus::Misaligned);
    const auto wordAddress = address & ~IOPAddress(3);
    REQUIRE(bus.read32(wordAddress).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(wordAddress, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.readInstruction32(wordAddress).status == IOPBusStatus::Unmapped);
    for (std::uint8_t mask = 1; mask <= 0xf; ++mask)
    {
      REQUIRE(bus.writeMasked32(wordAddress, 0, mask).status ==
        IOPBusStatus::Unmapped);
    }
    REQUIRE(bus.readDMAC32(wordAddress).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.writeDMAC32(wordAddress, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.dmaWriteStatus(wordAddress) == IOPBusStatus::Unmapped);
    std::array<std::uint8_t, 2> bytes = {{0x12, 0x34}};
    REQUIRE(bus.loadPhysical(address, bytes.data(), bytes.size()) ==
      IOPBusStatus::Unmapped);
    REQUIRE(bus.inspectPhysical(address, bytes.data(), bytes.size()) ==
      IOPBusStatus::Unmapped);
    REQUIRE(bytes == (std::array<std::uint8_t, 2>{{0x12, 0x34}}));
    for (IOPAddress other : {address + 2, address + 0x800,
        address | 0x80000000u, address | 0xa0000000u})
    {
      CAPTURE(other);
      REQUIRE(bus.read16(other).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write16(other, 0).status == IOPBusStatus::Unmapped);
    }
    std::uint16_t eeValue = 0x4321;
    REQUIRE_FALSE(system.eeBus().readData16(address, &eeValue));
    REQUIRE(eeValue == 0x4321);
    REQUIRE_FALSE(system.eeBus().writeData16(address, 0));
    IOPBus unattached;
    REQUIRE(unattached.read16(address).status == IOPBusStatus::Unmapped);
    REQUIRE(unattached.write16(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(system.iopStateHash() == before);
  }
}

TEST_CASE("SPU2 core attribute guest loads preserve aliases and branch load delays",
  "[iop][spu2][spu2-attr][trace]")
{
  for (const auto physical : coreAttributeAddresses)
  for (auto segment : {0u, 0x80000000u, 0xa0000000u})
  for (IOPWord load : {0x84220000u, 0x94220000u})
  for (const auto value : coreAttributeValues)
  {
    const auto address = physical | segment;
    CAPTURE(address);
    CAPTURE(load);
    NekoSystem system;
    programConfiguration(system);
    programMasterVolumes(system);
    REQUIRE(system.iopBus().write16(physical, value).status ==
      IOPBusStatus::Completed);
    const IOPWord expected = (value & 0x8000) != 0 && load == 0x84220000
      ? 0xffff0000u | value : value;
    installHalfwordReadProgram(system, address, load);
    system.startTrace();
    system.runMasterCycles(64);
    REQUIRE(system.iopCore().executionState() == IOPExecutionState::Running);
    REQUIRE(system.iopCore().exception().kind == IOPException::None);
    REQUIRE(system.iopCore().retiredInstructions() == 8);
    REQUIRE(system.iopCore().programCounter() == 0xa0000024);
    REQUIRE(system.iopCore().generalRegister(2) == expected);
    REQUIRE(system.iopCore().generalRegister(3) == 0xffffffff);
    REQUIRE(system.iopCore().generalRegister(4) == expected);
    REQUIRE(system.iopCore().generalRegister(5) == 0x1234);
    REQUIRE_FALSE(system.iopCore().branchContinuation().active);
    REQUIRE(system.iopCore().delayedResultContinuation().source ==
      IOPDelayedResultSource::None);
    IOPTraceTest::Events actual;
    unsigned delayLoads = 0;
    for (const auto &event : system.trace())
    {
      if (event.subsystem != NekoTraceSubsystem::IOP)
      {
        continue;
      }
      if (event.type == NekoTraceEventType::MemoryAccess &&
          event.value0 == address)
      {
        actual.push_back(event);
      }
      if (event.type == NekoTraceEventType::InstructionIssued &&
          event.value0 == 0xa0000010)
      {
        REQUIRE(event.masterCycle == 33);
        REQUIRE(event.value1 == load);
        REQUIRE(event.value3 == NekoIOPTraceInstruction::DELAY_SLOT);
        ++delayLoads;
      }
    }
    REQUIRE(delayLoads == 1);
    namespace Memory = NekoIOPTraceMemory;
    const auto route = segment == 0xa0000000
      ? Memory::CacheRoute::Uncached : Memory::CacheRoute::Cached;
    const auto metadata = nekoPackIOPMemoryTraceMetadata({
      physical, 2, Memory::AccessKind::DataLoad,
      Memory::Outcome::Completed, route, true, 0});
    IOPTraceTest::requireEvents(actual, {
      {33, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, value, 0, metadata}
    });
    for (std::size_t index = 0; index < configurationAddresses.size(); ++index)
    {
      REQUIRE(system.iopBus().read16(configurationAddresses[index]).value ==
        configurationValues[index]);
    }
    for (std::size_t index = 0; index < masterVolumeAddresses.size(); ++index)
    {
      REQUIRE(system.iopBus().read16(masterVolumeAddresses[index]).value ==
        masterVolumeValues[index]);
    }
    REQUIRE(system.iopInterruptController().status() == 0);
  }
}

TEST_CASE("SPU2 core attribute loads restore before and after the delayed result",
  "[iop][spu2][spu2-attr][save-state][trace]")
{
  for (const auto address : coreAttributeAddresses)
  for (IOPWord load : {0x84220000u, 0x94220000u})
  for (const auto value : coreAttributeValues)
  for (unsigned cut : {32u, 40u, 48u})
  {
    CAPTURE(address);
    CAPTURE(cut);
    CAPTURE(value);
    NekoSystem original;
    programConfiguration(original);
    programMasterVolumes(original);
    REQUIRE(original.iopBus().write16(address, value).status ==
      IOPBusStatus::Completed);
    const IOPWord expected = (value & 0x8000) != 0 && load == 0x84220000
      ? 0xffff0000u | value : value;
    installHalfwordReadProgram(original,
      address | 0xa0000000u, load);
    original.runMasterCycles(cut);
    REQUIRE(original.iopCore().branchContinuation().active == (cut == 32));
    REQUIRE((original.iopCore().delayedResultContinuation().source ==
      IOPDelayedResultSource::Load) == (cut == 40));
    const auto checkpoint = original.saveState();
    NekoSystem restored;
    NekoSystem plain;
    restored.loadState(checkpoint);
    plain.loadState(checkpoint);
    REQUIRE(plain.iopBus().read16(address).status == IOPBusStatus::Completed);
    REQUIRE(plain.iopBus().read16(address).value == value);
    REQUIRE(plain.saveState() == checkpoint);
    original.startTrace();
    restored.startTrace();
    original.runMasterCycles(64 - cut);
    restored.runMasterCycles(64 - cut);
    plain.runMasterCycles(64 - cut);
    REQUIRE(original.iopCore().retiredInstructions() == 8);
    REQUIRE(original.iopCore().generalRegister(3) == 0xffffffff);
    REQUIRE(original.iopCore().generalRegister(4) == expected);
    REQUIRE(original.iopCore().generalRegister(5) == 0x1234);
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireMachine(plain, original);
  }
}

TEST_CASE("SPU2 ADMAS loads restore before and after the delayed result",
  "[iop][spu2][spu2-admas-read][save-state][trace]")
{
  for (std::size_t core = 0; core < autoDMAAddresses.size(); ++core)
  for (unsigned cut : {32u, 40u})
  {
    CAPTURE(core);
    CAPTURE(cut);
    NekoSystem original;
    SPU2InputTest::programProfile(original.spu2Controller());
    const auto address = autoDMAAddresses[core];
    const auto selection = autoDMAWriteSelections[core];
    REQUIRE(original.iopBus().write16(address, selection).status ==
      IOPBusStatus::Completed);
    installHalfwordReadProgram(original, address | 0xa0000000u, 0x94220000);
    original.runMasterCycles(cut);
    REQUIRE(original.iopCore().branchContinuation().active == (cut == 32));
    REQUIRE((original.iopCore().delayedResultContinuation().source ==
      IOPDelayedResultSource::Load) == (cut == 40));
    const auto checkpoint = original.saveState();
    NekoSystem restored;
    NekoSystem plain;
    restored.loadState(checkpoint);
    plain.loadState(checkpoint);
    REQUIRE(plain.iopBus().read16(address).status == IOPBusStatus::Completed);
    REQUIRE(plain.iopBus().read16(address).value == selection);
    REQUIRE(plain.saveState() == checkpoint);
    original.startTrace();
    restored.startTrace();
    original.runMasterCycles(64 - cut);
    restored.runMasterCycles(64 - cut);
    plain.runMasterCycles(64 - cut);
    REQUIRE(original.iopCore().retiredInstructions() == 8);
    REQUIRE(original.iopCore().exception().kind == IOPException::None);
    REQUIRE(original.iopCore().generalRegister(3) == 0xffffffff);
    REQUIRE(original.iopCore().generalRegister(4) == selection);
    REQUIRE(original.iopCore().generalRegister(5) == 0x1234);
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireMachine(plain, original);
  }
}

TEST_CASE("SPU2 write-only initialization admits only exact zero halfword stores",
  "[iop][spu2][spu2-initialization]")
{
  for (const auto address : {unknownInitializationAddresses[0],
      unknownInitializationAddresses[1], IOPAddress(0x1f9007b8),
      IOPAddress(0x1f9007ba), IOPAddress(0x1f9007bc), IOPAddress(0x1f9007be)})
  {
    CAPTURE(address);
    NekoSystem system;
    programConfiguration(system);
    programMasterVolumes(system);
    system.iopInterruptController().assertSource(IOPInterruptSource::SPU);
    system.iopDMAController().writeAuxiliaryControl(0x12345678);
    auto &bus = system.iopBus();
    const auto before = system.iopStateHash();
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Completed);
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Completed);
    for (std::uint32_t value = 1; value <= 0xffff; ++value)
    {
      const auto result = bus.write16(address, static_cast<std::uint16_t>(value));
      if (result.status != IOPBusStatus::Unmapped)
      {
        FAIL("Nonzero initialization admitted: " << value);
      }
    }
    REQUIRE(system.iopStateHash() == before);
    REQUIRE(system.iopInterruptController().status() == (1u << 9));
    REQUIRE(system.iopDMAController().auxiliaryControl() == 0x12345678);
    for (std::size_t index = 0; index < configurationAddresses.size(); ++index)
    {
      REQUIRE(bus.read16(configurationAddresses[index]).value ==
        configurationValues[index]);
    }
    for (std::size_t index = 0; index < masterVolumeAddresses.size(); ++index)
    {
      REQUIRE(bus.read16(masterVolumeAddresses[index]).value ==
        masterVolumeValues[index]);
    }
    system.runMasterCycles(80);
    bus.reset();
    system.iopCore().reset();
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Completed);
    system.spu2Controller().reset();
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Completed);
    system.reset();
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Completed);
  }
}

TEST_CASE("SPU2 initialization and Auto DMA controls reject unsupported reads and transports",
  "[iop][spu2][spu2-initialization][spu2-autodma]")
{
  for (const auto address : {unknownInitializationAddresses[0],
      unknownInitializationAddresses[1], IOPAddress(0x1f9007b8),
      IOPAddress(0x1f9007ba), IOPAddress(0x1f9007bc), IOPAddress(0x1f9007be),
      autoDMAAddresses[0], autoDMAAddresses[1]})
  {
    CAPTURE(address);
    NekoSystem system;
    auto &bus = system.iopBus();
    const std::uint16_t selection = address == autoDMAAddresses[0] ? 1 :
      address == autoDMAAddresses[1] ? 2 : 0;
    SPU2InputTest::programProfile(system.spu2Controller());
    REQUIRE(bus.write16(address, selection).status == IOPBusStatus::Completed);
    const auto before = system.iopStateHash();
    const auto read = bus.read16(address);
    REQUIRE(read.status == (selection == 0
      ? IOPBusStatus::Unmapped : IOPBusStatus::Completed));
    if (selection != 0)
    {
      REQUIRE(read.value == selection);
    }
    REQUIRE(bus.read8(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read8(address + 1).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(address + 1, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read16(address + 1).status == IOPBusStatus::Misaligned);
    REQUIRE(bus.write16(address + 1, 0).status == IOPBusStatus::Misaligned);
    const auto wordStatus = address % 4 == 0
      ? IOPBusStatus::Unmapped : IOPBusStatus::Misaligned;
    REQUIRE(bus.read32(address).status == wordStatus);
    REQUIRE(bus.write32(address, 0).status == wordStatus);
    REQUIRE(bus.readInstruction32(address).status == wordStatus);
    for (std::uint8_t mask = 1; mask <= 0xf; ++mask)
    {
      REQUIRE(bus.writeMasked32(address, 0, mask).status == wordStatus);
    }
    REQUIRE(bus.readDMAC32(address).status == wordStatus);
    REQUIRE(bus.writeDMAC32(address, 0).status == wordStatus);
    REQUIRE(bus.dmaWriteStatus(address) == wordStatus);
    std::array<std::uint8_t, 2> bytes = {{0x12, 0x34}};
    REQUIRE(bus.loadPhysical(address, bytes.data(), bytes.size()) ==
      IOPBusStatus::Unmapped);
    REQUIRE(bus.inspectPhysical(address, bytes.data(), bytes.size()) ==
      IOPBusStatus::Unmapped);
    REQUIRE(bytes == (std::array<std::uint8_t, 2>{{0x12, 0x34}}));
    for (IOPAddress other : {0x1f9007b0u, 0x1f9007b2u, 0x1f9007b4u,
        0x1f9007b6u,
        0x1f9003b8u, 0x1f900bb8u, 0x1f900fb8u,
        0x1f9003bau, 0x1f900bbau, 0x1f900fbau,
        0x1f9003bcu, 0x1f900bbcu, 0x1f900fbcu,
        0x1f9003beu, 0x1f900bbeu, 0x1f900fbeu,
        0x1f900b5eu, 0x1f900b64u,
        0x1f900360u, 0x1f900362u, 0x1f900f60u, 0x1f900f62u,
        0x1f9001aeu, 0x1f9001b2u, 0x1f9005aeu, 0x1f9005b2u,
        0x1f9009b0u, 0x1f900db0u,
        address | 0x80000000u, address | 0xa0000000u})
    {
      CAPTURE(other);
      REQUIRE(bus.read16(other).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write16(other, 0).status == IOPBusStatus::Unmapped);
    }
    std::uint16_t eeValue = 0x4321;
    REQUIRE_FALSE(system.eeBus().readData16(address, &eeValue));
    REQUIRE(eeValue == 0x4321);
    REQUIRE_FALSE(system.eeBus().writeData16(address, selection));
    IOPBus unattached;
    REQUIRE(unattached.read16(address).status == IOPBusStatus::Unmapped);
    REQUIRE(unattached.write16(address, selection).status == IOPBusStatus::Unmapped);
    REQUIRE(system.iopStateHash() == before);
  }
}

TEST_CASE("SPU2 unnamed halfword initialization preserves guest store outcomes",
  "[iop][spu2][spu2-initialization][trace]")
{
  for (IOPAddress physical : {
      0x1f9007b8u, 0x1f9007bau, 0x1f9007bcu, 0x1f9007beu})
  for (auto segment : {0u, 0x80000000u, 0xa0000000u})
  for (std::uint16_t value : {0, 1, 0xffff})
  for (bool delaySlot : {false, true})
  {
    const auto address = physical | segment;
    CAPTURE(address);
    CAPTURE(value);
    CAPTURE(delaySlot);
    NekoSystem system;
    SPU2InputTest::programProfile(system.spu2Controller());
    REQUIRE(system.spu2Controller().writeAutoDMAWriteSelection(0, 1));
    REQUIRE(system.spu2Controller().beginInputWrite(0));
    REQUIRE(system.spu2Controller().writeInputWord(0, 0x12345678));
    system.iopInterruptController().assertSource(IOPInterruptSource::SPU);
    system.iopDMAController().writeAuxiliaryControl(0x12345678);
    installInitializationStoreProgram(system, address, value);
    if (!delaySlot)
    {
      REQUIRE(system.iopBus().write32(0xc, 0).status == IOPBusStatus::Completed);
      REQUIRE(system.iopBus().write32(0x14, 0).status == IOPBusStatus::Completed);
    }
    const auto before = system.spu2Controller().inputWriteState(0);
    system.startTrace();
    system.runMasterCycles(delaySlot ? 48 : 56);
    const bool accepted = value == 0;
    REQUIRE(system.iopCore().exception().kind == IOPException::None);
    REQUIRE(system.iopCore().executionState() == (accepted
      ? IOPExecutionState::Running : IOPExecutionState::Halted));
    REQUIRE(system.iopCore().retiredInstructions() ==
      (accepted ? (delaySlot ? 6 : 7) : 4));
    REQUIRE(system.iopCore().programCounter() ==
      (accepted ? 0xa000001c : 0xa0000010));
    REQUIRE(system.iopCore().generalRegister(3) == (accepted ? 0x1234 : 0));
    if (!accepted)
    {
      REQUIRE(system.iopCore().stopReason() == IOPStopReason::StoreFailure);
      REQUIRE(system.iopCore().storeFailure().value == value);
    }
    IOPTraceTest::Events actual;
    unsigned stores = 0;
    for (const auto &event : system.trace())
    {
      if (event.subsystem == NekoTraceSubsystem::IOP &&
          event.type == NekoTraceEventType::MemoryAccess &&
          event.value0 == address)
      {
        actual.push_back(event);
      }
      if (event.subsystem == NekoTraceSubsystem::IOP &&
          event.type == NekoTraceEventType::InstructionIssued &&
          event.value0 == 0xa0000010)
      {
        REQUIRE(event.value1 == 0xa4220000);
        REQUIRE(event.value3 == (delaySlot
          ? NekoIOPTraceInstruction::DELAY_SLOT : 0));
        ++stores;
      }
    }
    REQUIRE(stores == 1);
    namespace Memory = NekoIOPTraceMemory;
    const auto metadata = nekoPackIOPMemoryTraceMetadata({
      physical, 2, Memory::AccessKind::DataStore,
      accepted ? Memory::Outcome::Completed : Memory::Outcome::Unmapped,
      segment == 0xa0000000 ? Memory::CacheRoute::Uncached :
        Memory::CacheRoute::Cached,
      true, static_cast<std::uint8_t>(3u << (physical & 3))});
    IOPTraceTest::requireEvents(actual, {
      {33, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, value, 0, metadata}
    });
    const auto after = system.spu2Controller().inputWriteState(0);
    REQUIRE(after.selected == before.selected);
    REQUIRE(after.writeOpen == before.writeOpen);
    REQUIRE(after.requested == before.requested);
    REQUIRE(after.fillHalf == before.fillHalf);
    REQUIRE(after.readHalf == before.readHalf);
    REQUIRE(after.fillWords == before.fillWords);
    REQUIRE(system.iopInterruptController().status() == (1u << 9));
    REQUIRE(system.iopDMAController().auxiliaryControl() == 0x12345678);
  }
}

TEST_CASE("SPU2 bounded initialization guests preserve aliases and delay-slot outcomes",
  "[iop][spu2][spu2-initialization][spu2-attr][spu2-autodma][spu2-koff][trace]")
{
  for (const auto &addresses : {
      unknownInitializationAddresses, coreAttributeAddresses, autoDMAAddresses,
      keyOffLowAddresses, keyOffHighAddresses})
  for (const auto physical : addresses)
  for (auto segment : {0u, 0x80000000u, 0xa0000000u})
  for (std::uint16_t value : {
      0, 1, 2, 0x4000, 0x4001, 0x8000, 0x8001, 0xc000, 0xc001, 0xc041, 0xffff})
  {
    const auto address = physical | segment;
    CAPTURE(address);
    CAPTURE(value);
    const bool accepted = value == 0 ||
      (addresses == autoDMAAddresses &&
        value == (physical == autoDMAAddresses[0] ? 1 : 2)) ||
      (addresses == coreAttributeAddresses &&
        (value == 1 || value == 0x4000 || value == 0x4001 ||
          value == 0x8000 || value == 0x8001 ||
          value == 0xc000 || value == 0xc001)) ||
      addresses == keyOffLowAddresses || addresses == keyOffHighAddresses;
    NekoSystem system;
    programConfiguration(system);
    programMasterVolumes(system);
    if (addresses == autoDMAAddresses)
    {
      SPU2InputTest::programProfile(system.spu2Controller());
    }
    system.iopInterruptController().assertSource(IOPInterruptSource::SPU);
    system.iopDMAController().writeAuxiliaryControl(0x12345678);
    installInitializationStoreProgram(system, address, value);
    system.startTrace();
    system.runMasterCycles(48);
    REQUIRE(system.iopCore().exception().kind == IOPException::None);
    if (accepted)
    {
      REQUIRE(system.iopCore().executionState() == IOPExecutionState::Running);
      REQUIRE(system.iopCore().retiredInstructions() == 6);
      REQUIRE(system.iopCore().programCounter() == 0xa000001c);
      REQUIRE(system.iopCore().generalRegister(3) == 0x1234);
      REQUIRE_FALSE(system.iopCore().branchContinuation().active);
    }
    else
    {
      REQUIRE(system.iopCore().executionState() == IOPExecutionState::Halted);
      REQUIRE(system.iopCore().stopReason() == IOPStopReason::StoreFailure);
      REQUIRE(system.iopCore().retiredInstructions() == 4);
      REQUIRE(system.iopCore().programCounter() == 0xa0000010);
      REQUIRE(system.iopCore().storeFailure().value == value);
      REQUIRE(system.iopCore().generalRegister(3) == 0);
    }
    IOPTraceTest::Events actual;
    unsigned delayStores = 0;
    for (const auto &event : system.trace())
    {
      if (event.subsystem != NekoTraceSubsystem::IOP)
      {
        continue;
      }
      if (event.type == NekoTraceEventType::MemoryAccess &&
          event.value0 == address)
      {
        actual.push_back(event);
      }
      if (event.type == NekoTraceEventType::InstructionIssued &&
          event.value0 == 0xa0000010)
      {
        REQUIRE(event.masterCycle == 33);
        REQUIRE(event.value1 == 0xa4220000);
        REQUIRE(event.value3 == NekoIOPTraceInstruction::DELAY_SLOT);
        ++delayStores;
      }
    }
    REQUIRE(delayStores == 1);
    namespace Memory = NekoIOPTraceMemory;
    const auto route = segment == 0xa0000000
      ? Memory::CacheRoute::Uncached : Memory::CacheRoute::Cached;
    const auto metadata = nekoPackIOPMemoryTraceMetadata({
      physical, 2, Memory::AccessKind::DataStore,
      accepted ? Memory::Outcome::Completed : Memory::Outcome::Unmapped,
      route, true, static_cast<std::uint8_t>(3u << (physical & 3))});
    IOPTraceTest::requireEvents(actual, {
      {33, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, value, 0, metadata}
    });
    if (addresses == coreAttributeAddresses)
    {
      REQUIRE(system.iopBus().read16(physical).status == IOPBusStatus::Completed);
      REQUIRE(system.iopBus().read16(physical).value == (accepted ? value : 0));
    }
    if (addresses == autoDMAAddresses)
    {
      const std::size_t core = physical == autoDMAAddresses[0] ? 0 : 1;
      REQUIRE(system.spu2Controller().readAutoDMAWriteSelection(core) ==
        (accepted ? value : 0));
      REQUIRE(system.spu2Controller().readAutoDMAWriteSelection(1 - core) == 0);
    }
    for (std::size_t index = 0; index < configurationAddresses.size(); ++index)
    {
      REQUIRE(system.iopBus().read16(configurationAddresses[index]).value ==
        (addresses == autoDMAAddresses
          ? SPU2InputTest::configuration[index] : configurationValues[index]));
    }
    for (std::size_t index = 0; index < masterVolumeAddresses.size(); ++index)
    {
      REQUIRE(system.iopBus().read16(masterVolumeAddresses[index]).value ==
        masterVolumeValues[index]);
    }
    REQUIRE(system.iopInterruptController().status() == (1u << 9));
    REQUIRE(system.iopDMAController().auxiliaryControl() == 0x12345678);
  }
}

TEST_CASE("SPU2 bounded initialization resumes pending stores canonically",
  "[iop][spu2][spu2-initialization][spu2-attr][spu2-autodma][spu2-koff][save-state][trace]")
{
  for (const auto &addresses : {
      unknownInitializationAddresses, coreAttributeAddresses, autoDMAAddresses,
      keyOffLowAddresses, keyOffHighAddresses})
  for (const auto address : addresses)
  for (std::uint16_t value : {
      0, 1, 2, 0x4000, 0x4001, 0x8000, 0x8001, 0xc000, 0xc001, 0xffff})
  for (unsigned cut : {32u, 40u})
  {
    const bool keyOff = addresses == keyOffLowAddresses ||
      addresses == keyOffHighAddresses;
    const bool autoDMA = addresses == autoDMAAddresses &&
      value == (address == autoDMAAddresses[0] ? 1 : 2);
    if (value != 0 && !keyOff && !autoDMA &&
        (addresses != coreAttributeAddresses ||
          (value != 1 && value != 0x4000 && value != 0x4001 &&
            value != 0x8000 && value != 0x8001 &&
            value != 0xc000 && value != 0xc001)))
    {
      continue;
    }
    CAPTURE(address);
    CAPTURE(cut);
    NekoSystem original;
    programConfiguration(original);
    programMasterVolumes(original);
    if (addresses == autoDMAAddresses)
    {
      SPU2InputTest::programProfile(original.spu2Controller());
    }
    original.iopInterruptController().assertSource(IOPInterruptSource::SPU);
    original.iopDMAController().writeAuxiliaryControl(0x12345678);
    installInitializationStoreProgram(original,
      address | 0xa0000000u, value);
    original.runMasterCycles(cut);
    REQUIRE(original.iopCore().branchContinuation().active == (cut == 32));
    const auto checkpoint = original.saveState();
    NekoSystem restored;
    NekoSystem plain;
    restored.loadState(checkpoint);
    plain.loadState(checkpoint);
    const auto before = plain.iopStateHash();
    const std::uint16_t retained = cut == 32 ? 0 : value;
    if (addresses == autoDMAAddresses)
    {
      REQUIRE(plain.spu2Controller().readAutoDMAWriteSelection(
        address == autoDMAAddresses[0] ? 0 : 1) == retained);
    }
    REQUIRE(plain.iopBus().write16(address, retained).status ==
      IOPBusStatus::Completed);
    REQUIRE(plain.iopStateHash() == before);
    if (addresses == coreAttributeAddresses)
    {
      REQUIRE(plain.iopBus().read16(address).status == IOPBusStatus::Completed);
      REQUIRE(plain.iopBus().read16(address).value == retained);
    }
    REQUIRE(plain.saveState() == checkpoint);
    original.startTrace();
    restored.startTrace();
    original.runMasterCycles(48 - cut);
    restored.runMasterCycles(48 - cut);
    plain.runMasterCycles(48 - cut);
    REQUIRE(original.iopCore().executionState() == IOPExecutionState::Running);
    REQUIRE(original.iopCore().retiredInstructions() == 6);
    REQUIRE(original.iopCore().generalRegister(3) == 0x1234);
    REQUIRE(original.iopInterruptController().status() == (1u << 9));
    REQUIRE(original.iopDMAController().auxiliaryControl() == 0x12345678);
    if (addresses == coreAttributeAddresses)
    {
      REQUIRE(original.iopBus().read16(address).value == value);
    }
    if (addresses == autoDMAAddresses)
    {
      REQUIRE(original.spu2Controller().readAutoDMAWriteSelection(
        address == autoDMAAddresses[0] ? 0 : 1) == value);
    }
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireMachine(plain, original);
  }
}

TEST_CASE("SPU2 output control retains every image without signals or other programming",
  "[iop][spu2][spu2-output]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  programConfiguration(system);
  programMasterVolumes(system);
  for (const auto address : coreAttributeAddresses)
  {
    REQUIRE(bus.write16(address, 0x8000).status == IOPBusStatus::Completed);
  }
  system.iopInterruptController().assertSource(IOPInterruptSource::SPU);
  system.iopDMAController().writeAuxiliaryControl(0x12345678);
  const auto before = system.iopStateHash();
  for (std::uint32_t value = 0; value <= 0xffff; ++value)
  {
    const auto write = bus.write16(0x1f9007c0, static_cast<std::uint16_t>(value));
    const auto read = bus.read16(0x1f9007c0);
    if (write.status != IOPBusStatus::Completed ||
        read.status != IOPBusStatus::Completed || read.value != value)
    {
      FAIL("Output-control image did not round-trip: " << value);
    }
  }
  REQUIRE(bus.write16(0x1f9007c0, 0xc032).status == IOPBusStatus::Completed);
  for (const auto address : coreStatusAddresses)
  {
    REQUIRE(bus.read16(address).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(address).value == 0);
  }
  for (const auto &addresses : {keyOffLowAddresses, keyOffHighAddresses})
  for (const auto address : addresses)
  {
    REQUIRE(bus.write16(address, 0xffff).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(address).status == IOPBusStatus::Unmapped);
  }
  REQUIRE(system.iopStateHash() == before);
  system.runMasterCycles(80);
  REQUIRE(bus.read16(0x1f9007c0).value == 0xc032);
  REQUIRE(system.iopInterruptController().status() == (1u << 9));
  REQUIRE(system.iopDMAController().auxiliaryControl() == 0x12345678);
}

TEST_CASE("SPU2 S/PDIF configuration retains independent halfwords",
  "[iop][spu2]")
{
  NekoSystem system;
  for (auto address : configurationAddresses)
  {
    CAPTURE(address);
    REQUIRE(system.iopBus().read16(address).status == IOPBusStatus::Completed);
    REQUIRE(system.iopBus().read16(address).value == 0);
  }
  programConfiguration(system);
  for (std::size_t selected = 0; selected < configurationAddresses.size(); ++selected)
  {
    for (auto value : {0xffff, 0, 0x0008, 0x0900, 0x55aa, 0x8001})
    {
      REQUIRE(system.iopBus().write16(configurationAddresses[selected],
        static_cast<std::uint16_t>(value)).status == IOPBusStatus::Completed);
      for (std::size_t index = 0; index < configurationAddresses.size(); ++index)
      {
        REQUIRE(system.iopBus().read16(configurationAddresses[index]).value ==
          (index == selected ? value : configurationValues[index]));
      }
    }
    programConfiguration(system);
  }
  REQUIRE(system.iopInterruptController().status() == 0);
}

TEST_CASE("SPU2 fixed master volumes retain controls and reject sweep modes",
  "[iop][spu2]")
{
  NekoSystem system;
  programConfiguration(system);
  for (auto address : masterVolumeAddresses)
  {
    CAPTURE(address);
    REQUIRE(system.iopBus().read16(address).status == IOPBusStatus::Completed);
    REQUIRE(system.iopBus().read16(address).value == 0);
  }
  programMasterVolumes(system);
  for (std::size_t selected = 0; selected < masterVolumeAddresses.size(); ++selected)
  {
    CAPTURE(selected);
    for (auto value : {0, 1, 0x3fff, 0x4000, 0x7fff})
    {
      REQUIRE(system.iopBus().write16(masterVolumeAddresses[selected],
        static_cast<std::uint16_t>(value)).status == IOPBusStatus::Completed);
      for (std::size_t index = 0; index < masterVolumeAddresses.size(); ++index)
      {
        REQUIRE(system.iopBus().read16(masterVolumeAddresses[index]).value ==
          (index == selected ? value : masterVolumeValues[index]));
        REQUIRE(system.iopBus().read16(configurationAddresses[index]).value ==
          configurationValues[index]);
      }
    }
    programMasterVolumes(system);
    const auto before = system.iopStateHash();
    for (auto value : {0x8000, 0x9000, 0xa000, 0xb000,
        0xc000, 0xd000, 0xe000, 0xf000, 0xffff})
    {
      REQUIRE(system.iopBus().write16(masterVolumeAddresses[selected],
        static_cast<std::uint16_t>(value)).status == IOPBusStatus::Unmapped);
      REQUIRE(system.iopBus().read16(masterVolumeAddresses[selected]).value ==
        masterVolumeValues[selected]);
    }
    REQUIRE(system.iopStateHash() == before);
  }
  system.runMasterCycles(80);
  for (std::size_t index = 0; index < masterVolumeAddresses.size(); ++index)
  {
    REQUIRE(system.iopBus().read16(masterVolumeAddresses[index]).value ==
      masterVolumeValues[index]);
  }
  REQUIRE(system.iopInterruptController().status() == 0);
}

TEST_CASE("SPU2 fixed voice volumes retain independent controls and reject sweeps",
  "[iop][spu2][spu2-voice-volume]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  REQUIRE(bus.write16(0x1f900400, 0).status == IOPBusStatus::Completed);
  for (std::size_t index = 0; index < voiceVolumeCount; ++index)
  {
    CAPTURE(index);
    REQUIRE(bus.read16(voiceVolumeAddress(index)).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(voiceVolumeAddress(index)).value == 0);
  }
  programVoiceVolumes(system);
  for (std::size_t selected = 0; selected < voiceVolumeCount; ++selected)
  {
    CAPTURE(selected);
    for (std::uint16_t value : {0, 1, 0x3fff, 0x4000, 0x7fff})
    {
      REQUIRE(bus.write16(voiceVolumeAddress(selected), value).status ==
        IOPBusStatus::Completed);
      for (std::size_t index = 0; index < voiceVolumeCount; ++index)
      {
        REQUIRE(bus.read16(voiceVolumeAddress(index)).value ==
          (index == selected ? value : voiceVolumeValue(index)));
      }
    }
    REQUIRE(bus.write16(voiceVolumeAddress(selected), voiceVolumeValue(selected)).status ==
      IOPBusStatus::Completed);
  }
  const auto before = system.iopStateHash();
  for (std::size_t index = 0; index < voiceVolumeCount; ++index)
  for (std::uint16_t value : {
      0x8000, 0x9001, 0xa123, 0xbfff, 0xc000, 0xd000, 0xe000, 0xf000, 0xffff})
  {
    CAPTURE(index);
    CAPTURE(value);
    REQUIRE(bus.write16(voiceVolumeAddress(index), value).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read16(voiceVolumeAddress(index)).value == voiceVolumeValue(index));
  }
  REQUIRE(system.iopStateHash() == before);
}

TEST_CASE("SPU2 fixed voice volumes exclude other fields widths and transports",
  "[iop][spu2][spu2-voice-volume]")
{
  NekoSystem system;
  programVoiceVolumes(system);
  auto &bus = system.iopBus();
  IOPBus unattached;
  const auto before = system.iopStateHash();
  for (std::size_t index = 0; index < voiceVolumeCount; ++index)
  {
    const auto address = voiceVolumeAddress(index);
    const auto wordAddress = address & ~IOPAddress{3};
    CAPTURE(address);
    REQUIRE(bus.read8(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read8(address + 1).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(address + 1, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read16(address + 1).status == IOPBusStatus::Misaligned);
    REQUIRE(bus.write16(address + 1, 0).status == IOPBusStatus::Misaligned);
    REQUIRE(bus.read32(wordAddress).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(wordAddress, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.readInstruction32(wordAddress).status == IOPBusStatus::Unmapped);
    for (std::uint8_t mask = 1; mask <= 0xf; ++mask)
      REQUIRE(bus.writeMasked32(wordAddress, 0, mask).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.readDMAC32(wordAddress).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.writeDMAC32(wordAddress, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.dmaWriteStatus(wordAddress) == IOPBusStatus::Unmapped);
    std::array<std::uint8_t, 2> bytes = {{0x12, 0x34}};
    REQUIRE(bus.loadPhysical(address, bytes.data(), bytes.size()) == IOPBusStatus::Unmapped);
    REQUIRE(bus.inspectPhysical(address, bytes.data(), bytes.size()) == IOPBusStatus::Unmapped);
    REQUIRE(bytes == (std::array<std::uint8_t, 2>{{0x12, 0x34}}));
    for (auto segment : {0x80000000u, 0xa0000000u})
    {
      REQUIRE(bus.read16(address | segment).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write16(address | segment, 0).status == IOPBusStatus::Unmapped);
    }
    REQUIRE(unattached.read16(address).status == IOPBusStatus::Unmapped);
    REQUIRE(unattached.write16(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read16(address + 0x800).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write16(address + 0x800, 0).status == IOPBusStatus::Unmapped);
    std::uint16_t value = 0x4321;
    REQUIRE_FALSE(system.eeBus().readData16(address, &value));
    REQUIRE(value == 0x4321);
    REQUIRE_FALSE(system.eeBus().writeData16(address, 0));
    if (index % 2 == 0)
    {
      for (IOPAddress offset = 0xa; offset < 0x10; offset += 2)
      {
        REQUIRE(bus.read16(address + offset).status == IOPBusStatus::Unmapped);
        REQUIRE(bus.write16(address + offset, 0).status == IOPBusStatus::Unmapped);
      }
    }
  }
  for (IOPAddress address : {0x1f90017cu, 0x1f90017au, 0x1f90057cu,
      0x1f90057au, 0x1f801c00u, 0x1f801c02u})
  {
    REQUIRE(bus.read16(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Unmapped);
  }
  REQUIRE(system.iopStateHash() == before);
}

TEST_CASE("SPU2 fixed voice volumes preserve PCM PIO key-off and pending signals",
  "[iop][spu2][spu2-voice-volume][save-state]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  auto &spu = system.spu2Controller();
  programMasterVolumes(system);
  REQUIRE(bus.write16(transferStartAddressAddresses[1], 0x2800).status ==
    IOPBusStatus::Completed);
  REQUIRE(bus.write16(transferDataAddresses[0], 0x0707).status == IOPBusStatus::Completed);
  SPU2InputTest::programProfile(spu);
  for (std::size_t core = 0; core < 2; ++core)
  {
    REQUIRE(bus.write16(autoDMAAddresses[core], autoDMAWriteSelections[core]).status ==
      IOPBusStatus::Completed);
    REQUIRE(spu.writeInputWord(core, 0x12345678));
  }
  system.iopInterruptController().assertSource(IOPInterruptSource::SPU);
  system.iopDMAController().writeAuxiliaryControl(0x12345678);
  const auto before = system.saveState();
  programVoiceVolumes(system);
  for (const auto &addresses : {keyOffLowAddresses, keyOffHighAddresses})
  for (auto address : addresses)
    REQUIRE(bus.write16(address, 0xffff).status == IOPBusStatus::Completed);
  for (std::size_t index = 0; index < voiceVolumeCount; ++index)
  {
    REQUIRE(bus.read16(voiceVolumeAddress(index)).value == voiceVolumeValue(index));
    REQUIRE(bus.write16(voiceVolumeAddress(index), 0).status == IOPBusStatus::Completed);
  }
  REQUIRE(system.saveState() == before);
  programVoiceVolumes(system);
  SPU2InputTest::advanceSamples(spu, 1);
  for (std::size_t core = 0; core < 2; ++core)
    REQUIRE(bus.write16(autoDMAAddresses[core], 0).status == IOPBusStatus::Completed);
  REQUIRE(bus.write16(transferDataAddresses[0], 0xabcd).status == IOPBusStatus::Completed);
  REQUIRE(spu.readSoundRAMHalfword(0x2801) == 0xabcd);
  system.runMasterCycles(80);
  bus.reset();
  system.iopCore().reset();
  for (std::size_t index = 0; index < voiceVolumeCount; ++index)
    REQUIRE(bus.read16(voiceVolumeAddress(index)).value == voiceVolumeValue(index));
  REQUIRE(system.iopInterruptController().status() == (1u << 9));
  REQUIRE(system.iopDMAController().auxiliaryControl() == 0x12345678);
  spu.reset();
  for (std::size_t index = 0; index < voiceVolumeCount; ++index)
    REQUIRE(bus.read16(voiceVolumeAddress(index)).value == 0);
  programVoiceVolumes(system);
  system.reset();
  for (std::size_t index = 0; index < voiceVolumeCount; ++index)
    REQUIRE(bus.read16(voiceVolumeAddress(index)).value == 0);
}

TEST_CASE("SPU2 fixed voice-volume guests retain aliases and delayed load results",
  "[iop][spu2][spu2-voice-volume][trace]")
{
  for (auto index : {0u, 47u, 48u, 95u})
  for (auto segment : {0u, 0x80000000u, 0xa0000000u})
  for (std::uint16_t value : {0, 0x4000, 0x7fff})
  {
    CAPTURE(index);
    CAPTURE(segment);
    CAPTURE(value);
    NekoSystem system;
    const auto address = voiceVolumeAddress(index) | segment;
    installConfigurationProgram(system, address, value);
    system.startTrace();
    system.runMasterCycles(96);
    REQUIRE(system.iopCore().retiredInstructions() == 12);
    REQUIRE(system.iopCore().exception().kind == IOPException::None);
    REQUIRE(system.iopCore().generalRegister(5) == 0x1234);
    REQUIRE(system.iopBus().read32(0x100).value == 0);
    REQUIRE(system.iopBus().read32(0x104).value == value);
    REQUIRE(system.iopBus().read32(0x108).value == 0);
    REQUIRE(system.iopBus().read32(0x10c).value == value);
    unsigned accesses = 0;
    for (const auto &event : system.trace())
    {
      if (event.subsystem == NekoTraceSubsystem::IOP &&
          event.type == NekoTraceEventType::MemoryAccess && event.value0 == address)
      {
        REQUIRE(event.value1 == value);
        REQUIRE(event.masterCycle == (accesses == 0 ? 33 : accesses == 1 ? 41 : 65));
        ++accesses;
      }
    }
    REQUIRE(accesses == 3);
  }
}

TEST_CASE("SPU2 fixed voice-volume sweep guests fail before mutation",
  "[iop][spu2][spu2-voice-volume]")
{
  for (auto index : {0u, 47u, 48u, 95u})
  {
    NekoSystem system;
    programVoiceVolumes(system);
    installConfigurationProgram(system, voiceVolumeAddress(index) | 0xa0000000u, 0x8000);
    system.runMasterCycles(40);
    REQUIRE(system.iopCore().stopReason() == IOPStopReason::StoreFailure);
    REQUIRE(system.iopCore().programCounter() == 0xa0000010);
    REQUIRE(system.iopCore().retiredInstructions() == 4);
    for (std::size_t control = 0; control < voiceVolumeCount; ++control)
      REQUIRE(system.iopBus().read16(voiceVolumeAddress(control)).value ==
        voiceVolumeValue(control));
  }
}

TEST_CASE("SPU2 fixed voice volumes restore pending guest stores and loads",
  "[iop][spu2][spu2-voice-volume][save-state][trace]")
{
  for (auto index : {0u, 95u})
  for (unsigned cut : {32u, 48u, 72u})
  {
    NekoSystem original;
    programVoiceVolumes(original);
    installConfigurationProgram(original, voiceVolumeAddress(index) | 0xa0000000u, 0x4000);
    original.runMasterCycles(cut);
    const auto checkpoint = original.saveState();
    NekoSystem restored;
    NekoSystem plain;
    restored.loadState(checkpoint);
    plain.loadState(checkpoint);
    original.startTrace();
    restored.startTrace();
    original.runMasterCycles(96 - cut);
    restored.runMasterCycles(96 - cut);
    plain.runMasterCycles(96 - cut);
    REQUIRE(original.iopCore().retiredInstructions() == 12);
    REQUIRE(original.iopBus().read32(0x104).value == 0x4000);
    REQUIRE(original.iopBus().read32(0x10c).value == 0x4000);
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireMachine(plain, original);
  }
}

TEST_CASE("SPU2 fixed voice-volume canonical state validates controls transactionally",
  "[iop][spu2][spu2-voice-volume][save-state]")
{
  NekoSystem source;
  auto previous = source.iopStateHash();
  for (std::size_t index = 0; index < voiceVolumeCount; ++index)
  {
    REQUIRE(source.iopBus().write16(voiceVolumeAddress(index), voiceVolumeValue(index)).status ==
      IOPBusStatus::Completed);
    REQUIRE(source.iopStateHash() != previous);
    previous = source.iopStateHash();
  }
  const auto candidate = source.saveState();
  const auto fields = inspectSaveState(candidate);
  for (std::size_t index = 0; index < voiceVolumeCount; ++index)
  {
    const auto *field = fields.find(
      "system.iop.spu2.voiceVolumeControls[" + std::to_string(index) + "].value");
    REQUIRE(field != nullptr);
    REQUIRE(field->kind == SaveStateFieldKind::U16);
    REQUIRE(field->size == 2);
    REQUIRE(field->scalarValue == voiceVolumeValue(index));
  }
  NekoSystem destination;
  SPU2Controller foreign;
  destination.iopBus().attachSPU2(&foreign);
  destination.loadState(candidate);
  REQUIRE(destination.iopStateHash() == previous);
  REQUIRE(destination.saveState() == candidate);
  REQUIRE(destination.iopBus().write16(0x1f900572, 0x7fff).status == IOPBusStatus::Completed);
  REQUIRE(source.iopBus().read16(0x1f900572).value == voiceVolumeValue(95));
  IOPBus foreignBus;
  foreignBus.attachSPU2(&foreign);
  REQUIRE(foreignBus.read16(0x1f900572).value == 0);
  destination.startTrace();
  const auto before = destination.saveState();
  for (auto index : {0u, 47u, 48u, 95u})
  for (std::uint16_t value : {0x8000, 0xffff})
  {
    SaveStateMutation invalid(candidate);
    invalid.writeScalar(
      "system.iop.spu2.voiceVolumeControls[" + std::to_string(index) + "].value", value);
    invalid.refreshChecksum();
    REQUIRE_THROWS_AS(destination.loadState(invalid), std::invalid_argument);
    REQUIRE(destination.saveState() == before);
  }
  SaveStateMutation badChecksum(candidate);
  badChecksum.writeScalar("system.iop.spu2.voiceVolumeControls[95].value", 0);
  REQUIRE_THROWS_AS(destination.loadState(badChecksum), std::invalid_argument);
  REQUIRE(destination.saveState() == before);
  SaveStateMutation lateInvalid(candidate);
  lateInvalid.writeScalar("system.iop.timing.pixelPhase", 0xffff);
  lateInvalid.refreshChecksum();
  REQUIRE_THROWS_AS(destination.loadState(lateInvalid), std::invalid_argument);
  REQUIRE(destination.saveState() == before);
  for (auto version : {109u, 110u, 111u})
  {
    SaveStateMutation historical(candidate);
    historical.writeScalar("container.version", version);
    historical.refreshChecksum();
    REQUIRE_THROWS_AS(destination.loadState(historical), std::invalid_argument);
    REQUIRE(destination.saveState() == before);
  }
  REQUIRE(destination.traceEnabled());
}

TEST_CASE("SPU2 voice pitches retain independent full programmed halfwords",
  "[iop][spu2][spu2-pitch]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  REQUIRE(bus.write16(0x1f900404, 0x3fff).status == IOPBusStatus::Completed);
  REQUIRE(bus.read16(0x1f900404).value == 0x3fff);
  REQUIRE(bus.write16(0x1f900404, 0).status == IOPBusStatus::Completed);
  for (std::size_t index = 0; index < voicePitchCount; ++index)
  {
    REQUIRE(bus.read16(voicePitchAddress(index)).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(voicePitchAddress(index)).value == 0);
  }
  programVoiceVolumes(system);
  programVoicePitches(system);
  for (std::size_t selected = 0; selected < voicePitchCount; ++selected)
  {
    CAPTURE(selected);
    for (std::uint16_t value : {0, 1, 0x1000, 0x3fff, 0x4000, 0x7fff, 0x8000, 0xffff})
    {
      REQUIRE(bus.write16(voicePitchAddress(selected), value).status ==
        IOPBusStatus::Completed);
      for (std::size_t index = 0; index < voicePitchCount; ++index)
      {
        REQUIRE(bus.read16(voicePitchAddress(index)).value ==
          (index == selected ? value : voicePitchValue(index)));
      }
    }
    REQUIRE(bus.write16(voicePitchAddress(selected), voicePitchValue(selected)).status ==
      IOPBusStatus::Completed);
  }
  for (std::size_t index = 0; index < voiceVolumeCount; ++index)
    REQUIRE(bus.read16(voiceVolumeAddress(index)).value == voiceVolumeValue(index));
}

TEST_CASE("SPU2 voice pitches exclude other fields widths and transports",
  "[iop][spu2][spu2-pitch]")
{
  NekoSystem system;
  programVoicePitches(system);
  auto &bus = system.iopBus();
  IOPBus unattached;
  const auto before = system.iopStateHash();
  for (std::size_t index = 0; index < voicePitchCount; ++index)
  {
    const auto address = voicePitchAddress(index);
    CAPTURE(address);
    REQUIRE(bus.read8(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read8(address + 1).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(address + 1, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read16(address + 1).status == IOPBusStatus::Misaligned);
    REQUIRE(bus.write16(address + 1, 0).status == IOPBusStatus::Misaligned);
    REQUIRE(bus.read32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.readInstruction32(address).status == IOPBusStatus::Unmapped);
    for (std::uint8_t mask = 1; mask <= 0xf; ++mask)
      REQUIRE(bus.writeMasked32(address, 0, mask).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.readDMAC32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.writeDMAC32(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.dmaWriteStatus(address) == IOPBusStatus::Unmapped);
    std::array<std::uint8_t, 2> bytes = {{0x12, 0x34}};
    REQUIRE(bus.loadPhysical(address, bytes.data(), bytes.size()) == IOPBusStatus::Unmapped);
    REQUIRE(bus.inspectPhysical(address, bytes.data(), bytes.size()) == IOPBusStatus::Unmapped);
    REQUIRE(bytes == (std::array<std::uint8_t, 2>{{0x12, 0x34}}));
    for (auto segment : {0x80000000u, 0xa0000000u})
    {
      REQUIRE(bus.read16(address | segment).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write16(address | segment, 0).status == IOPBusStatus::Unmapped);
    }
    REQUIRE(unattached.read16(address).status == IOPBusStatus::Unmapped);
    REQUIRE(unattached.write16(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read16(address + 0x800).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write16(address + 0x800, 0).status == IOPBusStatus::Unmapped);
    std::uint16_t value = 0x4321;
    REQUIRE_FALSE(system.eeBus().readData16(address, &value));
    REQUIRE(value == 0x4321);
    REQUIRE_FALSE(system.eeBus().writeData16(address, 0));
    for (IOPAddress offset = 6; offset < 0xc; offset += 2)
    {
      REQUIRE(bus.read16(address + offset).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write16(address + offset, 0).status == IOPBusStatus::Unmapped);
    }
  }
  for (IOPAddress address : {0x1f9001b4u, 0x1f9005b4u, 0x1f801c04u, 0x1f801d74u})
  {
    REQUIRE(bus.read16(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Unmapped);
  }
  REQUIRE(system.iopStateHash() == before);
}

TEST_CASE("SPU2 voice pitches preserve programming PCM PIO key-off and pending signals",
  "[iop][spu2][spu2-pitch][save-state]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  auto &spu = system.spu2Controller();
  programConfiguration(system);
  programMasterVolumes(system);
  programVoiceVolumes(system);
  programVoiceModes(system);
  programVoiceMix(system);
  programMainMix(system);
  programEffectInputVolumes(system);
  programEffectEndAddresses(system);
  REQUIRE(bus.write16(transferStartAddressAddresses[1], 0x2800).status ==
    IOPBusStatus::Completed);
  REQUIRE(bus.write16(transferDataAddresses[0], 0x0707).status == IOPBusStatus::Completed);
  SPU2InputTest::programProfile(spu);
  for (std::size_t core = 0; core < 2; ++core)
  {
    REQUIRE(bus.write16(autoDMAAddresses[core], autoDMAWriteSelections[core]).status ==
      IOPBusStatus::Completed);
    REQUIRE(spu.writeInputWord(core, 0x12345678));
  }
  system.iopInterruptController().assertSource(IOPInterruptSource::SPU);
  system.iopDMAController().writeAuxiliaryControl(0x12345678);
  const auto before = system.saveState();
  programVoicePitches(system);
  for (const auto &addresses : {keyOffLowAddresses, keyOffHighAddresses})
  for (auto address : addresses)
    REQUIRE(bus.write16(address, 0xffff).status == IOPBusStatus::Completed);
  for (std::size_t index = 0; index < voicePitchCount; ++index)
  {
    REQUIRE(bus.read16(voicePitchAddress(index)).value == voicePitchValue(index));
    REQUIRE(bus.write16(voicePitchAddress(index), 0).status == IOPBusStatus::Completed);
  }
  REQUIRE(system.saveState() == before);
  programVoicePitches(system);
  SPU2InputTest::advanceSamples(spu, 1);
  for (std::size_t core = 0; core < 2; ++core)
  {
    REQUIRE(bus.write16(autoDMAAddresses[core], 0).status == IOPBusStatus::Completed);
    REQUIRE(bus.write16(coreAttributeAddresses[core], 0x8010).status ==
      IOPBusStatus::Completed);
  }
  programVoiceVolumes(system);
  REQUIRE(bus.write16(transferDataAddresses[0], 0xabcd).status == IOPBusStatus::Completed);
  REQUIRE(spu.readSoundRAMHalfword(0x2801) == 0xabcd);
  system.runMasterCycles(80);
  bus.reset();
  system.iopCore().reset();
  for (std::size_t index = 0; index < voicePitchCount; ++index)
    REQUIRE(bus.read16(voicePitchAddress(index)).value == voicePitchValue(index));
  REQUIRE(system.iopInterruptController().status() == (1u << 9));
  REQUIRE(system.iopDMAController().auxiliaryControl() == 0x12345678);
  spu.reset();
  for (std::size_t index = 0; index < voicePitchCount; ++index)
    REQUIRE(bus.read16(voicePitchAddress(index)).value == 0);
  programVoicePitches(system);
  system.reset();
  for (std::size_t index = 0; index < voicePitchCount; ++index)
    REQUIRE(bus.read16(voicePitchAddress(index)).value == 0);
}

TEST_CASE("SPU2 voice-pitch guests retain aliases signed values and delayed loads",
  "[iop][spu2][spu2-pitch][trace]")
{
  for (auto index : {0u, 23u, 24u, 47u})
  for (auto segment : {0u, 0x80000000u, 0xa0000000u})
  for (std::uint16_t value : {0x3fff, 0x8000, 0xffff})
  {
    CAPTURE(index);
    CAPTURE(segment);
    CAPTURE(value);
    NekoSystem system;
    const auto address = voicePitchAddress(index) | segment;
    const IOPWord signedValue = (value & 0x8000) != 0 ? 0xffff0000u | value : value;
    installConfigurationProgram(system, address, value);
    system.startTrace();
    system.runMasterCycles(96);
    REQUIRE(system.iopCore().retiredInstructions() == 12);
    REQUIRE(system.iopCore().exception().kind == IOPException::None);
    REQUIRE(system.iopCore().generalRegister(3) == signedValue);
    REQUIRE(system.iopCore().generalRegister(4) == value);
    REQUIRE(system.iopCore().generalRegister(5) == 0x1234);
    REQUIRE(system.iopBus().read32(0x100).value == 0);
    REQUIRE(system.iopBus().read32(0x104).value == signedValue);
    REQUIRE(system.iopBus().read32(0x108).value == 0);
    REQUIRE(system.iopBus().read32(0x10c).value == value);
    unsigned accesses = 0;
    for (const auto &event : system.trace())
    {
      if (event.subsystem == NekoTraceSubsystem::IOP &&
          event.type == NekoTraceEventType::MemoryAccess && event.value0 == address)
      {
        REQUIRE(event.value1 == value);
        REQUIRE(event.masterCycle == (accesses == 0 ? 33 : accesses == 1 ? 41 : 65));
        ++accesses;
      }
    }
    REQUIRE(accesses == 3);
  }
}

TEST_CASE("SPU2 voice pitches restore pending guest stores and signed loads",
  "[iop][spu2][spu2-pitch][save-state][trace]")
{
  for (auto index : {0u, 47u})
  for (unsigned cut : {32u, 48u, 72u})
  {
    NekoSystem original;
    programVoicePitches(original);
    installConfigurationProgram(original, voicePitchAddress(index) | 0xa0000000u, 0xffff);
    original.runMasterCycles(cut);
    const auto checkpoint = original.saveState();
    NekoSystem restored;
    NekoSystem plain;
    restored.loadState(checkpoint);
    plain.loadState(checkpoint);
    original.startTrace();
    restored.startTrace();
    original.runMasterCycles(96 - cut);
    restored.runMasterCycles(96 - cut);
    plain.runMasterCycles(96 - cut);
    REQUIRE(original.iopCore().retiredInstructions() == 12);
    REQUIRE(original.iopBus().read32(0x104).value == 0xffffffff);
    REQUIRE(original.iopBus().read32(0x10c).value == 0xffff);
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireMachine(plain, original);
  }
}

TEST_CASE("SPU2 voice-pitch canonical state retains upper bits and rejects corrupt states",
  "[iop][spu2][spu2-pitch][save-state]")
{
  NekoSystem source;
  auto previous = source.iopStateHash();
  for (std::size_t index = 0; index < voicePitchCount; ++index)
  {
    REQUIRE(source.iopBus().write16(voicePitchAddress(index), voicePitchValue(index)).status ==
      IOPBusStatus::Completed);
    REQUIRE(source.iopStateHash() != previous);
    previous = source.iopStateHash();
  }
  const auto candidate = source.saveState();
  const auto fields = inspectSaveState(candidate);
  for (std::size_t index = 0; index < voicePitchCount; ++index)
  {
    const auto *field = fields.find(
      "system.iop.spu2.voicePitchControls[" + std::to_string(index) + "].value");
    REQUIRE(field != nullptr);
    REQUIRE(field->kind == SaveStateFieldKind::U16);
    REQUIRE(field->size == 2);
    REQUIRE(field->scalarValue == voicePitchValue(index));
  }
  NekoSystem destination;
  SPU2Controller foreign;
  destination.iopBus().attachSPU2(&foreign);
  destination.loadState(candidate);
  REQUIRE(destination.iopStateHash() == previous);
  REQUIRE(destination.saveState() == candidate);
  REQUIRE(destination.iopBus().write16(0x1f900574, 0xffff).status == IOPBusStatus::Completed);
  REQUIRE(source.iopBus().read16(0x1f900574).value == voicePitchValue(47));
  IOPBus foreignBus;
  foreignBus.attachSPU2(&foreign);
  REQUIRE(foreignBus.read16(0x1f900574).value == 0);
  SaveStateMutation upperBits(candidate);
  upperBits.writeScalar("system.iop.spu2.voicePitchControls[0].value", 0x4000);
  upperBits.writeScalar("system.iop.spu2.voicePitchControls[47].value", 0xffff);
  upperBits.refreshChecksum();
  destination.loadState(upperBits);
  REQUIRE(destination.iopBus().read16(0x1f900004).value == 0x4000);
  REQUIRE(destination.iopBus().read16(0x1f900574).value == 0xffff);
  destination.startTrace();
  const auto before = destination.saveState();
  SaveStateMutation badChecksum(candidate);
  badChecksum.writeScalar("system.iop.spu2.voicePitchControls[47].value", 0);
  REQUIRE_THROWS_AS(destination.loadState(badChecksum), std::invalid_argument);
  REQUIRE(destination.saveState() == before);
  SaveStateMutation lateInvalid(candidate);
  lateInvalid.writeScalar("system.iop.timing.pixelPhase", 0xffff);
  lateInvalid.refreshChecksum();
  REQUIRE_THROWS_AS(destination.loadState(lateInvalid), std::invalid_argument);
  REQUIRE(destination.saveState() == before);
  for (auto version : {110u, 111u, 112u})
  {
    SaveStateMutation historical(candidate);
    historical.writeScalar("container.version", version);
    historical.refreshChecksum();
    REQUIRE_THROWS_AS(destination.loadState(historical), std::invalid_argument);
    REQUIRE(destination.saveState() == before);
  }
  REQUIRE(destination.traceEnabled());
}

TEST_CASE("SPU2 SSA originating zero store retires",
  "[iop][spu2][spu2-ssa]")
{
  NekoSystem system;
  REQUIRE(system.iopBus().write32(0, 0x3c07bf90).status == IOPBusStatus::Completed);
  REQUIRE(system.iopBus().write32(4, 0x34e705c0).status == IOPBusStatus::Completed);
  REQUIRE(system.iopBus().write32(8, 0xa4e00000).status == IOPBusStatus::Completed);
  system.iopCore().startExecution(0xa0000000);
  system.runMasterCycles(24);
  REQUIRE(system.iopCore().retiredInstructions() == 3);
  REQUIRE(system.iopCore().exception().kind == IOPException::None);
  REQUIRE(system.iopCore().executionState() == IOPExecutionState::Running);
  REQUIRE(system.iopBus().read16(0x1f9005c0).value == 0);
}

TEST_CASE("SPU2 SSA normalizes independent high and low address halves",
  "[iop][spu2][spu2-ssa]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  for (std::size_t index = 0; index < voiceStartAddressCount; ++index)
  {
    REQUIRE(bus.read16(voiceStartAddress(index)).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(voiceStartAddress(index)).value == 0);
  }
  programVoiceStartAddresses(system);
  for (std::size_t selected = 0; selected < voiceStartAddressCount; ++selected)
  {
    CAPTURE(selected);
    for (std::uint16_t value : {0, 1, 7, 8, 0xf, 0x10, 0xfff7, 0xfff8, 0xffff})
    {
      REQUIRE(bus.write16(voiceStartAddress(selected), value).status ==
        IOPBusStatus::Completed);
      const auto expected = value & (selected % 2 == 0 ? 0xf : 0xfff8);
      for (std::size_t index = 0; index < voiceStartAddressCount; ++index)
        REQUIRE(bus.read16(voiceStartAddress(index)).value ==
          (index == selected ? expected : voiceStartValue(index)));
    }
    REQUIRE(bus.write16(voiceStartAddress(selected), voiceStartValue(selected)).status ==
      IOPBusStatus::Completed);
  }
}

TEST_CASE("SPU2 SSA owner normalizes direct writes and checks indices",
  "[iop][spu2][spu2-ssa]")
{
  SPU2Controller spu;
  for (std::size_t index : {0u, 1u, 94u, 95u})
  {
    spu.writeVoiceStartAddress(index, 0xffff);
    REQUIRE(spu.readVoiceStartAddress(index) == (index % 2 == 0 ? 0xf : 0xfff8));
  }
  REQUIRE_THROWS_AS(spu.readVoiceStartAddress(96), std::out_of_range);
  REQUIRE_THROWS_AS(spu.writeVoiceStartAddress(96, 0xffff), std::out_of_range);
}

TEST_CASE("SPU2 SSA excludes loop current fields widths and non-CPU transports",
  "[iop][spu2][spu2-ssa]")
{
  NekoSystem system;
  programVoiceStartAddresses(system);
  auto &bus = system.iopBus();
  IOPBus unattached;
  const auto before = system.iopStateHash();
  for (std::size_t index = 0; index < voiceStartAddressCount; ++index)
  {
    const auto address = voiceStartAddress(index);
    const auto wordAddress = address & ~IOPAddress(3);
    CAPTURE(address);
    REQUIRE(bus.read8(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read8(address + 1).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(address + 1, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read16(address + 1).status == IOPBusStatus::Misaligned);
    REQUIRE(bus.write16(address + 1, 0).status == IOPBusStatus::Misaligned);
    REQUIRE(bus.read32(wordAddress).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(wordAddress, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.readInstruction32(wordAddress).status == IOPBusStatus::Unmapped);
    for (std::uint8_t mask = 1; mask <= 0xf; ++mask)
      REQUIRE(bus.writeMasked32(wordAddress, 0, mask).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.readDMAC32(wordAddress).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.writeDMAC32(wordAddress, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.dmaWriteStatus(wordAddress) == IOPBusStatus::Unmapped);
    std::array<std::uint8_t, 2> bytes = {{0x12, 0x34}};
    REQUIRE(bus.loadPhysical(address, bytes.data(), bytes.size()) == IOPBusStatus::Unmapped);
    REQUIRE(bus.inspectPhysical(address, bytes.data(), bytes.size()) == IOPBusStatus::Unmapped);
    REQUIRE(bytes == (std::array<std::uint8_t, 2>{{0x12, 0x34}}));
    for (auto segment : {0x80000000u, 0xa0000000u})
    {
      REQUIRE(bus.read16(address | segment).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write16(address | segment, 0).status == IOPBusStatus::Unmapped);
    }
    REQUIRE(unattached.read16(address).status == IOPBusStatus::Unmapped);
    REQUIRE(unattached.write16(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read16(address + 0x800).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write16(address + 0x800, 0).status == IOPBusStatus::Unmapped);
    std::uint16_t value = 0x4321;
    REQUIRE_FALSE(system.eeBus().readData16(address, &value));
    REQUIRE(value == 0x4321);
    REQUIRE_FALSE(system.eeBus().writeData16(address, 0));
    if (index % 2 == 0)
    {
      for (IOPAddress offset : {4u, 6u, 8u, 10u})
      {
        REQUIRE(bus.read16(address + offset).status == IOPBusStatus::Unmapped);
        REQUIRE(bus.write16(address + offset, 0).status == IOPBusStatus::Unmapped);
      }
    }
    REQUIRE(bus.read16(address).value == voiceStartValue(index));
  }
  for (IOPAddress address : {0x1f9001beu, 0x1f9005beu,
      0x1f9002e0u, 0x1f9002e2u, 0x1f9006e0u, 0x1f9006e2u,
      0x1f801c00u, 0x1f801c06u})
  {
    REQUIRE(bus.read16(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Unmapped);
  }
  REQUIRE(system.iopStateHash() == before);
}

TEST_CASE("SPU2 SSA preserves PCM PIO programming and unstarted key-off lifecycle",
  "[iop][spu2][spu2-ssa][save-state]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  auto &spu = system.spu2Controller();
  programConfiguration(system);
  programMasterVolumes(system);
  programVoiceVolumes(system);
  programVoicePitches(system);
  programVoiceADSR(system);
  programVoiceModes(system);
  programVoiceMix(system);
  programMainMix(system);
  programEffectInputVolumes(system);
  programEffectEndAddresses(system);
  REQUIRE(bus.write16(transferStartAddressAddresses[1], 0x2800).status ==
    IOPBusStatus::Completed);
  REQUIRE(bus.write16(transferDataAddresses[0], 0x0707).status == IOPBusStatus::Completed);
  SPU2InputTest::programProfile(spu);
  for (std::size_t core = 0; core < 2; ++core)
  {
    REQUIRE(bus.write16(autoDMAAddresses[core], autoDMAWriteSelections[core]).status ==
      IOPBusStatus::Completed);
    REQUIRE(spu.writeInputWord(core, 0x12345678));
  }
  system.iopInterruptController().assertSource(IOPInterruptSource::SPU);
  system.iopDMAController().writeAuxiliaryControl(0x12345678);
  const auto before = system.saveState();
  programVoiceStartAddresses(system);
  for (const auto &addresses : {keyOffLowAddresses, keyOffHighAddresses})
  for (auto address : addresses)
    REQUIRE(bus.write16(address, 0xffff).status == IOPBusStatus::Completed);
  for (std::size_t index = 0; index < voiceStartAddressCount; ++index)
  {
    REQUIRE(bus.read16(voiceStartAddress(index)).value == voiceStartValue(index));
    REQUIRE(bus.write16(voiceStartAddress(index), 0).status == IOPBusStatus::Completed);
  }
  REQUIRE(system.saveState() == before);
  programVoiceStartAddresses(system);
  SPU2InputTest::advanceSamples(spu, 1);
  for (std::size_t core = 0; core < 2; ++core)
  {
    REQUIRE(bus.write16(autoDMAAddresses[core], 0).status == IOPBusStatus::Completed);
    REQUIRE(bus.write16(coreAttributeAddresses[core], 0x8010).status ==
      IOPBusStatus::Completed);
  }
  programVoiceVolumes(system);
  programVoicePitches(system);
  programVoiceADSR(system);
  REQUIRE(bus.write16(transferDataAddresses[0], 0xabcd).status == IOPBusStatus::Completed);
  REQUIRE(spu.readSoundRAMHalfword(0x2801) == 0xabcd);
  system.runMasterCycles(80);
  bus.reset();
  system.iopCore().reset();
  for (std::size_t index = 0; index < voiceStartAddressCount; ++index)
    REQUIRE(bus.read16(voiceStartAddress(index)).value == voiceStartValue(index));
  REQUIRE(system.iopInterruptController().status() == (1u << 9));
  REQUIRE(system.iopDMAController().auxiliaryControl() == 0x12345678);
  spu.reset();
  for (std::size_t index = 0; index < voiceStartAddressCount; ++index)
    REQUIRE(bus.read16(voiceStartAddress(index)).value == 0);
  programVoiceStartAddresses(system);
  system.reset();
  for (std::size_t index = 0; index < voiceStartAddressCount; ++index)
    REQUIRE(bus.read16(voiceStartAddress(index)).value == 0);
}

TEST_CASE("SPU2 SSA guests store raw values and load normalized delayed results",
  "[iop][spu2][spu2-ssa][trace]")
{
  for (auto index : {0u, 1u, 46u, 47u, 48u, 49u, 94u, 95u})
  for (auto segment : {0u, 0x80000000u, 0xa0000000u})
  for (std::uint16_t value : {0, 0xffff})
  {
    CAPTURE(index);
    CAPTURE(segment);
    CAPTURE(value);
    NekoSystem system;
    const auto address = voiceStartAddress(index) | segment;
    const IOPWord normalized = value & (index % 2 == 0 ? 0xf : 0xfff8);
    const IOPWord signedValue =
      (normalized & 0x8000) != 0 ? 0xffff0000u | normalized : normalized;
    installConfigurationProgram(system, address, value);
    system.startTrace();
    system.runMasterCycles(96);
    REQUIRE(system.iopCore().retiredInstructions() == 12);
    REQUIRE(system.iopCore().exception().kind == IOPException::None);
    REQUIRE(system.iopCore().generalRegister(3) == signedValue);
    REQUIRE(system.iopCore().generalRegister(4) == normalized);
    REQUIRE(system.iopCore().generalRegister(5) == 0x1234);
    REQUIRE(system.iopBus().read32(0x100).value == 0);
    REQUIRE(system.iopBus().read32(0x104).value == signedValue);
    REQUIRE(system.iopBus().read32(0x108).value == 0);
    REQUIRE(system.iopBus().read32(0x10c).value == normalized);
    unsigned accesses = 0;
    for (const auto &event : system.trace())
    {
      if (event.subsystem == NekoTraceSubsystem::IOP &&
          event.type == NekoTraceEventType::MemoryAccess && event.value0 == address)
      {
        REQUIRE(event.value1 == (accesses == 0 ? value : normalized));
        REQUIRE(event.masterCycle == (accesses == 0 ? 33 : accesses == 1 ? 41 : 65));
        ++accesses;
      }
    }
    REQUIRE(accesses == 3);
  }
}

TEST_CASE("SPU2 SSA restores pending guest stores and normalized loads",
  "[iop][spu2][spu2-ssa][save-state][trace]")
{
  for (auto index : {0u, 1u, 94u, 95u})
  for (unsigned cut : {32u, 48u, 72u})
  {
    NekoSystem original;
    programVoiceStartAddresses(original);
    installConfigurationProgram(original, voiceStartAddress(index) | 0xa0000000u, 0xffff);
    original.runMasterCycles(cut);
    const auto checkpoint = original.saveState();
    NekoSystem restored;
    NekoSystem plain;
    restored.loadState(checkpoint);
    plain.loadState(checkpoint);
    original.startTrace();
    restored.startTrace();
    original.runMasterCycles(96 - cut);
    restored.runMasterCycles(96 - cut);
    plain.runMasterCycles(96 - cut);
    REQUIRE(original.iopCore().retiredInstructions() == 12);
    REQUIRE(original.iopBus().read32(0x104).value ==
      (index % 2 == 0 ? 0xf : 0xfffffff8u));
    REQUIRE(original.iopBus().read32(0x10c).value == (index % 2 == 0 ? 0xf : 0xfff8));
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireMachine(plain, original);
  }
}

TEST_CASE("SPU2 SSA canonical state retains normalized images and rejects invalid pairs",
  "[iop][spu2][spu2-ssa][save-state]")
{
  NekoSystem source;
  auto previous = source.iopStateHash();
  for (std::size_t index = 0; index < voiceStartAddressCount; ++index)
  {
    REQUIRE(source.iopBus().write16(voiceStartAddress(index), voiceStartValue(index)).status ==
      IOPBusStatus::Completed);
    REQUIRE(source.iopStateHash() != previous);
    previous = source.iopStateHash();
  }
  const auto candidate = source.saveState();
  const auto fields = inspectSaveState(candidate);
  for (std::size_t index = 0; index < voiceStartAddressCount; ++index)
  {
    const auto *field = fields.find(
      "system.iop.spu2.voiceStartAddresses[" + std::to_string(index) + "].value");
    REQUIRE(field != nullptr);
    REQUIRE(field->kind == SaveStateFieldKind::U16);
    REQUIRE(field->size == 2);
    REQUIRE(field->scalarValue == voiceStartValue(index));
  }
  NekoSystem destination;
  SPU2Controller foreign;
  destination.iopBus().attachSPU2(&foreign);
  destination.loadState(candidate);
  REQUIRE(destination.iopStateHash() == previous);
  REQUIRE(destination.saveState() == candidate);
  REQUIRE(destination.iopBus().write16(0x1f9006d6, 0xffff).status == IOPBusStatus::Completed);
  REQUIRE(destination.iopBus().read16(0x1f9006d6).value == 0xfff8);
  REQUIRE(source.iopBus().read16(0x1f9006d6).value == voiceStartValue(95));
  IOPBus foreignBus;
  foreignBus.attachSPU2(&foreign);
  REQUIRE(foreignBus.read16(0x1f9006d6).value == 0);
  destination.startTrace();
  const auto before = destination.saveState();
  for (auto index : {0u, 1u, 94u, 95u})
  {
    SaveStateMutation invalid(candidate);
    invalid.writeScalar(
      "system.iop.spu2.voiceStartAddresses[" + std::to_string(index) + "].value",
      index % 2 == 0 ? 0x10 : 1);
    invalid.refreshChecksum();
    REQUIRE_THROWS_AS(destination.loadState(invalid), std::invalid_argument);
    REQUIRE(destination.saveState() == before);
  }
  SaveStateMutation badChecksum(candidate);
  badChecksum.writeScalar("system.iop.spu2.voiceStartAddresses[95].value", 0);
  REQUIRE_THROWS_AS(destination.loadState(badChecksum), std::invalid_argument);
  REQUIRE(destination.saveState() == before);
  SaveStateMutation lateInvalid(candidate);
  lateInvalid.writeScalar("system.iop.timing.pixelPhase", 0xffff);
  lateInvalid.refreshChecksum();
  REQUIRE_THROWS_AS(destination.loadState(lateInvalid), std::invalid_argument);
  REQUIRE(destination.saveState() == before);
  for (auto version : {113u, 114u})
  {
    SaveStateMutation historical(candidate);
    historical.writeScalar("container.version", version);
    historical.refreshChecksum();
    REQUIRE_THROWS_AS(destination.loadState(historical), std::invalid_argument);
    REQUIRE(destination.saveState() == before);
  }
  REQUIRE(destination.traceEnabled());
}

TEST_CASE("SPU2 ADSR controls retain independent full programmed halfwords",
  "[iop][spu2][spu2-adsr]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  REQUIRE(bus.write16(0x1f900406, 0).status == IOPBusStatus::Completed);
  for (std::size_t index = 0; index < voiceADSRCount; ++index)
  {
    REQUIRE(bus.read16(voiceADSRAddress(index)).status == IOPBusStatus::Completed);
    REQUIRE(bus.read16(voiceADSRAddress(index)).value == 0);
  }
  programVoiceVolumes(system);
  programVoicePitches(system);
  programVoiceADSR(system);
  for (std::size_t selected = 0; selected < voiceADSRCount; ++selected)
  {
    CAPTURE(selected);
    for (std::uint16_t value : {0, 1, 0x1fff, 0x2000, 0x4000, 0x7fff, 0x8000, 0xffff})
    {
      REQUIRE(bus.write16(voiceADSRAddress(selected), value).status ==
        IOPBusStatus::Completed);
      for (std::size_t index = 0; index < voiceADSRCount; ++index)
      {
        REQUIRE(bus.read16(voiceADSRAddress(index)).value ==
          (index == selected ? value : voiceADSRValue(index)));
      }
    }
    if (selected % 2 == 1)
    {
      for (unsigned mode = 0; mode < 8; ++mode)
      {
        const auto value = static_cast<std::uint16_t>((mode << 13) | 0x1357);
        REQUIRE(bus.write16(voiceADSRAddress(selected), value).status ==
          IOPBusStatus::Completed);
        REQUIRE(bus.read16(voiceADSRAddress(selected)).value == value);
      }
    }
    REQUIRE(bus.write16(voiceADSRAddress(selected), voiceADSRValue(selected)).status ==
      IOPBusStatus::Completed);
  }
  for (std::size_t index = 0; index < voiceVolumeCount; ++index)
    REQUIRE(bus.read16(voiceVolumeAddress(index)).value == voiceVolumeValue(index));
  for (std::size_t index = 0; index < voicePitchCount; ++index)
    REQUIRE(bus.read16(voicePitchAddress(index)).value == voicePitchValue(index));
}

TEST_CASE("SPU2 ADSR controls exclude current values widths and transports",
  "[iop][spu2][spu2-adsr]")
{
  NekoSystem system;
  programVoiceADSR(system);
  auto &bus = system.iopBus();
  IOPBus unattached;
  const auto before = system.iopStateHash();
  for (std::size_t index = 0; index < voiceADSRCount; ++index)
  {
    const auto address = voiceADSRAddress(index);
    const auto wordAddress = address & ~IOPAddress(3);
    CAPTURE(address);
    REQUIRE(bus.read8(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read8(address + 1).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(address + 1, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read16(address + 1).status == IOPBusStatus::Misaligned);
    REQUIRE(bus.write16(address + 1, 0).status == IOPBusStatus::Misaligned);
    REQUIRE(bus.read32(wordAddress).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(wordAddress, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.readInstruction32(wordAddress).status == IOPBusStatus::Unmapped);
    for (std::uint8_t mask = 1; mask <= 0xf; ++mask)
      REQUIRE(bus.writeMasked32(wordAddress, 0, mask).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.readDMAC32(wordAddress).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.writeDMAC32(wordAddress, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.dmaWriteStatus(wordAddress) == IOPBusStatus::Unmapped);
    std::array<std::uint8_t, 2> bytes = {{0x12, 0x34}};
    REQUIRE(bus.loadPhysical(address, bytes.data(), bytes.size()) == IOPBusStatus::Unmapped);
    REQUIRE(bus.inspectPhysical(address, bytes.data(), bytes.size()) == IOPBusStatus::Unmapped);
    REQUIRE(bytes == (std::array<std::uint8_t, 2>{{0x12, 0x34}}));
    for (auto segment : {0x80000000u, 0xa0000000u})
    {
      REQUIRE(bus.read16(address | segment).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write16(address | segment, 0).status == IOPBusStatus::Unmapped);
    }
    REQUIRE(unattached.read16(address).status == IOPBusStatus::Unmapped);
    REQUIRE(unattached.write16(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read16(address + 0x800).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write16(address + 0x800, 0).status == IOPBusStatus::Unmapped);
    std::uint16_t value = 0x4321;
    REQUIRE_FALSE(system.eeBus().readData16(address, &value));
    REQUIRE(value == 0x4321);
    REQUIRE_FALSE(system.eeBus().writeData16(address, 0));
    if (index % 2 == 1)
    {
      for (IOPAddress offset : {2u, 4u, 6u})
      {
        REQUIRE(bus.read16(address + offset).status == IOPBusStatus::Unmapped);
        REQUIRE(bus.write16(address + offset, 0).status == IOPBusStatus::Unmapped);
      }
    }
  }
  for (IOPAddress address : {0x1f9001b4u, 0x1f9005b4u,
      0x1f9001c4u, 0x1f9001c8u, 0x1f9005c4u, 0x1f9005c8u,
      0x1f801c06u, 0x1f801c08u})
  {
    REQUIRE(bus.read16(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Unmapped);
  }
  REQUIRE(system.iopStateHash() == before);
}

TEST_CASE("SPU2 ADSR programming preserves PCM PIO key-off and pending signals",
  "[iop][spu2][spu2-adsr][save-state]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  auto &spu = system.spu2Controller();
  programConfiguration(system);
  programMasterVolumes(system);
  programVoiceVolumes(system);
  programVoicePitches(system);
  programVoiceModes(system);
  programVoiceMix(system);
  programMainMix(system);
  programEffectInputVolumes(system);
  programEffectEndAddresses(system);
  REQUIRE(bus.write16(transferStartAddressAddresses[1], 0x2800).status ==
    IOPBusStatus::Completed);
  REQUIRE(bus.write16(transferDataAddresses[0], 0x0707).status == IOPBusStatus::Completed);
  SPU2InputTest::programProfile(spu);
  for (std::size_t core = 0; core < 2; ++core)
  {
    REQUIRE(bus.write16(autoDMAAddresses[core], autoDMAWriteSelections[core]).status ==
      IOPBusStatus::Completed);
    REQUIRE(spu.writeInputWord(core, 0x12345678));
  }
  system.iopInterruptController().assertSource(IOPInterruptSource::SPU);
  system.iopDMAController().writeAuxiliaryControl(0x12345678);
  const auto before = system.saveState();
  programVoiceADSR(system);
  for (const auto &addresses : {keyOffLowAddresses, keyOffHighAddresses})
  for (auto address : addresses)
    REQUIRE(bus.write16(address, 0xffff).status == IOPBusStatus::Completed);
  for (std::size_t index = 0; index < voiceADSRCount; ++index)
  {
    REQUIRE(bus.read16(voiceADSRAddress(index)).value == voiceADSRValue(index));
    REQUIRE(bus.write16(voiceADSRAddress(index), 0).status == IOPBusStatus::Completed);
  }
  REQUIRE(system.saveState() == before);
  programVoiceADSR(system);
  SPU2InputTest::advanceSamples(spu, 1);
  for (std::size_t core = 0; core < 2; ++core)
  {
    REQUIRE(bus.write16(autoDMAAddresses[core], 0).status == IOPBusStatus::Completed);
    REQUIRE(bus.write16(coreAttributeAddresses[core], 0x8010).status ==
      IOPBusStatus::Completed);
  }
  programVoiceVolumes(system);
  programVoicePitches(system);
  REQUIRE(bus.write16(transferDataAddresses[0], 0xabcd).status == IOPBusStatus::Completed);
  REQUIRE(spu.readSoundRAMHalfword(0x2801) == 0xabcd);
  system.runMasterCycles(80);
  bus.reset();
  system.iopCore().reset();
  for (std::size_t index = 0; index < voiceADSRCount; ++index)
    REQUIRE(bus.read16(voiceADSRAddress(index)).value == voiceADSRValue(index));
  REQUIRE(system.iopInterruptController().status() == (1u << 9));
  REQUIRE(system.iopDMAController().auxiliaryControl() == 0x12345678);
  spu.reset();
  for (std::size_t index = 0; index < voiceADSRCount; ++index)
    REQUIRE(bus.read16(voiceADSRAddress(index)).value == 0);
  programVoiceADSR(system);
  system.reset();
  for (std::size_t index = 0; index < voiceADSRCount; ++index)
    REQUIRE(bus.read16(voiceADSRAddress(index)).value == 0);
}

TEST_CASE("SPU2 ADSR guests retire zero stores and preserve delayed signed loads",
  "[iop][spu2][spu2-adsr][trace]")
{
  NekoSystem origin;
  REQUIRE(origin.iopBus().write32(0, 0x3c05bf90).status == IOPBusStatus::Completed);
  REQUIRE(origin.iopBus().write32(4, 0x34a50406).status == IOPBusStatus::Completed);
  REQUIRE(origin.iopBus().write32(8, 0xa4a00000).status == IOPBusStatus::Completed);
  origin.iopCore().startExecution(0xa0000000);
  origin.runMasterCycles(24);
  REQUIRE(origin.iopCore().retiredInstructions() == 3);
  REQUIRE(origin.iopCore().exception().kind == IOPException::None);
  REQUIRE(origin.iopBus().read16(0x1f900406).value == 0);
  for (auto index : {0u, 1u, 46u, 47u, 48u, 49u, 94u, 95u})
  for (auto segment : {0u, 0x80000000u, 0xa0000000u})
  for (std::uint16_t value : {0, 0x2000, 0x8000, 0xffff})
  {
    CAPTURE(index);
    CAPTURE(segment);
    CAPTURE(value);
    NekoSystem system;
    const auto address = voiceADSRAddress(index) | segment;
    const IOPWord signedValue = (value & 0x8000) != 0 ? 0xffff0000u | value : value;
    installConfigurationProgram(system, address, value);
    system.startTrace();
    system.runMasterCycles(96);
    REQUIRE(system.iopCore().retiredInstructions() == 12);
    REQUIRE(system.iopCore().exception().kind == IOPException::None);
    REQUIRE(system.iopCore().generalRegister(3) == signedValue);
    REQUIRE(system.iopCore().generalRegister(4) == value);
    REQUIRE(system.iopCore().generalRegister(5) == 0x1234);
    REQUIRE(system.iopBus().read32(0x100).value == 0);
    REQUIRE(system.iopBus().read32(0x104).value == signedValue);
    REQUIRE(system.iopBus().read32(0x108).value == 0);
    REQUIRE(system.iopBus().read32(0x10c).value == value);
    unsigned accesses = 0;
    for (const auto &event : system.trace())
    {
      if (event.subsystem == NekoTraceSubsystem::IOP &&
          event.type == NekoTraceEventType::MemoryAccess && event.value0 == address)
      {
        REQUIRE(event.value1 == value);
        REQUIRE(event.masterCycle == (accesses == 0 ? 33 : accesses == 1 ? 41 : 65));
        ++accesses;
      }
    }
    REQUIRE(accesses == 3);
  }
}

TEST_CASE("SPU2 ADSR controls restore pending guest stores and loads",
  "[iop][spu2][spu2-adsr][save-state][trace]")
{
  for (auto index : {0u, 1u, 94u, 95u})
  for (unsigned cut : {32u, 48u, 72u})
  {
    NekoSystem original;
    programVoiceADSR(original);
    installConfigurationProgram(original, voiceADSRAddress(index) | 0xa0000000u, 0xffff);
    original.runMasterCycles(cut);
    const auto checkpoint = original.saveState();
    NekoSystem restored;
    NekoSystem plain;
    restored.loadState(checkpoint);
    plain.loadState(checkpoint);
    original.startTrace();
    restored.startTrace();
    original.runMasterCycles(96 - cut);
    restored.runMasterCycles(96 - cut);
    plain.runMasterCycles(96 - cut);
    REQUIRE(original.iopCore().retiredInstructions() == 12);
    REQUIRE(original.iopBus().read32(0x104).value == 0xffffffff);
    REQUIRE(original.iopBus().read32(0x10c).value == 0xffff);
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireMachine(plain, original);
  }
}

TEST_CASE("SPU2 ADSR canonical state retains raw modes and rejects corrupt states",
  "[iop][spu2][spu2-adsr][save-state]")
{
  NekoSystem source;
  auto previous = source.iopStateHash();
  for (std::size_t index = 0; index < voiceADSRCount; ++index)
  {
    REQUIRE(source.iopBus().write16(voiceADSRAddress(index), voiceADSRValue(index)).status ==
      IOPBusStatus::Completed);
    REQUIRE(source.iopStateHash() != previous);
    previous = source.iopStateHash();
  }
  const auto candidate = source.saveState();
  const auto fields = inspectSaveState(candidate);
  for (std::size_t index = 0; index < voiceADSRCount; ++index)
  {
    const auto *field = fields.find(
      "system.iop.spu2.voiceADSRControls[" + std::to_string(index) + "].value");
    REQUIRE(field != nullptr);
    REQUIRE(field->kind == SaveStateFieldKind::U16);
    REQUIRE(field->size == 2);
    REQUIRE(field->scalarValue == voiceADSRValue(index));
  }
  NekoSystem destination;
  SPU2Controller foreign;
  destination.iopBus().attachSPU2(&foreign);
  destination.loadState(candidate);
  REQUIRE(destination.iopStateHash() == previous);
  REQUIRE(destination.saveState() == candidate);
  REQUIRE(destination.iopBus().write16(0x1f900578, 0xffff).status == IOPBusStatus::Completed);
  REQUIRE(source.iopBus().read16(0x1f900578).value == voiceADSRValue(95));
  IOPBus foreignBus;
  foreignBus.attachSPU2(&foreign);
  REQUIRE(foreignBus.read16(0x1f900578).value == 0);
  SaveStateMutation rawModes(candidate);
  rawModes.writeScalar("system.iop.spu2.voiceADSRControls[1].value", 0x2000);
  rawModes.writeScalar("system.iop.spu2.voiceADSRControls[95].value", 0xffff);
  rawModes.refreshChecksum();
  destination.loadState(rawModes);
  REQUIRE(destination.iopBus().read16(0x1f900008).value == 0x2000);
  REQUIRE(destination.iopBus().read16(0x1f900578).value == 0xffff);
  destination.startTrace();
  const auto before = destination.saveState();
  SaveStateMutation badChecksum(candidate);
  badChecksum.writeScalar("system.iop.spu2.voiceADSRControls[95].value", 0);
  REQUIRE_THROWS_AS(destination.loadState(badChecksum), std::invalid_argument);
  REQUIRE(destination.saveState() == before);
  SaveStateMutation lateInvalid(candidate);
  lateInvalid.writeScalar("system.iop.timing.pixelPhase", 0xffff);
  lateInvalid.refreshChecksum();
  REQUIRE_THROWS_AS(destination.loadState(lateInvalid), std::invalid_argument);
  REQUIRE(destination.saveState() == before);
  for (auto version : {111u, 112u, 113u})
  {
    SaveStateMutation historical(candidate);
    historical.writeScalar("container.version", version);
    historical.refreshChecksum();
    REQUIRE_THROWS_AS(destination.loadState(historical), std::invalid_argument);
    REQUIRE(destination.saveState() == before);
  }
  REQUIRE(destination.traceEnabled());
}

TEST_CASE("SPU2 configuration excludes neighbors widths and non-CPU transports",
  "[iop][spu2][spu2-mode]")
{
  NekoSystem system;
  programConfiguration(system);
  programMasterVolumes(system);
  programVoiceMix(system);
  programMainMix(system);
  programEffectInputVolumes(system);
  programEffectEndAddresses(system);
  programTransferStartAddresses(system);
  programVoiceModes(system);
  auto &bus = system.iopBus();
  const auto before = system.iopStateHash();
  for (const auto &programming : halfwordProgramming)
  {
    const auto address = programming.address;
    CAPTURE(address);
    REQUIRE(bus.read8(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read8(address + 1).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(address, 0xff).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(address + 1, 0xff).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read16(address + 1).status == IOPBusStatus::Misaligned);
    REQUIRE(bus.write16(address + 1, 0).status == IOPBusStatus::Misaligned);
    REQUIRE(bus.read32(address + 1).status == IOPBusStatus::Misaligned);
    REQUIRE(bus.write32(address + 1, 0).status == IOPBusStatus::Misaligned);
    const auto wordAddress = address & ~IOPAddress(3);
    REQUIRE(bus.read32(wordAddress).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(wordAddress, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.readInstruction32(wordAddress).status == IOPBusStatus::Unmapped);
    for (std::uint8_t mask = 1; mask <= 0xf; ++mask)
    {
      REQUIRE(bus.writeMasked32(wordAddress, 0, mask).status ==
        IOPBusStatus::Unmapped);
    }
    REQUIRE(bus.readDMAC32(wordAddress).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.writeDMAC32(wordAddress, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.dmaWriteStatus(wordAddress) == IOPBusStatus::Unmapped);
    std::array<std::uint8_t, 2> bytes = {{0x12, 0x34}};
    REQUIRE(bus.loadPhysical(address, bytes.data(), bytes.size()) ==
      IOPBusStatus::Unmapped);
    REQUIRE(bus.inspectPhysical(address, bytes.data(), bytes.size()) ==
      IOPBusStatus::Unmapped);
    REQUIRE(bytes == (std::array<std::uint8_t, 2>{{0x12, 0x34}}));
    for (auto segment : {0x80000000u, 0xa0000000u})
    {
      REQUIRE(bus.read16(address | segment).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write16(address | segment, 0).status == IOPBusStatus::Unmapped);
    }
    std::uint16_t eeValue = 0x4321;
    REQUIRE_FALSE(system.eeBus().readData16(address, &eeValue));
    REQUIRE(eeValue == 0x4321);
    REQUIRE_FALSE(system.eeBus().writeData16(address, 0));
    IOPBus unattached;
    REQUIRE(unattached.read16(address).status == IOPBusStatus::Unmapped);
    REQUIRE(unattached.write16(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read16(address + 0x800).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write16(address + 0x800, 0).status == IOPBusStatus::Unmapped);
  }
  for (IOPAddress address : {0x1f9007c2u, 0x1f9007c4u,
      0x1f9003c0u, 0x1f900bc0u, 0x1f900fc0u,
      0x1f9007ceu, 0x1f9007d0u, 0x1f9003c6u, 0x1f90075eu,
      0x1f900770u, 0x1f900772u, 0x1f900774u, 0x1f900786u,
      0x1f900798u, 0x1f90079au, 0x1f90079cu,
      0x1f90033au, 0x1f90033eu, 0x1f900340u, 0x1f900342u,
      0x1f90073au, 0x1f90073eu, 0x1f900740u, 0x1f900742u,
      0x1f9002e0u, 0x1f9002e2u, 0x1f9006e0u, 0x1f9006e2u,
      0x1f90017eu, 0x1f90057eu,
      0x1f90019cu, 0x1f90019eu, 0x1f9001a0u, 0x1f9001a2u,
      0x1f90059cu, 0x1f90059eu, 0x1f9005a0u, 0x1f9005a2u,
      0x1f9001aeu, 0x1f9005aeu})
  {
    REQUIRE(bus.read16(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write16(address, 0xffff).status == IOPBusStatus::Unmapped);
  }
  REQUIRE(system.iopStateHash() == before);
}

TEST_CASE("SPU2 configuration survives time and local resets but not owner reset",
  "[iop][spu2]")
{
  NekoSystem system;
  programConfiguration(system);
  programMasterVolumes(system);
  system.runMasterCycles(80);
  system.iopBus().reset();
  system.iopCore().reset();
  for (std::size_t index = 0; index < configurationAddresses.size(); ++index)
  {
    REQUIRE(system.iopBus().read16(configurationAddresses[index]).value ==
      configurationValues[index]);
  }
  for (std::size_t index = 0; index < masterVolumeAddresses.size(); ++index)
  {
    REQUIRE(system.iopBus().read16(masterVolumeAddresses[index]).value ==
      masterVolumeValues[index]);
  }
  system.spu2Controller().reset();
  for (auto address : configurationAddresses)
  {
    REQUIRE(system.iopBus().read16(address).value == 0);
  }
  for (auto address : masterVolumeAddresses)
  {
    REQUIRE(system.iopBus().read16(address).value == 0);
  }
  programConfiguration(system);
  programMasterVolumes(system);
  system.reset();
  for (auto address : configurationAddresses)
  {
    REQUIRE(system.iopBus().read16(address).value == 0);
    REQUIRE(system.iopBus().write16(address, 0xffff).status ==
      IOPBusStatus::Completed);
  }
  for (auto address : masterVolumeAddresses)
  {
    REQUIRE(system.iopBus().read16(address).value == 0);
    REQUIRE(system.iopBus().write16(address, 0x7fff).status ==
      IOPBusStatus::Completed);
  }
}

TEST_CASE("SPU2 sweep guest stores stop before changing fixed controls",
  "[iop][spu2][execution]")
{
  for (auto physical : masterVolumeAddresses)
  {
    CAPTURE(physical);
    NekoSystem system;
    programConfiguration(system);
    programMasterVolumes(system);
    installConfigurationProgram(system, physical | 0xa0000000u, 0x8000);
    system.runMasterCycles(40);
    REQUIRE(system.iopCore().executionState() == IOPExecutionState::Halted);
    REQUIRE(system.iopCore().stopReason() == IOPStopReason::StoreFailure);
    REQUIRE(system.iopCore().programCounter() == 0xa0000010);
    REQUIRE(system.iopCore().retiredInstructions() == 4);
    REQUIRE(system.iopCore().exception().kind == IOPException::None);
    REQUIRE(system.iopCore().storeFailure().value == 0x8000);
    for (std::size_t index = 0; index < masterVolumeAddresses.size(); ++index)
    {
      REQUIRE(system.iopBus().read16(masterVolumeAddresses[index]).value ==
        masterVolumeValues[index]);
      REQUIRE(system.iopBus().read16(configurationAddresses[index]).value ==
        configurationValues[index]);
    }
    REQUIRE(system.iopInterruptController().status() == 0);
  }
}

TEST_CASE("SPU2 halfword guests preserve aliases delay slots and signed load delays",
  "[iop][spu2][spu2-mode][trace]")
{
  for (const auto &programming : halfwordProgramming)
  for (auto segment : {0u, 0x80000000u, 0xa0000000u})
  {
    const auto physical = programming.address;
    const auto address = physical | segment;
    CAPTURE(address);
    NekoSystem system;
    programConfiguration(system);
    programMasterVolumes(system);
    programVoiceMix(system);
    programMainMix(system);
    programEffectInputVolumes(system);
    programEffectEndAddresses(system);
    programTransferStartAddresses(system);
    programVoiceModes(system);
    installConfigurationProgram(system, address, programming.value);
    system.startTrace();
    system.runMasterCycles(96);
    requireConfigurationResult(system, physical, programming.signedLoad);
    IOPTraceTest::Events actual;
    unsigned delayStores = 0;
    for (const auto &event : system.trace())
    {
      if (event.subsystem != NekoTraceSubsystem::IOP)
      {
        continue;
      }
      if (event.type == NekoTraceEventType::MemoryAccess &&
          event.value0 == address)
      {
        actual.push_back(event);
      }
      if (event.type == NekoTraceEventType::InstructionIssued &&
          event.value0 == 0xa0000010)
      {
        REQUIRE(event.masterCycle == 33);
        REQUIRE(event.value1 == 0xa4220000);
        REQUIRE(event.value3 == NekoIOPTraceInstruction::DELAY_SLOT);
        ++delayStores;
      }
    }
    REQUIRE(delayStores == 1);
    namespace Memory = NekoIOPTraceMemory;
    const auto route = segment == 0xa0000000
      ? Memory::CacheRoute::Uncached : Memory::CacheRoute::Cached;
    const auto store = nekoPackIOPMemoryTraceMetadata({
      physical, 2, Memory::AccessKind::DataStore,
      Memory::Outcome::Completed, route, true,
      static_cast<std::uint8_t>((physical & 2) != 0 ? 0xc : 3)});
    const auto load = nekoPackIOPMemoryTraceMetadata({
      physical, 2, Memory::AccessKind::DataLoad,
      Memory::Outcome::Completed, route, true, 0});
    IOPTraceTest::requireEvents(actual, {
      {33, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, programming.value, 0, store},
      {41, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, programming.value, 0, load},
      {65, NekoTraceSubsystem::IOP, NekoTraceEventType::MemoryAccess,
        address, programming.value, 0, load}
    });
  }
}

TEST_CASE("SPU2 pending stores and loads restore traced and plain canonically",
  "[iop][spu2][spu2-mode][save-state][trace]")
{
  for (std::size_t selected : {0u, 3u, 4u, 5u, 8u, 9u, 10u, 23u, 24u, 25u, 26u,
      27u, 28u, 29u, 30u, 31u, 32u, 33u, 34u, 35u, 36u, 37u, 38u, 39u, 40u,
      41u, 42u, 43u, 44u, 45u, 46u, 47u, 48u, 49u, 50u, 51u, 52u})
  for (unsigned cut : {32u, 48u, 72u})
  {
    const auto &programming = halfwordProgramming[selected];
    CAPTURE(selected);
    CAPTURE(cut);
    NekoSystem original;
    programConfiguration(original);
    programMasterVolumes(original);
    programVoiceMix(original);
    programMainMix(original);
    programEffectInputVolumes(original);
    programEffectEndAddresses(original);
    programTransferStartAddresses(original);
    programVoiceModes(original);
    const auto physical = programming.address;
    CAPTURE(physical);
    installConfigurationProgram(original,
      physical | 0xa0000000u, programming.value);
    original.runMasterCycles(cut);
    REQUIRE(original.iopCore().branchContinuation().active == (cut == 32));
    REQUIRE((original.iopCore().delayedResultContinuation().source ==
      IOPDelayedResultSource::Load) == (cut != 32));
    const auto checkpoint = original.saveState();
    NekoSystem restored;
    NekoSystem plain;
    restored.loadState(checkpoint);
    plain.loadState(checkpoint);
    original.startTrace();
    restored.startTrace();
    original.runMasterCycles(96 - cut);
    restored.runMasterCycles(96 - cut);
    plain.runMasterCycles(96 - cut);
    requireConfigurationResult(original, physical, programming.signedLoad);
    requireConfigurationResult(restored, physical, programming.signedLoad);
    requireConfigurationResult(plain, physical, programming.signedLoad);
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireMachine(plain, original);
  }
}

TEST_CASE("SPU2 canonical state hashes every configuration word and restores local wiring",
  "[iop][spu2][spu2-mode][save-state]")
{
  NekoSystem source;
  const auto cold = source.saveState();
  auto previousHash = source.iopStateHash();
  for (std::size_t index = 0; index < configurationAddresses.size(); ++index)
  {
    REQUIRE(source.iopBus().write16(configurationAddresses[index],
      configurationValues[index]).status == IOPBusStatus::Completed);
    REQUIRE(source.iopStateHash() != previousHash);
    previousHash = source.iopStateHash();
  }
  for (const auto address : coreAttributeAddresses)
  for (std::uint16_t value : {
      1, 0x4000, 0x4001, 0x8000, 0x8001, 0xc000, 0xc001})
  {
    REQUIRE(source.iopBus().write16(address, value).status ==
      IOPBusStatus::Completed);
    REQUIRE(source.iopStateHash() != previousHash);
    previousHash = source.iopStateHash();
  }
  for (std::size_t index = 0; index < masterVolumeAddresses.size(); ++index)
  {
    REQUIRE(source.iopBus().write16(masterVolumeAddresses[index],
      masterVolumeValues[index]).status == IOPBusStatus::Completed);
    REQUIRE(source.iopStateHash() != previousHash);
    previousHash = source.iopStateHash();
  }
  for (std::size_t index = 0; index < voiceMixAddresses.size(); ++index)
  {
    REQUIRE(source.iopBus().write16(
      voiceMixAddresses[index], voiceMixValues[index]).status ==
      IOPBusStatus::Completed);
    REQUIRE(source.iopStateHash() != previousHash);
    previousHash = source.iopStateHash();
  }
  for (std::size_t core = 0; core < mainMixAddresses.size(); ++core)
  {
    REQUIRE(source.iopBus().write16(
      mainMixAddresses[core], mainMixValues[core]).status ==
      IOPBusStatus::Completed);
    REQUIRE(source.iopStateHash() != previousHash);
    previousHash = source.iopStateHash();
  }
  for (std::size_t index = 0; index < effectInputVolumeAddresses.size(); ++index)
  {
    REQUIRE(source.iopBus().write16(
      effectInputVolumeAddresses[index], effectInputVolumeValues[index]).status ==
      IOPBusStatus::Completed);
    REQUIRE(source.iopStateHash() != previousHash);
    previousHash = source.iopStateHash();
  }
  for (std::size_t core = 0; core < effectEndAddressHighAddresses.size(); ++core)
  {
    REQUIRE(source.iopBus().write16(
      effectEndAddressHighAddresses[core], effectEndAddressHighValues[core]).status ==
      IOPBusStatus::Completed);
    REQUIRE(source.iopStateHash() != previousHash);
    previousHash = source.iopStateHash();
  }
  for (std::size_t index = 0; index < transferStartAddressAddresses.size(); ++index)
  {
    REQUIRE(source.iopBus().write16(
      transferStartAddressAddresses[index], transferStartAddressValues[index]).status ==
      IOPBusStatus::Completed);
    REQUIRE(source.iopStateHash() != previousHash);
    previousHash = source.iopStateHash();
  }
  for (std::size_t index = 0; index < voiceModeAddresses.size(); ++index)
  {
    REQUIRE(source.iopBus().write16(
      voiceModeAddresses[index], voiceModeValues[index]).status ==
      IOPBusStatus::Completed);
    REQUIRE(source.iopStateHash() != previousHash);
    previousHash = source.iopStateHash();
  }
  SPU2InputTest::programProfile(source.spu2Controller());
  previousHash = source.iopStateHash();
  for (std::size_t core = 0; core < autoDMAAddresses.size(); ++core)
  {
    REQUIRE(source.iopBus().write16(
      autoDMAAddresses[core], autoDMAWriteSelections[core]).status ==
      IOPBusStatus::Completed);
    REQUIRE(source.iopStateHash() != previousHash);
    previousHash = source.iopStateHash();
  }
  const auto programmed = source.saveState();
  const auto fields = inspectSaveState(programmed);
  for (std::size_t index = 0; index < configurationAddresses.size(); ++index)
  {
    const auto *field = fields.find(
      "system.iop.spu2.spdifConfiguration[" + std::to_string(index) + "].value");
    REQUIRE(field != nullptr);
    REQUIRE(field->kind == SaveStateFieldKind::U16);
    REQUIRE(field->size == 2);
    REQUIRE(field->scalarValue == SPU2InputTest::configuration[index]);
  }
  for (std::size_t index = 0; index < masterVolumeAddresses.size(); ++index)
  {
    const auto *master = fields.find(
      "system.iop.spu2.masterVolumeControls[" + std::to_string(index) + "].value");
    REQUIRE(master != nullptr);
    REQUIRE(master->kind == SaveStateFieldKind::U16);
    REQUIRE(master->size == 2);
    REQUIRE(master->scalarValue == masterVolumeValues[index]);
  }
  NekoSystem destination;
  for (std::size_t core = 0; core < coreAttributeAddresses.size(); ++core)
  {
    const auto *attribute = fields.find(
      "system.iop.spu2.coreAttributes[" + std::to_string(core) + "].value");
    REQUIRE(attribute != nullptr);
    REQUIRE(attribute->kind == SaveStateFieldKind::U16);
    REQUIRE(attribute->size == 2);
    REQUIRE(attribute->scalarValue == 0xc001);
  }
  destination.loadState(programmed);
  REQUIRE(destination.iopStateHash() == previousHash);
  REQUIRE(destination.saveState() == programmed);
  source.spu2Controller().reset();
  for (std::size_t index = 0; index < voiceModeAddresses.size(); ++index)
  {
    const auto *field = fields.find(
      "system.iop.spu2.voiceModeMasks[" + std::to_string(index) + "].value");
    REQUIRE(field != nullptr);
    REQUIRE(field->kind == SaveStateFieldKind::U16);
    REQUIRE(field->size == 2);
    REQUIRE(field->scalarValue == voiceModeValues[index]);
    REQUIRE(destination.iopBus().read16(voiceModeAddresses[index]).value ==
      voiceModeValues[index]);
    REQUIRE(destination.iopBus().write16(voiceModeAddresses[index], 0xffff).status ==
      IOPBusStatus::Completed);
    REQUIRE(source.iopBus().read16(voiceModeAddresses[index]).value == 0);
  }
  for (std::size_t core = 0; core < autoDMAAddresses.size(); ++core)
  {
    const auto *field = fields.find(
      "system.iop.spu2.autoDMAWriteSelection[" + std::to_string(core) + "].value");
    REQUIRE(field != nullptr);
    REQUIRE(field->kind == SaveStateFieldKind::U16);
    REQUIRE(field->size == 2);
    REQUIRE(field->scalarValue == autoDMAWriteSelections[core]);
    REQUIRE(destination.spu2Controller().readAutoDMAWriteSelection(core) ==
      autoDMAWriteSelections[core]);
    REQUIRE(destination.iopBus().read16(autoDMAAddresses[core]).status ==
      IOPBusStatus::Completed);
    REQUIRE(destination.iopBus().read16(autoDMAAddresses[core]).value ==
      autoDMAWriteSelections[core]);
    REQUIRE(destination.iopBus().write16(autoDMAAddresses[core], 0).status ==
      IOPBusStatus::Completed);
    REQUIRE(source.spu2Controller().readAutoDMAWriteSelection(core) == 0);
  }
  for (std::size_t index = 0; index < transferStartAddressAddresses.size(); ++index)
  {
    const auto *field = fields.find(
      "system.iop.spu2.transferStartAddress[" + std::to_string(index) + "].value");
    REQUIRE(field != nullptr);
    REQUIRE(field->kind == SaveStateFieldKind::U16);
    REQUIRE(field->size == 2);
    REQUIRE(field->scalarValue == transferStartAddressValues[index]);
    REQUIRE(destination.iopBus().read16(transferStartAddressAddresses[index]).value ==
      transferStartAddressValues[index]);
    REQUIRE(destination.spu2Controller().readTransferStartAddress(index) ==
      transferStartAddressValues[index]);
    REQUIRE(destination.iopBus().write16(transferStartAddressAddresses[index], 0xffff).status ==
      IOPBusStatus::Completed);
    REQUIRE(source.iopBus().read16(transferStartAddressAddresses[index]).value == 0);
  }
  for (std::size_t core = 0; core < effectEndAddressHighAddresses.size(); ++core)
  {
    const auto *field = fields.find(
      "system.iop.spu2.effectEndAddressHigh[" + std::to_string(core) + "].value");
    REQUIRE(field != nullptr);
    REQUIRE(field->kind == SaveStateFieldKind::U16);
    REQUIRE(field->size == 2);
    REQUIRE(field->scalarValue == effectEndAddressHighValues[core]);
    REQUIRE(destination.iopBus().read16(effectEndAddressHighAddresses[core]).value ==
      effectEndAddressHighValues[core]);
    REQUIRE(destination.spu2Controller().readEffectEndAddressHigh(core) ==
      effectEndAddressHighValues[core]);
    REQUIRE(destination.iopBus().write16(effectEndAddressHighAddresses[core], 0xffff).status ==
      IOPBusStatus::Completed);
    REQUIRE(source.iopBus().read16(effectEndAddressHighAddresses[core]).value == 0);
  }
  for (std::size_t index = 0; index < effectInputVolumeAddresses.size(); ++index)
  {
    const auto *field = fields.find(
      "system.iop.spu2.effectInputVolumes[" + std::to_string(index) + "].value");
    REQUIRE(field != nullptr);
    REQUIRE(field->kind == SaveStateFieldKind::U16);
    REQUIRE(field->size == 2);
    REQUIRE(field->scalarValue == effectInputVolumeValues[index]);
    REQUIRE(destination.iopBus().read16(effectInputVolumeAddresses[index]).value ==
      effectInputVolumeValues[index]);
    REQUIRE(destination.spu2Controller().readEffectInputVolume(index) ==
      effectInputVolumeValues[index]);
    REQUIRE(destination.iopBus().write16(effectInputVolumeAddresses[index], 0xffff).status ==
      IOPBusStatus::Completed);
    REQUIRE(source.iopBus().read16(effectInputVolumeAddresses[index]).value == 0);
  }
  for (std::size_t core = 0; core < mainMixAddresses.size(); ++core)
  {
    const auto *field = fields.find(
      "system.iop.spu2.mainMix[" + std::to_string(core) + "].value");
    REQUIRE(field != nullptr);
    REQUIRE(field->kind == SaveStateFieldKind::U16);
    REQUIRE(field->size == 2);
    REQUIRE(field->scalarValue == mainMixValues[core]);
    REQUIRE(destination.iopBus().read16(mainMixAddresses[core]).value ==
      mainMixValues[core]);
    REQUIRE(destination.spu2Controller().readMainMix(core) == mainMixValues[core]);
    REQUIRE(destination.iopBus().write16(mainMixAddresses[core], 0xffff).status ==
      IOPBusStatus::Completed);
    REQUIRE(source.iopBus().read16(mainMixAddresses[core]).value == 0);
  }
  for (std::size_t index = 0; index < voiceMixAddresses.size(); ++index)
  {
    const auto *field = fields.find(
      "system.iop.spu2.voiceMix[" + std::to_string(index) + "].value");
    REQUIRE(field != nullptr);
    REQUIRE(field->kind == SaveStateFieldKind::U16);
    REQUIRE(field->size == 2);
    REQUIRE(field->scalarValue == voiceMixValues[index]);
    REQUIRE(destination.iopBus().read16(voiceMixAddresses[index]).value ==
      voiceMixValues[index]);
    REQUIRE(destination.spu2Controller().readVoiceMix(index) ==
      voiceMixValues[index]);
    REQUIRE(destination.iopBus().write16(voiceMixAddresses[index], 0xffff).status ==
      IOPBusStatus::Completed);
    REQUIRE(source.iopBus().read16(voiceMixAddresses[index]).value == 0);
  }
  for (std::size_t core = 0; core < coreAttributeAddresses.size(); ++core)
  {
    REQUIRE(destination.iopBus().read16(coreAttributeAddresses[core]).value ==
      0xc001);
    REQUIRE(destination.spu2Controller().readCoreAttribute(core) == 0xc001);
    REQUIRE(destination.iopBus().write16(coreAttributeAddresses[core], 0).status ==
      IOPBusStatus::Completed);
    REQUIRE(source.iopBus().read16(coreAttributeAddresses[core]).value == 0);
  }
  for (std::size_t index = 0; index < configurationAddresses.size(); ++index)
  {
    REQUIRE(destination.iopBus().read16(configurationAddresses[index]).value ==
      SPU2InputTest::configuration[index]);
    REQUIRE(destination.spu2Controller().readSPDIFConfiguration(index) ==
      SPU2InputTest::configuration[index]);
    REQUIRE(destination.iopBus().write16(configurationAddresses[index], 0xffff).status ==
      IOPBusStatus::Completed);
    REQUIRE(source.iopBus().read16(configurationAddresses[index]).value == 0);
  }
  for (std::size_t index = 0; index < masterVolumeAddresses.size(); ++index)
  {
    REQUIRE(destination.iopBus().read16(masterVolumeAddresses[index]).value ==
      masterVolumeValues[index]);
    REQUIRE(destination.spu2Controller().readMasterVolumeControl(index) ==
      masterVolumeValues[index]);
    REQUIRE(destination.iopBus().write16(masterVolumeAddresses[index], 0x7fff).status ==
      IOPBusStatus::Completed);
    REQUIRE(source.iopBus().read16(masterVolumeAddresses[index]).value == 0);
  }
  destination.loadState(cold);
  REQUIRE(destination.saveState() == cold);
  SPU2Controller foreign;
  destination.iopBus().attachSPU2(&foreign);
  REQUIRE_THROWS_AS(destination.saveState(), std::logic_error);
  REQUIRE_THROWS_AS(destination.iopStateHash(), std::logic_error);
  destination.loadState(programmed);
  REQUIRE(destination.iopBus().write16(autoDMAAddresses[1], 0).status ==
    IOPBusStatus::Completed);
  REQUIRE(destination.spu2Controller().readAutoDMAWriteSelection(1) == 0);
  REQUIRE(destination.spu2Controller().readAutoDMAWriteSelection(0) == 1);
  REQUIRE(foreign.readAutoDMAWriteSelection(1) == 0);
  REQUIRE(destination.iopBus().write16(autoDMAAddresses[0], 0).status ==
    IOPBusStatus::Completed);
  REQUIRE(destination.iopBus().write16(transferStartAddressAddresses[3], 0xffff).status ==
    IOPBusStatus::Completed);
  REQUIRE(destination.spu2Controller().readTransferStartAddress(3) == 0xffff);
  REQUIRE(destination.spu2Controller().readTransferStartAddress(0) ==
    transferStartAddressValues[0]);
  REQUIRE(foreign.readTransferStartAddress(3) == 0);
  REQUIRE(destination.iopBus().write16(effectEndAddressHighAddresses[1], 0xffff).status ==
    IOPBusStatus::Completed);
  REQUIRE(destination.spu2Controller().readEffectEndAddressHigh(1) == 0xffff);
  REQUIRE(destination.spu2Controller().readEffectEndAddressHigh(0) ==
    effectEndAddressHighValues[0]);
  REQUIRE(foreign.readEffectEndAddressHigh(1) == 0);
  REQUIRE(destination.iopBus().write16(effectInputVolumeAddresses[11], 0xffff).status ==
    IOPBusStatus::Completed);
  REQUIRE(destination.spu2Controller().readEffectInputVolume(11) == 0xffff);
  REQUIRE(destination.spu2Controller().readEffectInputVolume(0) == effectInputVolumeValues[0]);
  REQUIRE(foreign.readEffectInputVolume(11) == 0);
  REQUIRE(destination.iopBus().write16(mainMixAddresses[1], 0xffff).status ==
    IOPBusStatus::Completed);
  REQUIRE(destination.spu2Controller().readMainMix(1) == 0xffff);
  REQUIRE(destination.spu2Controller().readMainMix(0) == mainMixValues[0]);
  REQUIRE(foreign.readMainMix(1) == 0);
  REQUIRE(destination.iopBus().write16(voiceMixAddresses[15], 0xffff).status ==
    IOPBusStatus::Completed);
  REQUIRE(destination.spu2Controller().readVoiceMix(15) == 0xffff);
  REQUIRE(foreign.readVoiceMix(15) == 0);
  REQUIRE(destination.iopBus().write16(configurationAddresses[4], 0x1234).status ==
    IOPBusStatus::Completed);
  REQUIRE(destination.spu2Controller().readSPDIFConfiguration(4) == 0x1234);
  REQUIRE(foreign.readSPDIFConfiguration(4) == 0);
  REQUIRE(destination.iopBus().write16(masterVolumeAddresses[3], 0x4321).status ==
    IOPBusStatus::Completed);
  REQUIRE(destination.spu2Controller().readMasterVolumeControl(3) == 0x4321);
  REQUIRE(foreign.readMasterVolumeControl(3) == 0);
  REQUIRE(destination.iopBus().write16(coreAttributeAddresses[1], 0).status ==
    IOPBusStatus::Completed);
  REQUIRE(destination.spu2Controller().readCoreAttribute(1) == 0);
  REQUIRE(destination.spu2Controller().readCoreAttribute(0) == 0xc001);
  REQUIRE(foreign.readCoreAttribute(0) == 0);
  REQUIRE(foreign.readCoreAttribute(1) == 0);
}

TEST_CASE("SPU2 changed images and late invalid state reject without partial adoption",
  "[iop][spu2][sound-ram][save-state]")
{
  NekoSystem source;
  source.spu2Controller().writeSoundRAMHalfword(0, 0x1234);
  source.spu2Controller().writeSoundRAMHalfword(0xfffff, 0x5678);
  programConfiguration(source);
  programMasterVolumes(source);
  programVoiceMix(source);
  programMainMix(source);
  programEffectInputVolumes(source);
  programEffectEndAddresses(source);
  programTransferStartAddresses(source);
  SPU2InputTest::programProfile(source.spu2Controller());
  for (std::size_t core = 0; core < autoDMAAddresses.size(); ++core)
  {
    REQUIRE(source.iopBus().write16(
      autoDMAAddresses[core], autoDMAWriteSelections[core]).status ==
      IOPBusStatus::Completed);
  }
  REQUIRE(source.iopBus().write16(coreAttributeAddresses[0], 0xc001).status ==
    IOPBusStatus::Completed);
  const auto candidate = source.saveState();
  NekoSystem destination;
  destination.spu2Controller().writeSoundRAMHalfword(0, 0xabcd);
  destination.spu2Controller().writeSoundRAMHalfword(0xfffff, 0xef01);
  REQUIRE(destination.iopBus().write16(transferStartAddressAddresses[3], 0xabcd).status ==
    IOPBusStatus::Completed);
  REQUIRE(destination.iopBus().write16(effectEndAddressHighAddresses[1], 0x1234).status ==
    IOPBusStatus::Completed);
  REQUIRE(destination.iopBus().write16(effectInputVolumeAddresses[11], 0xffff).status ==
    IOPBusStatus::Completed);
  REQUIRE(destination.iopBus().write16(mainMixAddresses[1], 0xffff).status ==
    IOPBusStatus::Completed);
  REQUIRE(destination.iopBus().write16(voiceMixAddresses[15], 0xffff).status ==
    IOPBusStatus::Completed);
  REQUIRE(destination.iopBus().write16(configurationAddresses[0], 0xffff).status ==
    IOPBusStatus::Completed);
  REQUIRE(destination.iopBus().write16(masterVolumeAddresses[0], 0x4567).status ==
    IOPBusStatus::Completed);
  REQUIRE(destination.iopBus().write16(coreAttributeAddresses[1], 0x4001).status ==
    IOPBusStatus::Completed);
  destination.startTrace();
  const auto before = destination.saveState();
  for (std::size_t core = 0; core < autoDMAAddresses.size(); ++core)
  {
    const auto path =
      "system.iop.spu2.autoDMAWriteSelection[" + std::to_string(core) + "].value";
    SaveStateMutation badSelectionChecksum(candidate);
    badSelectionChecksum.writeScalar(path, 0);
    REQUIRE_THROWS_AS(destination.loadState(badSelectionChecksum),
      std::invalid_argument);
    REQUIRE(destination.saveState() == before);
    for (const auto value : {
        autoDMAWriteSelections[1 - core], std::uint16_t{3}, std::uint16_t{4},
        std::uint16_t{0x4000}, std::uint16_t{0x7fff},
        std::uint16_t{0x8000}, std::uint16_t{0xffff}})
    {
      CAPTURE(core);
      CAPTURE(value);
      SaveStateMutation unsupportedSelection(candidate);
      unsupportedSelection.writeScalar(path, value);
      unsupportedSelection.refreshChecksum();
      REQUIRE_THROWS_AS(destination.loadState(unsupportedSelection),
        std::invalid_argument);
      REQUIRE(destination.saveState() == before);
    }
  }
  for (std::size_t index = 0; index < transferStartAddressAddresses.size(); ++index)
  {
    SaveStateMutation badAddressChecksum(candidate);
    badAddressChecksum.writeScalar(
      "system.iop.spu2.transferStartAddress[" + std::to_string(index) + "].value",
      transferStartAddressValues[index] ^ 0xffff);
    REQUIRE_THROWS_AS(destination.loadState(badAddressChecksum),
      std::invalid_argument);
    REQUIRE(destination.saveState() == before);
  }
  for (std::size_t core = 0; core < effectEndAddressHighAddresses.size(); ++core)
  {
    SaveStateMutation badEndAddressChecksum(candidate);
    badEndAddressChecksum.writeScalar(
      "system.iop.spu2.effectEndAddressHigh[" + std::to_string(core) + "].value",
      effectEndAddressHighValues[core] ^ 0xffff);
    REQUIRE_THROWS_AS(destination.loadState(badEndAddressChecksum),
      std::invalid_argument);
    REQUIRE(destination.saveState() == before);
  }
  for (std::size_t index = 0; index < effectInputVolumeAddresses.size(); ++index)
  {
    SaveStateMutation badVolumeChecksum(candidate);
    badVolumeChecksum.writeScalar(
      "system.iop.spu2.effectInputVolumes[" + std::to_string(index) + "].value",
      effectInputVolumeValues[index] ^ 0xffff);
    REQUIRE_THROWS_AS(destination.loadState(badVolumeChecksum),
      std::invalid_argument);
    REQUIRE(destination.saveState() == before);
  }
  for (std::size_t core = 0; core < mainMixAddresses.size(); ++core)
  {
    SaveStateMutation badMainMixChecksum(candidate);
    badMainMixChecksum.writeScalar(
      "system.iop.spu2.mainMix[" + std::to_string(core) + "].value", 0x1234);
    REQUIRE_THROWS_AS(destination.loadState(badMainMixChecksum),
      std::invalid_argument);
    REQUIRE(destination.saveState() == before);
  }
  SaveStateMutation badRoutingChecksum(candidate);
  badRoutingChecksum.writeScalar("system.iop.spu2.voiceMix[15].value", 0x1234);
  REQUIRE_THROWS_AS(destination.loadState(badRoutingChecksum),
    std::invalid_argument);
  REQUIRE(destination.saveState() == before);
  SaveStateMutation badChecksum(candidate);
  badChecksum.writeScalar("system.iop.spu2.spdifConfiguration[4].value", 0x1234);
  REQUIRE_THROWS_AS(destination.loadState(badChecksum), std::invalid_argument);
  REQUIRE(destination.saveState() == before);
  SaveStateMutation badMasterChecksum(candidate);
  badMasterChecksum.writeScalar("system.iop.spu2.masterVolumeControls[3].value", 0x1234);
  REQUIRE_THROWS_AS(destination.loadState(badMasterChecksum), std::invalid_argument);
  REQUIRE(destination.saveState() == before);
  for (std::size_t index = 0; index < masterVolumeAddresses.size(); ++index)
  {
    CAPTURE(index);
    SaveStateMutation unsupportedSweep(candidate);
    unsupportedSweep.writeScalar(
      "system.iop.spu2.masterVolumeControls[" + std::to_string(index) + "].value",
      0x8000 | masterVolumeValues[index]);
    unsupportedSweep.refreshChecksum();
    REQUIRE_THROWS_AS(destination.loadState(unsupportedSweep), std::invalid_argument);
    REQUIRE(destination.saveState() == before);
  }
  for (std::size_t core = 0; core < coreAttributeAddresses.size(); ++core)
  for (std::uint16_t value : {
      2, 3, 0x20, 0x30, 0x40, 0x80, 0x4003, 0x8003, 0xc003, 0xc081, 0xffff})
  {
    CAPTURE(core);
    CAPTURE(value);
    SaveStateMutation unsupportedAttribute(candidate);
    unsupportedAttribute.writeScalar(
      "system.iop.spu2.coreAttributes[" + std::to_string(core) + "].value",
      value);
    unsupportedAttribute.refreshChecksum();
    REQUIRE_THROWS_AS(destination.loadState(unsupportedAttribute),
      std::invalid_argument);
    REQUIRE(destination.saveState() == before);
  }
  SaveStateMutation lateInvalid(candidate);
  lateInvalid.writeScalar("system.iop.timing.pixelPhase", 0xffff);
  lateInvalid.refreshChecksum();
  REQUIRE_THROWS_AS(destination.loadState(lateInvalid), std::invalid_argument);
  REQUIRE(destination.saveState() == before);
  SaveStateMutation badRAMChecksum(candidate);
  badRAMChecksum.writeByte("system.iop.spu2.soundRAM",
    SPU2Controller::SOUND_RAM_SIZE - 1, 0xff);
  REQUIRE_THROWS_AS(destination.loadState(badRAMChecksum), std::invalid_argument);
  REQUIRE(destination.saveState() == before);
  REQUIRE(destination.traceEnabled());
  SaveStateMutation previousVersion(candidate);
  previousVersion.writeScalar("container.version", 87);
  previousVersion.refreshChecksum();
  REQUIRE_THROWS_AS(destination.loadState(previousVersion), std::invalid_argument);
  REQUIRE(destination.saveState() == before);
  REQUIRE(destination.iopBus().write16(configurationAddresses[1], 0x5678).status ==
    IOPBusStatus::Completed);
  REQUIRE(destination.spu2Controller().readSPDIFConfiguration(1) == 0x5678);
}
