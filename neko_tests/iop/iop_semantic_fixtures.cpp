#include "iop_semantic_fixtures.hpp"

#include <algorithm>

#include "catch.hpp"
#include "iop_bus.hpp"
#include "iop_core.hpp"

namespace
{
  using namespace IOPSemanticFixtures;
  using Op = IOPOperation;

  // Independently authored words and results, not encoder/decoder output.
  // R30xx manual: Appendix A; load/branch delays pp. 19-20, 216, 257;
  // RFE p. 325. Timing totals describe Neko's functional runner, not silicon.
  Fixture arithmetic()
  {
    Fixture fixture;
    fixture.name = "wrapping arithmetic, shifts, and signed/unsigned comparisons";
    fixture.code = {
      {0x1000, 0x2408ffff, Op::AddImmediateUnsigned}, // addiu r8, r0, -1
      {0x1004, 0x3c098000, Op::LoadUpperImmediate}, // lui r9, 0x8000
      {0x1008, 0x01095021, Op::AddUnsigned}, // addu r10, r8, r9
      {0x100c, 0x01285823, Op::SubtractUnsigned}, // subu r11, r9, r8
      {0x1010, 0x000967c3, Op::ShiftRightArithmetic}, // sra r12, r9, 31
      {0x1014, 0x00096fc2, Op::ShiftRightLogical}, // srl r13, r9, 31
      {0x1018, 0x000d7100, Op::ShiftLeftLogical}, // sll r14, r13, 4
      {0x101c, 0x0100782a, Op::SetLessThan}, // slt r15, r8, r0
      {0x1020, 0x0100802b, Op::SetLessThanUnsigned}, // sltu r16, r8, r0
      {0x1024, 0x311180ff, Op::AndImmediate}, // andi r17, r8, 0x80ff
      {0x1028, 0x36320100, Op::OrImmediate}, // ori r18, r17, 0x0100
      {0x102c, 0x3a53ffff, Op::XorImmediate}, // xori r19, r18, 0xffff
      {0x1030, 0x0109a027, Op::Nor}, // nor r20, r8, r9
      {0x1034, 0x0109a826, Op::Xor}, // xor r21, r8, r9
      {0x1038, 0x29160001, Op::SetLessThanImmediate}, // slti r22, r8, 1
      {0x103c, 0x2c17ffff, Op::SetLessThanImmediateUnsigned}, // sltiu r23, r0, -1
      {0x1040, 0x24001234, Op::AddImmediateUnsigned}, // addiu r0, r0, 0x1234
      {0x1044, 0xac0a2000, Op::StoreWord}, // sw r10, 0x2000(r0)
      {0x1048, 0xac0b2004, Op::StoreWord}, // sw r11, 0x2004(r0)
      {0x104c, 0x03e00008, Op::JumpRegister}, // jr r31
      {0x1050, 0x24020000, Op::AddImmediateUnsigned} // addiu r2, r0, 0
    };
    fixture.initialMemory = {
      {0x1ffc, 0xdeadbeef}, {0x2000, 0xaaaaaaaa},
      {0x2004, 0xbbbbbbbb}, {0x2008, 0xcafebabe}
    };
    fixture.expected.instructions = 21;
    fixture.expected.cycles = 21;
    fixture.expected.masterCycles = 168;
    fixture.expected.registers = {
      {8, 0xffffffff}, {9, 0x80000000}, {10, 0x7fffffff},
      {11, 0x80000001}, {12, 0xffffffff}, {13, 1}, {14, 16},
      {15, 1}, {17, 0x80ff}, {18, 0x81ff}, {19, 0x7e00},
      {21, 0x7fffffff}, {22, 1}, {23, 1}
    };
    fixture.expected.changedMemory = {
      {0x2000, 0x7fffffff}, {0x2004, 0x80000001}
    };
    return fixture;
  }

