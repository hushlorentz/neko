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

  rsqrt.s $f20, $f2, $f3
  div.s $f21, $f2, $f4
  sqrt.s $f22, $f5

  mfc1 $15, $f20
  mfc1 $16, $f21
  mfc1 $17, $f22
  cfc1 $18, $31

  or $v0, $zero, $zero
  jr $ra
  nop

  .end _start

  .data
  .align 4
inputs:
  .word 0x40c00000
  .word 0x40800000
  .word 0x40000000
  .word 0x41100000
