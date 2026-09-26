  .set noreorder
  .text
  .globl _start
  .ent _start

  .macro load_qword reg, label
  lui $20, %hi(\label)
  addiu $20, $20, %lo(\label)
  lq \reg, 0($20)
  .endm

  .macro check_qword reg, label, code
  lui $20, %hi(scratch)
  addiu $20, $20, %lo(scratch)
  sq \reg, 0($20)
  lui $22, %hi(\label)
  addiu $22, $22, %lo(\label)
  ld $21, 0($20)
  ld $23, 0($22)
  addiu $v0, $zero, \code
  bne $21, $23, fail
  nop
  ld $21, 8($20)
  ld $23, 8($22)
  bne $21, $23, fail
  nop
  .endm

_start:
  load_qword $8, pmultw_source
  load_qword $9, pmultw_target
  pmultw $10, $8, $9
  check_qword $10, pmultw_result, 1
  pmflo $11
  check_qword $11, pmultw_lo, 2
  pmfhi $11
  check_qword $11, pmultw_hi, 3

  pmthi $zero
  pmtlo $zero
  load_qword $8, pmaddw_source
  load_qword $9, pmaddw_target
  pmaddw $10, $8, $9
  check_qword $10, pmaddw_result, 4
  pmflo $11
  check_qword $11, pmaddw_lo, 5
  pmfhi $11
  check_qword $11, pmaddw_hi, 6

  load_qword $8, pmulth_source
  load_qword $9, pmulth_target
  pmulth $10, $8, $9
  check_qword $10, pmulth_result, 7
  pmflo $11
  check_qword $11, pmulth_lo, 8
  pmfhi $11
  check_qword $11, pmulth_hi, 9

  load_qword $8, phmadh_source
  load_qword $9, phmadh_target
  phmadh $10, $8, $9
  check_qword $10, phmadh_result, 10
  pmflo $11
  check_qword $11, phmadh_lo, 11
  pmfhi $11
  check_qword $11, phmadh_hi, 12

  load_qword $8, pdivw_source
  load_qword $9, pdivw_target
  pdivw $8, $9
  pmflo $11
  check_qword $11, pdivw_lo, 13
  pmfhi $11
  check_qword $11, pdivw_hi, 14

  load_qword $8, pdivbw_source
  load_qword $9, pdivbw_target
  pdivbw $8, $9
  pmflo $11
  check_qword $11, pdivbw_lo, 15
  pmfhi $11
  check_qword $11, pdivbw_hi, 16

  or $v0, $zero, $zero
fail:
  jr $ra
  nop

  .end _start

  .data
  .align 4
pmultw_source:
  .word 0xfffffffe, 0xffffffff, 0x80000000, 0xffffffff
pmultw_target:
  .word 0x00000003, 0x00000000, 0xffffffff, 0xffffffff
pmultw_result:
  .word 0xfffffffa, 0xffffffff, 0x80000000, 0x00000000
pmultw_lo:
  .word 0xfffffffa, 0xffffffff, 0x80000000, 0xffffffff
pmultw_hi:
  .word 0xffffffff, 0xffffffff, 0x00000000, 0x00000000
pmaddw_source:
  .word 0xfffffffe, 0xffffffff, 0x00000002, 0x00000000
pmaddw_target:
  .word 0x00000003, 0x00000000, 0x00000003, 0x00000000
pmaddw_result:
  .word 0xfffffffa, 0xffffffff, 0x00000006, 0x00000000
pmaddw_lo:
  .word 0xfffffffa, 0xffffffff, 0x00000006, 0x00000000
pmaddw_hi:
  .word 0xffffffff, 0xffffffff, 0x00000000, 0x00000000
pmulth_source:
  .word 0x0002ffff, 0x0004fffd, 0x0001fffe, 0x80007fff
pmulth_target:
  .word 0x0003fffe, 0xfffb0004, 0x80000003, 0x0002ffff
pmulth_result:
  .word 0x00000002, 0xfffffff4, 0xfffffffa, 0xffff8001
pmulth_lo:
  .word 0x00000002, 0x00000006, 0xfffffffa, 0xffff8000
pmulth_hi:
  .word 0xfffffff4, 0xffffffec, 0xffff8001, 0xffff0000
phmadh_source:
  .word 0x00020001, 0x00040003, 0x00060005, 0x00080007
phmadh_target:
  .word 0x00030002, 0x00050004, 0x00070006, 0x00090008
phmadh_result:
  .word 0x00000008, 0x00000020, 0x00000048, 0x00000080
phmadh_lo:
  .word 0x00000008, 0x00000006, 0x00000048, 0x0000002a
phmadh_hi:
  .word 0x00000020, 0x00000014, 0x00000080, 0x00000048
pdivw_source:
  .word 0xfffffff9, 0xffffffff, 0x80000000, 0xffffffff
pdivw_target:
  .word 0x00000003, 0x00000000, 0xffffffff, 0xffffffff
pdivw_lo:
  .word 0xfffffffe, 0xffffffff, 0x80000000, 0xffffffff
pdivw_hi:
  .word 0xffffffff, 0xffffffff, 0x00000000, 0x00000000
pdivbw_source:
  .word 0xfffffff9, 0x00000007, 0x80000000, 0x7fffffff
pdivbw_target:
  .word 0x9abcfffd, 0x12345678, 0x76543210, 0xfedcba98
pdivbw_lo:
  .word 0x00000002, 0xfffffffe, 0x2aaaaaaa, 0xd5555556
pdivbw_hi:
  .word 0xffffffff, 0x00000001, 0xfffffffe, 0x00000001
scratch:
  .space 16
