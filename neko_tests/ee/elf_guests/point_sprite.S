  .set noreorder
  .text
  .globl _start
  .ent _start

_start:
  or $21, $ra, $zero
  lui $16, 0x1000
  ori $16, $16, 0x6000
  lui $17, 0x1000
  ori $17, $17, 0x3020
  lui $18, %hi(scratch)
  addiu $18, $18, %lo(scratch)

  lui $4, %hi(setup_and_point_tag)
  addiu $4, $4, %lo(setup_and_point_tag)
  ori $5, $zero, 5
  jal submit_packet
  nop

  or $19, $zero, $zero
  ori $20, $zero, 96

point_loop:
  ori $8, $zero, 29
  multu $19, $8
  mflo $9
  ori $10, $zero, 160
  divu $9, $10
  mfhi $9
  addiu $9, $9, 96
  sw $9, 0($18)
  sw $9, 4($18)
  ori $10, $zero, 3
  divu $9, $10
  mflo $11
  addiu $11, $11, 160
  sw $11, 8($18)
  ori $11, $zero, 255
  sw $11, 12($18)
  lq $12, 0($18)
  sq $12, 0($16)

  ori $8, $zero, 83
  multu $19, $8
  mflo $9
  ori $10, $zero, 640
  divu $9, $10
  mfhi $9
  sll $9, $9, 4
  sw $9, 0($18)

  ori $8, $zero, 47
  multu $19, $8
  mflo $9
  multu $19, $19
  mflo $11
  sll $12, $11, 1
  addu $11, $11, $12
  addu $9, $9, $11
  ori $10, $zero, 448
  divu $9, $10
  mfhi $9
  sll $9, $9, 4
  sw $9, 4($18)
  sw $zero, 8($18)
  sw $zero, 12($18)
  lq $12, 0($18)
  sq $12, 0($16)

  addiu $19, $19, 1
  bne $19, $20, point_loop
  nop

  lui $4, %hi(sprite_packet)
  addiu $4, $4, %lo(sprite_packet)
  ori $5, $zero, 29
  jal submit_packet
  nop

wait_for_gif:
  lw $8, 0($17)
  lui $9, 0x1f00
  ori $9, $9, 0x0240
  and $8, $8, $9
  bne $8, $zero, wait_for_gif
  nop

  or $v0, $zero, $zero
  jr $21
  nop

submit_packet:
  lq $8, 0($4)
  sq $8, 0($16)
  addiu $4, $4, 16
  addiu $5, $5, -1
  bne $5, $zero, submit_packet
  nop
  jr $ra
  nop

  .end _start

  .data
  .align 4
setup_and_point_tag:
  .word 0x00008003, 0x10004000, 0x0000000e, 0
  .word 0x000a0000, 0, 0x0000004c, 0
  .word 0x027f0000, 0x01bf0000, 0x00000040, 0
  .word 0, 0, 0x00000018, 0
  .word 0x00008060, 0x20004000, 0x00000051, 0

sprite_packet:
  .word 0x0000800e, 0x20034000, 0x00000051, 0

  .word 30, 60, 120, 255
  .word 0x00000500, 0x00000500, 0, 0
  .word 30, 60, 120, 255
  .word 0x00002300, 0x00000580, 0, 0

  .word 30, 60, 120, 255
  .word 0x00000500, 0x00001680, 0, 0
  .word 30, 60, 120, 255
  .word 0x00002300, 0x00001700, 0, 0

  .word 30, 60, 120, 255
  .word 0x00000500, 0x00000580, 0, 0
  .word 30, 60, 120, 255
  .word 0x00000580, 0x00001680, 0, 0

  .word 30, 60, 120, 255
  .word 0x00002280, 0x00000580, 0, 0
  .word 30, 60, 120, 255
  .word 0x00002300, 0x00001680, 0, 0

  .word 255, 80, 70, 255
  .word 0x00000680, 0x00000b40, 0, 0
  .word 255, 80, 70, 255
  .word 0x00000880, 0x00000d40, 0, 0

  .word 70, 210, 255, 255
  .word 0x00001f80, 0x00000ec0, 0, 0
  .word 70, 210, 255, 255
  .word 0x00002180, 0x000010c0, 0, 0

  .word 240, 190, 40, 255
  .word 0x000012c0, 0x00000cc0, 0, 0
  .word 240, 190, 40, 255
  .word 0x00001540, 0x00000f40, 0, 0

  .bss
  .align 4
scratch:
  .space 16
