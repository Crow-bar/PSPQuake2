/*
Copyright (C) 2023 Sergey Galushko

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// sys_psp.c

#include <pspsdk.h>
#include <pspkernel.h>

#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include <sys/select.h>

#include "../qcommon/qcommon.h"


PSP_MODULE_INFO("PSPQuake2", PSP_MODULE_USER, 0, 1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB( -1 * 1024 );

cvar_t *nostdout;

unsigned sys_frame_time;

qboolean stdin_active = true;

void IN_KeyUpdate(void);

// =======================================================================
// General routines
// =======================================================================

void Sys_ConsoleOutput (char *string)
{
	if (nostdout && nostdout->value)
		return;

	fputs(string, stdout);
}

void Sys_Printf (char *fmt, ...)
{
	va_list argptr;
	char    text[1024];
	unsigned char   *p;

	va_start (argptr,fmt);
	vsprintf (text,fmt,argptr);
	va_end (argptr);

	if (strlen(text) > sizeof(text))
		Sys_Error("memory overwrite in Sys_Printf");

	if (nostdout && nostdout->value)
		return;

	for (p = (unsigned char *)text; *p; p++) {
		*p &= 0x7f;
		if ((*p > 128 || *p < 32) && *p != 10 && *p != 13 && *p != 9)
			printf("[%02x]", *p);
		else
			putc(*p, stdout);
	}
}

void Sys_Quit (void)
{
	CL_Shutdown ();
	Qcommon_Shutdown ();

	sceKernelExitGame();
}

void Sys_Init(void)
{

}

void Sys_Error (char *error, ...)
{
	va_list argptr;
	char    string[1024];

	CL_Shutdown ();
	Qcommon_Shutdown ();

	va_start (argptr,error);
	vsprintf (string,error,argptr);
	va_end (argptr);
	fprintf(stderr, "Error: %s\n", string);

	sceKernelExitGame();
}

void Sys_Warn (char *warning, ...)
{
	va_list argptr;
	char    string[1024];

	va_start (argptr,warning);
	vsprintf (string,warning,argptr);
	va_end (argptr);
	fprintf(stderr, "Warning: %s", string);
}

/*
============
Sys_FileTime

returns -1 if not present
============
*/
int Sys_FileTime (char *path)
{
	struct stat buf;

	if (stat (path, &buf) == -1)
		return -1;

	return buf.st_mtime;
}

/*
============
Sys_ConsoleInput

working with psplink in tty mode
============
*/
char *Sys_ConsoleInput(void)
{
#ifdef USE_STDIN
	int			ret;
	SceInt64	result;
	char		*outbuff;

	static char buffer[2][512];
	static int	buffind = 0;
	static int	scefd = -1;

	if (!stdin_active)
		return NULL;

	result = 0;
	outbuff = NULL;

	// stdin fd
	if (scefd == -1)
	{
		scefd = sceKernelStdin ();
		if (scefd < 0)
		{
			stdin_active = false;
			printf("Sys_ConsoleInput: sceKernelStdin (0x%x)\n", scefd);
			return NULL;
		}

		ret = sceIoReadAsync (scefd, &buffer[buffind], sizeof(buffer[0]) - 1);
		if (ret < 0)
		{
			stdin_active = false;
			printf("Sys_ConsoleInput: sceIoReadAsync (0x%x)\n", ret);
			return NULL;
		}
	}

	ret = sceIoPollAsync (scefd, &result);
	if (ret == 0)
	{
		if (result > 1)
		{
			buffer[buffind][result - 1] = 0; // rip off the /n and terminate
			outbuff = buffer[buffind];
			buffind = !buffind;
		}

		ret = sceIoReadAsync (scefd, &buffer[buffind], sizeof(buffer[0]) - 1);
		if (ret < 0)
		{
			stdin_active = false;
			printf("Sys_ConsoleInput: sceIoReadAsync (0x%x)\n", ret);
			return NULL;
		}
	}
	else if (ret < 0)
	{
		stdin_active = false;
		printf("Sys_ConsoleInput: sceIoPollAsync (0x%x)\n", ret);
	}

	return outbuff;
#else
	return NULL;
#endif
}

/*****************************************************************************/


/*
=================
Sys_UnloadGame
=================
*/
void Sys_UnloadGame (void)
{

}

/*
=================
Sys_GetGameAPI

Loads the game dll
=================
*/
void *GetGameAPI (void *import);
void *Sys_GetGameAPI (void *parms)
{
	return GetGameAPI (parms);
}

/*****************************************************************************/

void Sys_AppActivate (void)
{

}

void Sys_SendKeyEvents (void)
{
	IN_KeyUpdate();

	// grab frame time
	sys_frame_time = Sys_Milliseconds();

}

/*****************************************************************************/

char *Sys_GetClipboardData(void)
{
	return NULL;
}

int main (int argc, char **argv)
{
	int 	time, oldtime, newtime;

	pspSdkDisableFPUExceptions();

	Qcommon_Init(argc, argv);

	nostdout = Cvar_Get("nostdout", "0", 0);

	oldtime = Sys_Milliseconds ();
	while (1)
	{
		// find time spent rendering last frame
		do {
			newtime = Sys_Milliseconds ();
			time = newtime - oldtime;
		} while (time < 1);
		Qcommon_Frame (time);
		oldtime = newtime;
	}

	//sceKernelExitGame();

	return 0;
}

void Sys_CopyProtect(void)
{

}
