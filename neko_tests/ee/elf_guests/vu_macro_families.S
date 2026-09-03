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

  vmul.xyzw $vf3, $vf1, $vf2
  vmax.xyzw $vf4, $vf1, $vf2
  vmini.xyzw $vf5, $vf1, $vf2
  vftoi0.xyzw $vf6, $vf1
  vitof0.xyzw $vf7, $vf6
  vmove.xyzw $vf8, $vf3

  ori $10, $zero, 7
  ctc2 $10, $vi1
  ori $11, $zero, 5
  ctc2 $11, $vi2
  viadd $vi3, $vi1, $vi2
  cfc2 $12, $vi3
  andi $12, $12, 0xffff
  ori $13, $zero, 12
  bne $12, $13, integer_failed
  nop

  qmfc2.i $14, $vf8
  lui $15, %hi(output_vectors)
  addiu $15, $15, %lo(output_vectors)
  sqc2 $vf8, 0($15)
  sqc2 $vf4, 16($15)
  sqc2 $vf5, 32($15)
  sqc2 $vf7, 48($15)

  lui $16, %hi(expected_vectors)
  addiu $16, $16, %lo(expected_vectors)
  ori $17, $zero, 8

compare_loop:
  ld $18, 0($15)
  ld $19, 0($16)
  bne $18, $19, vector_failed
  nop
  addiu $15, $15, 8
  addiu $16, $16, 8
  addiu $17, $17, -1
  bne $17, $zero, compare_loop
  nop

  or $v0, $zero, $zero
  jr $ra
  nop

integer_failed:
  ori $v0, $zero, 1
  jr $ra
  nop

vector_failed:
  ori $v0, $zero, 2
  jr $ra
  nop

  .end _start

  .data
  .align 4
input_a:
  .float 2.0, 3.0, 4.0, 5.0

  .align 4
input_b:
  .float 10.0, 1.0, 6.0, 2.0

  .align 4
expected_vectors:
  .float 20.0, 3.0, 24.0, 10.0
  .float 10.0, 3.0, 6.0, 5.0
  .float 2.0, 1.0, 4.0, 2.0
  .float 2.0, 3.0, 4.0, 5.0

  .align 4
output_vectors:
  .space 64
