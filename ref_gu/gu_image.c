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

#include "gu_local.h"
#include <malloc.h>

#define TEXTURE_SIZE_MAX	512
#define TEXTURE_SIZE_MIN	1

image_t		gltextures[MAX_GLTEXTURES];
#ifdef USE_HASH_FOR_TEXTURES
image_t		*gltextures_hash[MAX_GLTEXTURES_HASH];
#endif
int			numgltextures;
int			base_textureid;		// gltextures[i] = base_textureid+i

cvar_t		*intensity;

unsigned	d_8to24table[256] __attribute__((aligned(16)));

/*
===============
GL_SetTexturePalette
===============
*/
void GL_SetTexturePalette( unsigned palette[256] )
{
	sceKernelDcacheWritebackRange(palette, 256 * sizeof(unsigned));
	sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
	sceGuClutLoad(32, palette);
}

/*
===============
GL_TexEnv
===============
*/
void GL_TexEnv (int mode)
{
	if (mode != gl_state.currenttexenv)
	{
		sceGuTexFunc(mode, GU_TCC_RGBA);
		gl_state.currenttexenv = mode;
	}
}

/*
===============
GL_Bind
===============
*/
void GL_Bind (int texnum)
{
	extern	image_t	*draw_chars;
	image_t	*image;

	if (gl_nobind->value && draw_chars)		// performance evaluation option
		texnum = draw_chars->texnum;
	if (gl_state.currenttexture == texnum)
		return;

	image = &gltextures[texnum];

	// Set texture parameters
	sceGuTexMode (image->format, 0, 0, (image->flags & IMG_FLAG_SWIZZLED) ? GU_TRUE : GU_FALSE);

	// Set texture filter
	if (image->flags & IMG_FILTER_NEAREST)
		sceGuTexFilter (GU_NEAREST, GU_NEAREST);
	else
		sceGuTexFilter (GU_LINEAR, GU_LINEAR);

	// Set base texture
	sceGuTexImage (0, image->uplwidth, image->uplheight, image->uplwidth, image->data);

	gl_state.currenttexture = texnum;
}

/*
===============
GL_ImageList_f
===============
*/
void	GL_ImageList_f (void)
{
	int		i;
	image_t	*image;
	size_t	totalr, totalvr;
	int		hcollisions;

	ri.Con_Printf (PRINT_ALL, "------------------\n");
	totalr = totalvr = 0;
	hcollisions = 0;

#ifdef USE_HASH_FOR_TEXTURES
	for (i = 0; i < MAX_GLTEXTURES_HASH; i++) // order by hashkey
	for (image = gltextures_hash[i]; image != NULL; image = image->nexthash)
#else
	for (i=0, image=gltextures ; i<numgltextures ; i++, image++)
#endif
	{
		if (image->texnum < 0)
			continue;

		if(!image->flags & IMG_FLAG_EXTERNAL)
		{
			if(image->flags & IMG_FLAG_INVRAM)
				totalvr += image->size;
			else
				totalr += image->size;
		}

#ifdef USE_HASH_FOR_TEXTURES
		if (image != gltextures_hash[i])
		{
			hcollisions++;
			ri.Con_Printf (PRINT_ALL, "\x01[%4i] ", i);
		}
		else
			ri.Con_Printf (PRINT_ALL, "[%4i] ", i);
#endif

		switch (image->flags & IMG_TYPE_MASK)
		{
		case IMG_TYPE_SKIN:
			ri.Con_Printf (PRINT_ALL, "M ");
			break;
		case IMG_TYPE_SPRITE:
			ri.Con_Printf (PRINT_ALL, "S ");
			break;
		case IMG_TYPE_WALL:
			ri.Con_Printf (PRINT_ALL, "W ");
			break;
		case IMG_TYPE_PIC:
			ri.Con_Printf (PRINT_ALL, "P ");
			break;
		case IMG_TYPE_LM:
			ri.Con_Printf (PRINT_ALL, "L ");
			break;
		default:
			ri.Con_Printf (PRINT_ALL, "  ");
			break;
		}

		switch (image->flags & IMG_FORMAT_MASK)
		{
		case IMG_FORMAT_IND_32:
			ri.Con_Printf (PRINT_ALL, "I32  ");
			break;
		case IMG_FORMAT_IND_24:
			ri.Con_Printf (PRINT_ALL, "I24  ");
			break;
		case IMG_FORMAT_RGBA_8888:
			ri.Con_Printf (PRINT_ALL, "8888 ");
			break;
		case IMG_FORMAT_RGBA_4444:
			ri.Con_Printf (PRINT_ALL, "4444 ");
			break;
		case IMG_FORMAT_RGBA_5551:
			ri.Con_Printf (PRINT_ALL, "5551 ");
			break;
		case IMG_FORMAT_RGB_5650:
			ri.Con_Printf (PRINT_ALL, "5650 ");
			break;
		default:
			ri.Con_Printf (PRINT_ALL, "     ");
			break;
		}

		if(image->flags & IMG_FLAG_SCRAP)
			ri.Con_Printf (PRINT_ALL,  "SCRAP   ");
		else
			ri.Con_Printf (PRINT_ALL,  "%3i %3i ", image->uplwidth, image->uplheight);

		if(image->flags & IMG_FLAG_HAS_ALPHA)
			ri.Con_Printf (PRINT_ALL,  " ALP");
		else
			ri.Con_Printf (PRINT_ALL,  "    ");

		ri.Con_Printf (PRINT_ALL,  ": %s\n", image->name);
	}
	ri.Con_Printf (PRINT_ALL, "Total ram used %i\n", totalr);
	ri.Con_Printf (PRINT_ALL, "Total vram used %i\n", totalvr);
#ifdef USE_HASH_FOR_TEXTURES
	ri.Con_Printf (PRINT_ALL, "Total hash collisions: %i\n", hcollisions);
#endif
}


