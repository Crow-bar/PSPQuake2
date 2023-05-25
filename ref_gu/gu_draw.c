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

// gu_draw.c

#include "gu_local.h"

image_t		*draw_chars;

/*
===============
Draw_InitLocal
===============
*/
void Draw_InitLocal (void)
{
	// load console characters (don't bilerp characters)
	draw_chars = GL_FindImage ("pics/conchars.pcx", IMG_TYPE_PIC | IMG_FILTER_NEAREST);
}



/*
================
Draw_Char

Draws one 8*8 graphics character with 0 being transparent.
It can be clipped to the top of the screen to allow the console to be
smoothly scrolled off.
================
*/
void Draw_Char (int x, int y, int num)
{
	int				row, col;
	short			hrow, hcol, size;

	num &= 255;

	if ( (num&127) == 32 )
		return;		// space

	if (y <= -8)
		return;			// totally off screen

	row = num>>4;
	col = num&15;

	size = 8;
	hrow = row * size;
	hcol = col * size;

	GL_Bind (draw_chars->texnum);

	gu_vert_htv_t* const out = (gu_vert_htv_t*)sceGuGetMemory (sizeof(gu_vert_htv_t ) * 2);
	out[0].u = hcol;
	out[0].v = hrow;
	out[0].x = x;
	out[0].y = y;
	out[0].z = 0;
	out[1].u = hcol + size;
	out[1].v = hrow + size;
	out[1].x = x + size;
	out[1].y = y + size;
	out[1].z = 0;
	sceGuDrawArray (GU_SPRITES, GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, out);
}

/*
=============
Draw_FindPic
=============
*/
image_t	*Draw_FindPic (char *name)
{
	image_t *image;
	char	fullname[MAX_QPATH];

	if (name[0] != '/' && name[0] != '\\')
	{
		Com_sprintf (fullname, sizeof(fullname), "pics/%s.pcx", name);
		image = GL_FindImage (fullname, IMG_TYPE_PIC);
	}
	else
		image = GL_FindImage (name+1, IMG_TYPE_PIC);

	return image;
}

/*
=============
Draw_GetPicSize
=============
*/
void Draw_GetPicSize (int *w, int *h, char *pic)
{
	image_t *image;

	image = Draw_FindPic (pic);
	if (!image)
	{
		*w = *h = -1;
		return;
	}
	*w = image->width;
	*h = image->height;
}

/*
=============
Draw_StretchPic
=============
*/
void Draw_StretchPic (int x, int y, int w, int h, char *pic)
{
	image_t *image;

	image = Draw_FindPic (pic);
	if (!image)
	{
		ri.Con_Printf (PRINT_ALL, "Can't find pic: %s\n", pic);
		return;
	}

	//if (!(image->flags & IMG_FLAG_HAS_ALPHA))
	//	sceGuDisable (GU_ALPHA_TEST);

	GL_Bind (image->texnum);

	gu_vert_htv_t* const out = (gu_vert_htv_t*)sceGuGetMemory (sizeof(gu_vert_htv_t ) * 2);
	out[0].u = image->sl;
	out[0].v = image->tl;
	out[0].x = x;
	out[0].y = y;
	out[0].z = 0;
	out[1].u = image->sh;
	out[1].v = image->th;
	out[1].x = x + w;
	out[1].y = y + h;
	out[1].z = 0;
	sceGuDrawArray (GU_SPRITES, GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, out);

	//if (!(image->flags & IMG_FLAG_HAS_ALPHA))
	//	sceGuEnable (GU_ALPHA_TEST);
}


/*
=============
Draw_Pic
=============
*/
void Draw_Pic (int x, int y, char *pic)
{
	image_t *image;

	image = Draw_FindPic (pic);
	if (!image)
	{
		ri.Con_Printf (PRINT_ALL, "Can't find pic: %s\n", pic);
		return;
	}

	//if (!(image->flags & IMG_FLAG_HAS_ALPHA))
	//	sceGuDisable (GU_ALPHA_TEST);

	GL_Bind (image->texnum);

	gu_vert_htv_t* const out = (gu_vert_htv_t*)sceGuGetMemory (sizeof(gu_vert_htv_t ) * 2);
	out[0].u = image->sl;
	out[0].v = image->tl;
	out[0].x = x;
	out[0].y = y;
	out[0].z = 0;
	out[1].u = image->sh;
	out[1].v = image->th;
	out[1].x = x + image->width;
	out[1].y = y + image->height;
	out[1].z = 0;
	sceGuDrawArray (GU_SPRITES, GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, out);

	//if (!(image->flags & IMG_FLAG_HAS_ALPHA))
	//	sceGuEnable (GU_ALPHA_TEST);
}

