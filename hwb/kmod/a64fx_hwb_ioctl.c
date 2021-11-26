#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/cpumask.h>
#include <linux/bitmap.h>
#include <include/linux/smp.h>
#include <include/linux/cpumask.h>

#include "a64fx_hwb.h"
#include "a64fx_hwb_cmg.h"
#include "a64fx_hwb_asm.h"
#include "fujitsu_hpc_ioctl.h"

// Function to check a given cpumask whether it contains only CPUs of a single
// CMG, the mask contains at least two CPUs and all CPUs are online.
static int check_cpumask(struct a64fx_hwb_device *dev, struct cpumask *cpumask)
{
    int i = 0;
    int j = 0;
    int cpu = -1;
    int cmg = -1;
    if (cpumask_weight(cpumask) < 2)
    {
        return -EINVAL;
    }
    for_each_cpu(cpu, cpumask)
    {
        if (!cpu_online(cpu)) return -1;
        for (i = 0; i < dev->num_cmgs; i++)
        {
            for (j = 0; j < dev->cmgs[i].num_pes; j++)
            {
                if (dev->cmgs[i].pe_map[j].cpu_id == cpu)
                {
                    if (cmg < 0)
                    {
                        cmg = dev->cmgs[i].pe_map[j].cmg_id;
                    }
                    if (cmg != dev->cmgs[i].pe_map[j].cmg_id)
                    {
                        return -1;
                    }
                }
            }
        }
    }
    return cmg;
}


// Function to extract the CPUs on a CMG out of the cpumask and provide them as ppemask,
// the phyiscal location of a CPU/PE inside a CMG
static int cpumask_to_ppemask(struct a64fx_cmg_device* cmg, struct cpumask *cpumask, unsigned long* ppemask)
{
    int cpu = 0;
    int i = 0;
    unsigned long mask = 0x0UL;
    if ((!cmg) || (!ppemask) || (!cpumask))
    {
        return -EINVAL;
    }
    for_each_cpu(cpu, cpumask)
    {
        for (i = 0; i < cmg->num_pes; i++)
        {
            if (cmg->pe_map[i].cpu_id == cpu)
            {
                pr_debug("CPU %d -> CMG %d PPE %d\n", cpu, cmg->cmg_id, cmg->pe_map[i].ppe_id);
                set_bit(cmg->pe_map[i].ppe_id, &mask);
            }
        }
    }
    *ppemask = mask;
    return 0;
}

// Get the core_mapping structure for a given CMG and PPE. The core_mapping contains the window
// mapping
static struct a64fx_core_mapping* get_pemap(struct a64fx_hwb_device *dev, int cmg, int ppe)
{
    int i = 0;
    struct a64fx_core_mapping* pe = NULL;
    struct a64fx_cmg_device *cmgdev = &dev->cmgs[cmg];
    for (i = 0; i < cmgdev->num_pes; i++)
    {
        pe = &cmgdev->pe_map[i];
        if (pe->cmg_id == cmg && pe->ppe_id == ppe)
        {
            return pe;
        }
    }
    return NULL;
}


// Structure and function to be executed on a specific CPU via smp_call_function_*
// for allocating a barrier blade for a set of CPUs (ppemask)
struct hwb_allocate_info {
    int blade;
    int cmg;
    unsigned long ppemask;
};

static void oss_a64fx_hwb_allocate_func(void* info)
{
    struct hwb_allocate_info* ainfo = (struct hwb_allocate_info*) info;
    write_init_sync_bb(ainfo->blade, ainfo->ppemask);
    pr_debug("write_init_sync_bb (CMG %d, Blade %d, PPEmask 0x%lX) on CPU %d\n", ainfo->cmg, ainfo->blade, ainfo->ppemask, smp_processor_id());
}


// Structure and function to be executed on a specific CPU via smp_call_function_*
// for assigning a CPU-local window to the CMG-wide barrier blade
struct hwb_assign_info {
    int cpu;
    int cmg;
    int blade;
    int valid;
    int window;
};

static void oss_a64fx_hwb_assign_func(void* info)
{
    struct hwb_assign_info* ainfo = (struct hwb_assign_info*) info;
    write_assign_sync_wr(ainfo->window, ainfo->valid, ainfo->blade);
    pr_debug("write_assign_sync_wr (CMG %d, Blade %d, Window %d, Valid %d) on CPU %d\n", ainfo->cmg, ainfo->blade, ainfo->window, ainfo->valid, smp_processor_id());
}


// Each task can have multiple barriers allocated. The blade number inside the task contains information like participating
// CPUs, assigned CPU-specific window registers, ...
static struct a64fx_task_allocation * get_allocation(struct a64fx_cmg_device *cmg, struct a64fx_task_mapping *taskmap, int blade)
{
    struct list_head *cur = NULL;
    struct a64fx_task_allocation *alloc = NULL;
    list_for_each(cur, &taskmap->allocs)
    {
        alloc = list_entry(cur, struct a64fx_task_allocation, list);
        if (alloc->cmg == cmg->cmg_id && alloc->blade == blade)
        {
            return alloc;
        }
    }
    return NULL;
}

