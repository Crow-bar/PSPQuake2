/*
Copyright (C) 1997-2001 Id Software, Inc.

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

#include <sys/stat.h>
#include <dirent.h>
#include "qcommon.h"

// define this to dissalow any data but the demo pak file
//#define	NO_ADDONS

// if a packfile directory differs from this, it is assumed to be hacked
// Full version
#define	PAK0_CHECKSUM	0x40e614e0
// Demo
//#define	PAK0_CHECKSUM	0xb2c6d7ea
// OEM
//#define	PAK0_CHECKSUM	0x78e135c

#define	FS_BUFFER_SIZE	2048
#define	FS_COPY_SIZE	2048

/*
=============================================================================

QUAKE FILESYSTEM

=============================================================================
*/


//
// in memory
//

struct file_s
{
	FILE	*handle;
	int		flags;
	off_t	length;
	off_t	position;
	off_t	offset;
	struct
	{
		off_t	position;
		off_t	length;
		byte	*ptr;
	} buffer;
	struct file_s	*next;
};

typedef struct pack_s
{
	char	filename[MAX_OSPATH];
	int		numfiles;
	dpackfile_t	*files;
} pack_t;

char	fs_gamedir[MAX_OSPATH];
char	fs_basedir[MAX_OSPATH];
char	fs_rootdir[MAX_OSPATH];

cvar_t	*fs_gamedirvar;
cvar_t	*fs_rootdirvar;
cvar_t	*fs_customdirvar;

typedef struct filelink_s
{
	char	*from;
	int		fromlength;
	char	*to;
	int		tolength;
	struct filelink_s	*next;
} filelink_t;

filelink_t	*fs_links;

typedef struct searchpath_s
{
	char	filename[MAX_OSPATH];
	pack_t	*pack;		// only one of filename / pack will be used
	int		flags;
	struct searchpath_s *next;
} searchpath_t;

searchpath_t	*fs_searchpaths;
searchpath_t	*fs_base_searchpaths;	// without gamedirs


/*

All of Quake's data access is through a hierchal file system, but the contents of the file system can be transparently merged from several sources.

The "base directory" is the path to the directory holding the quake.exe and all game directories.  The sys_* files pass this to host_init in quakeparms_t->basedir.  This can be overridden with the "-basedir" command line parm to allow code debugging in a different directory.  The base directory is
only used during filesystem initialization.

The "game directory" is the first tree on the search path and directory that all generated files (savegames, screenshots, demos, config files) will be saved to.  This can be overridden with the "-game" command line parameter.  The game directory can never be changed while quake is executing.  This is a precacution against having a malicious server instruct clients to write files over areas they shouldn't.

*/

/*
=============================================================================

REAL FILESYSTEM

=============================================================================
*/

/*
==================
RFS_FileExists

Look for a file in the filesystem only
==================
*/
static qboolean RFS_FileExists (const char *filename)
{
	int			result;
	struct stat	buf;

	result = stat(filename, &buf);
	if(result < 0)
		return false;

	return S_ISREG(buf.st_mode);
}

/*
=============================================================================

COMMON FILE SYSTEM

=============================================================================
*/

/*
====================
FS_FindLink
====================
*/
static filelink_t *FS_FindLink(const char *oldpath, char *newpath, size_t nlength)
{
	filelink_t	*link;
	size_t		olength;

	olength = strlen(oldpath);

	for (link = fs_links; link; link = link->next)
	{
		if(link->fromlength > olength)
			continue;

		if (!strncmp (oldpath, link->from, link->fromlength))
		{
			Com_sprintf (newpath, nlength, "%s%s", link->to, oldpath + link->fromlength);
			return link;
		}
	}
	return NULL;
}

