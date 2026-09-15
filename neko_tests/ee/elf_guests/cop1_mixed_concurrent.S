  .set noreorder
  .text
  .globl _start
  .ent _start

_start:
  ctc1 $zero, $31

  lui $8, %hi(payload)
  addiu $8, $8, %lo(payload)
  lwc1 $f2, 0($8)
  lwc1 $f3, 4($8)
  lw $9, 8($8)
  lwc1 $f5, 12($8)

  add.s $f10, $f2, $f3
  mtc1 $9, $f4

  mfc1 $10, $f4
  add.s $f11, $f10, $f2

  swc1 $f11, 16($8)
  cvt.s.w $f12, $f5

  add.s $f13, $f12, $f2
  lwc1 $f14, 16($8)

  swc1 $f14, 20($8)
  mul.s $f15, $f13, $f2

  mfc1 $11, $f10
  mfc1 $12, $f11
  mfc1 $13, $f12
  mfc1 $14, $f13
  mfc1 $15, $f14
  mfc1 $16, $f15
  cfc1 $17, $31

  or $v0, $zero, $zero
  jr $ra
  nop

  .end _start

  .data
  .align 4
payload:
  .word 0x3fc00000
  .word 0x40100000
  .word 0x89abcdef
  .word 0xfffffffd
  .word 0
  .word 0
