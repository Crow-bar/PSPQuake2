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
// disable data conversion warnings

#include <stdio.h>
#include <math.h>

#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>

#include "../client/ref.h"

#include "gu_types.h"
#include "gu_helper.h"
#include "gu_vram.h"
#include "gu_extension.h"

#define	REF_VERSION	"GU 0.01"

// up / down
#define	PITCH	0

// left / right
#define	YAW		1

// fall over
#define	ROLL	2


#ifndef __VIDDEF_T
#define __VIDDEF_T
typedef struct
{
	unsigned		width, height;			// coordinates from main game
} viddef_t;
#endif

extern	viddef_t	vid;


/*

  skins will be outline flood filled and mip mapped
  pics and sprites with alpha will be outline flood filled
  pic won't be mip mapped

  model skin
  sprite frame
  wall texture
  pic

*/

#define IMG_TYPE_SKIN			0x00000000	// default
#define IMG_TYPE_SPRITE			0x00000001
#define IMG_TYPE_WALL			0x00000002
#define IMG_TYPE_PIC			0x00000004
#define IMG_TYPE_SKY			0x00000008
#define IMG_TYPE_LM				0x00000010
#define IMG_TYPE_MASK			0x0000001f

#define IMG_FORMAT_IND_32       0x00000000	// default
#define IMG_FORMAT_IND_24       0x00000020
#define IMG_FORMAT_RGBA_8888	0x00000040
#define IMG_FORMAT_RGBA_4444    0x00000080
#define IMG_FORMAT_RGBA_5551    0x00000100
#define IMG_FORMAT_RGB_5650     0x00000200
#define IMG_FORMAT_MASK			0x000003e0

#define IMG_FILTER_LINEAR		0x00000000	// default
#define IMG_FILTER_NEAREST		0x00000400
#define IMG_FILTER_MASK			0x00000400

#define IMG_FLAG_INVRAM			0x00000800
#define IMG_FLAG_SWIZZLED		0x00001000
#define IMG_FLAG_SCRAP			0x00002000
#define IMG_FLAG_HAS_ALPHA		0x00004000
#define IMG_FLAG_DYNAMIC		0x00008000
#define IMG_FLAG_EXTERNAL		0x00010000
#define IMG_FLAG_PALETTED		0x00020000
#define IMG_FLAG_MASK			0x0003f800

#define IMG_IS_IND(image)		(!((image)->flags & IMG_FORMAT_MASK) || ((image)->flags & IMG_FORMAT_IND_24))

#define IMG_IS_SKIN(image)		(!((image)->flags & IMG_TYPE_MASK))
#define IMG_IS_SPRITE(image)	((image)->flags & IMG_TYPE_SPRITE)
#define IMG_IS_WALL(image)		((image)->flags & IMG_TYPE_WALL)
#define IMG_IS_PIC(image)		((image)->flags & IMG_TYPE_PIC)
#define IMG_IS_SKY(image)		((image)->flags & IMG_TYPE_SKY)
#define IMG_IS_LM(image)		((image)->flags & IMG_TYPE_LM)

typedef struct image_s
{
	char		name[MAX_QPATH];		// game path, including extension
	short		width, height;			// source image
	short		uplwidth, uplheight;	// after power of two and picmip

	byte		*data;

	int			format;
	uint		flags;

	struct msurface_s	*texturechain;	// for sort-by-texture world drawing
	//int			texnum;					// gl texture binding
	short		sl, tl, sh, th;			// unless part of the scrap

	size_t		size;
	byte		bpp;

#ifdef USE_HASH_FOR_TEXTURES
	uint		hashkey;
	struct image_s		*nexthash;
#endif
} image_t;


#define	MAX_GLTEXTURES		1024
#define	MAX_GLTEXTURES_HASH	(MAX_GLTEXTURES >> 2)
#define	MAX_LIGHTMAPS		128
//===================================================================

typedef enum
{
	rserr_ok,

	rserr_invalid_fullscreen,
	rserr_invalid_mode,

	rserr_unknown
} rserr_t;

#include "gu_model.h"

void GL_BeginRendering (int *x, int *y, int *width, int *height);
void GL_EndRendering (void);

void GL_SetDefaultState( void );

extern int	gldepthmin, gldepthmax;