/*
====================
FS_FindFile

Look for a file in the real filesystem and in the paks

Return the searchpath where the file was found (or NULL)
and the file index in the package if relevant
====================
*/
static searchpath_t *FS_FindFile(const char *filename, int *index, int flags)
{
	searchpath_t	*search;
	char			netpath[MAX_OSPATH];
	pack_t			*pak;
	int				i;

	if( index )
		*index = -1;

	// search through the path, one element at a time
	for (search = fs_searchpaths; search; search = search->next)
	{
		if((flags & FS_PATH_MASK) && !(search->flags & flags & FS_PATH_MASK))
			continue;

		if((flags & FS_TYPE_MASK) && !(search->flags & flags & FS_TYPE_MASK))
			continue;

		// is the element a pak file?
		if (search->pack)
		{
			// look through all the pak file elements
			pak = search->pack;
			for (i = 0; i < pak->numfiles; i++)
			{
				if (!Q_strcasecmp (pak->files[i].name, filename))
				{	// found it!
					if( index )
						*index = i;

					Com_DPrintf ("FS_FindFile: pack: %s : %s\n", pak->filename, filename);
					return search;
				}
			}
		}
		else
		{
			// check a file in the directory tree
			Com_sprintf (netpath, sizeof(netpath), "%s/%s", search->filename, filename);

			if(RFS_FileExists(netpath))
			{
				Com_DPrintf ("FS_FindFile: %s\n", netpath);
				return search;
			}
		}
	}
	return NULL;
}

/*
==================
FS_FileExists

Look for a file in the real filesystem and in the paks
==================
*/
qboolean FS_FileExists(const char *filename, int flags)
{
	if(FS_FindFile(filename, NULL, flags))
		return true;
	return false;
}

/*
================
FS_FileRemove

delete specified file from gamefolder
================
*/
qboolean FS_FileRemove (const char *filename, int flags)
{
	char	path[MAX_OSPATH];

	if(!filename || !*filename)
		return false;

	Com_sprintf (path, sizeof(path), "%s/%s", FS_WriteDir(flags), filename);

	return (remove(path) == 0) ? true : false;
}

/*
==================
FS_Rename

rename specified file from base or game folder
==================
*/
qboolean FS_FileRename (const char *oldname, const char *newname, int flags)
{
	char		oldpath[MAX_OSPATH];
	char		newpath[MAX_OSPATH];
	const char	*wpath;

	if( !oldname || !newname || !*oldname || !*newname )
		return false;

	wpath = FS_WriteDir(flags);

	Com_sprintf (oldpath, sizeof(oldpath), "%s/%s", wpath, oldname);
	Com_sprintf (newpath, sizeof(newpath), "%s/%s", wpath, newname);

	return (rename(oldpath, newpath) == 0);
}

/*
============
FS_CreatePath

Creates any directories needed to store the given filename
============
*/
void FS_CreatePath (char *path)
{
	char	*ofs;

	for (ofs = path+1 ; *ofs ; ofs++)
	{
		if (*ofs == '/')
		{	// create the directory
			*ofs = 0;
			Sys_Mkdir (path);
			*ofs = '/';
		}
	}
}

// RAFAEL
/*
	Developer_searchpath
*/
int	Developer_searchpath (int who)
{

	int		ch;
	// PMM - warning removal
//	char	*start;
	searchpath_t	*search;

	if (who == 1) // xatrix
		ch = 'x';
	else if (who == 2)
		ch = 'r';

	for (search = fs_searchpaths ; search ; search = search->next)
	{
		if (strstr (search->filename, "xatrix"))
			return 1;

		if (strstr (search->filename, "rogue"))
			return 2;
/*
		start = strchr (search->filename, ch);

		if (start == NULL)
			continue;

		if (strcmp (start ,"xatrix") == 0)
			return (1);
*/
	}
	return (0);

}


