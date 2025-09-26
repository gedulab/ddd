#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>


// 定义设备名称
#define DEVICE_NAME "led_indicator"


// 定义 GPIO 引脚
#define LED_GPIO 123 // 你需要修改为你实际使用的 GPIO 引脚


// 定义字符设备结构体
struct led_dev {
    struct cdev cdev;
    int led_state; // 0: 关闭, 1: 开启
};


// 声明全局设备结构体
static struct led_dev *led_device;


// 定义设备类和设备号
static struct class *led_class;
static dev_t dev_num;


// 文件操作函数
static int led_open(struct inode *inode, struct file *file)
{
    struct led_dev *dev = container_of(inode->i_cdev, struct led_dev, cdev);
    file->private_data = dev;
    printk(KERN_INFO "LED driver opened\n");
    return 0;
}


static int led_release(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "LED driver closed\n");
    return 0;
}


static ssize_t led_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
    struct led_dev *dev = (struct led_dev *)file->private_data;
    char state_str[2];
    sprintf(state_str, "%d", dev->led_state);
    ssize_t len = strlen(state_str);


    if (count < len)
        return -EINVAL;


    if (copy_to_user(buf, state_str, len))
        return -EFAULT;


    return len;
}


static ssize_t led_write(struct file *file, const char __user *buf, size_t count, loff_t *offset)
{
    struct led_dev *dev = (struct led_dev *)file->private_data;
    char kbuf[2];


    if (count > 1) {
    	printk("too long input %ld\n", count);
	return -EINVAL;
    }

    if (copy_from_user(kbuf, buf, count))
        return -EFAULT;


    if (kbuf[0] == '1') {
        gpio_set_value(LED_GPIO, 1);
        dev->led_state = 1;
        printk(KERN_INFO "LED ON\n");
    } else if (kbuf[0] == '0') {
        gpio_set_value(LED_GPIO, 0);
        dev->led_state = 0;
        printk(KERN_INFO "LED OFF\n");
    } else {
        printk(KERN_INFO "Invalid command\n");
        return -EINVAL;
    }


    return count;
}


// 文件操作结构体
static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = led_open,
    .release = led_release,
    .read = led_read,
    .write = led_write,
};


// 模块初始化函数
static int __init led_init(void)
{
    int ret;


    // 申请设备号
    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        printk(KERN_ERR "Failed to allocate device number\n");
        return ret;
    }


    // 创建设备类
    led_class = class_create(THIS_MODULE, DEVICE_NAME);
    if (led_class == NULL) {
        printk(KERN_ERR "Failed to create device class\n");
        unregister_chrdev_region(dev_num, 1);
        return -ENOMEM;
    }


    // 分配设备结构体
    // led_device = devm_kzalloc(NULL, sizeof(struct led_dev), GFP_KERNEL);
    led_device = device_create(led_class, NULL, dev_num, NULL, DEVICE_NAME);
    if (!led_device) {
        class_destroy(led_class);
        unregister_chrdev_region(dev_num, 1);
        printk(KERN_ERR "Failed to allocate led_device\n");
        return -ENOMEM;
    }


    // 初始化字符设备
    cdev_init(&led_device->cdev, &fops);
    led_device->cdev.owner = THIS_MODULE;
    ret = cdev_add(&led_device->cdev, dev_num, 1);
    if (ret < 0) {
        printk(KERN_ERR "Failed to add cdev\n");
        class_destroy(led_class);
        unregister_chrdev_region(dev_num, 1);
        return ret;
    }


    // 创建设备节点
    device_create(led_class, NULL, dev_num, NULL, DEVICE_NAME);


    // 申请 GPIO 引脚
    ret = gpio_request(LED_GPIO, "led_gpio");
    if (ret < 0) {
        printk(KERN_ERR "Failed to request GPIO %d\n", LED_GPIO);
        cdev_del(&led_device->cdev);
        class_destroy(led_class);
        unregister_chrdev_region(dev_num, 1);
        return ret;
    }


    // 设置 GPIO 为输出
    ret = gpio_direction_output(LED_GPIO, 0); // 初始状态为关闭
    if (ret < 0) {
        printk(KERN_ERR "Failed to set GPIO direction\n");
        gpio_free(LED_GPIO);
        cdev_del(&led_device->cdev);
        class_destroy(led_class);
        unregister_chrdev_region(dev_num, 1);
        return ret;
    }


    led_device->led_state = 0; // 初始状态


    printk(KERN_INFO "LED driver initialized\n");
    return 0;
}


// 模块退出函数
static void __exit led_exit(void)
{
    // 释放 GPIO 引脚
    gpio_free(LED_GPIO);


    // 删除设备节点
    device_destroy(led_class, dev_num);


    // 删除字符设备
    cdev_del(&led_device->cdev);


    // 销毁设备类
    class_destroy(led_class);


    // 释放设备号
    unregister_chrdev_region(dev_num, 1);


    printk(KERN_INFO "LED driver exited\n");
}


module_init(led_init);
module_exit(led_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("LED Indicator Driver");
