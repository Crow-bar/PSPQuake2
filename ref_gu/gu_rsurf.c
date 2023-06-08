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
// GU_RSURF.C: surface-related refresh code
#include <assert.h>

#include "gu_local.h"

extern	char	loadname[32];

static vec3_t	modelorg;		// relative to viewpoint

msurface_t	*r_alpha_surfaces;

#define LIGHTMAP_BYTES 4

#define	BLOCK_WIDTH		128
#define	BLOCK_HEIGHT	128

int		c_visible_lightmaps;
int		c_visible_textures;

typedef struct
{
	int internal_format;
	int	current_lightmap_texture;

	msurface_t	*lightmap_surfaces[MAX_LIGHTMAPS];

	int			allocated[BLOCK_WIDTH];
	int			allocated_max;

	// the lightmap texture data needs to be kept in
	// main memory so texsubimage can update properly
	byte		lightmap_buffer[BLOCK_WIDTH*BLOCK_HEIGHT*LIGHTMAP_BYTES] __attribute__((aligned(16)));
} gllightmapstate_t;

static gllightmapstate_t gl_lms;


static void		LM_InitBlock( void );
static void		LM_UploadBlock( qboolean dynamic );
static qboolean	LM_AllocBlock (int w, int h, int *x, int *y);

extern void R_SetCacheState( msurface_t *surf );
extern void R_BuildLightMap (msurface_t *surf, byte *dest, int stride);

/*
=============================================================

	BRUSH MODELS

=============================================================
*/

/*
===============
R_TextureAnimation

Returns the proper texture for a given time and base texture
===============
*/
image_t *R_TextureAnimation (mtexinfo_t *tex)
{
	int		c;

	if (!tex->next)
		return tex->image;

	c = currententity->frame % tex->numframes;
	while (c)
	{
		tex = tex->next;
		c--;
	}

	return tex->image;
}

/*
================
DrawGLPoly
================
*/
void DrawGLPoly (glpoly_t *p)
{
	if (GU_ClipIsRequired (&p->verts[0], p->numverts))
	{
		// clip the polygon.
		gu_vert_ftv_t	*cv;
		int				cvc;

		GU_Clip (&p->verts[0], p->numverts, &cv, &cvc);
		if (cvc)
		{
#if CLIPPING_DEBUGGING
			sceGuDisable (GU_TEXTURE_2D);
			sceGuColor (0xff0000ff);
#endif
			sceGuDrawArray (GU_TRIANGLE_FAN, GU_TEXTURE_32BITF | GU_VERTEX_32BITF, cvc, 0, cv);
#if CLIPPING_DEBUGGING
			sceGuEnable (GU_TEXTURE_2D);
			sceGuColor (GU_HCOLOR_DEFAULT);
#endif
		}
	}
	else sceGuDrawArray (GU_TRIANGLE_FAN, GU_TEXTURE_32BITF | GU_VERTEX_32BITF, p->numverts, 0, &p->verts[0]);
}

//============
//PGM
/*
================
DrawGLFlowingPoly -- version of DrawGLPoly that handles scrolling texture
================
*/
void DrawGLFlowingPoly (msurface_t *fa)
{
	glpoly_t		*p;
	float			scroll;


	p = fa->polys;

	scroll = -64 * ( (r_newrefdef.time / 40.0) - (int)(r_newrefdef.time / 40.0) );
	if(scroll == 0.0)
		scroll = -64.0;

	sceGuTexOffset (scroll, 0.0f);
	if (GU_ClipIsRequired (&p->verts[0], p->numverts))
	{
		// clip the polygon.
		gu_vert_ftv_t	*cv;
		int				cvc;

		GU_Clip (&p->verts[0], p->numverts, &cv, &cvc);
		if (cvc)
		{
#if CLIPPING_DEBUGGING
			sceGuDisable (GU_TEXTURE_2D);
			sceGuColor (0xff0000ff);
#endif
			sceGuDrawArray (GU_TRIANGLE_FAN, GU_TEXTURE_32BITF | GU_VERTEX_32BITF, cvc, 0, cv);
#if CLIPPING_DEBUGGING
			sceGuEnable (GU_TEXTURE_2D);
			sceGuColor (GU_HCOLOR_DEFAULT);
#endif
		}
	}
	else sceGuDrawArray (GU_TRIANGLE_FAN, GU_TEXTURE_32BITF | GU_VERTEX_32BITF, p->numverts, 0, &p->verts[0]);
	sceGuTexOffset (0.0f, 0.0f);
}
//PGM
//============