/*
===========
FS_FOpen

Finds the file in the search path.
returns a file_t handle
Used for streaming data out of either a pak file or
a separate file.
===========
*/
file_t *FS_FOpen (const char *filename, int flags)
{
	file_t			*file;
	searchpath_t	*search;
	char			netpath[MAX_OSPATH];
	char			linkpath[MAX_OSPATH];
	char			mode[5];
	pack_t			*pak;
	int				index;

	// allocate new file handle
	file = (file_t *)Z_Malloc(sizeof(file_t) + FS_BUFFER_SIZE + 64);
	memset(file, 0, sizeof(file_t) + FS_BUFFER_SIZE + 64);

	// cache buffer
	file->buffer.ptr = (byte *)(((uintptr_t)file + sizeof(file_t) + 63) & (~63));

	// check for links first
	if(FS_FindLink(filename, linkpath, sizeof(linkpath)))
		filename = linkpath;

	memset(mode, 0, sizeof(mode));

	switch(flags & FS_MODE_MASK)
	{
	case FS_MODE_APPEND:
		strcpy(mode, "a");
		break;
	case FS_MODE_READ:
		strcpy(mode, "r");
		break;
	case FS_MODE_WRITE:
		strcpy(mode, "w");
		break;
	case FS_MODE_RW:
		strcpy(mode, "r+");
		break;
	default:
		return NULL;
	}

	if(!(flags & FS_FLAG_TEXT))
		strcat(mode, "b");

	file->flags = flags;

	if(flags & FS_MODE_READ)
	{
		// search through the path, one element at a time
		search = FS_FindFile(filename, &index, flags);
		if(search)
		{
			// inheritance
			file->flags |= search->flags;

			if(search->pack)
			{
				pak = search->pack;

				file->handle = fopen (pak->filename, "rb");
				if (!file->handle)
					Com_Error (ERR_FATAL, "FS_FOpen: can't open pak file %s", pak->filename);

				fseek (file->handle, pak->files[index].filepos, SEEK_SET);

				file->offset = pak->files[index].filepos;
				file->length = pak->files[index].filelen;
			}
			else
			{
				Com_sprintf (netpath, sizeof(netpath), "%s/%s", search->filename, filename);

				file->handle = fopen (netpath, mode);
				if (!file->handle)
				{
					Z_Free(file);
					return NULL;
				}

				fseek (file->handle, 0, SEEK_END);
				file->length = ftell (file->handle);
				fseek (file->handle, 0, SEEK_SET);
			}
			return file;
		}
	}
	else
	{
		file->flags |= FS_TYPE_RFS; // hardcoded

		Com_sprintf (netpath, sizeof(netpath), "%s/%s", FS_WriteDir(flags), filename);
		FS_CreatePath (netpath);

		file->handle = fopen (netpath, mode);
		if (!file->handle)
		{
			Z_Free(file);
			return NULL;
		}

		fseek (file->handle, 0, SEEK_END);
		file->length = ftell (file->handle);
		if((flags & FS_MODE_MASK) == FS_MODE_APPEND)
			file->position = file->length;
		else
			fseek (file->handle, 0, SEEK_SET);

		return file;
	}
	Com_DPrintf ("FS_FOpen: can't open file %s\n", filename);

	Z_Free(file);
	return NULL;
}

/*
==================
FS_FCheckFlags
==================
*/
qboolean FS_FCheckFlags (file_t *file, int flags)
{
	if(!file)
		return false;

	if((file->flags & flags) == flags)
		return true;

	return false;
}

/*
==================
FS_FLength

Gets a file lenght
==================
*/
off_t FS_FLength (file_t *file)
{
	if(!file)
		return -1;
	return file->length;
}

