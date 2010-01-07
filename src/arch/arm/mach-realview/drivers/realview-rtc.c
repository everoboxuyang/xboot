/*
 * drivers/realview_rtc.c
 *
 * realview rtc drivers, the primecell pl031 real time clock.
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
#include <debug.h>
#include <time/delay.h>
#include <xboot/initcall.h>
#include <xboot/io.h>
#include <xboot/ioctl.h>
#include <xboot/clk.h>
#include <xboot/printk.h>
#include <xboot/platform_device.h>
#include <time/xtime.h>
#include <rtc/rtc.h>
#include <realview/reg-rtc.h>


static void rtc_init(void)
{
	return;
}

static void rtc_exit(void)
{
	return;
}

static x_bool rtc_set_time(struct time * time)
{
	if(rtc_valid_time(time))
	{
		writel(REALVIEW_RTC_LR, time_to_rtc(time));
		return TRUE;
	}

	return FALSE;
}

static x_bool rtc_get_time(struct time * time)
{
	rtc_to_time(readl(REALVIEW_RTC_DR), time);

	return TRUE;
}

static struct rtc_driver realview_rtc = {
	.name			= "rtc",
	.init			= rtc_init,
	.exit			= rtc_exit,
	.set_time		= rtc_set_time,
	.get_time		= rtc_get_time,
	.set_alarm		= NULL,
	.get_alarm		= NULL,
	.alarm_enable	= NULL,
};

static __init void realview_rtc_init(void)
{
	if(!register_rtc(&realview_rtc))
		DEBUG_E("failed to register rtc driver '%s'", realview_rtc.name);
}

static __exit void realview_rtc_exit(void)
{
	if(!unregister_rtc(&realview_rtc))
		DEBUG_E("failed to unregister rtc driver '%s'", realview_rtc.name);
}

module_init(realview_rtc_init, LEVEL_DRIVER);
module_exit(realview_rtc_exit, LEVEL_DRIVER);
