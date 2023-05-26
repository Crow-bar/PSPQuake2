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
// gu_main.c
#include "gu_local.h"

void R_Clear (void);

viddef_t	vid;

refimport_t	ri;

model_t		*r_worldmodel;

int		gldepthmin, gldepthmax;

glstate_t  gl_state;

image_t		*r_notexture;		// use for bad textures
image_t		*r_particletexture;	// little dot for particles

entity_t	*currententity;
model_t		*currentmodel;

cplane_t	frustum[4];

int			r_visframecount;	// bumped when going to a new PVS
int			r_framecount;		// used for dlight push checking

int			c_brush_polys, c_alias_polys;

float		v_blend[4];			// final blending color

//
// view origin
//
vec3_t	vup;
vec3_t	vpn;
vec3_t	vright;
vec3_t	r_origin;

ScePspFMatrix4	r_world_matrix;
//ScePspFMatrix4	r_base_world_matrix;

//
// screen size info
//
refdef_t	r_newrefdef;

int		r_viewcluster, r_viewcluster2, r_oldviewcluster, r_oldviewcluster2;

cvar_t	*r_norefresh;
cvar_t	*r_drawentities;
cvar_t	*r_drawworld;
cvar_t	*r_speeds;
cvar_t	*r_fullbright;
cvar_t	*r_novis;
cvar_t	*r_nocull;
cvar_t	*r_lerpmodels;
cvar_t	*r_lefthand;

cvar_t	*r_lightlevel;	// FIXME: This is a HACK to get the client's light level

cvar_t	*gl_nosubimage;
cvar_t	*gl_allow_software;

cvar_t	*gl_vertex_arrays;

cvar_t	*gl_particle_min_size;
cvar_t	*gl_particle_max_size;
cvar_t	*gl_particle_size;
cvar_t	*gl_particle_att_a;
cvar_t	*gl_particle_att_b;
cvar_t	*gl_particle_att_c;

cvar_t	*gl_ext_swapinterval;
cvar_t	*gl_ext_palettedtexture;
cvar_t	*gl_ext_multitexture;
cvar_t	*gl_ext_pointparameters;
cvar_t	*gl_ext_compiled_vertex_array;

cvar_t	*gl_log;
cvar_t	*gl_bitdepth;
cvar_t	*gl_drawbuffer;
cvar_t  *gl_driver;
cvar_t	*gl_lightmap;
cvar_t	*gl_shadows;
cvar_t	*gl_mode;
cvar_t	*gl_dynamic;
cvar_t  *gl_monolightmap;
cvar_t	*gl_modulate;
cvar_t	*gl_nobind;
cvar_t	*gl_round_down;
cvar_t	*gl_picmip;
cvar_t	*gl_skymip;
cvar_t	*gl_showtris;
cvar_t	*gl_ztrick;
cvar_t	*gl_finish;
cvar_t	*gl_clear;
cvar_t	*gl_cull;
cvar_t	*gl_polyblend;
cvar_t	*gl_flashblend;
cvar_t	*gl_playermip;
cvar_t  *gl_saturatelighting;
cvar_t	*gl_swapinterval;
cvar_t	*gl_texturemode;
cvar_t	*gl_texturealphamode;
cvar_t	*gl_texturesolidmode;
cvar_t	*gl_lockpvs;

cvar_t	*gl_3dlabs_broken;

cvar_t	*vid_fullscreen;
cvar_t	*vid_gamma;
cvar_t	*vid_ref;

/*
=================
R_CullBox

Returns true if the box is completely outside the frustom
=================
*/
qboolean R_CullBox (vec3_t mins, vec3_t maxs)
{
	int		i;

	if (r_nocull->value)
		return false;

	for (i=0 ; i<4 ; i++)
		if ( BOX_ON_PLANE_SIDE(mins, maxs, &frustum[i]) == 2)
			return true;
	return false;
}


void R_RotateForEntity (entity_t *e)
{
	ScePspFVector3	translate;

	translate.x = e->origin[0];
	translate.y = e->origin[1];
	translate.z = e->origin[2];

	sceGumTranslate (&translate);

	sceGumRotateZ ( e->angles[1] * (GU_PI / 180.0f));
	sceGumRotateY (-e->angles[0] * (GU_PI / 180.0f));
	sceGumRotateX (-e->angles[2] * (GU_PI / 180.0f));

	sceGumUpdateMatrix ();
}

/*
=============================================================

  SPRITE MODELS

=============================================================
*/


/*
=================
R_DrawSpriteModel

=================
*/
void R_DrawSpriteModel (entity_t *e)
{
	float alpha = 1.0F;
	vec3_t	point;
	dsprframe_t	*frame;
	float		*up, *right;
	dsprite_t		*psprite;

	// don't even bother culling, because it's just a single
	// polygon without a surface cache

	psprite = (dsprite_t *)currentmodel->cache.data;

#if 0
	if (e->frame < 0 || e->frame >= psprite->numframes)
	{
		ri.Con_Printf (PRINT_ALL, "no such sprite frame %i\n", e->frame);
		e->frame = 0;
	}
#endif
	e->frame %= psprite->numframes;

	frame = &psprite->frames[e->frame];

#if 0
	if (psprite->type == SPR_ORIENTED)
	{	// bullet marks on walls
	vec3_t		v_forward, v_right, v_up;

	AngleVectors (currententity->angles, v_forward, v_right, v_up);
		up = v_up;
		right = v_right;
	}
	else
#endif
	{	// normal sprite
		up = vup;
		right = vright;
	}

	if ( e->flags & RF_TRANSLUCENT )
		alpha = e->alpha;

	if ( alpha != 1.0F )
		sceGuEnable (GU_BLEND);
	else
		sceGuEnable (GU_ALPHA_TEST);

	sceGuColor (GU_HCOLOR_4F(1, 1, 1, alpha));

    GL_Bind (currentmodel->skins[e->frame]->texnum);

	GL_TexEnv (GU_TFX_MODULATE);

	gu_vert_ftv_t* const out = (gu_vert_ftv_t*)sceGuGetMemory (sizeof(gu_vert_ftv_t) * 4);
	out[0].uv[0] = 0.0f;
	out[0].uv[1] = 1.0f;
	VectorMA (e->origin, -frame->origin_y, up, out[0].xyz);
	VectorMA (out[0].xyz, -frame->origin_x, right, out[0].xyz);
	out[1].uv[0] = 0.0f;
	out[1].uv[1] = 0.0f;
	VectorMA (e->origin, frame->height - frame->origin_y, up, out[1].xyz);
	VectorMA (out[1].xyz, -frame->origin_x, right, out[1].xyz);
	out[2].uv[0] = 1.0f;
	out[2].uv[1] = 0.0f;
	VectorMA (e->origin, frame->height - frame->origin_y, up, out[2].xyz);
	VectorMA (out[2].xyz, frame->width - frame->origin_x, right, out[2].xyz);
	out[3].uv[0] = 1.0f;
	out[3].uv[1] = 1.0f;
	VectorMA (e->origin, -frame->origin_y, up, out[3].xyz);
	VectorMA (out[3].xyz, frame->width - frame->origin_x, right, out[3].xyz);
	sceGuDrawArray (GU_TRIANGLE_FAN, GU_TEXTURE_32BITF | GU_VERTEX_32BITF, 4, 0, out);

	GL_TexEnv (GU_TFX_REPLACE);

	if ( alpha != 1.0F )
		sceGuDisable (GU_BLEND);
	else
		sceGuDisable (GU_ALPHA_TEST);

	sceGuColor (GU_HCOLOR_DEFAULT);
}

