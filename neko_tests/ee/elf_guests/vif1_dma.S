  .set noreorder
  .text
  .globl _start
  .ent _start

_start:
  lui $8, 0x1000

  ori $9, $8, 0xe000
  ori $10, $zero, 1
  sw $10, 0($9)

  ori $11, $8, 0xe010
  ori $12, $zero, 2
  sw $12, 0($11)
  lui $12, 0x0002
  sw $12, 0($11)

  ori $13, $8, 0x9010
  lui $14, %hi(vif1_packet)
  addiu $14, $14, %lo(vif1_packet)
  sw $14, 0($13)

  ori $13, $8, 0x9020
  ori $15, $zero, 1
  sw $15, 0($13)

  ori $13, $8, 0x9000
  ori $15, $zero, 0x0101
  sw $15, 0($13)

wait_for_dma:
  lw $16, 0($13)
  andi $16, $16, 0x0100
  bne $16, $zero, wait_for_dma
  nop

  lw $17, 0($11)
  andi $18, $17, 2
  beq $18, $zero, completion_failed
  nop

  lui $19, 0x0002
  and $18, $17, $19
  beq $18, $zero, mask_failed
  nop

  or $v0, $zero, $zero
  jr $ra
  nop

completion_failed:
  ori $v0, $zero, 1
  jr $ra
  nop

mask_failed:
  ori $v0, $zero, 2
  jr $ra
  nop

  .end _start

  .data
  .align 4
vif1_packet:
  .word 0, 0, 0, 0
