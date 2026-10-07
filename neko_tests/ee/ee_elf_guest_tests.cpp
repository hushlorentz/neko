#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "catch.hpp"
#include "elf_runner.hpp"
#include "neko_system.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_register_ids.hpp"

namespace
{
  struct ExceptionHandlingGuestResult
  {
    EEELFLoadResult load;
    std::uint64_t masterCycles = 0;
    bool returned = false;
    bool cycleLimitReached = false;
  };

  std::string guestPath(const std::string &fileName)
  {
    return
      std::string(NEKO_EE_ELF_GUEST_DIR) + "/" + fileName;
  }

  std::vector<std::uint8_t> readGuest(
    const std::string &fileName)
  {
    const std::string path = guestPath(fileName);
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
      throw std::runtime_error(
        "Could not open EE ELF guest: " + path);
    }
    return std::vector<std::uint8_t>(
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>());
  }

  ExceptionHandlingGuestResult runExceptionHandlingGuest(
    NekoSystem *system,
    const std::vector<std::uint8_t> &image,
    std::uint64_t maxMasterCycles)
  {
    ExceptionHandlingGuestResult result;
    result.load = system->loadELF(image);
    EECore &core = system->eeCore();
    core.startExecution(result.load.entryPoint);

    while (core.clockActive() &&
           result.masterCycles < maxMasterCycles)
    {
      if (core.programCounter() ==
          EEGuestRuntime::RETURN_ADDRESS)
      {
        core.haltExecution();
        result.returned = true;
        break;
      }
      system->clockMasterCycle();
      ++result.masterCycles;
    }

    result.cycleLimitReached =
      core.clockActive() &&
      result.masterCycles == maxMasterCycles;
    return result;
  }

  void uploadVectorCopyProgram(VPU *vpu)
  {
    vpu->writeMicroInstruction(
      0,
      VPU_MOVE_ENCODING |
        (UINT32_C(0xf) << 21) |
        (static_cast<std::uint32_t>(
          VPU_REGISTER_VF02) << 16) |
        (static_cast<std::uint32_t>(
          VPU_REGISTER_VF01) << 11),
      VPU_E_BIT | VPU_NOP);
    vpu->writeMicroInstruction(
      1,
      VPU_LOWER_NOP,
      VPU_NOP);
  }
}

TEST_CASE("PS2DEV scalar EE ELF guests complete successfully")
{
  struct GuestExpectation
  {
    const char *fileName;
    std::uint64_t instructions;
  };
  const GuestExpectation guests[] = {
    {"arithmetic.elf", 16},
    {"branches.elf", 12},
    {"memory.elf", 23},
    {"mmio.elf", 20},
    {"fifo.elf", 16},
    {"vif1_dma.elf", 34},
    {"cop2_transfer.elf", 19},
    {"cop2_control.elf", 23},
    {"vu_macro_arithmetic.elf", 35},
    {"vu_macro_families.elf", 107}
  };

  for (const GuestExpectation &guest : guests)
  {
    CAPTURE(guest.fileName);
    NekoSystem system;
    const EEGuestExecutionResult result =
      system.runELF(readGuest(guest.fileName), 512);

    REQUIRE(result.outcome == EEGuestOutcome::Completed);
    REQUIRE(result.exitCode == 0);
    REQUIRE(
      result.execution.instructions ==
      guest.instructions);
    REQUIRE_FALSE(result.execution.cycleLimitReached);
    REQUIRE(
      result.execution.programCounter ==
      EEGuestRuntime::RETURN_ADDRESS);
  }
}

TEST_CASE("PS2DEV COP0 TLB guest exposes decoded management results")
{
  const std::vector<std::uint8_t> guest =
    readGuest("cop0_tlb_management.elf");
  NekoSystem first;
  NekoSystem second;
  first.startTrace();
  second.startTrace();

  const EEGuestExecutionResult firstResult =
    first.runELF(guest, 1024);
  const EEGuestExecutionResult secondResult =
    second.runELF(guest, 1024);
  CAPTURE(neko_frontend::formatELFRun(firstResult));

  const auto requireExecution =
    [](const EEGuestExecutionResult &result)
    {
      REQUIRE(result.outcome == EEGuestOutcome::Completed);
      REQUIRE(result.exitCode == 0);
      REQUIRE_FALSE(result.execution.cycleLimitReached);
      REQUIRE(
        result.execution.programCounter ==
        EEGuestRuntime::RETURN_ADDRESS);
      REQUIRE(
        result.execution.pendingException ==
        EEException::None);
    };
  requireExecution(firstResult);
  requireExecution(secondResult);

  REQUIRE(first.traceHash() != 0);
  REQUIRE(first.traceHash() == second.traceHash());
  REQUIRE(
    first.eeCore().stateHash() ==
    second.eeCore().stateHash());

  const auto requireArchitecturalState =
    [](NekoSystem *system)
    {
      const EECore &core = system->eeCore();
      const std::uint32_t outputAddress =
        static_cast<std::uint32_t>(
          core.generalRegister(19).low);
      REQUIRE(outputAddress != 0);

      const std::uint32_t expected[] = {
        47,
        47,
        EECOP0PageMask::SIZE_16_KIB,
        UINT32_C(0x1234002a),
        UINT32_C(0x0001001e),
        UINT32_C(0x0002001a),
        5,
        EECOP0Index::PROBE_FAILURE,
        6,
        47,
        EECOP0PageMask::SIZE_64_KIB,
        UINT32_C(0x34000033),
        UINT32_C(0x0003001f),
        UINT32_C(0x0004001b),
        47,
        47
      };
      for (std::size_t index = 0;
           index < sizeof(expected) / sizeof(expected[0]);
           ++index)
      {
        CAPTURE(index);
        REQUIRE(
          system->eeBus().read32(
            outputAddress +
            static_cast<std::uint32_t>(index * 4)) ==
          expected[index]);
      }

      const EETLBEntry &indexed = core.tlbEntry(5);
      REQUIRE(
        indexed.pageMask ==
        EECOP0PageMask::SIZE_16_KIB);
      REQUIRE(indexed.entryHi == UINT32_C(0x1234002a));
      REQUIRE(indexed.evenPage.value == UINT32_C(0x0001001e));
      REQUIRE(indexed.oddPage.value == UINT32_C(0x0002001a));

      const EETLBEntry &global = core.tlbEntry(6);
      REQUIRE(global.global());
      REQUIRE(global.entryHi == UINT32_C(0x23456011));

      const EETLBEntry &random = core.tlbEntry(47);
      REQUIRE(
        random.pageMask ==
        EECOP0PageMask::SIZE_64_KIB);
      REQUIRE(random.entryHi == UINT32_C(0x34000033));
      REQUIRE(random.evenPage.value == UINT32_C(0x0003001f));
      REQUIRE(random.oddPage.value == UINT32_C(0x0004001b));
    };
  requireArchitecturalState(&first);
  requireArchitecturalState(&second);
}

