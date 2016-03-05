/* Copyright (C) 2014 Vincent TRUMPFF
 *
 * May be copied or modified under the terms of the GNU General Public
 * License. See linux/COPYING for more information.
 *
 * Generic software-only driver for AC button
 * (based on zero crossing detection)
 * via GPIO lib interface.
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/gpio.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/math64.h>
#include <linux/string.h>

#include "ac_common.h"

static struct hrtimer hr_timer;
static int timer_on = 0;

/* button_desc
 *
 * This structure maintains the information regarding a
 * single AC button
 */
struct button_desc
{
	// corresponding sysfs device
	struct device   *dev;

	// irq number
	int irq;

	// indicate if an interrupt occured between 50ms interval bounds
	int interrupted;

	// logical value
	int value;

	// only FLAG_ACBUTTON is used, for synchronizing inside module
	unsigned long flags;
#define FLAG_ACBUTTON 1
};

/* button_table
 *
 * The table will hold a description for any GPIO pin available
 * on the system. It's wasteful to preallocate the entire table,
 * but avoiding race conditions is so much easier this way ;-)
*/
static struct button_desc button_table[ARCH_NR_GPIOS];

/* lock protects against button_unexport() being called while
 * sysfs files are active.
 */
static DEFINE_MUTEX(sysfs_lock);

static int button_export(unsigned int gpio);
static int button_unexport(unsigned int gpio);
static ssize_t button_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t export_store(struct class *class, struct class_attribute *attr, const char *buf, size_t len);
static ssize_t unexport_store(struct class *class, struct class_attribute *attr, const char *buf, size_t len);

static irqreturn_t ac_button_irq_handler(int irq, void *dev_id);
static enum hrtimer_restart ac_button_hrtimer_callback(struct hrtimer *timer);

static int ac_button_init(void);
static void ac_button_exit(void);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vincent TRUMPFF");
MODULE_DESCRIPTION("Driver for AC button");

module_init(ac_button_init);
module_exit(ac_button_exit);

/* Sysfs attributes definition for buttons */
static DEVICE_ATTR(value,   0444, button_show, NULL);

static const struct attribute *ac_button_dev_attrs[] =
{
	&dev_attr_value.attr,
	NULL,
};

static const struct attribute_group ac_button_dev_attr_group =
{
	.attrs = (struct attribute **) ac_button_dev_attrs,
};

/* Sysfs definitions for ac_button class */
static struct class_attribute ac_button_class_attrs[] =
{
	__ATTR_WO(export),
	__ATTR_WO(unexport),
	__ATTR_NULL,
};
static struct class ac_button_class =
{
	.name =        "ac_button",
	.owner =       THIS_MODULE,
	.class_attrs = ac_button_class_attrs,
};

/* Show attribute values for buttons */
ssize_t button_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	const struct button_desc *desc = dev_get_drvdata(dev);
	ssize_t status;
	mutex_lock(&sysfs_lock);
	if(!test_bit(FLAG_ACBUTTON, &desc->flags))
	{
		status = -EIO;
	}
	else
	{
		if(strcmp(attr->attr.name, "value") == 0)
			status = sprintf(buf, "%d\n", desc->value);
		else
			status = -EIO;
	}
	mutex_unlock(&sysfs_lock);
	return status;
}

/* Export a GPIO pin to sysfs, and claim it for button usage.
 * See the equivalent function in drivers/gpio/gpiolib.c
 */
ssize_t export_store(struct class *class, struct class_attribute *attr, const char *buf, size_t len)
{
	long gpio;
	int status;
	int irq;

	status = kstrtol(buf, 0, &gpio);
	if(status < 0)
		goto fail_safe;

	status = gpio_request(gpio, "ac_button");
	if(status < 0)
		goto fail_safe;

	status = gpio_direction_input(gpio);
	if(status < 0)
		goto fail_after_gpio;

	status = irq = gpio_to_irq(gpio);
	if(status < 0)
		goto fail_after_gpio;

	status = request_irq(irq, ac_button_irq_handler, /*IRQF_TRIGGER_FALLING |*/ IRQF_TRIGGER_RISING | IRQF_NO_THREAD, "ac_button_gpio_irq", &ac_button_class);
	if(status < 0)
		goto fail_after_gpio;

	status = button_export(gpio);
	if(status < 0)
		goto fail_after_irq;

	button_table[gpio].irq = irq;
	set_bit(FLAG_ACBUTTON, &button_table[gpio].flags);

	if(!timer_on)
	{
		hrtimer_start(&hr_timer, ktime_set(0, 50000000), HRTIMER_MODE_REL); // 50ms
		timer_on = 1;
	}

	return len;

fail_after_irq:
	free_irq(irq, &ac_button_class);
fail_after_gpio:
  gpio_free(gpio);
fail_safe:
  pr_debug("%s: status %d\n", __func__, status);
  return status;
}

/* Unexport a button GPIO pin from sysfs, and unreclaim it.
 * See the equivalent function in drivers/gpio/gpiolib.c
 */
