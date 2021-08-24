#ifndef A64FX_HWB_H
#define A64FX_HWB_H

#define MAX_NUM_CMG    4
#define MAX_PE_PER_CMG 13
#define MAX_BB_PER_CMG 6
#define MAX_BW_PER_CMG 4

struct a64fx_core_mapping {
    int cpu_id;
    int cmg_id;
    int ppe_id;
    int pe_id;
    int cmg_offset;
    u32 bw_map;
};

/*struct a64fx_bb_mapping {*/
/*    int bb_id;*/
/*};*/

struct a64fx_bw_mapping {
    int bw_id;
};

struct a64fx_barrier_window {
    u32 mask;
    u32 bst;
};

struct a64fx_barrier_blade {
    u32 mask;
    u32 bst;
    struct a64fx_barrier_window* window;
};



struct a64fx_cmg_device {
    int cmg_id;
    int num_pes;
    u32 bb_active;
    struct kobject kobj;
    struct a64fx_core_mapping pe_map[MAX_PE_PER_CMG];
    u8 bb_map[MAX_BB_PER_CMG];
    struct a64fx_bw_mapping bw_map[MAX_BW_PER_CMG];
    spinlock_t cmg_lock;
};

struct a64fx_task_allocation {
    u8 bb;
    u8 cmg;
    struct cpumask cpumask;
};

struct a64fx_task_mapping {
    struct task_struct* task;
    struct a64fx_task_allocation allocations[MAX_NUM_CMG*MAX_BB_PER_CMG];
    int num_allocations;
};

struct a64fx_hwb_device {
    int num_cmgs;
    int num_bb_per_cmg;
    int num_bw_per_cmg;
    int max_pe_per_cmg;
    struct a64fx_cmg_device cmgs[MAX_NUM_CMG];
    struct miscdevice misc;
    spinlock_t dev_lock;
    refcount_t refcount;
    int num_tasks;
    struct a64fx_task_mapping tasks[MAX_NUM_CMG*MAX_BB_PER_CMG];
};

/*u64 read_hwb_ctrl()*/
/*{*/
/*    //IMP_BARRIER_CTRL_EL1*/
/*    u64 val = 0;*/
/*    asm ("MRS %0, S3_0_C11_C12_0" ::"r"(val));*/
/*    return val;*/
/*}*/

/*void write_hwb_ctrl(u64 val)*/
/*{*/
/*    //IMP_BARRIER_CTRL_EL1*/
/*    asm ("MSR S3_0_C11_C12_0,%0" :"=r"(val));*/
/*}*/

/*int read_init_sync_bb(int bb, u32 *mask, u32 *bst)*/
/*{*/
/*    u64 val = 0;*/
/*    switch(bb)*/
/*    {*/
/*        case 0:*/
/*            asm ("MRS %0, S3_0_C15_C13_0" :: "r"(val));*/
/*            break;*/
/*        case 1:*/
/*            asm ("MRS %0, S3_0_C15_C13_1" :: "r"(val));*/
/*            break;*/
/*        case 2:*/
/*            asm ("MRS %0, S3_0_C15_C13_2" :: "r"(val));*/
/*            break;*/
/*        case 3:*/
/*            asm ("MRS %0, S3_0_C15_C13_3" :: "r"(val));*/
/*            break;*/
/*        case 4:*/
/*            asm ("MRS %0, S3_0_C15_C13_4" :: "r"(val));*/
/*            break;*/
/*        case 5:*/
/*            asm ("MRS %0, S3_0_C15_C13_5" :: "r"(val));*/
/*            break;*/

/*    }*/
/*    *bst = val & 0x1FFF;*/
/*    *mask = (val >> 32) & 0x1FFF;*/
/*    return 0;*/
/*}*/

/*void write_init_sync_bb(int bb, u64 val)*/
/*{*/
/*    switch(bb)*/
/*    {*/
/*        case 0:*/
/*            asm ("MSR S3_0_C15_C13_0, %0" : "=r"(val));*/
/*            break;*/
/*        case 1:*/
/*            asm ("MSR S3_0_C15_C13_1, %0" : "=r"(val));*/
/*            break;*/
/*        case 2:*/
/*            asm ("MSR S3_0_C15_C13_2, %0" : "=r"(val));*/
/*            break;*/
/*        case 3:*/
/*            asm ("MSR S3_0_C15_C13_3, %0" : "=r"(val));*/
/*            break;*/
/*        case 4:*/
/*            asm ("MSR S3_0_C15_C13_4, %0" : "=r"(val));*/
/*            break;*/
/*        case 5:*/
/*            asm ("MSR S3_0_C15_C13_5, %0" : "=r"(val));*/
/*            break;*/

/*    }*/
/*}*/


/*u64 read_assign_sync_wr(int win)*/
/*{*/
/*    u64 val = 0;*/
/*    switch(win)*/
/*    {*/
/*        case 0:*/
/*            asm ("MRS %0, S3_0_C15_C15_0" :: "r" (val));*/
/*            break;*/
/*        case 1:*/
/*            asm ("MRS %0, S3_0_C15_C15_1" :: "r" (val));*/
/*            break;*/
/*        case 2:*/
/*            asm ("MRS %0, S3_0_C15_C15_2" :: "r" (val));*/
/*            break;*/
/*        case 3:*/
/*            asm ("MRS %0, S3_0_C15_C15_3" :: "r" (val));*/
/*            break;*/
/*    }*/
/*    return val;*/
/*}*/

/*void write_assign_sync_wr(int win, u64 val)*/
/*{*/
/*    switch(bb)*/
/*    {*/
/*        case 0:*/
/*            asm ("MSR S3_0_C15_C15_0, %0" : "=r"(val));*/
/*            break;*/
/*        case 1:*/
/*            asm ("MSR S3_0_C15_C15_1, %0" : "=r"(val));*/
/*            break;*/
/*        case 2:*/
/*            asm ("MSR S3_0_C15_C15_2, %0" : "=r"(val));*/
/*            break;*/
/*        case 3:*/
/*            asm ("MSR S3_0_C15_C15_3, %0" : "=r"(val));*/
/*            break;*/
/*    }*/
/*}*/


/*u64 read_display()*/
/*{*/
/*    u64 val = 0;*/
/*    asm ("MRS %0, S3_0_C11_C12_4" :: "r"(val));*/
/*    return val;*/
/*}*/

/*u64 read_bst_sync_wr(int win)*/
/*{*/
/*    u64 val = 0;*/
/*    switch(win)*/
/*    {*/
/*        case 0:*/
/*            asm ("MRS %0, S3_3_C15_C15_0" :: "r" (val));*/
/*            break;*/
/*        case 1:*/
/*            asm ("MRS %0, S3_3_C15_C15_1" :: "r" (val));*/
/*            break;*/
/*        case 2:*/
/*            asm ("MRS %0, S3_3_C15_C15_2" :: "r" (val));*/
/*            break;*/
/*        case 3:*/
/*            asm ("MRS %0, S3_3_C15_C15_3" :: "r" (val));*/
/*            break;*/
/*    }*/
/*    return val & 0x1;*/
/*}*/
#endif
