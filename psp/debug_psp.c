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

/*
Based on:

scr_printf.c - Debug screen functions.

PSP Software Development Kit - https://github.com/pspdev

Copyright (c) 2005 Marcus R. Brown <mrbrown@ocgnet.org>
Copyright (c) 2005 James Forshaw <tyranid@gmail.com>
Copyright (c) 2005 John Kelley <ps2dev@kelley.ca>

*/

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <psptypes.h>
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspge.h>

#define DBG_FONTID	(('T'<<24)+('N'<<16)+('F'<<8)+'D')

//=============================================================================
typedef struct
{
	uint8_t		x;
	uint8_t		y;
} ucord8_t;

typedef struct
{
	uint16_t	x;
	uint16_t	y;
} ucord16_t;

typedef struct
{
	uint32_t	id;
	ucord8_t	pixels;
	ucord8_t	bits;
	uint8_t		data[1];
} dbg_font_t;

typedef struct
{
	void		*ptr;
	int32_t		format;
	int32_t		stride;
	int32_t		width;
	int32_t		height;
	uint32_t	clcolor;
	uint32_t    active;
} disp_t;

typedef struct
{
	uint32_t	bgcolor;
	uint32_t	chcolor;
	ucord16_t	pos;
	ucord16_t	max;
} text_t;

typedef struct
{
	int32_t		pmid;
	disp_t		disp;
	text_t		text;
	dbg_font_t	font;
} dbg_context_t;
//=============================================================================

static dbg_context_t	*dbg_context = NULL;

//=============================================================================

/*
===================
Dbg_GetColor
===================
*/
static uint32_t Dbg_GetColor (int32_t format, int32_t r, int32_t g, int32_t b, int32_t a)
{
	switch (format)
	{
	case PSP_DISPLAY_PIXEL_FORMAT_565:
		r = ((r >> 3) & 0x1f);
		g = ((g >> 2) & 0x3f) << 5;
		b = ((b >> 3) & 0x1f) << 11;
		a = 0;
		break;
	case PSP_DISPLAY_PIXEL_FORMAT_5551:
		r = ((r >> 3) & 0x1f);
		g = ((g >> 3) & 0x1f) << 5;
		b = ((b >> 3) & 0x1f) << 10;
		a = ((a >> 7) & 0x01) << 15;
		break;
	case PSP_DISPLAY_PIXEL_FORMAT_4444:
		r = ((r >> 4) & 0x0f);
		g = ((g >> 4) & 0x0f) << 4;
		b = ((b >> 4) & 0x0f) << 8;
		a = ((a >> 4) & 0x0f) << 12;
		break;
	default:
		r = (r & 0xff);
		g = (g & 0xff) << 8;
		b = (b & 0xff) << 16;
		a = (a & 0xff) << 24;
		break;
	}

	return (a | r | g | b);
}


/*
===================
Dbg_SetClearColor
===================
*/
void Dbg_SetClearColor (int32_t r, int32_t g, int32_t b)
{
	if (!dbg_context)
		return;

	dbg_context->disp.clcolor = Dbg_GetColor (dbg_context->disp.format, r, g, b, 255);
}


/*
===================
Dbg_SetTextColor
===================
*/
void Dbg_SetTextColor (int32_t r, int32_t g, int32_t b)
{
	if (!dbg_context)
		return;

	dbg_context->text.chcolor = Dbg_GetColor (dbg_context->disp.format, r, g, b, 255);
}


/*
===================
Dbg_SetTextBackColor
===================
*/
void Dbg_SetTextBackColor (int32_t r, int32_t g, int32_t b)
{
	if (!dbg_context)
		return;

	dbg_context->text.bgcolor = Dbg_GetColor (dbg_context->disp.format, r, g, b, 255);
}


/*
===================
Dbg_SetTextBackOff
===================
*/
void Dbg_SetTextBackOff (void)
{
	if (!dbg_context)
		return;

	dbg_context->text.bgcolor =	0;
}