TEST_CASE("PS2DEV mapped-memory guest handles TLB exceptions")
{
  const std::vector<std::uint8_t> guest =
    readGuest("mapped_memory.elf");
  NekoSystem first;
  NekoSystem second;
  first.startTrace();
  second.startTrace();

  const ExceptionHandlingGuestResult firstResult =
    runExceptionHandlingGuest(&first, guest, 4096);
  const ExceptionHandlingGuestResult secondResult =
    runExceptionHandlingGuest(&second, guest, 4096);

  const auto requireExecution =
    [](const NekoSystem &system,
       const ExceptionHandlingGuestResult &result)
    {
      REQUIRE(result.returned);
      REQUIRE_FALSE(result.cycleLimitReached);
      REQUIRE(
        system.eeCore().programCounter() ==
        EEGuestRuntime::RETURN_ADDRESS);
      REQUIRE(system.eeCore().generalRegister(2).low == 0);
      REQUIRE(
        system.eeCore().pendingException() ==
        EEException::None);
    };
  requireExecution(first, firstResult);
  requireExecution(second, secondResult);

  REQUIRE(first.traceHash() != 0);
  REQUIRE(first.traceHash() == second.traceHash());
  REQUIRE(first.eeCore().stateHash() == second.eeCore().stateHash());

  const auto requireArchitecturalState =
    [](NekoSystem *system)
    {
      const std::uint32_t outputAddress =
        static_cast<std::uint32_t>(
          system->eeCore().generalRegister(19).low);
      REQUIRE(outputAddress != 0);

      const std::uint32_t expectedLoads[] = {
        UINT32_C(0x04040001),
        UINT32_C(0x04040002),
        UINT32_C(0x16160001),
        UINT32_C(0x16160002),
        UINT32_C(0x64640001),
        UINT32_C(0x64640002),
        UINT32_C(0x02560001),
        UINT32_C(0x02560002),
        UINT32_C(0x10010001),
        UINT32_C(0x10010002),
        UINT32_C(0x40040001),
        UINT32_C(0x40040002),
        UINT32_C(0x16100001),
        UINT32_C(0x16100002),
        UINT32_C(0xa51d0042),
        UINT32_C(0x610b0043)
      };
      for (std::size_t index = 0;
           index <
             sizeof(expectedLoads) / sizeof(expectedLoads[0]);
           ++index)
      {
        CAPTURE(index);
        REQUIRE(
          system->eeBus().read32(
            outputAddress +
            static_cast<std::uint32_t>(index * 4)) ==
          expectedLoads[index]);
      }

      REQUIRE(system->eeBus().read32(outputAddress + 64) == 3);
      REQUIRE(
        system->eeBus().read32(outputAddress + 144) ==
        UINT32_C(0xd17d00aa));

      struct ExceptionExpectation
      {
        std::uint32_t vectorMarker;
        std::uint32_t cause;
        std::uint32_t badVirtualAddress;
        std::uint32_t context;
        std::uint32_t entryHi;
        std::uint32_t epc;
      };
      const ExceptionExpectation exceptions[] = {
        {1, EEExceptionCode::TLB_LOAD_OR_FETCH * 4,
         UINT32_C(0x28000800), UINT32_C(0x00140000),
         UINT32_C(0x28000043), UINT32_C(0x001006b0)},
        {2, EEExceptionCode::TLB_LOAD_OR_FETCH * 4,
         UINT32_C(0x32000800), UINT32_C(0x00190000),
         UINT32_C(0x32000000), UINT32_C(0x00100778)},
        {2, EEExceptionCode::TLB_MODIFIED * 4,
         UINT32_C(0x34000800), UINT32_C(0x001a0000),
         UINT32_C(0x34000000), UINT32_C(0x00100840)}
      };
      for (std::size_t index = 0;
           index < sizeof(exceptions) / sizeof(exceptions[0]);
           ++index)
      {
        CAPTURE(index);
        const std::uint32_t recordAddress =
          outputAddress + 68 +
          static_cast<std::uint32_t>(index * 24);
        REQUIRE(
          system->eeBus().read32(recordAddress) ==
          exceptions[index].vectorMarker);
        REQUIRE(
          system->eeBus().read32(recordAddress + 4) ==
          exceptions[index].cause);
        REQUIRE(
          system->eeBus().read32(recordAddress + 8) ==
          exceptions[index].badVirtualAddress);
        REQUIRE(
          system->eeBus().read32(recordAddress + 12) ==
          exceptions[index].context);
        REQUIRE(
          system->eeBus().read32(recordAddress + 16) ==
          exceptions[index].entryHi);
        REQUIRE(
          system->eeBus().read32(recordAddress + 20) ==
          exceptions[index].epc);
      }
    };
  requireArchitecturalState(&first);
  requireArchitecturalState(&second);
}

TEST_CASE("PS2DEV scratchpad DMA guest composes CPU and channel visibility")
{
  const std::vector<std::uint8_t> guest =
    readGuest("scratchpad_dma.elf");
  NekoSystem first;
  NekoSystem second;
  first.startTrace();
  second.startTrace();

  const EEGuestExecutionResult firstResult =
    first.runELF(guest, 4096);
  const EEGuestExecutionResult secondResult =
    second.runELF(guest, 4096);
  CAPTURE(neko_frontend::formatELFRun(firstResult));
  CAPTURE(neko_frontend::formatELFRun(secondResult));

  const auto requireExecution =
    [](const EEGuestExecutionResult &result)
    {
      REQUIRE(result.outcome == EEGuestOutcome::Completed);
      REQUIRE(result.exitCode == 0);
      REQUIRE_FALSE(result.execution.cycleLimitReached);
      REQUIRE(
        result.execution.programCounter ==
        EEGuestRuntime::RETURN_ADDRESS);
      REQUIRE(
        result.execution.pendingException ==
        EEException::None);
    };
  requireExecution(firstResult);
  requireExecution(secondResult);

  REQUIRE(first.traceHash() != 0);
  REQUIRE(first.traceHash() == second.traceHash());
  REQUIRE(first.eeCore().stateHash() == second.eeCore().stateHash());

  const auto requireQuadword =
    [](const EEQuadword &actual,
       std::uint64_t expectedLow,
       std::uint64_t expectedHigh)
    {
      REQUIRE(actual.low == expectedLow);
      REQUIRE(actual.high == expectedHigh);
    };
  const auto requireArchitecturalState =
    [&](NekoSystem *system)
    {
      const std::uint32_t outputAddress =
        static_cast<std::uint32_t>(
          system->eeCore().generalRegister(19).low);
      REQUIRE(outputAddress != 0);

      const std::uint32_t expected[] = {
        UINT32_C(0x89abcdef),
        UINT32_C(0x00000000),
        UINT32_C(0x00000000),
        UINT32_C(0x00000010),
        UINT32_C(0x03000100),
        UINT32_C(0x00000000),
        UINT32_C(0x00000000),
        UINT32_C(0x00000010),
        UINT32_C(0x03000300),
        UINT32_C(0x00000080),
        UINT32_C(0x00000140),
        UINT32_C(0x00000080),
        UINT32_C(0x00000240),
        UINT32_C(0x11112222),
        UINT32_C(0xbbbbcccc),
        UINT32_C(0x03000300)
      };
      for (std::size_t index = 0;
           index < sizeof(expected) / sizeof(expected[0]);
           ++index)
      {
        CAPTURE(index);
        REQUIRE(
          system->eeBus().read32(
            outputAddress +
            static_cast<std::uint32_t>(index * 4)) ==
          expected[index]);
      }

      const std::uint32_t normalFromAddress =
        system->eeBus().read32(outputAddress + 64);
      const std::uint32_t normalToAddress =
        system->eeBus().read32(outputAddress + 68);
      const std::uint32_t interleaveFromAddress =
        system->eeBus().read32(outputAddress + 72);
      const std::uint32_t interleaveToAddress =
        system->eeBus().read32(outputAddress + 76);
      const std::uint32_t coherenceAddress =
        system->eeBus().read32(outputAddress + 80);
      REQUIRE(
        system->eeBus().read32(outputAddress + 84) ==
        UINT32_C(0x03000100));

      EEQuadword value = {};
      REQUIRE(system->eeBus().readDMAC128(
        normalFromAddress, &value));
      requireQuadword(
        value,
        UINT64_C(0x0123456789abcdef),
        UINT64_C(0xfedcba9876543210));
      REQUIRE(system->eeBus().readDMAC128(
        normalFromAddress + 16, &value));
      requireQuadword(
        value,
        UINT64_C(0x1111222233334444),
        UINT64_C(0xaaaabbbbccccdddd));

      REQUIRE(system->eeBus().readDMAC128(
        interleaveFromAddress, &value));
      requireQuadword(value, UINT64_C(0x10), UINT64_C(0x100));
      REQUIRE(system->eeBus().readDMAC128(
        interleaveFromAddress + 16, &value));
      requireQuadword(value, UINT64_C(0x20), UINT64_C(0x200));
      REQUIRE(system->eeBus().readDMAC128(
        interleaveFromAddress + 32, &value));
      requireQuadword(value, 0, 0);
      REQUIRE(system->eeBus().readDMAC128(
        interleaveFromAddress + 48, &value));
      requireQuadword(value, 0, 0);
      REQUIRE(system->eeBus().readDMAC128(
        interleaveFromAddress + 64, &value));
      requireQuadword(value, UINT64_C(0x30), UINT64_C(0x300));
      REQUIRE(system->eeBus().readDMAC128(
        interleaveFromAddress + 80, &value));
      requireQuadword(value, UINT64_C(0x40), UINT64_C(0x400));
      REQUIRE(system->eeBus().readDMAC128(
        interleaveFromAddress + 96, &value));
      requireQuadword(value, 0, 0);
      REQUIRE(system->eeBus().readDMAC128(
        interleaveFromAddress + 112, &value));
      requireQuadword(value, 0, 0);

      REQUIRE(system->eeBus().readDMAC128(
        coherenceAddress, &value));
      requireQuadword(
        value,
        UINT64_C(0x9999aaaabbbbcccc),
        UINT64_C(0xddddeeeeffff0000));

      EEMemorySystem &memory = system->eeMemorySystem();
      REQUIRE(
        memory.readScratchpadDMA128(0x3ff0, &value) ==
        EEScratchpadAccessResult::Completed);
      requireQuadword(
        value,
        UINT64_C(0x8877665544332211),
        UINT64_C(0xffeeddccbbaa9988));
      REQUIRE(
        memory.readScratchpadDMA128(0, &value) ==
        EEScratchpadAccessResult::Completed);
      requireQuadword(
        value,
        UINT64_C(0x1020304050607080),
        UINT64_C(0x90a0b0c0d0e0f000));
      REQUIRE(system->eeBus().readDMAC128(
        normalToAddress, &value));
      requireQuadword(
        value,
        UINT64_C(0x8877665544332211),
        UINT64_C(0xffeeddccbbaa9988));
      REQUIRE(
        memory.readScratchpadDMA128(0x200, &value) ==
        EEScratchpadAccessResult::Completed);
      requireQuadword(value, UINT64_C(0x50), UINT64_C(0x500));
      REQUIRE(
        memory.readScratchpadDMA128(0x210, &value) ==
        EEScratchpadAccessResult::Completed);
      requireQuadword(value, UINT64_C(0x60), UINT64_C(0x600));
      REQUIRE(
        memory.readScratchpadDMA128(0x220, &value) ==
        EEScratchpadAccessResult::Completed);
      requireQuadword(value, UINT64_C(0x70), UINT64_C(0x700));
      REQUIRE(
        memory.readScratchpadDMA128(0x230, &value) ==
        EEScratchpadAccessResult::Completed);
      requireQuadword(value, UINT64_C(0x80), UINT64_C(0x800));
      REQUIRE(system->eeBus().readDMAC128(
        interleaveToAddress, &value));
      requireQuadword(value, UINT64_C(0x50), UINT64_C(0x500));
      REQUIRE(system->eeBus().readDMAC128(
        interleaveToAddress + 80, &value));
      requireQuadword(value, UINT64_C(0x80), UINT64_C(0x800));

      REQUIRE(system->interruptPending());
    };
  requireArchitecturalState(&first);
  requireArchitecturalState(&second);
}