// Add a new allocation for a task for a barrier blade with the given cpumask. The cpumask should contain only CPUs located on the same CMG. In this module,
// this is done in the IOCTL allocate function.
static struct a64fx_task_allocation * new_allocation(struct a64fx_cmg_device *cmg, struct a64fx_task_mapping *taskmap, int blade, struct cpumask *cpumask)
{
    int i = 0;
    struct a64fx_task_allocation *alloc = NULL;
    struct hwb_allocate_info info = {0, 0UL};
    alloc = get_allocation(cmg, taskmap, blade);
    if (alloc)
    {
        pr_debug("Error allocation already exists\n");
        return NULL;
    }
    // no allocation for blade on CMG, allocate new one
    alloc = kmalloc(sizeof(struct a64fx_task_allocation), GFP_KERNEL);
    if (!alloc)
    {
        return NULL;
    }
    pr_debug("New allocation for CMG %d and Blade %d\n", cmg->cmg_id, blade);
    // save all required data in the allocation and add it to the task's allocations
    alloc->cmg = (u8)cmg->cmg_id;
    alloc->blade = (u8)blade;
    for (i = 0; i < MAX_PE_PER_CMG; i++)
        alloc->window[i] = A64FX_HWB_UNASSIGNED_WIN;
    alloc->task = taskmap->task;
    INIT_LIST_HEAD(&alloc->list);
    alloc->assign_count = 0;
    cpumask_clear(&alloc->assign_mask);
    cpumask_copy(&alloc->cpumask, cpumask);
    pr_debug("Add allocation for task %d\n", taskmap->task->pid);
    list_add(&alloc->list, &taskmap->allocs);
    taskmap->num_allocs++;

    // configure barrier blade register with given cpumask
    // it uses any of a CMG's CPUs
    info.blade = blade;
    info.cmg = cmg->cmg_id;
    cpumask_to_ppemask(cmg, &alloc->cpumask, &info.ppemask);
    pr_debug("PPEmask is 0x%lx\n", info.ppemask);
    smp_call_function_any(&cmg->cmgmask, oss_a64fx_hwb_allocate_func, &info, 1);
    pr_debug("Set blade %d in CMG %d active\n", info.blade, info.cmg);
    set_bit(info.blade, &cmg->bb_active);

    pr_debug("Task %d has %d allocations\n", task_pid_nr(taskmap->task), taskmap->num_allocs);
    return 0;
}

// Helper function to create a new allocation if it does not already exist
static struct a64fx_task_allocation * register_allocation(struct a64fx_cmg_device *cmg, struct a64fx_task_mapping *taskmap, int blade, struct cpumask *cpumask)
{
    struct a64fx_task_allocation *alloc = NULL;
    alloc = get_allocation(cmg, taskmap, blade);
    if (!alloc)
    {
        alloc = new_allocation(cmg, taskmap, blade, cpumask);
    }
    return alloc;
}


// Free an allocation. We have to check whether there are still CPUs assigned to the blade
// associated with an allocation. If there are, free the window registers on the still assigned CPUs
// Afterwards the barrier blade register is freed and the allocation removed for the task
static int free_allocation(struct a64fx_cmg_device *cmg, struct a64fx_task_mapping *taskmap, struct a64fx_task_allocation* alloc)
{
    int i = 0;
    struct hwb_allocate_info info = {0, 0UL};
    if (test_bit(alloc->blade, &cmg->bb_active))
    {
        int cpu = 0;
        if (alloc->assign_count > 0)
        {
            struct a64fx_core_mapping* pemap = NULL;
            pr_err("Allocation (PID %d CMG %d Blade %d) still assigned by %d threads\n", task_pid_nr(taskmap->task), alloc->cmg, alloc->blade, alloc->assign_count);
            for_each_cpu(cpu, &alloc->assign_mask)
            {
                struct hwb_assign_info ainfo = {
                    .cpu = cpu,
                    .cmg = alloc->cmg,
                    .blade = 0,
                    .valid = 0,
                };
                for (i = 0; i < cmg->num_pes; i++)
                {
                    if (cmg->pe_map[i].cpu_id == cpu)
                    {
                        pemap = &cmg->pe_map[i];
                        break;
                    }
                }
                ainfo.window = alloc->window[pemap->ppe_id];
                if (alloc->window[pemap->ppe_id] >= 0 && alloc->window[pemap->ppe_id] < MAX_BW_PER_CMG)
                {
                    pr_debug("Clear window %d on CPU %d\n", alloc->window[pemap->ppe_id], cpu);
                    smp_call_function_single(pemap->cpu_id, oss_a64fx_hwb_assign_func, &ainfo, 1);
                    cpumask_clear_cpu(cpu, &alloc->assign_mask);
                    clear_bit(alloc->window[pemap->ppe_id], &pemap->bw_map);
                    alloc->window[pemap->ppe_id] = A64FX_HWB_UNASSIGNED_WIN;
                    alloc->assign_count--;
                }
            }
        }
        pr_debug("PID %d free BB %d at CMG %d\n", task_pid_nr(taskmap->task), alloc->blade, alloc->cmg);
        info.blade = alloc->blade;
        info.cmg = alloc->cmg;
        info.ppemask = 0x0UL;
        smp_call_function_any(&cmg->cmgmask, oss_a64fx_hwb_allocate_func, &info, 1);
        clear_bit(alloc->blade, &cmg->bb_active);
    }
    else
    {
        pr_debug("AAAH! Task %d free inactive Blade %d at CMG %d\n", task_pid_nr(taskmap->task), alloc->blade, alloc->cmg);
    }
    list_del(&alloc->list);
    kfree(alloc);
    taskmap->num_allocs--;
    return 0;
}