/*
=============================================================================

  scrap allocation

  Allocate all the little status bar obejcts into a single texture
  to crutch up inefficient hardware / drivers

=============================================================================
*/

#define	MAX_SCRAPS		1
#define	BLOCK_WIDTH		256
#define	BLOCK_HEIGHT	256

int			scrap_allocated[MAX_SCRAPS][BLOCK_WIDTH];
byte		scrap_texels[MAX_SCRAPS][BLOCK_WIDTH*BLOCK_HEIGHT] __attribute__((aligned(16)));

// returns a texture number and the position inside it
int Scrap_AllocBlock (int w, int h, int *x, int *y)
{
	int		i, j;
	int		best, best2;
	int		texnum;

	for (texnum=0 ; texnum<MAX_SCRAPS ; texnum++)
	{
		best = BLOCK_HEIGHT;

		for (i=0 ; i<BLOCK_WIDTH-w ; i++)
		{
			best2 = 0;

			for (j=0 ; j<w ; j++)
			{
				if (scrap_allocated[texnum][i+j] >= best)
					break;
				if (scrap_allocated[texnum][i+j] > best2)
					best2 = scrap_allocated[texnum][i+j];
			}
			if (j == w)
			{	// this is a valid spot
				*x = i;
				*y = best = best2;
			}
		}

		if (best + h > BLOCK_HEIGHT)
			continue;

		for (i=0 ; i<w ; i++)
			scrap_allocated[texnum][*x + i] = best + h;

		return texnum;
	}

	return -1;
//	Sys_Error ("Scrap_AllocBlock: full");
}

/*
=================================================================

PCX LOADING

=================================================================
*/


/*
==============
LoadPCX
==============
*/
void LoadPCX (char *filename, byte **pic, byte **palette, int *width, int *height)
{
	byte	*raw;
	pcx_t	*pcx;
	int		x, y;
	int		len;
	int		dataByte, runLength;
	byte	*out, *pix;
	int		lowmark;

	*pic = NULL;
	*palette = NULL;

	//
	// load the file
	//
	lowmark = ri.Hunk_LowMark();
	raw = ri.FS_LoadFile (filename, &len, FS_PATH_ALL | FS_FLAG_MHUNK);
	if (!raw)
	{
		ri.Con_Printf (PRINT_DEVELOPER, "Bad pcx file %s\n", filename);
		ri.Hunk_FreeToLowMark(lowmark);
		return;
	}

	//
	// parse the PCX file
	//
	pcx = (pcx_t *)raw;

    pcx->xmin = LittleShort(pcx->xmin);
    pcx->ymin = LittleShort(pcx->ymin);
    pcx->xmax = LittleShort(pcx->xmax);
    pcx->ymax = LittleShort(pcx->ymax);
    pcx->hres = LittleShort(pcx->hres);
    pcx->vres = LittleShort(pcx->vres);
    pcx->bytes_per_line = LittleShort(pcx->bytes_per_line);
    pcx->palette_type = LittleShort(pcx->palette_type);

	raw = &pcx->data;

	if (pcx->manufacturer != 0x0a
		|| pcx->version != 5
		|| pcx->encoding != 1
		|| pcx->bits_per_pixel != 8
		|| pcx->xmax >= 640
		|| pcx->ymax >= 480)
	{
		ri.Con_Printf (PRINT_ALL, "Bad pcx file %s\n", filename);
		return;
	}

	out = malloc ( (pcx->ymax+1) * (pcx->xmax+1) );
	if(!out)
		ri.Sys_Error (ERR_DROP, "LoadPCX: not enough space for %s", filename);

	*pic = out;

	pix = out;

	if (palette)
	{
		*palette = malloc(768);
		if(!*palette)
			ri.Sys_Error (ERR_DROP, "LoadPCX: not enough space for %s", filename);
		memcpy (*palette, (byte *)pcx + len - 768, 768);
	}

	if (width)
		*width = pcx->xmax+1;
	if (height)
		*height = pcx->ymax+1;

	for (y=0 ; y<=pcx->ymax ; y++, pix += pcx->xmax+1)
	{
		for (x=0 ; x<=pcx->xmax ; )
		{
			dataByte = *raw++;

			if((dataByte & 0xC0) == 0xC0)
			{
				runLength = dataByte & 0x3F;
				dataByte = *raw++;
			}
			else
				runLength = 1;

			while(runLength-- > 0)
				pix[x++] = dataByte;
		}

	}

	if ( raw - (byte *)pcx > len)
	{
		ri.Con_Printf (PRINT_DEVELOPER, "PCX file %s was malformed", filename);
		free (*pic);
		*pic = NULL;
	}

	ri.Hunk_FreeToLowMark(lowmark);
}

/*
=========================================================

TARGA LOADING

=========================================================
*/

typedef struct _TargaHeader {
	unsigned char 	id_length, colormap_type, image_type;
	unsigned short	colormap_index, colormap_length;
	unsigned char	colormap_size;
	unsigned short	x_origin, y_origin, width, height;
	unsigned char	pixel_size, attributes;
} TargaHeader;


