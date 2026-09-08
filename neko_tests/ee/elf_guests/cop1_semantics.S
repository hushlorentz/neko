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
  lwc1 $f8, 8($8)
  lwc1 $f13, 12($8)

  add.s $f4, $f2, $f3
  sub.s $f5, $f3, $f2
  mul.s $f6, $f2, $f3
  abs.s $f7, $f8
  neg.s $f9, $f2
  max.s $f10, $f2, $f3
  min.s $f11, $f2, $f3
  cvt.s.w $f12, $f13
  cvt.w.s $f14, $f3
  sqrt.s $f15, $f8
  div.s $f16, $f0, $f0
  c.eq.s $f4, $f4

  mfc1 $9, $f4
  mfc1 $10, $f5
  mfc1 $11, $f6
  mfc1 $12, $f7
  mfc1 $13, $f9
  mfc1 $14, $f10
  mfc1 $15, $f11
  mfc1 $16, $f12
  mfc1 $17, $f14
  mfc1 $18, $f15
  mfc1 $19, $f16
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
  .word 0xc0800000
  .word 0xfffffffd