// Helper function to the task->allocation mapping for a task. The task group identifier
// is used to identify the relevant task mapping.
struct a64fx_task_mapping * get_taskmap(struct a64fx_hwb_device *dev, struct task_struct* task)
{
    struct list_head* cur = NULL;
    struct a64fx_task_mapping *taskmap = NULL;
    pr_debug("Get task (PID %d TGID %d)\n", task_pid_nr(task), task_tgid_nr(task));
    list_for_each(cur, &dev->task_list)
    {
        taskmap = list_entry(cur, struct a64fx_task_mapping, list);
        if (task_tgid_nr(taskmap->task) == task_tgid_nr(task))
        {
            return taskmap;
        }
    }
    return NULL;
}

// Create a new task mapping with zero allocations
static struct a64fx_task_mapping * new_taskmap(struct a64fx_hwb_device *dev, struct task_struct* task)
{
    struct a64fx_task_mapping *taskmap = NULL;
    taskmap = get_taskmap(dev, task);
    if (taskmap)
    {
        pr_debug("Error task already exists\n");
        return NULL;
    }
    taskmap = kmalloc(sizeof(struct a64fx_task_mapping), GFP_KERNEL);
    if (!taskmap)
    {
        return NULL;
    }
    pr_debug("New task (PID %d TGID %d)\n", task_pid_nr(task), task_tgid_nr(task));
    taskmap->task = task;
    pr_debug("Initialize list of allocations\n");
    taskmap->num_allocs = 0;
    INIT_LIST_HEAD(&taskmap->allocs);
    pr_debug("Add task to global task_list\n");
    INIT_LIST_HEAD(&taskmap->list);
    list_add(&taskmap->list, &dev->task_list);
    dev->num_tasks++;
    pr_debug("Currently %d tasks with allocations\n", dev->num_tasks);
    return taskmap;

}

// Unregister a task. If a task still has allocations, free the allocated barrier blades
int unregister_task(struct a64fx_hwb_device *dev, struct a64fx_task_mapping *taskmap)
{
    struct list_head *cur = NULL, *tmp = NULL;
    struct a64fx_task_allocation *alloc = NULL;
    if (taskmap)
    {
        if (taskmap->num_allocs > 0)
        {
            pr_debug("Task %d has %d allocations left, free all\n", task_pid_nr(taskmap->task), taskmap->num_allocs);
            list_for_each_safe(cur, tmp, &taskmap->allocs)
            {
                struct a64fx_cmg_device *cmg = NULL;
                alloc = list_entry(cur, struct a64fx_task_allocation, list);
                cmg = &dev->cmgs[(int)alloc->cmg];
                free_allocation(cmg, taskmap, alloc);
            }
        }
        pr_debug("Remove task %d\n", task_pid_nr(taskmap->task));
        list_del(&taskmap->list);
        kfree(taskmap);
        dev->num_tasks--;
        pr_debug("Currently %d/ tasks with allocations\n", dev->num_tasks);
    }
    return 0;
}


// Get the CMG and phyiscal PE offset inside the CMG from the hardware
// The A64FX provides a special register on each CPU for this purpose
static int _oss_a64fx_hwb_get_peinfo(u8* cmg, u8 * ppe)
{
    u64 val = 0;
    asm volatile ("MRS %0, S3_0_C11_C12_4" : "=r"(val) );
    *ppe = (u8)(val & 0xF);
    *cmg = (u8)((val >> 4) & 0x3);
    pr_debug("get_peinfo for CPU %d CMG%u PPE %u Raw 0x%llX\n", smp_processor_id(), *cmg, *ppe, val);
    return 0;
}

int oss_a64fx_hwb_get_peinfo(int *cmg, int *ppe)
{
    int err = 0;
    u8 ucmg;
    u8 uppe;
    err = read_peinfo(&ucmg, &uppe);
    if (err < 0)
    {
        pr_err("Invalid parameters for read_peinfo\n");
        return err;
    }
    *cmg = (int)ucmg;
    *ppe = (int)uppe;
    return 0;
}


