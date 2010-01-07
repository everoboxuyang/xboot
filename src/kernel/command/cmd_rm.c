/*
 * kernel/command/cmd_rm.c
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
#include <malloc.h>
#include <xboot/list.h>
#include <xboot/printk.h>
#include <xboot/initcall.h>
#include <shell/command.h>
#include <fs/fsapi.h>


#if	defined(CONFIG_COMMAND_RM) && (CONFIG_COMMAND_RM > 0)

static x_s32 do_rm(x_s32 argc, const x_s8 **argv)
{
	x_s32 i;
	x_s32 ret;
	struct stat st;

	if(argc < 2)
	{
		printk("usage:\r\n    rm [OPTION] FILE...\r\n");
		return (-1);
	}

	for(i=1; i<argc; i++)
	{
	    if(stat((const char*)argv[i], &st) == 0)
	    {
	        if(S_ISDIR(st.st_mode))
	            ret = rmdir((const char*)argv[i]);
	        else
	            ret = unlink((const char*)argv[i]);
			if(ret != 0)
				printk("rm: cannot remove %s: No such file or directory\r\n", argv[i]);
	    }
	    else
	    	printk("rm: cannot stat file or directory %s\r\n", argv[i]);
	}

	return 0;
}

static struct command rm_cmd = {
	.name		= "rm",
	.func		= do_rm,
	.desc		= "remove files or directories\r\n",
	.usage		= " rm [OPTION] FILE...\r\n",
	.help		= "    rm removes each specified file.  By default,\r\n"
				  "    it does not remove directories.\r\n"
};

static __init void rm_cmd_init(void)
{
	if(!command_register(&rm_cmd))
		DEBUG_E("register 'rm' command fail");
}

static __exit void rm_cmd_exit(void)
{
	if(!command_unregister(&rm_cmd))
		DEBUG_E("unregister 'rm' command fail");
}

module_init(rm_cmd_init, LEVEL_COMMAND);
module_exit(rm_cmd_exit, LEVEL_COMMAND);

#endif
