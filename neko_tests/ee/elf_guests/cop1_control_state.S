  .set noreorder
  .text
  .globl _start
  .ent _start

_start:
  lui $8, 0x0080
  ctc1 $8, $31
  cfc1 $9, $31

  lui $10, 0x0003
  ori $10, $10, 0xc000
  ctc1 $10, $31
  cfc1 $11, $31

  ori $12, $zero, 0x0078
  ctc1 $12, $31
  cfc1 $13, $31

  lui $14, 0xffff
  ori $14, $14, 0xffff
  ctc1 $14, $31
  cfc1 $15, $31

  ctc1 $zero, $31
  cfc1 $16, $31

  or $v0, $zero, $zero
  jr $ra
  nop

  .end _start
