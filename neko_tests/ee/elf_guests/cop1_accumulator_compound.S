  .set noreorder
  .text
  .globl _start
  .ent _start

_start:
  ctc1 $zero, $31

  lui $8, %hi(inputs)
  addiu $8, $8, %lo(inputs)
  lwc1 $f2, 0($8)
  lwc1 $f3, 4($8)
  lwc1 $f4, 8($8)
  lwc1 $f5, 12($8)
  lwc1 $f6, 16($8)
  lwc1 $f7, 20($8)
  lwc1 $f8, 24($8)
  lwc1 $f9, 28($8)
  lwc1 $f10, 32($8)
  lwc1 $f11, 36($8)

  adda.s $f2, $f3
  madd.s $f20, $f4, $f2

  suba.s $f5, $f2
  msub.s $f21, $f3, $f2

  mula.s $f3, $f4
  madd.s $f22, $f2, $f3

  adda.s $f2, $f3
  madda.s $f2, $f3
  msuba.s $f2, $f2
  madd.s $f23, $f6, $f6

  ctc1 $zero, $31
  adda.s $f9, $f9
  cfc1 $15, $31
  suba.s $f10, $f11
  cfc1 $16, $31

  ctc1 $zero, $31
  adda.s $f2, $f2
  madd.s $f24, $f7, $f8
  cfc1 $17, $31
  madd.s $f25, $f9, $f3
  cfc1 $18, $31
  msub.s $f26, $f7, $f8
  cfc1 $19, $31

  or $v0, $zero, $zero
  jr $ra
  nop

  .end _start

  .data
  .align 4
inputs:
  .word 0x3f800000
  .word 0x40000000
  .word 0x40400000
  .word 0x40800000
  .word 0x00000000
  .word 0x80800000
  .word 0x3f000000
  .word 0x7f800000
  .word 0x00800001
  .word 0x00800000