TEST_CASE("PS2DEV cache guest composes maintenance and coherence workflows")
{
  const std::vector<std::uint8_t> guest =
    readGuest("cache_workflow.elf");
  NekoSystem first;
  NekoSystem second;
  first.startTrace();
  second.startTrace();

  const EEGuestExecutionResult firstResult =
    first.runELF(guest, 4096);
  const EEGuestExecutionResult secondResult =
    second.runELF(guest, 4096);
  CAPTURE(neko_frontend::formatELFRun(firstResult));
  CAPTURE(neko_frontend::formatELFRun(secondResult));

  const auto requireExecution =
    [](const EEGuestExecutionResult &result)
    {
      REQUIRE(result.outcome == EEGuestOutcome::Completed);
      REQUIRE(result.exitCode == 0);
      REQUIRE_FALSE(result.execution.cycleLimitReached);
      REQUIRE(
        result.execution.programCounter ==
        EEGuestRuntime::RETURN_ADDRESS);
      REQUIRE(
        result.execution.pendingException ==
        EEException::None);
    };
  requireExecution(firstResult);
  requireExecution(secondResult);

  REQUIRE(first.traceHash() != 0);
  REQUIRE(first.traceHash() == second.traceHash());
  REQUIRE(first.eeCore().stateHash() == second.eeCore().stateHash());

  const auto requireArchitecturalState =
    [](NekoSystem *system)
    {
      const std::uint32_t outputAddress =
        static_cast<std::uint32_t>(
          system->eeCore().generalRegister(19).low);
      REQUIRE(outputAddress != 0);

      const std::uint32_t expected[] = {
        UINT32_C(0x33334444),
        UINT32_C(0x11112222),
        UINT32_C(0x33334444),
        UINT32_C(0x33334444),
        UINT32_C(0x55556666),
        UINT32_C(0x33334444),
        UINT32_C(0x55556666),
        UINT32_C(0xa1a2a3a4),
        UINT32_C(0xb1b2b3b4),
        UINT32_C(0x00000000),
        EECOP0TagLo::VALID | EECOP0TagLo::LOCK,
        UINT32_C(0x00002000) | EECOP0TagLo::VALID,
        1,
        1,
        1,
        1,
        2,
        1,
        2,
        1,
        2
      };
      for (std::size_t index = 0;
           index < sizeof(expected) / sizeof(expected[0]);
           ++index)
      {
        CAPTURE(index);
        REQUIRE(
          system->eeBus().read32(
            outputAddress +
            static_cast<std::uint32_t>(index * 4)) ==
          expected[index]);
      }

      const std::uint32_t dmaTargetAddress =
        system->eeBus().read32(outputAddress + 84);
      const std::uint32_t functionAddress =
        system->eeBus().read32(outputAddress + 88);
      const std::uint32_t firstAlias =
        system->eeBus().read32(outputAddress + 92);
      const std::uint32_t secondAlias =
        system->eeBus().read32(outputAddress + 96);
      REQUIRE(
        system->eeBus().read32(dmaTargetAddress) ==
        UINT32_C(0x55556666));
      REQUIRE(
        system->eeBus().read32(functionAddress) ==
        UINT32_C(0x24030002));

      EEMemorySystem &memory = system->eeMemorySystem();
      const std::size_t dataSet =
        (dmaTargetAddress >> 6) &
        (EEMemorySystem::DATA_CACHE_SET_COUNT - 1);
      bool foundDataLine = false;
      for (std::size_t way = 0;
           way < EEMemorySystem::CACHE_WAY_COUNT;
           ++way)
      {
        const EECacheLine &line =
          memory.dataCacheLine(dataSet, way);
        if (line.valid &&
            line.physicalTag ==
              (dmaTargetAddress &
               EECacheLine::PHYSICAL_TAG_MASK))
        {
          REQUIRE_FALSE(line.dirty);
          foundDataLine = true;
        }
      }
      REQUIRE(foundDataLine);

      const EECacheLine &locked =
        memory.dataCacheLine(4, 0);
      REQUIRE(locked.valid);
      REQUIRE(locked.locked);
      REQUIRE_FALSE(locked.dirty);
      REQUIRE(locked.physicalTag == 0);
      const EECacheLine &replaceable =
        memory.dataCacheLine(4, 1);
      REQUIRE(replaceable.valid);
      REQUIRE_FALSE(replaceable.locked);
      REQUIRE(
        replaceable.physicalTag ==
        UINT32_C(0x00002000));

      const std::size_t firstSet =
        (firstAlias >> 6) &
        (EEMemorySystem::INSTRUCTION_CACHE_SET_COUNT - 1);
      const std::size_t secondSet =
        (secondAlias >> 6) &
        (EEMemorySystem::INSTRUCTION_CACHE_SET_COUNT - 1);
      REQUIRE(firstSet != secondSet);
      const auto requireInstructionAlias =
        [&](std::size_t set)
        {
          bool found = false;
          for (std::size_t way = 0;
               way < EEMemorySystem::CACHE_WAY_COUNT;
               ++way)
          {
            const EECacheLine &line =
              memory.instructionCacheLine(set, way);
            if (line.valid &&
                line.physicalTag ==
                  (functionAddress &
                   EECacheLine::PHYSICAL_TAG_MASK))
            {
              found = true;
            }
          }
          REQUIRE(found);
        };
      requireInstructionAlias(firstSet);
      requireInstructionAlias(secondSet);
    };
  requireArchitecturalState(&first);
  requireArchitecturalState(&second);
}

TEST_CASE("PS2DEV MMI semantic guests complete successfully")
{
  struct GuestExpectation
  {
    const char *fileName;
    std::uint64_t instructions;
  };
  const GuestExpectation guests[] = {
    {"mmi_arithmetic.elf", 126},
    {"mmi_permutations.elf", 194},
    {"mmi_hilo.elf", 283}
  };

  for (const GuestExpectation &guest : guests)
  {
    CAPTURE(guest.fileName);
    NekoSystem system;
    const EEGuestExecutionResult result =
      system.runELF(readGuest(guest.fileName), 2048);
    CAPTURE(neko_frontend::formatELFRun(result));

    REQUIRE(result.outcome == EEGuestOutcome::Completed);
    REQUIRE(result.exitCode == 0);
    REQUIRE(
      result.execution.instructions ==
      guest.instructions);
    REQUIRE_FALSE(result.execution.cycleLimitReached);
    REQUIRE(
      result.execution.programCounter ==
      EEGuestRuntime::RETURN_ADDRESS);
  }
}

