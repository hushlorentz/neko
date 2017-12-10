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

#endif
