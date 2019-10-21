#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

static char *who = "World";
module_param(who, charp, 0000);

static int __init hello_init(void)
{
	pr_info("Hello, %s!\n", who);
	return 0;
}

static void __exit hello_exit(void)
{
	pr_info("Goodbye, %s!\n", who);
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Simon Schneeberger");
MODULE_AUTHOR("Moritz Tockner");
MODULE_DESCRIPTION("Module Hello 2");
MODULE_VERSION("2.0");
