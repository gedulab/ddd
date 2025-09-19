// SPDX-License-Identifier: GPL-2.0-only
/*
 * Character device driver for Rockchip RK3588 Temperature Sensor ADC (TSADC)
 *
 * This driver implements a character device interface to the TSADC hardware.
 * It bypasses the platform driver framework and manually handles resource
 * initialization and device file creation.
 *
 * Device file: /dev/rk3588_tsadc
 * Usage:
 *   - Read temperature: read() from the device file.
 *   - Control operations: use ioctl() with defined commands.
 *
 * Copyright (c) 2023 Your Name <your.email@example.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/poll.h>
#include <linux/wait.h>

/* --- Hardcoded Hardware Definitions for RK3588 --- */
/* !! IMPORTANT: These values MUST be verified for your specific board/kernel !! */
#define TSADC_PHYS_BASE       0xfec00000
#define TSADC_PHYS_SIZE       0x100
#define TSADC_IRQ             429 // GIC_SPI 162

/* Clock and reset names might vary, these are common for Rockchip */
#define TSADC_CLK_NAME        "tsadc"
#define TSADC_RST_APB_NAME    "tsadc-apb"

/* --- Register Offsets (from TRM) --- */
#define TSADC_AUTO_CON        0x0004
#define TSADC_INT_EN          0x0008
#define TSADC_INT_PD          0x000c
#define TSADC_DATA(chn)       (0x002C + (chn) * 4)
#define TSADC_COMP_INT(chn)   (0x0030 + (chn) * 4)
#define TSADC_HIGHT_INT_DEBOUNCE 0x0060
#define TSADC_AUTO_PERIOD     0x0068

#define TSADC_AUTO_CON_SRC_EN(chn) BIT(4 + (chn))
#define TSADC_AUTO_CON_START      BIT(0)
#define TSADC_INT_SRC_EN(chn)     BIT(chn)
#define TSADC_INT_SRC_MASK(chn)   BIT(chn)

#define TSADC_DATA_MASK       0xfff
#define TSADC_MAX_CHANNELS    8

#define TSADC_SAMPLE_RATE     (20 * 1000) /* 20 kHz */
#define TSADC_SAMPLE_CYCLE    (4800 / TSADC_SAMPLE_RATE)

/* --- Character Device Definitions --- */
#define DEVICE_NAME "rk3588_tsadc"
#define CLASS_NAME  "tsadc_class"

/* --- IOCTL Commands --- */
#define TSADC_MAGIC 'T'
#define TSADC_SET_CHANNEL _IOW(TSADC_MAGIC, 1, int)
#define TSADC_GET_CHANNEL _IOR(TSADC_MAGIC, 2, int)
#define TSADC_SET_INT_THRESHOLD _IOW(TSADC_MAGIC, 3, int) // temp in Celsius

/* --- Per-device structure --- */
struct tsadc_dev {
	dev_t dev_num;
	struct class *dev_class;
	struct cdev cdev;
	struct device *device;

	void __iomem *regs;
	struct clk *clk;
	struct reset_control *rst;
	int irq;

	int current_channel;
	int int_threshold_temp; // in Celsius
	wait_queue_head_t waitq;
	bool irq_fired;
};

static struct tsadc_dev *tsadc_device;

/* --- Temperature Conversion Table (from TRM) --- */
struct tsadc_table {
	int temp;
	int code;
};
#ifdef TRY_AI_BUG
static const struct tsadc_table rk3588_code_table[] = {
	{ -40, 3800 }, { -30, 3630 }, { -20, 3440 }, { -10, 3240 },
	{ 0, 3020 }, { 10, 2790 }, { 20, 2550 }, { 30, 2290 },
	{ 40, 2020 }, { 50, 1730 }, { 60, 1420 }, { 70, 1090 },
	{ 80, 740 }, { 90, 360 }, { 100, -50 }, { 110, -530 }
};
#else
static const struct tsadc_table rk3588_code_table[] = {
	{ 125, 395 },
	{ 85, 350 },
	{ 25, 285 }, 
	{ -40, 215 }, 
};
#endif