/*
=================
FS_FRead

Reads data from a file into the array.
=================
*/
off_t FS_FRead (file_t *file, const void *buffer, size_t size)
{
	off_t	readlenght, readresult;
	off_t	completed, remaining;
	byte	*buf;

	if(!file || size == 0)
		return 0;

	buf = (byte *)buffer;

	completed = 0;
	remaining = (off_t)size;

	// check cache
	if(file->buffer.position < file->buffer.length)
	{
		readlenght = file->buffer.length - file->buffer.position;
		if(readlenght > remaining)
			readlenght = remaining;

		memcpy(buf, &file->buffer.ptr[file->buffer.position], readlenght);
		file->buffer.position += readlenght;

		completed += readlenght;
		remaining -= readlenght;

		if(remaining == 0)
			return completed;
	}

	readlenght = file->length - file->position;
	if(readlenght == 0)
		return completed;

	// read to buffer
	if(remaining > (FS_BUFFER_SIZE >> 1))
	{
		if(readlenght > remaining)
			readlenght = remaining;

		readresult = (off_t)fread(&buf[completed], 1, readlenght, file->handle);

		completed += readresult;
		file->position += readresult;

		// cache invalidation
		file->buffer.position = 0;
		file->buffer.length = 0;
	}
	else
	{
		if(readlenght > FS_BUFFER_SIZE)
			readlenght = FS_BUFFER_SIZE;

		readresult = (off_t)fread(file->buffer.ptr, 1, readlenght, file->handle);

		if(readresult > 0)
		{
			readlenght = (remaining > readresult) ? readresult : remaining;

			memcpy(&buf[completed], file->buffer.ptr, readlenght);

			completed += readlenght;
			file->position += readresult;

			// cache update
			file->buffer.position = readlenght;
			file->buffer.length = readresult;
		}

	}

	return completed;
}

/*
=================
FS_FWrite

Writes data from the array to a file.
=================
*/
off_t FS_FWrite (file_t *file, const void *buffer, size_t size)
{
	off_t	completed;
	const byte	*buf;

	if(!file || size == 0)
		return 0;

	if(!(file->flags & FS_TYPE_RFS))
		return 0;

	buf = (const byte *)buffer;

	// seek to current position
	if(file->buffer.position != file->buffer.length)
	{
		file->position += file->buffer.position - file->buffer.length;
		fseek(file->handle, file->position, SEEK_SET);
	}

	// cache invalidation
	file->buffer.position = 0;
	file->buffer.length = 0;

	// write
	completed = (off_t)fwrite(buffer, 1, size, file->handle);
	if(completed > 0)
	{
		file->position += completed;

		if(file->length < file->position)
			file->length = file->position;
	}

	if(file->flags & FS_FLAG_FLUSH)
		fflush(file->handle);

	return completed;
}

/*
====================
FS_FPrintf

Sends formatted output to a file
====================
*/
int FS_FPrintf (file_t *file, const char *format, ...)
{
	va_list		argptr;
	static char	outbuff[MAX_PRINT_MSG];
	int			lenght;

	if( !file )
		return 0;

	va_start (argptr, format);
	lenght = vsnprintf(outbuff, sizeof(outbuff), format, argptr);
	va_end (argptr);

	if(lenght >= sizeof(outbuff))
		return -1;

	return FS_FWrite (file, outbuff, lenght);
}

/*
=================
FS_FSeek

Sets the file position indicator
=================
*/
int FS_FSeek (file_t *file, off_t offset, int whence)
{
	switch(whence)
	{
	case FS_SEEK_SET:
		break;
	case FS_SEEK_CUR:
		offset += file->position - file->buffer.length + file->buffer.position;
		break;
	case FS_SEEK_END:
		offset += file->length;
		break;
	default:
		return -1;
	}

	if(offset < 0 || offset > file->length)
		return -1;

	// check cache
	if(file->position - file->buffer.length <= offset && offset <= file->position)
	{
		file->buffer.position = offset + file->buffer.length - file->position;
		return 0;
	}

	// cache invalidation
	file->buffer.position = 0;
	file->buffer.length = 0;

	if(file->position == offset)
		return 0;

	if(fseek(file->handle, file->offset + offset, SEEK_SET) != 0)
		return -1;

	file->position = offset;

	return 0;
}

/*
====================
FS_FTell

Gets the file position indicator
====================
*/
off_t FS_FTell (file_t *file)
{
	if( !file )
		return 0;
	return file->position - file->buffer.length + file->buffer.position;
}

