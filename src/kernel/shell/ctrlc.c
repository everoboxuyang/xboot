/*
 * kernel/shell/ctrlc.c
 *
 *
 * Copyright (c) 2007-2008  jianjun jiang
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
#include <time/timer.h>
#include <xboot/scank.h>
#include <shell/ctrlc.h>


/*
 * check ctrl-c
 */
x_bool ctrlc(void)
{
	char c;

	if(getch(&c))
	{
		if(c == 3)		/* ctrl-c */
			return TRUE;
	}

	return FALSE;
}
