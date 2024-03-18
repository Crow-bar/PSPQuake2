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
#include <pspkernel.h>
#include <pspsdk.h>
#include <pspaudio.h>
#include <psputility.h>
#include <pspaudiocodec.h>
#include <pspmp3.h>

#include "../client/client.h"

#define MP3_ID3V1_ID			"TAG"
#define MP3_ID3V1_ID_SZ			3
#define MP3_ID3V1_SZ			128

#define MP3_ID3V2_ID			"ID3"
#define MP3_ID3V2_ID_SZ			3
#define MP3_ID3V2_SIZE_OFF		6
#define MP3_ID3V2_SIZE_SZ		4
#define MP3_ID3V2_HEADER_SZ		10

#define	MP3_ERRORS_MAX			3

#define MP3_FLAG_RES_THREAD			0x00000001
#define MP3_FLAG_RES_AVC_MOD		0x00000002
#define MP3_FLAG_RES_MP3_MOD		0x00000004
#define MP3_FLAG_RES_MP3_RES		0x00000008
#define MP3_FLAG_RES_MP3_FILE		0x00000010
#define MP3_FLAG_RES_MP3_HANDLE		0x00000020
#define MP3_FLAG_RES_PCM_CH			0x00000040

#define MP3_FLAG_STATE_INIT			0x00000100
#define MP3_FLAG_STATE_RUN			0x00000200
#define MP3_FLAG_STATE_END			0x00000400

#define MP3_FLAG_LOOP				0x00001000
#define MP3_FLAG_PAUSE				0x00002000


static SceUChar8 mp3buf[32*1024] __attribute__((aligned(64)));
static SceUChar8 pcmbuf[32*1152] __attribute__((aligned(64)));

static struct
{
	volatile int	flags;
	SceUID			thread;
	SceUID 			file;
	int				track;
	int				handle;
	int				poscache;
	struct
	{
		volatile int	volume;
		int				channels;
		int				samples;
		int				samplerate;
	} pcm;
} mp3;

cvar_t	*cd_volume;
cvar_t	*cd_nocd;

void CDAudio_Pause (void);
void CDAudio_Resume (void);

//=============================================================================

/*
=================
Sound_GetID3V2SizeMPG
=================
*/
static inline int CDAudio_GetID3V2Size (const byte *tagsize)
{
	int		size;
	byte	*sizeptr = (byte*)&size;

	// 7 bit per byte, invert endian
	sizeptr[0] = ((tagsize[3] >> 0) & 0x7f) | ((tagsize[2] & 0x01) << 7);
	sizeptr[1] = ((tagsize[2] >> 1) & 0x3f) | ((tagsize[1] & 0x03) << 6);
	sizeptr[2] = ((tagsize[1] >> 2) & 0x1f) | ((tagsize[0] & 0x07) << 5);
	sizeptr[3] = ((tagsize[0] >> 3) & 0x0f);

	return size;
}

/*
=================
CDAudio_FindHead
=================
*/
int CDAudio_FindHead (int file)
{
	int		result;
	byte	tagid[MP3_ID3V2_ID_SZ];
	byte	tagsize[MP3_ID3V2_SIZE_SZ];

	result = 0;

	if (sceIoLseek32 (file, 0, SEEK_SET) < 0)
	{
		Com_Printf ("CDAudio_FindHead: ID3V2 ID read error\n");
		return 0;
	}

	if (sceIoRead (file, tagid,  MP3_ID3V2_ID_SZ) < MP3_ID3V2_ID_SZ)
	{
		Com_Printf ("CDAudio_FindHead: ID3V2 ID read error\n");
		return 0;
	}

	if (!memcmp (tagid, MP3_ID3V2_ID, MP3_ID3V2_ID_SZ))
	{
		sceIoLseek32 (file, MP3_ID3V2_SIZE_OFF, SEEK_SET);
		if (sceIoRead (file, tagsize, MP3_ID3V2_SIZE_SZ) < MP3_ID3V2_SIZE_SZ)
			Com_Printf ("CDAudio_FindHead: ID3V2 SIZE read error\n");
		else result = CDAudio_GetID3V2Size (tagsize) + MP3_ID3V2_HEADER_SZ;
	}

	return result;
}

