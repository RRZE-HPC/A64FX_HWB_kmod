#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <include/linux/smp.h>
#include <include/linux/cpumask.h>

#include "a64fx_hwb.h"
#include "a64fx_hwb_cmg.h"
#include "a64fx_hwb_asm.h"
#include "fujitsu_hpc_ioctl.h"

static int pemask_to_cpumask(size_t pemask_size, unsigned long *pemask, struct cpumask *cpumask)
{
    int i = 0;
    int size = 0;
    int count = 0;
    if ((pemask_size == 0) || (!pemask))
    {
        return -EINVAL;
    }
    cpumask_clear(cpumask);
    size = (64 < pemask_size*8 ? 64 : pemask_size*8);
    for (i = 0; i < size; i++)
    {
        if (*pemask & (1UL<<i))
        {
            cpumask_set_cpu(i, cpumask);
            count++;
        }
    }
    return count;
}

static int check_cpumask(struct a64fx_hwb_device *dev, struct cpumask cpumask)
{
    int i = 0;
    int j = 0;
    int cpu = -1;
    int cmg = -1;
    if (cpumask_weight(&cpumask) < 2)
    {
        return -EINVAL;
    }
    for_each_cpu(cpu, &cpumask)
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
                        return 1;
                    }
                }
            }
        }
    }
    return 0;
}

static int cpumask_to_ppemask(struct a64fx_cmg_device* cmg, struct cpumask cpumask, unsigned long* ppemask)
{
    int cpu = 0;
    int i = 0;
    unsigned long mask = 0x0UL;
    if ((!cmg) || (!ppemask))
    {
        return -EINVAL;
    }
    for_each_cpu(cpu, &cpumask)
    {
        for (i = 0; i < cmg->num_pes; i++)
        {
            if (cmg->pe_map[i].cpu_id == cpu)
            {
                mask |= (1UL<<cmg->pe_map[i].ppe_id);
            }
        }
    }
    *ppemask = mask;
    return 0;
}

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

struct hwb_allocate_info {
    int blade;
    int cmg;
    unsigned long ppemask;
};

static void oss_a64fx_hwb_allocate_func(void* info)
{
    struct hwb_allocate_info* ainfo = (struct hwb_allocate_info*) info;
    write_init_sync_bb(ainfo->blade, ainfo->ppemask);
    pr_info("write_init_sync_bb (CMG %d, Blade %d, PPEmask 0x%lX) on CPU %d\n", ainfo->cmg, ainfo->blade, ainfo->ppemask, smp_processor_id());
}


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

static struct a64fx_task_allocation * new_allocation(struct a64fx_cmg_device *cmg, struct a64fx_task_mapping *taskmap, int blade)
{
    struct a64fx_task_allocation *alloc = NULL;
    struct hwb_allocate_info info = {0, 0UL};
    alloc = get_allocation(cmg, taskmap, blade);
    if (alloc)
    {
        pr_info("Error allocation already exists\n");
        return NULL;
    }
    alloc = kmalloc(sizeof(struct a64fx_task_allocation), GFP_KERNEL);
    if (!alloc)
    {
        return NULL;
    }
    pr_info("New allocation for CMG %d and Blade %d\n", cmg->cmg_id, blade);
    alloc->cmg = (u8)cmg->cmg_id;
    alloc->blade = (u8)blade;
    alloc->win_mask = 0x0;
    alloc->window = A64FX_HWB_UNASSIGNED_WIN;
    alloc->task = taskmap->task;
    refcount_set(&alloc->assign_count, 0);
    info.blade = blade;
    info.cmg = cmg->cmg_id;
    cpumask_to_ppemask(cmg, taskmap->cpumask, &info.ppemask);
    pr_info("PPEmask is 0x%lx\n", info.ppemask);
    smp_call_function_any(&cmg->cmgmask, oss_a64fx_hwb_allocate_func, &info, 1);
    pr_info("Set blade %d in CMG %d active\n", info.blade, info.cmg);
    set_bit(info.blade, &cmg->bb_active);
    pr_info("Add allocation for task %d\n", taskmap->task->pid);
    list_add(&alloc->list, &taskmap->allocs);
    refcount_inc(&taskmap->num_allocs);
    pr_info("Task %d has %d allocations\n", taskmap->task->pid, refcount_read(&taskmap->num_allocs));
    return 0;
}