  Fixture multiplyDivide()
  {
    Fixture fixture;
    fixture.name = "signed HI/LO product, quotient, and remainder";
    fixture.code = {
      {0x1000, 0x2408fff9, Op::AddImmediateUnsigned}, // addiu r8, r0, -7
      {0x1004, 0x24090003, Op::AddImmediateUnsigned}, // addiu r9, r0, 3
      {0x1008, 0x01090018, Op::Multiply}, // mult r8, r9
      {0x100c, 0x00005012, Op::MoveFromLO}, // mflo r10
      {0x1010, 0x00005810, Op::MoveFromHI}, // mfhi r11
      {0x1014, 0, Op::Nop},
      {0x1018, 0, Op::Nop},
      {0x101c, 0x0109001a, Op::Divide}, // div r8, r9
      {0x1020, 0x00006012, Op::MoveFromLO}, // mflo r12
      {0x1024, 0x00006810, Op::MoveFromHI}, // mfhi r13
      {0x1028, 0, Op::Nop},
      {0x102c, 0, Op::Nop},
      {0x1030, 0x03e00008, Op::JumpRegister}, // jr r31
      {0x1034, 0, Op::Nop}
    };
    fixture.expected.instructions = 14;
    fixture.expected.cycles = 14;
    fixture.expected.masterCycles = 112;
    fixture.expected.registers = {
      {8, 0xfffffff9}, {9, 3}, {10, 0xffffffeb}, {11, 0xffffffff},
      {12, 0xfffffffe}, {13, 0xffffffff}
    };
    fixture.expected.hi = 0xffffffff;
    fixture.expected.lo = 0xfffffffe;
    return fixture;
  }

  Fixture controlFlow()
  {
    Fixture fixture;
    fixture.name = "backward loop, taken/untaken branches, and linked call";
    fixture.code = {
      {0x1000, 0x03e08021, Op::AddUnsigned}, // addu r16, r31, r0
      {0x1004, 0x24080003, Op::AddImmediateUnsigned}, // addiu r8, r0, 3
      {0x1008, 0x25290001, Op::AddImmediateUnsigned}, // addiu r9, r9, 1
      {0x100c, 0x1528fffe, Op::BranchNotEqual}, // bne r9, r8, 0x1008
      {0x1010, 0x254a0002, Op::AddImmediateUnsigned}, // addiu r10, r10, 2
      {0x1014, 0x11280002, Op::BranchEqual}, // beq r9, r8, 0x1020
      {0x1018, 0x240b0055, Op::AddImmediateUnsigned}, // addiu r11, r0, 0x55
      {0x101c, 0x24020001, Op::AddImmediateUnsigned}, // skipped failure
      {0x1020, 0x0c000410, Op::JumpAndLink}, // jal 0x1040
      {0x1024, 0x240c0007, Op::AddImmediateUnsigned}, // addiu r12, r0, 7
      {0x1028, 0x0200f821, Op::AddUnsigned}, // addu r31, r16, r0
      {0x102c, 0x03e00008, Op::JumpRegister}, // jr r31
      {0x1030, 0, Op::Nop},
      {0x1040, 0x03e06821, Op::AddUnsigned}, // addu r13, r31, r0
      {0x1044, 0x03e00008, Op::JumpRegister}, // jr r31
      {0x1048, 0x258e0001, Op::AddImmediateUnsigned} // addiu r14, r12, 1
    };
    fixture.expected.instructions = 21;
    fixture.expected.cycles = 21;
    fixture.expected.masterCycles = 168;
    fixture.expected.registers = {
      {8, 3}, {9, 3}, {10, 6}, {11, 0x55}, {12, 7},
      {13, 0x1028}, {14, 8}, {16, 0xfffffffc}
    };
    return fixture;
  }