/*
=============
LoadTGA
=============
*/
void LoadTGA (char *name, byte **pic, int *width, int *height)
{
	int		columns, rows, numPixels;
	byte	*pixbuf;
	int		row, column;
	byte	*buf_p;
	byte	*buffer;
	int		length;
	TargaHeader		targa_header;
	byte			*targa_rgba;
	int		lowmark;
	byte tmp[2];

	*pic = NULL;

	//
	// load the file
	//
	lowmark = ri.Hunk_LowMark();
	buffer = ri.FS_LoadFile (name, &length, FS_PATH_ALL | FS_FLAG_MHUNK);
	if (!buffer)
	{
		ri.Con_Printf (PRINT_DEVELOPER, "Bad tga file %s\n", name);
		return;
	}

	buf_p = buffer;

	targa_header.id_length = *buf_p++;
	targa_header.colormap_type = *buf_p++;
	targa_header.image_type = *buf_p++;

	tmp[0] = buf_p[0];
	tmp[1] = buf_p[1];
	targa_header.colormap_index = LittleShort ( *((short *)tmp) );
	buf_p+=2;
	tmp[0] = buf_p[0];
	tmp[1] = buf_p[1];
	targa_header.colormap_length = LittleShort ( *((short *)tmp) );
	buf_p+=2;
	targa_header.colormap_size = *buf_p++;
	targa_header.x_origin = LittleShort ( *((short *)buf_p) );
	buf_p+=2;
	targa_header.y_origin = LittleShort ( *((short *)buf_p) );
	buf_p+=2;
	targa_header.width = LittleShort ( *((short *)buf_p) );
	buf_p+=2;
	targa_header.height = LittleShort ( *((short *)buf_p) );
	buf_p+=2;
	targa_header.pixel_size = *buf_p++;
	targa_header.attributes = *buf_p++;

	if (targa_header.image_type!=2
		&& targa_header.image_type!=10)
		ri.Sys_Error (ERR_DROP, "LoadTGA: Only type 2 and 10 targa RGB images supported\n");

	if (targa_header.colormap_type !=0
		|| (targa_header.pixel_size!=32 && targa_header.pixel_size!=24))
		ri.Sys_Error (ERR_DROP, "LoadTGA: Only 32 or 24 bit images supported (no colormaps)\n");

	columns = targa_header.width;
	rows = targa_header.height;
	numPixels = columns * rows;

	if (width)
		*width = columns;
	if (height)
		*height = rows;

	targa_rgba = malloc (numPixels*4);
	if(!targa_rgba)
		ri.Sys_Error (ERR_DROP, "LoadTGA: not enough space for %s", name);
	*pic = targa_rgba;

	if (targa_header.id_length != 0)
		buf_p += targa_header.id_length;  // skip TARGA image comment

	if (targa_header.image_type==2) {  // Uncompressed, RGB images
		for(row=rows-1; row>=0; row--) {
			pixbuf = targa_rgba + row*columns*4;
			for(column=0; column<columns; column++) {
				unsigned char red,green,blue,alphabyte;
				switch (targa_header.pixel_size) {
					case 24:

							blue = *buf_p++;
							green = *buf_p++;
							red = *buf_p++;
							*pixbuf++ = red;
							*pixbuf++ = green;
							*pixbuf++ = blue;
							*pixbuf++ = 255;
							break;
					case 32:
							blue = *buf_p++;
							green = *buf_p++;
							red = *buf_p++;
							alphabyte = *buf_p++;
							*pixbuf++ = red;
							*pixbuf++ = green;
							*pixbuf++ = blue;
							*pixbuf++ = alphabyte;
							break;
				}
			}
		}
	}
	else if (targa_header.image_type==10) {   // Runlength encoded RGB images
		unsigned char red,green,blue,alphabyte,packetHeader,packetSize,j;
		for(row=rows-1; row>=0; row--) {
			pixbuf = targa_rgba + row*columns*4;
			for(column=0; column<columns; ) {
				packetHeader= *buf_p++;
				packetSize = 1 + (packetHeader & 0x7f);
				if (packetHeader & 0x80) {        // run-length packet
					switch (targa_header.pixel_size) {
						case 24:
								blue = *buf_p++;
								green = *buf_p++;
								red = *buf_p++;
								alphabyte = 255;
								break;
						case 32:
								blue = *buf_p++;
								green = *buf_p++;
								red = *buf_p++;
								alphabyte = *buf_p++;
								break;
					}

					for(j=0;j<packetSize;j++) {
						*pixbuf++=red;
						*pixbuf++=green;
						*pixbuf++=blue;
						*pixbuf++=alphabyte;
						column++;
						if (column==columns) { // run spans across rows
							column=0;
							if (row>0)
								row--;
							else
								goto breakOut;
							pixbuf = targa_rgba + row*columns*4;
						}
					}
				}
				else {                            // non run-length packet
					for(j=0;j<packetSize;j++) {
						switch (targa_header.pixel_size) {
							case 24:
									blue = *buf_p++;
									green = *buf_p++;
									red = *buf_p++;
									*pixbuf++ = red;
									*pixbuf++ = green;
									*pixbuf++ = blue;
									*pixbuf++ = 255;
									break;
							case 32:
									blue = *buf_p++;
									green = *buf_p++;
									red = *buf_p++;
									alphabyte = *buf_p++;
									*pixbuf++ = red;
									*pixbuf++ = green;
									*pixbuf++ = blue;
									*pixbuf++ = alphabyte;
									break;
						}
						column++;
						if (column==columns) { // pixel packet run spans across rows
							column=0;
							if (row>0)
								row--;
							else
								goto breakOut;
							pixbuf = targa_rgba + row*columns*4;
						}
					}
				}
			}
			breakOut:;
		}
	}

	ri.Hunk_FreeToLowMark(lowmark);
}


/*
====================================================================

IMAGE FLOOD FILLING

====================================================================
*/


/*
=================
Mod_FloodFillSkin

Fill background pixels so mipmapping doesn't have haloes
=================
*/

typedef struct
{
	short		x, y;
} floodfill_t;

// must be a power of 2
#define FLOODFILL_FIFO_SIZE 0x1000
#define FLOODFILL_FIFO_MASK (FLOODFILL_FIFO_SIZE - 1)

#define FLOODFILL_STEP( off, dx, dy ) \
{ \
	if (pos[off] == fillcolor) \
	{ \
		pos[off] = 255; \
		fifo[inpt].x = x + (dx), fifo[inpt].y = y + (dy); \
		inpt = (inpt + 1) & FLOODFILL_FIFO_MASK; \
	} \
	else if (pos[off] != 255) fdc = pos[off]; \
}