#define	MAX_LBM_HEIGHT		480
#define BACKFACE_EPSILON	0.01

//====================================================

extern	image_t		gltextures[MAX_GLTEXTURES];
extern	int			numgltextures;


extern	image_t		*r_notexture;
extern	image_t		*r_particletexture;
extern	entity_t	*currententity;
extern	model_t		*currentmodel;
extern	int			r_visframecount;
extern	int			r_framecount;
extern	cplane_t	frustum[4];
extern	int			c_brush_polys, c_alias_polys;


//
// view origin
//
extern	vec3_t	vup;
extern	vec3_t	vpn;
extern	vec3_t	vright;
extern	vec3_t	r_origin;

//
// screen size info
//
extern	refdef_t	r_newrefdef;
extern	int		r_viewcluster, r_viewcluster2, r_oldviewcluster, r_oldviewcluster2;

extern	cvar_t	*r_norefresh;
extern	cvar_t	*r_lefthand;
extern	cvar_t	*r_drawentities;
extern	cvar_t	*r_drawworld;
extern	cvar_t	*r_speeds;
extern	cvar_t	*r_fullbright;
extern	cvar_t	*r_novis;
extern	cvar_t	*r_nocull;
extern	cvar_t	*r_lerpmodels;

extern	cvar_t	*r_lightlevel;	// FIXME: This is a HACK to get the client's light level

extern	cvar_t	*gl_particle_point;
extern	cvar_t	*gl_particle_scale;

extern	cvar_t	*gl_log;
extern	cvar_t	*gl_lightmap;
extern	cvar_t	*gl_shadows;
extern	cvar_t	*gl_mode;
extern	cvar_t	*gl_dynamic;
extern  cvar_t  *gl_monolightmap;
extern	cvar_t	*gl_modulate;
extern	cvar_t	*gl_nobind;
extern	cvar_t	*gl_round_down;
extern	cvar_t	*gl_picmip;
extern	cvar_t	*gl_skymip;
extern	cvar_t	*gl_skytga;
extern	cvar_t	*gl_showtris;
extern	cvar_t	*gl_clear;
extern	cvar_t	*gl_cull;
extern	cvar_t	*gl_polyblend;
extern	cvar_t	*gl_flashblend;
extern  cvar_t  *gl_saturatelighting;
extern  cvar_t  *gl_lockpvs;

extern	cvar_t	*vid_fullscreen;
extern	cvar_t	*vid_gamma;

extern	cvar_t	*intensity;

extern	int		c_visible_lightmaps;
extern	int		c_visible_textures;

extern	ScePspFMatrix4	r_world_matrix;

void R_TranslatePlayerSkin (int playernum);
void GL_Bind (image_t *image);
void GL_TexEnv (int mode);

void R_LightPoint (vec3_t p, vec3_t color);
void R_PushDlights (void);

//====================================================================

extern	model_t	*r_worldmodel;

extern	unsigned	d_8to24table[256];

extern	int		registration_sequence;


void V_AddBlend (float r, float g, float b, float a, float *v_blend);

qboolean	R_Init (void *hinstance, void *hWnd);
void		R_Shutdown (void);

void R_RenderView (refdef_t *fd);
void GL_ScreenShot_f (void);
void R_DrawAliasModel (entity_t *e);
void R_DrawBrushModel (entity_t *e);
void R_DrawSpriteModel (entity_t *e);
void R_DrawBeam( entity_t *e );
void R_DrawWorld (void);
void R_RenderDlights (void);
void R_DrawAlphaSurfaces (void);
void R_RenderBrushPoly (msurface_t *fa);
void R_InitParticleTexture (void);
void Draw_InitLocal (void);
void GL_SubdivideSurface (msurface_t *fa);
qboolean R_CullBox (vec3_t mins, vec3_t maxs);
void R_RotateForEntity (entity_t *e);
void R_MarkLeaves (void);

glpoly_t *WaterWarpPolyVerts (glpoly_t *p);
void EmitWaterPolys (msurface_t *fa);
void R_AddSkySurface (msurface_t *fa);
void R_ClearSkyBox (void);
void R_DrawSkyBox (void);
void R_PurgeSkybox (void);
void R_SetSky (char *name, float rotate, vec3_t axis);
void R_MarkLights (dlight_t *light, int bit, mnode_t *node);

