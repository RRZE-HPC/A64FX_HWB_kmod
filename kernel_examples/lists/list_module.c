#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>

#include "list_module.h"

struct maindev maindev = {
     .mylist = LIST_HEAD_INIT(maindev.mylist),
};


static int __init list_module_init(void)
{
    int i = 0;
    int num_entries = 5;
    for (i = 0; i < num_entries; i++)
    {
        ListEntry* e = kmalloc(sizeof(ListEntry), GFP_KERNEL);
        if (e)
        {
            pr_info("list_module_init: add entry %d\n", i);
            e->id = i;
            INIT_LIST_HEAD(&e->list);
            list_add(&e->list, &maindev.mylist);
        }
    }
    return 0;
}

static void __exit list_module_exit(void)
{
    struct list_head* cur_list, *tmp;
    ListEntry* e;
    list_for_each_safe(cur_list, tmp, &maindev.mylist)
    {
        e = list_entry(cur_list, ListEntry, list);
        pr_info("list_module_init: free entry %d\n", e->id);
        list_del(&(e->list));
        kfree(e);
    }
    return;
}


MODULE_DESCRIPTION("Module example for linked lists inside a device");
MODULE_AUTHOR("Thomas Gruber <thomas.gruber@fau.de>");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");

module_init(list_module_init);
module_exit(list_module_exit);