void R_FloodFillSkin( byte *skin, int skinwidth, int skinheight )
{
	byte				fillcolor = *skin; // assume this is the pixel to fill
	floodfill_t			fifo[FLOODFILL_FIFO_SIZE];
	int					inpt = 0, outpt = 0;
	int					filledcolor = -1;
	int					i;

	if (filledcolor == -1)
	{
		filledcolor = 0;
		// attempt to find opaque black
		for (i = 0; i < 256; ++i)
			if (d_8to24table[i] == (255 << 0)) // alpha 1.0
			{
				filledcolor = i;
				break;
			}
	}

	// can't fill to filled color or to transparent color (used as visited marker)
	if ((fillcolor == filledcolor) || (fillcolor == 255))
	{
		//printf( "not filling skin from %d to %d\n", fillcolor, filledcolor );
		return;
	}

	fifo[inpt].x = 0, fifo[inpt].y = 0;
	inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;

	while (outpt != inpt)
	{
		int			x = fifo[outpt].x, y = fifo[outpt].y;
		int			fdc = filledcolor;
		byte		*pos = &skin[x + skinwidth * y];

		outpt = (outpt + 1) & FLOODFILL_FIFO_MASK;

		if (x > 0)				FLOODFILL_STEP( -1, -1, 0 );
		if (x < skinwidth - 1)	FLOODFILL_STEP( 1, 1, 0 );
		if (y > 0)				FLOODFILL_STEP( -skinwidth, 0, -1 );
		if (y < skinheight - 1)	FLOODFILL_STEP( skinwidth, 0, 1 );
		skin[x + skinwidth * y] = fdc;
	}
}

//=======================================================


/*
================
GL_ResampleTexture
================
*/
void GL_ResampleTexture32 (const byte *src, int inwidth, int inheight, const byte *dst, int outwidth, int outheight)
{
	int		i, j;
	unsigned	*in, *out;
	unsigned	*inrow, *inrow2;
	unsigned	frac, fracstep;
	unsigned	p1[1024], p2[1024];
	byte		*pix1, *pix2, *pix3, *pix4;

	in = (unsigned *)src;
	out = (unsigned *)dst;

	fracstep = inwidth*0x10000/outwidth;

	frac = fracstep>>2;
	for (i=0 ; i<outwidth ; i++)
	{
		p1[i] = 4*(frac>>16);
		frac += fracstep;
	}
	frac = 3*(fracstep>>2);
	for (i=0 ; i<outwidth ; i++)
	{
		p2[i] = 4*(frac>>16);
		frac += fracstep;
	}

	for (i=0 ; i<outheight ; i++, out += outwidth)
	{
		inrow = in + inwidth*(int)((i+0.25)*inheight/outheight);
		inrow2 = in + inwidth*(int)((i+0.75)*inheight/outheight);
		frac = fracstep >> 1;
		for (j=0 ; j<outwidth ; j++)
		{
			pix1 = (byte *)inrow + p1[j];
			pix2 = (byte *)inrow + p2[j];
			pix3 = (byte *)inrow2 + p1[j];
			pix4 = (byte *)inrow2 + p2[j];
			((byte *)(out+j))[0] = (pix1[0] + pix2[0] + pix3[0] + pix4[0])>>2;
			((byte *)(out+j))[1] = (pix1[1] + pix2[1] + pix3[1] + pix4[1])>>2;
			((byte *)(out+j))[2] = (pix1[2] + pix2[2] + pix3[2] + pix4[2])>>2;
			((byte *)(out+j))[3] = (pix1[3] + pix2[3] + pix3[3] + pix4[3])>>2;
		}
	}
}

/*
=================
GL_ResampleTexture8
=================
*/
void GL_ResampleTexture8 (const byte *src, int inwidth, int inheight, byte *dst, int outwidth, int outheight)
{
	uint		fracstep;
	const byte	*inrow;
	uint		frac;
	int			i, j;

	if( !src )
		return;

	fracstep = inwidth * 0x10000 / outwidth;

	for (i = 0; i < outheight ; ++i, dst += outwidth)
	{
		inrow	= src + inwidth * (i * inheight / outheight);
		frac	= fracstep >> 1;
		for (j = 0; j < outwidth; ++j, frac += fracstep)
		{
			dst[j] = inrow[frac >> 16];
		}
	}
}

/*
===============
GL_TextureSwizzle

fast swizzling
===============
*/
static void GL_TextureSwizzle (byte *dst, const byte *src, int width, int height)
{
	int		blockx, blocky;
	int		j;
	int		width_blocks = (width >> 4);
	int		height_blocks = (height >> 3);
	int		src_pitch = (width - 16) >> 2;
	int		src_row = width << 3;
	const byte	*ysrc = src;
	int		*dst32 = (int*)dst;

	for ( blocky = 0; blocky < height_blocks; ++blocky )
	{
		const byte* xsrc = ysrc;
		for (blockx = 0; blockx < width_blocks; ++blockx)
		{
			const int* src32 = (int*)xsrc;
			for (j = 0; j < 8; ++j)
			{
				*(dst32++) = *(src32++);
				*(dst32++) = *(src32++);
				*(dst32++) = *(src32++);
				*(dst32++) = *(src32++);
				src32 += src_pitch;
			}
			xsrc += 16;
		}
		ysrc += src_row;
	}
}

/*
================
GL_LightScaleTexture

Scale up the pixel values in a texture to increase the
lighting range
================
*/
void GL_LightScaleTexture (unsigned *in, int inwidth, int inheight, qboolean only_gamma )
{
	int		i, c;
	byte	*p;

	p = (byte *)in;

	c = inwidth*inheight;
	if ( only_gamma )
	{
		for (i=0 ; i<c ; i++, p+=4)
		{
			p[0] = gl_state.gammatable[p[0]];
			p[1] = gl_state.gammatable[p[1]];
			p[2] = gl_state.gammatable[p[2]];
		}
	}
	else
	{
		for (i=0 ; i<c ; i++, p+=4)
		{
			p[0] = gl_state.gammatable[gl_state.intensitytable[p[0]]];
			p[1] = gl_state.gammatable[gl_state.intensitytable[p[1]]];
			p[2] = gl_state.gammatable[gl_state.intensitytable[p[2]]];
		}
	}
}

