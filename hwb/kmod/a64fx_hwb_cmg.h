#ifndef A64FX_HWB_CMG_H
#define A64FX_HWB_CMG_H

int initialize_cmg(int cmg_id, struct a64fx_cmg_device* dev, struct kobject* parent);
void destroy_cmg(struct a64fx_cmg_device* dev);

#endif