/*
=================
CDAudio_FindTail
=================
*/
int CDAudio_FindTail (int file)
{
	int		result;
	byte	tagid[MP3_ID3V1_ID_SZ];

	result = sceIoLseek32 (file, 0, SEEK_END);
	if (result < 0)
	{
		Com_Printf ("CDAudio_FindTail: ID3V1 ID read error\n");
		return 0;
	}

	if (sceIoLseek32 (file, result - MP3_ID3V1_SZ, SEEK_SET) < 0)
	{
		Com_Printf ("CDAudio_FindTail: ID3V1 ID read error\n");
		return 0;
	}

	if (sceIoRead (file, tagid,  MP3_ID3V1_ID_SZ) < MP3_ID3V1_ID_SZ)
	{
		Com_Printf ("CDAudio_FindTail: ID3V1 ID read error\n");
		return 0;
	}

	if (!memcmp(tagid, MP3_ID3V1_ID, MP3_ID3V1_ID_SZ))
		result -= MP3_ID3V1_SZ;

	return result;
}

/*
==================
CDAudio_FillBuffer
==================
*/
int CDAudio_FillBuffer (void)
{
	int		ret;
	byte	*dstptr;
	int		dstsize;
	int		dstpos;
	int		readsize;

	if (sceMp3CheckStreamDataNeeded (mp3.handle) <= 0)
		return 0;

	// get Info on the stream (where to fill to, how much to fill, where to fill from)
	ret = sceMp3GetInfoToAddStreamData (mp3.handle, &dstptr, &dstsize, &dstpos);
	if (ret < 0)
	{
		//Com_DPrintf ("CDAudio_FillBuffer: sceMp3GetInfoToAddStreamData (0x%x)\n", ret);
		return ret;
	}

	// seek file to position requested
	if (mp3.poscache != dstpos)
	{
		ret = sceIoLseek32 (mp3.file, dstpos, SEEK_SET);
		if (ret < 0)
			return ret;
		mp3.poscache = dstpos;
	}

	// read the amount of data
	readsize = sceIoRead (mp3.file, dstptr, dstsize);
	mp3.poscache += readsize;

	// notify mp3 library about how much we really wrote to the stream buffer
	ret = sceMp3NotifyAddStreamData (mp3.handle, readsize);
	//if (ret < 0)
	//	Com_DPrintf ("CDAudio_FillBuffer: sceMp3NotifyAddStreamData (0x%x)\n", ret);

	return ret;
}

/*
==================
CDAudio_MainThread
==================
*/
static int CDAudio_MainThread (SceSize args, void *argp)
{
	int		ret, errdec;
	short	*pcmdata;

	mp3.flags &= ~MP3_FLAG_STATE_END;

	while (mp3.flags & MP3_FLAG_STATE_RUN)
	{
		// Check if we need to fill our stream buffer
		if (CDAudio_FillBuffer () != 0)
			break;

		for (errdec = 0; errdec < MP3_ERRORS_MAX; errdec++)
		{
			ret = sceMp3Decode (mp3.handle, &pcmdata);
			if (ret >= 0 ) // decoding successful
			{
				break;
			}
			else if (ret == (int)0x80671402) // next frame header
			{
				if (CDAudio_FillBuffer () != 0)
					break;
			}
			else
				break;
		}

		if (ret <= 0)
		{
			//if (ret != 0x80671402 )
			//	Com_DPrintf ("sceMp3Decode returned (0x%x)\n", ret);
			//if (ret == 0)
			mp3.flags |= MP3_FLAG_STATE_END;
			break;
		}

		sceAudioSRCOutputBlocking (mp3.pcm.volume, pcmdata);
	}

	sceAudioSRCOutputBlocking (0, NULL);

	mp3.flags &= ~MP3_FLAG_STATE_RUN;

	sceKernelExitThread(0); // back to DORMANT state
	return 0;
}