ssize_t unexport_store(struct class *class, struct class_attribute *attr, const char *buf, size_t len)
{
	long gpio;
	int  status;

	status = kstrtol(buf, 0, &gpio);
	if(status < 0)
		goto done;

	status = -EINVAL;
	if(!gpio_is_valid(gpio))
		goto done;

	if(test_and_clear_bit(FLAG_ACBUTTON, &button_table[gpio].flags))
	{
		status = button_unexport(gpio);
		if(status == 0)
		{
			free_irq(button_table[gpio].irq, &ac_button_class);
			gpio_free(gpio);
		}
	}

done:
	if(status)
		pr_debug("%s: status %d\n", __func__, status);
	return status ? : len;
}

/* Setup the sysfs directory for a claimed button device */
int button_export(unsigned int gpio)
{
	struct button_desc *desc;
	struct device   *dev;
	int             status;

	mutex_lock(&sysfs_lock);

	desc = &button_table[gpio];
	desc->interrupted = 0;
	desc->value = 0;
	desc->dev = dev = device_create(&ac_button_class, NULL, MKDEV(0, 0), desc, "button%d", gpio);
	if(dev)
	{
		status = sysfs_create_group(&dev->kobj, &ac_button_dev_attr_group);
		if(status == 0)
			printk(KERN_INFO "Registered device button%d\n", gpio);
		else
			device_unregister(dev);
	}
	else
	{
		status = -ENODEV;
	}

	mutex_unlock(&sysfs_lock);

	if(status)
		pr_debug("%s: button%d status %d\n", __func__, gpio, status);
	return status;
}

/* Free a claimed button device and unregister the sysfs directory */
int button_unexport(unsigned int gpio)
{
	struct button_desc *desc;
	struct device   *dev;
	int             status;

	mutex_lock(&sysfs_lock);

	desc = &button_table[gpio];
	dev  = desc->dev;
	if(dev)
	{
		device_unregister(dev);
		printk(KERN_INFO "Unregistered device button%d\n", gpio);
		status = 0;
	}
	else
	{
		status = -ENODEV;
	}

	mutex_unlock(&sysfs_lock);

	if(status)
		pr_debug("%s: button%d status %d\n", __func__, gpio, status);
	return status;
}

irqreturn_t ac_button_irq_handler(int irq, void *dev_id)
{
	unsigned int gpio;
	struct button_desc *desc;

	if(dev_id != &ac_button_class)
		return IRQ_NONE;

	for(gpio=0; gpio<ARCH_NR_GPIOS; gpio++)
	{
		desc = &button_table[gpio];
		if(!test_bit(FLAG_ACBUTTON, &desc->flags))
			continue;
		if(irq != desc->irq)
			continue;

		gpio_get_value(gpio);

		desc->interrupted = 1;
		break;
	}

	return IRQ_HANDLED;
}

enum hrtimer_restart ac_button_hrtimer_callback(struct hrtimer *timer)
{
	unsigned int gpio;
	struct button_desc *desc;
	int restart_timer = 0;
	int interrupted;

	for(gpio=0; gpio<ARCH_NR_GPIOS; gpio++)
	{
		desc = &button_table[gpio];
		if(!test_bit(FLAG_ACBUTTON, &desc->flags))
			continue;

		interrupted = desc->interrupted;
		if(interrupted != desc->value)
		{
			// changing
			desc->value = interrupted;
			// notify change
			sysfs_notify(&desc->dev->kobj, NULL, "value");
		}

		desc->interrupted = 0;
		restart_timer = 1;
	}

	if(restart_timer)
	{
		// should use hrtimer_forward ?
		hrtimer_start(&hr_timer, ktime_set(0, 50000000), HRTIMER_MODE_REL); // 50ms
	}
	else
		timer_on = 0;

	return HRTIMER_NORESTART;
}

int __init ac_button_init(void)
{
	struct timespec tp;
	int status;
	printk(KERN_INFO "AC button v0.1 initializing.\n");

	hrtimer_get_res(CLOCK_MONOTONIC, &tp);
	printk(KERN_INFO "Clock resolution is %ldns\n", tp.tv_nsec);

	hrtimer_init(&hr_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hr_timer.function = &ac_button_hrtimer_callback;

	status = class_register(&ac_button_class);
	if(status < 0)
		goto fail_no_class;

	printk(KERN_INFO "AC button initialized.\n");
	return 0;

fail_no_class:
	return status;
}

void __exit ac_button_exit(void)
{
	unsigned int gpio;
	int status;
	int irq;

	hrtimer_cancel(&hr_timer);

	for(gpio=0; gpio<ARCH_NR_GPIOS; gpio++)
	{
		struct button_desc *desc;
		desc = &button_table[gpio];
		if(test_bit(FLAG_ACBUTTON, &desc->flags))
		{
			irq = desc->irq;
			status = button_unexport(gpio);
			if(status == 0)
			{
				free_irq(irq, &ac_button_class);
				gpio_free(gpio);
			}
		}
	}

	class_unregister(&ac_button_class);
	printk(KERN_INFO "AC button disabled.\n");
}