// Entry function to get the CMG and PPE offset through IOCTL
int oss_a64fx_hwb_get_peinfo_ioctl(unsigned long arg)
{
    int cmg, ppe;
    struct fujitsu_hwb_ioc_pe_info ustruct = {0};
    if (copy_from_user(&ustruct, (struct fujitsu_hwb_ioc_pe_info __user *)arg, sizeof(struct fujitsu_hwb_ioc_pe_info)))
    {
        pr_err("Error to get pe_info data\n");
        return -1;
    }
    // Based on the user-space library published by Fujitsu, these are the default error values
    ustruct.cmg = 0xFF;
    ustruct.ppe = 0xFF;
    if (!oss_a64fx_hwb_get_peinfo(&cmg, &ppe))
    {
        ustruct.cmg = (u8)cmg;
        ustruct.ppe = (u8)ppe;
    }
    if (copy_to_user((struct fujitsu_hwb_ioc_pe_info __user *)arg, &ustruct, sizeof(struct fujitsu_hwb_ioc_pe_info)))
    {
        pr_err("Error to copy back pe_info data\n");
        return -1;
    }
    return 0;
}


// Allocate a barrier blade with the given cpumask at the given CMG
// It returns the barrier blade allocated to be used by the user-space library as part of its bbid
int oss_a64fx_hwb_allocate(struct a64fx_hwb_device *dev, int cmg, struct cpumask *cpumask, int *blade)
{
    int err = 0;
    int bit = 0;
    struct a64fx_cmg_device *cmgdev = NULL;
    struct a64fx_task_mapping *taskmap = NULL;
    
    struct task_struct* current_task = get_current();
    if (cmg < 0 || cmg > MAX_NUM_CMG || (!blade))
    {
        return -EINVAL;
    }

    // acquire lock
    spin_lock(&dev->dev_lock);

    taskmap = get_taskmap(dev, current_task);
    if (!taskmap)
    {
        taskmap = new_taskmap(dev, current_task);
        if (!taskmap)
        {
            pr_err("Failed to register task or get existing mapping\n");
            err = -1;
            goto allocate_exit;
        }
    }
    cmgdev = &dev->cmgs[cmg];
    err = -ENODEV;
    // Search for a free barrier blade for a CMG
    bit = find_first_zero_bit(&cmgdev->bb_active, MAX_BB_PER_CMG);
    pr_debug("Blade %d is free, use it\n", bit);
    if (bit >= 0 && bit < dev->num_bb_per_cmg)
    {
        // Free blade found, register the allocation
        register_allocation(cmgdev, taskmap, bit, cpumask);
        *blade = bit;
        err = 0;
    }

allocate_exit:
    spin_unlock(&dev->dev_lock);
    pr_debug("Allocate returns %d\n", err);
    return err;
}

// Entry point for the allocation IOCTL
int oss_a64fx_hwb_allocate_ioctl(struct a64fx_hwb_device *dev, unsigned long arg)
{
    int err = 0;
    int cmg_id = 0;
    int bb_id = 0;
    unsigned long mask = 0;
    int cpu;
    struct cpumask *cpumask;
    struct fujitsu_hwb_ioc_bb_ctl ioc_bb_ctl = {0};
    struct fujitsu_hwb_ioc_bb_ctl __user *uarg = (struct fujitsu_hwb_ioc_bb_ctl __user *)arg;
    
    pr_debug("Start allocate\n");
    if (copy_from_user(&ioc_bb_ctl, uarg, sizeof(struct fujitsu_hwb_ioc_bb_ctl __user)))
    {
        pr_err("Error to get bb_ctl data\n");
        return -EINVAL;
    }
    pr_debug("Start allocate (pemask)\n");
    // Not 100% sure whether it is a single unsigned long or more. The ioc_bb_ctl contain a field
    // for mask size which is currently not used. But since the A64FX has less than 64 CPUs, a single
    // unsigned long should be enough.
    if (copy_from_user(&mask, (unsigned long __user *)ioc_bb_ctl.pemask, sizeof(unsigned long __user)))
    {
        pr_err("Error to get bb_ctl pemask data, %d\n", err);
        return -EINVAL;
    }
    pr_debug("Read cpumask 0x%lX\n", mask);
    cpumask = to_cpumask(&mask);
    for_each_cpu(cpu, cpumask)
	   {
		   pr_debug("CPU %d\n", cpu);
	   } 
    err = check_cpumask(dev, cpumask);
    if (err < 0)
    {
        pr_err("cpumask spans multiple CMGs, contains only a single CPU or contains offline CPUs\n");
        return -EINVAL;
    }
    cmg_id = err;
    bb_id = (int)ioc_bb_ctl.bb;
    pr_debug("Receive CMG %d and Blade %d from userspace\n", cmg_id, bb_id);
    err = oss_a64fx_hwb_allocate(dev, cmg_id, cpumask, &bb_id);
    if (err)
    {
        return err;
    }
    pr_debug("Return CMG %d and blade %d to userspace\n", cmg_id, bb_id);
    ioc_bb_ctl.bb = (u8)bb_id;
    ioc_bb_ctl.cmg = (u8)cmg_id;
    
    if (copy_to_user((struct fujitsu_hwb_ioc_bb_ctl __user *)arg, &ioc_bb_ctl, sizeof(struct fujitsu_hwb_ioc_bb_ctl __user)))
    {
        pr_err("Error to copy back bb_ctl data\n");
        return -1;
    }
    return 0;
}