TEST_CASE("PS2DEV mixed MMI guest exposes deterministic host state")
{
  const std::vector<std::uint8_t> guest =
    readGuest("mmi_mixed.elf");
  NekoSystem first;
  NekoSystem second;
  first.startTrace();
  second.startTrace();

  const EEGuestExecutionResult firstResult =
    first.runELF(guest, 4096);
  const EEGuestExecutionResult secondResult =
    second.runELF(guest, 4096);
  CAPTURE(neko_frontend::formatELFRun(firstResult));

  const auto requireExecution =
    [](const EEGuestExecutionResult &result)
    {
      REQUIRE(result.outcome == EEGuestOutcome::Completed);
      REQUIRE(result.exitCode == 0);
      REQUIRE(result.execution.instructions == 343);
      REQUIRE(result.execution.masterCycles == 266);
      REQUIRE(result.execution.eeCycles == 266);
      REQUIRE_FALSE(result.execution.cycleLimitReached);
      REQUIRE(
        result.execution.state ==
        EEExecutionState::Halted);
      REQUIRE(
        result.execution.stopReason ==
        EEStopReason::HostHalt);
      REQUIRE(
        result.execution.programCounter ==
        EEGuestRuntime::RETURN_ADDRESS);
      REQUIRE(
        result.execution.pendingException ==
        EEException::None);
      REQUIRE(result.execution.exceptionAddress == 0);
    };
  requireExecution(firstResult);
  requireExecution(secondResult);

  REQUIRE(first.trace().size() == second.trace().size());
  REQUIRE_FALSE(first.trace().empty());
  REQUIRE(first.traceHash() != 0);
  REQUIRE(first.traceHash() == second.traceHash());
  REQUIRE(first.eeCore().stateHash() != 0);
  REQUIRE(
    first.eeCore().stateHash() ==
    second.eeCore().stateHash());

  const auto requireArchitecturalState =
    [](NekoSystem *system)
    {
      const EECore &core = system->eeCore();
      REQUIRE(
        core.generalRegister(6) ==
        EERegister128{UINT64_C(16), UINT64_C(0)});
      REQUIRE(
        core.generalRegister(7) ==
        EERegister128{UINT64_C(3), UINT64_C(0)});
      REQUIRE(
        core.generalRegister(10) ==
        EERegister128{
          UINT64_C(0x0f0f00000f000f00),
          UINT64_C(0x1030507000000000)});
      REQUIRE(
        core.generalRegister(13) ==
        EERegister128{UINT64_C(2), UINT64_C(2)});
      REQUIRE(
        core.generalRegister(14) ==
        EERegister128{UINT64_C(1), UINT64_C(1)});
      REQUIRE(core.generalRegister(15).low == 5);
      REQUIRE(core.generalRegister(16).low == 7);
      REQUIRE(core.generalRegister(17).low == 11);
      REQUIRE(core.generalRegister(18).low == 13);
      REQUIRE(core.hi() == 1);
      REQUIRE(core.lo() == 2);
      REQUIRE(core.hi1() == 1);
      REQUIRE(core.lo1() == 2);
      REQUIRE(core.shiftAmount() == 16);
      REQUIRE(core.stopReason() == EEStopReason::HostHalt);

      const std::uint32_t outputAddress =
        static_cast<std::uint32_t>(
          core.generalRegister(19).low);
      REQUIRE(outputAddress != 0);
      REQUIRE(
        system->eeBus().readQuadword(outputAddress) ==
        GIFQuadword{
          UINT32_C(0x0f000f00),
          UINT32_C(0x0f0f0000),
          UINT32_C(0x00000000),
          UINT32_C(0x10305070)});
      REQUIRE(
        system->eeBus().readQuadword(outputAddress + 16) ==
        GIFQuadword{
          UINT32_C(0x00000002),
          UINT32_C(0x00000000),
          UINT32_C(0x00000002),
          UINT32_C(0x00000000)});
      REQUIRE(
        system->eeBus().readQuadword(outputAddress + 32) ==
        GIFQuadword{
          UINT32_C(0x00000001),
          UINT32_C(0x00000000),
          UINT32_C(0x00000001),
          UINT32_C(0x00000000)});
      REQUIRE(system->eeBus().read32(outputAddress + 48) == 16);
      REQUIRE(system->eeBus().read32(outputAddress + 52) == 3);
      REQUIRE(system->eeBus().read32(outputAddress + 56) == 5);
      REQUIRE(system->eeBus().read32(outputAddress + 60) == 7);
      REQUIRE(system->eeBus().read32(outputAddress + 64) == 11);
      REQUIRE(system->eeBus().read32(outputAddress + 68) == 13);
    };
  requireArchitecturalState(&first);
  requireArchitecturalState(&second);
}

TEST_CASE("PS2DEV COP1 semantic capstone integrates pipeline behavior")
{
  NekoSystem system;
  system.startTrace();
  const EEGuestExecutionResult result =
    system.runELF(readGuest("cop1_semantics.elf"), 256);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  REQUIRE(result.execution.instructions == 52);
  REQUIRE_FALSE(result.execution.cycleLimitReached);
  REQUIRE(
    result.execution.programCounter ==
    EEGuestRuntime::RETURN_ADDRESS);
  const EECore &core = system.eeCore();
  REQUIRE(
    core.floatingPointRegister(4) ==
    UINT32_C(0x40700000));
  REQUIRE(
    core.floatingPointRegister(5) ==
    UINT32_C(0x3f400000));
  REQUIRE(
    core.floatingPointRegister(6) ==
    UINT32_C(0x40580000));
  REQUIRE(
    core.floatingPointRegister(7) ==
    UINT32_C(0x40800000));
  REQUIRE(
    core.floatingPointRegister(9) ==
    UINT32_C(0xbfc00000));
  REQUIRE(
    core.floatingPointRegister(10) ==
    UINT32_C(0x40100000));
  REQUIRE(
    core.floatingPointRegister(11) ==
    UINT32_C(0x3fc00000));
  REQUIRE(
    core.floatingPointRegister(12) ==
    UINT32_C(0xc0400000));
  REQUIRE(core.floatingPointRegister(14) == 2);
  REQUIRE(
    core.floatingPointRegister(15) ==
    UINT32_C(0x40000000));
  REQUIRE(
    core.floatingPointRegister(16) ==
    UINT32_C(0x7fffffff));
  REQUIRE(
    core.floatingPointRegister(17) ==
    UINT32_C(0x40700000));
  REQUIRE(
    core.floatingPointRegister(18) ==
    UINT32_C(0x40b40000));
  REQUIRE(
    core.floatingPointRegister(19) ==
    UINT32_C(0x40700000));
  REQUIRE(core.generalRegister(21).low == UINT64_C(5));
  REQUIRE(
    core.generalRegister(22).low ==
    UINT64_C(0xfffffffffffffffd));
  REQUIRE(
    core.generalRegister(23).low ==
    UINT64_C(0x0000000040b40000));
  REQUIRE(
    core.generalRegister(24).low ==
    UINT64_C(0x0000000040700000));
  REQUIRE(
    core.generalRegister(25).low ==
    UINT64_C(0x0000000040700000));
  REQUIRE(core.generalRegister(26).low == UINT64_C(4));
  REQUIRE(
    core.cop1ControlRegister(31) ==
    (EECOP1Control::STATUS_FIXED |
     EECOP1Control::CONDITION |
     EECOP1Control::CAUSE_INVALID |
     EECOP1Control::STICKY_INVALID));
  const std::uint32_t expectedReadbacks[] = {
    UINT32_C(0x40700000),
    UINT32_C(0x3f400000),
    UINT32_C(0x40580000),
    UINT32_C(0x40800000),
    UINT32_C(0xbfc00000),
    UINT32_C(0x40100000),
    UINT32_C(0x3fc00000),
    UINT32_C(0xc0400000),
    UINT32_C(0x00000002),
    UINT32_C(0x40000000),
    UINT32_C(0x7fffffff),
    EECOP1Control::STATUS_FIXED |
      EECOP1Control::CONDITION |
      EECOP1Control::CAUSE_INVALID |
      EECOP1Control::STICKY_INVALID
  };
  for (std::size_t index = 0;
       index < sizeof(expectedReadbacks) /
         sizeof(expectedReadbacks[0]);
       ++index)
  {
    REQUIRE(
      static_cast<std::uint32_t>(
        core.generalRegister(9 + index).low) ==
      expectedReadbacks[index]);
  }

  const std::uint32_t branchAddress =
    result.load.entryPoint + 80;
  const std::uint32_t branchTarget =
    result.load.entryPoint + 92;
  std::size_t branchEvents = 0;
  for (const NekoTraceEvent &event : system.trace())
  {
    if (event.type ==
          NekoTraceEventType::BranchScheduled &&
        event.value0 == branchAddress)
    {
      REQUIRE(event.value1 == branchTarget);
      REQUIRE(event.value2 == NekoEETraceBranch::TAKEN);
      ++branchEvents;
    }
  }
  REQUIRE(branchEvents == 1);

  const std::uint32_t forwardingAddresses[] = {
    result.load.entryPoint + 96,
    result.load.entryPoint + 100
  };
  std::vector<NekoTraceEvent> forwardingAdmissions;
  std::vector<NekoTraceEvent> forwardingRetirements;
  for (const NekoTraceEvent &event : system.trace())
  {
    const std::uint32_t address =
      static_cast<std::uint32_t>(event.value1);
    if (address != forwardingAddresses[0] &&
        address != forwardingAddresses[1])
    {
      continue;
    }
    if (event.type ==
          NekoTraceEventType::COP1StageTransition &&
        (event.value2 &
         NekoEETraceCOP1Stage::FROM_MASK) ==
          NekoEETraceCOP1Stage::NONE)
    {
      forwardingAdmissions.push_back(event);
    }
    else if (
      event.type == NekoTraceEventType::COP1Retired)
    {
      forwardingRetirements.push_back(event);
    }
  }
  REQUIRE(forwardingAdmissions.size() == 2);
  REQUIRE(forwardingRetirements.size() == 2);
  for (std::size_t index = 0; index < 2; ++index)
  {
    REQUIRE(
      static_cast<std::uint32_t>(
        forwardingAdmissions[index].value1) ==
      forwardingAddresses[index]);
    REQUIRE(
      ((forwardingAdmissions[index].value2 >>
        NekoEETraceCOP1Stage::TO_SHIFT) &
       NekoEETraceCOP1Stage::FROM_MASK) ==
      NekoEETraceCOP1Stage::R);
    REQUIRE(
      static_cast<std::uint32_t>(
        forwardingRetirements[index].value1) ==
      forwardingAddresses[index]);
  }
  REQUIRE(
    forwardingAdmissions[1].masterCycle + 1 ==
    forwardingRetirements[0].masterCycle);

  const std::uint32_t operateAddress =
    result.load.entryPoint + 132;
  const std::uint32_t moveAddress =
    result.load.entryPoint + 136;
  std::vector<NekoTraceEvent> pairAdmissions;
  for (const NekoTraceEvent &event : system.trace())
  {
    if (event.type ==
          NekoTraceEventType::COP1StageTransition &&
        (event.value2 &
         NekoEETraceCOP1Stage::FROM_MASK) ==
          NekoEETraceCOP1Stage::NONE)
    {
      const std::uint32_t address =
        static_cast<std::uint32_t>(event.value1);
      if (address == operateAddress ||
          address == moveAddress)
      {
        pairAdmissions.push_back(event);
      }
    }
  }
  REQUIRE(pairAdmissions.size() == 2);
  REQUIRE(
    static_cast<std::uint32_t>(
      pairAdmissions[0].value1) ==
    operateAddress);
  REQUIRE(
    static_cast<std::uint32_t>(
      pairAdmissions[1].value1) ==
    moveAddress);
  for (const NekoTraceEvent &admission :
       pairAdmissions)
  {
    REQUIRE(
      ((admission.value2 >>
        NekoEETraceCOP1Stage::TO_SHIFT) &
       NekoEETraceCOP1Stage::FROM_MASK) ==
      NekoEETraceCOP1Stage::R);
  }
  REQUIRE(
    pairAdmissions[0].masterCycle ==
    pairAdmissions[1].masterCycle);

  std::vector<NekoTraceEvent> moveInterlocks;
  for (const NekoTraceEvent &event : system.trace())
  {
    if (event.type ==
          NekoTraceEventType::COP1ResourceInterlock &&
        event.value0 == moveAddress &&
        event.masterCycle >
          pairAdmissions[1].masterCycle)
    {
      moveInterlocks.push_back(event);
    }
  }
  REQUIRE(moveInterlocks.size() == 1);
  REQUIRE(
    moveInterlocks[0].masterCycle ==
    pairAdmissions[1].masterCycle + 1);
  REQUIRE(
    moveInterlocks[0].value2 ==
    static_cast<std::uint8_t>(
      EEOperation::AddSingleCOP1));
}