/*
================
GL_MipMap

Operates in place, quartering the size of the texture
================
*/
void GL_MipMap (byte *in, int width, int height)
{
	int		i, j;
	byte	*out;

	width <<=2;
	height >>= 1;
	out = in;
	for (i=0 ; i<height ; i++, in+=width)
	{
		for (j=0 ; j<width ; j+=8, out+=4, in+=8)
		{
			out[0] = (in[0] + in[4] + in[width+0] + in[width+4])>>2;
			out[1] = (in[1] + in[5] + in[width+1] + in[width+5])>>2;
			out[2] = (in[2] + in[6] + in[width+2] + in[width+6])>>2;
			out[3] = (in[3] + in[7] + in[width+3] + in[width+7])>>2;
		}
	}
}

/*
===============
GL_PixelConverter
===============
*/
void GL_PixelConverter (byte *dst, const byte *src, int width, int height, int informat, int outformat)
{
	int		x, y;
	byte	color[4];
	int		tempColor;
	byte	correction;

	color[0] = color[1] = color[2] = color[3] = 0xff;

	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			// unpack
			switch (informat)
			{
			case GU_PSM_T8:
				color[0]  = (d_8to24table[*src]      ) & 0xff;
				color[1]  = (d_8to24table[*src] >> 8 ) & 0xff;
				color[2]  = (d_8to24table[*src] >> 16) & 0xff;
				color[3]  = (d_8to24table[*src] >> 24) & 0xff; src++;
				break;
			case GU_PSM_5650:
				color[0]  = (*src & 0x1f) << 3;
				color[1]  = (*src & 0xe0) >> 3; src++;
				color[1] |= (*src & 0x07) << 5;
				color[2]  = (*src & 0xf8);      src++;
				color[3]  = 0xff;
				break;
			case GU_PSM_5551:
				color[0]  = (*src & 0x1f) << 3;
				color[1]  = (*src & 0xe0) >> 2; src++;
				color[1] |= (*src & 0x03) << 6;
				color[2]  = (*src & 0x7c) << 1;
				color[3]  = (*src & 0x80) ? 0xff : 0x00; src++;
				break;
			case GU_PSM_4444:
				color[0]  = (*src & 0x0f) << 4;
				color[1]  = (*src & 0xf0);      src++;
				color[2]  = (*src & 0x0f) << 4;
				color[3]  = (*src & 0xf0);      src++;
				break;
			case GU_PSM_8888:
				color[0]  = *src; src++;
				color[1]  = *src; src++;
				color[2]  = *src; src++;
				color[3]  = *src; src++;
				break;
			default:
				ri.Sys_Error (ERR_DROP, "GL_PixelConverter: unknown input format\n");
				break;
			}

			// pack
			switch (outformat)
			{
			case GU_PSM_T8:
				if (!gl_state.d_16to8table)
					ri.Sys_Error (ERR_DROP, "GL_PixelConverter: d_16to8table is null\n");

				// to 565
				tempColor  = (color[0] >> 3) & 0x01f;
				tempColor |= (color[1] << 3) & 0x7e0;
				tempColor |= (color[2] << 8) & 0xf80;

				*dst = gl_state.d_16to8table[tempColor]; dst++;
				break;
			case GU_PSM_5650:
				*dst  = (color[0] >> 3) & 0x1f;
				*dst |= (color[1] << 3) & 0xe0; dst++;
				*dst  = (color[1] >> 5) & 0x07;
				*dst |= (color[2]     ) & 0xf8; dst++;
				break;
			case GU_PSM_5551:
				*dst  = (color[0] >> 3);
				*dst |= (color[1] << 2) & 0xe0; dst++;
				*dst  = (color[1] >> 6) & 0x03;
				*dst |= (color[2] >> 1) & 0x7c;
				*dst |= (color[3]     ) & 0x80; dst++;
				break;
			case GU_PSM_4444:
				*dst  = (color[0] >> 4) & 0x0f;
				*dst |= (color[1]     ) & 0xf0; dst++;
				*dst  = (color[2] >> 4) & 0x0f;
				*dst |= (color[3]     ) & 0xf0; dst++;
				break;
			case GU_PSM_8888:
				*dst = color[0]; dst++;
				*dst = color[1]; dst++;
				*dst = color[2]; dst++;
				*dst = color[3]; dst++;
				break;
			default:
				ri.Sys_Error (ERR_DROP, "GL_PixelConverter: unknown output format\n");
				break;
			}
		}
	}
}

/*
===============
GL_SetTextureDimensions
===============
*/
static void GL_SetTextureDimensions (image_t *image, int width, int height)
{
	int		step = (int)gl_round_down->value;
	int		scaled_width, scaled_height;

	// store original sizes
	image->width = width;
	image->height = height;

	for (scaled_width = 1; scaled_width < width; scaled_width <<= 1);

	for (scaled_height = 1; scaled_height < height; scaled_height <<= 1);

	if(!IMG_IS_PIC(image))
	{
		if (gl_round_down->value && scaled_width > width)
			scaled_width >>= 1;

		if (gl_round_down->value && scaled_height > height)
			scaled_height >>= 1;
	}

	if (scaled_width > TEXTURE_SIZE_MAX || scaled_height > TEXTURE_SIZE_MAX)
	{
		while (scaled_width > TEXTURE_SIZE_MAX || scaled_height > TEXTURE_SIZE_MAX)
		{
			scaled_width >>= 1;
			scaled_height >>= 1;
		}
	}

	// set the texture dimensions
	image->uplwidth  = Q_max (TEXTURE_SIZE_MIN, scaled_width);
	image->uplheight = Q_max (TEXTURE_SIZE_MIN, scaled_height);
}