// Free an allocated barrier blade at given CMG
int oss_a64fx_hwb_free(struct a64fx_hwb_device *dev, int cmg_id, int blade)
{

    int err = -EINVAL;
    struct a64fx_cmg_device *cmg = NULL;
    struct a64fx_task_mapping *taskmap = NULL;
    struct a64fx_task_allocation *alloc = NULL;
    struct task_struct* current_task = get_current();

    spin_lock(&dev->dev_lock);
    taskmap = get_taskmap(dev, current_task);
    if (taskmap)
    {
        cmg = &dev->cmgs[cmg_id];
        alloc = get_allocation(cmg, taskmap, blade);
        if (!alloc)
        {
            pr_err("Blade %d on CMG %d not allocated by task\n", blade, cmg_id);
            goto free_exit;
        }
        // Only the task which allocated the barrier blade, can free it.
        if (task_pid_nr(current_task) == task_pid_nr(taskmap->task))
        {
            free_allocation(cmg, taskmap, alloc);
            if (taskmap->num_allocs == 0)
            {
                pr_debug("Task has no more allocations, unregister it\n");
                unregister_task(dev, taskmap);
            }
            err = 0;
        }
        else if (task_tgid_nr(current_task) == task_tgid_nr(taskmap->task))
        {
            // The task contains to the same task group but is not the task
            // that originally allocated the barrier blade.
            // In this case, we check whether the task is still assigned 
            // to the blade. If yes, unassign it and remove the CPU from the
            // allocation's cpumask so that the other tasks of the group
            // can still use the barrier.
            u8 cmg8, ppe8;
            struct a64fx_core_mapping* pe = NULL;
            int cpuid = get_cpu();
            _oss_a64fx_hwb_get_peinfo(&cmg8, &ppe8);
            pe = get_pemap(dev, (int)cmg8, (int)ppe8);
            pr_debug("Finishing only for CPU %d PE %d Blade %d\n", cpuid, pe->ppe_id, blade);
            
            if (cpumask_test_cpu(cpuid, &alloc->cpumask))
            {
                struct hwb_allocate_info info = {0, 0UL};
                if (cpumask_test_cpu(cpuid, &alloc->assign_mask))
                {
                    
                    struct hwb_assign_info ainfo = {
                        .cpu = pe->cpu_id,
                        .cmg = alloc->cmg,
                        .blade = 0,
                        .valid = 0,
                    };
                    pr_debug("CPU %d PE %d Blade %d assigned with win %d\n", cpuid, pe->ppe_id, blade, alloc->window[pe->ppe_id]);
                    // unassign window on CPU
                    clear_bit(alloc->window[pe->ppe_id], &pe->bw_map);
                    pe->win_blades[alloc->window[pe->ppe_id]] = A64FX_HWB_UNASSIGNED_WIN;
                    alloc->window[pe->ppe_id] = A64FX_HWB_UNASSIGNED_WIN;
                    smp_call_function_single(pe->cpu_id, oss_a64fx_hwb_assign_func, &ainfo, 1);
                    cpumask_clear_cpu(cpuid, &alloc->assign_mask);
                    alloc->assign_count--;
                }
                // Remove CPU from barrier blade
                info.blade = alloc->blade;
                info.cmg = alloc->cmg;
                info.ppemask = 0x0UL;
                cpumask_clear_cpu(cpuid, &alloc->cpumask);
                cpumask_to_ppemask(cmg, &alloc->cpumask, &info.ppemask);
                smp_call_function_any(&cmg->cmgmask, oss_a64fx_hwb_allocate_func, &info, 1);
                // Not sure if this is needed as only the original allocating CPU should
                // be able to free an allocation. This would be only needed if the originally
                // allocating CPU already vanished from the cpumask.
                if (cpumask_weight(&alloc->cpumask) == 0)
                {
                    pr_debug("No more CPUs in cpumask, free alloc\n");
                    free_allocation(cmg, taskmap, alloc);
                    if (taskmap->num_allocs == 0)
                    {
                        pr_debug("No more allocations, free task\n");
                        unregister_task(dev, taskmap);
                    }
                }
            }
            err = 0;
        }
        else
        {
            // The task has nothing to do with the task group
            pr_debug("Task cannot free barrier allocated by another process\n");
            err = -EINVAL;
        }
    }
free_exit:
    spin_unlock(&dev->dev_lock);
    pr_debug("Free returns %d\n", err);
    return err;
}


