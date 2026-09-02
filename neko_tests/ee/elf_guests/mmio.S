  .set noreorder
  .text
  .globl _start
  .ent _start

_start:
  lui $8, 0x1000
  ori $8, $8, 0xf010
  ori $9, $zero, 0x10
  sw $9, 0($8)
  lw $10, 0($8)
  bne $9, $10, interrupt_mask_failed
  nop

  lui $11, 0x1000
  ori $11, $11, 0xe000
  ori $12, $zero, 1
  sw $12, 0($11)
  lw $13, 0($11)
  bne $12, $13, dmac_control_failed
  nop

  lui $14, 0x1200
  ori $14, $14, 0x1040
  sd $12, 0($14)

  or $v0, $zero, $zero
  jr $ra
  nop

interrupt_mask_failed:
  ori $v0, $zero, 1
  jr $ra
  nop

dmac_control_failed:
  ori $v0, $zero, 2
  jr $ra
  nop

  .end _start