/*
===================
Dbg_GetDisplayWH
===================
*/
void Dbg_GetDisplayWH(int32_t *width, int32_t *height)
{
	if (!dbg_context)
		return;

	if (width) *width = dbg_context->disp.width;
	if (height) *height = dbg_context->disp.height;
}


/*
===================
Dbg_GetTextXY
===================
*/
void Dbg_GetTextXY (uint16_t *x, uint16_t *y)
{
	if (!dbg_context)
		return;

	if (x) *x = dbg_context->text.pos.x;
	if (y) *y = dbg_context->text.pos.y;
}


/*
===================
Dbg_GetTextMaxXY
===================
*/
void Dbg_GetTextMaxXY (uint16_t *x, uint16_t *y)
{
	if (!dbg_context)
		return;

	if (x) *x = dbg_context->text.max.x;
	if (y) *y = dbg_context->text.max.y;
}


/*
===================
Dbg_SetTextXY
===================
*/
void Dbg_SetTextXY (uint16_t x, uint16_t y)
{
	if (!dbg_context)
		return;

	dbg_context->text.pos.x = x;
	dbg_context->text.pos.y = y;
}


/*
===================
Dbg_DisplayActivate
===================
*/
void Dbg_DisplayActivate (void)
{
	if (!dbg_context)
		return;

	if (!dbg_context->disp.active)
	{
		sceDisplaySetMode (0, dbg_context->disp.width, dbg_context->disp.height);
		sceDisplaySetFrameBuf (dbg_context->disp.ptr,
			dbg_context->disp.stride,
			dbg_context->disp.format, 1);
		dbg_context->disp.active = 1;
	}
}


/*
===================
Dbg_DisplayClear
===================
*/
void Dbg_DisplayClear (void)
{
	int32_t	i;

	if (!dbg_context)
		return;

	if (!dbg_context->disp.active)
		Dbg_DisplayActivate ();

	if (dbg_context->disp.format == PSP_DISPLAY_PIXEL_FORMAT_8888)
	{
		for (i = 0; i < dbg_context->disp.stride * dbg_context->disp.height; i++)
			((uint32_t*)dbg_context->disp.ptr)[i] = dbg_context->disp.clcolor;
	}
	else
	{
		for (i = 0; i < dbg_context->disp.stride * dbg_context->disp.height; i++)
			((uint16_t*)dbg_context->disp.ptr)[i] = (uint16_t)dbg_context->disp.clcolor;
	}
}


/*
===================
Dbg_DrawTextFill
===================
*/
void Dbg_DrawTextFill (int32_t tx, int32_t ty, int32_t tw, int32_t th, int32_t r, int32_t g, int32_t b)
{
	int32_t		i, j;
	uint32_t	color;
	uint32_t	*vram32;
	uint16_t	*vram16;

	if (!dbg_context)
		return;

	if (!dbg_context->disp.active)
		Dbg_DisplayActivate ();

	if ((tx + tw > dbg_context->text.max.x) || (ty + th > dbg_context->text.max.y))
		return;

	color = Dbg_GetColor (dbg_context->disp.format, r, g, b, 255);

	tx *= dbg_context->font.pixels.x;
	ty *= dbg_context->font.pixels.y;
	tw *= dbg_context->font.pixels.x;
	th *= dbg_context->font.pixels.y;

	if (dbg_context->disp.format == PSP_DISPLAY_PIXEL_FORMAT_8888)
	{
		vram32 = dbg_context->disp.ptr;
		vram32 += tx + ty * dbg_context->disp.stride;

		for (i = 0; i < th; i++)
		{
			for (j = 0; j < tw; j++)
				vram32[j] = color;
			vram32 += dbg_context->disp.stride;
		}
	}
	else
	{
		vram16 = dbg_context->disp.ptr;
		vram16 += tx + ty * dbg_context->disp.stride;

		for (i = 0; i < th; i++)
		{
			for (j = 0; j < tw; j++)
				vram16[j] = (uint16_t)color;
			vram16 += dbg_context->disp.stride;
		}
	}
}