static struct a64fx_task_allocation * register_allocation(struct a64fx_cmg_device *cmg, struct a64fx_task_mapping *taskmap, int blade)
{
    struct a64fx_task_allocation *alloc = NULL;
    alloc = get_allocation(cmg, taskmap, blade);
    if (!alloc)
    {
        alloc = new_allocation(cmg, taskmap, blade);
    }
    return alloc;
}

static int free_allocation(struct a64fx_cmg_device *cmg, struct a64fx_task_mapping *taskmap, struct a64fx_task_allocation* alloc)
{
    struct hwb_allocate_info info = {0, 0UL};
    if (test_bit(alloc->blade, &cmg->bb_active))
    {
        int assign_count = refcount_read(&alloc->assign_count);
        if (assign_count > 0)
        {
            pr_err("Allocation (PID %d CMG %d Blade %d) still assigned by %d threads\n", taskmap->task->pid, alloc->cmg, alloc->blade, assign_count);
        }
        pr_info("PID %d free BB %d at CMG %d\n", taskmap->task->pid, alloc->blade, alloc->cmg);
        info.blade = alloc->blade;
        info.cmg = alloc->cmg;
        info.ppemask = 0x0UL;
        smp_call_function_any(&cmg->cmgmask, oss_a64fx_hwb_allocate_func, &info, 1);
        clear_bit(alloc->blade, &cmg->bb_active);
    }
    else
    {
        pr_info("AAAH! Task %d free inactive Blade %d at CMG %d\n", taskmap->task->pid, alloc->blade, alloc->cmg);
    }
    list_del(&alloc->list);
    kfree(alloc);
    refcount_dec(&taskmap->num_allocs);
    return 0;
}

struct a64fx_task_mapping * get_taskmap(struct a64fx_hwb_device *dev, struct task_struct* task)
{
    struct list_head* cur = NULL;
    struct a64fx_task_mapping *taskmap = NULL;
    pr_info("Get task %d (TGID %d)\n", task->pid, task_tgid_nr(task));
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

static struct a64fx_task_mapping * new_taskmap(struct a64fx_hwb_device *dev, struct task_struct* task, struct cpumask cpumask)
{
    struct a64fx_task_mapping *taskmap = NULL;
    taskmap = get_taskmap(dev, task);
    if (taskmap)
    {
        pr_info("Error task already exists\n");
        return NULL;
    }
    taskmap = kmalloc(sizeof(struct a64fx_task_mapping), GFP_KERNEL);
    if (!taskmap)
    {
        return NULL;
    }
    pr_info("New task %d\n", task->pid);
    taskmap->task = task;
    refcount_set(&taskmap->num_allocs, 0);
    cpumask_copy(&taskmap->cpumask, &cpumask);
    INIT_LIST_HEAD(&taskmap->list);
    INIT_LIST_HEAD(&taskmap->allocs);
    list_add(&taskmap->list, &dev->task_list);
    refcount_inc(&dev->num_tasks);
    pr_info("Currently %d tasks with allocations\n", refcount_read(&dev->num_tasks));
    return taskmap;

}


int unregister_task(struct a64fx_hwb_device *dev, struct a64fx_task_mapping *taskmap)
{
    int num_allocs = 0;
    struct list_head *cur = NULL, *tmp = NULL;
    struct a64fx_task_allocation *alloc = NULL;
    if (taskmap)
    {
        pr_info("Remove task %d\n", task_pid_nr(taskmap->task));
        num_allocs = refcount_read(&taskmap->num_allocs);
        if (num_allocs > 0)
        {
            pr_info("Task %d has %d allocations left, free all\n", task_pid_nr(taskmap->task), num_allocs);
            list_for_each_safe(cur, tmp, &taskmap->allocs)
            {
                struct a64fx_cmg_device *cmg = NULL;
                alloc = list_entry(cur, struct a64fx_task_allocation, list);
                cmg = &dev->cmgs[(int)alloc->cmg];
                free_allocation(cmg, taskmap, alloc);
/*                list_del(&alloc->list);*/
/*                kfree(alloc);*/
            }
        }
        list_del(&taskmap->list);
        kfree(taskmap);
        refcount_dec(&dev->num_tasks);
        pr_info("Currently %d tasks with allocations\n", refcount_read(&dev->num_tasks));
    }
    return 0;
}


#ifdef __x86_64__
static int _oss_a64fx_hwb_get_peinfo(u8* cmg, u8 * ppe)
{
    int cpuid = smp_processor_id();
    int num_cpus = num_online_cpus();
    int pe_per_cmg = (num_cpus/MAX_NUM_CMG);
    *cmg = (u8)(cpuid / pe_per_cmg);
    *ppe = (u8)(cpuid % pe_per_cmg);
    pr_info("get_peinfo for CPU %d of %d with %d PE/CMG: CMG%u PPE %u\n", cpuid, num_cpus, pe_per_cmg, *cmg, *ppe);
    return 0;
}
#endif
#ifdef __ARM_ARCH_8A
static int _oss_a64fx_hwb_get_peinfo(u8* cmg, u8 * ppe)
{
    u64 val = 0;
    asm volatile ("MRS %0, S3_0_C11_C12_4" : "=r"(val) );
    *ppe = (u8)(val & 0xF);
    *cmg = (u8)((val >> 4) & 0x3);
    pr_info("get_peinfo for CPU %d CMG%u PPE %u Raw 0x%llX\n", smp_processor_id(), *cmg, *ppe, val);
    return 0;
}
#endif

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

int oss_a64fx_hwb_get_peinfo_ioctl(unsigned long arg)
{
    int cmg, ppe;
    struct fujitsu_hwb_ioc_pe_info ustruct = {0};
/*    pr_info("FUJITSU_HWB_IOC_GET_PE_INFO...\n");*/
    if (copy_from_user(&ustruct, (struct fujitsu_hwb_ioc_pe_info __user *)arg, sizeof(struct fujitsu_hwb_ioc_pe_info)))
    {
        pr_err("Error to get pe_info data\n");
        return -1;
    }
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



int oss_a64fx_hwb_allocate(struct a64fx_hwb_device *dev, int cmg, struct cpumask cpumask, int *blade)
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
        taskmap = new_taskmap(dev, current_task, cpumask);
        if (!taskmap)
        {
            pr_err("Failed to register task or get existing mapping\n");
            err = -1;
            goto allocate_exit;
        }
    }
    cmgdev = &dev->cmgs[cmg];
    err = -ENODEV;
    bit = find_first_zero_bit(&cmgdev->bb_active, MAX_BB_PER_CMG);
    pr_info("Blade %d is free, use it\n", bit);
    if (bit >= 0 && bit < dev->num_bb_per_cmg)
    {
        register_allocation(cmgdev, taskmap, bit);
        *blade = bit;
        err = 0;
    }

allocate_exit:
    spin_unlock(&dev->dev_lock);
    pr_info("Allocate returns %d\n", err);
    return err;
}


