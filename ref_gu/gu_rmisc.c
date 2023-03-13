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
// r_misc.c

#include "gu_local.h"

//#define PARTICLE_8BIT

/*
==================
R_InitParticleTexture
==================
*/
#ifdef PARTICLE_8BIT

static byte	dottexture[8][8] =
{
	{0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff},
	{0xff,0xff,0xff,0x00,0x00,0xff,0xff,0xff},
	{0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff},
	{0xff,0x00,0x00,0x00,0x00,0x00,0x00,0xff},
	{0xff,0x00,0x00,0x00,0x00,0x00,0x00,0xff},
	{0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff},
	{0xff,0xff,0xff,0x00,0x00,0xff,0xff,0xff},
	{0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff},
};

static byte	notexture[8][8] =
{
	{0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f},
	{0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f},
	{0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f},
	{0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f},
	{0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00},
	{0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00},
	{0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00},
	{0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00},
};


#else

static byte	dottexture[8][8] =
{
	{0,0,0,0,0,0,0,0},
	{0,0,1,1,0,0,0,0},
	{0,1,1,1,1,0,0,0},
	{0,1,1,1,1,0,0,0},
	{0,0,1,1,0,0,0,0},
	{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
};

#endif

void R_InitParticleTexture (void)
{
	int		x,y;
#ifdef PARTICLE_8BIT
	//
	// particle texture
	//
	r_particletexture = GL_LoadPic ("***particle***", (byte *)dottexture, 8, 8, IMG_TYPE_SPRITE | IMG_FORMAT_IND_32);

	//
	// also use this for bad textures, but without alpha
	//
	r_notexture = GL_LoadPic ("***r_notexture***", (byte *)notexture, 8, 8, IMG_TYPE_WALL | IMG_FORMAT_IND_32);

#else
	byte	data[8][8][4];

	//
	// particle texture
	//

	for (x=0 ; x<8 ; x++)
	{
		for (y=0 ; y<8 ; y++)
		{
			data[y][x][0] = 255;
			data[y][x][1] = 255;
			data[y][x][2] = 255;
			data[y][x][3] = dottexture[x][y]*255;
		}
	}
	r_particletexture = GL_LoadPic ("***particle***", (byte *)data, 8, 8, IMG_TYPE_SPRITE | IMG_FORMAT_RGBA_8888);

	//
	// also use this for bad textures, but without alpha
	//
	for (x=0 ; x<8 ; x++)
	{
		for (y=0 ; y<8 ; y++)
		{
			data[y][x][0] = dottexture[x&3][y&3]*255;
			data[y][x][1] = 0; // dottexture[x&3][y&3]*255;
			data[y][x][2] = 0; //dottexture[x&3][y&3]*255;
			data[y][x][3] = 255;
		}
	}
	r_notexture = GL_LoadPic ("***r_notexture***", (byte *)data, 8, 8, IMG_TYPE_WALL | IMG_FORMAT_RGBA_8888);
#endif
}


/*
==============================================================================

						SCREEN SHOTS

==============================================================================
*/

typedef struct _TargaHeader {
	unsigned char 	id_length, colormap_type, image_type;
	unsigned short	colormap_index, colormap_length;
	unsigned char	colormap_size;
	unsigned short	x_origin, y_origin, width, height;
	unsigned char	pixel_size, attributes;
} TargaHeader;


/*
==================
GL_ScreenShot_f
==================
*/
void GL_ScreenShot_f (void)
{
	byte		*buffer;
	char		picname[80];
	char		checkname[MAX_OSPATH];
	int			i, j, size, temp;
	byte		*dst, *src;

//
// find a file name to save it to
//
	strcpy(picname,"quake00.tga");

	for (i=0 ; i<=99 ; i++)
	{
		picname[5] = i/10 + '0';
		picname[6] = i%10 + '0';
		Com_sprintf (checkname, sizeof(checkname), "scrnshot/%s", picname);
		if (!ri.FS_FileExists(checkname, FS_PATH_GAMEDIR | FS_TYPE_RFS))
			break;	// file doesn't exist
	}
	if (i==100)
	{
		ri.Con_Printf (PRINT_ALL, "SCR_ScreenShot_f: Couldn't create a file\n");
		return;
 	}

	size = vid.width*vid.height*3 + 18;
	buffer = malloc(size);
	memset (buffer, 0, 18);
	buffer[2] = 2;		// uncompressed type
	buffer[12] = vid.width&255;
	buffer[13] = vid.width>>8;
	buffer[14] = vid.height&255;
	buffer[15] = vid.height>>8;
	buffer[16] = 24;	// pixel size

	src = (byte*)gu_render.buffer.disp_ptr;
	dst = buffer+18;
	for(i = 0; i < vid.height; i++)
	{
		/***/
		for(j = 0; j < vid.width; j++)
		{
			// 565 to bgr
			dst[j * 3 + 2]  = (src[j * 2 + 0] & 0x1f) << 3;
			dst[j * 3 + 1]  = (src[j * 2 + 0] & 0xe0) >> 3;
			dst[j * 3 + 1] |= (src[j * 2 + 1] & 0x07) << 5;
			dst[j * 3 + 0]  = (src[j * 2 + 1] & 0xf8);
		}
		/***/
		dst += vid.width * 3;
		src += gu_render.buffer.width * 2;
	}

	ri.FS_WriteFile(checkname, buffer, size, FS_PATH_GAMEDIR);

	free (buffer);

	ri.Con_Printf (PRINT_ALL, "Wrote %s\n", picname);
}

/*
** GL_SetDefaultState
*/
void GL_SetDefaultState( void )
{
/*
	qglClearColor (1,0, 0.5 , 0.5);
	qglCullFace(GL_FRONT);
	qglEnable(GL_TEXTURE_2D);

	qglEnable(GL_ALPHA_TEST);
	qglAlphaFunc(GL_GREATER, 0.666);

	qglDisable (GL_DEPTH_TEST);
	qglDisable (GL_CULL_FACE);
	qglDisable (GL_BLEND);

	qglColor4f (1,1,1,1);

	qglPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
	qglShadeModel (GL_FLAT);

	GL_TextureMode( gl_texturemode->string );
	GL_TextureAlphaMode( gl_texturealphamode->string );
	GL_TextureSolidMode( gl_texturesolidmode->string );

	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);

	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	qglBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	GL_TexEnv( GL_REPLACE );

	if ( qglPointParameterfEXT )
	{
		float attenuations[3];

		attenuations[0] = gl_particle_att_a->value;
		attenuations[1] = gl_particle_att_b->value;
		attenuations[2] = gl_particle_att_c->value;

		qglEnable( GL_POINT_SMOOTH );
		qglPointParameterfEXT( GL_POINT_SIZE_MIN_EXT, gl_particle_min_size->value );
		qglPointParameterfEXT( GL_POINT_SIZE_MAX_EXT, gl_particle_max_size->value );
		qglPointParameterfvEXT( GL_DISTANCE_ATTENUATION_EXT, attenuations );
	}

	if ( qglColorTableEXT && gl_ext_palettedtexture->value )
	{
		qglEnable( GL_SHARED_TEXTURE_PALETTE_EXT );

		GL_SetTexturePalette( d_8to24table );
	}

	GL_UpdateSwapInterval();
*/
}

void GL_UpdateSwapInterval( void )
{
	if ( gl_swapinterval->modified )
	{
		gl_swapinterval->modified = false;

		if ( !gl_state.stereo_enabled )
		{
#ifdef _WIN32
			if ( qwglSwapIntervalEXT )
				qwglSwapIntervalEXT( gl_swapinterval->value );
#endif
		}
	}
}