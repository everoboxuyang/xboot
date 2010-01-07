/*
 * kernel/command/cmd_version.c
 *
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
#include <string.h>
#include <version.h>
#include <xboot/list.h>
#include <xboot/printk.h>
#include <xboot/initcall.h>
#include <shell/command.h>
#include <terminal/terminal.h>


#if	defined(CONFIG_COMMAND_VERSION) && (CONFIG_COMMAND_VERSION > 0)

extern struct hlist_head stdout_list;
extern struct hlist_head stdin_list;

static x_s32 version(x_s32 argc, const x_s8 **argv)
{
	struct terminal_stdout_list * list;
	struct hlist_node * pos;

	/* print xboot's banner */
	hlist_for_each_entry(list,  pos, &stdout_list, node)
	{
		xboot_banner(list->term);
	}

	return 0;
}

static struct command version_cmd = {
	.name		= "version",
	.func		= version,
	.desc		= "show xboot's version\r\n",
	.usage		= "version\r\n",
	.help		= "    version ignores any command line parameters that may be present.\r\n"
};

static __init void version_cmd_init(void)
{
	if(!command_register(&version_cmd))
		DEBUG_E("register 'version' command fail");
}

static __exit void version_cmd_exit(void)
{
	if(!command_unregister(&version_cmd))
		DEBUG_E("unregister 'version' command fail");
}

module_init(version_cmd_init, LEVEL_COMMAND);
module_exit(version_cmd_exit, LEVEL_COMMAND);

#endif
