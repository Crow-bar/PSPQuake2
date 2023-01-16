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
// swimp_psp.c

#include <pspkernel.h>
#include <pspdisplay.h>
#include <stdarg.h>
#include <stdio.h>

#include "../ref_soft/r_local.h"

/*****************************************************************************/
#undef PSP_FB_FORMAT

#define PSP_FB_WIDTH		480
#define PSP_FB_HEIGHT		272
#define PSP_FB_BWIDTH		512
#define PSP_FB_FORMAT		565 //4444,5551,565,8888

#if PSP_FB_FORMAT == 4444
#define PSP_FB_BPP		2
#define PSP_FB_PIXEL_FORMAT	PSP_DISPLAY_PIXEL_FORMAT_4444
#elif PSP_FB_FORMAT == 5551
#define PSP_FB_BPP		2
#define PSP_FB_PIXEL_FORMAT	PSP_DISPLAY_PIXEL_FORMAT_5551
#elif PSP_FB_FORMAT == 565
#define PSP_FB_BPP		2
#define PSP_FB_PIXEL_FORMAT	PSP_DISPLAY_PIXEL_FORMAT_565
#elif PSP_FB_FORMAT == 8888
#define PSP_FB_BPP		4
#define PSP_FB_PIXEL_FORMAT	PSP_DISPLAY_PIXEL_FORMAT_8888
#endif

/*****************************************************************************/

//int	VGA_width, VGA_height, VGA_rowbytes, VGA_bufferrowbytes, VGA_planar;
//byte	*VGA_pagebase;
//char	*framebuffer_ptr;

static byte *draw_buffer, *display_buffer;
static int draw_offset_x, draw_offset_y;
static byte palette_buffer[256 * PSP_FB_BPP];


/*
** SWimp_Init
**
** This routine is responsible for initializing the implementation
** specific stuff in a software rendering subsystem.
*/
int SWimp_Init( void *hInstance, void *wndProc )
{
	return true;
}

/*
** SWimp_InitGraphics
**
** This initializes the software refresh's implementation specific
** graphics subsystem.
**
** The necessary width and height parameters are grabbed from
** vid.width and vid.height.
*/
static qboolean SWimp_InitGraphics( qboolean fullscreen )
{
	SWimp_Shutdown();

	if( vid.width > PSP_FB_WIDTH || vid.height > PSP_FB_HEIGHT )
	{
		ri.Con_Printf( PRINT_ALL, " invalid mode\n" );
		return false;
	}

	// let the sound and input subsystems know about the new window
	ri.Vid_NewWindow (vid.width, vid.height);

//	Cvar_SetValue ("vid_mode", (float)modenum);

	vid.rowbytes = (vid.width + 63) & (~63);

	draw_offset_x = (PSP_FB_WIDTH - vid.width) / 2;
	draw_offset_y = (PSP_FB_HEIGHT - vid.height) / 2;
/*
	draw_buffer = (void*)malloc(PSP_FB_HEIGHT * PSP_FB_BWIDTH * PSP_FB_BPP);
	if( !draw_buffer )
		Sys_Error("Unabled to alloc draw_buffer!\n");
*/
	display_buffer = malloc(PSP_FB_BWIDTH * PSP_FB_HEIGHT * PSP_FB_BPP);
	if( !display_buffer )
		Sys_Error("Unabled to alloc display_buffer!\n");

	sceDisplaySetMode(0, PSP_FB_WIDTH, PSP_FB_HEIGHT);
	sceDisplaySetFrameBuf(NULL, 0, PSP_FB_PIXEL_FORMAT, 1);

	vid.buffer = malloc(vid.rowbytes * vid.height);
	if (!vid.buffer)
		Sys_Error("Unabled to alloc vid.buffer!\n");

	return true;
}

/*
** SWimp_EndFrame
**
** This does an implementation specific copy from the backbuffer to the
** front buffer.
*/
void SWimp_EndFrame (void)
{
	int	x, y;
	byte	*src_ptr;

#if PSP_FB_FORMAT == 8888
	uint32_t	*disp_ptr, *pal_ptr;

	disp_ptr = (uint32_t *)display_buffer;
	pal_ptr = (uint32_t *)palette_buffer;
#else
	uint16_t	*disp_ptr, *pal_ptr;

	disp_ptr = (uint16_t *)display_buffer;
	pal_ptr = (uint16_t *)palette_buffer;
#endif

	if (!display_buffer)
		return;

	src_ptr = vid.buffer;
	disp_ptr += (draw_offset_y * PSP_FB_BWIDTH + draw_offset_x);

	for(y = 0; y < vid.height; y++)
	{
		for(x = 0; x < vid.width; x++ )
			disp_ptr[x] = pal_ptr[src_ptr[x]];

		disp_ptr += PSP_FB_BWIDTH;
		src_ptr += vid.rowbytes;
	}

	//sceDisplayWaitVblankStart();
	sceKernelDcacheWritebackInvalidateAll();
	sceDisplaySetFrameBuf(display_buffer, PSP_FB_BWIDTH, PSP_FB_PIXEL_FORMAT, PSP_DISPLAY_SETBUF_NEXTFRAME);
}

