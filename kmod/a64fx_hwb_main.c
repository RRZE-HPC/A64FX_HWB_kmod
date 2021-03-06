#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__
#include <linux/version.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <include/linux/smp.h>
#include <include/linux/cpumask.h>
#include <linux/slab.h>
#include <linux/list.h>

#include "a64fx_hwb.h"
#include "a64fx_hwb_cmg.h"
#include "fujitsu_hpc_ioctl.h"
#include "a64fx_hwb_ioctl.h"
#include "a64fx_hwb_asm.h"

static long oss_a64fx_hwb_ioctl(struct file *file, unsigned int ioc, unsigned long arg);
static int oss_a64fx_hwb_open(struct inode *inode, struct file *file);
static int oss_a64fx_hwb_close(struct inode *inode, struct file *file);




static const struct file_operations oss_a64fx_hwb_fops = {
	.owner = THIS_MODULE,
	.open = oss_a64fx_hwb_open,
	.release = oss_a64fx_hwb_close,
	.unlocked_ioctl = oss_a64fx_hwb_ioctl,
	.compat_ioctl = oss_a64fx_hwb_ioctl,
};


/*
 * Global hwinfo attribute (sysfs file)
 */

static ssize_t hwinfo_show(struct device *device, struct device_attribute *attr, char *buf)
{
    struct a64fx_hwb_device* dev = dev_get_drvdata(device);
    return scnprintf(buf, PAGE_SIZE, "%d %d %d %d\n", dev->num_cmgs, dev->num_bb_per_cmg, dev->num_bw_per_cmg, dev->max_pe_per_cmg);
}

DEVICE_ATTR_RO(hwinfo);

/*struct attribute *oss_a64fx_sysfs_base_attrs[] = {*/
/*    &dev_attr_hwinfo.attr,*/
/*    NULL,*/
/*};*/


/*
 * Global device setup, creates /dev/fujitsu_hwb
 */
static struct a64fx_hwb_device oss_a64fx_hwb_device = {
    .misc = {
        .minor = MISC_DYNAMIC_MINOR,
        .name = "fujitsu_hwb",
        .fops = &oss_a64fx_hwb_fops,
        .mode = 0666,
    },
    .task_list = LIST_HEAD_INIT(oss_a64fx_hwb_device.task_list),
    .num_tasks = 0,
    .num_cmgs = 0,
    .active_count = 0,
};

struct hwb_ctrl_info {
    int el0ae;
    int el1ae;
};

static void oss_a64fx_hwb_ctrl_func(void* info)
{
    struct hwb_ctrl_info* cinfo = (struct hwb_ctrl_info*) info;
    write_hwb_ctrl(cinfo->el0ae, cinfo->el1ae);
    read_hwb_ctrl(&cinfo->el0ae, &cinfo->el1ae);
}

static int oss_a64fx_hwb_open(struct inode *inode, struct file *file)
{
    pr_debug("Opening device\n");
    spin_lock(&oss_a64fx_hwb_device.dev_lock);

    oss_a64fx_hwb_device.active_count++;
    pr_debug("Active Tasks %d\n", oss_a64fx_hwb_device.active_count);
    spin_unlock(&oss_a64fx_hwb_device.dev_lock);
    return 0;
}

static int oss_a64fx_hwb_close(struct inode *inode, struct file *file)
{
    int err = 0;
    struct task_struct* task = get_current();
    struct a64fx_task_mapping *taskmap = NULL;
    spin_lock(&oss_a64fx_hwb_device.dev_lock);
    pr_debug("Closing device (Active %d)\n", oss_a64fx_hwb_device.active_count);
    if (oss_a64fx_hwb_device.active_count > 0)
    {
        oss_a64fx_hwb_device.active_count--;
        taskmap = get_taskmap(&oss_a64fx_hwb_device, task);
        if (taskmap && task_pid_nr(task) == task_pid_nr(taskmap->task))
        {
            err = unregister_task(&oss_a64fx_hwb_device, taskmap);
            if (err)
            {
                pr_debug("Failed close for task %d (TGID %d)\n", task->pid, task->tgid);
            }
        }
    }
    else
    {
        pr_err("Close on not opened device\n");
    }
    pr_debug("Active Tasks %d\n", oss_a64fx_hwb_device.active_count);
    spin_unlock(&oss_a64fx_hwb_device.dev_lock);
    return 0;
}

static long oss_a64fx_hwb_ioctl(struct file *file, unsigned int ioc, unsigned long arg)
{
    int err = 0;
    
    switch (ioc) {
        case FUJITSU_HWB_IOC_GET_PE_INFO:
/*            pr_debug("FUJITSU_HWB_IOC_GET_PE_INFO...\n");*/
            err = oss_a64fx_hwb_get_peinfo_ioctl(arg);
            break;
        case FUJITSU_HWB_IOC_BW_ASSIGN:
            pr_debug("FUJITSU_HWB_IOC_BW_ASSIGN...\n");
            err = oss_a64fx_hwb_assign_blade_ioctl(&oss_a64fx_hwb_device, arg);
            break;
        case FUJITSU_HWB_IOC_BW_UNASSIGN:
            pr_debug("FUJITSU_HWB_IOC_BW_UNASSIGN...\n");
            err = oss_a64fx_hwb_unassign_blade_ioctl(&oss_a64fx_hwb_device, arg);
            break;
        case FUJITSU_HWB_IOC_BB_ALLOC:
            pr_debug("FUJITSU_HWB_IOC_BB_ALLOC...\n");
            err = oss_a64fx_hwb_allocate_ioctl(&oss_a64fx_hwb_device, arg);
            break;
        case FUJITSU_HWB_IOC_BB_FREE:
            pr_debug("FUJITSU_HWB_IOC_BB_FREE...\n");
            err = oss_a64fx_hwb_free_ioctl(&oss_a64fx_hwb_device, arg);
            break;
        case FUJITSU_HWB_IOC_RESET:
            pr_debug("FUJITSU_HWB_IOC_RESET...\n");
            err = oss_a64fx_hwb_reset_ioctl(&oss_a64fx_hwb_device, arg);
        default:
            err = -ENOTTY;
            break;
    }

    return err;
}