// Entry point to free a barrier blade
int oss_a64fx_hwb_free_ioctl(struct a64fx_hwb_device *dev, unsigned long arg)
{
    int err = 0;
    int cmg_id = 0;
    int bb_id = 0;
    struct fujitsu_hwb_ioc_bb_ctl ioc_bb_ctl = {0};
    if (copy_from_user(&ioc_bb_ctl, (struct fujitsu_hwb_ioc_bb_ctl __user *)arg, sizeof(struct fujitsu_hwb_ioc_bb_ctl)))
    {
        pr_err("Error to get bb_ctl data\n");
        return -1;
    }
    cmg_id = (int)ioc_bb_ctl.cmg;
    bb_id = (int)ioc_bb_ctl.bb;
    pr_debug("Receive CMG %d and Blade %d from userspace\n", cmg_id, bb_id);
    err = oss_a64fx_hwb_free(dev, cmg_id, bb_id);
    if (err)
    {
        return err;
    }
    if (copy_to_user((struct fujitsu_hwb_ioc_bb_ctl __user *)arg, &ioc_bb_ctl, sizeof(struct fujitsu_hwb_ioc_bb_ctl)))
    {
        pr_err("Error to copy back bb_ctl data\n");
        return -1;
    }
    return 0;
}


// Assign a CPU to a barrier blade. Each CPU has four window register which contain the offset of the barrier blade
// and a valid bit. The user-space IOCTL can supply a window offset to use but if -1, the next free window register
// is taken and returned
int oss_a64fx_hwb_assign_blade(struct a64fx_hwb_device *dev, int blade, int window, int* outwindow)
{
    int err = 0;
    int cpuid = 0;
    u8 cmg = 0, ppe = 0;
    struct task_struct* current_task = get_current();
    struct a64fx_task_mapping* taskmap = NULL;
    struct a64fx_task_allocation* alloc = NULL;
    
    // acquire lock
    spin_lock(&dev->dev_lock);
    cpuid = get_cpu();
    pr_debug("Get task mapping for PID %d (TGID %d) CPU %d\n", task_pid_nr(current_task), task_tgid_nr(current_task), cpuid);
    taskmap = get_taskmap(dev, current_task);
    if (!taskmap)
    {
        err = -ENODEV;
        goto assign_blade_out;
    }
    
    err = _oss_a64fx_hwb_get_peinfo(&cmg, &ppe);
    if (!err)
    {
        int cmg_id = (int)cmg;
        struct a64fx_cmg_device* cmgdev = &dev->cmgs[cmg_id];
        struct a64fx_core_mapping* pe = get_pemap(dev, cmg_id, (int)ppe);
        pr_debug("Get allocation for CMG %d and Blade %d (CPU %d, PPE %d)\n", cmg_id, blade, pe->cpu_id, pe->ppe_id);
        alloc = get_allocation(cmgdev, taskmap, blade);
        if (alloc)
        {
            spin_lock(&cmgdev->cmg_lock);
            err = -ENODEV;
            if (window < 0)
            {
                if (alloc->window[pe->ppe_id] == A64FX_HWB_UNASSIGNED_WIN)
                {
                    window = find_first_zero_bit(&pe->bw_map, MAX_BW_PER_CMG);
                    pr_debug("Get next free window %d", window);
                }
                else if (test_bit(alloc->window[pe->ppe_id], &pe->bw_map))
                {
                    window = alloc->window[pe->ppe_id];
                    pr_debug("Reuse window %d used by other pe", window);
                }
            }
            else
            {
                pr_debug("User has given window %d\n", window);
            }
            
            if (window >= 0 && window < MAX_BW_PER_CMG && (!test_bit(window, &pe->bw_map)))
            {
                struct hwb_assign_info info = {
                    .cpu = pe->cpu_id,
                    .window = window,
                    .cmg = cmg_id,
                    .blade = blade,
                    .valid = 1
                };
                pr_debug("Write window %d assign (CPU %d/%d CMG %d Blade %d)\n", window, pe->cpu_id, cpuid, cmg_id, blade);
                smp_call_function_single(pe->cpu_id, oss_a64fx_hwb_assign_func, &info, 1);
                pr_debug("Store window %d for CPU %d on CMG %d to Blade %d in allocation\n", window, pe->cpu_id, cmg_id, blade);
                alloc->window[pe->ppe_id] = window;
                pr_debug("Set window %d for CPU %d/%d\n", window, pe->cpu_id, cpuid);
                set_bit(window, &pe->bw_map);
                cpumask_set_cpu(pe->cpu_id, &alloc->assign_mask);
                alloc->assign_count++;
                pr_debug("%d Threads assigned to allocation (TGID %d CMG %d Blade %d)\n", alloc->assign_count, task_tgid_nr(taskmap->task), cmg_id, blade);
                *outwindow = window;
                err = 0;
            }
            else
            {
                pr_debug("Invalid window %d or already in use\n", window);
            }
            spin_unlock(&cmgdev->cmg_lock);
        }
        else
        {
            pr_debug("Cannot find allocation for CMG %d and Blade %d (CPU %d, PPE %d)\n", cmg_id, blade, pe->cpu_id, pe->ppe_id);
            err = -ENODEV;
        }
    }

assign_blade_out:
    // release cpu
    put_cpu();
    // release lock
    spin_unlock(&dev->dev_lock);
    pr_debug("Assign returns %d\n", err);
    return err;
}