/*
==================
CDAudio_Play
==================
*/
void CDAudio_Play (int track, qboolean looping)
{
	int		ret;
	char	path[MAX_OSPATH];
	SceMp3InitArg	mp3init;

	if(!(mp3.flags & MP3_FLAG_STATE_INIT))
		return;

	if (mp3.track == track && (mp3.flags & MP3_FLAG_RES_MP3_FILE))
		return;

	if (track < 1 /*|| track > 99*/)
	{
		Com_DPrintf("CDAudio_Play: Bad track number %u.\n", track);
		return;
	}

	CDAudio_Stop (); // stop previous

	Com_sprintf (path, sizeof(path), "%s/music/Track%02d.mp3", FS_GetWriteDir(FS_PATH_GAMEDIR), track);

	mp3.file = sceIoOpen (path, PSP_O_RDONLY, 0777);
	if (mp3.file < 0)
	{
		Com_DPrintf ("CDAudio_Play: sceIoOpen (0x%x)\n", mp3.file);
		return;
	}
	mp3.flags |= MP3_FLAG_RES_MP3_FILE;

	memset (&mp3init, 0, sizeof(SceMp3InitArg));

	mp3init.mp3StreamStart = CDAudio_FindHead (mp3.file);
	mp3init.mp3StreamEnd = CDAudio_FindTail (mp3.file);
	mp3init.mp3Buf = mp3buf;
	mp3init.mp3BufSize = sizeof(mp3buf);
	mp3init.pcmBuf = pcmbuf;
	mp3init.pcmBufSize = sizeof(pcmbuf);

	mp3.handle = sceMp3ReserveMp3Handle (&mp3init);
	if (mp3.handle < 0)
	{
		Com_DPrintf ("CDAudio_Play: sceMp3ReserveMp3Handle (0x%x)\n", mp3.handle);
		CDAudio_Stop (); // close
		return;
	}
	mp3.flags |= MP3_FLAG_RES_MP3_HANDLE;

	mp3.poscache = -1;

	// Fill the stream buffer with some data so that sceMp3Init has something to work with
	ret = CDAudio_FillBuffer ();
	if (ret != 0)
	{
		Com_DPrintf ("CDAudio_Play: CDAudio_FillBuffer (0x%x)\n", ret);
		CDAudio_Stop (); // close
		return;
	}

	ret = sceMp3Init (mp3.handle);
	if (ret < 0)
	{
		Com_DPrintf ("CDAudio_Play: sceMp3Init (0x%x)\n", ret);
		CDAudio_Stop (); // close
		return;
	}

	ret = sceMp3SetLoopNum (mp3.handle, looping ? -1 : 0);
	if (ret < 0)
		Com_DPrintf ("CDAudio_Play: sceMp3SetLoopNum (0x%x)\n", ret);
	mp3.flags |= (looping) ? MP3_FLAG_LOOP : 0;

	mp3.pcm.samples = sceMp3GetMaxOutputSample (mp3.handle);
	mp3.pcm.samplerate = sceMp3GetSamplingRate (mp3.handle);
	mp3.pcm.volume = cd_volume->value * PSP_AUDIO_VOLUME_MAX;
	if (mp3.pcm.volume > PSP_AUDIO_VOLUME_MAX)
		mp3.pcm.volume = PSP_AUDIO_VOLUME_MAX;
	mp3.pcm.channels = 2; // always 2

	ret = sceAudioSRCChReserve (mp3.pcm.samples, mp3.pcm.samplerate, mp3.pcm.channels);
	if (ret < 0)
	{
		Com_DPrintf ("CDAudio_Play: sceAudioSRCChReserve (0x%x)\n", ret);
		CDAudio_Stop (); // close
		return;
	}
	mp3.flags |= MP3_FLAG_RES_PCM_CH;

	mp3.track = track;

	CDAudio_Resume (); // start playing
}