TEST_CASE("PS2DEV COP1 transfer and memory guest preserves raw words")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("cop1_transfer_memory.elf"), 128);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  REQUIRE(result.execution.instructions == 17);
  REQUIRE_FALSE(result.execution.cycleLimitReached);
  REQUIRE(
    result.execution.programCounter ==
    EEGuestRuntime::RETURN_ADDRESS);

  const EECore &core = system.eeCore();
  REQUIRE(
    core.floatingPointRegister(2) ==
    UINT32_C(0x89abcdef));
  REQUIRE(
    core.floatingPointRegister(3) ==
    UINT32_C(0x89abcdef));
  REQUIRE(
    core.floatingPointRegister(4) ==
    UINT32_C(0x7fc12345));
  REQUIRE(
    core.floatingPointRegister(5) ==
    UINT32_C(0x89abcdef));
  REQUIRE(
    core.floatingPointRegister(6) ==
    UINT32_C(0x7fc12345));
  REQUIRE(
    core.generalRegister(10).low ==
    UINT64_C(0xffffffff89abcdef));
  REQUIRE(
    core.generalRegister(11).low ==
    UINT64_C(0xffffffff89abcdef));
  REQUIRE(
    core.generalRegister(12).low ==
    UINT64_C(0x000000007fc12345));
  REQUIRE(
    core.cop1ControlRegister(31) ==
    EECOP1Control::STATUS_FIXED);

  const std::uint32_t payloadAddress =
    static_cast<std::uint32_t>(
      core.generalRegister(8).low);
  REQUIRE(
    system.eeBus().read32(payloadAddress + 8) ==
    UINT32_C(0x89abcdef));
  REQUIRE(
    system.eeBus().read32(payloadAddress + 12) ==
    UINT32_C(0x7fc12345));
}

TEST_CASE("PS2DEV COP1 control-state guest applies FCR31 fields")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("cop1_control_state.elf"), 128);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  REQUIRE(result.execution.instructions == 19);
  REQUIRE_FALSE(result.execution.cycleLimitReached);
  REQUIRE(
    result.execution.programCounter ==
    EEGuestRuntime::RETURN_ADDRESS);

  const EECore &core = system.eeCore();
  REQUIRE(
    core.generalRegister(9).low ==
    (EECOP1Control::STATUS_FIXED |
     EECOP1Control::CONDITION));
  REQUIRE(
    core.generalRegister(11).low ==
    (EECOP1Control::STATUS_FIXED |
     EECOP1Control::CAUSE_MASK));
  REQUIRE(
    core.generalRegister(13).low ==
    (EECOP1Control::STATUS_FIXED |
     EECOP1Control::STICKY_MASK));
  REQUIRE(
    core.generalRegister(15).low ==
    (EECOP1Control::STATUS_FIXED |
     EECOP1Control::STATUS_WRITABLE_MASK));
  REQUIRE(
    core.generalRegister(16).low ==
    EECOP1Control::STATUS_FIXED);
  REQUIRE(
    core.cop1ControlRegister(31) ==
    EECOP1Control::STATUS_FIXED);
}

TEST_CASE("PS2DEV COP1 comparison guest covers every branch path")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("cop1_comparison_branches.elf"), 256);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  REQUIRE(result.execution.instructions == 38);
  REQUIRE_FALSE(result.execution.cycleLimitReached);
  REQUIRE(
    result.execution.programCounter ==
    EEGuestRuntime::RETURN_ADDRESS);

  const EECore &core = system.eeCore();
  REQUIRE(core.generalRegister(16).low == UINT64_C(0x5f));
  REQUIRE(core.generalRegister(17).low == UINT64_C(0xaa));
  REQUIRE(
    core.generalRegister(18).low ==
    EECOP1Control::STATUS_FIXED);
  REQUIRE(
    core.cop1ControlRegister(31) ==
    EECOP1Control::STATUS_FIXED);
}

TEST_CASE("PS2DEV COP1 conversion and unary guest covers raw edge cases")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("cop1_conversion_unary.elf"), 256);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  REQUIRE(result.execution.instructions == 35);
  REQUIRE_FALSE(result.execution.cycleLimitReached);
  REQUIRE(
    result.execution.programCounter ==
    EEGuestRuntime::RETURN_ADDRESS);

  const EECore &core = system.eeCore();
  REQUIRE(core.floatingPointRegister(10) == 0);
  REQUIRE(
    core.floatingPointRegister(11) ==
    UINT32_C(0x80000000));
  REQUIRE(
    core.floatingPointRegister(12) ==
    UINT32_C(0x40600000));
  REQUIRE(
    core.floatingPointRegister(13) ==
    UINT32_C(0x40600000));
  REQUIRE(core.floatingPointRegister(14) == 0);
  REQUIRE(
    core.floatingPointRegister(15) ==
    UINT32_C(0xc0400000));
  REQUIRE(
    core.floatingPointRegister(16) ==
    UINT32_C(0x4effffff));
  REQUIRE(
    core.floatingPointRegister(17) ==
    UINT32_C(0x7fffffff));
  REQUIRE(
    core.floatingPointRegister(18) ==
    UINT32_C(0x80000000));
  REQUIRE(
    core.floatingPointRegister(19) ==
    UINT32_C(0xffffffff));

  const std::uint32_t unaryStatus =
    EECOP1Control::STATUS_FIXED |
    EECOP1Control::STICKY_OVERFLOW |
    EECOP1Control::STICKY_UNDERFLOW;
  REQUIRE(core.generalRegister(23).low == unaryStatus);
  REQUIRE(core.generalRegister(24).low == unaryStatus);
  REQUIRE(
    core.generalRegister(25).low ==
    (unaryStatus |
     EECOP1Control::CAUSE_OVERFLOW |
     EECOP1Control::CAUSE_UNDERFLOW));
  REQUIRE(
    core.generalRegister(20).low ==
    (EECOP1Control::STATUS_FIXED |
     EECOP1Control::CAUSE_INVALID |
     EECOP1Control::STICKY_INVALID));
  REQUIRE(
    core.generalRegister(21).low ==
    (EECOP1Control::STATUS_FIXED |
     EECOP1Control::CAUSE_INVALID |
     EECOP1Control::STICKY_INVALID));
  REQUIRE(
    core.generalRegister(22).low ==
    (EECOP1Control::STATUS_FIXED |
     EECOP1Control::STICKY_INVALID));
  REQUIRE(
    core.cop1ControlRegister(31) ==
    (EECOP1Control::STATUS_FIXED |
     EECOP1Control::STICKY_INVALID));
}

