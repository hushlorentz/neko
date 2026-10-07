  .set noreorder
  .text
  .globl _start
  .ent _start

  .macro load_value address_upper, address_lower, offset, code
  lui $8, \address_upper
  ori $8, $8, \address_lower
  lw $9, 0($8)
  sw $9, \offset($19)
  addiu $v0, $zero, \code
  lui $10, %hi(expected_values + \offset)
  lw $10, %lo(expected_values + \offset)($10)
  bne $9, $10, fail
  nop
  .endm

  .macro write_value address_upper, address_lower, value_upper, value_lower
  lui $8, \address_upper
  ori $8, $8, \address_lower
  lui $9, \value_upper
  ori $9, $9, \value_lower
  sw $9, 0($8)
  .endm

  .macro install_tlb index, mask_upper, mask_lower, virtual_upper, virtual_lower, even_upper, even_lower, odd_upper, odd_lower
  addiu $8, $zero, \index
  mtc0 $8, $0
  lui $8, \mask_upper
  ori $8, $8, \mask_lower
  mtc0 $8, $5
  lui $8, \virtual_upper
  ori $8, $8, \virtual_lower
  mtc0 $8, $10
  lui $8, \even_upper
  ori $8, $8, \even_lower
  mtc0 $8, $2
  lui $8, \odd_upper
  ori $8, $8, \odd_lower
  mtc0 $8, $3
  tlbwi
  .endm

  .macro copy_vector source, end, destination
  lui $8, %hi(\source)
  addiu $8, $8, %lo(\source)
  ori $9, $zero, \destination
  addiu $10, $zero, (\end - \source) / 4
1:
  lw $11, 0($8)
  sw $11, 0($9)
  addiu $8, $8, 4
  addiu $9, $9, 4
  addiu $10, $10, -1
  bne $10, $zero, 1b
  nop
  .endm

  .macro check_exception count, record_offset, marker, cause, address_upper, address_lower, context_upper, context_lower, entry_hi_upper, entry_hi_lower, fault_label, code
  lw $8, 64($19)
  addiu $9, $zero, \count
  addiu $v0, $zero, \code
  bne $8, $9, fail
  nop
  lw $8, \record_offset($19)
  addiu $9, $zero, \marker
  bne $8, $9, fail
  nop
  lw $8, \record_offset + 4($19)
  addiu $9, $zero, \cause
  bne $8, $9, fail
  nop
  lw $8, \record_offset + 8($19)
  lui $9, \address_upper
  ori $9, $9, \address_lower
  bne $8, $9, fail
  nop
  lw $8, \record_offset + 12($19)
  lui $9, \context_upper
  ori $9, $9, \context_lower
  bne $8, $9, fail
  nop
  lw $8, \record_offset + 16($19)
  lui $9, \entry_hi_upper
  ori $9, $9, \entry_hi_lower
  bne $8, $9, fail
  nop
  lw $8, \record_offset + 20($19)
  lui $9, %hi(\fault_label)
  addiu $9, $9, %lo(\fault_label)
  bne $8, $9, fail
  nop
  .endm