//==================================================================================

/*
=============
R_DrawNullModel
=============
*/
#define NULL_MODEL_SIZE	16
void R_DrawNullModel (void)
{
	vec3_t	shadelight;
	int		i;
	float	radians;
	uint	vertcount;

	if ( currententity->flags & RF_FULLBRIGHT )
		shadelight[0] = shadelight[1] = shadelight[2] = 1.0F;
	else
		R_LightPoint (currententity->origin, shadelight);

	sceGumPushMatrix ();
	R_RotateForEntity (currententity);

	sceGuDisable (GU_TEXTURE_2D);
	sceGuColor (GU_HCOLOR_3FV(shadelight));

	gu_vert_ftv_t* out = (gu_vert_ftv_t*)sceGuGetMemory (sizeof(gu_vert_ftv_t) * 12);

	vertcount = 0;

	out[vertcount].x = 0;
	out[vertcount].y = 0;
	out[vertcount].z = -NULL_MODEL_SIZE;
	vertcount++;

	for (i = 0; i <= 4; i++)
	{
		radians = i * M_PI / 2.0f;
		out[vertcount].x = NULL_MODEL_SIZE * cos (radians);
		out[vertcount].y = NULL_MODEL_SIZE * sin (radians);
		out[vertcount].z = 0;

		vertcount++;
	}
	sceGuDrawArray(GU_TRIANGLE_FAN, GU_VERTEX_32BITF | GU_TRANSFORM_3D, vertcount, 0, out);

	out += vertcount;
	vertcount = 0;

	out[vertcount].x = 0;
	out[vertcount].y = 0;
	out[vertcount].z = NULL_MODEL_SIZE;
	vertcount++;

	for (i = 4; i >= 0; i--)
	{
		radians = i * M_PI / 2.0f;
		out[vertcount].x = NULL_MODEL_SIZE * cos (radians);
		out[vertcount].y = NULL_MODEL_SIZE * sin (radians);
		out[vertcount].z = 0;
		vertcount++;
	}
	sceGuDrawArray(GU_TRIANGLE_FAN, GU_VERTEX_32BITF | GU_TRANSFORM_3D, vertcount, 0, out);

	sceGuColor (GU_HCOLOR_DEFAULT);
	sceGumPopMatrix ();
	sceGumUpdateMatrix ();
	sceGuEnable (GU_TEXTURE_2D);
}

/*
=============
R_DrawEntitiesOnList
=============
*/
void R_DrawEntitiesOnList (void)
{
	int		i;

	if (!r_drawentities->value)
		return;

	// draw non-transparent first
	for (i=0 ; i<r_newrefdef.num_entities ; i++)
	{
		currententity = &r_newrefdef.entities[i];
		if (currententity->flags & RF_TRANSLUCENT)
			continue;	// solid

		if ( currententity->flags & RF_BEAM )
		{
			R_DrawBeam( currententity );
		}
		else
		{
			currentmodel = currententity->model;
			if (!currentmodel)
			{
				R_DrawNullModel ();
				continue;
			}
			switch (currentmodel->type)
			{
			case mod_alias:
				R_DrawAliasModel (currententity);
				break;
			case mod_brush:
				R_DrawBrushModel (currententity);
				break;
			case mod_sprite:
				R_DrawSpriteModel (currententity);
				break;
			default:
				ri.Sys_Error (ERR_DROP, "Bad modeltype");
				break;
			}
		}
	}

	// draw transparent entities
	// we could sort these if it ever becomes a problem...
	sceGuDepthMask (GU_TRUE);		// no z writes
	for (i=0 ; i<r_newrefdef.num_entities ; i++)
	{
		currententity = &r_newrefdef.entities[i];
		if (!(currententity->flags & RF_TRANSLUCENT))
			continue;	// solid

		if ( currententity->flags & RF_BEAM )
		{
			R_DrawBeam( currententity );
		}
		else
		{
			currentmodel = currententity->model;

			if (!currentmodel)
			{
				R_DrawNullModel ();
				continue;
			}
			switch (currentmodel->type)
			{
			case mod_alias:
				R_DrawAliasModel (currententity);
				break;
			case mod_brush:
				R_DrawBrushModel (currententity);
				break;
			case mod_sprite:
				R_DrawSpriteModel (currententity);
				break;
			default:
				ri.Sys_Error (ERR_DROP, "Bad modeltype");
				break;
			}
		}
	}
	sceGuDepthMask (GU_FALSE);		// back to writing

}

