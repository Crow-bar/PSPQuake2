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

#include <pspctrl.h>
#include <pspkernel.h>
#include <pspsdk.h>
#include <psputility.h>

#ifdef USE_GPROF
#include <pspprof.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <sys/select.h>

#include "../psp/debug_psp.h"
#include "../qcommon/qcommon.h"

PSP_MODULE_INFO ("PSPQuake2", PSP_MODULE_USER, 0, 1);
PSP_MAIN_THREAD_ATTR (THREAD_ATTR_USER | THREAD_ATTR_VFPU);
//PSP_HEAP_SIZE_KB (-1 * 1024);
PSP_HEAP_SIZE_KB (-1280);
PSP_HEAP_THRESHOLD_SIZE_KB (1280); // new sdk ...

cvar_t *nostdout;

unsigned sys_frame_time;

qboolean stdin_active = true;

void IN_KeyUpdate (void);

// =======================================================================
// General routines
// =======================================================================

/*
=================
Sys_ConsoleOutput
=================
*/
void Sys_ConsoleOutput (char *string)
{
	if (nostdout && nostdout->value)
		return;

	fputs (string, stdout);
}

/*
=================
Sys_Printf
=================
*/
void Sys_Printf (char *fmt, ...)
{
	va_list        argptr;
	char           text[1024];
	unsigned char *p;

	va_start (argptr, fmt);
	vsprintf (text, fmt, argptr);
	va_end (argptr);

	if (strlen (text) > sizeof (text))
		Sys_Error ("memory overwrite in Sys_Printf");

	if (nostdout && nostdout->value)
		return;

	for (p = (unsigned char *)text; *p; p++)
	{
		*p &= 0x7f;
		if ((*p > 128 || *p < 32) && *p != 10 && *p != 13 && *p != 9)
			printf ("[%02x]", *p);
		else
			putc (*p, stdout);
	}
}

/*
=================
Sys_Quit
=================
*/
void Sys_Quit (void)
{
	CL_Shutdown();
	Qcommon_Shutdown();

	Dbg_Shutdown();

#ifdef USE_GPROF
	gprof_cleanup();
#endif

	sceKernelExitGame();
}

#ifdef DEBUG
#include <malloc.h>
void Sys_MemStat (void)
{
	struct mallinfo mi;

	mi = mallinfo();

	Com_Printf ("Total non-mmapped bytes (arena):       %u\n", mi.arena);
	Com_Printf ("# of free chunks (ordblks):            %u\n", mi.ordblks);
	Com_Printf ("# of free fastbin blocks (smblks):     %u\n", mi.smblks);
	Com_Printf ("# of mapped regions (hblks):           %u\n", mi.hblks);
	Com_Printf ("Bytes in mapped regions (hblkhd):      %u\n", mi.hblkhd);
	Com_Printf ("Max. total allocated space (usmblks):  %u\n", mi.usmblks);
	Com_Printf ("Free bytes held in fastbins (fsmblks): %u\n", mi.fsmblks);
	Com_Printf ("Total allocated space (uordblks):      %u\n", mi.uordblks);
	Com_Printf ("Total free space (fordblks):           %u\n", mi.fordblks);
	Com_Printf ("Topmost releasable block (keepcost):   %u\n", mi.keepcost);
}
#endif

/*
=================
Sys_Init
=================
*/
void Sys_Init (void)
{
#ifdef DEBUG
	Cmd_AddCommand ("memstat", Sys_MemStat);
#endif
}

/*
=================
Sys_Error
=================
*/
void Sys_Error (char *error, ...)
{
	va_list     argptr;
	char        string[1024];
	int         size;
	SceCtrlData pad;

	CL_Shutdown();
	Qcommon_Shutdown();

	va_start (argptr, error);
	size = vsnprintf (string, sizeof (string), error, argptr);
	va_end (argptr);
	fprintf (stderr, "Error: %s\n", string);

	if (Dbg_Init (NULL, 8888, "dbgfont", 0) == 0)
	{
		Dbg_SetClearColor (0, 0, 0);
		Dbg_DisplayClear();

		Dbg_SetTextXY (22, 0);
		Dbg_SetTextColor (255, 0, 0);
		Dbg_DrawText ("======== ERROR ========", 23);

		Dbg_SetTextXY (0, 3);
		Dbg_SetTextColor (255, 255, 255);
		Dbg_DrawText (string, size);

		Dbg_DrawText ("\n\n\n\nPress X to shutdown.", 24);

		// Wait for a X button press.
		do
			sceCtrlReadBufferPositive (&pad, 1);
		while (!(pad.Buttons & PSP_CTRL_CROSS));

		Dbg_Shutdown();
	}

	sceKernelExitGame();
}

