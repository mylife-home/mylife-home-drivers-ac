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
#include "ac_zc.h"

static int ac_zc_id = -1;

// Redefinition of macro to enable permissions to world
#define __ATTR_NOCHECK(_name, _mode, _show, _store) {                   \
        .attr = {.name = __stringify(_name),                            \
                 .mode =_mode },                                        \
        .show   = _show,                                                \
        .store  = _store,                                               \
}

#define DEVICE_ATTR_NOCHECK(_name, _mode, _show, _store) \
        struct device_attribute dev_attr_##_name = __ATTR_NOCHECK(_name, _mode, _show, _store)

/* button_desc
 *
 * This structure maintains the information regarding a
 * single AC button
 */
struct button_desc
{
	// corresponding sysfs device
	struct device   *dev;
	
	// count number when zc leave and button is not viewed pressed
	// ZC has 2 pules per period (one on the top of the wave and one at the bottom), button has only one
	// if zero_count reach 2, then value should go to 0
	// ZC measure low pin level, so we register on AC_ZC_STATUS_LEAVE to go to high level
	// button measure low pin level too, so if level is 0 on AC_ZC_STATUS_LEAVE, button is pressed
	int zero_count;
	
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

static void ac_button_zc_handler(int status, void *data);
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
	__ATTR_NOCHECK(export,   0222, NULL, export_store),
	__ATTR_NOCHECK(unexport, 0222, NULL, unexport_store),
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

	status = kstrtol(buf, 0, &gpio);
	if(status < 0)
		goto done;

	status = gpio_request(gpio, "ac_button");
	if(status < 0)
		goto done;

	status = gpio_direction_input(gpio);
	if(status < 0)
		goto done;
  
	status = button_export(gpio);
	if(status < 0)
		goto done;

	set_bit(FLAG_ACBUTTON, &button_table[gpio].flags);

done:
	if(status)
	{
		gpio_free(gpio);
		pr_debug("%s: status %d\n", __func__, status);
	}
	return status ? : len;
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
			gpio_free(gpio);
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
	desc->zero_count = 0;
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
		put_device(dev);
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

void ac_button_zc_handler(int status, void *data)
{
	unsigned int gpio;
	int gpio_value;
	struct button_desc *desc;
	int pressed;
		
	// buttons status management
	for(gpio=0; gpio<ARCH_NR_GPIOS; gpio++)
	{
		desc = &button_table[gpio];
		if(!test_bit(FLAG_ACBUTTON, &desc->flags))
			continue;
		
		// we are leaving ZC so we must check the button status
		gpio_value = gpio_get_value(gpio);
		
		// button measure low pin level too, so if level is 0 on AC_ZC_STATUS_LEAVE, button is pressed
		pressed = gpio_value ? 0 : 1;
		
		if(!pressed && desc->value)
		{
			++desc->zero_count;
			
			// ZC has 2 pules per period (one on the top of the wave and one at the bottom), button has only one
			// if zero_count reach 2, then value should go to 0, else just wait
			if(desc->zero_count < 2)
				continue;
		}
		
		// pressed represents now the real status (if we are not sure, continue already used)
		if(pressed != desc->value)
		{
			// changing
			desc->value = pressed;
			// notify change
			sysfs_notify(&desc->dev->kobj, NULL, "value");
		}
	}
}

int __init ac_button_init(void)
{
	int status;
	printk(KERN_INFO "AC button v0.1 initializing.\n");

	status = class_register(&ac_button_class);
	if(status < 0)
		goto fail_no_class;
	
	// ZC measure low pin level, so we register on AC_ZC_STATUS_LEAVE to go to high level
	status = ac_zc_register(AC_ZC_STATUS_LEAVE, ac_button_zc_handler, NULL);
	if(status < 0)
		goto fail_zc_register;
	
	ac_zc_id = status;

	printk(KERN_INFO "AC button initialized.\n");
	return 0;

fail_zc_register:
	class_unregister(&ac_button_class);
fail_no_class:
	return status;
}

void __exit ac_button_exit(void)
{
	unsigned int gpio;
	int status;
	
	ac_zc_unregister(ac_zc_id);

	for(gpio=0; gpio<ARCH_NR_GPIOS; gpio++)
	{
		struct button_desc *desc;
		desc = &button_table[gpio];
		if(test_bit(FLAG_ACBUTTON, &desc->flags))
		{
			gpio_set_value(gpio, 0);
			status = button_unexport(gpio);
			if(status == 0)
				gpio_free(gpio);
		}
	}

	class_unregister(&ac_button_class);
	printk(KERN_INFO "AC button disabled.\n");
}