int oss_a64fx_hwb_allocate_ioctl(struct a64fx_hwb_device *dev, unsigned long arg)
{
    int err = 0;
    int cmg_id = 0;
    int bb_id = 0;
    unsigned long mask = 0;
    struct cpumask cpumask;
    struct fujitsu_hwb_ioc_bb_ctl ioc_bb_ctl = {0};
    struct fujitsu_hwb_ioc_bb_ctl __user *uarg = (struct fujitsu_hwb_ioc_bb_ctl __user *)arg;
    
    pr_info("Start allocate\n");
    if (copy_from_user(&ioc_bb_ctl, uarg, sizeof(struct fujitsu_hwb_ioc_bb_ctl __user)))
    {
        pr_err("Error to get bb_ctl data\n");
        return -EINVAL;
    }
    pr_info("Start allocate (pemask)\n");
    if (copy_from_user(&mask, (unsigned long __user *)ioc_bb_ctl.pemask, sizeof(unsigned long __user)))
    {
        pr_err("Error to get bb_ctl pemask data\n");
        return -EINVAL;
    }
    pr_info("Read pemask\n");
    
    err = pemask_to_cpumask(ioc_bb_ctl.size, &mask, &cpumask);
    if (err == 0)
    {
        pr_err("cpumask empty\n");
        return -EINVAL;
    }
    err = check_cpumask(dev, cpumask);
    if (err)
    {
        pr_err("cpumask spans multiple CMGs, contains only a single CPU or contains offline CPUs\n");
        return -EINVAL;
    }
    cmg_id = (int)ioc_bb_ctl.cmg;
    bb_id = (int)ioc_bb_ctl.bb;
    pr_info("Receive CMG %d and Blade %d from userspace\n", cmg_id, bb_id);
    err = oss_a64fx_hwb_allocate(dev, cmg_id, cpumask, &bb_id);
    if (err)
    {
        return err;
    }
    pr_info("Return blade %d to userspace\n", bb_id);
    ioc_bb_ctl.bb = (u8)bb_id;
    
    if (copy_to_user((struct fujitsu_hwb_ioc_bb_ctl __user *)arg, &ioc_bb_ctl, sizeof(struct fujitsu_hwb_ioc_bb_ctl __user)))
    {
        pr_err("Error to copy back bb_ctl data\n");
        return -1;
    }
    return 0;
}

