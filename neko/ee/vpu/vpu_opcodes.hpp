#ifndef VPU_OPCODES_H
#define VPU_OPCODES_H

#define VPU_I_BIT 0x80000000
#define VPU_E_BIT 0x40000000
#define VPU_M_BIT 0x20000000
#define VPU_D_BIT 0x10000000
#define VPU_T_BIT 0x8000000
#define VPU_DEST_SHIFT 21
#define VPU_DEST_X_BIT 0x200000
#define VPU_DEST_Y_BIT 0x400000
#define VPU_DEST_Z_BIT 0x800000
#define VPU_DEST_W_BIT 0x1000000
#define VPU_DEST_ALL_FIELDS 0x1e00000
#define VPU_DEST_MASK 15
#define VPU_FT_REG_SHIFT 16 
#define VPU_FS_REG_SHIFT 11
#define VPU_FD_REG_SHIFT 6
#define VPU_REG_MASK 0x1f

#define VPU_TYPE1_MASK 0x3f
#define VPU_TYPE3_MASK 0x7ff

#define VPU_ABS 0x1fd
#define VPU_ADD 0x28
#define VPU_ADDi 0x22
#define VPU_ADDq 0x20
#define VPU_ADDx 0
#define VPU_ADDy 0x1
#define VPU_ADDz 0x2
#define VPU_ADDw 0x3
#define VPU_ADDA 0x2bc
#define VPU_ADDAi 0x23e
#define VPU_ADDAq 0x23c
#define VPU_ADDAx 0x3c
#define VPU_ADDAy 0x3d
#define VPU_ADDAz 0x3e
#define VPU_ADDAw 0x3f
#define VPU_CLIP 0x1ff
#define VPU_FTOI0 0x17c
#define VPU_FTOI4 0x17d
#define VPU_FTOI12 0x17e
#define VPU_FTOI15 0x17f
#define VPU_ITOF0 0x13c
#define VPU_ITOF4 0x13d
#define VPU_ITOF12 0x13e
#define VPU_ITOF15 0x13f
#define VPU_MADD 0x29
#define VPU_MADDi 0x23
#define VPU_MADDq 0x21
#define VPU_MADDx 0x8
#define VPU_MADDy 0x9
#define VPU_MADDz 0xa
#define VPU_MADDw 0xb
#define VPU_MADDA 0x2bd
#define VPU_MADDAi 0x23f
#define VPU_MADDAq 0x23d
#define VPU_MADDAx 0xbc
#define VPU_MADDAy 0xbd
#define VPU_MADDAz 0xbe
#define VPU_MADDAw 0xbf
#define VPU_MAX 0x2b
#define VPU_MAXi 0x1d
#define VPU_MAXx 0x10
#define VPU_MAXy 0x11
#define VPU_MAXz 0x12
#define VPU_MAXw 0x13
#define VPU_MINI 0x2f
#define VPU_MINIi 0x1f
#define VPU_MINIx 0x14
#define VPU_MINIy 0x15
#define VPU_MINIz 0x16
#define VPU_MINIw 0x17
#define VPU_MSUB 0x2d
#define VPU_MSUBi 0x27
#define VPU_MSUBq 0x25
#define VPU_MSUBx 0xc
#define VPU_MSUBy 0xd
#define VPU_MSUBz 0xe
#define VPU_MSUBw 0xf
#define VPU_MSUBA 0x2fd
#define VPU_MSUBAi 0x27f
#define VPU_MSUBAq 0x27d
#define VPU_MSUBAx 0xfc
#define VPU_MSUBAy 0xfd
#define VPU_MSUBAz 0xfe
#define VPU_MSUBAw 0xff
#define VPU_MUL 0x2a
#define VPU_MULi 0x1e
#define VPU_MULq 0x1c
#define VPU_MULx 0x18
#define VPU_MULy 0x19
#define VPU_MULz 0x1a
#define VPU_MULw 0x1b
#define VPU_MULA 0x2be
#define VPU_MULAi 0x1fe
#define VPU_MULAq 0x1fc
#define VPU_MULAx 0x1bc
#define VPU_MULAy 0x1bd
#define VPU_MULAz 0x1be
#define VPU_MULAw 0x1bf
#define VPU_NOP 0x2ff
#define VPU_OPMULA 0x2fe
#define VPU_OPMSUB 0x2e
#define VPU_SUB 0x2c
#define VPU_SUBi 0x26
#define VPU_SUBq 0x24
#define VPU_SUBx 0x4
#define VPU_SUBy 0x5
#define VPU_SUBz 0x6
#define VPU_SUBw 0x7
#define VPU_SUBA 0x2cf
#define VPU_SUBAi 0x27e
#define VPU_SUBAq 0x27c
#define VPU_SUBAx 0x7c
#define VPU_SUBAy 0x7d
#define VPU_SUBAz 0x7e
#define VPU_SUBAw 0x7f

#endif
