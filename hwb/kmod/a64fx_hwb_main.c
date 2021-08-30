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
};

static int oss_a64fx_hwb_open(struct inode *inode, struct file *file)
{
    pr_info("Fujitsu HWB: Opening device\n");
    return 0;
}

static int oss_a64fx_hwb_close(struct inode *inode, struct file *file)
{
    pr_info("Fujitsu HWB: Closing device\n");
    return 0;
}

static long oss_a64fx_hwb_ioctl(struct file *file, unsigned int ioc, unsigned long arg)
{
    int err = 0;
    
    switch (ioc) {
        case FUJITSU_HWB_IOC_GET_PE_INFO:
            pr_info("Fujitsu HWB: FUJITSU_HWB_IOC_GET_PE_INFO...\n");
            err = oss_a64fx_hwb_get_peinfo_ioctl(arg);
            break;
        case FUJITSU_HWB_IOC_BW_ASSIGN:
            pr_info("Fujitsu HWB: FUJITSU_HWB_IOC_BW_ASSIGN...\n");
            err = oss_a64fx_hwb_assign_blade_ioctl(&oss_a64fx_hwb_device, arg);
            break;
        case FUJITSU_HWB_IOC_BW_UNASSIGN:
            pr_info("Fujitsu HWB: FUJITSU_HWB_IOC_BW_UNASSIGN...\n");
            err = oss_a64fx_hwb_unassign_blade_ioctl(&oss_a64fx_hwb_device, arg);
            break;
        case FUJITSU_HWB_IOC_BB_ALLOC:
            pr_info("Fujitsu HWB: FUJITSU_HWB_IOC_BB_ALLOC...\n");
            err = oss_a64fx_hwb_allocate(&oss_a64fx_hwb_device, arg);
            break;
        case FUJITSU_HWB_IOC_BB_FREE:
            pr_info("Fujitsu HWB: FUJITSU_HWB_IOC_BB_FREE...\n");
            err = oss_a64fx_hwb_free_ioctl(&oss_a64fx_hwb_device, arg);
            break;
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
        pr_info("CMG%d count %d\n", i, tmpcount);
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
    struct device *dev = NULL;
    pr_info("Fujitsu HWB: initializing...\n");

    // Create misc device fujitsu_hwb
    err = misc_register(&oss_a64fx_hwb_device.misc);
    if (err) {
        pr_err("Fujitsu HWB: misc_register failed\n");
        return err;
    }
    dev = oss_a64fx_hwb_device.misc.this_device;
    // Set driver data to reuse it in hwinfo_show()
    dev_set_drvdata(dev, &oss_a64fx_hwb_device);
    // Create global sysfs attribute 'hwinfo'
    err = device_create_file(dev, &dev_attr_hwinfo);
    if (err) {
        pr_err("Fujitsu HWB: creation of hwinfo sysfs file failed\n");
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

    pr_info("Fujitsu HWB: init done\n");
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
    struct device *dev = NULL;
    pr_info("Fujitsu HWB: exiting...\n");
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
    pr_info("Fujitsu HWB: exit done\n");
}

MODULE_DESCRIPTION("Module for A64fx hardware barrier");
MODULE_AUTHOR("Thomas Gruber <thomas.gruber@fau.de>");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");

module_init(oss_a64fx_hwb_init);
module_exit(oss_a64fx_hwb_exit);