TEST_CASE("PS2DEV COP1 basic arithmetic guest covers flags and saturation")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("cop1_basic_arithmetic.elf"), 256);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  REQUIRE(result.execution.instructions == 37);
  REQUIRE_FALSE(result.execution.cycleLimitReached);
  REQUIRE(
    result.execution.programCounter ==
    EEGuestRuntime::RETURN_ADDRESS);

  const EECore &core = system.eeCore();
  REQUIRE(
    core.floatingPointRegister(20) ==
    UINT32_C(0x40700000));
  REQUIRE(
    core.floatingPointRegister(21) ==
    UINT32_C(0x40800000));
  REQUIRE(
    core.floatingPointRegister(22) ==
    UINT32_C(0xc0c00000));
  REQUIRE(
    core.floatingPointRegister(23) ==
    UINT32_C(0xbf800000));
  REQUIRE(
    core.floatingPointRegister(24) ==
    UINT32_C(0xc0000000));
  REQUIRE(
    core.floatingPointRegister(25) ==
    UINT32_C(0x7fffffff));
  REQUIRE(core.floatingPointRegister(26) == 0);
  REQUIRE(
    core.floatingPointRegister(27) ==
    UINT32_C(0x7fffffff));
  REQUIRE(core.floatingPointRegister(28) == 0);
  REQUIRE(core.floatingPointRegister(29) == 0);
  REQUIRE(
    core.floatingPointRegister(30) ==
    UINT32_C(0x80000000));

  const std::uint32_t overflowStatus =
    EECOP1Control::STATUS_FIXED |
    EECOP1Control::CAUSE_OVERFLOW |
    EECOP1Control::STICKY_OVERFLOW;
  const std::uint32_t stickyStatus =
    EECOP1Control::STATUS_FIXED |
    EECOP1Control::STICKY_OVERFLOW |
    EECOP1Control::STICKY_UNDERFLOW;
  REQUIRE(core.generalRegister(15).low == overflowStatus);
  REQUIRE(
    core.generalRegister(16).low ==
    (stickyStatus |
     EECOP1Control::CAUSE_UNDERFLOW));
  REQUIRE(
    core.generalRegister(17).low ==
    (stickyStatus |
     EECOP1Control::CAUSE_OVERFLOW));
  REQUIRE(
    core.generalRegister(18).low ==
    (stickyStatus |
     EECOP1Control::CAUSE_UNDERFLOW));
  REQUIRE(core.generalRegister(19).low == stickyStatus);
  REQUIRE(core.generalRegister(20).low == stickyStatus);
  REQUIRE(core.cop1ControlRegister(31) == stickyStatus);
}

TEST_CASE("PS2DEV COP1 accumulator guest covers forwarding and flags")
{
  NekoSystem system;
  system.startTrace();
  const EEGuestExecutionResult result =
    system.runELF(readGuest("cop1_accumulator_compound.elf"), 256);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  REQUIRE(result.execution.instructions == 39);
  REQUIRE_FALSE(result.execution.cycleLimitReached);
  REQUIRE(
    result.execution.programCounter ==
    EEGuestRuntime::RETURN_ADDRESS);

  const EECore &core = system.eeCore();
  REQUIRE(
    core.floatingPointRegister(20) ==
    UINT32_C(0x40c00000));
  REQUIRE(
    core.floatingPointRegister(21) ==
    UINT32_C(0x3f800000));
  REQUIRE(
    core.floatingPointRegister(22) ==
    UINT32_C(0x41000000));
  REQUIRE(
    core.floatingPointRegister(23) ==
    UINT32_C(0x40800000));
  REQUIRE(
    core.floatingPointRegister(24) ==
    UINT32_C(0x40000000));
  REQUIRE(
    core.floatingPointRegister(25) ==
    UINT32_C(0x7fffffff));
  REQUIRE(
    core.floatingPointRegister(26) ==
    UINT32_C(0x40000000));
  REQUIRE(
    core.floatingPointAccumulator() ==
    UINT32_C(0x40000000));

  const std::uint32_t overflowStatus =
    EECOP1Control::STATUS_FIXED |
    EECOP1Control::CAUSE_OVERFLOW |
    EECOP1Control::STICKY_OVERFLOW;
  const std::uint32_t stickyStatus =
    EECOP1Control::STATUS_FIXED |
    EECOP1Control::STICKY_OVERFLOW |
    EECOP1Control::STICKY_UNDERFLOW;
  REQUIRE(core.generalRegister(15).low == overflowStatus);
  REQUIRE(
    core.generalRegister(16).low ==
    (stickyStatus |
     EECOP1Control::CAUSE_UNDERFLOW));
  REQUIRE(
    core.generalRegister(17).low ==
    (EECOP1Control::STATUS_FIXED |
     EECOP1Control::STICKY_UNDERFLOW));
  REQUIRE(
    core.generalRegister(18).low ==
    (stickyStatus |
     EECOP1Control::CAUSE_OVERFLOW));
  REQUIRE(core.generalRegister(19).low == stickyStatus);
  REQUIRE(core.cop1ControlRegister(31) == stickyStatus);

  const std::uint32_t chainAddresses[] = {
    result.load.entryPoint + 52,
    result.load.entryPoint + 56,
    result.load.entryPoint + 60,
    result.load.entryPoint + 64,
    result.load.entryPoint + 68,
    result.load.entryPoint + 72,
    result.load.entryPoint + 76,
    result.load.entryPoint + 80,
    result.load.entryPoint + 84,
    result.load.entryPoint + 88
  };
  const auto isChainAddress =
    [&chainAddresses](std::uint32_t address)
    {
      for (const std::uint32_t chainAddress :
           chainAddresses)
      {
        if (address == chainAddress)
        {
          return true;
        }
      }
      return false;
    };

  std::vector<NekoTraceEvent> admissions;
  std::vector<NekoTraceEvent> retirements;
  for (const NekoTraceEvent &event : system.trace())
  {
    const std::uint32_t address =
      static_cast<std::uint32_t>(event.value1);
    if (!isChainAddress(address))
    {
      continue;
    }
    if (event.type ==
          NekoTraceEventType::COP1StageTransition &&
        (event.value2 &
         NekoEETraceCOP1Stage::FROM_MASK) ==
          NekoEETraceCOP1Stage::NONE)
    {
      admissions.push_back(event);
    }
    else if (
      event.type == NekoTraceEventType::COP1Retired)
    {
      retirements.push_back(event);
    }
  }

  REQUIRE(admissions.size() == 10);
  REQUIRE(retirements.size() == 10);
  for (std::size_t index = 0; index < 10; ++index)
  {
    REQUIRE(
      static_cast<std::uint32_t>(
        admissions[index].value1) ==
      chainAddresses[index]);
    REQUIRE(
      static_cast<std::uint32_t>(
        retirements[index].value1) ==
      chainAddresses[index]);
  }
  const std::size_t forwardingConsumers[] = {
    1, 3, 5, 7, 8, 9
  };
  for (const std::size_t consumer :
       forwardingConsumers)
  {
    REQUIRE(
      admissions[consumer].masterCycle + 1 ==
      retirements[consumer - 1].masterCycle);
  }
}

TEST_CASE("PS2DEV COP1 divider guest overlaps initiation and visibility")
{
  NekoSystem system;
  system.startTrace();
  const EEGuestExecutionResult result =
    system.runELF(readGuest("cop1_dividers.elf"), 256);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  REQUIRE(result.execution.instructions == 17);
  REQUIRE_FALSE(result.execution.cycleLimitReached);
  REQUIRE(
    result.execution.programCounter ==
    EEGuestRuntime::RETURN_ADDRESS);

  const EECore &core = system.eeCore();
  for (const std::size_t registerIndex : {20, 21, 22})
  {
    REQUIRE(
      core.floatingPointRegister(registerIndex) ==
      UINT32_C(0x40400000));
  }
  for (const std::size_t registerIndex : {15, 16, 17})
  {
    REQUIRE(
      core.generalRegister(registerIndex).low ==
      UINT64_C(0x0000000040400000));
  }
  REQUIRE(
    core.generalRegister(18).low ==
    EECOP1Control::STATUS_FIXED);
  REQUIRE(
    core.cop1ControlRegister(31) ==
    EECOP1Control::STATUS_FIXED);

  const std::uint32_t dividerAddresses[] = {
    result.load.entryPoint + 28,
    result.load.entryPoint + 32,
    result.load.entryPoint + 36
  };
  const auto isDividerAddress =
    [&dividerAddresses](std::uint32_t address)
    {
      for (const std::uint32_t dividerAddress :
           dividerAddresses)
      {
        if (address == dividerAddress)
        {
          return true;
        }
      }
      return false;
    };

  std::vector<NekoTraceEvent> admissions;
  std::vector<NekoTraceEvent> retirements;
  for (const NekoTraceEvent &event : system.trace())
  {
    const std::uint32_t address =
      static_cast<std::uint32_t>(event.value1);
    if (!isDividerAddress(address))
    {
      continue;
    }
    if (event.type ==
          NekoTraceEventType::COP1StageTransition &&
        (event.value2 &
         NekoEETraceCOP1Stage::FROM_MASK) ==
          NekoEETraceCOP1Stage::NONE)
    {
      admissions.push_back(event);
    }
    else if (
      event.type == NekoTraceEventType::COP1Retired)
    {
      retirements.push_back(event);
    }
  }

  REQUIRE(admissions.size() == 3);
  REQUIRE(retirements.size() == 3);
  const std::uint64_t latencies[] = {14, 8, 8};
  for (std::size_t index = 0; index < 3; ++index)
  {
    REQUIRE(
      static_cast<std::uint32_t>(
        admissions[index].value1) ==
      dividerAddresses[index]);
    REQUIRE(
      admissions[index].value2 ==
      (NekoEETraceCOP1Stage::NONE |
       (NekoEETraceCOP1Stage::R <<
        NekoEETraceCOP1Stage::TO_SHIFT) |
       (latencies[index] <<
        NekoEETraceCOP1Stage::
          REMAINING_CYCLES_SHIFT)));
    REQUIRE(
      retirements[index].masterCycle ==
      admissions[index].masterCycle +
        latencies[index]);
  }
  REQUIRE(
    admissions[1].masterCycle ==
    admissions[0].masterCycle + 13);
  REQUIRE(
    retirements[0].masterCycle ==
    admissions[1].masterCycle + 1);
  REQUIRE(
    admissions[2].masterCycle ==
    admissions[1].masterCycle + 7);
  REQUIRE(
    retirements[1].masterCycle ==
    admissions[2].masterCycle + 1);
}