/*
==================
GL_SetTextureFormat
==================
*/
static void GL_SetTextureFormat (image_t *image)
{
	switch(image->flags & IMG_FORMAT_MASK)
	{
	case IMG_FORMAT_IND_32: // default
		image->format = GU_PSM_T8;
		break;
	case IMG_FORMAT_IND_24:
		image->format = GU_PSM_T8;
		break;
	case IMG_FORMAT_RGBA_8888:
		image->format = GU_PSM_8888;
		break;
	case IMG_FORMAT_RGBA_4444:
		image->format = GU_PSM_4444;
		break;
	case IMG_FORMAT_RGBA_5551:
		image->format = GU_PSM_5551;
		break;
	case IMG_FORMAT_RGB_5650:
		image->format = GU_PSM_5650;
		break;
	}
}

/*
==================
GL_CalcTextureSize
==================
*/
static size_t GL_CalcTextureSize (int format, int width, int height, byte *outbpp)
{
	size_t		size = 0;
	byte		bpp = 0;

	switch(format)
	{
	case GU_PSM_T4:
	case GU_PSM_DXT1:
		size = ((width * height) >> 1);
		break;
	case GU_PSM_T8:
	case GU_PSM_DXT3:
	case GU_PSM_DXT5:
		bpp = 1;
		size = width * height;
		break;
	case GU_PSM_T16:
	case GU_PSM_4444:
	case GU_PSM_5551:
	case GU_PSM_5650:
		bpp = 2;
		size = width * height * 2;
		break;
	case GU_PSM_T32:
	case GU_PSM_8888:
		bpp = 4;
		size = width * height * 4;
		break;
	default:
		ri.Sys_Error (ERR_DROP, "GL_CalcTextureSize: bad texture internal format (%u)\n", format);
		break;
	}

	if(outbpp != NULL) *outbpp = bpp;

	return size;
}

/*
==================
GL_HasAplha
==================
*/
static qboolean GL_TextureHasAplha (const byte *in, int width, int height, byte bpp)
{
	int			i;
	const byte	*scan;

	scan = in + bpp - 1;
	for (i = 0; i < width * height; i++)
	{
		if ((bpp == 1 && *scan == 255) || (bpp != 1 && *scan != 255))
			return true;
		scan += bpp;
	}
	return false;
}

/*
===============
GL_UploadTexture

Returns has_alpha
===============
*/
void GL_UploadTexture (image_t *image, byte *pic, int width, int height)
{
	int			scaled_width, scaled_height;
	qboolean	swizzle;
	byte		*tempbuff;
	int			i;
	int			mark;

	GL_SetTextureDimensions (image, width, height);
	GL_SetTextureFormat (image);

	image->size = GL_CalcTextureSize (image->format, image->uplwidth, image->uplheight, &image->bpp);

	swizzle = false;

	if (!pic)
		return;

	if (!(image->flags & IMG_FLAG_DYNAMIC))
	{
		swizzle = true;

		if (GL_TextureHasAplha (pic, width, height, image->bpp))
			image->flags |=	IMG_FLAG_HAS_ALPHA;
	}

	if (image->bpp == 1)
		image->flags |= IMG_FLAG_PALLETED;

	// allocate
	image->data = (byte*)valloc (image->size);
	if (!image->data)
	{
		image->data = (byte*)memalign (16, image->size);
		if (!image->data)
			ri.Sys_Error (ERR_DROP, "GL_UploadTexture: %s out of memory for texture ( %lu )\n", image->name, image->size );
	}
	else image->flags |= IMG_FLAG_INVRAM;

	if (swizzle)
	{
		mark = ri.Hunk_LowMark ();
		tempbuff = ri.Hunk_Alloc (image->size);
	}
	else
		tempbuff = image->data;

	image->sl = 0;
	image->tl = 0;

	if ((image->uplwidth != width || image->uplheight != height) /*&& !IMG_IS_PIC(image)*/)
	{
		if (image->bpp == 1)
			GL_ResampleTexture8 (pic, width, height, tempbuff, image->uplwidth, image->uplheight);
		else if (image->bpp == 4)
			GL_ResampleTexture32 (pic, width, height, tempbuff, image->uplwidth, image->uplheight);

		image->sh = image->uplwidth;
		image->th = image->uplheight;
	}
	else
	{
		for(i = 0; i < height; i++)
			memcpy (&tempbuff[i * image->uplwidth * image->bpp], &pic[i * width * image->bpp], width * image->bpp);

		image->sh = width;
		image->th = height;
	}

	if (swizzle)
	{
		GL_TextureSwizzle (image->data, tempbuff, image->uplwidth * image->bpp, image->uplheight);
		ri.Hunk_FreeToLowMark (mark);
		image->flags |= IMG_FLAG_SWIZZLED;
	}

	//GL_LightScaleTexture (scaled, scaled_width, scaled_height, !mipmap );

	sceKernelDcacheWritebackRange (image->data, image->size);
}

/*
===============
GL_UploadExternal
===============
*/
void GL_UploadExternal (image_t *image, byte *pic, int width, int height)
{
	int			scaled_width, scaled_height;
	qboolean	swizzle;
	byte		*tempbuff;
	int			i;
	int			mark;

	GL_SetTextureFormat (image);

	image->width = image->uplwidth = width;
	image->height = image->uplheight = height;
	image->size = GL_CalcTextureSize (image->format, image->uplwidth, image->uplheight, &image->bpp);

	if (!pic)
		ri.Sys_Error (ERR_DROP, "GL_UploadExternal: %s without external buffer!\n", image->name);

	if ((int)pic % 16 != 0) // check align
		ri.Sys_Error (ERR_DROP, "GL_UploadExternal: %s texture buffer is not aligned to 16-bytes!\n", image->name);

	if (image->bpp == 1)
		image->flags |= IMG_FLAG_PALLETED;

	image->data = pic;
	image->sl = 0;
	image->tl = 0;
	image->sh = image->width;
	image->th = image->height;
}

