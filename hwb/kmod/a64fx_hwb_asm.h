#ifndef A64FX_HWB_ASM_H
#define A64FX_HWB_ASM_H

#define A64FX_PEINFO_CMG_MASK 0x3UL
#define A64FX_PEINFO_CMG_OFFSET 4
#define A64FX_PEINFO_PPE_MASK 0xFUL
int read_peinfo(u8 *cmg, u8 *ppe);


#define A64FX_HWB_CTRL_EL0AE_SHIFT 62
#define A64FX_HWB_CTRL_EL1AE_SHIFT 63
int read_hwb_ctrl(int *el0ae, int *el1ae);
int write_hwb_ctrl(int el0ae, int el1ae);


#define A64FX_HWB_INIT_BST_MASK 0x1FFFUL
#define A64FX_HWB_INIT_BST_SHIFT 32
#define A64FX_HWB_INIT_LBSY_SHIFT 20
int read_init_sync_bb(int bb, unsigned long *mask, unsigned long *bst);
int write_init_sync_bb(int blade, unsigned long bst_mask);


#define A64FX_HWB_ASSIGN_BB_MASK 0x7UL
#define A64FX_HWB_ASSIGN_VALID_BIT 63
int read_assign_sync_wr(int window, int* valid, int *blade);
int write_assign_sync_wr(int window, int valid, int blade);


#define A64FX_HWB_SYNC_WINDOW_MASK 0x1UL
int read_bst_sync_wr(int window, int* sync);
int write_bst_sync_wr(int window, int sync);

#endif /* A64FX_HWB_ASM_H */
