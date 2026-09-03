  .set noreorder
  .text
  .globl _start
  .ent _start

_start:
  lui $8, %hi(input_a)
  addiu $8, $8, %lo(input_a)
  lqc2 $vf1, 0($8)

  lui $9, %hi(input_b)
  addiu $9, $9, %lo(input_b)
  lqc2 $vf2, 0($9)

  vadd.xyzw $vf3, $vf1, $vf2
  vsub.xyzw $vf4, $vf3, $vf2
  vsubw.xyzw $vf5, $vf2, $vf1
  qmfc2.i $14, $vf5

  lui $10, %hi(output_vectors)
  addiu $10, $10, %lo(output_vectors)
  sqc2 $vf4, 0($10)
  sqc2 $vf5, 16($10)

  ld $11, 0($8)
  ld $12, 0($10)
  bne $11, $12, arithmetic_failed
  nop
  ld $11, 8($8)
  ld $12, 8($10)
  bne $11, $12, arithmetic_failed
  nop

  lui $13, %hi(expected_broadcast)
  addiu $13, $13, %lo(expected_broadcast)
  ld $11, 0($13)
  ld $12, 16($10)
  bne $11, $12, broadcast_failed
  nop
  ld $11, 8($13)
  ld $12, 24($10)
  bne $11, $12, broadcast_failed
  nop

  or $v0, $zero, $zero
  jr $ra
  nop

arithmetic_failed:
  ori $v0, $zero, 1
  jr $ra
  nop

broadcast_failed:
  ori $v0, $zero, 2
  jr $ra
  nop

  .end _start

  .data
  .align 4
input_a:
  .float 1.0, 2.0, 3.0, 4.0

  .align 4
input_b:
  .float 10.0, 20.0, 30.0, 40.0

  .align 4
expected_broadcast:
  .float 6.0, 16.0, 26.0, 36.0

  .align 4
output_vectors:
  .word 0, 0, 0, 0
  .word 0, 0, 0, 0