  Fixture memory()
  {
    Fixture fixture;
    fixture.name = "little-endian widths, sign extension, and delayed loads";
    fixture.code = {
      {0x1000, 0x24082000, Op::AddImmediateUnsigned}, // addiu r8, r0, 0x2000
      {0x1004, 0x8d090000, Op::LoadWord}, // lw r9, 0(r8)
      {0x1008, 0x01205021, Op::AddUnsigned}, // addu r10, r9, r0 (old value)
      {0x100c, 0x01205821, Op::AddUnsigned}, // addu r11, r9, r0
      {0x1010, 0x810c0000, Op::LoadByte}, // lb r12, 0(r8)
      {0x1014, 0x910d0001, Op::LoadByteUnsigned}, // lbu r13, 1(r8)
      {0x1018, 0x850e0000, Op::LoadHalfword}, // lh r14, 0(r8)
      {0x101c, 0x950f0002, Op::LoadHalfwordUnsigned}, // lhu r15, 2(r8)
      {0x1020, 0, Op::Nop},
      {0x1024, 0xa10c0004, Op::StoreByte}, // sb r12, 4(r8)
      {0x1028, 0xa50e0006, Op::StoreHalfword}, // sh r14, 6(r8)
      {0x102c, 0xad0b0008, Op::StoreWord}, // sw r11, 8(r8)
      {0x1030, 0x8d000008, Op::LoadWord}, // lw r0, 8(r8)
      {0x1034, 0x03e00008, Op::JumpRegister}, // jr r31
      {0x1038, 0, Op::Nop}
    };
    fixture.initialMemory = {
      {0x1ffc, 0xdeadbeef}, {0x2000, 0x44338281},
      {0x2004, 0x11223344}, {0x2008, 0xaaaaaaaa}, {0x200c, 0xcafebabe}
    };
    fixture.expected.instructions = 15;
    fixture.expected.cycles = 15;
    fixture.expected.masterCycles = 120;
    fixture.expected.registers = {
      {8, 0x2000}, {9, 0x44338281}, {11, 0x44338281},
      {12, 0xffffff81}, {13, 0x82}, {14, 0xffff8281}, {15, 0x4433}
    };
    fixture.expected.changedMemory = {
      {0x2004, 0x82813381}, {0x2008, 0x44338281}
    };
    return fixture;
  }

  Fixture mergeMemory()
  {
    Fixture fixture;
    fixture.name = "paired unaligned merge loads and byte-enabled stores";
    fixture.code = {
      {0x1000, 0x3c08aabb, Op::LoadUpperImmediate}, // lui r8, 0xaabb
      {0x1004, 0x3508ccdd, Op::OrImmediate}, // ori r8, r8, 0xccdd
      {0x1008, 0x88082004, Op::LoadWordLeft}, // lwl r8, 0x2004(r0)
      {0x100c, 0x98082001, Op::LoadWordRight}, // lwr r8, 0x2001(r0)
      {0x1010, 0, Op::Nop},
      {0x1014, 0xa8082014, Op::StoreWordLeft}, // swl r8, 0x2014(r0)
      {0x1018, 0xb8082011, Op::StoreWordRight}, // swr r8, 0x2011(r0)
      {0x101c, 0x8c092010, Op::LoadWord}, // lw r9, 0x2010(r0)
      {0x1020, 0x8c0a2014, Op::LoadWord}, // lw r10, 0x2014(r0)
      {0x1024, 0, Op::Nop},
      {0x1028, 0x03e00008, Op::JumpRegister}, // jr r31
      {0x102c, 0, Op::Nop}
    };
    fixture.initialMemory = {
      {0x2000, 0x44332211}, {0x2004, 0x88776655}, {0x200c, 0xcafebabe},
      {0x2010, 0xaabbccdd}, {0x2014, 0xdeadbeef}, {0x2018, 0xfeedface}
    };
    fixture.expected.instructions = 12;
    fixture.expected.cycles = 12;
    fixture.expected.masterCycles = 96;
    fixture.expected.registers = {
      {8, 0x55443322}, {9, 0x443322dd}, {10, 0xdeadbe55}
    };
    fixture.expected.changedMemory = {
      {0x2010, 0x443322dd}, {0x2014, 0xdeadbe55}
    };
    return fixture;
  }