TEST_CASE("PS2DEV COP1 mixed guest is concurrent and deterministic")
{
  const std::vector<std::uint8_t> guest =
    readGuest("cop1_mixed_concurrent.elf");
  NekoSystem first;
  NekoSystem second;
  first.startTrace();
  second.startTrace();

  const EEGuestExecutionResult firstResult =
    first.runELF(guest, 512);
  const EEGuestExecutionResult secondResult =
    second.runELF(guest, 512);

  REQUIRE(firstResult.outcome == EEGuestOutcome::Completed);
  REQUIRE(firstResult.exitCode == 0);
  REQUIRE(firstResult.execution.instructions == 27);
  REQUIRE_FALSE(firstResult.execution.cycleLimitReached);
  REQUIRE(
    firstResult.execution.programCounter ==
    EEGuestRuntime::RETURN_ADDRESS);
  REQUIRE(
    firstResult.execution.masterCycles ==
    secondResult.execution.masterCycles);
  REQUIRE(
    firstResult.execution.eeCycles ==
    secondResult.execution.eeCycles);
  REQUIRE(
    firstResult.execution.instructions ==
    secondResult.execution.instructions);
  REQUIRE(firstResult.outcome == secondResult.outcome);
  REQUIRE(firstResult.exitCode == secondResult.exitCode);
  REQUIRE(
    first.eeCore().stateHash() ==
    second.eeCore().stateHash());
  REQUIRE(first.traceHash() == second.traceHash());
  REQUIRE(first.saveState() == second.saveState());

  const EECore &core = first.eeCore();
  REQUIRE(
    core.floatingPointRegister(4) ==
    UINT32_C(0x89abcdef));
  REQUIRE(
    core.floatingPointRegister(10) ==
    UINT32_C(0x40700000));
  REQUIRE(
    core.floatingPointRegister(11) ==
    UINT32_C(0x40a80000));
  REQUIRE(
    core.floatingPointRegister(12) ==
    UINT32_C(0xc0400000));
  REQUIRE(
    core.floatingPointRegister(13) ==
    UINT32_C(0xbfc00000));
  REQUIRE(
    core.floatingPointRegister(14) ==
    UINT32_C(0x40a80000));
  REQUIRE(
    core.floatingPointRegister(15) ==
    UINT32_C(0xc0100000));
  REQUIRE(
    core.generalRegister(10).low ==
    UINT64_C(0xffffffff89abcdef));
  REQUIRE(
    core.generalRegister(11).low ==
    UINT64_C(0x0000000040700000));
  REQUIRE(
    core.generalRegister(12).low ==
    UINT64_C(0x0000000040a80000));
  REQUIRE(
    core.generalRegister(13).low ==
    UINT64_C(0xffffffffc0400000));
  REQUIRE(
    core.generalRegister(14).low ==
    UINT64_C(0xffffffffbfc00000));
  REQUIRE(
    core.generalRegister(15).low ==
    UINT64_C(0x0000000040a80000));
  REQUIRE(
    core.generalRegister(16).low ==
    UINT64_C(0xffffffffc0100000));
  REQUIRE(
    core.generalRegister(17).low ==
    EECOP1Control::STATUS_FIXED);
  REQUIRE(
    core.cop1ControlRegister(31) ==
    EECOP1Control::STATUS_FIXED);

  const std::uint32_t payloadAddress =
    static_cast<std::uint32_t>(
      core.generalRegister(8).low);
  REQUIRE(
    first.eeBus().read32(payloadAddress + 16) ==
    UINT32_C(0x40a80000));
  REQUIRE(
    first.eeBus().read32(payloadAddress + 20) ==
    UINT32_C(0x40a80000));
  REQUIRE(
    second.eeBus().read32(payloadAddress + 16) ==
    UINT32_C(0x40a80000));
  REQUIRE(
    second.eeBus().read32(payloadAddress + 20) ==
    UINT32_C(0x40a80000));

  const std::uint32_t pairAddresses[] = {
    firstResult.load.entryPoint + 28,
    firstResult.load.entryPoint + 32,
    firstResult.load.entryPoint + 36,
    firstResult.load.entryPoint + 40,
    firstResult.load.entryPoint + 44,
    firstResult.load.entryPoint + 48,
    firstResult.load.entryPoint + 52,
    firstResult.load.entryPoint + 56,
    firstResult.load.entryPoint + 60,
    firstResult.load.entryPoint + 64
  };
  std::vector<NekoTraceEvent> admissions;
  for (const NekoTraceEvent &event : first.trace())
  {
    if (event.type !=
          NekoTraceEventType::COP1StageTransition ||
        (event.value2 &
         NekoEETraceCOP1Stage::FROM_MASK) !=
          NekoEETraceCOP1Stage::NONE)
    {
      continue;
    }
    const std::uint32_t address =
      static_cast<std::uint32_t>(event.value1);
    for (const std::uint32_t pairAddress :
         pairAddresses)
    {
      if (address == pairAddress)
      {
        admissions.push_back(event);
        break;
      }
    }
  }

  REQUIRE(admissions.size() == 10);
  for (std::size_t index = 0; index < 10; ++index)
  {
    REQUIRE(
      static_cast<std::uint32_t>(
        admissions[index].value1) ==
      pairAddresses[index]);
  }
  for (std::size_t index = 0; index < 10; index += 2)
  {
    REQUIRE(
      admissions[index].masterCycle ==
      admissions[index + 1].masterCycle);
  }

  struct MoveInterlock
  {
    std::uint32_t address;
    std::size_t admissionIndex;
    EEOperation blocker;
  };
  const MoveInterlock moveInterlocks[] = {
    {
      pairAddresses[1],
      1,
      EEOperation::AddSingleCOP1
    },
    {
      pairAddresses[2],
      2,
      EEOperation::AddSingleCOP1
    },
    {
      pairAddresses[4],
      4,
      EEOperation::ConvertWordToSingleCOP1
    },
    {
      pairAddresses[7],
      7,
      EEOperation::AddSingleCOP1
    },
    {
      pairAddresses[8],
      8,
      EEOperation::MultiplySingleCOP1
    }
  };
  for (const MoveInterlock &interlock : moveInterlocks)
  {
    std::size_t interlockCount = 0;
    for (const NekoTraceEvent &event : first.trace())
    {
      if (event.type ==
            NekoTraceEventType::COP1ResourceInterlock &&
          event.value0 == interlock.address &&
          event.masterCycle >
            admissions[interlock.admissionIndex]
              .masterCycle &&
          event.value2 ==
            static_cast<std::uint8_t>(
              interlock.blocker))
      {
        REQUIRE(
          event.masterCycle ==
          admissions[interlock.admissionIndex]
              .masterCycle + 1);
        ++interlockCount;
      }
    }
    REQUIRE(interlockCount == 1);
  }

  std::uint64_t firstAddRetirement = 0;
  for (const NekoTraceEvent &event : first.trace())
  {
    if (event.type == NekoTraceEventType::COP1Retired &&
        static_cast<std::uint32_t>(event.value1) ==
          pairAddresses[0])
    {
      firstAddRetirement = event.masterCycle;
    }
  }
  REQUIRE(firstAddRetirement != 0);
  REQUIRE(
    admissions[2].masterCycle ==
    firstAddRetirement);
}

TEST_CASE("PS2DEV EE ELF guest controls and polls vector units through COP2")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("cop2_control.elf"), 96);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(system.vu0().intRegisterValue(3) == UINT16_C(0x7f01));
  REQUIRE(system.eeCore().generalRegister(11).low == 1);
  REQUIRE(system.eeCore().generalRegister(12).low == 1);
  REQUIRE(system.vu1().getState() == VPU_STATE_STOP);
  REQUIRE(system.vu1().stoppedByForceBreak());
}

TEST_CASE("PS2DEV EE ELF guest transfers vectors through VU0 COP2")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("cop2_transfer.elf"), 64);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  const FPRegister *vf1 = system.vu0().fpRegisterValue(1);
  const FPRegister *vf2 = system.vu0().fpRegisterValue(2);
  REQUIRE(vf1->x.bits() == UINT32_C(0x33221100));
  REQUIRE(vf1->y.bits() == UINT32_C(0x77665544));
  REQUIRE(vf1->z.bits() == UINT32_C(0xbbaa9988));
  REQUIRE(vf1->w.bits() == UINT32_C(0xffeeddcc));
  REQUIRE(vf2->x.bits() == vf1->x.bits());
  REQUIRE(vf2->y.bits() == vf1->y.bits());
  REQUIRE(vf2->z.bits() == vf1->z.bits());
  REQUIRE(vf2->w.bits() == vf1->w.bits());
}