/*
** GL_DrawParticles
**
*/
void GL_DrawParticles( int num_particles, const particle_t particles[], const unsigned colortable[768] )
{
	const particle_t *p;
	int				i;
	vec3_t			up, right;
	float			scale;
	//uint			color;

    GL_Bind (r_particletexture->texnum);
	sceGuDepthMask (GU_TRUE);		// no z buffering
	sceGuEnable (GU_BLEND);
	GL_TexEnv (GU_TFX_MODULATE);

	//VectorScale (vup, 1.5, up);
	//VectorScale (vright, 1.5, right);

	gu_vert_ftcv_t* const out = (gu_vert_ftcv_t*)sceGuGetMemory (sizeof(gu_vert_ftcv_t) * num_particles * 2);

	for ( p = particles, i = 0; i < num_particles; i++, p++)
	{
#if 1
		// hack a scale up to keep particles from disapearing
		scale = ( p->origin[0] - r_origin[0] ) * vpn[0] +
			    ( p->origin[1] - r_origin[1] ) * vpn[1] +
			    ( p->origin[2] - r_origin[2] ) * vpn[2];

		if (scale < 20)
			scale = 1;
		else
			scale = 1 + scale * 0.004f;

		VectorScale (vup, scale, up);
		VectorScale (vright, scale, right);

		//color = d_8to24table[p->color];
		//(byte *)color[3] = p->alpha*255;

		out[i * 2 + 0].u = 0.0f;
		out[i * 2 + 0].v = 0.0f;
		out[i * 2 + 0].c = d_8to24table[p->color];
		out[i * 2 + 0].a = p->alpha * 255; // replace alpha
		out[i * 2 + 0].x = p->origin[0] + right[0] + up[0];
		out[i * 2 + 0].y = p->origin[1] + right[1] + up[1];
		out[i * 2 + 0].z = p->origin[2] + right[2] + up[2];

		out[i * 2 + 1].u = 1.0f;
		out[i * 2 + 1].v = 1.0f;
		out[i * 2 + 1].c = out[i * 2 + 0].c;
		out[i * 2 + 1].x = p->origin[0] - right[0] - up[0];
		out[i * 2 + 1].y = p->origin[1] - right[1] - up[1];
		out[i * 2 + 1].z = p->origin[2] - right[2] - up[2];
#else
		// hack a scale up to keep particles from disapearing
		scale = ( p->origin[0] - r_origin[0] ) * vpn[0] +
			    ( p->origin[1] - r_origin[1] ) * vpn[1] +
			    ( p->origin[2] - r_origin[2] ) * vpn[2];

		if (scale < 20)
			scale = 1;
		else
			scale = 1 + scale * 0.004;

		*(int *)color = colortable[p->color];
		color[3] = p->alpha*255;

		qglColor4ubv( color );

		qglTexCoord2f( 0.0625, 0.0625 );
		qglVertex3fv( p->origin );

		qglTexCoord2f( 1.0625, 0.0625 );
		qglVertex3f( p->origin[0] + up[0]*scale,
			         p->origin[1] + up[1]*scale,
					 p->origin[2] + up[2]*scale);

		qglTexCoord2f( 0.0625, 1.0625 );
		qglVertex3f( p->origin[0] + right[0]*scale,
			         p->origin[1] + right[1]*scale,
					 p->origin[2] + right[2]*scale);
#endif
	}
	sceGuDrawArray (GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF, num_particles * 2, 0, out);

	sceGuDisable (GU_BLEND);
	sceGuColor (GU_HCOLOR_DEFAULT);
	sceGuDepthMask (GU_FALSE);		// back to normal Z buffering
	GL_TexEnv (GU_TFX_REPLACE);
}

/*
===============
R_DrawParticles
===============
*/
void R_DrawParticles (void)
{
	if ( gl_ext_pointparameters->value )
	{
		int		i;
		//uint	color;
		const particle_t *p;

		sceGuDepthMask (GU_TRUE);
		sceGuEnable (GU_BLEND);
		sceGuDisable (GU_TEXTURE_2D);

		//qglPointSize( gl_particle_size->value );
		gu_vert_fcv_t* const out = (gu_vert_fcv_t*)sceGuGetMemory (sizeof(gu_vert_fcv_t) * r_newrefdef.num_particles);

		for ( i = 0, p = r_newrefdef.particles; i < r_newrefdef.num_particles; i++, p++ )
		{
			//color = d_8to24table[p->color];
			//(byte *)color[3] = p->alpha * 255;

			out[i].c = d_8to24table[p->color];
			out[i].a = p->alpha * 255;

			out[i].x = p->origin[0];
			out[i].y = p->origin[1];
			out[i].z = p->origin[2];
		}
		sceGuDrawArray (GU_POINTS, GU_COLOR_8888 | GU_VERTEX_32BITF, r_newrefdef.num_particles, 0, out);

		sceGuDisable (GU_BLEND);
		sceGuColor (GU_HCOLOR_DEFAULT);
		sceGuDepthMask (GU_FALSE);
		sceGuEnable (GU_TEXTURE_2D);

	}
	else
	{
		GL_DrawParticles (r_newrefdef.num_particles, r_newrefdef.particles, d_8to24table);
	}
}

/*
============
R_PolyBlend
============
*/
void R_PolyBlend (void)
{
	if (!gl_polyblend->value)
		return;
	if (!v_blend[3])
		return;

	sceGuEnable (GU_BLEND);
	sceGuDisable (GU_DEPTH_TEST);
	sceGuDisable (GU_TEXTURE_2D);

	sceGuColor (GU_HCOLOR_4FV(v_blend));

	gu_vert_hv_t* const out = (gu_vert_hv_t*)sceGuGetMemory(sizeof(gu_vert_hv_t) * 2);
	out[0].x = 0;
	out[0].y = 0;
	out[0].z = 0;
	out[1].x = vid.width;
	out[1].y = vid.height;
	out[1].z = 0;
	sceGuDrawArray (GU_SPRITES, GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, out); // through mode

	sceGuDisable (GU_BLEND);
	sceGuEnable (GU_TEXTURE_2D);

	sceGuColor (GU_HCOLOR_DEFAULT);
}

//=======================================================================

int SignbitsForPlane (cplane_t *out)
{
	int	bits, j;

	// for fast box on planeside test

	bits = 0;
	for (j=0 ; j<3 ; j++)
	{
		if (out->normal[j] < 0)
			bits |= 1<<j;
	}
	return bits;
}


