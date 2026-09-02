  .set noreorder
  .text
  .globl _start
  .ent _start

_start:
  lui $8, 0x1000
  ori $8, $8, 0x4000

  lui $9, %hi(fifo_data)
  addiu $9, $9, %lo(fifo_data)
  lq $10, 0($9)
  sq $10, 0($8)

  lui $8, 0x1000
  ori $8, $8, 0x5000
  sq $10, 0($8)

  lui $8, 0x1000
  ori $8, $8, 0x6000
  lq $10, 16($9)
  sq $10, 0($8)

  or $v0, $zero, $zero
  jr $ra
  nop

  .end _start

  .data
  .align 4
fifo_data:
  .word 0, 0, 0, 0
  .word 0x00008000, 0, 0, 0
