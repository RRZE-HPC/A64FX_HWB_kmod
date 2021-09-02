#ifndef LIST_MODULE_H
#define LIST_MODULE_H


typedef struct {
    int id;
    struct list_head list;
} ListEntry;


typedef struct maindev {
    spinlock_t list_lock;
    struct list_head mylist;
} MainDevice;


#endif