void R_SetFrustum (void)
{
	int		i;

#if 0
	/*
	** this code is wrong, since it presume a 90 degree FOV both in the
	** horizontal and vertical plane
	*/
	// front side is visible
	VectorAdd (vpn, vright, frustum[0].normal);
	VectorSubtract (vpn, vright, frustum[1].normal);
	VectorAdd (vpn, vup, frustum[2].normal);
	VectorSubtract (vpn, vup, frustum[3].normal);

	// we theoretically don't need to normalize these vectors, but I do it
	// anyway so that debugging is a little easier
	VectorNormalize( frustum[0].normal );
	VectorNormalize( frustum[1].normal );
	VectorNormalize( frustum[2].normal );
	VectorNormalize( frustum[3].normal );
#else
	// rotate VPN right by FOV_X/2 degrees
	RotatePointAroundVector( frustum[0].normal, vup, vpn, -(90-r_newrefdef.fov_x / 2 ) );
	// rotate VPN left by FOV_X/2 degrees
	RotatePointAroundVector( frustum[1].normal, vup, vpn, 90-r_newrefdef.fov_x / 2 );
	// rotate VPN up by FOV_X/2 degrees
	RotatePointAroundVector( frustum[2].normal, vright, vpn, 90-r_newrefdef.fov_y / 2 );
	// rotate VPN down by FOV_X/2 degrees
	RotatePointAroundVector( frustum[3].normal, vright, vpn, -( 90 - r_newrefdef.fov_y / 2 ) );
#endif

	for (i=0 ; i<4 ; i++)
	{
		frustum[i].type = PLANE_ANYZ;
		frustum[i].dist = DotProduct (r_origin, frustum[i].normal);
		frustum[i].signbits = SignbitsForPlane (&frustum[i]);
	}
}

//=======================================================================