/* --- Helper Functions --- */
static int code_to_temp(int code, int *temp)
{
	/* (Same implementation as the platform driver version) */
	unsigned int low = 1, high = ARRAY_SIZE(rk3588_code_table) - 1, mid;
	u32 num, den;

	if (code > rk3588_code_table[0].code) return -EAGAIN;
	if (code < rk3588_code_table[high].code) return -EINVAL;

	while (low <= high) {
		mid = (low + high) / 2;
		if (code >= rk3588_code_table[mid].code && code <= rk3588_code_table[mid - 1].code) 
			break;
		else if (code > rk3588_code_table[mid].code) 
			high = mid - 1;
		else 
			low = mid + 1;
	}
	printk("%s found %d\n", __func__, mid);

	num = rk3588_code_table[mid - 1].temp - rk3588_code_table[mid].temp;
	den = rk3588_code_table[mid - 1].code - rk3588_code_table[mid].code;
	*temp =  ((code - rk3588_code_table[mid].code) * num) / den + rk3588_code_table[mid].temp;
	return 0;
}

static int temp_to_code(int temp)
{
	/* (A simple reverse lookup, could be optimized) */
	int i;
	for (i = 0; i < ARRAY_SIZE(rk3588_code_table) - 1; i++) {
		if (temp <= rk3588_code_table[i].temp && temp > rk3588_code_table[i+1].temp) {
			// Simple linear interpolation for threshold
			return rk3588_code_table[i].code;
		}
	}
	return rk3588_code_table[0].code; // Default to coldest
}

/* --- File Operations --- */
static int my_open(struct inode *inode, struct file *file)
{
	struct tsadc_dev *dev = container_of(inode->i_cdev, struct tsadc_dev, cdev);
	file->private_data = dev;
	printk(KERN_INFO DEVICE_NAME ": Device opened %pK\n", dev);
	return 0;
}

static int my_release(struct inode *inode, struct file *file)
{
	printk(KERN_INFO DEVICE_NAME ": Device closed\n");
	return 0;
}

static ssize_t my_read(struct file *file, char __user *buf, size_t len, loff_t *off)
{
	struct tsadc_dev *dev = file->private_data;
	int raw_val, temp_c, ret;
	char kbuf[16];

	if (*off > 0 || len < 12) return 0; // Signal EOF

	raw_val = readl_relaxed(dev->regs + TSADC_DATA(dev->current_channel)) & TSADC_DATA_MASK;
	ret = code_to_temp(raw_val, &temp_c);
	if (ret) {
		if (ret == -EAGAIN) temp_c = -40; // Sensor not ready
		else return ret; // Critical error
	}

	sprintf(kbuf, "%d\n", temp_c);
	printk("channel %d code %d temperature %d\n", dev->current_channel, raw_val, temp_c);

	if (copy_to_user(buf, kbuf, strlen(kbuf))) return -EFAULT;
	
	*off += strlen(kbuf);
	return strlen(kbuf);
}

static long my_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct tsadc_dev *dev = file->private_data;
	int channel, temp;
	
	switch (cmd) {
	case TSADC_SET_CHANNEL:
		if (copy_from_user(&channel, (int __user *)arg, sizeof(int)))
			return -EFAULT;
		if (channel < 0 || channel >= TSADC_MAX_CHANNELS)
			return -EINVAL;
		dev->current_channel = channel;
		printk(KERN_INFO DEVICE_NAME ": Channel set to %d\n", channel);
		break;

	case TSADC_GET_CHANNEL:
		if (copy_to_user((int __user *)arg, &dev->current_channel, sizeof(int)))
			return -EFAULT;
		break;

	case TSADC_SET_INT_THRESHOLD:
		if (copy_from_user(&temp, (int __user *)arg, sizeof(int)))
			return -EFAULT;
		dev->int_threshold_temp = temp;
		writel_relaxed(temp_to_code(temp), dev->regs + TSADC_COMP_INT(dev->current_channel));
		writel_relaxed(TSADC_INT_SRC_EN(dev->current_channel), dev->regs + TSADC_INT_EN);
		printk(KERN_INFO DEVICE_NAME ": Interrupt threshold set to %d C\n", temp);
		break;

	default:
		return -ENOTTY;
	}
	return 0;
}