#if 0
short LittleShort (short l);
short BigShort (short l);
int	LittleLong (int l);
float LittleFloat (float f);

char	*va(char *format, ...);
// does a varargs printf into a temp buffer
#endif

void COM_StripExtension (char *in, char *out);

void	Draw_GetPicSize (int *w, int *h, char *name);
void	Draw_Pic (int x, int y, char *name);
void	Draw_StretchPic (int x, int y, int w, int h, char *name);
void	Draw_Char (int x, int y, int c);
void	Draw_TileClear (int x, int y, int w, int h, char *name);
void	Draw_Fill (int x, int y, int w, int h, int c);
void	Draw_Line (int x0, int y0, int x1, int y1, int c);
void	Draw_FadeScreen (void);
void	Draw_StretchRaw (int x, int y, int w, int h, int cols, int rows, byte *data);

void	R_BeginFrame( float camera_separation );
void	R_SwapBuffers( int );
void	R_SetPalette ( const unsigned char *palette);

int		Draw_GetPalette (void);

struct image_s *R_RegisterSkin (char *name);

void LoadPCX (char *filename, byte **pic, byte **palette, int *width, int *height);
image_t *GL_LoadPic (char *name, byte *pic, int width, int height, int flags);
image_t	*GL_FindImage (char *name, int flags);
void	GL_TextureMode( char *string );
void	GL_ImageList_f (void);

void	GL_SetTexturePalette( unsigned palette[256] );

void	GL_InitImages (void);
void	GL_ShutdownImages (void);

void    GL_FreeImage (image_t *image);
void    GL_FreeImages (void);

void GL_TextureAlphaMode( char *string );
void GL_TextureSolidMode( char *string );

void GL_ResampleTexture32 (const byte *src, int inwidth, int inheight, const byte *dst, int outwidth, int outheight);
void GL_ResampleTexture8 (const byte *src, int inwidth, int inheight, byte *dst, int outwidth, int outheight);
void GL_PixelConverter (byte *dst, const byte *src, int width, int height, int informat, int outformat);
qboolean GL_UpdateTexture (image_t *image, int xoff, int yoff, int width, int height, const void *buffer);
void GL_BuildGammaTable (float gamma, float intensity);

//
// gu_clipping.c
//
#define CLIPPING_DEBUGGING 0
void GU_ClipSetWorldFrustum (void);
void GU_ClipRestoreWorldFrustum (void);
void GU_ClipSetModelFrustum (void);
int GU_ClipIsRequired (gu_vert_ftv_t* uv, int uvc);
void GU_Clip (gu_vert_ftv_t *uv, int uvc, gu_vert_ftv_t **cv, int* cvc);

/*
** GL extension emulation functions
*/
void GL_DrawParticles( int n, const particle_t particles[] );

typedef struct
{
	struct
	{
		int		width;
		int		height;
	} screen;

	struct
	{
		int		width;
		int		format;
		int		bpp;
		void	*draw_ptr;
		void	*disp_ptr;
		void	*depth_ptr;
	} buffer;

	struct
	{
		void	*ptr;
		size_t	size;
	} list;
} gurender_t;

typedef struct
{
	float inverse_intensity;
	qboolean fullscreen;

	int	prev_mode;

	unsigned char *d_16to8table;

	image_t	*lightmap_textures[MAX_LIGHTMAPS];

	image_t	*currenttexture;
	int		currenttexenv;

	float camera_separation;
	qboolean stereo_enabled;

	byte	gammatable[256];
	byte	intensitytable[256];
} glstate_t;

extern gurender_t	gu_render;
extern glstate_t	gl_state;

/*
====================================================================

IMPORTED FUNCTIONS

====================================================================
*/

extern	refimport_t	ri;


/*
====================================================================

IMPLEMENTATION SPECIFIC FUNCTIONS

====================================================================
*/

void		GU_BeginFrame (float camera_separation);
void		GU_EndFrame (void);
int 		GU_Init (void *hinstance, void *hWnd);
void		GU_Shutdown (void);
int     	GU_SetMode (int *pwidth, int *pheight, int mode, qboolean fullscreen);
void		GU_AppActivate (qboolean active);
void		GU_EnableLogging (qboolean enable);
void		GU_LogNewFrame (void);