/*
===============
R_SetupFrame
===============
*/
void R_SetupFrame (void)
{
	int i;
	mleaf_t	*leaf;

	r_framecount++;

// build the transformation matrix for the given view angles
	VectorCopy (r_newrefdef.vieworg, r_origin);

	AngleVectors (r_newrefdef.viewangles, vpn, vright, vup);

// current viewcluster
	if ( !( r_newrefdef.rdflags & RDF_NOWORLDMODEL ) )
	{
		r_oldviewcluster = r_viewcluster;
		r_oldviewcluster2 = r_viewcluster2;
		leaf = Mod_PointInLeaf (r_origin, r_worldmodel);
		r_viewcluster = r_viewcluster2 = leaf->cluster;

		// check above and below so crossing solid water doesn't draw wrong
		if (!leaf->contents)
		{	// look down a bit
			vec3_t	temp;

			VectorCopy (r_origin, temp);
			temp[2] -= 16;
			leaf = Mod_PointInLeaf (temp, r_worldmodel);
			if ( !(leaf->contents & CONTENTS_SOLID) &&
				(leaf->cluster != r_viewcluster2) )
				r_viewcluster2 = leaf->cluster;
		}
		else
		{	// look up a bit
			vec3_t	temp;

			VectorCopy (r_origin, temp);
			temp[2] += 16;
			leaf = Mod_PointInLeaf (temp, r_worldmodel);
			if ( !(leaf->contents & CONTENTS_SOLID) &&
				(leaf->cluster != r_viewcluster2) )
				r_viewcluster2 = leaf->cluster;
		}
	}

	for (i=0 ; i<4 ; i++)
		v_blend[i] = r_newrefdef.blend[i];

	c_brush_polys = 0;
	c_alias_polys = 0;

#if 0 // not used, sceGuClear has a bug
	// clear out the portion of the screen that the NOWORLDMODEL defines
	if ( r_newrefdef.rdflags & RDF_NOWORLDMODEL )
	{
		//sceGuEnable (GU_SCISSOR_TEST);
		sceGuClearColor (GU_HCOLOR_4F(0.3, 0.3, 0.3, 1.0));
		sceGuScissor (r_newrefdef.x, vid.height - r_newrefdef.height - r_newrefdef.y, r_newrefdef.width, r_newrefdef.height );
		sceGuClear (GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
		sceGuClearColor (GU_HCOLOR_4F(1.0, 0.0, 0.5, 0.5));
		//sceGuDisable (GU_SCISSOR_TEST);
	}
#endif
}

/*
=============
R_SetupGL
=============
*/
void R_SetupGL (void)
{
	float	screenaspect;
//	float	yfov;
	int		x, x2, y2, y, w, h;
	ScePspFVector3	translate;


	//
	// set up viewport
	//
	x = floor(r_newrefdef.x * vid.width / vid.width);
	x2 = ceil((r_newrefdef.x + r_newrefdef.width) * vid.width / vid.width);
	y = floor(vid.height - r_newrefdef.y * vid.height / vid.height);
	y2 = ceil(vid.height - (r_newrefdef.y + r_newrefdef.height) * vid.height / vid.height);

	w = x2 - x;
	h = y - y2;

	sceGuViewport ( 2048 /*- ( vid.width >> 1 ) + x + ( w >> 1 )*/,
					2048 + (vid.height >> 1) - y2 - (h >> 1),
					w, h);
	sceGuScissor (x, vid.height - y2 - h, x + w, vid.height - y2);

	//
	// set up projection matrix
	//
    screenaspect = (float)r_newrefdef.width/r_newrefdef.height;
//	yfov = 2*atan((float)r_newrefdef.height/r_newrefdef.width)*180/M_PI;
	sceGumMatrixMode (GU_PROJECTION);
	sceGumLoadIdentity ();

    sceGumPerspective (r_newrefdef.fov_y,  screenaspect,  4,  4096);

	sceGuFrontFace (GU_CW); // GL_FRONT

	sceGumMatrixMode (GU_VIEW);
    sceGumLoadIdentity ();

	sceGumRotateX (-90 * (GU_PI / 180.0f));
	sceGumRotateZ ( 90 * (GU_PI / 180.0f));
	sceGumRotateX (-r_newrefdef.viewangles[2] * (GU_PI / 180.0f));
	sceGumRotateY (-r_newrefdef.viewangles[0] * (GU_PI / 180.0f));
	sceGumRotateZ (-r_newrefdef.viewangles[1] * (GU_PI / 180.0f));

	translate.x = -r_newrefdef.vieworg[0];
	translate.y = -r_newrefdef.vieworg[1];
	translate.z = -r_newrefdef.vieworg[2];

	sceGumTranslate (&translate);

//	if ( gl_state.camera_separation != 0 && gl_state.stereo_enabled )
//		qglTranslatef ( gl_state.camera_separation, 0, 0 );

	sceGumMatrixMode (GU_MODEL);
	sceGumLoadIdentity ();

	sceGumUpdateMatrix ();	// apply a matrix transformation

	//sceGumStoreMatrix(&r_world_matrix);

	//
	// set drawing parms
	//
	if (gl_cull->value)
		sceGuEnable (GU_CULL_FACE);
	else
		sceGuDisable (GU_CULL_FACE);

	sceGuDisable (GU_BLEND);
	sceGuDisable (GU_ALPHA_TEST);
	sceGuEnable (GU_DEPTH_TEST);

	GU_ClipSetWorldFrustum ();
}

/*
=============
R_Clear
=============
*/
void R_Clear (void)
{
	int	cmask;

	cmask = GU_DEPTH_BUFFER_BIT | GU_FAST_CLEAR_BIT;

	if (gl_clear->value)
		cmask |= GU_COLOR_BUFFER_BIT;

	sceGuClear (cmask);

	gldepthmin = 0;
	gldepthmax = 65535;

	sceGuDepthFunc (GU_LEQUAL);
	sceGuDepthRange (gldepthmin, gldepthmax);
}

/*
================
R_RenderView

r_newrefdef must be set before the first call
================
*/
void R_RenderView (refdef_t *fd)
{
	if (r_norefresh->value)
		return;

	r_newrefdef = *fd;

	if (!r_worldmodel && !( r_newrefdef.rdflags & RDF_NOWORLDMODEL ) )
		ri.Sys_Error (ERR_DROP, "R_RenderView: NULL worldmodel");

	if (r_speeds->value)
	{
		c_brush_polys = 0;
		c_alias_polys = 0;
	}

	R_PushDlights ();

#if 0
	if (gl_finish->value)
		qglFinish ();
#endif

	R_SetupFrame ();

	R_SetFrustum ();

	R_SetupGL ();

	R_MarkLeaves ();	// done here so we know if we're in water

	R_DrawWorld ();

	R_DrawEntitiesOnList ();

	R_RenderDlights ();

	R_DrawParticles ();

	R_DrawAlphaSurfaces ();

	R_PolyBlend(); // flash

	if (r_speeds->value)
	{
		ri.Con_Printf (PRINT_ALL, "%4i wpoly %4i epoly %i tex %i lmaps\n",
			c_brush_polys,
			c_alias_polys,
			c_visible_textures,
			c_visible_lightmaps);
	}
}


void	R_SetGL2D (void)
{
	// set 2D virtual screen size
#if 1
	sceGuViewport (2048, 2048, vid.width, vid.height);
	sceGuScissor (0, 0, vid.width, vid.height);
	sceGuDisable (GU_DEPTH_TEST);
	sceGuDisable (GU_CULL_FACE);
	sceGuDisable (GU_BLEND);
	sceGuEnable (GU_ALPHA_TEST);
	sceGuColor (GU_HCOLOR_DEFAULT);
#else
	qglViewport (0,0, vid.width, vid.height);
	qglMatrixMode(GL_PROJECTION);
    qglLoadIdentity ();
	qglOrtho  (0, vid.width, vid.height, 0, -99999, 99999);
	qglMatrixMode(GL_MODELVIEW);
    qglLoadIdentity ();
	qglDisable (GL_DEPTH_TEST);
	qglDisable (GL_CULL_FACE);
	qglDisable (GL_BLEND);
	qglEnable (GL_ALPHA_TEST);
	qglColor4f (1,1,1,1);
#endif
}

#if 0
static void GL_DrawColoredStereoLinePair( float r, float g, float b, float y )
{
	qglColor3f( r, g, b );
	qglVertex2f( 0, y );
	qglVertex2f( vid.width, y );
	qglColor3f( 0, 0, 0 );
	qglVertex2f( 0, y + 1 );
	qglVertex2f( vid.width, y + 1 );
}

static void GL_DrawStereoPattern( void )
{
	int i;

	if ( !( gl_config.renderer & GL_RENDERER_INTERGRAPH ) )
		return;

	if ( !gl_state.stereo_enabled )
		return;

	R_SetGL2D();

	qglDrawBuffer( GL_BACK_LEFT );

	for ( i = 0; i < 20; i++ )
	{
		qglBegin( GL_LINES );
			GL_DrawColoredStereoLinePair( 1, 0, 0, 0 );
			GL_DrawColoredStereoLinePair( 1, 0, 0, 2 );
			GL_DrawColoredStereoLinePair( 1, 0, 0, 4 );
			GL_DrawColoredStereoLinePair( 1, 0, 0, 6 );
			GL_DrawColoredStereoLinePair( 0, 1, 0, 8 );
			GL_DrawColoredStereoLinePair( 1, 1, 0, 10);
			GL_DrawColoredStereoLinePair( 1, 1, 0, 12);
			GL_DrawColoredStereoLinePair( 0, 1, 0, 14);
		qglEnd();

		GU_EndFrame();
	}
}
#endif

/*
====================
R_SetLightLevel

====================
*/
void R_SetLightLevel (void)
{
	vec3_t		shadelight;

	if (r_newrefdef.rdflags & RDF_NOWORLDMODEL)
		return;

	// save off light value for server to look at (BIG HACK!)

	R_LightPoint (r_newrefdef.vieworg, shadelight);

	// pick the greatest component, which should be the same
	// as the mono value returned by software
	if (shadelight[0] > shadelight[1])
	{
		if (shadelight[0] > shadelight[2])
			r_lightlevel->value = 150*shadelight[0];
		else
			r_lightlevel->value = 150*shadelight[2];
	}
	else
	{
		if (shadelight[1] > shadelight[2])
			r_lightlevel->value = 150*shadelight[1];
		else
			r_lightlevel->value = 150*shadelight[2];
	}

}

/*
@@@@@@@@@@@@@@@@@@@@@
R_RenderFrame

@@@@@@@@@@@@@@@@@@@@@
*/
void R_RenderFrame (refdef_t *fd)
{
	R_RenderView( fd );
	R_SetLightLevel ();
	R_SetGL2D ();
}


void R_Register( void )
{
	r_lefthand = ri.Cvar_Get( "hand", "0", CVAR_USERINFO | CVAR_ARCHIVE );
	r_norefresh = ri.Cvar_Get ("r_norefresh", "0", 0);
	r_fullbright = ri.Cvar_Get ("r_fullbright", "0", 0);
	r_drawentities = ri.Cvar_Get ("r_drawentities", "1", 0);
	r_drawworld = ri.Cvar_Get ("r_drawworld", "1", 0);
	r_novis = ri.Cvar_Get ("r_novis", "0", 0);
	r_nocull = ri.Cvar_Get ("r_nocull", "0", 0);
	r_lerpmodels = ri.Cvar_Get ("r_lerpmodels", "1", 0);
	r_speeds = ri.Cvar_Get ("r_speeds", "0", 0);

	r_lightlevel = ri.Cvar_Get ("r_lightlevel", "0", 0);

	gl_nosubimage = ri.Cvar_Get( "gl_nosubimage", "0", 0 );
	gl_allow_software = ri.Cvar_Get( "gl_allow_software", "0", 0 );

	gl_particle_min_size = ri.Cvar_Get( "gl_particle_min_size", "2", CVAR_ARCHIVE );
	gl_particle_max_size = ri.Cvar_Get( "gl_particle_max_size", "40", CVAR_ARCHIVE );
	gl_particle_size = ri.Cvar_Get( "gl_particle_size", "40", CVAR_ARCHIVE );
	gl_particle_att_a = ri.Cvar_Get( "gl_particle_att_a", "0.01", CVAR_ARCHIVE );
	gl_particle_att_b = ri.Cvar_Get( "gl_particle_att_b", "0.0", CVAR_ARCHIVE );
	gl_particle_att_c = ri.Cvar_Get( "gl_particle_att_c", "0.01", CVAR_ARCHIVE );

	gl_modulate = ri.Cvar_Get ("gl_modulate", "1", CVAR_ARCHIVE );
	gl_log = ri.Cvar_Get( "gl_log", "0", 0 );
	gl_bitdepth = ri.Cvar_Get( "gl_bitdepth", "0", 0 );
	gl_mode = ri.Cvar_Get( "gl_mode", "3", CVAR_ARCHIVE );
	gl_lightmap = ri.Cvar_Get ("gl_lightmap", "0", 0);
	gl_shadows = ri.Cvar_Get ("gl_shadows", "0", CVAR_ARCHIVE );
	gl_dynamic = ri.Cvar_Get ("gl_dynamic", "1", 0);
	gl_nobind = ri.Cvar_Get ("gl_nobind", "0", 0);
	gl_round_down = ri.Cvar_Get ("gl_round_down", "1", 0);
	gl_picmip = ri.Cvar_Get ("gl_picmip", "0", 0);
	gl_skymip = ri.Cvar_Get ("gl_skymip", "0", 0);
	gl_showtris = ri.Cvar_Get ("gl_showtris", "0", 0);
	gl_ztrick = ri.Cvar_Get ("gl_ztrick", "0", 0);
	gl_finish = ri.Cvar_Get ("gl_finish", "0", CVAR_ARCHIVE);
	gl_clear = ri.Cvar_Get ("gl_clear", "0", 0);
	gl_cull = ri.Cvar_Get ("gl_cull", "1", 0);
	gl_polyblend = ri.Cvar_Get ("gl_polyblend", "1", 0);
	gl_flashblend = ri.Cvar_Get ("gl_flashblend", "1", 0);
	gl_playermip = ri.Cvar_Get ("gl_playermip", "0", 0);
	gl_monolightmap = ri.Cvar_Get( "gl_monolightmap", "0", 0 );
	gl_driver = ri.Cvar_Get( "gl_driver", "opengl32", CVAR_ARCHIVE );
	gl_texturemode = ri.Cvar_Get( "gl_texturemode", "GL_LINEAR_MIPMAP_NEAREST", CVAR_ARCHIVE );
	gl_texturealphamode = ri.Cvar_Get( "gl_texturealphamode", "default", CVAR_ARCHIVE );
	gl_texturesolidmode = ri.Cvar_Get( "gl_texturesolidmode", "default", CVAR_ARCHIVE );
	gl_lockpvs = ri.Cvar_Get( "gl_lockpvs", "0", 0 );

	gl_vertex_arrays = ri.Cvar_Get( "gl_vertex_arrays", "0", CVAR_ARCHIVE );

	gl_ext_swapinterval = ri.Cvar_Get( "gl_ext_swapinterval", "1", CVAR_ARCHIVE );
	gl_ext_palettedtexture = ri.Cvar_Get( "gl_ext_palettedtexture", "1", CVAR_ARCHIVE );
	gl_ext_multitexture = ri.Cvar_Get( "gl_ext_multitexture", "1", CVAR_ARCHIVE );
	gl_ext_pointparameters = ri.Cvar_Get( "gl_ext_pointparameters", "1", CVAR_ARCHIVE );
	gl_ext_compiled_vertex_array = ri.Cvar_Get( "gl_ext_compiled_vertex_array", "1", CVAR_ARCHIVE );

	gl_drawbuffer = ri.Cvar_Get( "gl_drawbuffer", "GL_BACK", 0 );
	gl_swapinterval = ri.Cvar_Get( "gl_swapinterval", "1", CVAR_ARCHIVE );

	gl_saturatelighting = ri.Cvar_Get( "gl_saturatelighting", "0", 0 );

	gl_3dlabs_broken = ri.Cvar_Get( "gl_3dlabs_broken", "1", CVAR_ARCHIVE );

	vid_fullscreen = ri.Cvar_Get( "vid_fullscreen", "0", CVAR_ARCHIVE );
	vid_gamma = ri.Cvar_Get( "vid_gamma", "1.0", CVAR_ARCHIVE );
	vid_ref = ri.Cvar_Get( "vid_ref", "gu", CVAR_ARCHIVE );

	ri.Cmd_AddCommand( "imagelist", GL_ImageList_f );
	ri.Cmd_AddCommand( "screenshot", GL_ScreenShot_f );
	ri.Cmd_AddCommand( "modellist", Mod_Modellist_f );
}

/*
==================
R_SetMode
==================
*/
qboolean R_SetMode (void)
{
	rserr_t err;
	qboolean fullscreen;

	if (vid_fullscreen->modified)
	{
		ri.Con_Printf( PRINT_ALL, "R_SetMode() - CDS not allowed with this driver\n" );
		ri.Cvar_SetValue( "vid_fullscreen", !vid_fullscreen->value );
		vid_fullscreen->modified = false;
	}

	fullscreen = vid_fullscreen->value;

	vid_fullscreen->modified = false;
	gl_mode->modified = false;

	if ( ( err = GU_SetMode( &vid.width, &vid.height, gl_mode->value, fullscreen ) ) == rserr_ok )
	{
		gl_state.prev_mode = gl_mode->value;
	}
	else
	{
		if ( err == rserr_invalid_fullscreen )
		{
			ri.Cvar_SetValue( "vid_fullscreen", 0);
			vid_fullscreen->modified = false;
			ri.Con_Printf( PRINT_ALL, "ref_gl::R_SetMode() - fullscreen unavailable in this mode\n" );
			if ( ( err = GU_SetMode( &vid.width, &vid.height, gl_mode->value, false ) ) == rserr_ok )
				return true;
		}
		else if ( err == rserr_invalid_mode )
		{
			ri.Cvar_SetValue( "gl_mode", gl_state.prev_mode );
			gl_mode->modified = false;
			ri.Con_Printf( PRINT_ALL, "ref_gl::R_SetMode() - invalid mode\n" );
		}

		// try setting it back to something safe
		if ( ( err = GU_SetMode( &vid.width, &vid.height, gl_state.prev_mode, false ) ) != rserr_ok )
		{
			ri.Con_Printf( PRINT_ALL, "ref_gl::R_SetMode() - could not revert to safe mode\n" );
			return false;
		}
	}
	return true;
}

/*
===============
R_Init
===============
*/
qboolean R_Init( void *hinstance, void *hWnd )
{
	int		err;
	int		j;
	extern float r_turbsin[256];

	for ( j = 0; j < 256; j++ )
	{
		r_turbsin[j] *= 0.5;
	}

	memset(&gl_state, 0, sizeof(gl_state));

	gl_state.currenttexture = -1;
	gl_state.currenttexenv = -1;

	ri.Con_Printf (PRINT_ALL, "ref_gu version: "REF_VERSION"\n");

	Draw_GetPalette ();

	R_Register();

	// set our "safe" modes
	gl_state.prev_mode = 3;

	// initialize OS-specific parts
	if (!GU_Init( hinstance, hWnd ))
		return false;

	// create the window and set up the context
	if ( !R_SetMode () )
	{
        ri.Con_Printf (PRINT_ALL, "ref_gl::R_Init() - could not R_SetMode()\n" );
		return false;
	}

	//ri.Vid_MenuInit();

	/*
	** draw our stereo patterns
	*/
#if 0 // commented out until H3D pays us the money they owe us
	GL_DrawStereoPattern();
#endif

	GL_InitImages ();
	Mod_Init ();
	R_InitParticleTexture ();
	Draw_InitLocal ();

	return true;
}

/*
===============
R_Shutdown
===============
*/
void R_Shutdown (void)
{
	ri.Cmd_RemoveCommand ("modellist");
	ri.Cmd_RemoveCommand ("screenshot");
	ri.Cmd_RemoveCommand ("imagelist");

	Mod_FreeAll ();

	GL_ShutdownImages ();

	/*
	** shut down OS specific GU stuff like contexts, etc.
	*/
	GU_Shutdown();
}



/*
@@@@@@@@@@@@@@@@@@@@@
R_BeginFrame
@@@@@@@@@@@@@@@@@@@@@
*/
void R_BeginFrame( float camera_separation )
{

	gl_state.camera_separation = camera_separation;

	/*
	** change modes if necessary
	*/
	if ( gl_mode->modified || vid_fullscreen->modified )
	{	// FIXME: only restart if CDS is required
		cvar_t	*ref;

		ref = ri.Cvar_Get ("vid_ref", "gu", 0);
		ref->modified = true;
	}

	if ( gl_log->modified )
	{
		GU_EnableLogging( gl_log->value );
		gl_log->modified = false;
	}

	if ( gl_log->value )
	{
		GU_LogNewFrame();
	}

	if ( vid_gamma->modified || intensity->modified )
	{
		GL_BuildGammaTable (vid_gamma->value, intensity->value);
		if ( vid_gamma->modified )
			R_SetPalette (NULL);

		vid_gamma->modified = false;
		intensity->modified = false;
	}

	GU_BeginFrame( camera_separation );

	/*
	** go into 2D mode
	*/
	R_SetGL2D ();

	/*
	** swapinterval stuff
	*/
	GL_UpdateSwapInterval();

	//
	// clear screen if desired
	//
	R_Clear ();
}

/*
=============
R_SetPalette
=============
*/
unsigned r_rawpalette[256] __attribute__((aligned(16)));

void R_SetPalette (const unsigned char *palette)
{
	int		i;
	byte	color[4];

	byte *rp = (byte *)r_rawpalette;

#if 0
	if (palette)
	{
		for (i = 0; i < 256; i++)
		{
			rp[i*4+0] = palette[i*3+0];
			rp[i*4+1] = palette[i*3+1];
			rp[i*4+2] = palette[i*3+2];
			rp[i*4+3] = 0xff;
		}
	}
	else
	{
		for (i = 0; i < 256; i++)
		{
			rp[i*4+0] = d_8to24table[i] & 0xff;
			rp[i*4+1] = (d_8to24table[i] >> 8) & 0xff;
			rp[i*4+2] = (d_8to24table[i] >> 16) & 0xff;
			rp[i*4+3] = 0xff;
		}
	}
	GL_SetTexturePalette( r_rawpalette );
#else
	for (i = 0; i < 256; i++)
	{
		if ( palette )
		{
			color[0] = palette[i * 3 + 0];
			color[1] = palette[i * 3 + 1];
			color[2] = palette[i * 3 + 2];
			color[3] = 0xff;
		}
		else
		{
			color[0] = d_8to24table[i] & 0xff;
			color[1] = (d_8to24table[i] >> 8) & 0xff;
			color[2] = (d_8to24table[i] >> 16) & 0xff;
			color[3] = (d_8to24table[i] >> 24) & 0xff;
		}

		rp[i * 4 + 0] = gl_state.gammatable[color[0]];
		rp[i * 4 + 1] = gl_state.gammatable[color[1]];
		rp[i * 4 + 2] = gl_state.gammatable[color[2]];
		rp[i * 4 + 3] = color[3];
	}

	GL_SetTexturePalette (r_rawpalette);
#endif

	sceGuClearColor (0x00000000);
	sceGuClear (GU_COLOR_BUFFER_BIT | GU_FAST_CLEAR_BIT);
	sceGuClearColor (GU_HCOLOR_4F(1.0, 0.0, 0.5 , 0.5));
}

/*
** R_DrawBeam
*/
void R_DrawBeam( entity_t *e )
{
#define NUM_BEAM_SEGS 6

	int		i;
	byte	color[4];

	vec3_t	perpvec;
	vec3_t	direction, normalized_direction;
	vec3_t	start_points[NUM_BEAM_SEGS], end_points[NUM_BEAM_SEGS];
	vec3_t	oldorigin, origin;

	oldorigin[0] = e->oldorigin[0];
	oldorigin[1] = e->oldorigin[1];
	oldorigin[2] = e->oldorigin[2];

	origin[0] = e->origin[0];
	origin[1] = e->origin[1];
	origin[2] = e->origin[2];

	normalized_direction[0] = direction[0] = oldorigin[0] - origin[0];
	normalized_direction[1] = direction[1] = oldorigin[1] - origin[1];
	normalized_direction[2] = direction[2] = oldorigin[2] - origin[2];

	if ( VectorNormalize( normalized_direction ) == 0 )
		return;

	PerpendicularVector( perpvec, normalized_direction );
	VectorScale( perpvec, e->frame / 2, perpvec );

	for ( i = 0; i < NUM_BEAM_SEGS; i++ )
	{
		RotatePointAroundVector( start_points[i], normalized_direction, perpvec, (360.0/NUM_BEAM_SEGS)*i );
		VectorAdd( start_points[i], origin, start_points[i] );
		VectorAdd( start_points[i], direction, end_points[i] );
	}

	sceGuDisable (GU_TEXTURE_2D);
	sceGuEnable (GU_BLEND);
	sceGuDepthMask (GU_TRUE);

	color[0] = ( d_8to24table[e->skinnum & 0xFF] ) & 0xFF;
	color[1] = ( d_8to24table[e->skinnum & 0xFF] >> 8 ) & 0xFF;
	color[2] = ( d_8to24table[e->skinnum & 0xFF] >> 16 ) & 0xFF;
	color[3] = e->alpha*255;

	sceGuColor (GU_HCOLOR_4UBV(color));

	gu_vert_fv_t* const out = (gu_vert_fv_t*)sceGuGetMemory (sizeof(gu_vert_fv_t) * NUM_BEAM_SEGS * 4);
	for ( i = 0; i < NUM_BEAM_SEGS; i++ )
	{
		VectorCopy (start_points[i],                        out[i * 4 + 0].xyz);
		VectorCopy (end_points[i],                          out[i * 4 + 1].xyz);
		VectorCopy (start_points[(i + 1) % NUM_BEAM_SEGS],  out[i * 4 + 2].xyz);
		VectorCopy (end_points[(i + 1) % NUM_BEAM_SEGS],    out[i * 4 + 3].xyz);
	}
	sceGuDrawArray (GU_TRIANGLE_STRIP, GU_VERTEX_32BITF, NUM_BEAM_SEGS * 4, 0, out);

	sceGuEnable (GU_TEXTURE_2D);
	sceGuDisable (GU_BLEND);
	sceGuDepthMask (GU_TRUE);
}

//===================================================================


void	R_BeginRegistration (char *map);
struct model_s	*R_RegisterModel (char *name);
struct image_s	*R_RegisterSkin (char *name);
void	R_EndRegistration (void);
void	R_ClearRegistered (void);

void	R_RenderFrame (refdef_t *fd);

struct image_s	*Draw_FindPic (char *name);

void	Draw_Pic (int x, int y, char *name);
void	Draw_Char (int x, int y, int c);
void	Draw_TileClear (int x, int y, int w, int h, char *name);
void	Draw_Fill (int x, int y, int w, int h, int c);
void	Draw_FadeScreen (void);

/*
@@@@@@@@@@@@@@@@@@@@@
GetRefAPI

@@@@@@@@@@@@@@@@@@@@@
*/
refexport_t GetRefAPI (refimport_t rimp )
{
	refexport_t	re;

	ri = rimp;

	re.api_version = API_VERSION;

	re.BeginRegistration = R_BeginRegistration;
	re.RegisterModel = R_RegisterModel;
	re.RegisterSkin = R_RegisterSkin;
	re.RegisterPic = Draw_FindPic;
	re.SetSky = R_SetSky;
	re.EndRegistration = R_EndRegistration;
	re.ClearRegistered = R_ClearRegistered;

	re.RenderFrame = R_RenderFrame;

	re.DrawGetPicSize = Draw_GetPicSize;
	re.DrawPic = Draw_Pic;
	re.DrawStretchPic = Draw_StretchPic;
	re.DrawChar = Draw_Char;
	re.DrawTileClear = Draw_TileClear;
	re.DrawFill = Draw_Fill;
	re.DrawFadeScreen= Draw_FadeScreen;

	re.DrawStretchRaw = Draw_StretchRaw;

	re.Init = R_Init;
	re.Shutdown = R_Shutdown;

	re.CinematicSetPalette = R_SetPalette;
	re.BeginFrame = R_BeginFrame;
	re.EndFrame = GU_EndFrame;

	re.AppActivate = GU_AppActivate;

	return re;
}


#ifndef REF_HARD_LINKED
// this is only here so the functions in q_shared.c and q_shwin.c can link
void Sys_Error (char *error, ...)
{
	va_list		argptr;
	char		text[1024];

	va_start (argptr, error);
	vsprintf (text, error, argptr);
	va_end (argptr);

	ri.Sys_Error (ERR_FATAL, "%s", text);
}

void Com_Printf (char *fmt, ...)
{
	va_list		argptr;
	char		text[1024];

	va_start (argptr, fmt);
	vsprintf (text, fmt, argptr);
	va_end (argptr);

	ri.Con_Printf (PRINT_ALL, "%s", text);
}

#endif
