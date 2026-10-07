  .set noreorder
  .text
  .globl _start
  .ent _start

  .macro check_store reg, offset, upper, lower, code
  sw \reg, \offset($19)
  lui $10, \upper
  ori $10, $10, \lower
  addiu $v0, $zero, \code
  bne \reg, $10, fail
  nop
  .endm

_start:
  lui $19, %hi(outputs)
  addiu $19, $19, %lo(outputs)

  # Pin Random at entry 47 so TLBWR has a deterministic destination.
  addiu $8, $zero, 47
  mtc0 $8, $6
  mfc0 $9, $6
  check_store $9, 0, 0x0000, 0x002f, 1
  mfc0 $9, $1
  check_store $9, 4, 0x0000, 0x002f, 2

  # Write entry 5 through the decoded indexed path.
  addiu $8, $zero, 5
  mtc0 $8, $0
  ori $8, $zero, 0x6000
  mtc0 $8, $5
  lui $8, 0x1234
  ori $8, $8, 0x002a
  mtc0 $8, $10
  lui $8, 0x0001
  ori $8, $8, 0x001e
  mtc0 $8, $2
  lui $8, 0x0002
  ori $8, $8, 0x001a
  mtc0 $8, $3
  tlbwi

  # Clear the transfer registers, read entry 5 back, and publish it.
  mtc0 $zero, $5
  mtc0 $zero, $10
  mtc0 $zero, $2
  mtc0 $zero, $3
  tlbr
  mfc0 $9, $5
  check_store $9, 8, 0x0000, 0x6000, 3
  mfc0 $9, $10
  check_store $9, 12, 0x1234, 0x002a, 4
  mfc0 $9, $2
  check_store $9, 16, 0x0001, 0x001e, 5
  mfc0 $9, $3
  check_store $9, 20, 0x0002, 0x001a, 6

  # Probe the indexed mapping with matching and mismatching ASIDs.
  lui $8, 0x1234
  ori $8, $8, 0x002a
  mtc0 $8, $10
  tlbp
  mfc0 $9, $0
  check_store $9, 24, 0x0000, 0x0005, 7

  lui $8, 0x1234
  ori $8, $8, 0x002b
  mtc0 $8, $10
  tlbp
  mfc0 $9, $0
  check_store $9, 28, 0x8000, 0x0000, 8

  # Write a combined-global mapping at entry 6 and probe with another ASID.
  addiu $8, $zero, 6
  mtc0 $8, $0
  mtc0 $zero, $5
  lui $8, 0x2345
  ori $8, $8, 0x6011
  mtc0 $8, $10
  lui $8, 0x0005
  ori $8, $8, 0x001f
  mtc0 $8, $2
  lui $8, 0x0006
  ori $8, $8, 0x001b
  mtc0 $8, $3
  tlbwi

  lui $8, 0x2345
  ori $8, $8, 0x60ff
  mtc0 $8, $10
  tlbp
  mfc0 $9, $0
  check_store $9, 32, 0x0000, 0x0006, 9

  # TLBWR uses the pinned Random value and writes entry 47.
  lui $8, 0x0001
  ori $8, $8, 0xe000
  mtc0 $8, $5
  lui $8, 0x3400
  ori $8, $8, 0x0033
  mtc0 $8, $10
  lui $8, 0x0003
  ori $8, $8, 0x001f
  mtc0 $8, $2
  lui $8, 0x0004
  ori $8, $8, 0x001b
  mtc0 $8, $3
  tlbwr

  addiu $8, $zero, 47
  mtc0 $8, $0
  tlbr
  mfc0 $9, $0
  check_store $9, 36, 0x0000, 0x002f, 10
  mfc0 $9, $5
  check_store $9, 40, 0x0001, 0xe000, 11
  mfc0 $9, $10
  check_store $9, 44, 0x3400, 0x0033, 12
  mfc0 $9, $2
  check_store $9, 48, 0x0003, 0x001f, 13
  mfc0 $9, $3
  check_store $9, 52, 0x0004, 0x001b, 14
  mfc0 $9, $1
  check_store $9, 56, 0x0000, 0x002f, 15
  mfc0 $9, $6
  check_store $9, 60, 0x0000, 0x002f, 16

  or $v0, $zero, $zero
fail:
  jr $ra
  nop

  .end _start

  .data
  .align 4
outputs:
  .space 64