  Fixture cop0()
  {
    Fixture fixture;
    fixture.name = "delayed COP0 reads, software pending bits, EPC, and RFE";
    fixture.code = {
      {0x1000, 0x24080034, Op::AddImmediateUnsigned}, // addiu r8, r0, 0x34
      {0x1004, 0x40886000, Op::MoveToCOP0}, // mtc0 r8, Status
      {0x1008, 0x40096000, Op::MoveFromCOP0}, // mfc0 r9, Status
      {0x100c, 0x01205021, Op::AddUnsigned}, // addu r10, r9, r0 (old value)
      {0x1010, 0x01205821, Op::AddUnsigned}, // addu r11, r9, r0
      {0x1014, 0x3c0c89ab, Op::LoadUpperImmediate}, // lui r12, 0x89ab
      {0x1018, 0x358ccdef, Op::OrImmediate}, // ori r12, r12, 0xcdef
      {0x101c, 0x408c7000, Op::MoveToCOP0}, // mtc0 r12, EPC
      {0x1020, 0x400d7000, Op::MoveFromCOP0}, // mfc0 r13, EPC
      {0x1024, 0, Op::Nop},
      {0x1028, 0x240e0300, Op::AddImmediateUnsigned}, // addiu r14, r0, 0x300
      {0x102c, 0x408e6800, Op::MoveToCOP0}, // mtc0 r14, Cause
      {0x1030, 0x400f6800, Op::MoveFromCOP0}, // mfc0 r15, Cause
      {0x1034, 0, Op::Nop},
      {0x1038, 0x42000010, Op::ReturnFromException}, // rfe
      {0x103c, 0x40116000, Op::MoveFromCOP0}, // mfc0 r17, Status
      {0x1040, 0, Op::Nop},
      {0x1044, 0xac0b2000, Op::StoreWord}, // sw r11, 0x2000(r0)
      {0x1048, 0xac0d2004, Op::StoreWord}, // sw r13, 0x2004(r0)
      {0x104c, 0xac0f2008, Op::StoreWord}, // sw r15, 0x2008(r0)
      {0x1050, 0xac11200c, Op::StoreWord}, // sw r17, 0x200c(r0)
      {0x1054, 0x03e00008, Op::JumpRegister}, // jr r31
      {0x1058, 0, Op::Nop}
    };
    fixture.initialMemory = {
      {0x1ffc, 0xdeadbeef}, {0x2000, 0}, {0x2004, 0},
      {0x2008, 0}, {0x200c, 0}, {0x2010, 0xcafebabe}
    };
    fixture.expected.instructions = 23;
    fixture.expected.cycles = 23;
    fixture.expected.masterCycles = 184;
    fixture.expected.status = 0x0020003d;
    fixture.expected.cause = 0x300;
    fixture.expected.exceptionProgramCounter = 0x89abcdef;
    fixture.expected.registers = {
      {8, 0x34}, {9, 0x00200034}, {11, 0x00200034}, {12, 0x89abcdef},
      {13, 0x89abcdef}, {14, 0x300}, {15, 0x300}, {17, 0x0020003d}
    };
    fixture.expected.changedMemory = {
      {0x2000, 0x00200034}, {0x2004, 0x89abcdef},
      {0x2008, 0x300}, {0x200c, 0x0020003d}
    };
    return fixture;
  }

  Fixture reservedAfterLoad()
  {
    Fixture fixture;
    fixture.name = "reserved decode commits the older delayed load";
    fixture.code = {
      {0x1000, 0x24082000, Op::AddImmediateUnsigned}, // addiu r8, r0, 0x2000
      {0x1004, 0x8d090000, Op::LoadWord}, // lw r9, 0(r8)
      {0x1008, 0xffffffff, Op::None, IOPDecodeDisposition::Reserved},
      {0x100c, 0x24020007, Op::AddImmediateUnsigned} // unreachable failure
    };
    fixture.initialMemory = {{0x2000, 0xdeadbeef}};
    fixture.expected.instructions = 2;
    fixture.expected.cycles = 3;
    fixture.expected.masterCycles = 24;
    fixture.expected.outcome = IOPProgramOutcome::Exception;
    fixture.expected.programCounter = 0xbfc00180;
    fixture.expected.exception.kind = IOPException::ReservedInstruction;
    fixture.expected.cause = 0x28;
    fixture.expected.exceptionProgramCounter = 0x1008;
    fixture.expected.registers = {{8, 0x2000}, {9, 0xdeadbeef}};
    return fixture;
  }