TEST_CASE("PS2DEV EE ELF guest calls VU0 microprograms through COP2")
{
  NekoSystem system;
  uploadVectorCopyProgram(&system.vu0());

  const EEGuestExecutionResult result =
    system.runELF(readGuest("vcallms.elf"), 128);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  REQUIRE(result.execution.instructions == 36);
  const FPRegister *vf1 = system.vu0().fpRegisterValue(1);
  const FPRegister *vf2 = system.vu0().fpRegisterValue(2);
  REQUIRE(vf2->x.bits() == vf1->x.bits());
  REQUIRE(vf2->y.bits() == vf1->y.bits());
  REQUIRE(vf2->z.bits() == vf1->z.bits());
  REQUIRE(vf2->w.bits() == vf1->w.bits());
  REQUIRE_FALSE(system.vu0().clockActive());
}

TEST_CASE("PS2DEV EE ELF guest executes VU0 macro arithmetic")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(
      readGuest("vu_macro_arithmetic.elf"),
      128);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  const FPRegister *dependent =
    system.vu0().fpRegisterValue(4);
  REQUIRE(dependent->x == 1);
  REQUIRE(dependent->y == 2);
  REQUIRE(dependent->z == 3);
  REQUIRE(dependent->w == 4);
  const FPRegister *broadcast =
    system.vu0().fpRegisterValue(5);
  REQUIRE(broadcast->x == 6);
  REQUIRE(broadcast->y == 16);
  REQUIRE(broadcast->z == 26);
  REQUIRE(broadcast->w == 36);
}

TEST_CASE("PS2DEV EE ELF guest executes remaining VU0 macro families")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(
      readGuest("vu_macro_families.elf"),
      512);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  REQUIRE(result.execution.instructions == 107);
  REQUIRE(system.vu0().intRegisterValue(3) == 12);
  const FPRegister *moved =
    system.vu0().fpRegisterValue(8);
  REQUIRE(moved->x == 20);
  REQUIRE(moved->y == 3);
  REQUIRE(moved->z == 24);
  REQUIRE(moved->w == 10);
}

TEST_CASE("PS2DEV EE ELF guest configures VIF1 DMA")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("vif1_dma.elf"), 128);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(system.vif1().wordsIngested() == 4);
  REQUIRE(system.vif1().fifoQuadwordCount() == 0);
  REQUIRE(system.vif1DMAC().transferredQuadwordCount() == 1);
  REQUIRE(
    (system.vif1DMAC().channelControl() &
     GIFDMACChannelControl::START) == 0);
  REQUIRE(
    (system.dmacController().status() &
     DMACStatus::CHANNEL_1) != 0);
  REQUIRE(
    (system.dmacController().status() &
     DMACStatus::CHANNEL_1_MASK) != 0);
  REQUIRE(system.dmacController().interruptPending());
}

TEST_CASE("Tracing does not alter guest execution or machine state")
{
  struct GuestRun
  {
    const char *fileName;
    std::uint64_t maxMasterCycles;
  };
  const GuestRun guests[] = {
    {"cop1_mixed_concurrent.elf", 512},
    {"rotation_vu1.elf", 4096}
  };

  for (const GuestRun &guest : guests)
  {
    CAPTURE(guest.fileName);
    const std::vector<std::uint8_t> image =
      readGuest(guest.fileName);
    NekoSystem untraced;
    NekoSystem traced;
    traced.startTrace();

    const EEGuestExecutionResult untracedResult =
      untraced.runELF(image, guest.maxMasterCycles);
    const EEGuestExecutionResult tracedResult =
      traced.runELF(image, guest.maxMasterCycles);

    REQUIRE_FALSE(untraced.traceEnabled());
    REQUIRE(traced.traceEnabled());
    REQUIRE_FALSE(traced.trace().empty());
    REQUIRE(
      untracedResult.load.entryPoint ==
      tracedResult.load.entryPoint);
    REQUIRE(
      untracedResult.load.loadedSegments ==
      tracedResult.load.loadedSegments);
    REQUIRE(
      untracedResult.load.fileBytes ==
      tracedResult.load.fileBytes);
    REQUIRE(
      untracedResult.load.zeroedBytes ==
      tracedResult.load.zeroedBytes);
    REQUIRE(
      untracedResult.execution.masterCycles ==
      tracedResult.execution.masterCycles);
    REQUIRE(
      untracedResult.execution.eeCycles ==
      tracedResult.execution.eeCycles);
    REQUIRE(
      untracedResult.execution.instructions ==
      tracedResult.execution.instructions);
    REQUIRE(
      untracedResult.execution.cycleLimitReached ==
      tracedResult.execution.cycleLimitReached);
    REQUIRE(
      untracedResult.execution.state ==
      tracedResult.execution.state);
    REQUIRE(
      untracedResult.execution.stopReason ==
      tracedResult.execution.stopReason);
    REQUIRE(
      untracedResult.execution.programCounter ==
      tracedResult.execution.programCounter);
    REQUIRE(
      untracedResult.execution.pendingException ==
      tracedResult.execution.pendingException);
    REQUIRE(
      untracedResult.execution.exceptionAddress ==
      tracedResult.execution.exceptionAddress);
    REQUIRE(untracedResult.outcome == tracedResult.outcome);
    REQUIRE(untracedResult.exitCode == tracedResult.exitCode);
    REQUIRE(
      untraced.eeCore().stateHash() ==
      traced.eeCore().stateHash());
    REQUIRE(untraced.saveState() == traced.saveState());
  }
}

TEST_CASE("PS2DEV EE ELF guest renders a rotating VU1 triangle")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("rotation_vu1.elf"), 4096);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  REQUIRE(result.execution.instructions == 1361);
  REQUIRE(system.vu1().getState() == VPU_STATE_READY);
  REQUIRE_FALSE(system.gifPath1().path1TransferActive());
  REQUIRE(system.gifPath1().transferredQuadwordCount() == 12);
  REQUIRE(system.vif1DMAC().transferredQuadwordCount() == 38);
  REQUIRE(system.vif1().wordsIngested() == 152);
  REQUIRE(system.gs().triangleCount() == 1);
  const GIFQuadword first =
    system.vu1().readDataQuadword(12);
  const GIFQuadword second =
    system.vu1().readDataQuadword(14);
  const GIFQuadword third =
    system.vu1().readDataQuadword(16);
  REQUIRE(first[0] == UINT32_C(0x00008000));
  REQUIRE(first[1] == UINT32_C(0x00007caf));
  REQUIRE(first[2] == UINT32_C(0x00000c00));
  REQUIRE(second[0] == UINT32_C(0x000071db));
  REQUIRE(second[1] == UINT32_C(0x00008ad4));
  REQUIRE(second[2] == UINT32_C(0x00000c00));
  REQUIRE(third[0] == UINT32_C(0x00007783));
  REQUIRE(third[1] == UINT32_C(0x0000907c));
  REQUIRE(third[2] == UINT32_C(0x00000c00));
  REQUIRE(system.gs().pixelWriteCount() == 20566);
  REQUIRE(
    system.gs().framebufferHash(0, 640, 448) ==
    UINT64_C(0xdf0bce57b91fbbd3));
}

TEST_CASE("PS2DEV EE ELF guest renders the point and sprite scene")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("point_sprite.elf"), 40000);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  REQUIRE(system.gifPath3().guestFIFOQuadwordCount() == 0);
  REQUIRE(system.gifPath3().transferredQuadwordCount() == 226);
  REQUIRE(system.gs().pointCount() == 96);
  REQUIRE(system.gs().lineCount() == 0);
  REQUIRE(system.gs().spriteCount() == 7);
  REQUIRE(system.gs().triangleCount() == 0);
  REQUIRE(
    system.gs().framebufferHash(0, 640, 448) ==
    UINT64_C(0xc1adcf6554c82b99));
}

TEST_CASE("PS2DEV EE ELF guest writes VIF and GIF FIFOs")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("fifo.elf"), 128);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(system.vif0().wordsIngested() == 4);
  REQUIRE(system.vif1().wordsIngested() == 4);
  REQUIRE(system.vif0().fifoQuadwordCount() == 0);
  REQUIRE(system.vif1().fifoQuadwordCount() == 0);
  REQUIRE(system.gifPath3().guestFIFOQuadwordCount() == 0);
  REQUIRE(system.gifPath3().transferredQuadwordCount() == 1);
}

TEST_CASE("PS2DEV EE ELF guest drives mapped device registers")
{
  NekoSystem system;
  const EEGuestExecutionResult result =
    system.runELF(readGuest("mmio.elf"), 128);

  REQUIRE(result.outcome == EEGuestOutcome::Completed);
  REQUIRE(
    system.interruptController().mask() ==
    EEInterruptSource::mask(EEInterruptSource::VIF0));
  REQUIRE(system.dmacController().control() == 1);
  REQUIRE(system.gs().hostInterfaceReversed());
}

TEST_CASE("PS2DEV EE ELF guest runs through frontend support")
{
  const neko_frontend::ELFRunReport report =
    neko_frontend::runELFFile(
      guestPath("arithmetic.elf"),
      128);

  REQUIRE(report.result.outcome == EEGuestOutcome::Completed);
  REQUIRE(report.result.exitCode == 0);
  REQUIRE(report.hostExitCode == 0);
  REQUIRE(
    report.diagnostic.find("outcome=completed") !=
    std::string::npos);
}