/*
===================
Dbg_DrawChar
===================
*/
void Dbg_DrawChar (int32_t c, int32_t x, int32_t y, uint32_t chcolor, uint32_t bgcolor)
{
	int32_t		i, j, k;
	uint8_t		*fontptr;
	int32_t		charbits;
	int32_t		charpixels;
	uint32_t	*vram32;
	uint16_t	*vram16;

	if (!dbg_context)
		return;

	if (!dbg_context->disp.active)
		Dbg_DisplayActivate ();

	charbits = dbg_context->font.bits.x * dbg_context->font.bits.y;
	charpixels = dbg_context->font.pixels.x * dbg_context->font.pixels.y;
	fontptr = &dbg_context->font.data[c * (charbits >> 3)];

	if (dbg_context->disp.format == PSP_DISPLAY_PIXEL_FORMAT_8888)
	{
		vram32 = dbg_context->disp.ptr;
		vram32 += x + y * dbg_context->disp.stride;

		for (i = 0; i < charbits; i++)
		{
			k = (i % dbg_context->font.bits.x);
			j = (k % 8);

			if ((*fontptr & (128 >> j)))
				vram32[k] = chcolor;
			else if (bgcolor != 0)
				vram32[k] = bgcolor;

			if (i != 0)
			{
				if (j == 0) fontptr++;
				if (k == 0) vram32 += dbg_context->disp.stride;
			}
		}
	}
	else
	{
		vram16 = dbg_context->disp.ptr;
		vram16 += x + y * dbg_context->disp.stride;

		for (i = 0; i < charbits; i++)
		{
			k = (i % dbg_context->font.bits.x);
			j = (i % 8);

			if ((*fontptr & (128 >> j)))
				vram16[k] = (uint16_t)chcolor;
			else if (bgcolor != 0)
				vram16[k] = (uint16_t)bgcolor;

			if (i != 0)
			{
				if (j == 0) fontptr++;
				if (k == 0) vram16 += dbg_context->disp.stride;
			}
		}
	}
}


/*
===================
Dbg_DrawText
===================
*/
void Dbg_DrawText (const uint8_t *text, int32_t size)
{
	int32_t	i, j;
	int32_t	c;

	if (!dbg_context)
		return;

	for (i = 0; i < size; i++)
	{
		c = text[i];
		switch (c)
		{
			case '\r':
				dbg_context->text.pos.x = 0;
				break;
			case '\n':
				dbg_context->text.pos.x = 0;
				dbg_context->text.pos.y++;
				if (dbg_context->text.pos.y == dbg_context->text.max.y)
				{
					dbg_context->text.pos.y = 0;
					Dbg_DisplayClear ();
				}
				break;
			case '\t':
				for (j = 0; j < 5; j++)
				{
					Dbg_DrawChar (' ', dbg_context->text.pos.x * dbg_context->font.pixels.x,
									dbg_context->text.pos.y * dbg_context->font.pixels.y,
									dbg_context->text.chcolor,
									dbg_context->text.bgcolor);
					dbg_context->text.pos.x++;
				}
				break;
			default:
				Dbg_DrawChar (c, dbg_context->text.pos.x * dbg_context->font.pixels.x,
								dbg_context->text.pos.y * dbg_context->font.pixels.y,
								dbg_context->text.chcolor,
								dbg_context->text.bgcolor);
				dbg_context->text.pos.x++;
				if (dbg_context->text.pos.x == dbg_context->text.max.x)
				{
					dbg_context->text.pos.x = 0;
					dbg_context->text.pos.y++;
					if (dbg_context->text.pos.y == dbg_context->text.max.y)
					{
						dbg_context->text.pos.y = 0;
						Dbg_DisplayClear ();
					}
				}
				break;
		}
	}
}


/*
===================
Dbg_Printf
===================
*/
void Dbg_Printf (const uint8_t *format, ...)
{
	va_list	argptr;
	uint8_t	string[2048];
	int32_t	size;

	va_start (argptr, format);
	size = vsnprintf (string, sizeof(string), format, argptr);
	va_end (argptr);

	Dbg_DrawText (string, size);
}


