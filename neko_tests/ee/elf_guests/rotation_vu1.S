  .set noreorder
  .text
  .globl _start
  .ent _start

_start:
  lui $8, 0x1000

  ori $9, $8, 0xe000
  ori $10, $zero, 1
  sw $10, 0($9)

  ori $11, $8, 0x9010
  lui $12, %hi(vif1_packet)
  addiu $12, $12, %lo(vif1_packet)
  sw $12, 0($11)

  ori $11, $8, 0x9020
  ori $12, $zero, 38
  sw $12, 0($11)

  ori $11, $8, 0x9000
  ori $12, $zero, 0x0101
  sw $12, 0($11)

wait_for_dma:
  lw $12, 0($11)
  andi $12, $12, 0x0100
  bne $12, $zero, wait_for_dma
  nop

wait_for_vu1:
  bc2t wait_for_vu1
  nop

  or $v0, $zero, $zero
  jr $ra
  nop

  .end _start

  .data
  .align 4
vif1_packet:
  .word 0x00000000
  .word 0x4a250000
  .incbin "../../vpu/integration/rotation_vu1_capstone.bin"

  .word 0x01000404
  .word 0x6c110000

  .word 3, 12, 2, 5

  .word 0x3f3504f3, 0xbf3504f3, 0, 0
  .word 0x3f3504f3, 0x3f3504f3, 0, 0
  .word 0, 0, 0x3f800000, 0

  .word 0x45000000, 0x45000000, 0x45800000, 0x45080000

  .word 4, 0x10000000, 0x0000000e, 0
  .word 0x000a0000, 0, 0x0000004c, 0
  .word 0x00006c00, 0x00007900, 0x00000018, 0
  .word 0x027f0000, 0x01bf0000, 0x00000040, 0
  .word 0x0000000b, 0, 0, 0

  .word 0x00008003, 0x20000000, 0x0000005e, 0

  .word 0x000000ff, 0x3f800000, 1, 0
  .word 0xc2800000, 0xc2800000, 0x44800000, 0
  .word 0x0000ff00, 0x3f800000, 1, 0
  .word 0xc2800000, 0x42c00000, 0x44800000, 0
  .word 0x00ff0000, 0x3f800000, 1, 0
  .word 0x00000000, 0x42c00000, 0x44800000, 0

  .word 0, 0
  .word 0x14000000, 0, 0, 0
