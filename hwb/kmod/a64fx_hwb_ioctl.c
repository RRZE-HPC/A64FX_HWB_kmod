#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <include/linux/smp.h>
#include <include/linux/cpumask.h>

#include "a64fx_hwb.h"
#include "a64fx_hwb_cmg.h"
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
    for_each_cpu(cpu, &cpumask)
    {
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

static int cpumask_online(struct cpumask cpumask)
{
    int cpu = -1;
    for_each_cpu(cpu, &cpumask)
    {
        if (!cpu_online(cpu))
        {
            return -1;
        }
    }
    return 0;
}

static struct a64fx_task_allocation * get_allocation(struct a64fx_hwb_device *dev, struct a64fx_task_mapping *taskmap, int cmg, int bb)
{
    struct list_head *cur = NULL;
    struct a64fx_task_allocation *alloc = NULL;
    list_for_each(cur, &taskmap->allocs)
    {
        alloc = list_entry(cur, struct a64fx_task_allocation, list);
        if (alloc->cmg == cmg && alloc->bb == bb)
        {
            return alloc;
        }
    }
    return NULL;
}

static struct a64fx_task_allocation * new_allocation(struct a64fx_hwb_device *dev, struct a64fx_task_mapping *taskmap, int cmg, int bb)
{
    struct a64fx_task_allocation *alloc = NULL;
    alloc = get_allocation(dev, taskmap, cmg, bb);
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
    pr_info("New allocation for CMG %d and Blade %d\n", cmg, bb);
    alloc->cmg = (u8)cmg;
    alloc->bb = (u8)bb;
    alloc->win_mask = 0x0;
    alloc->task = taskmap->task;
    list_add(&alloc->list, &taskmap->allocs);
    taskmap->num_allocs++;
    return 0;
}

static struct a64fx_task_allocation * register_allocation(struct a64fx_hwb_device *dev, struct a64fx_task_mapping *taskmap, int cmg, int bb)
{
    struct a64fx_task_allocation *alloc = NULL;
    alloc = get_allocation(dev, taskmap, cmg, bb);
    if (!alloc)
    {
        alloc = new_allocation(dev, taskmap, cmg, bb);
    }
    return alloc;
}

static int free_allocation(struct a64fx_hwb_device *dev, struct a64fx_task_mapping *taskmap, struct a64fx_task_allocation* alloc)
{
    int cmg_id = alloc->cmg;
    if (test_bit(alloc->bb, &dev->cmgs[cmg_id].bb_active))
    {
        pr_info("Free BB %d at CMG %d\n", alloc->bb, alloc->cmg);
        clear_bit(alloc->bb, &dev->cmgs[cmg_id].bb_active);
    }
    list_del(&alloc->list);
    kfree(alloc);
    taskmap->num_allocs--;
    return 0;
}

static struct a64fx_task_mapping * get_taskmap(struct a64fx_hwb_device *dev, struct task_struct* task)
{
    struct list_head* cur = NULL;
    struct a64fx_task_mapping *taskmap = NULL;
    pr_info("Get task %d (TGID %d)\n", task->pid, task->tgid);
    list_for_each(cur, &dev->task_list)
    {
        taskmap = list_entry(cur, struct a64fx_task_mapping, list);
        if (taskmap->task->tgid == task->tgid)
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
    //refcount_set(&taskmap->refcount, 0);
    taskmap->num_allocs = 0;
    cpumask_copy(&taskmap->cpumask, &cpumask);
    INIT_LIST_HEAD(&taskmap->list);
    INIT_LIST_HEAD(&taskmap->allocs);
    list_add(&taskmap->list, &dev->task_list);
    dev->num_tasks++;
    return taskmap;

}

static struct a64fx_task_mapping * register_task(struct a64fx_hwb_device *dev, struct task_struct* task, struct cpumask cpumask)
{
    struct a64fx_task_mapping *taskmap = NULL;
/*    struct task_struct* current_task = get_current();*/
    taskmap = get_taskmap(dev, task);
    if (!taskmap)
    {
        taskmap = new_taskmap(dev, task, cpumask);
    }
    //refcount_inc(&taskmap->refcount);
    return taskmap;
}

static int unregister_task(struct a64fx_hwb_device *dev, struct a64fx_task_mapping *taskmap)
{
    int i = 0;
    struct list_head *cur = NULL, *tmp = NULL;
    struct a64fx_task_allocation *alloc = NULL;
    if (taskmap)
    {
        if (taskmap->num_allocs > 0)
        {
            list_for_each_safe(cur, tmp, &taskmap->allocs)
            {
                alloc = list_entry(cur, struct a64fx_task_allocation, list);
                free_allocation(dev, taskmap, alloc);
                list_del(&alloc->list);
                kfree(alloc);
                taskmap->num_allocs--;
            }
        }
        list_del(&taskmap->list);
        kfree(taskmap);
        dev->num_tasks--;
    }
    return 0;
}

static int valid_task(struct a64fx_hwb_device *dev)
{
    struct list_head *cur = NULL;
    struct a64fx_task_mapping* taskmap = NULL;
    struct task_struct* current_task = get_current();
    list_for_each(cur, &dev->task_list)
    {
        taskmap = list_entry(cur, struct a64fx_task_mapping, list);
        if (taskmap->task->tgid == current_task->tgid)
        {
            return 1;
        }
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
    pr_info("Fujitsu HWB: get_peinfo for CPU %d of %d with %d PE/CMG: CMG%u PPE %u\n", cpuid, num_cpus, pe_per_cmg, *cmg, *ppe);
    return 0;
}
#else
static int _oss_a64fx_hwb_get_peinfo(u8* cmg, u8 * ppe)
{
    u64 val = 0;
    asm ("MRS %0, S3_0_C11_C12_4" :: "r"(val));
    *ppe = (u8)(val & 0xF);
    *cmg = (u8)((val >> 4) & 0x3);
    return 0;
}
#endif

int oss_a64fx_hwb_get_peinfo(int *cmg, int *ppe)
{
    u8 ucmg;
    u8 uppe;
    _oss_a64fx_hwb_get_peinfo(&ucmg, &uppe);
    *cmg = (int)ucmg;
    *ppe = (int)uppe;
    return 0;
}

int oss_a64fx_hwb_get_peinfo_ioctl(unsigned long arg)
{
    struct fujitsu_hwb_ioc_pe_info ustruct = {0};
    pr_info("Fujitsu HWB: FUJITSU_HWB_IOC_GET_PE_INFO...\n");
    if (copy_from_user(&ustruct, (struct fujitsu_hwb_ioc_pe_info __user *)arg, sizeof(struct fujitsu_hwb_ioc_pe_info)))
    {
        pr_err("Fujitsu HWB: Error to get pe_info data\n");
        return -1;
    }
    ustruct.cmg = 0xFF;
    ustruct.ppe = 0xFF;
    _oss_a64fx_hwb_get_peinfo(&ustruct.cmg, &ustruct.ppe);
    if (copy_to_user((struct fujitsu_hwb_ioc_pe_info __user *)arg, &ustruct, sizeof(struct fujitsu_hwb_ioc_pe_info)))
    {
        pr_err("Fujitsu HWB: Error to copy back pe_info data\n");
        return -1;
    }
    return 0;
}

int oss_a64fx_hwb_allocate(struct a64fx_hwb_device *dev, unsigned long arg)
{
    int err = 0;
    int cmg_id = -1;
    struct a64fx_cmg_device *cmg = NULL;
    struct a64fx_task_mapping *taskmap = NULL;
/*    struct a64fx_task_allocation *alloc = NULL;*/
    unsigned long mask;
    struct cpumask cpumask;
    struct fujitsu_hwb_ioc_bb_ctl ioc_bb_ctl = {0};
    struct fujitsu_hwb_ioc_bb_ctl __user *uarg = (struct fujitsu_hwb_ioc_bb_ctl __user *)arg;
    struct task_struct* current_task = get_current();
    
    // acquire lock
    pr_info("Fujitsu HWB: Start allocate\n");
    if (copy_from_user(&ioc_bb_ctl, uarg, sizeof(struct fujitsu_hwb_ioc_bb_ctl __user)))
    {
        pr_err("Fujitsu HWB: Error to get bb_ctl data\n");
        return -1;
    }
    pr_info("Fujitsu HWB: Start allocate (pemask)\n");
    if (copy_from_user(&mask, (unsigned long __user *)ioc_bb_ctl.pemask, sizeof(unsigned long __user)))
    {
        pr_err("Fujitsu HWB: Error to get bb_ctl pemask data\n");
        return -1;
    }
    pr_info("Fujitsu HWB: Read pemask\n");
    spin_lock(&dev->dev_lock);
    err = pemask_to_cpumask(ioc_bb_ctl.size, &mask, &cpumask);
    if (err == 0)
    {
        err = -EINVAL;
        pr_err("Fujitsu HWB: cpumask empty\n");
        goto allocate_exit;
    }
    err = check_cpumask(dev, cpumask);
    if (err)
    {
        pr_err("Fujitsu HWB: cpumask spans multiple CMGs! Not allowed\n");
        goto allocate_exit;
    }
    err = cpumask_online(cpumask);
    if (err)
    {
        pr_err("Fujitsu HWB: cpumask contains offline cpus\n");
        goto allocate_exit;
    }
    taskmap = get_taskmap(dev, current_task);
    if (!taskmap)
    {
        taskmap = new_taskmap(dev, current_task, cpumask);
        if (!taskmap)
        {
            pr_err("Fujitsu HWB: Failed to register task or get existing mapping\n");
            err = -1;
            goto allocate_exit;
        }
    }
    cmg_id = (int)ioc_bb_ctl.cmg;
    cmg = &dev->cmgs[cmg_id];
    err = find_first_zero_bit(&cmg->bb_active, 64);
    pr_info("Fujitsu HWB: Bit %d is zero, use it\n", err);
    if (err >= 0 && err < dev->num_bb_per_cmg)
    {
        register_allocation(dev, taskmap, cmg_id, err);
        ioc_bb_ctl.bb = (u8)err;
        ioc_bb_ctl.cmg = (u8)cmg_id;
        set_bit(err, &cmg->bb_active);
        err = 0;
    }
    else
    {
        err = -ENODEV;
    }

allocate_exit:
    spin_unlock(&dev->dev_lock);
    if (copy_to_user((struct fujitsu_hwb_ioc_bb_ctl __user *)arg, &ioc_bb_ctl, sizeof(struct fujitsu_hwb_ioc_bb_ctl __user)))
    {
        pr_err("Fujitsu HWB: Error to copy back bb_ctl data\n");
        return -1;
    }

    return err;
}

int oss_a64fx_hwb_free(struct a64fx_hwb_device *dev, int cmg_id, int bb_id)
{
/*    int i = 0;*/
/*    int j = 0;*/
/*    struct a64fx_cmg_device *cmg = NULL;*/
    struct a64fx_task_mapping *taskmap = NULL;
    struct a64fx_task_allocation *alloc = NULL;
    struct task_struct* current_task = get_current();

    spin_lock(&dev->dev_lock);
    taskmap = get_taskmap(dev, current_task);
    if (taskmap)
    {
        alloc = get_allocation(dev, taskmap, cmg_id, bb_id);
        if (alloc)
        {
            free_allocation(dev, taskmap, alloc);
        }
        if (taskmap->num_allocs == 0)
        {
            unregister_task(dev, taskmap);
        }
    }
    spin_unlock(&dev->dev_lock);
    return 0;
}

int oss_a64fx_hwb_free_ioctl(struct a64fx_hwb_device *dev, unsigned long arg)
{
    int cmg_id = 0;
    int bb_id = 0;
    struct fujitsu_hwb_ioc_bb_ctl ioc_bb_ctl = {0};
    if (copy_from_user(&ioc_bb_ctl, (struct fujitsu_hwb_ioc_bb_ctl __user *)arg, sizeof(struct fujitsu_hwb_ioc_bb_ctl)))
    {
        pr_err("Fujitsu HWB: Error to get bb_ctl data\n");
        return -1;
    }
    cmg_id = (int)ioc_bb_ctl.cmg;
    bb_id = (int)ioc_bb_ctl.bb;
    oss_a64fx_hwb_free(dev, cmg_id, bb_id);
    if (copy_to_user((struct fujitsu_hwb_ioc_bb_ctl __user *)arg, &ioc_bb_ctl, sizeof(struct fujitsu_hwb_ioc_bb_ctl)))
    {
        pr_err("Fujitsu HWB: Error to copy back bb_ctl data\n");
        return -1;
    }
    return 0;
}

int oss_a64fx_hwb_assign_blade(struct a64fx_hwb_device *dev, int blade, int window, int* outwindow)
{
    int err = 0;
    u8 cmg = 0, ppe = 0;
    struct task_struct* current_task = get_current();
    struct a64fx_task_mapping* taskmap = NULL;
    struct a64fx_task_allocation* alloc = NULL;
    
    // acquire lock
    spin_lock(&dev->dev_lock);
    pr_info("Fujitsu HWB: Get task mapping\n");
    taskmap = get_taskmap(dev, current_task);
    if (!taskmap)
    {
        // Try parent task. This happens in case of OpenMP and others.
        pr_info("Fujitsu HWB: Get parent task mapping\n");
        taskmap = get_taskmap(dev, current_task->real_parent);
        if (!taskmap)
        {
            err = -ENODEV;
            goto assign_blade_out;
        }
    }
    
    err = _oss_a64fx_hwb_get_peinfo(&cmg, &ppe);
    if (!err)
    {
        int cmg_id = (int)cmg;
        pr_info("Fujitsu HWB: Get allocation for CMG %d and Blade %d\n", cmg_id, blade);
        alloc = get_allocation(dev, taskmap, cmg_id, blade);
        if (alloc)
        {
            struct a64fx_cmg_device* cmgdev = &dev->cmgs[cmg_id];
            spin_lock(&cmgdev->cmg_lock);
            err = -ENODEV;
            if (window < 0)
            {
                window = find_first_zero_bit(&cmgdev->bw_active, MAX_BW_PER_CMG);
                pr_info("Fujitsu HWB: Get next free window %d", window);
            }
            if (window >= 0 && window < MAX_BW_PER_CMG && test_bit(window, &cmgdev->bw_active) == 0)
            {
                //cmgdev->bb_map[blade] |= (1<<window);
                pr_info("Fujitsu HWB: Use window %d for CMG %d and Blade %d\n", window, cmg_id, blade);
                *outwindow = window;
                set_bit(window, &cmgdev->bw_active);
/*                assign_bit(window, &cmgdev->bw_active[blade], 1);*/
                set_bit(window, &alloc->win_mask);
                err = 0;
            }
            spin_unlock(&cmgdev->cmg_lock);
        }
        else
        {
            err = -ENODEV;
        }
    }

    
assign_blade_out:
    // release lock
    spin_unlock(&dev->dev_lock);
    pr_info("Fujitsu HWB: Assign returns %d\n", err);
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
        pr_err("Fujitsu HWB: Error to get bb_ctl data\n");
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
            pr_err("Fujitsu HWB: Error to copy back bb_ctl data\n");
            return -1;
        }
        return 0;
    }
    return err;
}

int oss_a64fx_hwb_unassign_blade(struct a64fx_hwb_device *dev, int blade, int window)
{
    int err = 0;
    u8 cmg = 0, ppe = 0;
    struct task_struct* current_task = get_current();
    struct a64fx_task_mapping* taskmap = NULL;
    struct a64fx_task_allocation* alloc = NULL;

    spin_lock(&dev->dev_lock);
    pr_info("Fujitsu HWB: Get task mapping\n");
    taskmap = get_taskmap(dev, current_task);
    if (!taskmap)
    {
        // Try parent task. This happens in case of OpenMP and others.
        pr_info("Fujitsu HWB: Get parent task mapping\n");
        taskmap = get_taskmap(dev, current_task->real_parent);
        if (!taskmap)
        {
            err = -ENODEV;
            goto unassign_blade_out;
        }
    }

    err = _oss_a64fx_hwb_get_peinfo(&cmg, &ppe);
    if (!err)
    {
        int cmg_id = (int)cmg;
        pr_info("Fujitsu HWB: Get allocation for CMG %d and Blade %d\n", cmg_id, blade);
        alloc = get_allocation(dev, taskmap, cmg_id, blade);
        if (alloc)
        {
            struct a64fx_cmg_device* cmgdev = &dev->cmgs[cmg_id];
            spin_lock(&cmgdev->cmg_lock);
            if (test_bit(window, &cmgdev->bw_active))
            {
                pr_info("Fujitsu HWB: Free window %d for CMG %d and Blade %d\n", window, cmg_id, blade);
                clear_bit(window, &cmgdev->bw_active);
                clear_bit(window, &alloc->win_mask);
            }
            spin_unlock(&cmgdev->cmg_lock);
        }
    }

unassign_blade_out:
    spin_unlock(&dev->dev_lock);
    pr_info("Fujitsu HWB: Unassign returns %d\n", err);
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
        pr_err("Fujitsu HWB: Error to get bb_ctl data\n");
        return -1;
    }
    bb_id = (int)ioc_bw_ctl.bb;
    win_id = (int)ioc_bw_ctl.window;
    err = oss_a64fx_hwb_unassign_blade(dev, bb_id, win_id);
    if (!err)
    {
        if (copy_to_user((struct fujitsu_hwb_ioc_bw_ctl __user *)arg, &ioc_bw_ctl, sizeof(struct fujitsu_hwb_ioc_bw_ctl)))
        {
            pr_err("Fujitsu HWB: Error to copy back bb_ctl data\n");
            return -1;
        }
        return 0;
    }
    return err;
}

