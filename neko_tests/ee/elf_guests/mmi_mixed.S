  .set noreorder
  .text
  .globl _start
  .ent _start

  .macro load_qword reg, label
  lui $20, %hi(\label)
  addiu $20, $20, %lo(\label)
  lq \reg, 0($20)
  .endm

  .macro check_scalar reg, expected, code
  addiu $23, $zero, \expected
  addiu $v0, $zero, \code
  bne \reg, $23, fail
  nop
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
  # Ordinary independent ALU instructions form an O pair.
  addiu $2, $zero, 11
  addiu $3, $zero, 22
  addu $4, $2, $3
  check_scalar $4, 33, 1

  # Wide Y pair with a younger Pipe-1 leading-zero-count operation.
  load_qword $8, wide_source
  load_qword $9, wide_target
  load_qword $11, plzcw_initial
  load_qword $12, plzcw_source
  pand $10, $8, $9
  plzcw $11, $12
  check_qword $10, pand_expected, 2
  check_qword $11, plzcw_expected, 3

  # Wide Y pair with a younger Pipe-1 ALU operation.
  por $10, $8, $9
  addiu $14, $zero, 37
  check_qword $10, por_expected, 4
  check_scalar $14, 37, 5

  # Wide Y pair with a younger Pipe-1 MAC1 operation.
  addiu $17, $zero, 6
  addiu $18, $zero, 7
  pxor $10, $8, $9
  mult1 $16, $17, $18
  check_qword $10, pxor_expected, 6
  check_scalar $16, 42, 7
  mflo1 $19
  check_scalar $19, 42, 8

  # Respect the three-instruction ordering window on both sides of QFSRV.
  load_qword $8, funnel_source
  load_qword $9, funnel_target
  addiu $5, $zero, 1
  addiu $6, $zero, 2
  mtsab $5, 0
  addiu $2, $zero, 1
  addiu $3, $zero, 2
  addiu $4, $zero, 4
  qfsrv $10, $8, $9
  addiu $2, $2, 1
  addiu $3, $3, 1
  addiu $4, $4, 1
  mtsab $6, 0
  mfsa $7
  check_qword $10, qfsrv_expected, 9
  check_scalar $7, 16, 10

  # Packed work owns both HI/LO halves and blocks a younger scalar MAC0.
  load_qword $8, packed_source
  load_qword $9, packed_target
  addiu $11, $zero, 6
  addiu $12, $zero, 7
  pmultw $10, $8, $9
  mult $13, $11, $12
  check_qword $10, packed_result, 11
  check_scalar $13, 42, 12
  mflo $14
  check_scalar $14, 42, 13

  # The same packed operation blocks a younger scalar MAC1.
  pmultw $10, $8, $9
  mult1 $13, $11, $12
  check_qword $10, packed_result, 14
  check_scalar $13, 42, 15
  mflo1 $14
  check_scalar $14, 42, 16

  # Older scalar MAC0 work makes younger packed work retry.
  mult $13, $11, $12
  pmultw $10, $8, $9
  check_scalar $13, 42, 17
  check_qword $10, packed_result, 18

  # Older scalar MAC1 work makes younger packed work retry.
  mult1 $13, $11, $12
  pmultw $10, $8, $9
  check_scalar $13, 42, 19
  check_qword $10, packed_result, 20
  pmflo $14
  check_qword $14, packed_result, 21

  # Packed divide completes asynchronously while unrelated pairs proceed.
  load_qword $8, divide_source
  load_qword $9, divide_target
  load_qword $11, wide_source
  load_qword $12, wide_target
  pdivw $8, $9
  pand $10, $11, $12
  addiu $7, $zero, 3
  addiu $15, $zero, 5
  addiu $16, $zero, 7
  addiu $17, $zero, 11
  addiu $18, $zero, 13
  pmflo $13
  pmfhi $14
  check_qword $10, pand_expected, 22
  check_scalar $7, 3, 23
  check_scalar $15, 5, 24
  check_scalar $16, 7, 25
  check_scalar $17, 11, 26
  check_scalar $18, 13, 27
  check_qword $13, divide_quotient, 28
  check_qword $14, divide_remainder, 29

  or $v0, $zero, $zero
fail:
  jr $ra
  nop

  .end _start

  .data
  .align 4
wide_source:
  .word 0xff00ff00, 0x0f0f0f0f, 0xaaaaaaaa, 0x12345678
wide_target:
  .word 0x0f0f0f0f, 0xffff0000, 0x55555555, 0xf0f0f0f0
pand_expected:
  .word 0x0f000f00, 0x0f0f0000, 0x00000000, 0x10305070
por_expected:
  .word 0xff0fff0f, 0xffff0f0f, 0xffffffff, 0xf2f4f6f8
pxor_expected:
  .word 0xf00ff00f, 0xf0f00f0f, 0xffffffff, 0xe2c4a688
plzcw_source:
  .word 0xff0ff00f, 0x000fff0f, 0x9abcdef0, 0x12345678
plzcw_initial:
  .word 0xffffffff, 0xffffffff, 0x76543210, 0xfedcba98
plzcw_expected:
  .word 0x00000007, 0x0000000b, 0x76543210, 0xfedcba98
funnel_source:
  .word 0x13121110, 0x17161514, 0x1b1a1918, 0x1f1e1d1c
funnel_target:
  .word 0x03020100, 0x07060504, 0x0b0a0908, 0x0f0e0d0c
qfsrv_expected:
  .word 0x04030201, 0x08070605, 0x0c0b0a09, 0x100f0e0d
packed_source:
  .word 0x00000002, 0x00000000, 0x00000003, 0x00000000
packed_target:
  .word 0x00000004, 0x00000000, 0x00000005, 0x00000000
packed_result:
  .word 0x00000008, 0x00000000, 0x0000000f, 0x00000000
divide_source:
  .word 0x00000007, 0x00000000, 0x00000009, 0x00000000
divide_target:
  .word 0x00000003, 0x00000000, 0x00000004, 0x00000000
divide_quotient:
  .word 0x00000002, 0x00000000, 0x00000002, 0x00000000
divide_remainder:
  .word 0x00000001, 0x00000000, 0x00000001, 0x00000000
scratch:
  .space 16
