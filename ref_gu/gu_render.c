/*
Copyright (C) 1997-2001 Id Software, Inc.
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
// gu_render.c
#include "gu_local.h"

gurender_t	gu_render;

// Set frame buffer
#define PSP_FB_WIDTH		480
#define PSP_FB_HEIGHT		272
#define PSP_FB_BWIDTH		512
#define PSP_FB_FORMAT		GU_PSM_5650 //4444,5551,5650,8888

#if   PSP_FB_FORMAT == GU_PSM_4444
#define PSP_FB_BPP			2
#elif PSP_FB_FORMAT == GU_PSM_5551
#define PSP_FB_BPP			2
#elif PSP_FB_FORMAT == GU_PSM_5650
#define PSP_FB_BPP			2
#elif PSP_FB_FORMAT == GU_PSM_8888
#define PSP_FB_BPP			4
#endif

#define PSP_GU_LIST_SIZE	0x100000 // 1Mb

static byte context_list[PSP_GU_LIST_SIZE] __attribute__((aligned( 64 )));

qboolean GU_InitGraphics( qboolean fullscreen );

/*
===============
GU_SetMode
===============
*/
int GU_SetMode( int *pwidth, int *pheight, int mode, qboolean fullscreen )
{
	int width, height;

#if 0
	fprintf(stderr, "GLimp_SetMode\n");

	ri.Con_Printf( PRINT_ALL, "Initializing OpenGL display\n");

	ri.Con_Printf (PRINT_ALL, "...setting mode %d:", mode );

	if ( !ri.Vid_GetModeInfo( &width, &height, mode ) )
	{
		ri.Con_Printf( PRINT_ALL, " invalid mode\n" );
		return rserr_invalid_mode;
	}

	ri.Con_Printf( PRINT_ALL, " %d %d\n", width, height );

	// destroy the existing window
	//GLimp_Shutdown ();

	*pwidth = width;
	*pheight = height;

	if ( !GLimp_InitGraphics( fullscreen ) ) {
		// failed to set a valid mode in windowed mode
		return rserr_invalid_mode;
	}
#else
	width = PSP_FB_WIDTH;
	height = PSP_FB_HEIGHT;

	ri.Con_Printf( PRINT_ALL, "Initializing display\n");
	ri.Con_Printf( PRINT_ALL, "...setting mode: %d %d\n", width, height);

	*pwidth = width;
	*pheight = height;
#endif

	// let the sound and input subsystems know about the new window
	ri.Vid_NewWindow (width, height);

	return rserr_ok;
}

/*
===============
GU_Shutdown
===============
*/
void GU_Shutdown (void)
{
	fprintf(stderr, "GU_Shutdown\n");

	// Finish rendering.
	sceGuFinish ();
	sceGuSync (GU_SYNC_FINISH, GU_SYNC_WAIT);

	// Shut down the display.
	sceGuTerm();

	// Free the buffers.
	if (gu_render.buffer.draw_ptr)
		vfree (gu_render.buffer.draw_ptr);
	if (gu_render.buffer.disp_ptr)
		vfree (gu_render.buffer.disp_ptr);
	if (gu_render.buffer.depth_ptr)
		vfree (gu_render.buffer.depth_ptr);

	memset (&gu_render, 0, sizeof(gu_render));
}

