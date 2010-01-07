/*
 * drivers/led/trigger-comm.c
 *
 *
 * Copyright (c) 2007-2009  jianjun jiang <jerryjianjun@gmail.com>
 * website: http://xboot.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <configs.h>
#include <default.h>
#include <types.h>
#include <string.h>
#include <malloc.h>
#include <xboot/printk.h>
#include <xboot/initcall.h>
#include <xboot/platform_device.h>
#include <time/tick.h>
#include <time/timer.h>
#include <led/led-trigger.h>


static x_bool valid = FALSE;
static x_u32 activity;
static x_u32 last_activity;
static struct timer_list comm_trigger_timer;


void comm_trigger_activity(void)
{
	if(valid)
	{
		activity++;
		if(!timer_pending(&comm_trigger_timer))
			mod_timer(&comm_trigger_timer, jiffies + 1);
	}
}

static void comm_trigger_function(x_u32 data)
{
	struct trigger * trigger = (struct trigger *)(data);
	struct led * led = (struct led *)(trigger->led);

	if(last_activity != activity)
	{
		last_activity = activity;
		led->set(LED_BRIGHTNESS_FULL);
		mod_timer(&comm_trigger_timer, jiffies + 1);
	}
	else
	{
		led->set(LED_BRIGHTNESS_OFF);
	}
}

static struct trigger comm_trigger = {
	.name     		= "led-communication",
	.activate 		= NULL,
	.deactivate		= NULL,
	.led			= NULL,
	.priv			= NULL,
};

static __init void comm_trigger_init(void)
{
	struct led * led;

	led = (struct led *)platform_device_get_data(comm_trigger.name);
	if(led && led->set)
	{
		if(led->init)
			(led->init)();

		comm_trigger.led = led;
		if(trigger_register(&comm_trigger))
		{
			setup_timer(&comm_trigger_timer, comm_trigger_function, (x_u32)(&comm_trigger));
			valid = TRUE;
			return;
		}
	}

	valid = FALSE;
}

static __exit void comm_trigger_exit(void)
{
	trigger_unregister(&comm_trigger);
}

module_init(comm_trigger_init, LEVEL_DRIVER);
module_exit(comm_trigger_exit, LEVEL_DRIVER);
