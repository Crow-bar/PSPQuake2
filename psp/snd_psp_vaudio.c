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
// snd_psp_vaudio.c

#include <pspkernel.h>
#include <psputility.h>
#include <pspvaudio.h>
#include <pspdmac.h>

#include <malloc.h>

#include "../client/client.h"
#include "../client/snd_loc.h"

#define	PSP_NUM_AUDIO_SAMPLES	1024 // must be multiple of 64
#define PSP_OUTPUT_CHANNELS		2

#define SND_DMA_SAMPLES			16384

#define SND_FLAG_SEMA			0x00000001
#define SND_FLAG_THREAD			0x00000002
#define SND_FLAG_VA_MOD			0x00000004
#define SND_FLAG_VA_CH			0x00000008
#define SND_FLAG_INIT			0x00000010
#define SND_FLAG_RUN			0x00000020

static struct
{
	volatile int	flags;
	SceUID			thread;
	SceUID			sema;
	struct
	{
		int		channels;
		int		samples;
		int		samplerate;
		int		current;
		size_t	len;
		byte	*ptr[2];
	} buffer;
} snd;

cvar_t	*snd_eq;
cvar_t	*snd_alc;

//=============================================================================

/*
==================
SNDDMA_MainThread
==================
*/
static int SNDDMA_MainThread (SceSize args, void *argp)
{
	int	size, pos;
	int wrapped, remaining;

	size = dma.samples << 1;

	while (snd.flags & SND_FLAG_RUN)
	{
		sceKernelWaitSema (snd.sema, 1, NULL);

		pos     = dma.samplepos << 1;
		wrapped = pos + snd.buffer.len - size;

		if (wrapped < 0)
		{
			memcpy (snd.buffer.ptr[snd.buffer.current], dma.buffer + pos, snd.buffer.len);
			dma.samplepos += snd.buffer.len >> 1;
		}
		else
		{
			remaining = size - pos;
			memcpy (snd.buffer.ptr[snd.buffer.current], dma.buffer + pos, remaining);
			if (wrapped > 0)
				memcpy (&snd.buffer.ptr[snd.buffer.current][remaining], dma.buffer, wrapped);
			dma.samplepos = wrapped >> 1;
		}

		sceKernelSignalSema (snd.sema, 1);

		sceVaudioOutputBlocking (PSP_VAUDIO_VOLUME_MAX, snd.buffer.ptr[snd.buffer.current]);
		snd.buffer.current ^= 1;
	}

	sceKernelExitThread(0);
	return 0;
}

/*
==================
SNDDMA_Init
==================
*/
qboolean SNDDMA_Init(void)
{
	int	ret;

	if(snd.flags & SND_FLAG_INIT)
		return true;

	snd_eq = Cvar_Get("snd_eq", "0", CVAR_ARCHIVE); // 0 - off
	snd_alc = Cvar_Get("snd_alc", "0", CVAR_ARCHIVE); // 0 - off

	ret = sceUtilityLoadModule (PSP_MODULE_AV_VAUDIO);
	if (ret < 0)
	{
		Com_Printf("SNDDMA_Init: PSP_MODULE_AV_VAUDIO (0x%x)\n", ret);
		return false;
	}
	snd.flags |= SND_FLAG_VA_MOD;

	switch ((int)s_khz->value)
	{
	case 48:
		snd.buffer.samplerate = 48000;
		break;
	case 44:
		snd.buffer.samplerate = 44100;
		break;
	case 32:
		snd.buffer.samplerate = 32000;
		break;
	case 24:
		snd.buffer.samplerate = 24000;
		break;
	case 22:
		snd.buffer.samplerate = 22050;
		break;
	case 16:
		snd.buffer.samplerate = 16000;
		break;
	case 12:
		snd.buffer.samplerate = 12000;
		break;
	case 11:
		snd.buffer.samplerate = 11025;
		break;
	case 8:
		snd.buffer.samplerate = 8000;
		break;
	default:
		snd.buffer.samplerate = 22050;
		Com_Printf("Don't currently support %i kHz sample rate.  Using %i.\n",
			(int)s_khz->value, (int)(snd.buffer.samplerate / 1000));
		break;
	}

	// set external output buffer
	snd.buffer.channels     = PSP_OUTPUT_CHANNELS;
	snd.buffer.samples      = PSP_NUM_AUDIO_SAMPLES;
	snd.buffer.current      = 0;
	snd.buffer.len          = snd.buffer.samples * snd.buffer.channels * 2; // always 16 bit
	snd.buffer.ptr[0]       = memalign (64, snd.buffer.len * 2); // double buffering
	if (!snd.buffer.ptr[0])
	{
		SNDDMA_Shutdown ();
		return false;
	}
	snd.buffer.ptr[1]       = &snd.buffer.ptr[0][snd.buffer.len];

	if ((int)s_loadas8bit->value)
		dma.width = 1;
	else
		dma.width = 2;

	if (dma.width != 2)
	{
		Com_Printf ("Don't currently support %i-bit data. Forcing 16-bit.\n", dma.width * 8);
		dma.width = 2;
		Cvar_SetValue ("s_loadas8bit", false);
	}

	// set internal output buffer
	dma.speed               = snd.buffer.samplerate;
	dma.channels            = snd.buffer.channels;
	dma.samples             = SND_DMA_SAMPLES * dma.channels;
	dma.samplepos           = 0;
	dma.submission_chunk    = 1;
	dma.buffer              = memalign (64, dma.samples * 2); // always 16 bit
	if (!dma.buffer)
	{
		SNDDMA_Shutdown ();
		return false;
	}

	// clearing buffers
	memset(dma.buffer, 0, dma.samples * 2);
	memset(snd.buffer.ptr[0], 0, snd.buffer.len * 2);

	// allocate and initialize output channel
	ret = sceVaudioChReserve (snd.buffer.samples, snd.buffer.samples, snd.buffer.channels);
	if (ret)
	{
		Com_Printf ("SNDDMA_Init: sceVaudioChReserve (0x%x)\n", ret);
		SNDDMA_Shutdown ();
		return false;
	}
	snd.flags |= SND_FLAG_VA_CH;

	sceVaudioSetEffectType ((int)snd_eq->value, PSP_VAUDIO_VOLUME_MAX);
	sceVaudioSetAlcMode ((int)snd_alc->value);

	snd_eq->modified = false;
	snd_alc->modified = false;

	// create semaphore
	snd.sema = sceKernelCreateSema ("sound_sema", 0, 1, 255, NULL);
	if (snd.sema <= 0)
	{
		Com_Printf ("SNDDMA_Init: sceKernelCreateSema (0x%x)\n", snd.sema);
		SNDDMA_Shutdown ();
		return false;
	}
	snd.flags |= SND_FLAG_SEMA;

	// create audio thread
	snd.thread = sceKernelCreateThread ("sound_thread", SNDDMA_MainThread, 0x12, 0x8000, 0, 0);
	if (snd.thread < 0)
	{
		Com_Printf ("SNDDMA_Init: sceKernelCreateThread (0x%x)\n", snd.thread);
		SNDDMA_Shutdown ();
		return false;
	}
	snd.flags |= SND_FLAG_THREAD | SND_FLAG_RUN;

	// start audio thread
	ret = sceKernelStartThread (snd.thread, 0, 0);
	if (ret < 0)
	{
		Com_Printf ("SNDDMA_Init: sceKernelStartThread (0x%x)\n", ret);
		snd.flags &= ~SND_FLAG_RUN;
		SNDDMA_Shutdown ();
		return false;
	}

	Com_Printf ("Using PSP Vaudio driver: %d Hz\n", dma.speed);

	snd.flags |= SND_FLAG_INIT;

	return true;
}