/*
=================
Sys_Warn
=================
*/
void Sys_Warn (char *warning, ...)
{
	va_list argptr;
	char    string[1024];

	va_start (argptr, warning);
	vsprintf (string, warning, argptr);
	va_end (argptr);
	fprintf (stderr, "Warning: %s", string);
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
char *Sys_ConsoleInput (void)
{
#ifdef USE_STDIN
	int      ret;
	SceInt64 result;
	char    *outbuff;

	static char buffer[2][512];
	static int  buffind = 0;
	static int  scefd   = -1;

	if (!stdin_active)
		return NULL;

	result  = 0;
	outbuff = NULL;

	// stdin fd
	if (scefd == -1)
	{
		scefd = sceKernelStdin();
		if (scefd < 0)
		{
			stdin_active = false;
			printf ("Sys_ConsoleInput: sceKernelStdin (0x%x)\n", scefd);
			return NULL;
		}

		ret = sceIoReadAsync (scefd, &buffer[buffind], sizeof (buffer[0]) - 1);
		if (ret < 0)
		{
			stdin_active = false;
			printf ("Sys_ConsoleInput: sceIoReadAsync (0x%x)\n", ret);
			return NULL;
		}
	}

	ret = sceIoPollAsync (scefd, &result);
	if (ret == 0)
	{
		if (result > 1)
		{
			buffer[buffind][result - 1] = 0; // rip off the /n and terminate
			outbuff                     = buffer[buffind];
			buffind                     = !buffind;
		}

		ret = sceIoReadAsync (scefd, &buffer[buffind], sizeof (buffer[0]) - 1);
		if (ret < 0)
		{
			stdin_active = false;
			printf ("Sys_ConsoleInput: sceIoReadAsync (0x%x)\n", ret);
			return NULL;
		}
	}
	else if (ret < 0)
	{
		stdin_active = false;
		printf ("Sys_ConsoleInput: sceIoPollAsync (0x%x)\n", ret);
	}

	return outbuff;
#else
	return NULL;
#endif
}

/*****************************************************************************/
#ifndef GAME_HARD_LINKED
SceUID Sys_LoadModule (const char *filename, int mpid, SceSize argsize, void *argp)
{
	SceKernelLMOption option;
	SceUID            modid  = 0;
	int               retVal = 0, mresult;

	memset (&option, 0, sizeof (option));
	option.size     = sizeof (option);
	option.mpidtext = mpid;
	option.mpiddata = mpid;
	option.position = 0;
	option.access   = 1;

	retVal = sceKernelLoadModule (filename, 0, &option);
	if (retVal < 0)
		return retVal;

	modid = retVal;

	retVal = sceKernelStartModule (modid, argsize, argp, &mresult, NULL);
	if (retVal < 0)
		return retVal;

	return modid;
}

int Sys_UnloadModule (SceUID modid, int *sce_code)
{
	int status;
	*sce_code = sceKernelStopModule (modid, 0, NULL, &status, NULL);

	if ((*sce_code) < 0)
		return -2;
	else if (status == SCE_KERNEL_ERROR_NOT_STOPPED)
		return -1;

	*sce_code = sceKernelUnloadModule (modid);
	return (((*sce_code) < 0) ? -2 : 0);
}
#endif

static SceUID game_library = -1;

/*
=================
Sys_UnloadGame
=================
*/
void Sys_UnloadGame (void)
{
#ifndef GAME_HARD_LINKED
	int result, scecode;
	if (game_library >= 0)
	{
		result = Sys_UnloadModule (game_library, &scecode);
		if (result < 0)
		{
			if (result == -1)
				Com_Error (ERR_FATAL, "Sys_UnloadGame doesn't want to stop");
			else
				Com_Error (ERR_FATAL, "Sys_UnloadGame error ( %#010x )", scecode);

			return;
		}
	}
	game_library = -1;
#endif
}

/*
=================
Sys_GetGameAPI

Loads the game dll
=================
*/
void *Sys_GetGameAPI (void *parms)
{
#ifndef GAME_HARD_LINKED
	char        name[MAX_QPATH];
	const char *gamename = "gamepsp.prx";
	void       *modarg[2];
	void       *(*GetGameAPI) (void *);

	if (game_library >= 0)
		Com_Error (ERR_FATAL, "Sys_GetGameAPI without Sys_UnloadingGame");

	Com_sprintf (name, sizeof (name), "%s/%s", FS_GetWriteDir (FS_PATH_GAMEDIR), gamename);

	Com_Printf ("------- Loading %s -------\n", gamename);

	modarg[0] = &GetGameAPI;
	modarg[1] = NULL;

	game_library = Sys_LoadModule (name, 0, sizeof (modarg), modarg);
	if (game_library < 0)
	{
		Com_Printf ("Sys_LoadModule error (%#010x)\n", game_library);
		return NULL;
	}
	Com_DPrintf ("LoadLibrary (%s) modid (%i)\n", name, game_library);
#else
	void *GetGameAPI (void *import);
#endif

	return GetGameAPI (parms);
}

/*****************************************************************************/

/*
=================
Sys_AppActivate
=================
*/
void Sys_AppActivate (void)
{
}

/*
=================
Sys_SendKeyEvents
=================
*/
void Sys_SendKeyEvents (void)
{
	IN_KeyUpdate();

	// grab frame time
	sys_frame_time = Sys_Milliseconds();
}

/*****************************************************************************/

/*
=================
Sys_GetClipboardData
=================
*/
char *Sys_GetClipboardData (void)
{
	return NULL;
}

/*****************************************************************************/

/*
=================
Sys_ParseCmdFile
=================
*/
char *Sys_ParseCmdFile (const char *fname, void (*callback) (char *))
{
	int    i, ret, cmd_fd;
	size_t cmd_fsize;
	char  *cmd_buff, *cmd_last;

	cmd_fd = sceIoOpen (fname, PSP_O_RDONLY, 0777);
	if (cmd_fd < 0)
		return NULL;

	cmd_fsize = sceIoLseek (cmd_fd, 0, PSP_SEEK_END);
	sceIoLseek (cmd_fd, 0, PSP_SEEK_SET);

	if (cmd_fsize == 0)
	{
		sceIoClose (cmd_fd);
		return NULL;
	}
	cmd_buff = (char *)malloc (cmd_fsize + 1);
	if (!cmd_buff)
	{
		sceIoClose (cmd_fd);
		return NULL;
	}

	ret = sceIoRead (cmd_fd, cmd_buff, cmd_fsize);
	if (ret < 0)
	{
		free (cmd_buff);
		sceIoClose (cmd_fd);
		return NULL;
	}

	if (ret != cmd_fsize)
		cmd_fsize = ret;

	cmd_buff[cmd_fsize] = 0;
	cmd_last            = NULL;

	for (i = 0; i < cmd_fsize; i++)
	{
		if (isspace (cmd_buff[i]))
		{
			cmd_buff[i] = 0;
			if (cmd_last)
				callback (cmd_last);
			cmd_last = NULL;
		}
		else if (!cmd_last)
			cmd_last = &cmd_buff[i];
	}

	if (cmd_last)
		callback (cmd_last);

	sceIoClose (cmd_fd);

	return cmd_buff;
}

/*****************************************************************************/

/*
=================
Sys_CopyProtect
=================
*/
void Sys_CopyProtect (void)
{
}

/*****************************************************************************/

/*
=================
main
=================
*/
int main (int argc, char **argv)
{
	int time, oldtime, newtime;

	pspSdkDisableFPUExceptions();

	Qcommon_Init (argc, argv);

	nostdout = Cvar_Get ("nostdout", "0", 0);

	oldtime = Sys_Milliseconds();
	while (1)
	{
		newtime = Sys_Milliseconds();
		time    = newtime - oldtime;

		// find time spent rendering last frame
		if (time)
		{
			Qcommon_Frame (time);
			oldtime = newtime;
		}
	}

	// sceKernelExitGame();

	return 0;
}