/*
===============
GL_UploadScrap
===============
*/
qboolean GL_UploadScrap (image_t *image, byte *pic, int width, int height)
{
	int		x, y;
	int		i, j, k;
	int		texnum;

	x = y = 0;

	texnum = Scrap_AllocBlock (width, height, &x, &y);
	if (texnum == -1)
		return false;

	// copy the texels into the scrap block
	k = 0;
	for (i=0 ; i<height ; i++)
		for (j=0 ; j<width ; j++, k++)
			scrap_texels[texnum][(y+i)*BLOCK_WIDTH + x + j] = pic[k];

	image->format = GU_PSM_T8;
	image->bpp = 1;
	image->data = (byte *)scrap_texels[texnum];
	image->width = width;
	image->height = height;
	image->uplwidth = BLOCK_WIDTH;
	image->uplheight = BLOCK_HEIGHT;
	image->flags |= IMG_FLAG_HAS_ALPHA | IMG_FLAG_SCRAP | IMG_FLAG_PALLETED;
	image->size = 0;

	image->sl = x;
	image->tl = y;
	image->sh = x + image->width;
	image->th = y + image->height;

	return true;
}

/*
===============
GL_UpdateTexture
===============
*/
qboolean GL_UpdateTexture (image_t *image, int xoff, int yoff, int width, int height, const void *buffer)
{
	byte		*dst, *src;

	if(!(image->flags & IMG_FLAG_DYNAMIC))
	{
		ri.Con_Printf (PRINT_ALL, "GL_UpdateTexture: !IMG_FLAG_DYNAMIC\n");
		return false;
	}

	if ((image->uplwidth < width + xoff) || (image->uplheight < height + yoff))
	{
		ri.Con_Printf (PRINT_ALL, "GL_UpdateTexture: %s invalid update area size XY[%d x %d] WH[%d x %d]\n", image->name, width, height, xoff, yoff);
		return false;
	}

	src = (byte *)buffer;
	dst = image->data + (yoff * image->uplwidth + xoff) * image->bpp;
	while (height--)
	{
		memcpy (dst, src, width * image->bpp);
		dst += image->uplwidth * image->bpp;
		src += width * image->bpp;
	}

	sceKernelDcacheWritebackRange (image->data, image->size);
	return true;
}

/*
================
GL_LoadPic

This is also used as an entry point for the generated r_notexture
================
*/
image_t *GL_LoadPic (char *name, byte *pic, int width, int height, int flags)
{
	image_t		*image;
	int			index, i;

	// find a free image_t
	for (index=0, image=gltextures ; index<numgltextures ; index++,image++)
	{
		if (!image->name[0])
			break;
	}
	if (index == numgltextures)
	{
		if (numgltextures == MAX_GLTEXTURES)
			ri.Sys_Error (ERR_DROP, "MAX_GLTEXTURES");
		numgltextures++;
	}
	image = &gltextures[index];

	if (strlen(name) >= sizeof(image->name))
		ri.Sys_Error (ERR_DROP, "Draw_LoadPic: \"%s\" is too long", name);

	strcpy (image->name, name);
	image->flags = flags;
	image->texnum = index;

#ifdef USE_HASH_FOR_TEXTURES
	// add to hash
	image->hashkey = Com_StringHash (name, MAX_GLTEXTURES_HASH);
	image->nexthash = gltextures_hash[image->hashkey];
	gltextures_hash[image->hashkey] = image;
#endif

	if (IMG_IS_SKIN(image) && IMG_IS_IND(image))
		R_FloodFillSkin(pic, width, height);

	// use external buffer
	if (flags & IMG_FLAG_EXTERNAL)
	{
		GL_UploadExternal (image, pic, width, height);
		return image;
	}

	// load little pics into the scrap
	if (IMG_IS_PIC(image) && IMG_IS_IND(image) && width < 64 && height < 64)
	{
		if (GL_UploadScrap (image, pic, width, height))
			return image;
	}

	GL_UploadTexture (image, pic, width, height);

	return image;
}


/*
================
GL_LoadWal
================
*/
image_t *GL_LoadWal (char *name)
{
	miptex_t	*mt;
	int			width, height, ofs;
	image_t		*image;
	int			lowmark;

	lowmark = ri.Hunk_LowMark();
	mt = (miptex_t *)ri.FS_LoadFile (name, NULL, FS_PATH_ALL | FS_FLAG_MHUNK);
	if (!mt)
	{
		ri.Con_Printf (PRINT_ALL, "GL_FindImage: can't load %s\n", name);
		return r_notexture;
	}

	width = LittleLong (mt->width);
	height = LittleLong (mt->height);
	ofs = LittleLong (mt->offsets[0]);

	image = GL_LoadPic (name, (byte *)mt + ofs, width, height, IMG_TYPE_WALL | IMG_FORMAT_IND_32);

	ri.Hunk_FreeToLowMark(lowmark);

	return image;
}

