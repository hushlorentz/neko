  .set noreorder
  .text
  .globl _start
  .ent _start

_start:
  ctc1 $zero, $31

  lui $8, %hi(payload)
  addiu $8, $8, %lo(payload)

  lw $9, 0($8)
  mtc1 $9, $f2
  mfc1 $10, $f2
  mov.s $f3, $f2
  swc1 $f3, 8($8)

  lwc1 $f4, 4($8)
  swc1 $f4, 12($8)

  lwc1 $f5, 8($8)
  mfc1 $11, $f5
  lwc1 $f6, 12($8)
  mfc1 $12, $f6

  or $v0, $zero, $zero
  jr $ra
  nop

  .end _start

  .data
  .align 4
payload:
  .word 0x89abcdef
  .word 0x7fc12345
  .word 0
  .word 0
