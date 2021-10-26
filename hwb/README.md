# A64FX Hardware Barrier

This folder contains the kernel module (`kmod`) and [user-space library](https://github.com/fujitsu/hardware_barrier) (`ulib`) for the A64FX hardware barrier.

Each CMG (core-memory-group) contains 4 barrier blade and 6 barrier window registers.

# A64FX HWB scheme
1.  **Initialize BBs**
    Assing PEs to one of the BB in CMG
    ```
      |     +-----+ +-----+ +-----+ +-----+ +-----+ +-----+
      |     | BB0 | | BB1 | | BB2 | | BB3 | | BB4 | | BB5 |
      |     +-----+ +-----+ +-----+ +-----+ +-----+ +-----+
    CMG      ^        ^       ^        ^       ^       ^
      |      | _______|_______|________|_______|_______|
      |      |/
      |     PE[0-12]
    ```
    by using `write_init_sync_bb(int blade, unsigned long bst_mask)` and setting all BST bits initially to `0`.
2.  **Assign WRs**
    Assign WRs so that a window maps to a BB
    ```
      |     +-----+
    CMG     | BBx |
      |     +-----+
               ^
               |________________________
               |       |       |       |
      |     +-----+ +-----+ +-----+ +-----+
    PE      | WR0 | | WR1 | | WR2 | | WR3 |
      |     +-----+ +-----+ +-----+ +-----+
    ```
    by using `write_assign_sync_wr(int window, int valid, int blade)`
3.  **Do work**
    Each PE processes its workload.
4.  **Set BST for each finished PE**
    When a PE is finished, read `LSBY`, invert it and write its updated BST bit.
    ```
      |    +--------------------------------------------------------+
    CMG    |     RES0     | BST_MASK | RES0 | LBSY | RES0 |   BST   |
      |    +--------------------------------------------------------+
                                               ^
      |                                        |    (invert)
    PE                                         0x ==========> 0x1
      |                                                        | (based on PE id)
                                                              \/
      |    +--------------------------------------------------------+
    CMG    |     RES0     | BST_MASK | RES0 | LBSY | RES0 |   BST   |
      |    +--------------------------------------------------------+
    ```
    This is done by using `read_bst_sync_wr(int window, int* sync)` and `write_bst_sync_wr(int window, int sync)`
    `LSBY` gets updated based on all participating `BST` bits defined by `BST_MASK` and is set to
    `0x1` when all participating `BST` bits are `0x1`.
5.  **Check if HWB is done**
    Read out `LSBY` and check if it is equal to the local BST bit (i.e., `0x1`) with `read_bst_sync_wr(int window, int* sync)`.
    Repeat this step as long as check was unsuccessful.


# State of kernel module

- [x] Create `misc` device `/dev/fujitsu_hwb`
- [x] Create `sysfs` interfaces based on the [sysfs description](https://github.com/fujitsu/hardware_barrier/blob/develop/sysfs_interface.md)
  - [x] Create global read-only entry `/sys/class/misc/fujitsu_hwb/hwinfo`
  - [x] Create subfolders per CMG with various read-only entries below `/sys/class/misc/fujitsu_hwb/CMG[0-3]/`
- [x] Create IOCTL call `FUJITSU_HWB_IOC_GET_PE_INFO` to retrieve the physical location of a hardware thread
- [x] Create IOCTL call `FUJITSU_HWB_IOC_BB_ALLOC` to allocate a barrier blade in a CMG
- [x] Create IOCTL call `FUJITSU_HWB_IOC_BB_FREE` to free a barrier blade in a CMG
- [x] Create IOCTL call `FUJITSU_HWB_IOC_BW_ASSIGN` to assign a barrier window to a barrier blade in a CMG
- [x] Create IOCTL call `FUJITSU_HWB_IOC_BW_UNASSIGN` to release a barrier window from a barrier blade in a CMG
- [x] Ensure all tests shipped with the user-space library work

**DONE!**

