#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#define DEVICE_NAME "threaded_irq_example"
static int major_num;
static struct cdev my_cdev;
static struct class *my_class;

// 假设这是您的设备结构
struct my_device_data {
	int irq;
	// 您的设备特定数据
};
static struct my_device_data *my_device;

// 您需要根据您的硬件修改此中断号
static int irq_number = 10;
module_param(irq_number, int, S_IRUGO);
MODULE_PARM_DESC(irq_number, "The IRQ number to request");

// 中断处理函数 (上半部)
static irqreturn_t my_irq_handler(int irq, void *dev_id)
{
	printk(KERN_INFO "%s: Interrupt %d received (top half)\n", DEVICE_NAME, irq);
	// 在这里进行快速的硬件状态读取和确认
	// 返回 IRQ_WAKE_THREAD 通知中断线程执行下半部
	return IRQ_WAKE_THREAD;
}

// 中断线程函数 (下半部)
static irqreturn_t my_irq_thread_handler(int irq, void *dev_id)
{
	struct my_device_data *dev = (struct my_device_data *)dev_id;

	// 在这里执行更耗时的中断处理任务
	printk(KERN_INFO "%s: Interrupt %d processing in thread (bottom half)\n",
	       DEVICE_NAME, irq);
	printk(KERN_INFO "%s: Received interrupt on IRQ %d\n", DEVICE_NAME, dev->irq);

	// 在这里处理您的设备相关操作
	// 例如，读取数据，更新状态，通知用户空间等

	return IRQ_HANDLED;
}

// 文件操作函数 (用于简单的用户空间交互)
static int device_open(struct inode *inode, struct file *file)
{
	printk(KERN_INFO "%s: Device opened\n", DEVICE_NAME);
	return 0;
}

static int device_release(struct inode *inode, struct file *file)
{
	printk(KERN_INFO "%s: Device released\n", DEVICE_NAME);
	return 0;
}

static ssize_t device_read(struct file *filp, char __user *buffer,
			   size_t length, loff_t *offset)
{
	char msg[] = "Hello from interrupt thread!\n";
	size_t msg_len = strlen(msg);

	if (*offset >= msg_len)
		return 0;
	if (length > msg_len - *offset)
		length = msg_len - *offset;
	if (copy_to_user(buffer, msg + *offset, length))
		return -EFAULT;
	*offset += length;
	return length;
}

static const struct file_operations fops = {
	.open    = device_open,
	.release = device_release,
	.read    = device_read,
};

static int __init threaded_irq_init(void)
{
	int result;
	dev_t dev = 0;

	result = alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME);
	if (result < 0) {
		printk(KERN_ERR "%s: Can't allocate major number\n", DEVICE_NAME);
		return result;
	}
	major_num = MAJOR(dev);
	printk(KERN_INFO "%s: Major number allocated is %d\n", DEVICE_NAME, major_num);

	my_class = class_create(DEVICE_NAME);
	if (IS_ERR(my_class)) {
		unregister_chrdev_region(dev, 1);
		printk(KERN_ERR "%s: Can't create device class\n", DEVICE_NAME);
		return PTR_ERR(my_class);
	}

	cdev_init(&my_cdev, &fops);
	result = cdev_add(&my_cdev, MKDEV(major_num, 0), 1);
	if (result < 0) {
		class_destroy(my_class);
		unregister_chrdev_region(dev, 1);
		printk(KERN_ERR "%s: Can't add cdev\n", DEVICE_NAME);
		return result;
	}

	device_create(my_class, NULL, MKDEV(major_num, 0), NULL, DEVICE_NAME);
	printk(KERN_INFO "%s: Device node created (/dev/%s)\n", DEVICE_NAME,
	       DEVICE_NAME);

	// 分配设备特定数据
	my_device = kmalloc(sizeof(struct my_device_data), GFP_KERNEL);
	if (!my_device) {
		device_destroy(my_class, MKDEV(major_num, 0));
		cdev_del(&my_cdev);
		class_destroy(my_class);
		unregister_chrdev_region(dev, 1);
		printk(KERN_ERR "%s: Failed to allocate device structure\n", DEVICE_NAME);
		return -ENOMEM;
	}
	my_device->irq = irq_number;

	// 正确调用 request_threaded_irq()
	result = request_threaded_irq(irq_number,        // 中断号
				      my_irq_handler,    // 中断处理函数 (上半部)
				      my_irq_thread_handler, // 中断线程函数 (下半部)
//				      IRQF_ONESHOT,       // 中断标志 (根据您的需求选择)
				      IRQF_SHARED,       // 中断标志 (根据您的需求选择)
				      DEVICE_NAME,       // 设备名称 (用于 /proc/interrupts)
				      my_device);        // 传递给处理函数的设备特定数据

	if (result) {
		kfree(my_device);
		device_destroy(my_class, MKDEV(major_num, 0));
		cdev_del(&my_cdev);
		class_destroy(my_class);
		unregister_chrdev_region(dev, 1);
		printk(KERN_ERR "%s: Failed to request IRQ %d\n", DEVICE_NAME, irq_number);
		return result;
	}

	printk(KERN_INFO "%s: Successfully registered IRQ %d with threaded handler\n",
	       DEVICE_NAME, irq_number);

	return 0;
}

static void __exit threaded_irq_exit(void)
{
	free_irq(irq_number, my_device);
	kfree(my_device);
	device_destroy(my_class, MKDEV(major_num, 0));
	cdev_del(&my_cdev);
	class_destroy(my_class);
	unregister_chrdev_region(MKDEV(major_num, 0), 1);
	printk(KERN_INFO "%s: Module unloaded\n", DEVICE_NAME);
}

module_init(threaded_irq_init);
module_exit(threaded_irq_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Example kernel module demonstrating request_threaded_irq");
