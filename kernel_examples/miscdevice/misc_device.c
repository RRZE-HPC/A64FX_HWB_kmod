#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>

#include "misc_device.h"

static const struct file_operations mydev_fops = {
	.owner = THIS_MODULE,
};

MyDevice mydev = {
    .dev = {
        .minor = MISC_DYNAMIC_MINOR,
        .name = "mymisc",
        .fops = &mydev_fops,
        .mode = 0666,
    },
};

static int __init miscdev_init(void)
{
    int err = misc_register(&mydev.dev);
    if (err) {
        pr_err("misc_register failed\n");
        return err;
    }
    return 0;
}


static void __exit miscdev_exit(void)
{
    misc_deregister(&mydev.dev);
    return;
}

MODULE_DESCRIPTION("Module for creating a misc device");
MODULE_AUTHOR("Thomas Gruber <thomas.gruber@fau.de>");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");

module_init(miscdev_init);
module_exit(miscdev_exit);
