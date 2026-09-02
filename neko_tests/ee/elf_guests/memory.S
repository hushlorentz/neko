  .set noreorder
  .text
  .globl _start
  .ent _start

_start:
  addiu $sp, $sp, -32

  lui $t0, 0x89ab
  ori $t0, $t0, 0xcdef
  sw $t0, 0($sp)
  lw $t1, 0($sp)
  bne $t0, $t1, word_failed
  nop

  lui $t2, 0x1122
  ori $t2, $t2, 0x3344
  dsll32 $t2, $t2, 0
  lui $t3, 0x89ab
  ori $t3, $t3, 0xcdef
  dsll32 $t3, $t3, 0
  dsrl32 $t3, $t3, 0
  or $t2, $t2, $t3
  sd $t2, 8($sp)
  ld $t3, 8($sp)
  bne $t2, $t3, doubleword_failed
  nop

  addiu $sp, $sp, 32
  or $v0, $zero, $zero
  jr $ra
  nop

word_failed:
  addiu $sp, $sp, 32
  ori $v0, $zero, 1
  jr $ra
  nop

doubleword_failed:
  addiu $sp, $sp, 32
  ori $v0, $zero, 2
  jr $ra
  nop

  .end _start
