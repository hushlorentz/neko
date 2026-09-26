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
  load_qword $8, marker_source
  load_qword $9, marker_target

  pextlb $10, $8, $9
  check_qword $10, pextlb_expected, 1

  load_qword $8, pack_source
  load_qword $9, pack_target
  ppach $10, $8, $9
  check_qword $10, ppach_expected, 2

  load_qword $8, marker_source
  load_qword $9, marker_target
  pcpyud $10, $8, $9
  check_qword $10, pcpyud_expected, 3

  pexeh $10, $9
  check_qword $10, pexeh_expected, 4

  prevh $10, $9
  check_qword $10, prevh_expected, 5

  load_qword $9, packed_pixels
  pext5 $10, $9
  check_qword $10, pext5_expected, 6

  load_qword $9, halfword_shifts
  psllh $10, $9, 1
  check_qword $10, psllh_expected, 7

  load_qword $9, word_shifts
  psraw $10, $9, 1
  check_qword $10, psraw_expected, 8

  load_qword $8, variable_counts
  load_qword $9, variable_values
  psllvw $10, $9, $8
  check_qword $10, psllvw_expected, 9

  addiu $11, $zero, 1
  mtsab $11, 0
  load_qword $8, funnel_source
  load_qword $9, funnel_target
  qfsrv $10, $8, $9
  check_qword $10, qfsrv_expected, 10

  or $v0, $zero, $zero
fail:
  jr $ra
  nop

  .end _start

  .data
  .align 4
marker_source:
  .word 0x13121110, 0x17161514, 0xf3f2f1f0, 0xf7f6f5f4
marker_target:
  .word 0x03020100, 0x07060504, 0xe3e2e1e0, 0xe7e6e5e4
pextlb_expected:
  .word 0x11011000, 0x13031202, 0x15051404, 0x17071606
pack_source:
  .word 0xa111a010, 0xa313a212, 0xa515a414, 0xa717a616
pack_target:
  .word 0xb121b020, 0xb323b222, 0xb525b424, 0xb727b626
ppach_expected:
  .word 0xb222b020, 0xb626b424, 0xa212a010, 0xa616a414
pcpyud_expected:
  .word 0xf3f2f1f0, 0xf7f6f5f4, 0xe3e2e1e0, 0xe7e6e5e4
pexeh_expected:
  .word 0x03020504, 0x07060100, 0xe3e2e5e4, 0xe7e6e1e0
prevh_expected:
  .word 0x05040706, 0x01000302, 0xe5e4e7e6, 0xe1e0e3e2
packed_pixels:
  .word 0xa5a50000, 0xa5a57fff, 0xa5a58000, 0xa5a5d6b3
pext5_expected:
  .word 0x00000000, 0x00f8f8f8, 0x80000000, 0x80a8a898
halfword_shifts:
  .word 0x80010001, 0xffff7fff, 0x80001234, 0xff0000ff
psllh_expected:
  .word 0x00020002, 0xfffefffe, 0x00002468, 0xfe0001fe
word_shifts:
  .word 0x00000001, 0x80000001, 0x7fffffff, 0xff0000ff
psraw_expected:
  .word 0x00000000, 0xc0000000, 0x3fffffff, 0xff80007f
variable_counts:
  .word 0x00000021, 0xaaaaaaaa, 0x00000044, 0xbbbbbbbb
variable_values:
  .word 0x40000001, 0xdeadbeef, 0xf0000001, 0xcafebabe
psllvw_expected:
  .word 0x80000002, 0xffffffff, 0x00000010, 0x00000000
funnel_source:
  .word 0x13121110, 0x17161514, 0x1b1a1918, 0x1f1e1d1c
funnel_target:
  .word 0x03020100, 0x07060504, 0x0b0a0908, 0x0f0e0d0c
qfsrv_expected:
  .word 0x04030201, 0x08070605, 0x0c0b0a09, 0x100f0e0d
scratch:
  .space 16