/*
===============
GL_FindImage

Finds or loads the given image
===============
*/
image_t	*GL_FindImage (char *name, int flags)
{
	image_t	*image;
	int		i, len;
	byte	*pic, *palette;
	int		width, height;
	uint	hashkey;

	if (!name)
		return NULL;	//	ri.Sys_Error (ERR_DROP, "GL_FindImage: NULL name");
	len = strlen(name);
	if (len<5)
		return NULL;	//	ri.Sys_Error (ERR_DROP, "GL_FindImage: bad name: %s", name);

	// look for it
#ifdef USE_HASH_FOR_TEXTURES
	hashkey = Com_StringHash (name, MAX_GLTEXTURES_HASH);
	for (image = gltextures_hash[hashkey]; image != NULL; image = image->nexthash)
#else
	for (i=0, image=gltextures ; i<numgltextures ; i++,image++)
#endif
	{
		if (!stricmp(name, image->name))
			return image;
	}

	//
	// load the pic from disk
	//
	pic = NULL;
	palette = NULL;
	if (!strcmp(name+len-4, ".pcx"))
	{
		LoadPCX (name, &pic, &palette, &width, &height);
		if (!pic)
			return NULL; // ri.Sys_Error (ERR_DROP, "GL_FindImage: can't load %s", name);
		image = GL_LoadPic (name, pic, width, height, flags | IMG_FORMAT_IND_32);
	}
	else if (!strcmp(name+len-4, ".wal"))
	{
		image = GL_LoadWal (name);
	}
	else if (!strcmp(name+len-4, ".tga"))
	{
		LoadTGA (name, &pic, &width, &height);
		if (!pic)
			return NULL; // ri.Sys_Error (ERR_DROP, "GL_FindImage: can't load %s", name);
		image = GL_LoadPic (name, pic, width, height, flags | IMG_FORMAT_RGBA_8888);
	}
	else
		return NULL;	//	ri.Sys_Error (ERR_DROP, "GL_FindImage: bad extension on: %s", name);


	if (pic)
		free(pic);
	if (palette)
		free(palette);

	return image;
}



/*
===============
R_RegisterSkin
===============
*/
struct image_s *R_RegisterSkin (char *name)
{
	return GL_FindImage (name, IMG_TYPE_SKIN);
}

/*
================
GL_FreeImage
================
*/
void GL_FreeImage (image_t *image)
{
#ifdef USE_HASH_FOR_TEXTURES
	// remove from hash
	image_t	**prev;

	for (prev = &gltextures_hash[image->hashkey]; *prev != NULL; prev = &(*prev)->nexthash)
	{
		if(*prev == image)
		{
			*prev = image->nexthash;
			break;
		}
	}
#endif

	// free it
	if(image->data && !(image->flags & IMG_FLAG_EXTERNAL))
	{
		if(image->flags & IMG_FLAG_INVRAM)
			vfree(image->data);
		else
			free(image->data);
	}
	memset (image, 0, sizeof(*image));

}

/*
================
GL_FreeImages

Any image that was not touched on this registration sequence
will be freed.
================
*/
void GL_FreeImages (void)
{
	int		i;
	image_t	*image;

	// never free r_notexture or particle texture
	//r_notexture->registration_sequence = registration_sequence;
	//r_particletexture->registration_sequence = registration_sequence;

	for (i = 0, image = gltextures ; i < numgltextures ; i++, image++)
	{
		if (!image->name[0])
			continue;
		if (image == r_particletexture || image == r_notexture)
			continue;
		if (image->flags & IMG_FLAG_SCRAP)
			continue;
		if (image->flags & IMG_TYPE_PIC)
			continue;		// don't free pics

		// free it
		GL_FreeImage(image);
	}
}


/*
===============
Draw_GetPalette
===============
*/
int Draw_GetPalette (void)
{
	int		i;
	int		r, g, b;
	unsigned	v;
	byte	*pic, *pal;
	int		width, height;

	// get the palette

	LoadPCX ("pics/colormap.pcx", &pic, &pal, &width, &height);
	if (!pal)
		ri.Sys_Error (ERR_FATAL, "Couldn't load pics/colormap.pcx");

	for (i=0 ; i<256 ; i++)
	{
		r = pal[i*3+0];
		g = pal[i*3+1];
		b = pal[i*3+2];

		v = (255<<24) + (r<<0) + (g<<8) + (b<<16);
		d_8to24table[i] = LittleLong(v);
	}

	d_8to24table[255] &= LittleLong(0xffffff);	// 255 is transparent

	free (pic);
	free (pal);

	return 0;
}

/*
===============
GL_BuildGammaTable
===============
*/
void GL_BuildGammaTable (float gamma, float intensity)
{
	int		i, j;
	byte	*p;

	for (i = 0; i < 256; i++)
	{
		if (gamma == 1.0)
		{
			gl_state.gammatable[i] = i;
		}
		else
		{
			float inf;

			inf = 255 * pow ( (i+0.5)/255.5 , gamma ) + 0.5;
			if (inf < 0)
				inf = 0;
			if (inf > 255)
				inf = 255;
			gl_state.gammatable[i] = inf;
		}
	}

	for (i = 0; i < 256; i++)
	{
		j = i * intensity;
		if (j > 255)
			j = 255;
		gl_state.intensitytable[i] = j;
	}

	gl_state.inverse_intensity = 1 / intensity;
}

/*
===============
GL_InitImages
===============
*/
void	GL_InitImages (void)
{
	memset (gltextures, 0, sizeof(gltextures));
#ifdef USE_HASH_FOR_TEXTURES
	memset (gltextures_hash, 0, sizeof(gltextures_hash));
#endif

	// init intensity conversions
	intensity = ri.Cvar_Get ("intensity", "2", 0);

	if ( intensity->value <= 1 )
		ri.Cvar_Set( "intensity", "1" );

	Draw_GetPalette ();
/*
	gl_state.d_16to8table = ri.FS_LoadFile("pics/16to8.dat", NULL, FS_PATH_ALL);
	if ( !gl_state.d_16to8table )
		ri.Sys_Error( ERR_FATAL, "Couldn't load pics/16to8.pcx");
*/
	GL_BuildGammaTable (vid_gamma->value, intensity->value);
}

/*
===============
GL_ShutdownImages
===============
*/
void	GL_ShutdownImages (void)
{
	int		i;
	image_t	*image;

	for (i=0, image=gltextures ; i<numgltextures ; i++, image++)
	{
		if (!image->name[0])
			continue;
		if (image->flags & IMG_FLAG_SCRAP)
			continue;

		// free it
		GL_FreeImage(image);
	}

	//if (gl_state.d_16to8table)
	//	FS_FreeFile (gl_state.d_16to8table);
}