/*
==================
SNDDMA_GetDMAPos
==================
*/
int	SNDDMA_GetDMAPos(void)
{
	return dma.samplepos;
}

/*
==================
SNDDMA_Shutdown
==================
*/
void SNDDMA_Shutdown(void)
{
	Com_Printf ("Shutting down Vaudio.\n");

	if (snd.flags & SND_FLAG_THREAD)
	{
		if (snd.flags & SND_FLAG_RUN)
		{
			snd.flags &= ~SND_FLAG_RUN;
			sceKernelWaitThreadEnd(snd.thread, NULL);
		}
		sceKernelDeleteThread(snd.thread);
		snd.thread = -1;
		snd.flags &= ~SND_FLAG_THREAD;
	}

	if (snd.flags & SND_FLAG_SEMA)
	{
		sceKernelDeleteSema (snd.sema);
		snd.sema = -1;
		snd.flags &= ~SND_FLAG_SEMA;
	}

	if (snd.flags & SND_FLAG_VA_CH)
	{
		sceVaudioChRelease ();
		snd.flags &= ~SND_FLAG_VA_CH;
	}

	if (snd.flags & SND_FLAG_VA_MOD)
	{
		sceUtilityUnloadModule (PSP_MODULE_AV_VAUDIO);
		snd.flags &= ~SND_FLAG_VA_MOD;
	}

	if (snd.buffer.ptr[0])
	{
		free (snd.buffer.ptr[0]);
		snd.buffer.ptr[0] = NULL;
		snd.buffer.ptr[1] = NULL;
	}

	if (dma.buffer)
	{
		free (dma.buffer);
		dma.buffer = NULL;
	}

	snd.flags &= ~SND_FLAG_INIT;
}

/*
==================
SNDDMA_BeginPainting
==================
*/
void SNDDMA_BeginPainting (void)
{
	if (snd_eq->modified)
	{
		sceVaudioSetEffectType ((int)snd_eq->value, PSP_VAUDIO_VOLUME_MAX);
		snd_eq->modified = false;
	}

	if (snd_alc->modified)
	{
		sceVaudioSetAlcMode ((int)snd_alc->value);
		snd_alc->modified = false;
	}

	if (snd.flags & SND_FLAG_SEMA)
		sceKernelWaitSema (snd.sema, 1, NULL);
}

/*
==================
SNDDMA_BeginPainting
==================
*/
void SNDDMA_Submit(void)
{
	if (snd.flags & SND_FLAG_SEMA)
		sceKernelSignalSema (snd.sema, 1);
}