_start:
  lui $19, %hi(outputs)
  addiu $19, $19, %lo(outputs)

  # Install exception-vector stubs while reset ERL identity routing is active.
  copy_vector refill_vector_image, refill_vector_image_end, 0x0000
  copy_vector general_vector_image, general_vector_image_end, 0x0180

  # Seed the physical words used at each even/odd page boundary.
  write_value 0x0020, 0x0ffc, 0x0404, 0x0001
  write_value 0x0020, 0x1000, 0x0404, 0x0002
  write_value 0x0030, 0x3ffc, 0x1616, 0x0001
  write_value 0x0030, 0x4000, 0x1616, 0x0002
  write_value 0x0040, 0xfffc, 0x6464, 0x0001
  write_value 0x0041, 0x0000, 0x6464, 0x0002
  write_value 0x0053, 0xfffc, 0x0256, 0x0001
  write_value 0x0054, 0x0000, 0x0256, 0x0002
  write_value 0x006f, 0xfffc, 0x1001, 0x0001
  write_value 0x0070, 0x0000, 0x1001, 0x0002
  write_value 0x00bf, 0xfffc, 0x4004, 0x0001
  write_value 0x00c0, 0x0000, 0x4004, 0x0002
  write_value 0x00ff, 0xfffc, 0x1610, 0x0001
  write_value 0x0100, 0x0000, 0x1610, 0x0002
  write_value 0x01e0, 0x0800, 0xa51d, 0x0042
  write_value 0x01e2, 0x0800, 0x610b, 0x0043

  # Entry 0 keeps this guest's low-kuseg text and data mapped after ERL clears.
  install_tlb 0, 0x0000, 0x0000, 0x0010, 0x0000, 0x0000, 0x401f, 0x0000, 0x405f

  install_tlb 1, 0x0000, 0x0000, 0x1000, 0x0000, 0x0000, 0x801f, 0x0000, 0x805f
  install_tlb 2, 0x0000, 0x6000, 0x1200, 0x0000, 0x0000, 0xc01f, 0x0000, 0xc11f
  install_tlb 3, 0x0001, 0xe000, 0x1400, 0x0000, 0x0001, 0x001f, 0x0001, 0x041f
  install_tlb 4, 0x0007, 0xe000, 0x1600, 0x0000, 0x0001, 0x401f, 0x0001, 0x501f
  install_tlb 5, 0x001f, 0xe000, 0x1800, 0x0000, 0x0001, 0x801f, 0x0001, 0xc01f
  install_tlb 6, 0x007f, 0xe000, 0x1c00, 0x0000, 0x0002, 0x001f, 0x0003, 0x001f
  install_tlb 7, 0x01ff, 0xe000, 0x2000, 0x0000, 0x0000, 0x001f, 0x0004, 0x001f

  # Entry 8 is ASID-specific; entry 9 is combined-global.
  install_tlb 8, 0x0000, 0x0000, 0x2800, 0x0042, 0x0007, 0x801e, 0x0007, 0x805e
  install_tlb 9, 0x0000, 0x0000, 0x2a00, 0x0000, 0x0007, 0x881f, 0x0007, 0x885f

  mtc0 $zero, $12
  nop

  load_value 0x1000, 0x0ffc, 0, 1
  load_value 0x1000, 0x1000, 4, 2
  load_value 0x1200, 0x3ffc, 8, 3
  load_value 0x1200, 0x4000, 12, 4
  load_value 0x1400, 0xfffc, 16, 5
  load_value 0x1401, 0x0000, 20, 6
  load_value 0x1603, 0xfffc, 24, 7
  load_value 0x1604, 0x0000, 28, 8
  load_value 0x180f, 0xfffc, 32, 9
  load_value 0x1810, 0x0000, 36, 10
  load_value 0x1c3f, 0xfffc, 40, 11
  load_value 0x1c40, 0x0000, 44, 12
  load_value 0x20ff, 0xfffc, 48, 13
  load_value 0x2100, 0x0000, 52, 14

  # A valid dirty mapping permits a store and exposes the written value.
  lui $8, 0x1000
  ori $8, $8, 0x0800
  lui $9, 0xd17d
  ori $9, $9, 0x00aa
  sw $9, 0($8)
  lw $10, 0($8)
  sw $10, 144($19)
  addiu $v0, $zero, 15
  bne $9, $10, fail
  nop

  lui $8, 0x2800
  ori $8, $8, 0x0042
  mtc0 $8, $10
  load_value 0x2800, 0x0800, 56, 16

  lui $8, 0x2a00
  ori $8, $8, 0x0043
  mtc0 $8, $10
  load_value 0x2a00, 0x0800, 60, 17

  # The same VPN now mismatches only by ASID and reaches the refill vector.
  lui $8, 0x2800
  ori $8, $8, 0x0043
  mtc0 $8, $10
  lui $8, 0x2800
refill_fault:
  lw $9, 0x0800($8)
  check_exception 1, 68, 1, 8, 0x2800, 0x0800, 0x0014, 0x0000, 0x2800, 0x0043, refill_fault, 18

  # A matched combined-global entry with V clear reaches the general vector.
  install_tlb 10, 0x0000, 0x0000, 0x3200, 0x0000, 0x0007, 0x901d, 0x0007, 0x905d
  lui $8, 0x3200
invalid_fault:
  lw $9, 0x0800($8)
  check_exception 2, 92, 2, 8, 0x3200, 0x0800, 0x0019, 0x0000, 0x3200, 0x0000, invalid_fault, 19

  # A valid, non-dirty mapping rejects a store with TLB Modified.
  install_tlb 11, 0x0000, 0x0000, 0x3400, 0x0000, 0x0007, 0xa01b, 0x0007, 0xa05b
  lui $8, 0x3400
modified_fault:
  sw $zero, 0x0800($8)
  check_exception 3, 116, 2, 4, 0x3400, 0x0800, 0x001a, 0x0000, 0x3400, 0x0000, modified_fault, 20

  or $v0, $zero, $zero
fail:
  jr $ra
  nop

  .end _start

  .align 4
refill_vector_image:
  addiu $26, $zero, 1
  j shared_exception_handler
  nop
refill_vector_image_end:

general_vector_image:
  addiu $26, $zero, 2
  j shared_exception_handler
  nop
general_vector_image_end:

shared_exception_handler:
  sw $26, 140($19)
  lw $27, 64($19)
  sltiu $26, $27, 3
  bne $26, $zero, record_exception
  nop
  addiu $26, $zero, -1
  sw $26, 64($19)
  mfc0 $26, $14
  addiu $26, $26, 4
  mtc0 $26, $14
  eret
  nop
record_exception:
  sll $26, $27, 3
  sll $27, $27, 4
  addu $27, $27, $26
  addu $27, $27, $19
  addiu $27, $27, 68
  lw $26, 140($19)
  sw $26, 0($27)
  mfc0 $26, $13
  sw $26, 4($27)
  mfc0 $26, $8
  sw $26, 8($27)
  mfc0 $26, $4
  sw $26, 12($27)
  mfc0 $26, $10
  sw $26, 16($27)
  mfc0 $26, $14
  sw $26, 20($27)
  addiu $26, $26, 4
  mtc0 $26, $14
  lw $26, 64($19)
  addiu $26, $26, 1
  sw $26, 64($19)
  eret
  nop

  .data
  .align 4
expected_values:
  .word 0x04040001, 0x04040002
  .word 0x16160001, 0x16160002
  .word 0x64640001, 0x64640002
  .word 0x02560001, 0x02560002
  .word 0x10010001, 0x10010002
  .word 0x40040001, 0x40040002
  .word 0x16100001, 0x16100002
  .word 0xa51d0042, 0x610b0043

outputs:
  .space 148
