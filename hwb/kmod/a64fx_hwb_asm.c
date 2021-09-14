#include <include/linux/smp.h>
#include <include/linux/cpumask.h>

#include "a64fx_hwb.h"

#ifdef __ARM_ARCH_8A
#include "a64fx_hwb_asm.h"



int read_hwb_ctrl(int *el0ae, int *el1ae)
{
    //IMP_BARRIER_CTRL_EL1
    u64 val = 0;
    if ((!el0ae) || (!el1ae))
        return -EINVAL;
    asm volatile ("MRS %0, S3_0_C11_C12_0" : "=r"(val));
    *el0ae = (val >> A64FX_HWB_CTRL_EL0AE_SHIFT) & 0x1;
    *el1ae = (val >> A64FX_HWB_CTRL_EL1AE_SHIFT) & 0x1;
    return 0;
}

int write_hwb_ctrl(int el0ae, int el1ae)
{
    //IMP_BARRIER_CTRL_EL1
    u64 val = 0;
    asm volatile ("MRS %0, S3_0_C11_C12_0" : "=r"(val));
    if (el0ae)
    {
        val |= (1ULL<<A64FX_HWB_CTRL_EL0AE_SHIFT);
    }
    else
    {
        val &= ~(1ULL<<A64FX_HWB_CTRL_EL0AE_SHIFT);
    }
    if (el1ae)
    {
        val |= (1ULL<<A64FX_HWB_CTRL_EL1AE_SHIFT);
    }
    else
    {
        val &= ~(1ULL<<A64FX_HWB_CTRL_EL1AE_SHIFT);
    }
    asm volatile ("MSR S3_0_C11_C12_0, %0" :: "r"(val));
    return 0;
}

int read_init_sync_bb(int bb, unsigned long *mask, unsigned long *bst)
{
    u64 val = 0;
    switch(bb)
    {
        case 0:
            asm volatile ("MRS %0, S3_0_C15_C13_0" : "=r"(val));
            break;
        case 1:
            asm volatile ("MRS %0, S3_0_C15_C13_1" : "=r"(val));
            break;
        case 2:
            asm volatile ("MRS %0, S3_0_C15_C13_2" : "=r"(val));
            break;
        case 3:
            asm volatile ("MRS %0, S3_0_C15_C13_3" : "=r"(val));
            break;
        case 4:
            asm volatile ("MRS %0, S3_0_C15_C13_4" : "=r"(val));
            break;
        case 5:
            asm volatile ("MRS %0, S3_0_C15_C13_5" : "=r"(val));
            break;

    }
    *bst = val & A64FX_HWB_INIT_BST_MASK;
    *mask = (val >> A64FX_HWB_INIT_BST_SHIFT) & A64FX_HWB_INIT_BST_MASK;
    return 0;
}

int write_init_sync_bb(int blade, unsigned long bst_mask)
{
    u64 val = 0;
    if ((blade < 0) || (blade >= MAX_BB_PER_CMG) || (bst_mask == 0x0UL))
    {
        return -EINVAL;
    }
/*    switch(blade)*/
/*    {*/
/*        case 0:*/
/*            asm volatile ("MRS %0, S3_0_C15_C13_0" : "=r"(val));*/
/*            break;*/
/*        case 1:*/
/*            asm volatile ("MRS %0, S3_0_C15_C13_1" : "=r"(val));*/
/*            break;*/
/*        case 2:*/
/*            asm volatile ("MRS %0, S3_0_C15_C13_2" : "=r"(val));*/
/*            break;*/
/*        case 3:*/
/*            asm volatile ("MRS %0, S3_0_C15_C13_3" : "=r"(val));*/
/*            break;*/
/*        case 4:*/
/*            asm volatile ("MRS %0, S3_0_C15_C13_4" : "=r"(val));*/
/*            break;*/
/*        case 5:*/
/*            asm volatile ("MRS %0, S3_0_C15_C13_5" : "=r"(val));*/
/*            break;*/

/*    }*/
/*    pr_info("write_init_sync_bb: old 0x%llx\n", val);*/
/*    val &= (A64FX_HWB_INIT_BST_MASK << A64FX_HWB_INIT_BST_SHIFT);*/
/*    val &= ~(1UL<<A64FX_HWB_INIT_LBSY_SHIFT);*/
/*    val &= ~(A64FX_HWB_INIT_BST_MASK);*/
    val |= (bst_mask & A64FX_HWB_INIT_BST_MASK) << A64FX_HWB_INIT_BST_SHIFT;
    pr_info("write_init_sync_bb: new 0x%llx\n", val);
    switch(blade)
    {
        case 0:
            asm volatile ("MSR S3_0_C15_C13_0, %0" :: "r"(val));
            break;
        case 1:
            asm volatile ("MSR S3_0_C15_C13_1, %0" :: "r"(val));
            break;
        case 2:
            asm volatile ("MSR S3_0_C15_C13_2, %0" :: "r"(val));
            break;
        case 3:
            asm volatile ("MSR S3_0_C15_C13_3, %0" :: "r"(val));
            break;
        case 4:
            asm volatile ("MSR S3_0_C15_C13_4, %0" :: "r"(val));
            break;
        case 5:
            asm volatile ("MSR S3_0_C15_C13_5, %0" :: "r"(val));
            break;
    }
    return 0;
}


