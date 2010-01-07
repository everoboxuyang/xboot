/*
 * kernel/command/cmd_echo.c
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
#include <ctype.h>
#include <string.h>
#include <debug.h>
#include <malloc.h>
#include <xboot/list.h>
#include <xboot/printk.h>
#include <xboot/initcall.h>
#include <shell/command.h>


#if	defined(CONFIG_COMMAND_ECHO) && (CONFIG_COMMAND_ECHO > 0)

static x_s32 hextobin (x_s8 c)
{
	switch (c)
    {
    default: return c - '0';
    case 'a': case 'A': return 10;
    case 'b': case 'B': return 11;
    case 'c': case 'C': return 12;
    case 'd': case 'D': return 13;
    case 'e': case 'E': return 14;
    case 'f': case 'F': return 15;
    }
}

static x_s32 echo(x_s32 argc, const x_s8 **argv)
{
	const x_s8 *temp;
	const x_s8 *s;
	x_s8 c, ch;
	x_s32 i;
	x_bool do_v9 = FALSE;
	x_bool display_return = TRUE;

	--argc;
	++argv;

	while(argc > 0 && *argv[0] == '-')
	{
		temp = argv[0] + 1;

		/*
		 * If it appears that we are handling options, then make sure that
		 * all of the options specified are actually valid.  Otherwise, the
		 * string should just be echoed.
		 */
		for(i = 0; temp[i]; i++)
		{
			switch (temp[i])
			{
			case 'e':
			case 'E':
			case 'n':
				break;
			default:
				goto just_echo;
			}
		}

		if (i == 0)
		  goto just_echo;

		/*
		 * All of the options in TEMP are valid options to ECHO.
		 * Handle them.
		 */
		while(*temp)
		{
			switch(*temp++)
		    {
		    case 'e':
		      do_v9 = TRUE;
		      break;

		    case 'E':
		      do_v9 = FALSE;
		      break;

		    case 'n':
		      display_return = FALSE;
		      break;
		    }
		}

		argc--;
		argv++;
	}

just_echo:
	if(do_v9)
	{
		while(argc > 0)
		{
			s = argv[0];
			while ((c = *s++))
		    {
				if (c == '\\' && *s)
				{
					switch (c = *s++)
					{
					case 'a': c = '\a'; break;
					case 'b': c = '\b'; break;
					case 'c': return 0;
					case 'f': c = '\f'; break;
					case 'n': c = '\n'; break;
					case 'r': c = '\r'; break;
					case 't': c = '\t'; break;
					case 'v': c = '\v'; break;
					case 'x':
						ch = *s;
						if (! isxdigit (ch))
							goto not_an_escape;
						s++;
						c = hextobin (ch);
						ch = *s;
						if (isxdigit (ch))
						{
							s++;
							c = c * 16 + hextobin (ch);
						}
						break;
					case '0':
						c = 0;
						if (! ('0' <= *s && *s <= '7'))
							break;
						c = *s++;
						/* fall through */
					case '1': case '2': case '3':
					case '4': case '5': case '6': case '7':
						c -= '0';
						if ('0' <= *s && *s <= '7')
							c = c * 8 + (*s++ - '0');
						if ('0' <= *s && *s <= '7')
							c = c * 8 + (*s++ - '0');
						break;
					case '\\':
						break;
not_an_escape:
					default:
						printk("\\");
						break;
					}
				}
				printk("%c", c);
		    }
			argc--;
			argv++;
			if (argc > 0)
				printk(" ");
		}
	}
	else
	{
		while(argc > 0)
		{
			printk("%s",argv[0]);
			argc--;
			argv++;
			if(argc > 0)
				printk(" ");
		}
	}

	if(display_return)
		printk("\r\n");

	return 0;
}

static struct command echo_cmd = {
	.name		= "echo",
	.func		= echo,
	.desc		= "display a line of text\r\n",
	.usage		= "echo [OPTION]... [STRING]...\r\n",
	.help		= "    echo the STRING(s) to standard output.\r\n"
				  "    -n    do not output the trailing newline\r\n"
				  "    -e    enable interpretation of backslash escapes\r\n"
				  "    -E    disable interpretation of backslash escapes (default)\r\n"
};

static __init void echo_cmd_init(void)
{
	if(!command_register(&echo_cmd))
		DEBUG_E("register 'echo' command fail");
}

static __exit void echo_cmd_exit(void)
{
	if(!command_unregister(&echo_cmd))
		DEBUG_E("unregister 'echo' command fail");
}

module_init(echo_cmd_init, LEVEL_COMMAND);
module_exit(echo_cmd_exit, LEVEL_COMMAND);

#endif