  Fixture overflowInDelaySlot()
  {
    Fixture fixture;
    fixture.name = "trapping arithmetic cancels a pending branch";
    fixture.code = {
      {0x1000, 0x3c087fff, Op::LoadUpperImmediate}, // lui r8, 0x7fff
      {0x1004, 0x3508ffff, Op::OrImmediate}, // ori r8, r8, 0xffff
      {0x1008, 0x10000001, Op::BranchEqual}, // beq r0, r0, 0x1010
      {0x100c, 0x21090001, Op::AddImmediate}, // addi r9, r8, 1
      {0x1010, 0x24020007, Op::AddImmediateUnsigned} // unreachable failure
    };
    fixture.expected.instructions = 3;
    fixture.expected.cycles = 4;
    fixture.expected.masterCycles = 32;
    fixture.expected.outcome = IOPProgramOutcome::Exception;
    fixture.expected.programCounter = 0xbfc00180;
    fixture.expected.exception = {IOPException::ArithmeticOverflow, 0, true, 0};
    fixture.expected.cause = 0x80000030;
    fixture.expected.exceptionProgramCounter = 0x1008;
    fixture.expected.registers = {{8, 0x7fffffff}};
    return fixture;
  }

  void loadWord(IOPBus &bus, IOPAddress address, IOPWord word)
  {
    const std::uint8_t bytes[] = {
      static_cast<std::uint8_t>(word),
      static_cast<std::uint8_t>(word >> 8),
      static_cast<std::uint8_t>(word >> 16),
      static_cast<std::uint8_t>(word >> 24)
    };
    CAPTURE(address);
    REQUIRE(bus.loadPhysical(address, bytes, sizeof(bytes)) ==
      IOPBusStatus::Completed);
  }
}

const std::array<IOPSemanticFixtures::Fixture, 8> &IOPSemanticFixtures::all()
{
  static const std::array<Fixture, 8> fixtures = {{
    arithmetic(), multiplyDivide(), controlFlow(), memory(),
    mergeMemory(), cop0(), reservedAfterLoad(), overflowInDelaySlot()
  }};
  return fixtures;
}

void IOPSemanticFixtures::load(IOPBus &bus, const Fixture &fixture)
{
  for (const auto &instruction : fixture.code)
  {
    loadWord(bus, instruction.address, instruction.word);
  }
  for (const auto &word : fixture.initialMemory)
  {
    loadWord(bus, word.address, word.value);
  }
}

void IOPSemanticFixtures::requireState(
  IOPBus &bus, const IOPCore &core, const Fixture &fixture)
{
  const auto &expected = fixture.expected;
  REQUIRE(core.elapsedCycles() == expected.cycles);
  REQUIRE(core.retiredInstructions() == expected.instructions);
  REQUIRE(core.programCounter() == expected.programCounter);
  REQUIRE(core.status() == expected.status);
  REQUIRE(core.cause() == expected.cause);
  REQUIRE(core.exceptionProgramCounter() == expected.exceptionProgramCounter);
  REQUIRE(core.badVirtualAddress() == expected.exception.badVirtualAddress);
  REQUIRE(core.hi() == expected.hi);
  REQUIRE(core.lo() == expected.lo);
  REQUIRE_FALSE(core.branchContinuation().active);
  REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::None);

  std::array<IOPWord, IOPCore::GENERAL_REGISTER_COUNT> registers = {};
  registers[29] = fixture.config.stackPointer;
  registers[31] = fixture.config.returnAddress;
  for (const auto &value : expected.registers)
  {
    registers.at(value.index) = value.value;
  }
  for (std::size_t index = 0; index < registers.size(); ++index)
  {
    CAPTURE(index);
    REQUIRE(core.generalRegister(index) == registers[index]);
  }
  for (const auto &initial : fixture.initialMemory)
  {
    CAPTURE(initial.address);
    const auto changed = std::find_if(
      expected.changedMemory.begin(), expected.changedMemory.end(),
      [&initial](const MemoryWord &word)
      {
        return word.address == initial.address;
      });
    const IOPWord value = changed == expected.changedMemory.end()
      ? initial.value : changed->value;
    const auto actual = bus.read32(initial.address);
    REQUIRE(actual.status == IOPBusStatus::Completed);
    REQUIRE(actual.value == value);
  }
}
