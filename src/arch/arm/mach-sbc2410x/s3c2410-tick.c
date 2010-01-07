/*
 * arch/arm/mach-sbc2410x/s3c2410-tick.c
 *
 * Copyright (c) 2007-2008  jianjun jiang <jjjstudio@gmail.com>
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
#include <debug.h>
#include <div64.h>
#include <time/tick.h>
#include <xboot/io.h>
#include <xboot/clk.h>
#include <xboot/irq.h>
#include <xboot/printk.h>
#include <xboot/initcall.h>
#include <s3c2410/reg-timer.h>


/*
 * tick timer interrupt.
 */
static void timer_interrupt(void)
{
	tick_interrupt();
}

static x_bool tick_timer_init(void)
{
	x_u64 pclk;

	if(!clk_get_rate("pclk", &pclk))
		return FALSE;

	if(!request_irq("INT_TIMER4", timer_interrupt))
		return FALSE;

	/* use pwm timer 4, prescaler for timer 4 is 16 */
	writel(S3C2410_TCFG0, (readl(S3C2410_TCFG0) & ~(0xff<<8)) | (0x0f<<8));

	/* select mux input for pwm timer4 is 1/2 */
	writel(S3C2410_TCFG1, (readl(S3C2410_TCFG1) & ~(0xf<<16)) | (0x00<<16));

	/* load value for 10 ms timeout */
	writel(S3C2410_TCNTB4, (x_u32)div64(pclk, (2 * 16 * 100)));

	/* auto load, manaual update of timer 4 and stop timer4 */
	writel(S3C2410_TCON, (readl(S3C2410_TCON) & ~(0x7<<20)) | (0x06<<20));

	/* start timer4 */
	writel(S3C2410_TCON, (readl(S3C2410_TCON) & ~(0x7<<20)) | (0x05<<20));

	return TRUE;
}

static struct tick s3c2410_tick = {
	.hz			= 100,
	.init		= tick_timer_init,
};

static __init void s3c2410_tick_init(void)
{
	if(!register_tick(&s3c2410_tick))
		DEBUG_E("failed to register tick");
}

module_init(s3c2410_tick_init, LEVEL_MACH_RES);
