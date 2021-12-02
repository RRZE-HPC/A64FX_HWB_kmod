#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <include/linux/cpumask.h>

#include "a64fx_hwb.h"
#include "a64fx_hwb_cmg.h"
#include "a64fx_hwb_asm.h"
#include "a64fx_hwb_ioctl.h"


/*
 * This file contains basically all CMG-specific sysfs entries. The main device provides
 * only the hwinfo sysfs entry but for CMGs you can get the current state of allocated
 * barrier blades and windows through sysfs. Each CMG is represented by an own kobject to
 * use the common sysfs handlers while still being able to access the driver's CMG structures.
 *
 * While some functions use information from the driver's structures others are directly
 * read from the hardware registers.
 */

static inline struct a64fx_cmg_device *kobj_to_cmg(struct kobject *kobj)
{
    return container_of(kobj, struct a64fx_cmg_device, kobj);
}


static ssize_t core_map_show(struct kobject *kobj, struct kobj_attribute * attr, char* buf)
{
    int i = 0;
    int slen = 0;
    struct a64fx_cmg_device *cmg = kobj_to_cmg(kobj);
    for (i = 0; i < cmg->num_pes; i++)
    {
        slen += snprintf(&buf[slen], PAGE_SIZE-slen, "%d %d\n", cmg->pe_map[i].cpu_id, cmg->pe_map[i].ppe_id);
    }
    buf[slen] = '\0';
    return slen;
}

static ssize_t used_bb_bmap_show(struct kobject *kobj, struct kobj_attribute * attr, char* buf)
{
    struct a64fx_cmg_device *cmg = kobj_to_cmg(kobj);
    return scnprintf(buf, PAGE_SIZE, "%.4lx\n", cmg->bb_active);
}

static ssize_t used_bw_bmap_show(struct kobject *kobj, struct kobj_attribute * attr, char* buf)
{
    int i = 0;
    int slen = 0;
    struct a64fx_cmg_device *cmg = kobj_to_cmg(kobj);
    for (i = 0; i < cmg->num_pes; i++)
    {
        slen += snprintf(&buf[slen], PAGE_SIZE-slen, "%d %.4lx\n", cmg->pe_map[i].cpu_id, cmg->pe_map[i].bw_map);
    }
    buf[slen] = '\0';
    return slen;
}

struct bb_show_info {
    int blade;
    unsigned long mask;
    unsigned long bst;
};

static void _init_sync_bb_show_func(void* info)
{
    struct bb_show_info* bbinfo = (struct bb_show_info*)info;
    read_init_sync_bb(bbinfo->blade, &bbinfo->mask, &bbinfo->bst);
}

static ssize_t init_sync_bb_show(struct a64fx_cmg_device *cmg, int blade, char* buf)
{
    struct bb_show_info info = {blade, 0U, 0U};
    smp_call_function_any(&cmg->cmgmask, _init_sync_bb_show_func, &info, 1);
    return scnprintf(buf, PAGE_SIZE, "%.4lx\n%.4lx\n", info.mask, info.bst);
}

static ssize_t init_sync_bb0_show(struct kobject *kobj, struct kobj_attribute * attr, char* buf)
{
    struct a64fx_cmg_device *cmg = kobj_to_cmg(kobj);
    return init_sync_bb_show(cmg, 0, buf);
}

static ssize_t init_sync_bb1_show(struct kobject *kobj, struct kobj_attribute * attr, char* buf)
{
    struct a64fx_cmg_device *cmg = kobj_to_cmg(kobj);
    return init_sync_bb_show(cmg, 1, buf);
}

static ssize_t init_sync_bb2_show(struct kobject *kobj, struct kobj_attribute * attr, char* buf)
{
    struct a64fx_cmg_device *cmg = kobj_to_cmg(kobj);
    return init_sync_bb_show(cmg, 2, buf);
}

static ssize_t init_sync_bb3_show(struct kobject *kobj, struct kobj_attribute * attr, char* buf)
{
    struct a64fx_cmg_device *cmg = kobj_to_cmg(kobj);
    return init_sync_bb_show(cmg, 3, buf);
}