/*
** R_DrawTriangleOutlines
*/
void R_DrawTriangleOutlines (void)
{
	int			i, j;
	glpoly_t	*p;

	sceGuDisable (GU_TEXTURE_2D);
	sceGuDisable (GU_DEPTH_TEST);
	sceGuColor (GU_HCOLOR_DEFAULT);

	for (i=0 ; i<MAX_LIGHTMAPS ; i++)
	{
		msurface_t *surf;

		for ( surf = gl_lms.lightmap_surfaces[i]; surf != 0; surf = surf->lightmapchain )
		{
			p = surf->polys;
			for ( ; p ; p=p->chain)
			{
				for (j=2 ; j<p->numverts ; j++ )
				{
					gu_vert_fv_t* const out = (gu_vert_fv_t*)sceGuGetMemory (sizeof(gu_vert_fv_t) * 4);
					VectorCopy (p->verts[0].xyz,     out[0].xyz);
					VectorCopy (p->verts[j - 1].xyz, out[1].xyz);
					VectorCopy (p->verts[j].xyz,     out[2].xyz);
					VectorCopy (p->verts[0].xyz,     out[3].xyz);
					sceGuDrawArray (GU_LINE_STRIP, GU_VERTEX_32BITF, 4, 0, out);
				}
			}
		}
	}

	sceGuEnable (GU_DEPTH_TEST);
	sceGuEnable (GU_TEXTURE_2D);
}

/*
** DrawGLPolyChain
*/
void DrawGLPolyChain( glpoly_t *p, float soffset, float toffset )
{
	qboolean		dynamic;

	dynamic = (soffset == 0.0f && toffset == 0.0f) ? false : true;

	if (dynamic) sceGuTexOffset (-soffset, -toffset);

	for ( ; p != 0; p = p->chain )
	{
		if (GU_ClipIsRequired (&p->verts[p->numverts], p->numverts))
		{
			// clip the polygon.
			gu_vert_ftv_t	*cv;
			int				cvc;

			GU_Clip (&p->verts[p->numverts], p->numverts, &cv, &cvc);
			if (cvc)
			{
#if CLIPPING_DEBUGGING
				sceGuDisable (GU_TEXTURE_2D);
				sceGuColor (0xff0000ff);
#endif
				sceGuDrawArray (GU_TRIANGLE_FAN, GU_TEXTURE_32BITF | GU_VERTEX_32BITF, cvc, 0, cv);
#if CLIPPING_DEBUGGING
				sceGuEnable (GU_TEXTURE_2D);
				sceGuColor (GU_HCOLOR_DEFAULT);
#endif
			}
		}
		else sceGuDrawArray (GU_TRIANGLE_FAN, GU_TEXTURE_32BITF | GU_VERTEX_32BITF, p->numverts, 0, &p->verts[p->numverts]);
	}

	if (dynamic) sceGuTexOffset (0.0f, 0.0f);
}

