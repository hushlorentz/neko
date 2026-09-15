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

  ori $10, $zero, 0xc018

  ctc1 $10, $31
  abs.s $f10, $f2
  cfc1 $23, $31

  ctc1 $10, $31
  neg.s $f11, $f3
  cfc1 $24, $31

  abs.s $f12, $f6
  neg.s $f13, $f6

  ctc1 $10, $31
  cvt.s.w $f14, $f3
  cfc1 $25, $31
  cvt.s.w $f15, $f4
  cvt.s.w $f16, $f5

  ctc1 $zero, $31
  cvt.w.s $f17, $f7
  cfc1 $20, $31
  cvt.w.s $f18, $f8
  cfc1 $21, $31
  cvt.w.s $f19, $f9
  cfc1 $22, $31

  or $v0, $zero, $zero
  jr $ra
  nop

  .end _start

  .data
  .align 4
inputs:
  .word 0x80000000
  .word 0x00000000
  .word 0xfffffffd
  .word 0x7fffffff
  .word 0xc0600000
  .word 0x4f000000
  .word 0xcf000000
  .word 0xbff33333
