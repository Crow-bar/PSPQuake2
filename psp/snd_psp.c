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
// snd_psp.c

#include <pspaudio.h>
#include <pspkernel.h>
#include <pspdmac.h>

#include <malloc.h>

#include "../client/client.h"
#include "../client/snd_loc.h"

#define	PSP_NUM_AUDIO_SAMPLES	1024 // must be multiple of 64
#define PSP_OUTPUT_SAMPLERATE	44100 // only 44100
#define PSP_OUTPUT_CHANNELS		2
#define PSP_OUTPUT_BUFFER_SIZE	((PSP_NUM_AUDIO_SAMPLES) * (PSP_OUTPUT_CHANNELS))

#define SOUND_DMA_SAMPLES		16384

#define SND_FLAG_SEMA			0x00000001
#define SND_FLAG_THREAD			0x00000002
#define SND_FLAG_CH				0x00000004
#define SND_FLAG_INIT			0x00000008
#define SND_FLAG_RUN			0x00000010

static struct
{
	volatile int	flags;
	SceUID			thread;
	SceUID			sema;
	int				channel;
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

//=============================================================================

/*
==================
SNDDMA_CopySamples
==================
*/
static void SNDDMA_CopySamples (byte *dst, int dstpos, int scale, byte *src, int srcpos, int srcsamples, int srcwidth)
{
	int	i, j;

	if (dstscale == 1 && srcwidth == 2)
	{
		memcpy (&((short *)dst)[dstpos], &((short *)src)[srcpos], srcsamples * 2);
	}
	else
	{
		for (i = 0; i < srcsamples; i++)
		{
			for(j = 0; j < scale; j++)
			{
				if (srcwidth == 1)
					((short *)dst)[dstpos + (i * scale + j)] = (src[srcpos + i] - 128) << 8;
				else
					((short *)dst)[dstpos + (i * scale + j)] = ((short *)src)[srcpos + i];
			}
		}
	}
}

/*
==================
SNDDMA_MainThread
==================
*/
static int SNDDMA_MainThread (SceSize args, void *argp)
{
	int	wrapped, remaining;
	int	samplescale, samplesread, sampleswidth;

	samplescale = snd.buffer.samplerate / dma.speed;
	samplesread = (snd.buffer.samples * snd.buffer.channels)  / samplescale;
	sampleswidth = dma.samplebits / 8;

	while (snd.flags & SND_FLAG_RUN)
	{
		sceKernelWaitSema (snd.sema, 1, NULL);

		wrapped = dma.samplepos + samplesread - dma.samples;

		if (wrapped < 0)
		{
			SNDDMA_CopySamples (snd.buffer.ptr[snd.buffer.current], 0, samplescale,
								dma.buffer, dma.samplepos, samplesread, sampleswidth);
			dma.samplepos += samplesread;
		}
		else
		{
			remaining = dma.samples - dma.samplepos;

			SNDDMA_CopySamples (snd.buffer.ptr[snd.buffer.current], 0, samplescale,
								dma.buffer, dma.samplepos, remaining, sampleswidth);

			if (wrapped > 0)
			{
				SNDDMA_CopySamples (snd.buffer.ptr[snd.buffer.current], remaining * samplescale, samplescale,
									dma.buffer, 0, wrapped, sampleswidth);
			}
			dma.samplepos = wrapped;
		}

		sceKernelSignalSema (snd.sema, 1);

		sceAudioOutputBlocking(snd.channel, PSP_AUDIO_VOLUME_MAX, snd.buffer.ptr[snd.buffer.current]);
		snd.buffer.current = !snd.buffer.current;
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

	// set external output buffer
	snd.buffer.samplerate   = PSP_OUTPUT_SAMPLERATE;
	snd.buffer.channels     = PSP_OUTPUT_CHANNELS;
	snd.buffer.samples      = PSP_NUM_AUDIO_SAMPLES;
	snd.buffer.current      = 0;
	snd.buffer.len          = snd.buffer.samples * snd.buffer.channels * 2; // always 16 bit
	snd.buffer.ptr[0]       = memalign (64, snd.buffer.len * 2); // double buffering
	if (!snd.buffer.ptr[0])
		return false;
	snd.buffer.ptr[1]       = &snd.buffer.ptr[0][snd.buffer.len];

	// set internal output buffer
	switch ((int)s_khz->value)
	{
	case 44:
		dma.speed = 44100;
		break;
	case 22:
		dma.speed = 22050;
		break;
	case 11:
		dma.speed = 11025;
		break;
	default:
		dma.speed = 22050;
		Com_Printf("Don't currently support %i kHz sample rate.  Using %i.\n",
			(int)s_khz->value, (int)(dma.speed / 1000));
		break;
	}

	if ((int)s_loadas8bit->value)
		dma.samplebits = 8;
	else
		dma.samplebits = 16;

	dma.channels            = snd.buffer.channels;
	dma.samples             = SOUND_DMA_SAMPLES * PSP_OUTPUT_CHANNELS;
	dma.samplepos           = 0;
	dma.submission_chunk    = 1;
	dma.buffer              = memalign(64, dma.samples * (dma.samplebits / 8));
	if (!dma.buffer)
	{
		SNDDMA_Shutdown ();
		return false;
	}

	// clearing buffers
	memset(dma.buffer, 0, dma.samples * (dma.samplebits / 8));
	memset(snd.buffer.ptr[0], 0, snd.buffer.len * 2);

	// allocate and initialize a hardware output channel
	snd.channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL,
		PSP_NUM_AUDIO_SAMPLES, PSP_AUDIO_FORMAT_STEREO);
	if(snd.channel < 0)
	{
		Com_Printf ("SNDDMA_Init: sceAudioChReserve (0x%x)\n", snd.channel);
		SNDDMA_Shutdown();
		return false;
	}
	snd.flags |= SND_FLAG_CH;

	// create semaphore
	snd.sema = sceKernelCreateSema("sound_sema", 0, 1, 255, NULL);
	if(snd.sema <= 0)
	{
		Com_Printf ("SNDDMA_Init: sceKernelCreateSema (0x%x)\n", snd.sema);
		SNDDMA_Shutdown();
		return false;
	}
	snd.flags |= SND_FLAG_SEMA;

	// create audio thread
	snd.thread = sceKernelCreateThread("sound_thread", SNDDMA_MainThread, 0x12, 0x8000, 0, 0);
	if(snd.thread < 0)
	{
		Com_Printf ("SNDDMA_Init: sceKernelCreateThread (0x%x)\n", snd.thread);
		SNDDMA_Shutdown();
		return false;
	}
	snd.flags |= SND_FLAG_THREAD | SND_FLAG_RUN;

	// start audio thread
	if(sceKernelStartThread( snd.thread, 0, 0 ) < 0)
	{
		Com_Printf ("SNDDMA_Init: sceKernelStartThread (0x%x)\n", ret);
		snd.flags &= ~SND_FLAG_RUN;
		SNDDMA_Shutdown ();
		return false;
	}

	Com_Printf("Using PSP audio driver: %d Hz\n", dma.speed);

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
	Com_Printf("Shutting down audio.\n");

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

	if (snd.flags & SND_FLAG_CH)
	{
		sceAudioChRelease(snd.channel);
		snd.channel = -1;
		snd.flags &= ~SND_FLAG_CH;
	}

	if (snd.buffer.ptr[0])
	{
		free (snd.buffer.ptr[0]);
		snd.buffer.ptr[0] = NULL;
		snd.buffer.ptr[1] = NULL;
	}

	if( dma.buffer )
	{
		free(dma.buffer);
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
	if (snd.flags & SND_FLAG_SEMA)
		sceKernelWaitSema(snd.sema, 1, NULL);
}

/*
==================
SNDDMA_BeginPainting
==================
*/
void SNDDMA_Submit(void)
{
	if (snd.flags & SND_FLAG_SEMA)
		sceKernelSignalSema(snd.sema, 1);
}