/*
================
FS_FCopy
================
*/
qboolean FS_FCopy (file_t *outfile, file_t *infile)
{
	size_t		size;
	off_t		completed, remaining;
	byte		*buffer;
	qboolean	result;

	if(!outfile || !infile)
		return false;

	result = true;

	remaining = infile->length;
	buffer = Z_Malloc(FS_COPY_SIZE);

	while(remaining)
	{
		size = (remaining > FS_COPY_SIZE) ? FS_COPY_SIZE : remaining;

		completed = FS_FRead(infile, buffer, size);
		if(completed != size)
		{
			result = false;
			break;
		}

		completed = FS_FWrite(outfile, buffer, size);
		if(completed != size)
		{
			result = false;
			break;
		}

		remaining -= completed;
	}

	Z_Free(buffer);
	return result;
}

/*
==============
FS_FClose

For some reason, other dll's can't just cal fclose()
on files returned by FS_FOpen...
==============
*/
void FS_FClose (file_t *file)
{
	if(!file)
		return;

	if(file->handle)
		fclose (file->handle);

	Z_Free(file);
}

/*
============
FS_LoadFile

Filename are reletive to the quake search path
============
*/
byte *FS_LoadFile (const char *path, size_t *size, int flags)
{
	file_t	*file;
	byte	*buffer;
	size_t	buffersize;

	buffersize = 0;
	buffer = NULL;	// quiet compiler warning

	// look for it in the filesystem or pack files
	file = FS_FOpen (path, FS_MODE_READ | (flags & (FS_PATH_MASK | FS_TYPE_MASK)));
	if (file)
	{
		buffersize = file->length;

		if(flags & FS_FLAG_NTER)
			buffersize++;

#if 0
		if(flags & FS_FLAG_HUNK)
			Com_DPrintf("FS_LoadFile: Hunk_AllocName (%i, %s)\n", buffersize, path);
		else if(flags & FS_FLAG_TEMP)
			Com_DPrintf("FS_LoadFile: Hunk_TempAlloc (%i, %s)\n", buffersize, path);
		else
			Com_DPrintf("FS_LoadFile: Z_Malloc (%i, %s)\n", buffersize, path);
#endif

		if(flags & FS_FLAG_HUNK)
			buffer = Hunk_AllocName(buffersize, path);
		else if(flags & FS_FLAG_TEMP)
			buffer = Hunk_TempAlloc(buffersize);
		else
			buffer = Z_Malloc(buffersize);

		if (!buffer)
			Com_Error (ERR_FATAL, "FS_LoadFile: not enough space for %s", path);

		if(flags & FS_FLAG_NTER)
		{
			buffer[buffersize] = 0;
			buffersize--;
		}

		FS_FRead (file, buffer, buffersize);
		FS_FClose (file);
	}

	if(size)
		*size = buffersize;

	return buffer;
}


/*
=============
FS_FreeFile
=============
*/
void FS_FreeFile (void *buffer)
{
	Z_Free (buffer);
}

/*
============
FS_WriteFile

Filename are reletive to the quake search path
============
*/
qboolean FS_WriteFile(const char *path, const void *buffer, size_t len, int flags)
{
	file_t *file;

	file = FS_FOpen(path, FS_MODE_WRITE | (flags & FS_PATH_MASK));
	if(!file)
	{
		Com_DPrintf("FS_WriteFile: unable to write file %s\n", path);
		return false;
	}

	FS_FWrite(file, buffer, len);
	FS_FClose(file);

	return true;
}