int read_assign_sync_wr(int window, int* valid, int *blade)
{
    u64 val = 0;
    if ((window < 0) || (window >= MAX_BW_PER_CMG) || (!valid) || (!blade))
    {
        return -EINVAL;
    }
    switch(window)
    {
        case 0:
            asm volatile ("MRS %0, S3_0_C15_C15_0" : "=r" (val));
            break;
        case 1:
            asm volatile ("MRS %0, S3_0_C15_C15_1" : "=r" (val));
            break;
        case 2:
            asm volatile ("MRS %0, S3_0_C15_C15_2" : "=r" (val));
            break;
        case 3:
            asm volatile ("MRS %0, S3_0_C15_C15_3" : "=r" (val));
            break;
    }
    *valid = (val >> A64FX_HWB_ASSIGN_VALID_BIT) & 0x1;
    *blade = (val & A64FX_HWB_ASSIGN_BB_MASK);
    return 0;
}

int write_assign_sync_wr(int window, int valid, int blade)
{
    u64 val = 0;
    if ((window < 0) || (window >= MAX_BW_PER_CMG) || (blade < 0) || (blade >= MAX_BB_PER_CMG))
        return -EINVAL;
/*    switch(window)*/
/*    {*/
/*        case 0:*/
/*            asm volatile ("MRS %0, S3_0_C15_C15_0" : "=r" (val));*/
/*            break;*/
/*        case 1:*/
/*            asm volatile ("MRS %0, S3_0_C15_C15_1" : "=r" (val));*/
/*            break;*/
/*        case 2:*/
/*            asm volatile ("MRS %0, S3_0_C15_C15_2" : "=r" (val));*/
/*            break;*/
/*        case 3:*/
/*            asm volatile ("MRS %0, S3_0_C15_C15_3" : "=r" (val));*/
/*            break;*/
/*    }*/
/*    pr_info("write_assign_sync_wr: old 0x%llx\n", val);*/
/*    val &= ~(A64FX_HWB_ASSIGN_BB_MASK);*/
    val |= (blade & A64FX_HWB_ASSIGN_BB_MASK);
    if (valid)
    {
        val |= (1ULL<<A64FX_HWB_ASSIGN_VALID_BIT);
    }
    else
    {
        val &= ~(1ULL<<A64FX_HWB_ASSIGN_VALID_BIT);
    }
    pr_info("write_assign_sync_wr: new 0x%llx\n", val);
    switch(window)
    {
        case 0:
            asm volatile ("MSR S3_0_C15_C15_0, %0" :: "r"(val));
            break;
        case 1:
            asm volatile ("MSR S3_0_C15_C15_1, %0" :: "r"(val));
            break;
        case 2:
            asm volatile ("MSR S3_0_C15_C15_2, %0" :: "r"(val));
            break;
        case 3:
            asm volatile ("MSR S3_0_C15_C15_3, %0" :: "r"(val));
            break;
    }
    return 0;
}

// not required, asm call directly in _oss_a64fx_hwb_get_peinfo()
int read_peinfo(u8 *cmg, u8 *ppe)
{
    u64 val = 0;
    if ((!cmg) || (!ppe))
        return -EINVAL;
    asm volatile ("MRS %0, S3_0_C11_C12_4" : "=r"(val));
    *ppe = (u8)(val & A64FX_PEINFO_PPE_MASK);
    *cmg = (u8)((val >> A64FX_PEINFO_CMG_OFFSET) & A64FX_PEINFO_CMG_MASK);
    return 0;
}

int read_bst_sync_wr(int window, int* sync)
{
    u64 val = 0;
    if ((window < 0) || (window >= MAX_BW_PER_CMG) || (!sync))
        return -EINVAL;
    switch(window)
    {
        case 0:
            asm volatile ("MRS %0, S3_3_C15_C15_0" : "=r" (val));
            break;
        case 1:
            asm volatile ("MRS %0, S3_3_C15_C15_1" : "=r" (val));
            break;
        case 2:
            asm volatile ("MRS %0, S3_3_C15_C15_2" : "=r" (val));
            break;
        case 3:
            asm volatile ("MRS %0, S3_3_C15_C15_3" : "=r" (val));
            break;
    }
    *sync = val & A64FX_HWB_SYNC_WINDOW_MASK;
    return 0;
}

int write_bst_sync_wr(int window, int sync)
{
    u64 val = 0;
    if ((window < 0) || (window >= MAX_BW_PER_CMG))
        return -EINVAL;
/*    switch(window)*/
/*    {*/
/*        case 0:*/
/*            asm volatile ("MRS %0, S3_3_C15_C15_0" : "=r" (val));*/
/*            break;*/
/*        case 1:*/
/*            asm volatile ("MRS %0, S3_3_C15_C15_1" : "=r" (val));*/
/*            break;*/
/*        case 2:*/
/*            asm volatile ("MRS %0, S3_3_C15_C15_2" : "=r" (val));*/
/*            break;*/
/*        case 3:*/
/*            asm volatile ("MRS %0, S3_3_C15_C15_3" : "=r" (val));*/
/*            break;*/
/*    }*/
/*    pr_info("write_bst_sync_wr: old 0x%llx\n", val);*/
    if (sync)
    {
        val |= A64FX_HWB_SYNC_WINDOW_MASK;
    }
    else
    {
        val &= ~(A64FX_HWB_SYNC_WINDOW_MASK);
    }
    pr_info("write_bst_sync_wr: new 0x%llx\n", val);
    switch(window)
    {
        case 0:
            asm volatile ("MSR S3_3_C15_C15_0, %0" :: "r" (val));
            break;
        case 1:
            asm volatile ("MSR S3_3_C15_C15_1, %0" :: "r" (val));
            break;
        case 2:
            asm volatile ("MSR S3_3_C15_C15_2, %0" :: "r" (val));
            break;
        case 3:
            asm volatile ("MSR S3_3_C15_C15_3, %0" :: "r" (val));
            break;
    }
    return 0;
}


#endif

