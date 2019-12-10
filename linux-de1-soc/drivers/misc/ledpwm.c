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
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>

#define PWM_MAX 0x7FF
#define PWM_MIN 0
#define PERCENTAGE_MAX 100
#define PERCENTAGE_MIN 0
#define PWM_VAL_PERIOD 200
#define DEVICE_NAME_LEN 8

static ssize_t ledpwm_read(struct file *filp, char __user *buff,
			   size_t count, loff_t *offp);
static ssize_t ledpwm_write(struct file *filp, const char __user *buff,
			    size_t count, loff_t *offp);
static int ledpwm_probe(struct platform_device *pdev);
static int ledpwm_remove(struct platform_device *pdev);


static const struct of_device_id ledpwm_of_match[] = {
	{ .compatible = "altr,de1soc-ledpwm", },
	{}
};

struct ledpwm {
	struct cdev cdev;
	struct miscdevice misc;
	u32 *registers;
	char last;
	char device_name[DEVICE_NAME_LEN];
};

static const struct file_operations fops = {
	.read	 = ledpwm_read,
	.write   = ledpwm_write
};

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
	struct ledpwm *ledpwm;

	if (filp == NULL || buff == NULL || offp == NULL) {
		pr_err("Nullptr error in ledpwm_read().");
		return -EINVAL;
	}

	ledpwm = container_of(filp->private_data, struct ledpwm, misc);

	/* Signal that end of file is reached with this read operation
	 * (only 1 Byte is readable)
	 */
	if (*offp != 0)
		return 0;

	/* Read last LEDPWM percentage */
	if (copy_to_user(buff, &ledpwm->last, 1)) {
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
	struct ledpwm *ledpwm;

	if (filp == NULL || buff == NULL || offp == NULL) {
		pr_err("Nullptr error in ledpwm_read().");
		return -EINVAL;
	}

	if (*offp != 0) {
		pr_err("Offset greater than 0 is not allowed.");
		return -EINVAL;
	}

	ledpwm = container_of(filp->private_data, struct ledpwm, misc);

	/* Write all given ledpwm values with 200ms
	 * in between values to the register.
	 */
	for (i = 0; i < count; i++) {
		/* Copy current percentage value
		 * in buffer to last set pwm value
		 */
		if (copy_from_user(&ledpwm->last, buff+i, 1)) {
			pr_err("Copy to user failed.");
			return -EFAULT;
		}

		/* Convert last written percentage to a ledpwm value
		 * and write it to the register.
		 */
		iowrite32(calc_percent_to_ledpwm(ledpwm->last),
			  ledpwm->registers);

		delay_jiffies = jiffies + HZ * PWM_VAL_PERIOD / 1000;
		while (time_before(jiffies, delay_jiffies))
			;

		*offp += 1;
	}

	return count;
}

static int ledpwm_probe(struct platform_device *pdev)
{
	struct resource *io = 0;
	struct ledpwm *ledpwm = 0;
	int retval = 0;
	static atomic_t ledpwm_no = ATOMIC_INIT(-1);
	int no = atomic_inc_return(&ledpwm_no);

	pr_info("ledpwm_probe nr. %d started ", no);

	ledpwm = devm_kzalloc(&pdev->dev, sizeof(*ledpwm), GFP_KERNEL);
	if (ledpwm == 0) {
		pr_err("Memory allocation for ledpwm failed.");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, ledpwm);

	/* Remap the physical address in virtual memory. */
	io = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (io == 0) {
		pr_err("Remapping physical address failed.");
		return -ENOMEM;
	}
	ledpwm->registers = devm_ioremap_resource(&pdev->dev, io);
	if (ledpwm->registers == 0) {
		pr_err("Remapping physical address failed.");
		return -ENOMEM;
	}

	snprintf(ledpwm->device_name,
		 sizeof(ledpwm->device_name),
		 "ledpwm%d", no);

	ledpwm->misc.name = ledpwm->device_name;
	ledpwm->misc.minor = MISC_DYNAMIC_MINOR;
	ledpwm->misc.fops = &fops;
	ledpwm->misc.parent = &pdev->dev;

	retval = misc_register(&ledpwm->misc);

	if (retval == 0) {
		iowrite32(PWM_MAX, ledpwm->registers);
		ledpwm->last = 100;
	}

	return retval;
}

static int ledpwm_remove(struct platform_device *pdev)
{
	struct ledpwm *ledpwm = platform_get_drvdata(pdev);

	if (ledpwm == 0) {
		pr_err("Getting device data failed.");
		return -ENODEV;
	}

	misc_deregister(&ledpwm->misc);

	iowrite32(PWM_MIN, ledpwm->registers);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver ledpwm_driver = {
	.driver = {
		.name = "LedpwmDriver",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(ledpwm_of_match)
	},
	.probe = ledpwm_probe,
	.remove = ledpwm_remove
};
module_platform_driver(ledpwm_driver);

MODULE_DEVICE_TABLE(of, ledpwm_of_match);
MODULE_DESCRIPTION("Module to output pwm patterns on the red leds.");
MODULE_AUTHOR("Simon Schneeberger");
MODULE_AUTHOR("Mortiz Tockner");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.1");