static ssize_t init_sync_bb4_show(struct kobject *kobj, struct kobj_attribute * attr, char* buf)
{
    struct a64fx_cmg_device *cmg = kobj_to_cmg(kobj);
    return init_sync_bb_show(cmg, 4, buf);
}

static ssize_t init_sync_bb5_show(struct kobject *kobj, struct kobj_attribute * attr, char* buf)
{
    struct a64fx_cmg_device *cmg = kobj_to_cmg(kobj);
    return init_sync_bb_show(cmg, 5, buf);
}

static struct kobj_attribute core_map_attr = __ATTR(core_map, 0444, core_map_show, NULL);
static struct kobj_attribute used_bb_map_attr = __ATTR(used_bb_bmap, 0444, used_bb_bmap_show, NULL);
static struct kobj_attribute used_bw_map_attr = __ATTR(used_bw_bmap, 0444, used_bw_bmap_show, NULL);
static struct kobj_attribute init_sync_bb0_attr = __ATTR(init_sync_bb0, 0400, init_sync_bb0_show, NULL);
static struct kobj_attribute init_sync_bb1_attr = __ATTR(init_sync_bb1, 0400, init_sync_bb1_show, NULL);
static struct kobj_attribute init_sync_bb2_attr = __ATTR(init_sync_bb2, 0400, init_sync_bb2_show, NULL);
static struct kobj_attribute init_sync_bb3_attr = __ATTR(init_sync_bb3, 0400, init_sync_bb3_show, NULL);
static struct kobj_attribute init_sync_bb4_attr = __ATTR(init_sync_bb4, 0400, init_sync_bb4_show, NULL);
static struct kobj_attribute init_sync_bb5_attr = __ATTR(init_sync_bb5, 0400, init_sync_bb5_show, NULL);
struct kobj_type* kobjtype = NULL;


static struct attribute *cmg_attrs[] = {
    &core_map_attr.attr,
    &used_bb_map_attr.attr,
    &used_bw_map_attr.attr,
    &init_sync_bb0_attr.attr,
    &init_sync_bb1_attr.attr,
    &init_sync_bb2_attr.attr,
    &init_sync_bb3_attr.attr,
    &init_sync_bb4_attr.attr,
    &init_sync_bb5_attr.attr,
    NULL,
};

static struct attribute_group cmg_group_attrs = {
    .name = NULL,
    .attrs = cmg_attrs,
};


void init_cmgmask(struct a64fx_cmg_device* dev)
{
    int i = 0;
    for (i = 0; i < dev->num_pes; i++)
    {
        cpumask_set_cpu(dev->pe_map[i].cpu_id, &dev->cmgmask);
    }
}


int initialize_cmg(int cmg_id, struct a64fx_cmg_device* dev, struct kobject* parent)
{
    int ret = 0;
    struct kobject* kobj = NULL;
    
    pr_debug("init CMG%d\n", cmg_id);
    dev->bb_active = 0x0U;
    dev->cmg_id = cmg_id;
    spin_lock_init(&dev->cmg_lock);
    init_cmgmask(dev);

    if (!kobjtype)
    {
        // This is a workaround to get the default kobjtype structure
        pr_debug("create Test kobj to get default kobjtype\n");
        kobj = kobject_create_and_add("Test", parent);
        kobjtype = get_ktype(kobj);
        kobject_put(kobj);
    }
    pr_debug("create CMG kobj\n");
    kobject_init(&dev->kobj, kobjtype);
    ret = kobject_add(&dev->kobj, parent, "CMG%d", cmg_id);
    if(ret){
        pr_err("Cannot create kobject for CMG%d\n", cmg_id);
        return ret;
    }
    kobject_get(&dev->kobj);
    ret = sysfs_create_group(&dev->kobj, &cmg_group_attrs);
    if (ret) {
        pr_err("Cannot create sysfs files for CMG%d\n", cmg_id);
        return ret;
    }
    return 0;
}

void destroy_cmg(struct a64fx_cmg_device* dev)
{
    if (dev)
    {
        pr_debug("destroy CMG%d\n", dev->cmg_id);
        sysfs_remove_group(&dev->kobj, &cmg_group_attrs);
        kobject_put(&dev->kobj);
    }
}