/*
=================
FS_LoadPackFile

Takes an explicit (not game tree related) path to a pak file.

Loads the header and directory, adding the files at the beginning
of the list so they override previous pack files.
=================
*/
pack_t *FS_LoadPackFile (char *packfile)
{
	dpackheader_t	header;
	int				i;
	dpackfile_t		*packfiles;
	int				numpackfiles;
	pack_t			*pack;
	FILE			*packhandle;
	unsigned		checksum;

	packhandle = fopen(packfile, "rb");
	if (!packhandle)
		return NULL;

	fread (&header, 1, sizeof(header), packhandle);
	if (LittleLong(header.ident) != IDPAKHEADER)
		Com_Error (ERR_FATAL, "%s is not a packfile", packfile);

	header.dirofs = LittleLong (header.dirofs);
	header.dirlen = LittleLong (header.dirlen);

	numpackfiles = header.dirlen / sizeof(dpackfile_t);

	if (numpackfiles > MAX_FILES_IN_PACK)
		Com_Error (ERR_FATAL, "%s has %i files", packfile, numpackfiles);

	packfiles = Hunk_AllocName (numpackfiles * sizeof(dpackfile_t), "packfile");

	fseek (packhandle, header.dirofs, SEEK_SET);
	fread (packfiles, numpackfiles, sizeof(dpackfile_t), packhandle);

#ifdef NO_ADDONS
// crc the directory to check for modifications
	checksum = Com_BlockChecksum ((void *)packfiles, header.dirlen);

	if (checksum != PAK0_CHECKSUM)
		return NULL;
#endif

// parse the directory
	for (i = 0; i < numpackfiles; i++)
	{
		packfiles[i].filepos = LittleLong(packfiles[i].filepos);
		packfiles[i].filelen = LittleLong(packfiles[i].filelen);
	}

	pack = Hunk_Alloc (sizeof (pack_t));
	strcpy (pack->filename, packfile);
	pack->numfiles = numpackfiles;
	pack->files = packfiles;

	fclose(packhandle);

	Com_Printf ("Added packfile %s (%i files)\n", packfile, numpackfiles);
	return pack;
}


/*
================
FS_AddGameDirectory

Sets fs_gamedir, adds the directory to the head of the path,
then loads and adds pak1.pak pak2.pak ...
================
*/
void FS_AddGameDirectory (char *dir, int flags)
{
	int				i;
	searchpath_t	*search;
	pack_t			*pak;
	char			pakfile[MAX_OSPATH];

	if(flags & FS_PATH_BASEDIR)
		strcpy (fs_basedir, dir);

	if(flags & FS_PATH_GAMEDIR)
		strcpy (fs_gamedir, dir);

	//
	// add the directory to the search path
	//
	search = Hunk_Alloc(sizeof(searchpath_t));
	strcpy (search->filename, dir);
	search->flags = flags | FS_TYPE_RFS;
	search->next = fs_searchpaths;
	fs_searchpaths = search;

	//
	// add any pak files in the format pak0.pak pak1.pak, ...
	//
	for (i = 0; i < 10; i++)
	{
		Com_sprintf (pakfile, sizeof(pakfile), "%s/pak%i.pak", dir, i);
		pak = FS_LoadPackFile (pakfile);
		if (!pak)
			continue;
		search = Hunk_Alloc(sizeof(searchpath_t));
		search->pack = pak;
		search->flags = flags | FS_TYPE_PAK;
		search->next = fs_searchpaths;
		fs_searchpaths = search;
	}
}

/*
==================
FS_WriteDir

Called to find where to write a file (demos, savegames, etc)
==================
*/
const char *FS_WriteDir(int flags)
{
	if(flags & FS_PATH_ROOTDIR)
		return fs_rootdir;

	if(flags & FS_PATH_BASEDIR)
		return fs_basedir;

	return fs_gamedir; // default
}

/*
=============
FS_ExecAutoexec
=============
*/
void FS_ExecAutoexec (void)
{
	char *dir;
	char name [MAX_QPATH];

	Com_sprintf(name, sizeof(name), "%s/%s/autoexec.cfg", fs_gamedir, dir);
	if (Sys_FindFirst(name, 0, SFF_SUBDIR | SFF_HIDDEN | SFF_SYSTEM))
		Cbuf_AddText ("exec autoexec.cfg\n");
	Sys_FindClose();
}