/*
==================
CDAudio_Stop
==================
*/
void CDAudio_Stop (void)
{
	CDAudio_Pause ();

	mp3.flags &= ~MP3_FLAG_LOOP;
	mp3.flags &= ~MP3_FLAG_PAUSE;

	if (mp3.flags & MP3_FLAG_RES_MP3_HANDLE)
	{
		sceMp3ReleaseMp3Handle (mp3.handle);
		mp3.handle = -1;
		mp3.flags &= ~MP3_FLAG_RES_MP3_HANDLE;
	}

	if (mp3.flags & MP3_FLAG_RES_MP3_FILE)
	{
		sceIoClose (mp3.file);
		mp3.file = -1;
		mp3.flags &= ~MP3_FLAG_RES_MP3_FILE;
	}

	if (mp3.flags & MP3_FLAG_RES_PCM_CH)
	{
		sceAudioSRCChRelease();
		mp3.flags &= ~MP3_FLAG_RES_PCM_CH;
	}
}


/*
==================
CDAudio_Pause
==================
*/
void CDAudio_Pause(void)
{
	if (!(mp3.flags & MP3_FLAG_STATE_INIT))
		return;

	if (!(mp3.flags & MP3_FLAG_STATE_RUN))
		return;

	mp3.flags &= ~MP3_FLAG_STATE_RUN;
	sceKernelWaitThreadEnd (mp3.thread, NULL);

	mp3.flags |= MP3_FLAG_PAUSE;
}


/*
==================
CDAudio_Resume
==================
*/
void CDAudio_Resume (void)
{
	int	ret;

	if (!(mp3.flags & MP3_FLAG_STATE_INIT))
		return;

	if (!(mp3.flags & MP3_FLAG_RES_MP3_HANDLE) || !(mp3.flags & MP3_FLAG_RES_MP3_FILE))
		return;

	if (mp3.flags & MP3_FLAG_STATE_RUN)
		return;

	mp3.flags |= MP3_FLAG_STATE_RUN;

	ret = sceKernelStartThread (mp3.thread, 0, 0);
	if (ret < 0)
	{
		Com_Printf ("CDAudio_Resume: sceKernelStartThread (0x%x)\n", ret);
		mp3.flags &= ~MP3_FLAG_STATE_RUN;
	}

	mp3.flags &= ~MP3_FLAG_PAUSE;
}


/*
==================
CDAudio_Update
==================
*/
void CDAudio_Update (void)
{
	if (cd_nocd->modified)
	{
		if (cd_nocd->value)
			CDAudio_Shutdown ();
		else
			CDAudio_Init ();

		cd_nocd->modified = false;
	}

	if (cd_volume->modified)
	{
		mp3.pcm.volume = cd_volume->value * PSP_AUDIO_VOLUME_MAX;
		if (mp3.pcm.volume > PSP_AUDIO_VOLUME_MAX)
			mp3.pcm.volume = PSP_AUDIO_VOLUME_MAX;

		if (mp3.pcm.volume <= 0)
			CDAudio_Pause ();
		else
			CDAudio_Resume ();

		cd_volume->modified = false;
	}

	if (mp3.flags & MP3_FLAG_STATE_END)
	{
		CDAudio_Stop (); // free
		mp3.flags &= ~MP3_FLAG_STATE_END;
	}
}

