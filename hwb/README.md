# A64FX Hardware Barrier

This folder contains the kernel module (`kmod`) and [user-space library](https://github.com/fujitsu/hardware_barrier) (`ulib`) for the A64FX hardware barrier.

Each CMG (core-memory-group) contains 4 barrier blade and 6 barrier window registers. 

# State of kernel module

- [x] Create `misc` device `/dev/fujitsu_hwb`
- [x] Create `sysfs` interfaces based on the [sysfs description](https://github.com/fujitsu/hardware_barrier/blob/develop/sysfs_interface.md)
  - [x] Create global read-only entry `/sys/class/misc/fujitsu_hwb/hwinfo`
  - [x] Create subfolders per CMG with various read-only entries below `/sys/class/misc/fujitsu_hwb/CMG[0-3]/`
- [x] Create IOCTL call `FUJITSU_HWB_IOC_GET_PE_INFO` to retrieve the physical location of a hardware thread
- [ ] Create IOCTL call `FUJITSU_HWB_IOC_BB_ALLOC` to allocate a barrier blade in a CMG
- [ ] Create IOCTL call `FUJITSU_HWB_IOC_BB_FREE` to free a barrier blade in a CMG
- [ ] Create IOCTL call `FUJITSU_HWB_IOC_BW_ASSIGN` to assign a barrier window to a barrier blade in a CMG
- [ ] Create IOCTL call `FUJITSU_HWB_IOC_BW_UNASSIGN` to release a barrier window from a barrier blade in a CMG

