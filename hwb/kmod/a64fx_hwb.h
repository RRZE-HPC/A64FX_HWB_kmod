#ifndef A64FX_HWB_H
#define A64FX_HWB_H

#include <linux/refcount.h>
#include <linux/kobject.h>
#include <linux/miscdevice.h>

#define MAX_NUM_CMG    4
#define MAX_PE_PER_CMG 13
#define MAX_BB_PER_CMG 6
#define MAX_BW_PER_CMG 4

#define IMP_BARRIER_CTRL_EL1_EL0AE_BIT 62
#define IMP_BARRIER_CTRL_EL1_EL1AE_BIT 63

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
    unsigned long bb_active;
    unsigned long bw_active;
    struct kobject kobj;
    struct a64fx_core_mapping pe_map[MAX_PE_PER_CMG];
    long unsigned int bb_map[MAX_BB_PER_CMG];
    long unsigned int bw_map[MAX_BW_PER_CMG];
/*    struct a64fx_bw_mapping bw_map[MAX_BW_PER_CMG];*/
    spinlock_t cmg_lock;
};

struct a64fx_task_allocation {
    u8 bb;
    u8 cmg;
    long unsigned int win_mask;
    struct task_struct* task;
    struct list_head list;
};

struct a64fx_task_mapping {
    struct task_struct* task;
    refcount_t refcount;
    struct cpumask cpumask;
    struct list_head list;
    struct list_head allocs;
    //struct a64fx_task_allocation allocations[MAX_NUM_CMG*MAX_BB_PER_CMG];
    int num_allocs;
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
    struct list_head task_list;
    struct a64fx_task_mapping tasks[MAX_NUM_CMG*MAX_BB_PER_CMG];
};


#endif
