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
  lwc1 $f12, 40($8)
  lwc1 $f13, 44($8)
  lwc1 $f14, 48($8)

  add.s $f20, $f2, $f3
  sub.s $f21, $f4, $f2
  mul.s $f22, $f5, $f6
  max.s $f23, $f7, $f5
  min.s $f24, $f7, $f5

  ctc1 $zero, $31

  add.s $f25, $f8, $f8
  cfc1 $15, $31

  sub.s $f26, $f9, $f10
  cfc1 $16, $31

  mul.s $f27, $f8, $f13
  cfc1 $17, $31

  mul.s $f28, $f10, $f14
  cfc1 $18, $31

  max.s $f29, $f11, $f12
  cfc1 $19, $31

  min.s $f30, $f11, $f12
  cfc1 $20, $31

  or $v0, $zero, $zero
  jr $ra
  nop

  .end _start

  .data
  .align 4
inputs:
  .word 0x3fc00000
  .word 0x40100000
  .word 0x40b00000
  .word 0xc0000000
  .word 0x40400000
  .word 0xbf800000
  .word 0x7f800000
  .word 0x00800001
  .word 0x00800000
  .word 0x00000000
  .word 0x80000000
  .word 0x40000000
  .word 0x3f000000