int oss_a64fx_hwb_assign_blade_ioctl(struct a64fx_hwb_device *dev, unsigned long arg)
{
    int err = 0;
    int bb_id = 0;
    int win_id = 0;
    int win_out = 0;
    struct fujitsu_hwb_ioc_bw_ctl ioc_bw_ctl = {0};
    if (copy_from_user(&ioc_bw_ctl, (struct fujitsu_hwb_ioc_bw_ctl __user *)arg, sizeof(struct fujitsu_hwb_ioc_bw_ctl)))
    {
        pr_err("Error to get bb_ctl data\n");
        return -1;
    }
    bb_id = (int)ioc_bw_ctl.bb;
    win_id = (int)ioc_bw_ctl.window;
    err = oss_a64fx_hwb_assign_blade(dev, bb_id, win_id, &win_out);
    if (!err)
    {
        ioc_bw_ctl.window = (u8)win_out;
        if (copy_to_user((struct fujitsu_hwb_ioc_bw_ctl __user *)arg, &ioc_bw_ctl, sizeof(struct fujitsu_hwb_ioc_bw_ctl)))
        {
            pr_err("Error to copy back bb_ctl data\n");
            return -1;
        }
        return 0;
    }
    return err;
}

int oss_a64fx_hwb_unassign_blade(struct a64fx_hwb_device *dev, int blade, int window)
{
    int err = 0;
    int cpuid = 0;
    u8 cmg = 0, ppe = 0;
    int valid = 0, bb = 0;
    struct task_struct* current_task = get_current();
    struct a64fx_task_mapping* taskmap = NULL;
    struct a64fx_task_allocation* alloc = NULL;

    spin_lock(&dev->dev_lock);
    cpuid = get_cpu();
    pr_debug("Get task mapping\n");
    taskmap = get_taskmap(dev, current_task);
    if (!taskmap)
    {
        err = -ENODEV;
        goto unassign_blade_out;
    }

    err = _oss_a64fx_hwb_get_peinfo(&cmg, &ppe);
    if (!err)
    {
        int cmg_id = (int)cmg;
        struct a64fx_core_mapping* pe = get_pemap(dev, cmg_id, (int)ppe);
        struct a64fx_cmg_device* cmgdev = &dev->cmgs[cmg_id];
        pr_debug("Get allocation for CMG %d and Blade %d\n", cmg_id, blade);
        alloc = get_allocation(cmgdev, taskmap, blade);
        err = -EINVAL;
        if (alloc)
        {
            struct a64fx_cmg_device* cmgdev = &dev->cmgs[cmg_id];
            window = alloc->window[pe->ppe_id];
            if (window >= 0 && window < MAX_BW_PER_CMG)
            {
                spin_lock(&cmgdev->cmg_lock);
                if (test_bit(window, &pe->bw_map))
                {
                    read_assign_sync_wr(window, &valid, &bb);
                    if (bb == blade)
                    {
                        struct hwb_assign_info info = {
                            .cpu = pe->cpu_id,
                            .window = window,
                            .cmg = cmg_id,
                            .blade = 0,
                            .valid = 0
                        };
                        pr_debug("Free window %d for CMG %d and Blade %d\n", window, cmg_id, blade);
                        clear_bit(window, &pe->bw_map);
                        pr_debug("Clear window %d assign (CPU %d/%d CMG %d Blade %d)\n", window, pe->cpu_id, cpuid, cmg_id, blade);
                        smp_call_function_single(pe->cpu_id, oss_a64fx_hwb_assign_func, &info, 1);
                        pr_debug("Remove mapping window %d on CMG %d to Blade %d\n", window, cmg_id, blade);
                        alloc->window[pe->ppe_id] = A64FX_HWB_UNASSIGNED_WIN;
                        pr_debug("Clear window %d for CPU %d/%d\n", window, pe->cpu_id, cpuid);
                        clear_bit(window, &pe->bw_map);
                        cpumask_clear_cpu(pe->cpu_id, &alloc->assign_mask);
                        alloc->assign_count--;
                        err = 0;
                        pr_debug("%d Threads assigned to allocation (TGID %d CMG %d Blade %d)\n", alloc->assign_count, task_tgid_nr(taskmap->task), cmg_id, blade);
                    }
                    else
                    {
                        pr_err("AAAH! Window %d maps to blade %d but we want to unassign blade %d\n", window, blade, bb);
                    }
                }
                spin_unlock(&cmgdev->cmg_lock);
            }
            else
            {
                pr_err("AAAH! Window %d in allocation does not fit user given %d\n", alloc->window[pe->ppe_id], window);
            }
        }
        else
        {
            pr_err("AAAH! No allocation for task (PID %d TGID %d)\n", task_pid_nr(taskmap->task), task_tgid_nr(taskmap->task));
        }
    }

unassign_blade_out:
    put_cpu();
    spin_unlock(&dev->dev_lock);
    pr_debug("Unassign returns %d\n", err);
    return err;
}

