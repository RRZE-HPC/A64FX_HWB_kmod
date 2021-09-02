#include <include/linux/smp.h>
#include <include/linux/cpumask.h>

#include "a64fx_hwb.h"

#ifdef __ARM_ARCH_8A


u64 read_hwb_ctrl()
{
    //IMP_BARRIER_CTRL_EL1
    u64 val = 0;
    asm ("MRS %0, S3_0_C11_C12_0" ::"r"(val));
    return val;
}

void write_hwb_ctrl(u64 val)
{
    //IMP_BARRIER_CTRL_EL1
    asm ("MSR S3_0_C11_C12_0,%0" :"=r"(val));
}

int read_init_sync_bb(int bb, u32 *mask, u32 *bst)
{
    u64 val = 0;
    switch(bb)
    {
        case 0:
            asm ("MRS %0, S3_0_C15_C13_0" :: "r"(val));
            break;
        case 1:
            asm ("MRS %0, S3_0_C15_C13_1" :: "r"(val));
            break;
        case 2:
            asm ("MRS %0, S3_0_C15_C13_2" :: "r"(val));
            break;
        case 3:
            asm ("MRS %0, S3_0_C15_C13_3" :: "r"(val));
            break;
        case 4:
            asm ("MRS %0, S3_0_C15_C13_4" :: "r"(val));
            break;
        case 5:
            asm ("MRS %0, S3_0_C15_C13_5" :: "r"(val));
            break;

    }
    *bst = val & 0x1FFF;
    *mask = (val >> 32) & 0x1FFF;
    return 0;
}

void write_init_sync_bb(int bb, u64 val)
{
    switch(bb)
    {
        case 0:
            asm ("MSR S3_0_C15_C13_0, %0" : "=r"(val));
            break;
        case 1:
            asm ("MSR S3_0_C15_C13_1, %0" : "=r"(val));
            break;
        case 2:
            asm ("MSR S3_0_C15_C13_2, %0" : "=r"(val));
            break;
        case 3:
            asm ("MSR S3_0_C15_C13_3, %0" : "=r"(val));
            break;
        case 4:
            asm ("MSR S3_0_C15_C13_4, %0" : "=r"(val));
            break;
        case 5:
            asm ("MSR S3_0_C15_C13_5, %0" : "=r"(val));
            break;

    }
}


u64 read_assign_sync_wr(int win)
{
    u64 val = 0;
    switch(win)
    {
        case 0:
            asm ("MRS %0, S3_0_C15_C15_0" :: "r" (val));
            break;
        case 1:
            asm ("MRS %0, S3_0_C15_C15_1" :: "r" (val));
            break;
        case 2:
            asm ("MRS %0, S3_0_C15_C15_2" :: "r" (val));
            break;
        case 3:
            asm ("MRS %0, S3_0_C15_C15_3" :: "r" (val));
            break;
    }
    return val;
}

void write_assign_sync_wr(int win, u64 val)
{
    switch(win)
    {
        case 0:
            asm ("MSR S3_0_C15_C15_0, %0" : "=r"(val));
            break;
        case 1:
            asm ("MSR S3_0_C15_C15_1, %0" : "=r"(val));
            break;
        case 2:
            asm ("MSR S3_0_C15_C15_2, %0" : "=r"(val));
            break;
        case 3:
            asm ("MSR S3_0_C15_C15_3, %0" : "=r"(val));
            break;
    }
}


u64 read_display()
{
    u64 val = 0;
    asm ("MRS %0, S3_0_C11_C12_4" :: "r"(val));
    return val;
}

u64 read_bst_sync_wr(int win)
{
    u64 val = 0;
    switch(win)
    {
        case 0:
            asm ("MRS %0, S3_3_C15_C15_0" :: "r" (val));
            break;
        case 1:
            asm ("MRS %0, S3_3_C15_C15_1" :: "r" (val));
            break;
        case 2:
            asm ("MRS %0, S3_3_C15_C15_2" :: "r" (val));
            break;
        case 3:
            asm ("MRS %0, S3_3_C15_C15_3" :: "r" (val));
            break;
    }
    return val & 0x1;
}
#endif

