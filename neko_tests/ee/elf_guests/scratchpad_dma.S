  .set noreorder
  .text
  .globl _start
  .ent _start

  .macro install_tlb index, mask, entry_hi, entry_lo0, entry_lo1
  addiu $8, $zero, \index
  mtc0 $8, $0
  lui $8, %hi(\mask)
  ori $8, $8, %lo(\mask)
  mtc0 $8, $5
  lui $8, %hi(\entry_hi)
  ori $8, $8, %lo(\entry_hi)
  mtc0 $8, $10
  lui $8, %hi(\entry_lo0)
  ori $8, $8, %lo(\entry_lo0)
  mtc0 $8, $2
  lui $8, %hi(\entry_lo1)
  ori $8, $8, %lo(\entry_lo1)
  mtc0 $8, $3
  tlbwi
  .endm

  .macro publish reg, offset
  sw \reg, \offset($18)
  .endm

  .macro check reg, upper, lower, code
  lui $10, \upper
  ori $10, $10, \lower
  addiu $v0, $zero, \code
  bne \reg, $10, fail
  nop
  .endm

  .macro load_symbol reg, symbol
  lui \reg, %hi(\symbol)
  addiu \reg, \reg, %lo(\symbol)
  .endm

  .macro wait_channel base
1:
  lw $9, 0(\base)
  andi $9, $9, 0x0100
  bne $9, $zero, 1b
  nop
  .endm