int oss_a64fx_hwb_free(struct a64fx_hwb_device *dev, int cmg_id, int blade)
{
/*    int i = 0;*/
/*    int j = 0;*/
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
        free_allocation(cmg, taskmap, alloc);
        if (refcount_read(&taskmap->num_allocs) == 0)
        {
            pr_info("Task has no more allocations, unregister it\n");
            unregister_task(dev, taskmap);
        }
        err = 0;
    }
free_exit:
    spin_unlock(&dev->dev_lock);
    pr_info("Free returns %d\n", err);
    return err;
}

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
    pr_info("Receive CMG %d and Blade %d from userspace\n", cmg_id, bb_id);
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

int oss_a64fx_hwb_assign_blade(struct a64fx_hwb_device *dev, int blade, int window, int* outwindow)
{
    int err = 0;
    int cpuid = 0;
    u8 cmg = 0, ppe = 0;
    int valid = 0, bb = 0;
    struct task_struct* current_task = get_current();
    struct a64fx_task_mapping* taskmap = NULL;
    struct a64fx_task_allocation* alloc = NULL;
    
    // acquire lock
    spin_lock(&dev->dev_lock);
    cpuid = get_cpu();
    pr_info("Get task mapping for PID %d (TGID %d) CPU %d\n", task_pid_nr(current_task), task_tgid_nr(current_task), cpuid);
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
        pr_info("Get allocation for CMG %d and Blade %d (CPU %d, PPE %d)\n", cmg_id, blade, pe->cpu_id, pe->ppe_id);
        alloc = get_allocation(cmgdev, taskmap, blade);
        if (alloc)
        {
            spin_lock(&cmgdev->cmg_lock);
            err = -ENODEV;
            if (window < 0)
            {
                if (alloc->window == A64FX_HWB_UNASSIGNED_WIN)
                {
                    window = find_first_zero_bit(&pe->bw_map, MAX_BW_PER_CMG);
                    pr_info("Get next free window %d", window);
                }
                else
                {
                    window = alloc->window;
                    pr_info("Reuse window %d", window);
                }
            }
            
            if (window >= 0 && window < MAX_BW_PER_CMG && (!test_bit(window, &pe->bw_map)))
            {
                read_assign_sync_wr(window, &valid, &bb);
                if (valid)
                {
                    pr_err("Error in bookkeeping, window %d is assigned to blade %d\n", window, bb);
                }
                else
                {
                    //cmgdev->bb_map[blade] |= (1<<window);
/*                    pr_info("Use window %d for CMG %d and Blade %d\n", window, cmg_id, blade);*/
/*                    set_bit(window, &cmgdev->bw_active);*/
/*                    pr_info("Map window %d on CMG %d to Blade %d\n", window, cmg_id, blade);*/
/*                    cmgdev->bw_map[window] = blade;*/
                    pr_info("Write window %d assign (CPU %d/%d CMG %d Blade %d)\n", window, pe->cpu_id, cpuid, cmg_id, blade);
                    write_assign_sync_wr(window, 1, blade);
                    if (alloc->window == A64FX_HWB_UNASSIGNED_WIN)
                    {
                        pr_info("Store window %d on CMG %d to Blade %d in allocation\n", window, cmg_id, blade);
                        set_bit(window, &alloc->win_mask);
                        alloc->window = window;
                    }
                    pr_info("Set window %d for CPU %d/%d\n", window, pe->cpu_id, cpuid);
                    set_bit(window, &pe->bw_map);
                    refcount_inc(&alloc->assign_count);
                    pr_info("%d Threads assigned to allocation (TGID %d CMG %d Blade %d)\n", refcount_read(&alloc->assign_count), task_tgid_nr(taskmap->task), cmg_id, blade);
                    *outwindow = window;
                    err = 0;
                }
            }
            spin_unlock(&cmgdev->cmg_lock);
        }
        else
        {
            pr_info("Cannot find allocation for CMG %d and Blade %d (CPU %d, PPE %d)\n", cmg_id, blade, pe->cpu_id, pe->ppe_id);
            err = -ENODEV;
        }
    }

    