/*
================
FS_SetGamedir

Sets the gamedir and path to a different directory.
================
*/
void FS_SetGamedir (char *dir)
{
	searchpath_t	*next;

	if (strstr(dir, "..") || strstr(dir, "/")
		|| strstr(dir, "\\") || strstr(dir, ":") )
	{
		Com_Printf ("Gamedir should be a single filename, not a path\n");
		return;
	}
#if 0
	//
	// free up any current game dir info
	//
	while (fs_searchpaths != fs_base_searchpaths)
	{
		if (fs_searchpaths->pack)
		{
			Z_Free (fs_searchpaths->pack->files);
			Z_Free (fs_searchpaths->pack);
		}
		next = fs_searchpaths->next;
		Z_Free (fs_searchpaths);
		fs_searchpaths = next;
	}
#endif
	//
	// flush all data, so it will be forced to reload
	//
	if (dedicated && !dedicated->value)
		Cbuf_AddText ("vid_restart\nsnd_restart\n");

	if (!strcmp(dir, BASEDIRNAME) || (*dir == 0))
	{
		Cvar_FullSet ("gamedir", "", CVAR_SERVERINFO|CVAR_NOSET);
		Cvar_FullSet ("game", "", CVAR_LATCH|CVAR_SERVERINFO);

		strcpy(fs_gamedir, fs_basedir);
	}
	else
	{
		Cvar_FullSet ("gamedir", dir, CVAR_SERVERINFO|CVAR_NOSET);
		FS_AddGameDirectory (va("%s/%s", fs_rootdir, dir), FS_PATH_GAMEDIR);
	}
}


/*
================
FS_Link_f

Creates a filelink_t
================
*/
void FS_Link_f (void)
{
	filelink_t	*l, **prev;

	if (Cmd_Argc() != 3)
	{
		Com_Printf ("USAGE: link <from> <to>\n");
		return;
	}

	// see if the link already exists
	prev = &fs_links;
	for (l = fs_links; l; l = l->next)
	{
		if (!strcmp (l->from, Cmd_Argv(1)))
		{
			Z_Free (l->to);
			if (!strlen(Cmd_Argv(2)))
			{	// delete it
				*prev = l->next;
				Z_Free (l->from);
				Z_Free (l);
				return;
			}
			l->to = CopyString (Cmd_Argv(2));
			l->tolength = strlen(l->to);
			return;
		}
		prev = &l->next;
	}

	// create a new link
	l = Z_Malloc(sizeof(*l));
	l->next = fs_links;
	fs_links = l;
	l->from = CopyString(Cmd_Argv(1));
	l->fromlength = strlen(l->from);
	l->to = CopyString(Cmd_Argv(2));
	l->tolength = strlen(l->to);
}

/*
** FS_ListFiles
*/
char **FS_ListFiles( char *findname, int *numfiles, unsigned musthave, unsigned canthave )
{
	char *s;
	int nfiles = 0;
	char **list = 0;

	s = Sys_FindFirst( findname, musthave, canthave );
	while ( s )
	{
		if ( s[strlen(s)-1] != '.' )
			nfiles++;
		s = Sys_FindNext( musthave, canthave );
	}
	Sys_FindClose ();

	if ( !nfiles )
		return NULL;

	nfiles++; // add space for a guard
	*numfiles = nfiles;

	list = malloc( sizeof( char * ) * nfiles );
	memset( list, 0, sizeof( char * ) * nfiles );

	s = Sys_FindFirst( findname, musthave, canthave );
	nfiles = 0;
	while ( s )
	{
		if ( s[strlen(s)-1] != '.' )
		{
			list[nfiles] = strdup( s );
#ifdef _WIN32
			strlwr( list[nfiles] );
#endif
			nfiles++;
		}
		s = Sys_FindNext( musthave, canthave );
	}
	Sys_FindClose ();

	return list;
}

