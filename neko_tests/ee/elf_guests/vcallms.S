  .set noreorder
  .text
  .globl _start
  .ent _start

_start:
  lui $8, %hi(input_quadword)
  addiu $8, $8, %lo(input_quadword)
  lqc2 $vf1, 0($8)

  vcallms 0
  qmfc2.i $9, $vf2

  lui $10, %hi(output_quadword)
  addiu $10, $10, %lo(output_quadword)
  sqc2 $vf2, 0($10)

  ld $11, 0($8)
  ld $12, 0($10)
  bne $11, $12, call_failed
  nop
  ld $11, 8($8)
  ld $12, 8($10)
  bne $11, $12, call_failed
  nop

  lui $13, %hi(second_input_quadword)
  addiu $13, $13, %lo(second_input_quadword)
  lqc2 $vf1, 0($13)

  ctc2 $zero, $vi27
  nop
  nop
  vcallmsr $vi27
  qmfc2.i $9, $vf2
  sqc2 $vf2, 0($10)

  ld $11, 0($13)
  ld $12, 0($10)
  bne $11, $12, call_register_failed
  nop
  ld $11, 8($13)
  ld $12, 8($10)
  bne $11, $12, call_register_failed
  nop

  or $v0, $zero, $zero
  jr $ra
  nop

call_failed:
  ori $v0, $zero, 1
  jr $ra
  nop

call_register_failed:
  ori $v0, $zero, 2
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
second_input_quadword:
  .word 0x01234567
  .word 0x89abcdef
  .word 0x13579bdf
  .word 0x2468ace0

  .align 4
output_quadword:
  .word 0, 0, 0, 0