assign_blade_out:
    // release cpu
    put_cpu();
    // release lock
    spin_unlock(&dev->dev_lock);
    pr_info("Assign returns %d\n", err);
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
    pr_info("Get task mapping\n");
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
        pr_info("Get allocation for CMG %d and Blade %d\n", cmg_id, blade);
        alloc = get_allocation(cmgdev, taskmap, blade);
        err = -EINVAL;
        if (alloc)
        {
            struct a64fx_cmg_device* cmgdev = &dev->cmgs[cmg_id];
            if (alloc->window == window)
            {
                spin_lock(&cmgdev->cmg_lock);
                if (test_bit(window, &pe->bw_map))
                {
                    read_assign_sync_wr(window, &valid, &bb);
                    if (bb == blade)
                    {
                        pr_info("Free window %d for CMG %d and Blade %d\n", window, cmg_id, blade);
                        clear_bit(window, &pe->bw_map);
                        pr_info("Clear window %d assign (CPU %d/%d CMG %d Blade %d)\n", window, pe->cpu_id, cpuid, cmg_id, blade);
                        write_assign_sync_wr(window, 0, 0);
                        if (refcount_dec_and_test(&alloc->assign_count))
                        {
                            pr_info("Remove mapping window %d on CMG %d to Blade %d\n", window, cmg_id, blade);
                            alloc->window = A64FX_HWB_UNASSIGNED_WIN;
                            clear_bit(window, &alloc->win_mask);
                            pr_info("Clear window %d for CPU %d/%d\n", window, pe->cpu_id, cpuid);
                            clear_bit(window, &pe->bw_map);
/*                            cmgdev->bw_map[window] = 0;*/
                        }
                        err = 0;
                        pr_info("%d Threads assigned to allocation (TGID %d CMG %d Blade %d)\n", refcount_read(&alloc->assign_count), task_tgid_nr(taskmap->task), cmg_id, blade);
                    }
                    else
                    {
                        pr_err("AAAH! Window %d maps to blade %d\n", window, blade);
                        
                    }
                }
                spin_unlock(&cmgdev->cmg_lock);
            }
            else
            {
                pr_err("AAAH! Window %d in allocation does not fit user given %d\n", alloc->window, window);
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
    pr_info("Unassign returns %d\n", err);
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
        write_assign_sync_wr(i, 0, 0);
        write_bst_sync_wr(i, 0);
    }
    for (i = 0; i < MAX_BB_PER_CMG; i++)
    {
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
    pr_info("Reset all registers\n");
    on_each_cpu(asm_reset_func, NULL, 0);

    list_for_each_safe(taskcur, tasktmp, &dev->task_list)
    {
        taskmap = list_entry(taskcur, struct a64fx_task_mapping, list);
        list_for_each_safe(alloccur, alloctmp, &taskmap->allocs)
        {
            alloc = list_entry(alloccur, struct a64fx_task_allocation, list);
            pr_info("Delete allocation (PID %d CMG %d Blade %d Window %d)\n", alloc->task->pid, alloc->cmg, alloc->blade, alloc->window);
            list_del(&alloc->list);
            refcount_dec(&taskmap->num_allocs);
            kfree(alloc);
        }
        pr_info("Delete task mapping (PID %d)\n", taskmap->task->pid);
        list_del(&taskmap->list);
        refcount_dec(&dev->num_tasks);
        kfree(taskmap);
    }
    pr_info("Reset all bookkeeping variables\n");
    refcount_set(&dev->num_tasks, 0);
    refcount_set(&dev->active_count, 0);
    for (i = 0; i < MAX_NUM_CMG; i++)
    {
        cmg = &dev->cmgs[i];
        pr_info("Reset CMG %d\n", cmg->cmg_id);
        cmg->bb_active = 0x0;
/*        cmg->bw_active = 0x0;*/
/*        for (j = 0; j < MAX_BW_PER_CMG; j++)*/
/*        {*/
/*            cmg->bw_map[j] = 0;*/
/*        }*/
        for (j = 0; j < MAX_PE_PER_CMG; j++)
        {
            cmg->pe_map[j].bw_map = 0x0;
        }
    }
    spin_unlock(&dev->dev_lock);
    return 0;
}