/*
** FS_Dir_f
*/
void FS_Dir_f( void )
{
	char	*path = NULL;
	char	findname[1024];
	char	wildcard[1024] = "*.*";
	char	**dirnames;
	int		ndirs;

	if ( Cmd_Argc() != 1 )
	{
		strcpy( wildcard, Cmd_Argv( 1 ) );
	}

	while ( ( path = FS_NextPath( path ) ) != NULL )
	{
		char *tmp = findname;

		Com_sprintf( findname, sizeof(findname), "%s/%s", path, wildcard );

		while ( *tmp != 0 )
		{
			if ( *tmp == '\\' )
				*tmp = '/';
			tmp++;
		}
		Com_Printf( "Directory of %s\n", findname );
		Com_Printf( "----\n" );

		if ( ( dirnames = FS_ListFiles( findname, &ndirs, 0, 0 ) ) != 0 )
		{
			int i;

			for ( i = 0; i < ndirs-1; i++ )
			{
				if ( strrchr( dirnames[i], '/' ) )
					Com_Printf( "%s\n", strrchr( dirnames[i], '/' ) + 1 );
				else
					Com_Printf( "%s\n", dirnames[i] );

				free( dirnames[i] );
			}
			free( dirnames );
		}
		Com_Printf( "\n" );
	};
}

/*
============
FS_Path_f

============
*/
void FS_Path_f (void)
{
	searchpath_t	*s;
	filelink_t		*l;

	Com_Printf ("Current search path:\n");
	for (s = fs_searchpaths; s; s = s->next)
	{
		if (s == fs_base_searchpaths)
			Com_Printf ("----------\n");
		if (s->pack)
			Com_Printf ("%s (%i files)", s->pack->filename, s->pack->numfiles);
		else
			Com_Printf ("%s", s->filename);

		if( s->flags & FS_PATH_BASEDIR   ) Com_Printf( "\x01 [BASE]" );
		if( s->flags & FS_PATH_GAMEDIR   ) Com_Printf( "\x01 [GAME]" );
		if( s->flags & FS_PATH_CUSTOMDIR ) Com_Printf( "\x01 [CUST]" );

		Com_Printf ("\n");
	}

	for (l = fs_links ; l; l = l->next)
	{
		if(l == fs_links)
			Com_Printf ("\nLinks:\n");
		Com_Printf ("%s : %s\n", l->from, l->to);
	}
}

/*
================
FS_NextPath

Allows enumerating all of the directories in the search path
================
*/
char *FS_NextPath (char *prevpath)
{
	searchpath_t	*s;
	char			*prev;

	if (!prevpath)
		return fs_gamedir;

	prev = fs_gamedir;
	for (s=fs_searchpaths ; s ; s=s->next)
	{
		if (s->pack)
			continue;
		if (prevpath == prev)
			return s->filename;
		prev = s->filename;
	}

	return NULL;
}


/*
================
FS_InitFilesystem
================
*/
void FS_InitFilesystem (void)
{
	int baseflags;

	Cmd_AddCommand ("path", FS_Path_f);
	Cmd_AddCommand ("link", FS_Link_f);
	Cmd_AddCommand ("dir", FS_Dir_f );

	// allows the game to run from outside the data tree
	fs_rootdirvar      = Cvar_Get ("rootdir",  ".",    CVAR_NOSET);
	// Logically concatenates the custom dir after the basedir for
	// allows the game to run from outside the data tree
	fs_customdirvar    = Cvar_Get ("customdir",	"",     CVAR_NOSET);
	// game override
	fs_gamedirvar      = Cvar_Get ("game",     "",     CVAR_LATCH | CVAR_SERVERINFO);

	// set root dir
	if (fs_rootdirvar->string[0])
		strcpy (fs_rootdir, fs_rootdirvar->string);
	else
		strcpy (fs_rootdir, ".");

	if (fs_customdirvar->string[0])
		FS_AddGameDirectory (va("%s/%s", fs_rootdir, fs_customdirvar->string), FS_PATH_CUSTOMDIR);

	baseflags = FS_PATH_BASEDIR;
	if (!fs_gamedirvar->string[0])
		baseflags |= FS_PATH_GAMEDIR;

	// start up with baseq2 by default
	FS_AddGameDirectory (va("%s/"BASEDIRNAME, fs_rootdir), baseflags);

	// any set gamedirs will be freed up to here
	fs_base_searchpaths = fs_searchpaths;

	// check for game override
	if (fs_gamedirvar->string[0])
		FS_SetGamedir (fs_gamedirvar->string);
}



