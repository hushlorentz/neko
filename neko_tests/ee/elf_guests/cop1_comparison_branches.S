  .set noreorder
  .text
  .globl _start
  .ent _start

_start:
  ctc1 $zero, $31

  lui $8, %hi(inputs)
  addiu $8, $8, %lo(inputs)
  lwc1 $f2, 0($8)
  lwc1 $f3, 4($8)
  lwc1 $f4, 8($8)

  or $16, $zero, $zero
  or $17, $zero, $zero

  c.f.s $f2, $f2
  bc1f bc1f_taken
  addiu $16, $16, 1
  addiu $17, $17, 1
bc1f_taken:

  c.eq.s $f2, $f3
  bc1f bc1f_untaken
  addiu $16, $16, 2
  addiu $17, $17, 2
bc1f_untaken:

  c.eq.s $f2, $f3
  bc1t bc1t_taken
  addiu $16, $16, 4
  addiu $17, $17, 4
bc1t_taken:

  c.lt.s $f4, $f2
  bc1t bc1t_untaken
  addiu $16, $16, 8
  addiu $17, $17, 8
bc1t_untaken:

  c.lt.s $f4, $f2
  bc1fl bc1fl_taken
  addiu $16, $16, 16
  addiu $17, $17, 16
bc1fl_taken:

  c.le.s $f2, $f4
  bc1fl bc1fl_untaken
  addiu $16, $16, 32
  addiu $17, $17, 32
bc1fl_untaken:

  c.le.s $f2, $f4
  bc1tl bc1tl_taken
  addiu $16, $16, 64
  addiu $17, $17, 64
bc1tl_taken:

  c.f.s $f2, $f2
  bc1tl bc1tl_untaken
  addiu $16, $16, 128
  addiu $17, $17, 128
bc1tl_untaken:

  cfc1 $18, $31
  or $v0, $zero, $zero
  jr $ra
  nop

  .end _start

  .data
  .align 4
inputs:
  .word 0x3f800000
  .word 0x3f800000
  .word 0x40000000
