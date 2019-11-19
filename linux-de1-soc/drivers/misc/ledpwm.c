#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/ioport.h>
#include <linux/jiffies.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/uaccess.h>

#define PWM_MAX 0x7FF
#define PWM_MIN 0
#define PERCENTAGE_MAX 100
#define PERCENTAGE_MIN 0
#define PWM_INCREMENT 100
#define PWM_ADDR_BASE 0xFF203080
#define PWM_NUM_OF_LEDS 9
#define TIMER_INITIAL_TIME 3000
#define TIMER_PERIOD 10
#define PWM_VAL_PERIOD 200

struct ledpwm {
	struct cdev cdev;
	u32 *registers;
	char last;
};
static struct ledpwm ledpwm;
static struct class *cdev_class;
static struct device *cdev_device;
static dev_t dev;

static u32 calc_percent_to_ledpwm(char const percent)
{
	/* If given percentage is over 100 the maximum pwm value
	 * (= 0x7FF) is returned
	 */
	if (percent > PERCENTAGE_MAX)
		return PWM_MAX;

	return (u32)(percent * PWM_MAX / PERCENTAGE_MAX);
}

static ssize_t ledpwm_read(struct file *filp, char __user *buff,
			   size_t count, loff_t *offp)
{
	if (filp == NULL || buff == NULL || offp == NULL) {
		pr_err("Nullptr error in ledpwm_read().");
		return -EINVAL;
	}

	/* Signal that end of file is reached with this read operation
	 * (only 1 Byte is readable)
	 */
	if (*offp != 0)
		return 0;

	/* Read last LEDPWM percentage */
	if (copy_to_user(buff, &ledpwm.last, 1)) {
		pr_err("Copy to user failed.");
		return -EFAULT;
	}

	*offp += 1;

	return 1;
}

static ssize_t ledpwm_write(struct file *filp, const char __user *buff,
			    size_t count, loff_t *offp)
{
	size_t i = 0;
	unsigned long delay_jiffies = 0;

	if (filp == NULL || buff == NULL || offp == NULL) {
		pr_err("Nullptr error in ledpwm_read().");
		return -EINVAL;
	}

	if (*offp != 0) {
		pr_err("Offset greater than 0 is not allowed.");
		return -EINVAL;
	}

	/* Write all given ledpwm values with 200ms
	 * in between values to the register.
	 */
	for (i = 0; i < count; i++) {
		/* Copy current percentage value
		 * in buffer to last set pwm value
		 */
		if (copy_from_user(&ledpwm.last, buff+i, 1)) {
			pr_err("Copy to user failed.");
			return -EFAULT;
		}

		/* Convert last written percentage to a ledpwm value
		 * and write it to the register.
		 */
		iowrite32(calc_percent_to_ledpwm(ledpwm.last),
			  ledpwm.registers + (PWM_NUM_OF_LEDS-1));

		delay_jiffies = jiffies + HZ * PWM_VAL_PERIOD / 1000;
		while (time_before(jiffies, delay_jiffies))
			;

		*offp += 1;
	}

	return count;
}

static const struct file_operations fops = {
	.read	 = ledpwm_read,
	.write   = ledpwm_write
};

static int major;
static u32 current_pwm_val;

static struct timer_list pattern_timer;

static void pwm_pattern(unsigned long var)
{
	size_t i = 0;

	for (i = 0; i < PWM_NUM_OF_LEDS-1; i++)
		iowrite32(current_pwm_val, ledpwm.registers + i);

	if (current_pwm_val >= PWM_MAX - PWM_INCREMENT)
		current_pwm_val = 0;
	else
		current_pwm_val = current_pwm_val + PWM_INCREMENT;

	mod_timer(&pattern_timer, jiffies + msecs_to_jiffies(TIMER_PERIOD));
}

static int __init ledpwm_init(void)
{
	size_t i = 0;

	/* Request memory for pwm led registers. */
	if (request_mem_region(PWM_ADDR_BASE,
			       PWM_NUM_OF_LEDS,
			       "ledpwmRegion") == NULL) {
		pr_err("Memory request failed.");
		return -ENOMEM;
	}

	/* Remap the physical address in virtual memory. */
	ledpwm.registers = ioremap(PWM_ADDR_BASE, PWM_NUM_OF_LEDS);
	if (ledpwm.registers == 0) {
		pr_err("Memory remapping failed.");
		release_mem_region(PWM_ADDR_BASE, PWM_NUM_OF_LEDS);
		return -ENOMEM;
	}

	for (i = 0; i < PWM_NUM_OF_LEDS; i++)
		iowrite32(PWM_MAX, ledpwm.registers + i);
	ledpwm.last = 100;

	/* Allocate, initialize and add character device region. */
	major = register_chrdev(0, "ledpwm", &fops);
	if (major < 0) {
		pr_err("Character device registering failed.");
		iounmap(ledpwm.registers);
		release_mem_region(PWM_ADDR_BASE, PWM_NUM_OF_LEDS);
		return -ECANCELED;
	}

	dev = MKDEV(major, 0);

	/* Create device class. */
	cdev_class = class_create(THIS_MODULE, "ldd5");
	if (IS_ERR(cdev_class)) {
		pr_err("Character device class create failed.");
		unregister_chrdev(major, "ledpwm");
		iounmap(ledpwm.registers);
		release_mem_region(PWM_ADDR_BASE, PWM_NUM_OF_LEDS);
		return PTR_ERR(cdev_class);
	}

	/* Create device. */
	cdev_device = device_create(cdev_class, NULL, dev, &ledpwm, "ledpwm0");
	if (IS_ERR(cdev_device)) {
		pr_err("Character device create failed.");
		class_destroy(cdev_class);
		unregister_chrdev(major, "ledpwm");
		iounmap(ledpwm.registers);
		release_mem_region(PWM_ADDR_BASE, PWM_NUM_OF_LEDS);
		return PTR_ERR(cdev_device);
	}

	/* Initialize timer to start periodic pwm pattern after 3 seconds. */
	init_timer(&pattern_timer);
	pattern_timer.data = (unsigned long)&pattern_timer;
	pattern_timer.function = pwm_pattern;
	pattern_timer.expires = jiffies + msecs_to_jiffies(TIMER_INITIAL_TIME);

	add_timer(&pattern_timer);

	return 0;
}

static void __exit ledpwm_exit(void)
{
	size_t i = 0;

	for (i = 0; i < PWM_NUM_OF_LEDS; i++)
		iowrite32(PWM_MIN, ledpwm.registers + i);

	del_timer_sync(&pattern_timer);
	device_destroy(cdev_class, dev);
	class_destroy(cdev_class);
	unregister_chrdev(major, "ledpwm");
	iounmap(ledpwm.registers);
	release_mem_region(PWM_ADDR_BASE, PWM_NUM_OF_LEDS);
}

module_init(ledpwm_init);
module_exit(ledpwm_exit);

MODULE_DESCRIPTION("Module to output pwm patterns on the red leds.");
MODULE_AUTHOR("Simon Schneeberger");
MODULE_AUTHOR("Mortiz Tockner");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.1");
