#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

// 模块许可证声明
MODULE_LICENSE("GPL");
MODULE_AUTHOR("GEDU Shanghai Lab");
MODULE_DESCRIPTION("A minimal Linux driver for DDD camp in 2025");
MODULE_VERSION("0.1");

// 模块初始化函数
static int __init minimal_init(void) {
    int current_year = 0x2025;

    printk(KERN_INFO "Minimal driver: Hello, DDD World! Current year is %x. Current command is %s\n", 
		    current_year, current->comm);

    return 0;
}

// 模块退出函数
static void __exit minimal_exit(void) {
    printk(KERN_INFO "Minimal driver: Goodbye, World!\n");
}

// 注册初始化和退出函数
module_init(minimal_init);
module_exit(minimal_exit);

