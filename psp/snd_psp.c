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
#define PSP_OUTPUT_CHANNELS		2
#define PSP_OUTPUT_BUFFER_SIZE	((PSP_NUM_AUDIO_SAMPLES) * (PSP_OUTPUT_CHANNELS))

#define SOUND_DMA_SAMPLES		16384

static struct
{
	SceUID          threadUID;
	SceUID          semaUID;
	int             channel;
	volatile int    volume;
	volatile int    running;
} snd_psp;


static short snd_psp_buff[2][PSP_OUTPUT_BUFFER_SIZE] __attribute__((aligned(64)));

static qboolean     snd_psp_init = false;

/*
==================
SNDDMA_MainThread
==================
*/
static int SNDDMA_MainThread( SceSize args, void *argp )
{
	int index = 0;

	while(snd_psp.running)
	{
		sceKernelWaitSema(snd_psp.semaUID, 1, NULL);

		int len     = PSP_OUTPUT_BUFFER_SIZE;
		int size    = dma.samples;
		int pos     = dma.samplepos;
		int wrapped = pos + len - size;

		if(wrapped < 0)
		{
			sceDmacMemcpy(snd_psp_buff[index], dma.buffer + (pos * 2), len * 2);
			dma.samplepos += len;
		}
		else
		{
			int remaining = size - pos;
			sceDmacMemcpy(snd_psp_buff[index], dma.buffer + (pos * 2), remaining * 2);
			if(wrapped > 0)
				sceDmacMemcpy(snd_psp_buff[index] + (remaining * 2), dma.buffer, wrapped * 2);
			dma.samplepos = wrapped;
		}

		sceKernelSignalSema(snd_psp.semaUID, 1);

		//sceAudioOutputPannedBlocking(snd_psp.channel, snd_psp.volume, snd_psp.volume, snd_psp_buff[index]);
		sceAudioOutputBlocking(snd_psp.channel, snd_psp.volume, snd_psp_buff[index]);
		index = !index;
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
	if(snd_psp_init)
		return true;

	if ((int)s_loadas8bit->value)
		dma.samplebits = 8;
	else
		dma.samplebits = 16;

	if (dma.samplebits != 16)
	{
		Com_Printf("Don't currently support %i-bit data. Forcing 16-bit.\n", dma.samplebits);
		dma.samplebits = 16;
		Cvar_SetValue("s_loadas8bit", false);
	}

	if ((int)s_khz->value == 44)
	{
		dma.speed = 44100;
	}
	else
	{
		dma.speed = 44100;
		Com_Printf("Don't currently support %i kHz sample rate.  Using %i.\n",
			(int)s_khz->value, (int)(dma.speed / 1000));
	}

	dma.channels            = PSP_OUTPUT_CHANNELS;
	dma.samples             = SOUND_DMA_SAMPLES * PSP_OUTPUT_CHANNELS;
	dma.samplepos           = 0;
	dma.submission_chunk    = 1;
	dma.buffer              = memalign(64, dma.samples * 2); // 16 bit
	if( !dma.buffer )
		return false;

	// clearing buffers
	memset(dma.buffer, 0, dma.samples * 2);
	memset(snd_psp_buff, 0, sizeof(snd_psp_buff));

	snd_psp.threadUID   = -1;
	snd_psp.semaUID     = -1;
	snd_psp.channel     = -1;
	snd_psp.volume      = PSP_AUDIO_VOLUME_MAX;
	snd_psp.running     = 1;

	// allocate and initialize a hardware output channel
	snd_psp.channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, 
		PSP_NUM_AUDIO_SAMPLES, PSP_AUDIO_FORMAT_STEREO);
	if(snd_psp.channel < 0)
	{
		SNDDMA_Shutdown();
		return false;
	}

	// create semaphore
	snd_psp.semaUID = sceKernelCreateSema("sound_sema", 0, 1, 255, NULL);
	if(snd_psp.semaUID <= 0)
	{
		SNDDMA_Shutdown();
		return false;
	}

	// create audio thread
	snd_psp.threadUID = sceKernelCreateThread("sound_thread", SNDDMA_MainThread, 0x12, 0x8000, 0, 0);
	if(snd_psp.threadUID < 0)
	{
		SNDDMA_Shutdown();
		return false;
	}

	// start audio thread
	if(sceKernelStartThread( snd_psp.threadUID, 0, 0 ) < 0)
	{
		SNDDMA_Shutdown();
		return false;
	}

	Com_Printf("Using PSP audio driver: %d Hz\n", dma.speed);

	snd_psp_init = true;

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

	snd_psp.running = 0;

	if(snd_psp.threadUID >= 0)
	{
		sceKernelWaitThreadEnd(snd_psp.threadUID, NULL);
		sceKernelDeleteThread(snd_psp.threadUID);
		snd_psp.threadUID = -1;
	}

	if(snd_psp.semaUID > 0)
	{
		sceKernelDeleteSema(snd_psp.semaUID);
		snd_psp.semaUID = -1;
	}

	if(snd_psp.channel >= 0)
	{
		sceAudioChRelease(snd_psp.channel);
		snd_psp.channel = -1;
	}

	if( dma.buffer )
	{
		free(dma.buffer);
		dma.buffer = NULL;
	}
	
	snd_psp_init = false;
}

/*
==================
SNDDMA_BeginPainting
==================
*/
void SNDDMA_BeginPainting (void)
{
	if(snd_psp.semaUID > 0)
		sceKernelWaitSema(snd_psp.semaUID, 1, NULL);
}

/*
==================
SNDDMA_BeginPainting
==================
*/
void SNDDMA_Submit(void)
{
	if(snd_psp.semaUID > 0)
		sceKernelSignalSema(snd_psp.semaUID, 1);
}
