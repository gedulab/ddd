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
	{ 0x6c4b6684, "reset_control_assert" },
	{ 0xb6e6d99d, "clk_disable" },
	{ 0xb077e70a, "clk_unprepare" },
	{ 0xc1514a3b, "free_irq" },
	{ 0x40f0683e, "reset_control_put" },
	{ 0x2e1ca751, "clk_put" },
	{ 0xedc03953, "iounmap" },
	{ 0x8b2c9a79, "cdev_del" },
	{ 0x2db97ef4, "device_destroy" },
	{ 0xc528274, "class_destroy" },
	{ 0x6091b333, "unregister_chrdev_region" },
	{ 0x37a0cba, "kfree" },
	{ 0x92ebaf5b, "kmalloc_caches" },
	{ 0xccdbf6dd, "kmalloc_trace" },
	{ 0xa2a635bc, "__init_waitqueue_head" },
	{ 0xe3ec2f2b, "alloc_chrdev_region" },
	{ 0xdd94b9bf, "__class_create" },
	{ 0xc05321a2, "device_create" },
	{ 0x41bd437d, "cdev_init" },
	{ 0xec9984f4, "cdev_add" },
	{ 0xaf56600a, "arm64_use_ng_mappings" },
	{ 0x40863ba1, "ioremap_prot" },
	{ 0x12a4e128, "__arch_copy_from_user" },
	{ 0xdcb764ad, "memset" },
	{ 0x8da6585d, "__stack_chk_fail" },
	{ 0x6cbbfc54, "__arch_copy_to_user" },
	{ 0x3c3ff9fd, "sprintf" },
	{ 0x98cf60b3, "strlen" },
	{ 0x62371f6e, "module_layout" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "531641FFD6BBB994BB75FA8");