static unsigned int my_poll(struct file *file, struct poll_table_struct *wait)
{
	struct tsadc_dev *dev = file->private_data;
	unsigned int mask = 0;

	poll_wait(file, &dev->waitq, wait);

	if (dev->irq_fired) {
		mask |= POLLIN | POLLRDNORM; // Data is available to read
		dev->irq_fired = false; // Reset flag
	}
	return mask;
}

static const struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = my_open,
	.release = my_release,
	.read = my_read,
	.unlocked_ioctl = my_ioctl,
	.poll = my_poll,
};

/* --- Interrupt Handler --- */
static irqreturn_t tsadc_isr(int irq, void *dev_id)
{
	struct tsadc_dev *dev = (struct tsadc_dev *)dev_id;
	u32 val;

	val = readl_relaxed(dev->regs + TSADC_INT_PD);
	writel_relaxed(val, dev->regs + TSADC_INT_PD); // Clear interrupt

	if (val & TSADC_INT_SRC_MASK(dev->current_channel)) {
		printk(KERN_INFO DEVICE_NAME ": Temperature threshold crossed for channel %d!\n", dev->current_channel);
		dev->irq_fired = true;
		wake_up_interruptible(&dev->waitq);
	}
	return IRQ_HANDLED;
}

/* --- Module Init/Exit --- */
static int __init tsadc_char_init(void)
{
	int ret;

	/* 1. Allocate and initialize the device structure */
	tsadc_device = kzalloc(sizeof(*tsadc_device), GFP_KERNEL);
	if (!tsadc_device) return -ENOMEM;

	init_waitqueue_head(&tsadc_device->waitq);
	tsadc_device->current_channel = 0; // Default to channel 0
	tsadc_device->int_threshold_temp = 85; // Default threshold

	/* 2. Allocate character device numbers */
	ret = alloc_chrdev_region(&tsadc_device->dev_num, 0, 1, DEVICE_NAME);
	if (ret < 0) {
		printk(KERN_ERR DEVICE_NAME ": Failed to allocate device number\n");
		goto free_dev;
	}

	/* 3. Create device class */
	tsadc_device->dev_class = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(tsadc_device->dev_class)) {
		printk(KERN_ERR DEVICE_NAME ": Failed to create device class\n");
		ret = PTR_ERR(tsadc_device->dev_class);
		goto unreg_chrdev;
	}

	/* 4. Create device file in /dev */
	tsadc_device->device = device_create(tsadc_device->dev_class, NULL, tsadc_device->dev_num, NULL, DEVICE_NAME);
	if (IS_ERR(tsadc_device->device)) {
		printk(KERN_ERR DEVICE_NAME ": Failed to create device file\n");
		ret = PTR_ERR(tsadc_device->device);
		goto destroy_class;
	}

	/* 5. Initialize and add cdev */
	cdev_init(&tsadc_device->cdev, &fops);
	tsadc_device->cdev.owner = THIS_MODULE;
	ret = cdev_add(&tsadc_device->cdev, tsadc_device->dev_num, 1);
	if (ret < 0) {
		printk(KERN_ERR DEVICE_NAME ": Failed to add cdev\n");
		goto destroy_device;
	}
	
	/* 6. Manually map hardware resources */
	/* !! This is the main difference from platform driver !! */
	tsadc_device->regs = ioremap(TSADC_PHYS_BASE, TSADC_PHYS_SIZE);
	if (!tsadc_device->regs) {
		printk(KERN_ERR DEVICE_NAME ": Failed to map registers\n");
		ret = -ENOMEM;
		goto del_cdev;
	}
