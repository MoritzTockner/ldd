#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include <linux/of.h>

#define DEVICE_NAME_LEN 11
#define FIFO_SIZE 8
#define KEY_EDGE_REG_MASK 0x0F
#define KEY_MASK_REG 0x0F

static irqreturn_t handler(int irq, void *data);
static ssize_t pushbutton_read(struct file *filp, char __user *buff,
			   size_t count, loff_t *offp);
static int pushbutton_probe(struct platform_device *pdev);
static int pushbutton_remove(struct platform_device *pdev);

static wait_queue_head_t queue;

static const struct of_device_id pushbutton_of_match[] = {
	{ .compatible = "altr,de1soc-pushbutton", },
	{}
};

struct pushbutton {
	struct cdev cdev;
	struct miscdevice misc;
	u32 *registers;
	struct kfifo button_event;
	char device_name[DEVICE_NAME_LEN];
};

static const struct file_operations fops = {
	.read	 = pushbutton_read
};

static irqreturn_t handler(int irq, void *data)
{
	struct pushbutton *buttondata = data;
	char edge = 0;

	edge = (char)(ioread32(buttondata->registers + 1) & KEY_EDGE_REG_MASK);

	/* No synchronization needed because only one write at a time.*/
	kfifo_put(&(buttondata->button_event), edge);

	iowrite32(KEY_EDGE_REG_MASK, buttondata->registers + 1);

	wake_up_interruptible(&queue);

	return IRQ_HANDLED;
}

static ssize_t pushbutton_read(struct file *filp, char __user *buff,
			   size_t count, loff_t *offp)
{
	struct pushbutton *data;
	int ret = 0;

	if (filp == NULL || buff == NULL || offp == NULL) {
		pr_err("Nullptr error in ledpwm_read().");
		return -EINVAL;
	}

	data = container_of(filp->private_data, struct pushbutton, misc);

	/* Only one read at a time */
	wait_event_interruptible(queue, !kfifo_is_empty(&(data->button_event)));

	ret = kfifo_to_user(
		&(data->button_event),
		buff,
		count,
		(unsigned int *)offp
	);

	wake_up_interruptible(&queue);


	if (ret != 0)
		pr_err("Copy to user failed.\n");

	/* Return number of bytes read or error code. */
	return ret ? ret : *offp;
}

static int pushbutton_probe(struct platform_device *pdev)
{
	struct resource *io = 0;
	struct pushbutton *data = 0;
	int retval = 0;
	int irq = 0;
	int status = 0;

	/* Init memory for driver data (struct pushbutton) */
	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (data == 0) {
		pr_err("Memory allocation for pushbutton failed.\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, data);

	/* Init buffer for key events */
	init_waitqueue_head(&queue);

	/* Init kfifo for key events */
	retval = kfifo_alloc(&(data->button_event), FIFO_SIZE, GFP_KERNEL);
	if (retval) {
		pr_err("Error kfifo_alloc.\n");
		platform_set_drvdata(pdev, NULL);
		return retval;
	}

	/* Remap the physical address in virtual memory. */
	io = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (io == 0) {
		pr_err("Remapping physical address failed.\n");
		kfifo_free(&(data->button_event));
		platform_set_drvdata(pdev, NULL);
		return -ENOMEM;
	}
	data->registers = devm_ioremap_resource(&pdev->dev, io);
	if (data->registers == 0) {
		pr_err("Remapping physical address failed.\n");
		kfifo_free(&(data->button_event));
		platform_set_drvdata(pdev, NULL);
		return -ENOMEM;
	}

	snprintf(data->device_name,
		 sizeof(data->device_name),
		 "pushbutton");

	data->misc.name = data->device_name;
	data->misc.minor = MISC_DYNAMIC_MINOR;
	data->misc.fops = &fops;
	data->misc.parent = &pdev->dev;

	retval = misc_register(&data->misc);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		pr_err("Irq request failed.");
		misc_deregister(&data->misc);
		kfifo_free(&(data->button_event));
		platform_set_drvdata(pdev, NULL);
		return irq;
	}

	status = devm_request_irq(
		&(pdev->dev),
		irq,
		handler,
		IRQF_SHARED,
		dev_name(&(pdev->dev)),
		data);

	if (status != 0) {
		pr_err("Irq request failed.");
		misc_deregister(&data->misc);
		kfifo_free(&(data->button_event));
		platform_set_drvdata(pdev, NULL);
		return status;
	}

	pr_info("Pushbutton configured.\n");

	/* Set irq mask register bits. */
	iowrite32(KEY_MASK_REG, data->registers);

	return retval;
}

static int pushbutton_remove(struct platform_device *pdev)
{
	struct pushbutton *data = platform_get_drvdata(pdev);

	if (data == 0) {
		pr_err("Getting device data failed.");
		return -ENODEV;
	}

	misc_deregister(&(data->misc));

	kfifo_free(&(data->button_event));

	platform_set_drvdata(pdev, NULL);


	return 0;
}

static struct platform_driver pushbutton_driver = {
	.driver = {
		.name = "PushbuttonDriver",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(pushbutton_of_match)
	},
	.probe = pushbutton_probe,
	.remove = pushbutton_remove
};
module_platform_driver(pushbutton_driver);

MODULE_DEVICE_TABLE(of, pushbutton_of_match);
MODULE_DESCRIPTION("Module to read key presses.");
MODULE_AUTHOR("Simon Schneeberger");
MODULE_AUTHOR("Mortiz Tockner");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.1");