/*
===============
GU_Init
===============
*/
int GU_Init (void *hinstance, void *wndproc)
{
	memset (&gu_render, 0, sizeof(gu_render));

	if (vinit() < 0)
	{
		Sys_Error("VRam unavailable!");
		return false;
	}

	gu_render.screen.width = PSP_FB_WIDTH;
	gu_render.screen.height = PSP_FB_HEIGHT;

	gu_render.buffer.width = PSP_FB_BWIDTH;
	gu_render.buffer.format = PSP_FB_FORMAT;
	gu_render.buffer.bpp = PSP_FB_BPP;

	gu_render.list.ptr = context_list;
	gu_render.list.size = sizeof(context_list);

	gu_render.buffer.draw_ptr = (void*)valloc (gu_render.buffer.width * gu_render.screen.height * gu_render.buffer.bpp);
	if (!gu_render.buffer.draw_ptr)
		Sys_Error("Memory allocation failled! (draw buffer)\n");

	gu_render.buffer.disp_ptr = (void*)valloc (gu_render.buffer.width * gu_render.screen.height * gu_render.buffer.bpp);
	if (!gu_render.buffer.disp_ptr)
		Sys_Error("Memory allocation failled! (disp buffer)\n");

	gu_render.buffer.depth_ptr = (void*)valloc (gu_render.buffer.width * gu_render.screen.height * gu_render.buffer.bpp);
	if (!gu_render.buffer.depth_ptr)
		Sys_Error("Memory allocation failled! (depth buffer)\n");

	// Initialise the GU.
	sceGuInit();

	// Set up the GU.
	sceGuStart (GU_DIRECT, gu_render.list.ptr);

	sceGuDrawBuffer ( gu_render.buffer.format, vrelptr (gu_render.buffer.draw_ptr), gu_render.buffer.width );
	sceGuDispBuffer ( gu_render.screen.width, gu_render.screen.height, vrelptr (gu_render.buffer.disp_ptr), gu_render.buffer.width );
	sceGuDepthBuffer ( vrelptr (gu_render.buffer.depth_ptr), gu_render.buffer.width );

	// Set the rendering offset and viewport.
	sceGuOffset (2048 - (gu_render.screen.width / 2), 2048 - (gu_render.screen.height / 2));
	sceGuViewport (2048, 2048, gu_render.screen.width, gu_render.screen.height );
	sceGuDepthRange (0, 65535);

	// Set up scissoring.
	sceGuEnable (GU_SCISSOR_TEST);
	sceGuScissor (0, 0, gu_render.screen.width, gu_render.screen.height);

	// Set up drawing functions
	sceGuClearColor (GU_COLOR(1.0f, 0.5f, 0.5f, 1.0f));
	sceGuColor (GU_HCOLOR_DEFAULT);

	sceGuEnable (GU_DEPTH_TEST);
	sceGuDepthFunc (GU_LEQUAL);

	sceGuDisable (GU_BLEND);
	sceGuBlendFunc (GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);

	sceGuDisable (GU_ALPHA_TEST);
	sceGuAlphaFunc (GU_GREATER, 0xaa, 0xff);

	sceGuDisable (GU_CULL_FACE);
	sceGuFrontFace (GU_CW);

	sceGuEnable (GU_TEXTURE_2D);
	sceGuShadeModel (GU_FLAT);
	sceGuEnable (GU_CLIP_PLANES);
	sceGuDepthOffset (0);

	// Set tex defaults
	GL_TexEnv (GU_TFX_REPLACE);
	GL_SetTexturePalette (d_8to24table);

	// Set the default matrices.
	sceGumMatrixMode (GU_PROJECTION);
	sceGumLoadIdentity ();
	sceGumMatrixMode (GU_VIEW);
	sceGumLoadIdentity ();
	sceGumMatrixMode (GU_MODEL);
	sceGumLoadIdentity ();
	sceGumMatrixMode (GU_TEXTURE);
	sceGumLoadIdentity ();

	sceGumUpdateMatrix ();

	sceGuFinish ();
	sceGuSync (GU_SYNC_FINISH, GU_SYNC_WAIT);

	// Turn on the display.
	sceDisplayWaitVblankStart ();
	sceGuDisplay (GU_TRUE);

	// Start a new render.
	sceGuStart (GU_DIRECT, gu_render.list.ptr);

	return true;
}

/*
===============
GU_BeginFrame
===============
*/
void GU_BeginFrame (float camera_seperation)
{

}

/*
===============
GU_EndFrame

Responsible for doing a swapbuffers and possibly for other stuff
as yet to be determined.
===============
*/
void GU_EndFrame (void)
{
	// finish rendering.
	sceGuFinish ();
	sceGuSync (GU_SYNC_FINISH, GU_SYNC_WAIT);

	// swap the buffers.
	sceGuSwapBuffers();

	void* p_swap = gu_render.buffer.disp_ptr;
	gu_render.buffer.disp_ptr = gu_render.buffer.draw_ptr;
	gu_render.buffer.draw_ptr = p_swap;

	// start a new render.
	sceGuStart (GU_DIRECT, gu_render.list.ptr);
}

/*
===============
GU_AppActivate
===============
*/
void GU_AppActivate (qboolean active)
{
}

/*
===============
GU_InitGraphics

The necessary width and height parameters are grabbed from
vid.width and vid.height.
===============
*/
qboolean GU_InitGraphics (qboolean fullscreen)
{

	return true;
}

/*
===============
GU_EnableLogging
===============
*/
void GU_EnableLogging (qboolean enable)
{

}

/*
===============
GU_LogNewFrame
===============
*/
void GU_LogNewFrame (void)
{

}