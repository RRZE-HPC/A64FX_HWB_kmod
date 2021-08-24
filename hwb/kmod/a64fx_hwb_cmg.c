#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <include/linux/cpumask.h>

#include "a64fx_hwb.h"
#include "a64fx_hwb_cmg.h"
#include "a64fx_hwb_ioctl.h"

static inline struct a64fx_cmg_device *kobj_to_cmg(struct kobject *kobj)
{
    return container_of(kobj, struct a64fx_cmg_device, kobj);
}


static ssize_t core_map_show(struct kobject *kobj, struct kobj_attribute * attr, char* buf)
{
    int i = 0;
    int slen = 0;
    struct a64fx_cmg_device *cmg = kobj_to_cmg(kobj);
    pr_info("Fujitsu HWB: Core_map for CMG%d with %d PEs\n", cmg->cmg_id, cmg->num_pes);
    for (i = 0; i < cmg->num_pes; i++)
    {
        slen += snprintf(&buf[slen], PAGE_SIZE-slen, "%d %d\n", cmg->pe_map[i].cpu_id, cmg->pe_map[i].ppe_id);
        pr_info("Fujitsu HWB: CMG %d CPU %d PPE %d\n", cmg->cmg_id, cmg->pe_map[i].cpu_id, cmg->pe_map[i].ppe_id);
    }
    buf[slen] = '\0';
    return slen;
}

static ssize_t used_bb_bmap_show(struct kobject *kobj, struct kobj_attribute * attr, char* buf)
{
    struct a64fx_cmg_device *cmg = kobj_to_cmg(kobj);
    return scnprintf(buf, PAGE_SIZE, "%.4x\n", cmg->bb_active);
}

static ssize_t used_bw_bmap_show(struct kobject *kobj, struct kobj_attribute * attr, char* buf)
{
    int i = 0;
    int slen = 0;
    struct a64fx_cmg_device *cmg = kobj_to_cmg(kobj);
    for (i = 0; i < cmg->num_pes; i++)
    {
        slen += snprintf(&buf[slen], PAGE_SIZE-slen, "%d %.4x\n", cmg->pe_map[i].cpu_id, cmg->pe_map[i].bw_map);
    }
    buf[slen] = '\0';
    return slen;
}

static ssize_t init_sync_bb0_show(struct kobject *kobj, struct kobj_attribute * attr, char* buf)
{
    u32 mask = 0x0U;
    u32 bst = 0x0U;
    return scnprintf(buf, PAGE_SIZE, "%.4x\n%.4x\n", mask, bst);
}

static struct kobj_attribute core_map_attr = __ATTR(core_map, 0444, core_map_show, NULL);
static struct kobj_attribute used_bb_map_attr = __ATTR(used_bb_bmap, 0444, used_bb_bmap_show, NULL);
static struct kobj_attribute used_bw_map_attr = __ATTR(used_bw_bmap, 0444, used_bw_bmap_show, NULL);
static struct kobj_attribute init_sync_bb0_attr = __ATTR(init_sync_bb0, 0400, init_sync_bb0_show, NULL);
struct kobj_type* kobjtype = NULL;



int initialize_cmg(int cmg_id, struct a64fx_cmg_device* dev, struct kobject* parent)
{
    int ret = 0;
    //char buf[20];
    int i = 0;
    int cpus_per_pe = num_online_cpus()/MAX_NUM_CMG;
    struct kobject* kobj = NULL;
    
    pr_info("Fujitsu HWB: init CMG%d\n", cmg_id);
    dev->bb_active = 0x0U;
    dev->cmg_id = cmg_id;
    spin_lock_init(&dev->cmg_lock);

    if (!kobjtype)
    {
        pr_info("Fujitsu HWB: create TeST kobj\n");
        kobj = kobject_create_and_add("TeST", parent);
        kobjtype = get_ktype(kobj);
        kobject_put(kobj);
    }
    pr_info("Fujitsu HWB: create CMG kobj\n");
    kobject_init(&dev->kobj, kobjtype);
    ret = kobject_add(&dev->kobj, parent, "CMG%d", cmg_id);
    if(ret){
        pr_err("Cannot create kobject for CMG%d\n", cmg_id);
        return ret;
    }
    kobject_get(&dev->kobj);
    ret = sysfs_create_file(&dev->kobj, &core_map_attr.attr);
    if(ret){
        pr_err("Cannot create core_map for CMG%d\n", cmg_id);
        return ret;
    }
    ret = sysfs_create_file(&dev->kobj, &used_bb_map_attr.attr);
    if(ret){
        pr_err("Cannot create used_bb_map for CMG%d\n", cmg_id);
        return ret;
    }
    ret = sysfs_create_file(&dev->kobj, &used_bw_map_attr.attr);
    if(ret){
        pr_err("Cannot create used_bw_map for CMG%d\n", cmg_id);
        return ret;
    }
    ret = sysfs_create_file(&dev->kobj, &init_sync_bb0_attr.attr);
    if(ret){
        pr_err("Cannot create init_sync_bb0 for CMG%d\n", cmg_id);
        return ret;
    }
    return 0;
}

void destroy_cmg(struct a64fx_cmg_device* dev)
{
    sysfs_remove_file(&dev->kobj, &init_sync_bb0_attr.attr);
    sysfs_remove_file(&dev->kobj, &used_bw_map_attr.attr);
    sysfs_remove_file(&dev->kobj, &used_bb_map_attr.attr);
    sysfs_remove_file(&dev->kobj, &core_map_attr.attr);
    kobject_put(&dev->kobj);
}
