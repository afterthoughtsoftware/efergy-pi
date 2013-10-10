#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/bitrev.h>
#include <linux/time.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/kfifo.h>

#define DEVICE_NAME "device"
#define CLASS_NAME "efergy"
#define EFERGY_FIFO_SIZE  128

static struct timeval falling;
static struct timeval rising;
static u8 bit;
static u8 word;
static int irq_number;
static int gpio_number;

static int efergy_major;
static struct device* efergy_device;
static struct class* efergy_class;
static DEFINE_MUTEX(efergy_device_mutex);
static DECLARE_KFIFO(efergy_msg_fifo, u8, EFERGY_FIFO_SIZE);

static int efergy_device_open(struct inode* inode, struct file* filp)
{
	if ( ((filp->f_flags & O_ACCMODE) == O_WRONLY)
	     || ((filp->f_flags & O_ACCMODE) == O_RDWR) ) {
		pr_warn("write access is prohibited\n");
		return -EACCES;
	}

	if (!mutex_trylock(&efergy_device_mutex)) {
		pr_warn("another process is accessing the device\n");
		return -EBUSY;
	}

	return 0;
}

static int efergy_device_close(struct inode* inode, struct file* filp)
{
	mutex_unlock(&efergy_device_mutex);

	return 0;
}

static ssize_t efergy_device_read(struct file* filp, char __user *buffer, size_t length, loff_t* offset)
{
	int retval;
	unsigned int copied;

	if (kfifo_is_empty(&efergy_msg_fifo))
		return 0;

	retval = kfifo_to_user(&efergy_msg_fifo, buffer, length, &copied);

	return retval ? retval : copied;
}

static struct file_operations fops = {
	.read = efergy_device_read,
	.open = efergy_device_open,
	.release = efergy_device_close
};

static irqreturn_t efergy_edge(int irq, void* dev_id)
{
	/* This data runs at ~4800bps so ignore anything > 250us. Each bit
	 * starts and ends with a falling edge, if the signal was high for >50%
	 * of the cycle time the bit is a 1, or a zero otherwise */
	struct timeval now;
	s64 us_cycle, us_high;

	if (gpio_get_value(gpio_number) == 1) {
		do_gettimeofday(&rising);
	} else {
		do_gettimeofday(&now);

		us_cycle = timeval_to_ns(&now) - timeval_to_ns(&falling);
		if (us_cycle < 250 * 1000) {
			us_high = timeval_to_ns(&now) - timeval_to_ns(&rising);
			if (us_high > us_cycle / 2)
				word |= 1U << bit++;
			else
				++bit;

			if (bit == 8) {
				word = bitrev8(word);
				kfifo_put(&efergy_msg_fifo, &word);
				word = bit = 0;
			}
		} else {
			word = bit = 0;
		}

		do_gettimeofday(&falling);
	}
	return(IRQ_HANDLED);
}

static int __init efergy_init(void)
{
	int retval;

	// TODO: GPIO number as module parameter?
	gpio_number = 25;

	retval = gpio_request_one(gpio_number, GPIOF_DIR_IN, "efergy_rfdata");
	if (retval) {
		pr_err("Can't request gpio %d\n", gpio_number);
		return retval;
	}

	irq_number = gpio_to_irq(gpio_number);
	if (request_irq(irq_number, efergy_edge, IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING, "efergy_edge", NULL) ) {
		pr_err("GPIO_RISING: trouble requesting IRQ %d (rising)",irq_number);
		return(-EIO);
	}

	efergy_major = register_chrdev(0, DEVICE_NAME, &fops);
	if (efergy_major < 0) {
		pr_err("failed to register device: error %d\n", efergy_major);
		retval =efergy_major;
		goto failed_chrdevreg;
	}

	efergy_class = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(efergy_class)) {
		pr_err("failed to register device class '%s'\n", CLASS_NAME);
		retval = PTR_ERR(efergy_class);
		goto failed_classreg;
	}

	efergy_device = device_create(efergy_class, NULL, MKDEV(efergy_major, 0), NULL, CLASS_NAME "_" DEVICE_NAME);
	if (IS_ERR(efergy_device)) {
		pr_err("failed to create device '%s_%s'\n", CLASS_NAME, DEVICE_NAME);
		retval = PTR_ERR(efergy_device);
		goto failed_devreg;
	}

	mutex_init(&efergy_device_mutex);

	INIT_KFIFO(efergy_msg_fifo);

	return 0;

failed_devreg:
	class_unregister(efergy_class);
	class_destroy(efergy_class);
failed_classreg:
	unregister_chrdev(efergy_major, DEVICE_NAME);
failed_chrdevreg:
	free_irq(irq_number, NULL);

	return -1;
}

static void __exit efergy_exit(void)
{
	device_destroy(efergy_class, MKDEV(efergy_major, 0));
	class_unregister(efergy_class);
	class_destroy(efergy_class);
	unregister_chrdev(efergy_major, DEVICE_NAME);
	free_irq(irq_number, NULL);
	gpio_free(gpio_number);

	return;
}

module_init(efergy_init);
module_exit(efergy_exit);

MODULE_LICENSE("GPL");