_start:
  load_symbol $19, outputs
  lui $18, 0xa000
  or $18, $18, $19

  # Keep the guest mapped after reset ERL clears and map one 16 KiB SPR page.
  install_tlb 0, 0x00000000, 0x00100000, 0x0000401f, 0x0000405f
  install_tlb 5, 0x00006000, 0x30000000, 0x8000001f, 0x00000000
  mtc0 $zero, $12
  nop

  lui $20, 0x3000
  lui $21, 0xb000
  ori $21, $21, 0xe000
  lui $22, 0xb000
  ori $22, $22, 0xd000
  lui $23, 0xb000
  ori $23, $23, 0xd400

  # Enable DMA and both channel-completion masks.
  ori $8, $zero, 1
  sw $8, 0($21)
  lui $8, 0x0300
  sw $8, 0x10($21)

  # CPU qword stores seed the wrapping fromSPR normal transfer.
  load_symbol $8, normal_from_values
  lq $9, 0($8)
  sq $9, 0x3ff0($20)
  lq $9, 16($8)
  sq $9, 0($20)
  lw $9, 0x3ff0($20)
  publish $9, 0
  check $9, 0x89ab, 0xcdef, 1

  load_symbol $8, normal_from_destination
  sw $8, 0x10($22)
  ori $9, $zero, 2
  sw $9, 0x20($22)
  ori $9, $zero, 0x3ff0
  sw $9, 0x80($22)
  ori $9, $zero, 0x0100
  sw $9, 0($22)
  wait_channel $22

  lw $9, 0x20($22)
  publish $9, 4
  check $9, 0x0000, 0x0000, 2
  lw $9, 0($22)
  publish $9, 8
  check $9, 0x0000, 0x0000, 3
  lw $9, 0x80($22)
  publish $9, 12
  check $9, 0x0000, 0x0010, 4
  lw $9, 0x10($21)
  publish $9, 16
  check $9, 0x0300, 0x0100, 5

  # toSPR normal transfer shares CPU-visible storage and wraps SADR.
  load_symbol $8, normal_to_source
  sw $8, 0x10($23)
  ori $9, $zero, 2
  sw $9, 0x20($23)
  ori $9, $zero, 0x3ff0
  sw $9, 0x80($23)
  ori $9, $zero, 0x0100
  sw $9, 0($23)
  wait_channel $23

  lw $9, 0x20($23)
  publish $9, 20
  check $9, 0x0000, 0x0000, 6
  lw $9, 0($23)
  publish $9, 24
  check $9, 0x0000, 0x0000, 7
  lw $9, 0x80($23)
  publish $9, 28
  check $9, 0x0000, 0x0010, 8
  lw $9, 0x10($21)
  publish $9, 32
  check $9, 0x0300, 0x0300, 9
  lw $9, 0x3ff0($20)
  check $9, 0x4433, 0x2211, 10
  lw $9, 0($20)
  check $9, 0x5060, 0x7080, 11

  # Clear normal-transfer completion before proving interleave completion.
  ori $9, $zero, 0x0300
  sw $9, 0x10($21)
  lw $9, 0x10($21)
  check $9, 0x0300, 0x0000, 12

  # Seed four SPR rows and transfer them with 2-qword transfer/skip rows.
  load_symbol $8, interleave_from_values
  lq $9, 0($8)
  sq $9, 0x0100($20)
  lq $9, 16($8)
  sq $9, 0x0110($20)
  lq $9, 32($8)
  sq $9, 0x0120($20)
  lq $9, 48($8)
  sq $9, 0x0130($20)
  lui $9, 0x0002
  ori $9, $9, 0x0002
  sw $9, 0x30($21)

  load_symbol $8, interleave_from_destination
  sw $8, 0x10($22)
  ori $9, $zero, 4
  sw $9, 0x20($22)
  ori $9, $zero, 0x0100
  sw $9, 0x80($22)
  ori $9, $zero, 0x0108
  sw $9, 0($22)
  wait_channel $22
  lw $9, 0x10($22)
  subu $9, $9, $8
  publish $9, 36
  check $9, 0x0000, 0x0080, 13
  lw $9, 0x80($22)
  publish $9, 40
  check $9, 0x0000, 0x0140, 14
  lw $9, 0x10($21)
  publish $9, 84
  check $9, 0x0300, 0x0100, 15

  load_symbol $8, interleave_to_source
  sw $8, 0x10($23)
  ori $9, $zero, 4
  sw $9, 0x20($23)
  ori $9, $zero, 0x0200
  sw $9, 0x80($23)
  ori $9, $zero, 0x0108
  sw $9, 0($23)
  wait_channel $23
  lw $9, 0x10($23)
  subu $9, $9, $8
  publish $9, 44
  check $9, 0x0000, 0x0080, 16
  lw $9, 0x80($23)
  publish $9, 48
  check $9, 0x0000, 0x0240, 17
  lw $9, 0x0200($20)
  check $9, 0x0000, 0x0050, 18
  lw $9, 0x0210($20)
  check $9, 0x0000, 0x0060, 19
  lw $9, 0x0220($20)
  check $9, 0x0000, 0x0070, 20
  lw $9, 0x0230($20)
  check $9, 0x0000, 0x0080, 21
  lw $9, 0x10($21)
  publish $9, 60
  check $9, 0x0300, 0x0300, 22

  # Prime a cached alias, overwrite physical memory through fromSPR, and prove
  # that the resident clean line remains stale while KSEG1 sees DMA data.
  mfc0 $8, $16
  lui $9, 0x0001
  or $8, $8, $9
  mtc0 $8, $16
  load_symbol $8, coherence_destination
  lui $9, 0x8000
  or $11, $8, $9
  lw $12, 0($11)

  load_symbol $9, coherence_replacement
  lq $10, 0($9)
  sq $10, 0x0300($20)
  sw $8, 0x10($22)
  ori $9, $zero, 1
  sw $9, 0x20($22)
  ori $9, $zero, 0x0300
  sw $9, 0x80($22)
  ori $9, $zero, 0x0100
  sw $9, 0($22)
  wait_channel $22

  lw $12, 0($11)
  publish $12, 52
  check $12, 0x1111, 0x2222, 23
  lui $9, 0xa000
  or $8, $8, $9
  lw $12, 0($8)
  publish $12, 56
  check $12, 0xbbbb, 0xcccc, 24
  lw $9, 0x10($21)
  check $9, 0x0300, 0x0300, 25

  load_symbol $8, normal_from_destination
  publish $8, 64
  load_symbol $8, normal_to_source
  publish $8, 68
  load_symbol $8, interleave_from_destination
  publish $8, 72
  load_symbol $8, interleave_to_source
  publish $8, 76
  load_symbol $8, coherence_destination
  publish $8, 80

  or $v0, $zero, $zero
fail:
  jr $ra
  nop

  .end _start

  .data
  .align 6
normal_from_values:
  .word 0x89abcdef, 0x01234567, 0x76543210, 0xfedcba98
  .word 0x33334444, 0x11112222, 0xccccdddd, 0xaaaabbbb
normal_from_destination:
  .space 32

normal_to_source:
  .word 0x44332211, 0x88776655, 0xbbaa9988, 0xffeeddcc
  .word 0x50607080, 0x10203040, 0xd0e0f000, 0x90a0b0c0

  .align 6
interleave_from_values:
  .word 0x00000010, 0, 0x00000100, 0
  .word 0x00000020, 0, 0x00000200, 0
  .word 0x00000030, 0, 0x00000300, 0
  .word 0x00000040, 0, 0x00000400, 0
interleave_from_destination:
  .space 128

  .align 6
interleave_to_source:
  .word 0x00000050, 0, 0x00000500, 0
  .word 0x00000060, 0, 0x00000600, 0
  .space 32
  .word 0x00000070, 0, 0x00000700, 0
  .word 0x00000080, 0, 0x00000800, 0

  .align 6
coherence_destination:
  .word 0x11112222, 0x33334444, 0x55556666, 0x77778888
coherence_replacement:
  .word 0xbbbbcccc, 0x9999aaaa, 0xffff0000, 0xddddeeee

  .align 4
outputs:
  .space 88
