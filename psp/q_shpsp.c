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
// q_shpsp.c

#include <pspkernel.h>
#include <pspiofilemgr.h>

#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/time.h>
#include <malloc.h>
#include <ctype.h>

#include "../psp/glob.h"

#include "../qcommon/qcommon.h"

/*
================
Sys_Milliseconds
================
*/
int curtime;
int Sys_Milliseconds (void)
{
	static int timebase = 0;

	if(!timebase)
	{
		timebase = sceKernelGetSystemTimeLow();
		return 0;
	}

	curtime = (sceKernelGetSystemTimeLow() - timebase) / 1000;

	return curtime;
}

void Sys_Mkdir (char *path)
{
	sceIoMkdir (path, 0777);
}

char *strlwr (char *s)
{
	while (*s) {
		*s = tolower(*s);
		s++;
	}
}

//============================================

static	char	findbase[MAX_OSPATH];
static	char	findpath[MAX_OSPATH];
static	char	findpattern[MAX_OSPATH];
static	SceUID	fdir = -1;

static qboolean CompareAttributes (SceIoDirent *d, unsigned musthave, unsigned canthave)
{
	// . and .. never match
	if (strcmp(d->d_name, ".") == 0 || strcmp(d->d_name, "..") == 0)
		return false;

	if (FIO_S_ISDIR(d->d_stat.st_mode) && (canthave & SFF_SUBDIR))
		return false;

	if ((musthave & SFF_SUBDIR) && !FIO_S_ISDIR(d->d_stat.st_mode))
		return false;

	return true;
}

char *Sys_FindFirst (char *path, unsigned musthave, unsigned canhave)
{
	SceIoDirent	d;
	char *p;

	if (fdir >= 0)
		Sys_Error ("Sys_BeginFind without close");

	strcpy(findbase, path);

	if ((p = strrchr(findbase, '/')) != NULL) {
		*p = 0;
		strcpy(findpattern, p + 1);
	} else
		strcpy(findpattern, "*");

	if (strcmp(findpattern, "*.*") == 0)
		strcpy(findpattern, "*");

	fdir = sceIoDopen (path);
	if (fdir < 0)
		return NULL;

	while (1)
	{
		memset (&d, 0, sizeof(SceIoDirent));
		if(!sceIoDread (fdir, &d))
			break;

		if (!*findpattern || glob_match(findpattern, d.d_name))
		{
			if (CompareAttributes(&d, musthave, canhave))
			{
				sprintf (findpath, "%s/%s", findbase, d.d_name);
				return findpath;
			}
		}
	}

	return NULL;
}

char *Sys_FindNext (unsigned musthave, unsigned canhave)
{
	SceIoDirent	d;

	if (fdir < 0)
		return NULL;

	while (1)
	{
		memset (&d, 0, sizeof(SceIoDirent));
		if(!sceIoDread (fdir, &d))
			break;

		if (!*findpattern || glob_match(findpattern, d.d_name))
		{
			if (CompareAttributes(&d, musthave, canhave))
			{
				sprintf (findpath, "%s/%s", findbase, d.d_name);
				return findpath;
			}
		}
	}

	return NULL;
}

void Sys_FindClose (void)
{
	if (fdir >= 0)
		sceIoDclose (fdir);
	fdir = -1;
}


//============================================