/*
===================
Dbg_CreateContext
===================
*/
static dbg_context_t *Dbg_CreateContext (uint8_t *fontpath)
{
	int32_t			ret;
	int32_t			fd, fsize;
	int32_t			pmid, pmsize;
	dbg_context_t	*context;

	if (!fontpath)
		return NULL;

	fd = sceIoOpen (fontpath, PSP_O_RDONLY, 0777);
	if (fd < 0)
		return NULL;

	fsize = sceIoLseek (fd, 0, PSP_SEEK_END);
	sceIoLseek (fd, 0, PSP_SEEK_SET);
	pmsize = sizeof(dbg_context_t) - sizeof(dbg_font_t) + fsize;
	pmid = sceKernelAllocPartitionMemory (PSP_MEMORY_PARTITION_USER, "debug", PSP_SMEM_Low, pmsize, NULL);
	if (pmid <= 0)
	{
		sceIoClose (fd);
		return NULL;
	}

	context = sceKernelGetBlockHeadAddr (pmid);
	if (!context)
	{
		sceKernelFreePartitionMemory (pmid);
		sceIoClose(fd);
		return NULL;
	}

	memset (context, 0, pmsize);

	ret = sceIoRead (fd, &context->font, fsize);
	if(ret < 0)
	{
		sceKernelFreePartitionMemory (pmid);
		sceIoClose(fd);
		return NULL;
	}
	sceIoClose(fd);

	if (context->font.id != DBG_FONTID)
	{
		sceKernelFreePartitionMemory (pmid);
		return NULL;
	}

	context->pmid = pmid;

	return context;
}


/*
===================
Dbg_Init
===================
*/
int32_t Dbg_Init (void *dbuffer, int32_t format, uint8_t *fontpath, uint8_t usbuffer)
{
	int32_t	mode;

	if (dbg_context)
		return 0;

	dbg_context = Dbg_CreateContext (fontpath);
	if (!dbg_context)
		return -1;

	if (usbuffer && sceDisplayIsForeground ())
	{
		// get current
		sceDisplayGetMode ((int *)&mode, (int *)&dbg_context->disp.width, (int *)&dbg_context->disp.height);
		sceDisplayGetFrameBuf (&dbg_context->disp.ptr,
			(int *)&dbg_context->disp.stride,
			(int *)&dbg_context->disp.format, 0);
		dbg_context->disp.active = 1;
	}
	else
	{
		dbg_context->disp.ptr = dbuffer ? dbuffer : ((void *) (0x40000000 | (uint32_t) sceGeEdramGetAddr()));

		switch (format)
		{
		case 5650:
		case 565:
			dbg_context->disp.format = PSP_DISPLAY_PIXEL_FORMAT_565;
			break;
		case 5551:
			dbg_context->disp.format = PSP_DISPLAY_PIXEL_FORMAT_5551;
			break;
		case 4444:
			dbg_context->disp.format = PSP_DISPLAY_PIXEL_FORMAT_4444;
			break;
		default:
			dbg_context->disp.format = PSP_DISPLAY_PIXEL_FORMAT_8888;
			break;
		}

		dbg_context->disp.stride = 512;
		dbg_context->disp.width = 480;
		dbg_context->disp.height = 272;
	}

	Dbg_SetClearColor (0, 0, 0);
	Dbg_SetTextColor (255, 255, 255);
	Dbg_SetTextBackOff ();

	dbg_context->text.max.x = dbg_context->disp.width / dbg_context->font.pixels.x;
	dbg_context->text.max.y = dbg_context->disp.height / dbg_context->font.pixels.y;

	return 0;
}


/*
===================
Dbg_Shutdown
===================
*/
void Dbg_Shutdown (void)
{
	if (!dbg_context)
		return;

	if (dbg_context->disp.active) // black screen
		sceDisplaySetFrameBuf (NULL, 0, PSP_DISPLAY_PIXEL_FORMAT_8888, 1);

	sceKernelFreePartitionMemory (dbg_context->pmid);
	dbg_context = NULL;
}
