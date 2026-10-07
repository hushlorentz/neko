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

  .macro call_alias reg, offset, expected, code
  jalr $ra, \reg
  nop
  publish $3, \offset
  check $3, 0x0000, \expected, \code
  .endm

  .macro wait_channel base
1:
  lw $9, 0(\base)
  andi $9, $9, 0x0100
  bne $9, $zero, 1b
  nop
  .endm

_start:
  or $23, $ra, $zero
  load_symbol $19, outputs
  lui $18, 0xa000
  or $18, $18, $19

  # Map the guest, one scratchpad page, and two virtual instruction aliases
  # selecting distinct I-cache sets for the same physical code.
  install_tlb 0, 0x00000000, 0x00100000, 0x0000401f, 0x0000405f
  install_tlb 5, 0x00006000, 0x30000000, 0x8000001f, 0x00000000

  load_symbol $8, self_modify_target
  srl $9, $8, 12
  sll $9, $9, 12
  srl $10, $9, 6
  ori $10, $10, 0x001f
  addiu $11, $zero, 6
  mtc0 $11, $0
  mtc0 $zero, $5
  lui $11, 0x2000
  mtc0 $11, $10
  mtc0 $10, $2
  mtc0 $10, $3
  tlbwi

  andi $11, $8, 0x0fff
  lui $24, 0x2000
  or $24, $24, $11
  lui $25, 0x2000
  ori $25, $25, 0x1000
  or $25, $25, $11

  mtc0 $zero, $12
  nop

  mfc0 $8, $16
  lui $9, 0x0003
  or $8, $8, $9
  mtc0 $8, $16

  # Dirty a cached physical alias, publish it through explicit writeback,
  # replace memory through fromSPR DMA, and prove that disabling a cache only
  # bypasses its retained lines until explicit invalidation.
  load_symbol $8, dma_target
  lui $14, 0x8000
  or $14, $14, $8
  lui $15, 0xa000
  or $15, $15, $8

  lw $9, 0($14)
  lui $9, 0x3333
  ori $9, $9, 0x4444
  sw $9, 0($14)
  lw $9, 0($14)
  publish $9, 0
  check $9, 0x3333, 0x4444, 1
  lw $9, 0($15)
  publish $9, 4
  check $9, 0x1111, 0x2222, 2

  cache 0x1c, 0($14)
  lw $9, 0($15)
  publish $9, 8
  check $9, 0x3333, 0x4444, 3

  lui $20, 0x3000
  load_symbol $8, dma_replacement
  lq $9, 0($8)
  sq $9, 0($20)
  lui $21, 0xb000
  ori $21, $21, 0xe000
  lui $22, 0xb000
  ori $22, $22, 0xd000
  ori $9, $zero, 1
  sw $9, 0($21)
  load_symbol $8, dma_target
  sw $8, 0x10($22)
  ori $9, $zero, 1
  sw $9, 0x20($22)
  sw $zero, 0x80($22)
  ori $9, $zero, 0x0100
  sw $9, 0($22)
  wait_channel $22

  lw $9, 0($14)
  publish $9, 12
  check $9, 0x3333, 0x4444, 4
  mfc0 $8, $16
  lui $9, 0x0001
  nor $9, $9, $zero
  and $8, $8, $9
  mtc0 $8, $16
  lw $9, 0($14)
  publish $9, 16
  check $9, 0x5555, 0x6666, 5
  mfc0 $8, $16
  lui $9, 0x0001
  or $8, $8, $9
  mtc0 $8, $16
  lw $9, 0($14)
  publish $9, 20
  check $9, 0x3333, 0x4444, 6
  cache 0x1a, 0($14)
  lw $9, 0($14)
  publish $9, 24
  check $9, 0x5555, 0x6666, 7

  # Build a locked D-cache line through index tag/data operations. Normal
  # stores update its data without setting Dirty, and replacement pressure is
  # confined to the other way.
  lui $16, 0x8000
  ori $16, $16, 0x0100
  ori $8, $zero, 0x0028
  mtc0 $8, $28
  cache 0x12, 0($16)
  lui $8, 0xa1a2
  ori $8, $8, 0xa3a4
  mtc0 $8, $28
  cache 0x13, 0($16)
  lw $9, 0($16)
  publish $9, 28
  check $9, 0xa1a2, 0xa3a4, 8
  lui $9, 0xb1b2
  ori $9, $9, 0xb3b4
  sw $9, 0($16)
  lw $9, 0($16)
  publish $9, 32
  check $9, 0xb1b2, 0xb3b4, 9
  lui $17, 0xa000
  ori $17, $17, 0x0100
  lw $9, 0($17)
  publish $9, 36
  check $9, 0x0000, 0x0000, 10
  cache 0x10, 0($16)
  mfc0 $9, $28
  publish $9, 40
  check $9, 0x0000, 0x0028, 11

  lui $8, 0xa000
  ori $8, $8, 0x1100
  lui $9, 0x1111
  ori $9, $9, 0x1111
  sw $9, 0($8)
  lui $8, 0xa000
  ori $8, $8, 0x2100
  lui $9, 0x2222
  ori $9, $9, 0x2222
  sw $9, 0($8)
  lui $8, 0x8000
  ori $8, $8, 0x1100
  lw $9, 0($8)
  lui $8, 0x8000
  ori $8, $8, 0x2100
  lw $9, 0($8)
  addiu $8, $16, 1
  cache 0x10, 0($8)
  mfc0 $9, $28
  publish $9, 44
  check $9, 0x0000, 0x2020, 12

  # Fill both virtual I-cache aliases, modify the physical instruction through
  # D-cache, then make each alias observe the replacement only through the
  # explicit writeback/invalidate workflow.
  cache 0x0e, 0($24)
  call_alias $24, 48, 0x0001, 13
  call_alias $25, 52, 0x0001, 14

  lui $8, 0x2403
  ori $8, $8, 0x0002
  sw $8, 0($24)
  call_alias $24, 56, 0x0001, 15
  call_alias $25, 60, 0x0001, 16
  cache 0x1c, 0($24)
  cache 0x0b, 0($24)
  call_alias $24, 64, 0x0002, 17
  call_alias $25, 68, 0x0001, 18

  mfc0 $8, $16
  lui $9, 0x0002
  nor $9, $9, $zero
  and $8, $8, $9
  mtc0 $8, $16
  call_alias $25, 72, 0x0002, 19
  mfc0 $8, $16
  lui $9, 0x0002
  or $8, $8, $9
  mtc0 $8, $16
  call_alias $25, 76, 0x0001, 20
  cache 0x0b, 0($25)
  call_alias $25, 80, 0x0002, 21

  load_symbol $8, dma_target
  publish $8, 84
  load_symbol $8, self_modify_target
  publish $8, 88
  publish $24, 92
  publish $25, 96

  or $v0, $zero, $zero
fail:
  or $ra, $23, $zero
  jr $ra
  nop

  .align 6
self_modify_target:
  addiu $3, $zero, 1
  jr $ra
  nop

  .end _start

  .data
  .align 6
dma_target:
  .word 0x11112222, 0x77778888, 0x9999aaaa, 0xbbbbcccc
dma_replacement:
  .word 0x55556666, 0xddddeeee, 0xffff0000, 0x12345678

  .align 6
outputs:
  .space 100
