#ifndef A64FX_HWB_IOCTL_H
#define A64FX_HWB_IOCTL_H

#include "a64fx_hwb.h"

int oss_a64fx_hwb_get_peinfo(int *cmg, int *ppe);
int oss_a64fx_hwb_get_peinfo_ioctl(unsigned long arg);
int oss_a64fx_hwb_allocate(struct a64fx_hwb_device *dev, unsigned long arg);
int oss_a64fx_hwb_free(struct a64fx_hwb_device *dev, int cmg_id, int bb_id);
int oss_a64fx_hwb_free_ioctl(struct a64fx_hwb_device *dev, unsigned long arg);
int oss_a64fx_hwb_assign_blade(struct a64fx_hwb_device *dev, int blade, int window);
int oss_a64fx_hwb_assign_blade_ioctl(struct a64fx_hwb_device *dev, unsigned long arg);
int oss_a64fx_hwb_unassign_blade(struct a64fx_hwb_device *dev, int bb, int window);
int oss_a64fx_hwb_unassign_blade_ioctl(struct a64fx_hwb_device *dev, unsigned long arg);

struct a64fx_task_mapping * get_taskmap(struct a64fx_hwb_device *dev, struct task_struct* task);
int unregister_task(struct a64fx_hwb_device *dev, struct a64fx_task_mapping *taskmap);

#endif
