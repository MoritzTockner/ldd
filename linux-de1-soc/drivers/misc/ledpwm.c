
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/ioport.h>
#include <linux/jiffies.h>
#include <linux/errno.h>
#include <linux/io.h>

#define PWM_MAX 0x7FF
#define PWM_MIN 0
#define PWM_INCREMENT 100
#define PWM_ADDR_BASE 0xFF203080
#define PWM_NUM_OF_LEDS 8
#define TIMER_INITIAL_TIME 3000
#define TIMER_PERIOD 10

static u32 *PWM_Leds;
static u32 current_pwm_val;
static int i;

static struct timer_list timer;
static struct timer_list pattern_timer;

static void pwm_pattern(unsigned long var)
{
	for (i = 0; i < PWM_NUM_OF_LEDS; i++)
		iowrite32(current_pwm_val, PWM_Leds + i);

	if (current_pwm_val >= PWM_MAX - PWM_INCREMENT)
		current_pwm_val = 0;
	else
		current_pwm_val = current_pwm_val + PWM_INCREMENT;

	mod_timer(&pattern_timer, jiffies + msecs_to_jiffies(TIMER_PERIOD));
}

static void start_pwm_pattern(unsigned long var)
{
	pattern_timer.function = pwm_pattern;
	pattern_timer.data = (unsigned long)&pattern_timer;
	pattern_timer.expires = jiffies;
	current_pwm_val = 0;

	add_timer(&pattern_timer);
}

static int __init ledpwm_init(void)
{
	/* Request memory for pwm led registers. */
	if (request_mem_region(PWM_ADDR_BASE,
			       PWM_NUM_OF_LEDS,
			       "PWM Leds") == NULL) {
		pr_err("Memory request failed.");
		return -ENOMEM;
	}

	/* Remap the physical address in virtual memory. */
	PWM_Leds = ioremap(PWM_ADDR_BASE, PWM_NUM_OF_LEDS);
	if (PWM_Leds == 0) {
		pr_err("Memory remoapping failed.");
		return -ENOMEM;
	}

	for (i = 0; i < PWM_NUM_OF_LEDS; i++)
		iowrite32(PWM_MAX, PWM_Leds + i);


	/* Initialize timer to start periodic pwm pattern after 3 seconds. */
	init_timer(&timer);
	timer.data = (unsigned long)&timer;
	timer.function = start_pwm_pattern;
	timer.expires = jiffies + msecs_to_jiffies(TIMER_INITIAL_TIME);

	add_timer(&timer);

	return 0;
}

static void __exit ledpwm_exit(void)
{
	for (i = 0; i < PWM_NUM_OF_LEDS; i++)
		iowrite32(PWM_MIN, PWM_Leds + i);

	del_timer_sync(&timer);
	del_timer_sync(&pattern_timer);
	release_mem_region(PWM_ADDR_BASE, PWM_NUM_OF_LEDS);
}

module_init(ledpwm_init);
module_exit(ledpwm_exit);

MODULE_DESCRIPTION("");
MODULE_AUTHOR("Simon Schneeberger");
MODULE_AUTHOR("Moritz Tockner");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
