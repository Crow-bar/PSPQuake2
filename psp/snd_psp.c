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

typedef void (*SNDDMACopyFunc_t) (byte *dst, int dstpos, int scale, byte *src, int srcpos, int srcsamples);
static struct
{
	volatile int	flags;
	SceUID			thread;
	SceUID			sema;
	int				channel;
	SNDDMACopyFunc_t copySamplesFunc;
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
SNDDMA_Copy_8BitScaled
==================
*/
static void SNDDMA_Copy_8BitScaled (byte *dst, int dstpos, int scale, byte *src, int srcpos, int srcsamples)
{
	__asm__ volatile (
		".set		push\n"
		".set		noreorder\n"
		"move		$t0, $zero\n"			// $t0 = i
		"sll		$t1, %1, 1\n"			// $t1 = (dstpos << 1)
		"move		$t2, %4\n"				// $t2 = srcpos
	"0:\n" // Outside loop (i)
		"add		$t3, %3, $t2\n"			// $t3 = src + $t2
		"lb			$t3, 0($t3)\n"			// $t3 = byte(t3)
		"addi		$t3, $t3, -128 \n"		// $t3 -= 128
		"sll		$t3, $t3, 8\n"			// $t3 = ($t3 << 8)
		"move		$t4, $zero \n"			// $t4 = j
	"1:\n" // Inside loop (j)
		"add		$t5, %0, $t1\n"			// dst($t7) = dst + $t1
		"sh			$t3, 0($t5)\n"			// *dst = halfword($t5)
		"addi		$t4, $t4, 1 \n"			// $t4 += 1 (j++)
		"blt		$t4, %2, 1b\n"			// if (j < scale) goto inside loop begin
		"addi		$t1, $t1, 2\n"			// $t1 += 2 (delay slot)
		"addi		$t0, $t0, 1\n"			// $t0 += 1 (i++)
		"blt		$t0, %5, 0b\n"			// if (i < srcsamples) goto outside loop begin
		"addi		$t2, $t2, 1\n"			// $t2 += 2 (srcpos + 1) (delay slot)
		".set		pop\n"
		::	"r"(dst), "r"(dstpos), "r"(scale),
			"r"(src), "r"(srcpos), "r"(srcsamples)
		:	"$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$memory"
	);
}

/*
==================
SNDDMA_Copy_16BitScaled
==================
*/
static void SNDDMA_Copy_16BitScaled (byte *dst, int dstpos, int scale, byte *src, int srcpos, int srcsamples)
{
	__asm__ volatile (
		".set		push\n"
		".set		noreorder\n"
		"move		$t0, $zero\n"			// $t0 = i
		"sll		$t1, %1, 1\n"			// $t1 = (dstpos << 1)
		"sll		$t2, %4, 1\n"			// $t2 = (srcpos << 1)
	"0:\n" // Outside loop (i)
		"add		$t3, %3, $t2\n"			// $t3 = src + $t2
		"lh			$t3, 0($t3)\n"			// $t3 = short(t3)
		"move		$t4, $zero \n"			// $t4 = j
	"1:\n" // Inside loop (j)
		"add		$t5, %0, $t1\n"			// dst($t7) = dst + $t1
		"sh			$t3, 0($t5)\n"			// *dst = short($t5)
		"addi		$t4, $t4, 1 \n"			// $t4 += 1 (j++)
		"blt		$t4, %2, 1b\n"			// if (j < scale) goto inside loop begin
		"addi		$t1, $t1, 2\n"			// $t1 += 2 (delay slot)
		"addi		$t0, $t0, 1\n"			// $t0 += 1 (i++)
		"blt		$t0, %5, 0b\n"			// if (i < srcsamples) goto outside loop begin
		"addi		$t2, $t2, 2\n"			// $t2 += 2 (srcpos + 2) (delay slot)
		".set		pop\n"
		::	"r"(dst), "r"(dstpos), "r"(scale),
			"r"(src), "r"(srcpos), "r"(srcsamples)
		:	"$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$memory"
	);
}

/*
==================
SNDDMA_Copy_16BitDirect
==================
*/
static void SNDDMA_Copy_16BitDirect (byte *dst, int dstpos, int scale, byte *src, int srcpos, int srcsamples)
{
	sceDmacMemcpy (&((short *)dst)[dstpos], &((short *)src)[srcpos], srcsamples << 1);
}

//=============================================================================

/*
==================
SNDDMA_MainThread
==================
*/
static int SNDDMA_MainThread (SceSize args, void *argp)
{
	int	wrapped, remaining;
	int	samplescale, samplesread;

	samplescale = snd.buffer.samplerate / dma.speed;
	samplesread = (snd.buffer.samples * snd.buffer.channels)  / samplescale;

	while (snd.flags & SND_FLAG_RUN)
	{
		sceKernelWaitSema (snd.sema, 1, NULL);

		wrapped = dma.samplepos + samplesread - dma.samples;

		if (wrapped < 0)
		{
			snd.copySamplesFunc (snd.buffer.ptr[snd.buffer.current], 0, samplescale,
								dma.buffer, dma.samplepos, samplesread);
			dma.samplepos += samplesread;
		}
		else
		{
			remaining = dma.samples - dma.samplepos;

			snd.copySamplesFunc (snd.buffer.ptr[snd.buffer.current], 0, samplescale,
								dma.buffer, dma.samplepos, remaining);

			if (wrapped > 0)
			{
				snd.copySamplesFunc (snd.buffer.ptr[snd.buffer.current], remaining * samplescale, samplescale,
									dma.buffer, 0, wrapped);
			}
			dma.samplepos = wrapped;
		}

		sceKernelSignalSema (snd.sema, 1);

		sceAudioOutputBlocking(snd.channel, PSP_AUDIO_VOLUME_MAX, snd.buffer.ptr[snd.buffer.current]);
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
		dma.width = 1;
	else
		dma.width = 2;

	dma.channels            = snd.buffer.channels;
	dma.samples             = SOUND_DMA_SAMPLES * PSP_OUTPUT_CHANNELS;
	dma.samplepos           = 0;
	dma.submission_chunk    = 1;
	dma.buffer              = memalign(64, dma.samples * dma.width);
	if (!dma.buffer)
	{
		SNDDMA_Shutdown ();
		return false;
	}

	if (dma.width == 2)
		snd.copySamplesFunc = (dma.speed == 44100) ? SNDDMA_Copy_16BitDirect : SNDDMA_Copy_16BitScaled;
	else
		snd.copySamplesFunc = SNDDMA_Copy_8BitScaled;

	// clearing buffers
	memset(dma.buffer, 0, dma.samples * dma.width);
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
