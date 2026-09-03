  .set noreorder
  .text
  .globl _start
  .ent _start

_start:
  lui $8, %hi(input_quadword)
  addiu $8, $8, %lo(input_quadword)
  lqc2 $vf1, 0($8)
  qmfc2 $9, $vf1
  qmtc2 $9, $vf2

  lui $10, %hi(output_quadword)
  addiu $10, $10, %lo(output_quadword)
  sqc2 $vf2, 0($10)

  ld $11, 0($8)
  ld $12, 0($10)
  bne $11, $12, transfer_failed
  nop

  ld $11, 8($8)
  ld $12, 8($10)
  bne $11, $12, transfer_failed
  nop

  or $v0, $zero, $zero
  jr $ra
  nop

transfer_failed:
  ori $v0, $zero, 1
  jr $ra
  nop

  .end _start

  .data
  .align 4
input_quadword:
  .word 0x33221100
  .word 0x77665544
  .word 0xbbaa9988
  .word 0xffeeddcc

  .align 4
output_quadword:
  .word 0, 0, 0, 0