/*
** SWimp_SetMode
*/
rserr_t SWimp_SetMode( int *pwidth, int *pheight, int mode, qboolean fullscreen )
{
	rserr_t retval = rserr_ok;

	ri.Con_Printf (PRINT_ALL, "setting mode %d:", mode );

	if ( !ri.Vid_GetModeInfo( pwidth, pheight, mode ) )
	{
		ri.Con_Printf( PRINT_ALL, " invalid mode\n" );
		return rserr_invalid_mode;
	}

	ri.Con_Printf( PRINT_ALL, " %d %d\n", *pwidth, *pheight);

	if ( !SWimp_InitGraphics( false ) ) {
		// failed to set a valid mode in windowed mode
		return rserr_invalid_mode;
	}

	R_GammaCorrectAndSetPalette( ( const unsigned char * ) d_8to24table );

	return retval;
}

/*
** SWimp_SetPalette
**
** System specific palette setting routine.  A NULL palette means
** to use the existing palette.
*/
void SWimp_SetPalette( const unsigned char *palette )
{
	int	i;
	const byte	*srcpal_ptr;
	byte		*dstpal_ptr;

	if( !palette_buffer ) return;

	if ( !palette )
		palette = ( const unsigned char * ) sw_state.currentpalette;

	srcpal_ptr = palette;
	dstpal_ptr = palette_buffer;

#if   PSP_FB_FORMAT == 4444
	for(i = 0; i < 256; i++, dstpal_ptr += 2, srcpal_ptr += 4)
	{
		dstpal_ptr[0]  = ( srcpal_ptr[0] >> 4 ) & 0x0f;
		dstpal_ptr[0] |= ( srcpal_ptr[1]      ) & 0xf0;
		dstpal_ptr[1]  = ( srcpal_ptr[2] >> 4 ) & 0x0f;
		dstpal_ptr[1] |= ( srcpal_ptr[3]      ) & 0xf0;
	}
#elif PSP_FB_FORMAT == 5551
	for(i = 0; i < 256; i++, dstpal_ptr += 2, srcpal_ptr += 4)
	{
		dstpal_ptr[0]  = ( srcpal_ptr[0] >> 3 );
		dstpal_ptr[0] |= ( srcpal_ptr[1] << 2 ) & 0xe0;
		dstpal_ptr[1]  = ( srcpal_ptr[1] >> 6 ) & 0x03;
		dstpal_ptr[1] |= ( srcpal_ptr[2] >> 1 ) & 0x7c;
		dstpal_ptr[1] |= ( srcpal_ptr[3]      ) & 0x80;
	}
#elif PSP_FB_FORMAT == 565
	for(i = 0; i < 256; i++, dstpal_ptr += 2, srcpal_ptr += 4)
	{
		dstpal_ptr[0]  = ( srcpal_ptr[0] >> 3 ) & 0x1f;
		dstpal_ptr[0] |= ( srcpal_ptr[1] << 3 ) & 0xe0;
		dstpal_ptr[1]  = ( srcpal_ptr[1] >> 5 ) & 0x07;
		dstpal_ptr[1] |= ( srcpal_ptr[2]      ) & 0xf8;	
	}
#elif PSP_FB_FORMAT == 8888
	memcpy(dstpal_ptr, srcpal_ptr, 256 * 4);
#endif
}

/*
** SWimp_Shutdown
**
** System specific graphics subsystem shutdown routine.
*/
void SWimp_Shutdown( void )
{
	if (vid.buffer)
	{
		free(vid.buffer);
		vid.buffer = NULL;
	}
	if (display_buffer)
	{
		free(display_buffer);
		display_buffer = NULL;
	}
}

/*
** SWimp_AppActivate
*/
void SWimp_AppActivate( qboolean active )
{
}

//===============================================================================
