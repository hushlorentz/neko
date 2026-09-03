  .set noreorder
  .text
  .globl _start
  .ent _start

_start:
  ori $8, $zero, 0x7f01
  ctc2 $8, $vi3
  cfc2 $9, $vi3
  andi $9, $9, 0xffff
  bne $8, $9, control_failed
  nop

  or $10, $zero, $zero
  ctc2 $10, $vi31
  nop
  bc2tl vu1_running
  ori $11, $zero, 1

  ori $v0, $zero, 2
  jr $ra
  nop

vu1_running:
  beq $11, $zero, running_delay_failed
  nop

  ori $10, $zero, 0x0100
  ctc2 $10, $vi28
  nop
  bc2fl vu1_stopped
  ori $12, $zero, 1

  ori $v0, $zero, 3
  jr $ra
  nop

vu1_stopped:
  beq $12, $zero, stopped_delay_failed
  nop

  or $v0, $zero, $zero
  jr $ra
  nop

control_failed:
  ori $v0, $zero, 1
  jr $ra
  nop

running_delay_failed:
  ori $v0, $zero, 4
  jr $ra
  nop

stopped_delay_failed:
  ori $v0, $zero, 5
  jr $ra
  nop

  .end _start