/*
=============
Draw_TileClear

This repeats a 64*64 tile graphic to fill the screen around a sized down
refresh window.
=============
*/
void Draw_TileClear (int x, int y, int w, int h, char *pic)
{
	image_t	*image;

	image = Draw_FindPic (pic);
	if (!image)
	{
		ri.Con_Printf (PRINT_ALL, "Can't find pic: %s\n", pic);
		return;
	}

	//if (!(image->flags & IMG_FLAG_HAS_ALPHA))
	//	sceGuDisable (GU_ALPHA_TEST);

	GL_Bind (image->texnum);

	gu_vert_htv_t* const out = (gu_vert_htv_t*)sceGuGetMemory (sizeof(gu_vert_htv_t ) * 2);
	out[0].u = x;
	out[0].v = y;
	out[0].x = x;
	out[0].y = y;
	out[0].z = 0;
	out[1].u = x + w;
	out[1].v = y + h;
	out[1].x = x + w;
	out[1].y = y + h;
	out[1].z = 0;
	sceGuDrawArray (GU_SPRITES, GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, out);

	//if (!(image->flags & IMG_FLAG_HAS_ALPHA))
	//	sceGuEnable (GU_ALPHA_TEST);
}


/*
=============
Draw_Fill

Fills a box of pixels with a single color
=============
*/
void Draw_Fill (int x, int y, int w, int h, int c)
{
	union
	{
		unsigned	c;
		byte		v[4];
	} color;

	if ( (unsigned)c > 255)
		ri.Sys_Error (ERR_FATAL, "Draw_Fill: bad color");

	sceGuDisable (GU_TEXTURE_2D);

	color.c = d_8to24table[c];
	sceGuColor (GU_HCOLOR_3UBV(color.v));

	gu_vert_hv_t* const out = (gu_vert_hv_t*)sceGuGetMemory (sizeof(gu_vert_hv_t) * 2);
	out[0].x = x;
	out[0].y = y;
	out[0].z = 0;
	out[1].x = x + w;
	out[1].y = y + h;
	out[1].z = 0;
	sceGuDrawArray (GU_SPRITES, GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, out);

	sceGuColor (GU_HCOLOR_DEFAULT);
	sceGuEnable (GU_TEXTURE_2D);
}

//=============================================================================

/*
================
Draw_FadeScreen

================
*/
void Draw_FadeScreen (void)
{
	sceGuEnable (GU_BLEND);
	sceGuDisable (GU_TEXTURE_2D);
	sceGuColor (GU_HCOLOR_4UB(0, 0, 0, 0xcc));

	gu_vert_hv_t* const out = (gu_vert_hv_t*)sceGuGetMemory (sizeof(gu_vert_hv_t) * 2);
	out[0].x = 0;
	out[0].y = 0;
	out[0].z = 0;
	out[1].x = vid.width;
	out[1].y = vid.height;
	out[1].z = 0;
	sceGuDrawArray (GU_SPRITES, GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, out);

	sceGuColor (GU_HCOLOR_DEFAULT);
	sceGuEnable (GU_TEXTURE_2D);
	sceGuDisable (GU_BLEND);
}


//====================================================================


/*
=============
Draw_StretchRaw
=============
*/
void Draw_StretchRaw (int x, int y, int w, int h, int cols, int rows, byte *data)
{
	static byte image8[256*256] __attribute__((aligned(16)));

	GL_ResampleTexture8 (data, cols, rows, image8, 256, 256);

	sceKernelDcacheWritebackRange (image8, sizeof(image8));

	sceGuTexMode (GU_PSM_T8, 0, 0, GU_FALSE);
	sceGuTexFilter (GU_LINEAR, GU_LINEAR);
	sceGuTexImage (0, 256, 256, 256, image8);

	gl_state.currenttexture = -1;

	gu_vert_htv_t* const out = (gu_vert_htv_t*)sceGuGetMemory (sizeof(gu_vert_htv_t ) * 2);
	out[0].u = 0;
	out[0].v = 0;
	out[0].x = x;
	out[0].y = y;
	out[0].z = 0;
	out[1].u = 256;
	out[1].v = 256;
	out[1].x = x + w;
	out[1].y = y + h;
	out[1].z = 0;
	sceGuDrawArray (GU_SPRITES, GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, out);
}

