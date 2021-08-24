#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
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
    int i = 0;
    int j = 0;
    int k = 0;
    int new_task = 0;
    int cmg_id = -1;
    int bb_id = -1;
    struct a64fx_cmg_device *cmg = NULL;
    struct a64fx_task_mapping *task = NULL;
    struct a64fx_task_allocation *alloc = NULL;
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
    i = pemask_to_cpumask(ioc_bb_ctl.size, &mask, &cpumask);
    if (i == 0)
    {
        err = -EFAULT;
        goto allocate_exit;
    }

    for (i = 0; i < dev->num_tasks; i++)
    {
        if (current_task == dev->tasks[i].task)
        {
            task = &dev->tasks[i];
        }
    }
    if (!task)
    {
        if ((dev->num_tasks == MAX_NUM_CMG*MAX_BB_PER_CMG))
        {
            task = &dev->tasks[dev->num_tasks];
            new_task++;
        }
        else
        {
            err = -EBUSY;
            goto allocate_exit;
        }
    }
    spin_lock(&dev->dev_lock);
    task->task = current_task;
    
    alloc = &task->allocations[task->num_allocations];
    ioc_bb_ctl.cmg = 0x0;
    for_each_cpu(i, &cpumask)
    {
        for (j = 0; j < dev->num_cmgs; j++)
        {
            cmg = &dev->cmgs[j];
            for (k = 0; k < cmg->num_pes; k++)
            {
                if (cmg->pe_map[k].cpu_id == i)
                {
                    if (cmg_id < 0)
                    {
                        cmg_id = cmg->pe_map[k].cmg_id;
                    } else {
                        pr_err("Fujitsu HWB: Allocation for multiple CMGs not allowed\n");
                    }
                }
            }
        }
        cpumask_set_cpu(i, &alloc->cpumask);
    }
    cmg = &dev->cmgs[cmg_id];
    for (i = 0; i < dev->num_bb_per_cmg; i++)
    {
        
        if (!(cmg->bb_active & (1<<i)))
        {
            spin_lock(&cmg->cmg_lock);
            cmg->bb_active |= (1<<i);
            bb_id = i;
            alloc->bb = i;
            alloc->cmg = cmg_id;
            pr_info("Fujitsu HWB: Allocate BB %d at CMG%d\n", alloc->bb, alloc->cmg);
            task->num_allocations++;
            spin_unlock(&cmg->cmg_lock);
            break;
        }
    }
    
    dev->num_tasks += new_task;
    spin_unlock(&dev->dev_lock);
allocate_exit:
    if (copy_to_user((struct fujitsu_hwb_ioc_bb_ctl __user *)arg, &ioc_bb_ctl, sizeof(struct fujitsu_hwb_ioc_bb_ctl __user)))
    {
        pr_err("Fujitsu HWB: Error to copy back bb_ctl data\n");
        return -1;
    }
    
    // release lock

    return err;
}