/*
** R_BlendLightMaps
**
** This routine takes all the given light mapped surfaces in the world and
** blends them into the framebuffer.
*/
void R_BlendLightmaps (void)
{
	int			i;
	msurface_t	*surf, *newdrawsurf = 0;

	// don't bother if we're set to fullbright
	if (r_fullbright->value)
		return;
	if (!r_worldmodel->lightdata)
		return;

	// don't bother writing Z
	sceGuDepthMask (GU_TRUE);

	/*
	** set the appropriate blending mode unless we're only looking at the
	** lightmaps.
	*/
	if (!gl_lightmap->value)
	{
		sceGuEnable (GU_BLEND);

		if ( gl_saturatelighting->value )
		{
			sceGuBlendFunc (GU_ADD, GU_FIX, GU_FIX, GU_HBLEND_ONE, GU_HBLEND_ONE);
		}
		else
		{
			if ( gl_monolightmap->string[0] != '0' )
			{
				switch ( toupper( gl_monolightmap->string[0] ) )
				{
				case 'I':
				case 'L':
					sceGuBlendFunc (GU_ADD, GU_FIX, GU_SRC_COLOR, GU_HBLEND_ZERO, 0);
					break;
				case 'A':
				default:
					sceGuBlendFunc (GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
					break;
				}
			}
			else
			{
				sceGuBlendFunc (GU_ADD, GU_FIX, GU_SRC_COLOR, GU_HBLEND_ZERO, 0);
			}
		}
	}

	if ( currentmodel == r_worldmodel )
		c_visible_lightmaps = 0;

	/*
	** render static lightmaps first
	*/
	for ( i = 1; i < MAX_LIGHTMAPS; i++ )
	{
		if ( gl_lms.lightmap_surfaces[i] )
		{
			if (currentmodel == r_worldmodel)
				c_visible_lightmaps++;
			GL_Bind( gl_state.lightmap_textures[i]);

			for ( surf = gl_lms.lightmap_surfaces[i]; surf != 0; surf = surf->lightmapchain )
			{
				if ( surf->polys )
				{
					DrawGLPolyChain( surf->polys, 0, 0 );
				}
			}
		}
	}

	/*
	** render dynamic lightmaps
	*/
	if ( gl_dynamic->value )
	{
		LM_InitBlock();

		GL_Bind( gl_state.lightmap_textures[0] );

		if (currentmodel == r_worldmodel)
			c_visible_lightmaps++;

		newdrawsurf = gl_lms.lightmap_surfaces[0];

		for ( surf = gl_lms.lightmap_surfaces[0]; surf != 0; surf = surf->lightmapchain )
		{
			int		smax, tmax;
			byte	*base;

			smax = (surf->extents[0]>>4)+1;
			tmax = (surf->extents[1]>>4)+1;

			if ( LM_AllocBlock( smax, tmax, &surf->dlight_s, &surf->dlight_t ) )
			{
				base = gl_lms.lightmap_buffer;
				base += ( surf->dlight_t * BLOCK_WIDTH + surf->dlight_s ) * LIGHTMAP_BYTES;

				R_BuildLightMap (surf, base, BLOCK_WIDTH*LIGHTMAP_BYTES);
			}
			else
			{
				msurface_t *drawsurf;

				// upload what we have so far
				LM_UploadBlock( true );

				// draw all surfaces that use this lightmap
				for ( drawsurf = newdrawsurf; drawsurf != surf; drawsurf = drawsurf->lightmapchain )
				{
					if ( drawsurf->polys )
						DrawGLPolyChain( drawsurf->polys,
							              ( drawsurf->light_s - drawsurf->dlight_s ) * ( 1.0 / 128.0 ),
										( drawsurf->light_t - drawsurf->dlight_t ) * ( 1.0 / 128.0 ) );
				}

				newdrawsurf = drawsurf;

				// clear the block
				LM_InitBlock();

				// try uploading the block now
				if ( !LM_AllocBlock( smax, tmax, &surf->dlight_s, &surf->dlight_t ) )
				{
					ri.Sys_Error( ERR_FATAL, "Consecutive calls to LM_AllocBlock(%d,%d) failed (dynamic)\n", smax, tmax );
				}

				base = gl_lms.lightmap_buffer;
				base += ( surf->dlight_t * BLOCK_WIDTH + surf->dlight_s ) * LIGHTMAP_BYTES;

				R_BuildLightMap (surf, base, BLOCK_WIDTH*LIGHTMAP_BYTES);
			}
		}

		/*
		** draw remainder of dynamic lightmaps that haven't been uploaded yet
		*/
		if ( newdrawsurf )
			LM_UploadBlock( true );

		for ( surf = newdrawsurf; surf != 0; surf = surf->lightmapchain )
		{
			if ( surf->polys )
				DrawGLPolyChain( surf->polys, ( surf->light_s - surf->dlight_s ) * ( 1.0 / 128.0 ), ( surf->light_t - surf->dlight_t ) * ( 1.0 / 128.0 ) );
		}
	}

	/*
	** restore state
	*/
	sceGuDisable (GU_BLEND);
	sceGuBlendFunc (GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
	sceGuDepthMask (GU_FALSE);

}

/*
================
R_RenderBrushPoly
================
*/
void R_RenderBrushPoly (msurface_t *fa)
{
	int			maps;
	image_t		*image;
	qboolean is_dynamic = false;

	c_brush_polys++;

	image = R_TextureAnimation (fa->texinfo);
	GL_Bind( image );

	if (fa->flags & SURF_DRAWTURB)
	{
		// warp texture, no lightmaps
		GL_TexEnv (GU_TFX_MODULATE);
		sceGuColor (GU_HCOLOR_4F(
					gl_state.inverse_intensity,
					gl_state.inverse_intensity,
					gl_state.inverse_intensity,
					1.0f));
		EmitWaterPolys (fa);
		GL_TexEnv (GU_TFX_REPLACE);

		return;
	}

	GL_TexEnv (GU_TFX_REPLACE);

//======
//PGM
	if(fa->texinfo->flags & SURF_FLOWING)
		DrawGLFlowingPoly (fa);
	else
		DrawGLPoly (fa->polys);
//PGM
//======
	/*
	** check for lightmap modification
	*/
	for ( maps = 0; maps < MAXLIGHTMAPS && fa->styles[maps] != 255; maps++ )
	{
		if ( r_newrefdef.lightstyles[fa->styles[maps]].white != fa->cached_light[maps] )
			goto dynamic;
	}

	// dynamic this frame or dynamic previously
	if ( ( fa->dlightframe == r_framecount ) )
	{
dynamic:
		if ( gl_dynamic->value )
		{
			if (!( fa->texinfo->flags & (SURF_SKY|SURF_TRANS33|SURF_TRANS66|SURF_WARP ) ) )
			{
				is_dynamic = true;
			}
		}
	}

	if ( is_dynamic )
	{
		// PSP: always using dynamic lightmap surfaces
		fa->lightmapchain = gl_lms.lightmap_surfaces[0];
		gl_lms.lightmap_surfaces[0] = fa;
	}
	else
	{
		fa->lightmapchain = gl_lms.lightmap_surfaces[fa->lightmaptexturenum];
		gl_lms.lightmap_surfaces[fa->lightmaptexturenum] = fa;
	}
}


/*
================
R_DrawAlphaSurfaces

Draw water surfaces and windows.
The BSP tree is waled front to back, so unwinding the chain
of alpha_surfaces will draw back to front, giving proper ordering.
================
*/
void R_DrawAlphaSurfaces (void)
{
	msurface_t	*s;
	float		intens;

	//
	// go back to the world matrix
	//
	sceGumLoadIdentity (); // qglLoadMatrixf (r_world_matrix);

	sceGuEnable (GU_BLEND);
	GL_TexEnv (GU_TFX_MODULATE);

	// the textures are prescaled up for a better lighting range,
	// so scale it back down
	intens = gl_state.inverse_intensity;

	for (s=r_alpha_surfaces ; s ; s=s->texturechain)
	{
		GL_Bind(s->texinfo->image);
		c_brush_polys++;
		if (s->texinfo->flags & SURF_TRANS33)
			sceGuColor (GU_HCOLOR_4F(intens, intens, intens, 0.33));
		else if (s->texinfo->flags & SURF_TRANS66)
			sceGuColor (GU_HCOLOR_4F(intens, intens, intens, 0.66));
		else
			sceGuColor (GU_HCOLOR_4F(intens, intens, intens, 1.0));
		if (s->flags & SURF_DRAWTURB)
			EmitWaterPolys (s);
		else if(s->texinfo->flags & SURF_FLOWING)			// PGM	9/16/98
			DrawGLFlowingPoly (s);							// PGM
		else
			DrawGLPoly (s->polys);
	}

	GL_TexEnv (GU_TFX_REPLACE);
	sceGuColor (GU_HCOLOR_DEFAULT);
	sceGuDisable (GU_BLEND);

	r_alpha_surfaces = NULL;
}

/*
================
DrawTextureChains
================
*/
void DrawTextureChains (void)
{
	int		i;
	msurface_t	*s;
	image_t		*image;

	c_visible_textures = 0;

//	GL_TexEnv (GU_TFX_REPLACE);

	for ( i = 0, image=gltextures ; i<numgltextures ; i++,image++)
	{
/*
		if (!image->registration_sequence)
			continue;
*/
		s = image->texturechain;
		if (!s)
			continue;
		c_visible_textures++;

		for ( ; s ; s=s->texturechain)
			R_RenderBrushPoly (s);

		image->texturechain = NULL;
	}

	GL_TexEnv (GU_TFX_REPLACE);
}

/*
=================
R_DrawInlineBModel
=================
*/
void R_DrawInlineBModel (void)
{
	int			i, k;
	cplane_t	*pplane;
	float		dot;
	msurface_t	*psurf;
	dlight_t	*lt;

	// calculate dynamic lighting for bmodel
	if ( gl_flashblend->value <= 1 )
	{
		lt = r_newrefdef.dlights;
		for (k=0 ; k<r_newrefdef.num_dlights ; k++, lt++)
		{
			R_MarkLights (lt, 1<<k, currentmodel->nodes + currentmodel->firstnode);
		}
	}

	psurf = &currentmodel->surfaces[currentmodel->firstmodelsurface];

	if ( currententity->flags & RF_TRANSLUCENT )
	{
		sceGuEnable (GU_BLEND);
		sceGuColor (GU_HCOLOR_4F(1.0, 1.0, 1.0, 0.25));
		GL_TexEnv (GU_TFX_MODULATE);
	}

	//
	// draw texture
	//
	for (i=0 ; i<currentmodel->nummodelsurfaces ; i++, psurf++)
	{
	// find which side of the node we are on
		pplane = psurf->plane;

		dot = DotProduct (modelorg, pplane->normal) - pplane->dist;

	// draw the polygon
		if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
			(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
		{
			if (psurf->texinfo->flags & (SURF_TRANS33|SURF_TRANS66) )
			{	// add to the translucent chain
				psurf->texturechain = r_alpha_surfaces;
				r_alpha_surfaces = psurf;
			}
			else
			{
				R_RenderBrushPoly (psurf);
			}
		}
	}

	if ( !(currententity->flags & RF_TRANSLUCENT) )
	{
		R_BlendLightmaps ();
	}
	else
	{
		sceGuDisable (GU_BLEND);
		sceGuColor (GU_HCOLOR_DEFAULT);
		GL_TexEnv (GU_TFX_REPLACE);
	}
}

/*
=================
R_DrawBrushModel
=================
*/
void R_DrawBrushModel (entity_t *e)
{
	vec3_t		mins, maxs;
	int			i;
	qboolean	rotated;

	if (currentmodel->nummodelsurfaces == 0)
		return;

	currententity = e;
	gl_state.currenttexture = NULL;

	if (e->angles[0] || e->angles[1] || e->angles[2])
	{
		rotated = true;
		for (i=0 ; i<3 ; i++)
		{
			mins[i] = e->origin[i] - currentmodel->radius;
			maxs[i] = e->origin[i] + currentmodel->radius;
		}
	}
	else
	{
		rotated = false;
		VectorAdd (e->origin, currentmodel->mins, mins);
		VectorAdd (e->origin, currentmodel->maxs, maxs);
	}

	if (R_CullBox (mins, maxs))
		return;

	sceGuColor (GU_HCOLOR_DEFAULT);
	memset (gl_lms.lightmap_surfaces, 0, sizeof(gl_lms.lightmap_surfaces));

	VectorSubtract (r_newrefdef.vieworg, e->origin, modelorg);
	if (rotated)
	{
		vec3_t	temp;
		vec3_t	forward, right, up;

		VectorCopy (modelorg, temp);
		AngleVectors (e->angles, forward, right, up);
		modelorg[0] = DotProduct (temp, forward);
		modelorg[1] = -DotProduct (temp, right);
		modelorg[2] = DotProduct (temp, up);
	}

	sceGumPushMatrix ();

	e->angles[0] = -e->angles[0];	// stupid quake bug
	e->angles[2] = -e->angles[2];	// stupid quake bug

	R_RotateForEntity (e);

	e->angles[0] = -e->angles[0];	// stupid quake bug
	e->angles[2] = -e->angles[2];	// stupid quake bug

	//GL_TexEnv( GU_TFX_REPLACE );

	GU_ClipSetModelFrustum ();
	R_DrawInlineBModel ();
	GU_ClipRestoreWorldFrustum ();

	sceGumPopMatrix ();
	sceGumUpdateMatrix ();
}

/*
=============================================================

	WORLD MODEL

=============================================================
*/

/*
================
R_RecursiveWorldNode
================
*/
void R_RecursiveWorldNode (mnode_t *node)
{
	int			c, side, sidebit;
	cplane_t	*plane;
	msurface_t	*surf, **mark;
	mleaf_t		*pleaf;
	float		dot;
	image_t		*image;

	if (node->contents == CONTENTS_SOLID)
		return;		// solid

	if (node->visframe != r_visframecount)
		return;
	if (R_CullBox (node->minmaxs, node->minmaxs+3))
		return;

// if a leaf node, draw stuff
	if (node->contents != -1)
	{
		pleaf = (mleaf_t *)node;

		// check for door connected areas
		if (r_newrefdef.areabits)
		{
			if (! (r_newrefdef.areabits[pleaf->area>>3] & (1<<(pleaf->area&7)) ) )
				return;		// not visible
		}

		mark = pleaf->firstmarksurface;
		c = pleaf->nummarksurfaces;

		if (c)
		{
			do
			{
				(*mark)->visframe = r_framecount;
				mark++;
			} while (--c);
		}

		return;
	}

// node is just a decision point, so go down the apropriate sides

// find which side of the node we are on
	plane = node->plane;

	switch (plane->type)
	{
	case PLANE_X:
		dot = modelorg[0] - plane->dist;
		break;
	case PLANE_Y:
		dot = modelorg[1] - plane->dist;
		break;
	case PLANE_Z:
		dot = modelorg[2] - plane->dist;
		break;
	default:
		dot = DotProduct (modelorg, plane->normal) - plane->dist;
		break;
	}

	if (dot >= 0)
	{
		side = 0;
		sidebit = 0;
	}
	else
	{
		side = 1;
		sidebit = SURF_PLANEBACK;
	}

// recurse down the children, front side first
	R_RecursiveWorldNode (node->children[side]);

	// draw stuff
	for ( c = node->numsurfaces, surf = r_worldmodel->surfaces + node->firstsurface; c ; c--, surf++)
	{
		if (surf->visframe != r_framecount)
			continue;

		if ( (surf->flags & SURF_PLANEBACK) != sidebit )
			continue;		// wrong side

		if (surf->texinfo->flags & SURF_SKY)
		{	// just adds to visible sky bounds
			R_AddSkySurface (surf);
		}
		else if (surf->texinfo->flags & (SURF_TRANS33|SURF_TRANS66))
		{	// add to the translucent chain
			surf->texturechain = r_alpha_surfaces;
			r_alpha_surfaces = surf;
		}
		else
		{
			// the polygon is visible, so add it to the texture
			// sorted chain
			// FIXME: this is a hack for animation
			image = R_TextureAnimation (surf->texinfo);
			surf->texturechain = image->texturechain;
			image->texturechain = surf;
		}
	}

	// recurse down the back side
	R_RecursiveWorldNode (node->children[!side]);
}


/*
=============
R_DrawWorld
=============
*/
void R_DrawWorld (void)
{
	entity_t	ent;

	if (!r_drawworld->value)
		return;

	if ( r_newrefdef.rdflags & RDF_NOWORLDMODEL )
		return;

	currentmodel = r_worldmodel;

	VectorCopy (r_newrefdef.vieworg, modelorg);

	// auto cycle the world frame for texture animation
	memset (&ent, 0, sizeof(ent));
	ent.frame = (int)(r_newrefdef.time*2);
	currententity = &ent;

	gl_state.currenttexture = NULL;

	sceGuColor (GU_HCOLOR_DEFAULT);

	memset (gl_lms.lightmap_surfaces, 0, sizeof(gl_lms.lightmap_surfaces));

	R_ClearSkyBox ();
	R_RecursiveWorldNode (r_worldmodel->nodes);
	DrawTextureChains ();
	R_BlendLightmaps ();

	R_DrawSkyBox ();

	if (gl_showtris->value)
		R_DrawTriangleOutlines ();
}


/*
===============
R_MarkLeaves

Mark the leaves and nodes that are in the PVS for the current
cluster
===============
*/
void R_MarkLeaves (void)
{
	byte	*vis;
	byte	fatvis[MAX_MAP_LEAFS/8];
	mnode_t	*node;
	int		i, c;
	mleaf_t	*leaf;
	int		cluster;

	if (r_oldviewcluster == r_viewcluster && r_oldviewcluster2 == r_viewcluster2 && !r_novis->value && r_viewcluster != -1)
		return;

	// development aid to let you run around and see exactly where
	// the pvs ends
	if (gl_lockpvs->value)
		return;

	r_visframecount++;
	r_oldviewcluster = r_viewcluster;
	r_oldviewcluster2 = r_viewcluster2;

	if (r_novis->value || r_viewcluster == -1 || !r_worldmodel->vis)
	{
		// mark everything
		for (i=0 ; i<r_worldmodel->numleafs ; i++)
			r_worldmodel->leafs[i].visframe = r_visframecount;
		for (i=0 ; i<r_worldmodel->numnodes ; i++)
			r_worldmodel->nodes[i].visframe = r_visframecount;
		return;
	}

	vis = Mod_ClusterPVS (r_viewcluster, r_worldmodel);
	// may have to combine two clusters because of solid water boundaries
	if (r_viewcluster2 != r_viewcluster)
	{
		memcpy (fatvis, vis, (r_worldmodel->numleafs+7)/8);
		vis = Mod_ClusterPVS (r_viewcluster2, r_worldmodel);
		c = (r_worldmodel->numleafs+31)/32;
		for (i=0 ; i<c ; i++)
			((int *)fatvis)[i] |= ((int *)vis)[i];
		vis = fatvis;
	}

	for (i=0,leaf=r_worldmodel->leafs ; i<r_worldmodel->numleafs ; i++, leaf++)
	{
		cluster = leaf->cluster;
		if (cluster == -1)
			continue;
		if (vis[cluster>>3] & (1<<(cluster&7)))
		{
			node = (mnode_t *)leaf;
			do
			{
				if (node->visframe == r_visframecount)
					break;
				node->visframe = r_visframecount;
				node = node->parent;
			} while (node);
		}
	}

#if 0
	for (i=0 ; i<r_worldmodel->vis->numclusters ; i++)
	{
		if (vis[i>>3] & (1<<(i&7)))
		{
			node = (mnode_t *)&r_worldmodel->leafs[i];	// FIXME: cluster
			do
			{
				if (node->visframe == r_visframecount)
					break;
				node->visframe = r_visframecount;
				node = node->parent;
			} while (node);
		}
	}
#endif
}



/*
=============================================================================

  LIGHTMAP ALLOCATION

=============================================================================
*/
static void LM_InitBlock( void )
{
	memset( gl_lms.allocated, 0, sizeof( gl_lms.allocated ) );
	gl_lms.allocated_max = 0;
}

static void LM_UploadBlock( qboolean dynamic )
{
	char	lmname[16];

	if (dynamic)
	{
		// prepare lightmap_buffer before rendering
		sceKernelDcacheWritebackRange (gl_lms.lightmap_buffer, gl_lms.allocated_max * BLOCK_WIDTH * LIGHTMAP_BYTES);
		return;
	}

	snprintf(lmname, sizeof(lmname), "*lightmap_%i", gl_lms.current_lightmap_texture);

	gl_state.lightmap_textures[gl_lms.current_lightmap_texture] =
		GL_LoadPic (lmname, gl_lms.lightmap_buffer, BLOCK_WIDTH, BLOCK_HEIGHT, IMG_TYPE_LM | IMG_FORMAT_RGBA_8888);
	//gl_lms.internal_format
	if ( ++gl_lms.current_lightmap_texture == MAX_LIGHTMAPS )
		ri.Sys_Error( ERR_DROP, "LM_UploadBlock() - MAX_LIGHTMAPS exceeded\n" );
}

// returns a texture number and the position inside it
static qboolean LM_AllocBlock (int w, int h, int *x, int *y)
{
	int		i, j;
	int		allocated, best, best2;

	best = BLOCK_HEIGHT;

	for (i=0 ; i<BLOCK_WIDTH-w ; i++)
	{
		best2 = 0;

		for (j=0 ; j<w ; j++)
		{
			if (gl_lms.allocated[i+j] >= best)
				break;
			if (gl_lms.allocated[i+j] > best2)
				best2 = gl_lms.allocated[i+j];
		}
		if (j == w)
		{	// this is a valid spot
			*x = i;
			*y = best = best2;
		}
	}

	allocated = best + h;

	if (allocated > BLOCK_HEIGHT)
		return false;

	if (gl_lms.allocated_max < allocated)
		gl_lms.allocated_max = allocated;

	for (i=0 ; i<w ; i++)
		gl_lms.allocated[*x + i] = allocated;

	return true;
}

/*
================
GL_BuildPolygonFromSurface
================
*/
void GL_BuildPolygonFromSurface(msurface_t *fa)
{
	int			i, lindex, lnumverts;
	medge_t		*pedges;
	float		*vec;
	float		s, t;
	glpoly_t	*poly;

	// reconstruct the polygon
	pedges = currentmodel->edges;
	lnumverts = fa->numedges;

	//
	// draw texture
	//
	poly = ri.Hunk_AllocName (sizeof(glpoly_t) + (lnumverts * 2 - 1) * sizeof(gu_vert_ftv_t), loadname);
	poly->next = fa->polys;
	poly->flags = fa->flags;
	fa->polys = poly;
	poly->numverts = lnumverts;

	for (i=0 ; i<lnumverts ; i++)
	{
		lindex = currentmodel->surfedges[fa->firstedge + i];

		if (lindex > 0)
			vec = currentmodel->vertexes[pedges[lindex].v[0]].position;
		else
			vec = currentmodel->vertexes[pedges[-lindex].v[1]].position;

		s = DotProduct (vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		t = DotProduct (vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];

		//VectorAdd (total, vec, total);
		VectorCopy (vec, poly->verts[i].xyz);
		poly->verts[i].u = s / fa->texinfo->image->width;
		poly->verts[i].v = t / fa->texinfo->image->height;

		//
		// lightmap texture coordinates
		//
		s -= fa->texturemins[0];
		s += fa->light_s*16;
		s += 8;

		t -= fa->texturemins[1];
		t += fa->light_t*16;
		t += 8;

		VectorCopy (vec, poly->verts[i + lnumverts].xyz);
		poly->verts[i + lnumverts].u = s / (BLOCK_WIDTH * 16);
		poly->verts[i + lnumverts].v = t / (BLOCK_HEIGHT * 16);
	}

	poly->numverts = lnumverts;

}

/*
========================
GL_CreateSurfaceLightmap
========================
*/
void GL_CreateSurfaceLightmap (msurface_t *surf)
{
	int		smax, tmax;
	byte	*base;

	if (surf->flags & (SURF_DRAWSKY|SURF_DRAWTURB))
		return;

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;

	if ( !LM_AllocBlock( smax, tmax, &surf->light_s, &surf->light_t ) )
	{
		LM_UploadBlock( false );
		LM_InitBlock();
		if ( !LM_AllocBlock( smax, tmax, &surf->light_s, &surf->light_t ) )
		{
			ri.Sys_Error( ERR_FATAL, "Consecutive calls to LM_AllocBlock(%d,%d) failed\n", smax, tmax );
		}
	}

	surf->lightmaptexturenum = gl_lms.current_lightmap_texture;

	base = gl_lms.lightmap_buffer;
	base += (surf->light_t * BLOCK_WIDTH + surf->light_s) * LIGHTMAP_BYTES;

	R_SetCacheState( surf );
	R_BuildLightMap (surf, base, BLOCK_WIDTH*LIGHTMAP_BYTES);
}



/*
==================
GL_BeginBuildingLightmaps

==================
*/
void GL_BeginBuildingLightmaps (model_t *m)
{
	static lightstyle_t	lightstyles[MAX_LIGHTSTYLES];
	int				i;

	memset( gl_lms.allocated, 0, sizeof(gl_lms.allocated) );

	r_framecount = 1;		// no dlightcache

	/*
	** setup the base lightstyles so the lightmaps won't have to be regenerated
	** the first time they're seen
	*/
	for (i=0 ; i<MAX_LIGHTSTYLES ; i++)
	{
		lightstyles[i].rgb[0] = 1;
		lightstyles[i].rgb[1] = 1;
		lightstyles[i].rgb[2] = 1;
		lightstyles[i].white = 3;
	}
	r_newrefdef.lightstyles = lightstyles;

	memset(gl_state.lightmap_textures, 0, sizeof(gl_state.lightmap_textures));
	gl_lms.current_lightmap_texture = 1;
#if 0
	/*
	** if mono lightmaps are enabled and we want to use alpha
	** blending (a,1-a) then we're likely running on a 3DLabs
	** Permedia2.  In a perfect world we'd use a GL_ALPHA lightmap
	** in order to conserve space and maximize bandwidth, however
	** this isn't a perfect world.
	**
	** So we have to use alpha lightmaps, but stored in GL_RGBA format,
	** which means we only get 1/16th the color resolution we should when
	** using alpha lightmaps.  If we find another board that supports
	** only alpha lightmaps but that can at least support the GL_ALPHA
	** format then we should change this code to use real alpha maps.
	*/
	if ( toupper( gl_monolightmap->string[0] ) == 'A' )
	{
		gl_lms.internal_format = gl_tex_alpha_format;
	}
	/*
	** try to do hacked colored lighting with a blended texture
	*/
	else if ( toupper( gl_monolightmap->string[0] ) == 'C' )
	{
		gl_lms.internal_format = gl_tex_alpha_format;
	}
	else if ( toupper( gl_monolightmap->string[0] ) == 'I' )
	{
		gl_lms.internal_format = GL_INTENSITY8;
	}
	else if ( toupper( gl_monolightmap->string[0] ) == 'L' )
	{
		gl_lms.internal_format = GL_LUMINANCE8;
	}
	else
	{
		gl_lms.internal_format = gl_tex_solid_format;
	}

	/*
	** initialize the dynamic lightmap texture
	*/
	GL_Bind( gl_state.lightmap_textures + 0 );
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	qglTexImage2D( GL_TEXTURE_2D,
				   0,
				   gl_lms.internal_format,
				   BLOCK_WIDTH, BLOCK_HEIGHT,
				   0,
				   GL_LIGHTMAP_FORMAT,
				   GL_UNSIGNED_BYTE,
				   dummy );
#else
	/*
	** initialize the dynamic lightmap texture
	*/
	gl_state.lightmap_textures[0] =
			GL_LoadPic ("*lightmap_0d", gl_lms.lightmap_buffer, BLOCK_WIDTH, BLOCK_HEIGHT, IMG_TYPE_LM | IMG_FORMAT_RGBA_8888 | IMG_FLAG_EXTERNAL);
#endif
}

/*
=======================
GL_EndBuildingLightmaps
=======================
*/
void GL_EndBuildingLightmaps (void)
{
	LM_UploadBlock (false);
}