int oss_a64fx_hwb_unassign_blade_ioctl(struct a64fx_hwb_device *dev, unsigned long arg)
{
    int err = 0;
    int bb_id = 0;
    int win_id = 0;
    struct fujitsu_hwb_ioc_bw_ctl ioc_bw_ctl;
    if (copy_from_user(&ioc_bw_ctl, (struct fujitsu_hwb_ioc_bw_ctl __user *)arg, sizeof(struct fujitsu_hwb_ioc_bw_ctl)))
    {
        pr_err("Error to get bb_ctl data\n");
        return -1;
    }
    bb_id = (int)ioc_bw_ctl.bb;
    win_id = (int)ioc_bw_ctl.window;
    err = oss_a64fx_hwb_unassign_blade(dev, bb_id, win_id);
    if (err)
    {
        return err;
    }
    if (copy_to_user((struct fujitsu_hwb_ioc_bw_ctl __user *)arg, &ioc_bw_ctl, sizeof(struct fujitsu_hwb_ioc_bw_ctl)))
    {
        pr_err("Error to copy back bb_ctl data\n");
        return -1;
    }
    return 0;
}



void asm_reset_func(void* info)
{
    int i = 0;
    for (i = 0; i < MAX_BW_PER_CMG; i++)
    {
        int valid, blade;
        read_assign_sync_wr(i, &valid, &blade);
        pr_debug("Reset CPU %d: Win %d Valid %d Blade %d\n", smp_processor_id(), i, valid, blade);
        write_assign_sync_wr(i, 0, 0);
        write_bst_sync_wr(i, 0);
    }
    for (i = 0; i < MAX_BB_PER_CMG; i++)
    {
        unsigned long bst, bst_mask;
        read_init_sync_bb(i, &bst, &bst_mask);
        pr_debug("Reset CPU %d: Blade %d BST 0x%lx BSTMASK 0x%lx\n", smp_processor_id(), i, bst, bst_mask);
        write_init_sync_bb(i, 0x0);
    }
    write_hwb_ctrl(0, 0);
}

int oss_a64fx_hwb_reset_ioctl(struct a64fx_hwb_device *dev, unsigned long arg)
{
    int i = 0;
    int j = 0;
    struct a64fx_cmg_device* cmg = NULL;
    struct list_head *taskcur = NULL, *tasktmp = NULL;
    struct a64fx_task_mapping* taskmap = NULL;
    struct list_head *alloccur = NULL, *alloctmp = NULL;
    struct a64fx_task_allocation* alloc = NULL;

    spin_lock(&dev->dev_lock);
    pr_debug("Reset all registers\n");
    on_each_cpu(asm_reset_func, NULL, 0);

    list_for_each_safe(taskcur, tasktmp, &dev->task_list)
    {
        taskmap = list_entry(taskcur, struct a64fx_task_mapping, list);
        list_for_each_safe(alloccur, alloctmp, &taskmap->allocs)
        {
            alloc = list_entry(alloccur, struct a64fx_task_allocation, list);
            pr_debug("Delete allocation (PID %d CMG %d Blade %d)\n", alloc->task->pid, alloc->cmg, alloc->blade);
            list_del(&alloc->list);
            taskmap->num_allocs--;
            kfree(alloc);
        }
        pr_debug("Delete task mapping (PID %d)\n", taskmap->task->pid);
        list_del(&taskmap->list);
        dev->num_tasks--;
        kfree(taskmap);
    }
    pr_debug("Reset all bookkeeping variables\n");
    dev->num_tasks = 0;
    dev->active_count = 0;
    for (i = 0; i < MAX_NUM_CMG; i++)
    {
        cmg = &dev->cmgs[i];
        pr_debug("Reset CMG %d\n", cmg->cmg_id);
        cmg->bb_active = 0x0;
        for (j = 0; j < MAX_PE_PER_CMG; j++)
        {
            cmg->pe_map[j].bw_map = 0x0;
        }
    }
    spin_unlock(&dev->dev_lock);
    return 0;
}