int oss_a64fx_hwb_free(struct a64fx_hwb_device *dev, int cmg_id, int bb_id)
{
    int i = 0;
    int j = 0;
    struct a64fx_cmg_device *cmg = NULL;
    struct a64fx_task_mapping *task = NULL;
    struct a64fx_task_allocation *alloc = NULL;
    struct task_struct* current_task = get_current();

    spin_lock(&dev->dev_lock);
    for (i = 0; i < dev->num_tasks; i++)
    {
        task = &dev->tasks[i];
        if (task->task == current_task)
        {
            
/*            dev->tasks[i].task = dev->tasks[dev->num_tasks-1].task;*/
/*            dev->tasks[i].cpumask = dev->tasks[dev->num_tasks-1].cpumask;*/
/*            dev->tasks[i].bb = dev->tasks[dev->num_tasks-1].bb;*/
/*            dev->tasks[i].cmg_mask = dev->tasks[dev->num_tasks-1].cmg_mask;*/
            for (j = 0; j < task->num_allocations; j++)
            {
                alloc = &task->allocations[j];
                if (alloc->cmg == cmg_id && alloc->bb == bb_id)
                {
                    cmg = &dev->cmgs[cmg_id];
                    spin_lock(&cmg->cmg_lock);
                    cmg->bb_active &= (~(1<<bb_id));
                    cpumask_copy(&alloc->cpumask, &task->allocations[task->num_allocations-1].cpumask);
                    alloc->bb = task->allocations[task->num_allocations-1].bb;
                    alloc->cmg = task->allocations[task->num_allocations-1].cmg;
                    pr_info("Fujitsu HWB: Free BB %d at CMG%d\n", bb_id, cmg_id);
                    task->num_allocations--;
                    spin_unlock(&cmg->cmg_lock);
                    break;
                }
            }
            if (task->num_allocations == 0)
            {
                dev->tasks[i].task = dev->tasks[dev->num_tasks-1].task;
                dev->tasks[i].num_allocations = dev->tasks[dev->num_tasks-1].num_allocations;
                memcpy(dev->tasks[i].allocations, dev->tasks[dev->num_tasks-1].allocations, dev->tasks[i].num_allocations*sizeof(struct a64fx_task_allocation));
                dev->num_tasks--;
            }
/*            spin_lock(&dev->cmgs[cmg_id].cmg_lock);*/
/*            dev->cmgs[cmg_id].bb_active &= (~(1<<bb_id));*/
/*            dev->tasks[i].cmg_mask &= (~(1<<cmg_id));*/
/*            spin_unlock(&dev->cmgs[cmg_id].cmg_lock);*/
/*            pr_info("Fujitsu HWB: Free BB %d at CMG%d\n", bb_id, cmg_id);*/
/*            dev->num_tasks--;*/
            break;
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

int oss_a64fx_hwb_assign_blade(struct a64fx_hwb_device *dev, int blade, int window)
{
    int err = 0;
    u8 cmg = 0, ppe = 0;
    
    struct task_struct* current_task = get_current();
    
    // acquire lock
    
    err = _oss_a64fx_hwb_get_peinfo(&cmg, &ppe);
    if (!err)
    {
        int cmg_id = (int)cmg;
        struct a64fx_cmg_device* cmgdev = &dev->cmgs[cmg_id];
        spin_lock(&cmgdev->cmg_lock);
        cmgdev->bb_map[blade] |= (1<<window);
        spin_unlock(&cmgdev->cmg_lock);
    }

    
    
    // release lock
    
    
    return err;
}

int oss_a64fx_hwb_assign_blade_ioctl(struct a64fx_hwb_device *dev, unsigned long arg)
{
    int err = 0;
    int bb_id = 0;
    int win_id = 0;
    struct fujitsu_hwb_ioc_bw_ctl ioc_bw_ctl = {0};
    if (copy_from_user(&ioc_bw_ctl, (struct fujitsu_hwb_ioc_bw_ctl __user *)arg, sizeof(struct fujitsu_hwb_ioc_bw_ctl)))
    {
        pr_err("Fujitsu HWB: Error to get bb_ctl data\n");
        return -1;
    }
    bb_id = (int)ioc_bw_ctl.bb;
    win_id = (int)ioc_bw_ctl.window;
    err = oss_a64fx_hwb_assign_blade(dev, bb_id, win_id);
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

int oss_a64fx_hwb_unassign_blade(struct a64fx_hwb_device *dev, int bb, int window)
{
    int err = 0;
    u8 cmg = 0, ppe = 0;
    struct fujitsu_hwb_ioc_bw_ctl ioc_bw_ctl = {0};
    struct task_struct* current_task = get_current();

    err = _oss_a64fx_hwb_get_peinfo(&cmg, &ppe);
    if (!err)
    {
        struct a64fx_cmg_device* cmgdev = &dev->cmgs[(int)cmg];
        spin_lock(&cmgdev->cmg_lock);
        cmgdev->bb_map[bb] &= ~(1<<window);
        spin_unlock(&cmgdev->cmg_lock);
    }

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

