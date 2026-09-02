  .set noreorder
  .text
  .globl _start
  .ent _start

_start:
  addiu $8, $zero, 7
  addiu $9, $zero, -3
  addu $10, $8, $9
  addiu $11, $zero, 4
  bne $10, $11, arithmetic_failed
  nop

  slt $12, $9, $8
  beq $12, $zero, comparison_failed
  nop

  sll $13, $8, 2
  addiu $14, $zero, 28
  bne $13, $14, shift_failed
  nop

  or $v0, $zero, $zero
  jr $ra
  nop

arithmetic_failed:
  ori $v0, $zero, 1
  jr $ra
  nop

comparison_failed:
  ori $v0, $zero, 2
  jr $ra
  nop

shift_failed:
  ori $v0, $zero, 3
  jr $ra
  nop

  .end _start
