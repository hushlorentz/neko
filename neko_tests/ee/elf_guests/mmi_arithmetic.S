  .set noreorder
  .text
  .globl _start
  .ent _start

  .macro load_qword reg, label
  lui $20, %hi(\label)
  addiu $20, $20, %lo(\label)
  lq \reg, 0($20)
  .endm

  .macro check_qword reg, label, code
  lui $20, %hi(scratch)
  addiu $20, $20, %lo(scratch)
  sq \reg, 0($20)
  lui $22, %hi(\label)
  addiu $22, $22, %lo(\label)
  ld $21, 0($20)
  ld $23, 0($22)
  addiu $v0, $zero, \code
  bne $21, $23, fail
  nop
  ld $21, 8($20)
  ld $23, 8($22)
  bne $21, $23, fail
  nop
  .endm

_start:
  load_qword $8, word_source
  load_qword $9, word_target

  paddw $10, $8, $9
  check_qword $10, paddw_expected, 1

  pceqw $10, $8, $9
  check_qword $10, pceqw_expected, 2

  pcgtw $10, $8, $9
  check_qword $10, pcgtw_expected, 3

  pmaxw $10, $8, $9
  check_qword $10, pmaxw_expected, 4

  pabsw $10, $8
  check_qword $10, pabsw_expected, 5

  load_qword $8, halfword_source
  load_qword $9, halfword_target
  paddsh $10, $8, $9
  check_qword $10, paddsh_expected, 6

  load_qword $8, plzcw_source
  load_qword $10, plzcw_initial
  plzcw $10, $8
  check_qword $10, plzcw_expected, 7

  or $v0, $zero, $zero
fail:
  jr $ra
  nop

  .end _start

  .data
  .align 4
word_source:
  .word 0x00000001, 0x7fffffff, 0xffffffff, 0x80000000
word_target:
  .word 0x00000000, 0x80000000, 0xffffffff, 0xffffffff
paddw_expected:
  .word 0x00000001, 0xffffffff, 0xfffffffe, 0x7fffffff
pceqw_expected:
  .word 0x00000000, 0x00000000, 0xffffffff, 0x00000000
pcgtw_expected:
  .word 0xffffffff, 0xffffffff, 0x00000000, 0x00000000
pmaxw_expected:
  .word 0x00000001, 0x7fffffff, 0xffffffff, 0xffffffff
pabsw_expected:
  .word 0x00000001, 0x7fffffff, 0x00000001, 0x7fffffff
halfword_source:
  .word 0x807f807f, 0xff01817e, 0x807f807f, 0xff01817e
halfword_target:
  .word 0x0000ff01, 0x01ffff01, 0x0000ff01, 0x01ffff01
paddsh_expected:
  .word 0x807f8000, 0x0100807f, 0x807f8000, 0x0100807f
plzcw_source:
  .word 0xff0ff00f, 0x000fff0f, 0x9abcdef0, 0x12345678
plzcw_initial:
  .word 0xffffffff, 0xffffffff, 0x76543210, 0xfedcba98
plzcw_expected:
  .word 0x00000007, 0x0000000b, 0x76543210, 0xfedcba98
scratch:
  .space 16