#ifdef TRY_AI_BUG
	tsadc_device->clk = clk_get(NULL, TSADC_CLK_NAME);
	if (IS_ERR(tsadc_device->clk)) {
		printk(KERN_ERR DEVICE_NAME ": Failed to get clock\n");
		ret = PTR_ERR(tsadc_device->clk);
		goto unmap_regs;
	}

	tsadc_device->rst = reset_control_get(NULL, TSADC_RST_APB_NAME);
	if (IS_ERR(tsadc_device->rst)) {
		printk(KERN_ERR DEVICE_NAME ": Failed to get reset control\n");
		ret = PTR_ERR(tsadc_device->rst);
		goto put_clk;
	}

	tsadc_device->irq = TSADC_IRQ;
	ret = request_irq(tsadc_device->irq, tsadc_isr, 0, DEVICE_NAME, tsadc_device);
	if (ret) {
		printk(KERN_ERR DEVICE_NAME ": Failed to request IRQ\n");
		goto put_reset;
	}
#endif //

	/* 7. Initialize Hardware */
#ifdef TRY_AI_BUG	
	ret = clk_prepare_enable(tsadc_device->clk);
	if (ret) {
		printk(KERN_ERR DEVICE_NAME ": Failed to enable clock\n");
		goto free_irq;
	}
	reset_control_deassert(tsadc_device->rst);

	writel_relaxed(TSADC_SAMPLE_CYCLE, tsadc_device->regs + TSADC_AUTO_PERIOD);
	writel_relaxed(0, tsadc_device->regs + TSADC_HIGHT_INT_DEBOUNCE);
#endif	
	writel_relaxed(TSADC_AUTO_CON_SRC_EN(tsadc_device->current_channel), tsadc_device->regs + TSADC_AUTO_CON);
	writel_relaxed(TSADC_AUTO_CON_START | TSADC_AUTO_CON_SRC_EN(tsadc_device->current_channel),
		       tsadc_device->regs + TSADC_AUTO_CON);
	
	printk(KERN_INFO DEVICE_NAME ": Driver loaded. Major: %d, Minor: %d\n",
	       MAJOR(tsadc_device->dev_num), MINOR(tsadc_device->dev_num));
	return 0;

/* Error handling path */
free_irq:
	free_irq(tsadc_device->irq, tsadc_device);
put_reset:
	reset_control_put(tsadc_device->rst);
put_clk:
	clk_put(tsadc_device->clk);
unmap_regs:
	iounmap(tsadc_device->regs);
del_cdev:
	cdev_del(&tsadc_device->cdev);
destroy_device:
	device_destroy(tsadc_device->dev_class, tsadc_device->dev_num);
destroy_class:
	class_destroy(tsadc_device->dev_class);
unreg_chrdev:
	unregister_chrdev_region(tsadc_device->dev_num, 1);
free_dev:
	kfree(tsadc_device);
	return ret;
}

static void __exit tsadc_char_exit(void)
{
	/* 1. Shutdown Hardware */
	writel_relaxed(0, tsadc_device->regs + TSADC_AUTO_CON);
	writel_relaxed(0, tsadc_device->regs + TSADC_INT_EN);
	reset_control_assert(tsadc_device->rst);
	clk_disable_unprepare(tsadc_device->clk);
	
	/* 2. Free Resources */
	free_irq(tsadc_device->irq, tsadc_device);
	reset_control_put(tsadc_device->rst);
	clk_put(tsadc_device->clk);
	iounmap(tsadc_device->regs);

	/* 3. Destroy Character Device */
	cdev_del(&tsadc_device->cdev);
	device_destroy(tsadc_device->dev_class, tsadc_device->dev_num);
	class_destroy(tsadc_device->dev_class);
	unregister_chrdev_region(tsadc_device->dev_num, 1);
	
	kfree(tsadc_device);
	printk(KERN_INFO DEVICE_NAME ": Driver unloaded\n");
}

module_init(tsadc_char_init);
module_exit(tsadc_char_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Raymond Zhang <yinkui.zhang@nanocode.cn>");
MODULE_DESCRIPTION("Character device driver for RK3588 TSADC by GSL");