/*
==================
CD_f
==================
*/
static void CD_f (void)
{
	char	*command;
	int		ret;
	int		n;

	if (Cmd_Argc() < 2)
		return;

	command = Cmd_Argv (1);

	if (Q_strcasecmp(command, "play") == 0)
	{
		CDAudio_Play((byte)atoi(Cmd_Argv (2)), false);
		return;
	}

	if (Q_strcasecmp(command, "loop") == 0)
	{
		CDAudio_Play((byte)atoi(Cmd_Argv (2)), true);
		return;
	}

	if (Q_strcasecmp(command, "stop") == 0)
	{
		CDAudio_Stop();
		return;
	}

	if (Q_strcasecmp(command, "pause") == 0)
	{
		CDAudio_Pause();
		return;
	}

	if (Q_strcasecmp(command, "resume") == 0)
	{
		CDAudio_Resume();
		return;
	}

	if (Q_strcasecmp(command, "info") == 0)
	{
		Com_Printf("%s %s track %u\n",
			(mp3.flags & MP3_FLAG_PAUSE) ? "Paused" : "Currently",
			(mp3.flags & MP3_FLAG_LOOP) ? "looping" : "playing",
			mp3.track);

		Com_Printf("Volume is %f\n", cd_volume->value);
		return;
	}
}

/*
==================
CDAudio_Init
==================
*/
int CDAudio_Init (void)
{
	int	ret;

	if (mp3.flags & MP3_FLAG_STATE_INIT)
		return 0;

	cd_nocd = Cvar_Get ("cd_nocd", "0", CVAR_ARCHIVE);
	if (cd_nocd->value)
		return 0;

	cd_volume = Cvar_Get ("cd_volume", "0.6", CVAR_ARCHIVE);

	ret = sceUtilityLoadModule (PSP_MODULE_AV_AVCODEC);
	if (ret < 0)
	{
		Com_Printf ("CDAudio_Init: PSP_MODULE_AV_AVCODEC (0x%x)\n", ret);
		return -1;
	}
	mp3.flags |= MP3_FLAG_RES_AVC_MOD;

	ret = sceUtilityLoadModule (PSP_MODULE_AV_MP3);
	if (ret < 0)
	{
		Com_Printf ("CDAudio_Init: PSP_MODULE_AV_MP3 (0x%x)\n", ret);
		return -1;
	}
	mp3.flags |= MP3_FLAG_RES_MP3_MOD;

	ret = sceMp3InitResource ();
	if (ret < 0)
	{
		Com_Printf ("CDAudio_Init: sceMp3InitResource (0x%x)\n", ret);
		return -1;
	}
	mp3.flags |= MP3_FLAG_RES_MP3_RES;

	mp3.thread = sceKernelCreateThread ("mp3decode_thread", CDAudio_MainThread, 0x1e, 0x2000, 0, 0);
	if (mp3.thread < 0)
	{
		Com_Printf ("CDAudio_Init: sceKernelCreateThread (0x%x)\n", ret);
		return -1;
	}
	mp3.flags |= MP3_FLAG_RES_THREAD;

	Cmd_AddCommand ("cd", CD_f);
	Com_Printf ("MP3 Audio Initialized\n");

	mp3.flags |= MP3_FLAG_STATE_INIT;
	return 0;
}


/*
==================
CDAudio_Shutdown
==================
*/
void CDAudio_Shutdown (void)
{
	Com_Printf ("Shutting down MP3 Audio\n");

	CDAudio_Stop ();

	Cmd_RemoveCommand ("cd");

	if (mp3.flags & MP3_FLAG_RES_THREAD)
	{
		sceKernelDeleteThread (mp3.thread);
		mp3.thread = -1;
		mp3.flags &= ~MP3_FLAG_RES_THREAD;
	}

	if (mp3.flags & MP3_FLAG_RES_MP3_RES)
	{
		sceMp3TermResource();
		mp3.flags &= ~MP3_FLAG_RES_MP3_RES;
	}

	if (mp3.flags & MP3_FLAG_RES_MP3_MOD)
	{
		sceUtilityUnloadModule(PSP_MODULE_AV_MP3);
		mp3.flags &= ~MP3_FLAG_RES_MP3_MOD;
	}

	if (mp3.flags & MP3_FLAG_RES_AVC_MOD)
	{
		sceUtilityUnloadModule(PSP_MODULE_AV_AVCODEC);
		mp3.flags &= ~MP3_FLAG_RES_AVC_MOD;
	}

	mp3.flags &= ~MP3_FLAG_STATE_INIT;
}
