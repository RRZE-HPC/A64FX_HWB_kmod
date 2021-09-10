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

#define A64FX_HWB_UNASSIGNED_WIN (-1)
#define A64FX_HWB_UNASSIGNED_BB (-1)

struct a64fx_core_mapping {
    int cpu_id;
    int cmg_id;
    int ppe_id;
    int pe_id;
    int cmg_offset;
    unsigned long bw_map;
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
    struct cpumask cmgmask;
    struct a64fx_core_mapping pe_map[MAX_PE_PER_CMG];
    int bw_map[MAX_BW_PER_CMG];
    spinlock_t cmg_lock;
};

struct a64fx_task_allocation {
    u8 blade;
    u8 cmg;
    long unsigned int win_mask;
    int window;
    int assign_count_safe;
    struct cpumask assign_mask;
    refcount_t assign_count;
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
    refcount_t num_allocs;
    int num_allocs_safe;
};

struct a64fx_hwb_device {
    int num_cmgs;
    int num_bb_per_cmg;
    int num_bw_per_cmg;
    int max_pe_per_cmg;
    struct a64fx_cmg_device cmgs[MAX_NUM_CMG];
    struct miscdevice misc;
    spinlock_t dev_lock;
    refcount_t active_count;
    int active_count_safe;
    refcount_t num_tasks;
    int num_tasks_safe;
    struct list_head task_list;
};


#endif
