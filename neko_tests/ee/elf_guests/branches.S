  .set noreorder
  .text
  .globl _start
  .ent _start

_start:
  or $t0, $zero, $zero
  beq $zero, $zero, taken
  addiu $t0, $t0, 1
  ori $v0, $zero, 1
  jr $ra
  nop

taken:
  addiu $t1, $zero, 1
  bne $t0, $t1, delay_slot_failed
  nop

  or $v0, $zero, $zero
  bnel $zero, $zero, unexpected_likely_taken
  ori $v0, $zero, 2

  bne $v0, $zero, annul_failed
  nop

  jr $ra
  nop

delay_slot_failed:
  ori $v0, $zero, 3
  jr $ra
  nop

unexpected_likely_taken:
  ori $v0, $zero, 4
  jr $ra
  nop

annul_failed:
  ori $v0, $zero, 5
  jr $ra
  nop

  .end _start
