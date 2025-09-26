#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/export-internal.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif


static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0x92997ed8, "_printk" },
	{ 0xfe990052, "gpio_free" },
	{ 0x8b2c9a79, "cdev_del" },
	{ 0x2db97ef4, "device_destroy" },
	{ 0xc528274, "class_destroy" },
	{ 0x6091b333, "unregister_chrdev_region" },
	{ 0x324b8c32, "gpio_to_desc" },
	{ 0x46142c09, "gpiod_set_raw_value" },
	{ 0xe3ec2f2b, "alloc_chrdev_region" },
	{ 0xdd94b9bf, "__class_create" },
	{ 0xc05321a2, "device_create" },
	{ 0x41bd437d, "cdev_init" },
	{ 0xec9984f4, "cdev_add" },
	{ 0x47229b5c, "gpio_request" },
	{ 0x4f7b3ffe, "gpiod_direction_output_raw" },
	{ 0x8da6585d, "__stack_chk_fail" },
	{ 0x12a4e128, "__arch_copy_from_user" },
	{ 0x8c8569cb, "kstrtoint" },
	{ 0x5b3ce126, "gpiod_get_raw_value" },
	{ 0x656e4a6e, "snprintf" },
	{ 0x98cf60b3, "strlen" },
	{ 0x6cbbfc54, "__arch_copy_to_user" },
	{ 0x62371f6e, "module_layout" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "2200CDA1111B6BE9A92A498");
