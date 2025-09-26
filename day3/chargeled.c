#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>


#define DEVICE_NAME "led_control"
#define GPIO_PIN 123  // 修改为你实际使用的GPIO引脚

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("A simple LED control driver using GPIO");


static int major_number;
static struct class *led_class = NULL;
static struct device *led_device = NULL;
static struct cdev led_cdev;


// 定义ioctl命令
#define LED_ON  _IO('K', 1)
#define LED_OFF _IO('K', 0)


// 函数声明
static int led_open(struct inode *inode, struct file *file);
static int led_release(struct inode *inode, struct file *file);
static long led_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static ssize_t led_read(struct file *file, char __user *buf, size_t count, loff_t *ppos);
static ssize_t led_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos);

// 文件操作结构体
static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = led_open,
    .read = led_read,
    .write = led_write,
    .release = led_release,
    .unlocked_ioctl = led_ioctl,
};


// 驱动打开函数
static int led_open(struct inode *inode, struct file *file) {
    printk(KERN_INFO "LED Driver: open()\n");
    return 0;
}


// 驱动释放函数
static int led_release(struct inode *inode, struct file *file) {
    printk(KERN_INFO "LED Driver: release()\n");
    return 0;
}


// ioctl函数
static long led_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    switch (cmd) {
        case LED_ON:
            gpio_set_value(GPIO_PIN, 1);  // 点亮LED
            printk(KERN_INFO "LED: ON\n");
            break;
        case LED_OFF:
            gpio_set_value(GPIO_PIN, 0);  // 关闭LED
            printk(KERN_INFO "LED: OFF\n");
            break;
        default:
            return -ENOTTY; // ioctl 命令不正确
    }
    return 0;
}
// Read回调函数，用于从设备读取数据
static ssize_t led_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
    int led_state;
    char kernel_buf[2]; // 存储LED状态的缓冲区，需要考虑字符串结束符
    size_t len;


    // 获取GPIO的状态
    led_state = gpio_get_value(GPIO_PIN);


    // 将LED状态转换为字符
    snprintf(kernel_buf, sizeof(kernel_buf), "%d", led_state);
    len = strlen(kernel_buf);



    // 检查用户空间缓冲区大小是否足够
    if (count < len) {
        return -ENOSPC; // 用户空间缓冲区太小
    }


    // 将数据复制到用户空间
    if (copy_to_user(buf, kernel_buf, len)) {
        return -EFAULT; // 复制数据失败
    }


    printk(KERN_INFO "LED Driver: Read %zu bytes\n", len);
    return len; // 返回实际读取的字节数
}



// Write回调函数，用于向设备写入数据
static ssize_t led_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
    char kernel_buf[20];  // 用于存储用户空间写入的数据，只取第一个字符
    int led_value;


    // 检查写入的字节数是否大于缓冲区大小
    if (count > sizeof(kernel_buf) - 1) {
        printk(KERN_WARNING "LED Driver: Write - Input too long %d\n", count);
        return -EINVAL; // 输入太长
    }


    // 将用户空间的数据复制到内核空间
    if (copy_from_user(kernel_buf, buf, count)) {
        return -EFAULT; // 复制数据失败
    }
    kernel_buf[count] = '\0'; // 确保字符串以null结尾


    // 将字符转换为整数
    if (kstrtoint(kernel_buf, 10, &led_value) != 0) {
        printk(KERN_WARNING "LED Driver: Write - Invalid input\n");
        return -EINVAL; // 无效的输入
    }


    // 根据写入的值控制LED
    if (led_value == 0) {
        gpio_set_value(GPIO_PIN, 0); // 关闭LED
        printk(KERN_INFO "LED: OFF (Write)\n");
    } else if (led_value == 1) {
        gpio_set_value(GPIO_PIN, 1); // 打开LED
        printk(KERN_INFO "LED: ON (Write)\n");
    } else {
        printk(KERN_WARNING "LED Driver: Write - Invalid LED value\n");
        return -EINVAL; // 无效的LED值
    }


    printk(KERN_INFO "LED Driver: Wrote %zu bytes\n", count);
    return count; // 返回实际写入的字节数
}

// 模块初始化函数
static int __init led_driver_init(void) {
    int ret = 0;


    printk(KERN_INFO "LED Driver: init()\n");


    // 1. 动态分配主设备号
    ret = alloc_chrdev_region(&major_number, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        printk(KERN_ALERT "Failed to allocate major number\n");
        return ret;
    }
    printk(KERN_INFO "Allocated major number %d\n", MAJOR(major_number));


    // 2. 创建设备类
    led_class = class_create(THIS_MODULE, "led_class");
    if (IS_ERR(led_class)) {
        unregister_chrdev_region(major_number, 1);
        printk(KERN_ALERT "Failed to create class\n");
        return PTR_ERR(led_class);
    }


    // 3. 创建设备
    led_device = device_create(led_class, NULL, major_number, NULL, DEVICE_NAME);
    if (IS_ERR(led_device)) {
        class_destroy(led_class);
        unregister_chrdev_region(major_number, 1);
        printk(KERN_ALERT "Failed to create device\n");
        return PTR_ERR(led_device);
    }


    // 4. 初始化cdev结构体
    cdev_init(&led_cdev, &fops);
    led_cdev.owner = THIS_MODULE;


    // 5. 注册字符设备
    ret = cdev_add(&led_cdev, major_number, 1);
    if (ret < 0) {
        device_destroy(led_class, major_number);
        class_destroy(led_class);
        unregister_chrdev_region(major_number, 1);
        printk(KERN_ALERT "Failed to add cdev\n");
        return ret;
    }


    // 6.  GPIO 初始化
    if (!gpio_is_valid(GPIO_PIN)) {
        printk(KERN_ALERT "Invalid GPIO pin\n");
        cdev_del(&led_cdev);
        device_destroy(led_class, major_number);
        class_destroy(led_class);
        unregister_chrdev_region(major_number, 1);
        return -ENODEV;
    }


    // 7. 请求 GPIO
    ret = gpio_request(GPIO_PIN, "LED");
    if (ret < 0) {
        printk(KERN_ALERT "Failed to request GPIO pin\n");
        cdev_del(&led_cdev);
        device_destroy(led_class, major_number);
        class_destroy(led_class);
        unregister_chrdev_region(major_number, 1);
        return ret;
    }


    // 8. 设置 GPIO 为输出
    ret = gpio_direction_output(GPIO_PIN, 0); // 初始状态为关闭
    if (ret < 0) {
        printk(KERN_ALERT "Failed to set GPIO direction\n");
        gpio_free(GPIO_PIN);
        cdev_del(&led_cdev);
        device_destroy(led_class, major_number);
        class_destroy(led_class);
        unregister_chrdev_region(major_number, 1);
        return ret;
    }


    printk(KERN_INFO "LED Driver initialized successfully\n");
    return 0;
}


// 模块退出函数
static void __exit led_driver_exit(void) {
    printk(KERN_INFO "LED Driver: exit()\n");


    // 1. 释放 GPIO
    gpio_free(GPIO_PIN);


    // 2. 删除字符设备
    cdev_del(&led_cdev);


    // 3. 销毁设备
    device_destroy(led_class, major_number);


    // 4. 销毁设备类
    class_destroy(led_class);


    // 5. 释放设备号
    unregister_chrdev_region(major_number, 1);


    printk(KERN_INFO "LED Driver exited successfully\n");
}


module_init(led_driver_init);
module_exit(led_driver_exit);