void fill_pe_map(void *info)
{
    int cmg = 0;
    int ppe = 0;
    struct a64fx_hwb_device* dev = (struct a64fx_hwb_device*)info;
    // preemption disabled, safe to use smp_processor_id()
    int cpuid = smp_processor_id();
    oss_a64fx_hwb_get_peinfo(&cmg, &ppe);
    dev->cmgs[cmg].pe_map[ppe].cpu_id = cpuid;
    dev->cmgs[cmg].pe_map[ppe].cmg_id = cmg;
    dev->cmgs[cmg].pe_map[ppe].ppe_id = ppe;
    pr_debug("CPU %d CMG %d PPE %d\n", cpuid, cmg, ppe);
    dev->cmgs[cmg].pe_map[ppe].bw_map = 0x0;
}

static int get_max_pe_per_cmg(struct a64fx_hwb_device* dev)
{
    int i = 0;
    int j = 0;
    int maxcount = 0;
    int tmpcount = 0;
    for (i = 0; i < MAX_NUM_CMG; i++)
    {
        tmpcount = 0;
        for (j = 0; j < MAX_PE_PER_CMG; j++)
        {
            if (dev->cmgs[i].pe_map[j].cpu_id >= 0)
            {
                tmpcount++;
            }
        }
        pr_debug("CMG %d has %d PEs\n", i, tmpcount);
        dev->cmgs[i].num_pes = tmpcount;
        if (tmpcount > maxcount)
        {
            maxcount = tmpcount;
        }
    }
    return maxcount;
}

static int __init oss_a64fx_hwb_init(void)
{
    int err = 0;
    int i = 0;
    int j = 0;
    struct hwb_ctrl_info info = {1, 1};
    struct device *dev = NULL;
    pr_debug("initializing...\n");

    // Create misc device fujitsu_hwb
    err = misc_register(&oss_a64fx_hwb_device.misc);
    if (err) {
        pr_err("misc_register failed\n");
        return err;
    }
    dev = oss_a64fx_hwb_device.misc.this_device;
    // Set driver data to reuse it in hwinfo_show()
    dev_set_drvdata(dev, &oss_a64fx_hwb_device);
    // Create global sysfs attribute 'hwinfo'
    err = device_create_file(dev, &dev_attr_hwinfo);
    if (err) {
        pr_err("creation of hwinfo sysfs file failed\n");
        goto unreg_miscdev;
    }

    // Iterate over CMGs and initialize data structures and CMG
    // related sysfs files
    for (i = 0; i < MAX_NUM_CMG; i++)
        memset(oss_a64fx_hwb_device.cmgs[i].pe_map, -1, sizeof(struct a64fx_core_mapping)*MAX_PE_PER_CMG);

    on_each_cpu(fill_pe_map, (void*)&oss_a64fx_hwb_device, 1);
    oss_a64fx_hwb_device.num_cmgs = MAX_NUM_CMG;
    oss_a64fx_hwb_device.max_pe_per_cmg = get_max_pe_per_cmg(&oss_a64fx_hwb_device);
    oss_a64fx_hwb_device.num_bb_per_cmg = MAX_BB_PER_CMG;
    oss_a64fx_hwb_device.num_bw_per_cmg = MAX_BW_PER_CMG;

    for (i = 0; i < MAX_NUM_CMG; i++)
    {
        err = initialize_cmg(i, &oss_a64fx_hwb_device.cmgs[i], &dev->kobj);
        if (err < 0)
        {
            for (j = 0; j < i; j++)
            {
                destroy_cmg(&oss_a64fx_hwb_device.cmgs[j]);
            }
            goto remove_global_sysfs;
        }
    }

    pr_debug("init done\n");
    for_each_online_cpu(j)
    {
        smp_call_function_single(j, oss_a64fx_hwb_ctrl_func, &info, 1);
    }
    return err;
remove_global_sysfs:
    device_remove_file(dev, &dev_attr_hwinfo);
unreg_miscdev:
    misc_deregister(&oss_a64fx_hwb_device.misc);
    return err;
}

static void __exit oss_a64fx_hwb_exit(void)
{
    int i = 0;
    int j = 0;
    struct device *dev = NULL;
    struct hwb_ctrl_info info = {0, 0};
    pr_debug("exiting...\n");
    for_each_online_cpu(j)
    {
        smp_call_function_single(j, oss_a64fx_hwb_ctrl_func, &info, 1);
    }
    // Iterate over CMGs and destroy data structures and CMG
    // related sysfs files
    for (i = 0; i < oss_a64fx_hwb_device.num_cmgs; i++)
    {
        destroy_cmg(&oss_a64fx_hwb_device.cmgs[i]);
    }
    dev = oss_a64fx_hwb_device.misc.this_device;
    // Remove global sysfs attribute 'hwinfo'
    device_remove_file(dev, &dev_attr_hwinfo);
    // Remove misc device fujitsu_hwb
    misc_deregister(&oss_a64fx_hwb_device.misc);
    pr_debug("exit done\n");
}

MODULE_DESCRIPTION("Module for A64fx hardware barrier");
MODULE_AUTHOR("Thomas Gruber <thomas.gruber@fau.de>");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.2");

module_init(oss_a64fx_hwb_init);
module_exit(oss_a64fx_hwb_exit);



