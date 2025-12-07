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
// gu_light.c

#include "gu_local.h"

int	r_dlightframecount;

#define	DLIGHT_CUTOFF	64

/*
=============================================================================

DYNAMIC LIGHTS BLEND RENDERING

=============================================================================
*/

// sin(((16 - i) / 16.0) * (M_PI * 2))
// cos(((16 - i) / 16.0) * (M_PI * 2))
static float dlighttab[17][2] = {
{ 0.000000,  1.000000},
{-0.382683,  0.923879},
{-0.707107,  0.707107},
{-0.923879,  0.382683},
{-1.000000,  0.000000},
{-0.923879, -0.382683},
{-0.707107, -0.707107},
{-0.382683, -0.923879},
{-0.000000, -1.000000},
{ 0.382683, -0.923879},
{ 0.707107, -0.707107},
{ 0.923879, -0.382683},
{ 1.000000, -0.000000},
{ 0.923879,  0.382683},
{ 0.707107,  0.707107},
{ 0.382683,  0.923879},
{ 0.000000,  1.000000},
};

void R_RenderDlight (dlight_t *light)
{
	int		i;
	uint	vertcount;

	gu_vert_fcv_t* const out = (gu_vert_fcv_t*)extGuGetAlignedMemory(16, sizeof(gu_vert_fcv_t) * 18);

	vertcount = 0;

	__asm__ volatile (
		".set		push\n"					// save assembler option
		".set		noreorder\n"			// suppress reordering
		"lv.s		S013,  0(%1)\n"			// S013 = light->intensity
		"lv.s		S010,  0(%2)\n"			// S010 = light->color[0]
		"lv.s		S011,  4(%2)\n"			// S011 = light->color[1]
		"lv.s		S012,  8(%2)\n"			// S012 = light->color[2]
		"lv.s		S100,  0(%3)\n"			// S100 = light->origin[0]
		"lv.s		S110,  4(%3)\n"			// S110 = light->origin[1]
		"lv.s		S120,  8(%3)\n"			// S120 = light->origin[2]
		"lv.s		S101,  0(%4)\n"			// S101 = vright[0]
		"lv.s		S111,  4(%4)\n"			// S111 = vright[1]
		"lv.s		S121,  8(%4)\n"			// S121 = vright[2]
		"lv.s		S102,  0(%5)\n"			// S102 = vup[0]
		"lv.s		S112,  4(%5)\n"			// S112 = vup[1]
		"lv.s		S122,  8(%5)\n"			// S122 = vup[2]
		"lv.s		S103,  0(%6)\n"			// S103 = vpn[0]
		"lv.s		S113,  4(%6)\n"			// S113 = vpn[1]
		"lv.s		S123,  8(%6)\n"			// S123 = vpn[2]
		"vfim.s		S000, 0.2\n"			// S000 = 0.2
		"vfim.s		S001, 0.35\n"			// S001 = 0.35
		"vmul.q		C000, C000[X,X,X,Y], C010\n" // C000 = [light->color[*] * 0.2, light->intensity * 0.35]
		"vmscl.t	E101, E101, S003\n"		// E101 = [vright, vup, vpn] *= light->intensity * 0.35
		"viim.s		S003, 255\n"			// S003 = 255
		"vsat0.t	C000, C000\n"			// C000 = saturation to [0:1](c000)
		"vscl.t		C000, C000, S003\n"		// C000 = C000 * 255
		"vf2iz.q	C000, C000, 23\n"		// C000 = (int)C000 * 2^23
		"vi2uc.q	S000, C000\n"			// S000 = ((S003>>23)<<24) | ((S002>>23)<<16) | ((S001>>23)<<8) | (S000>>23)
		"vsub.t		C001, R100, R103\n"		// C001 = light->origin[i] - vpn[i]*rad;
		"sv.q		C000, %0\n"				// out[vertcount] = C000
		"li			$t0, 0xff000000\n"		// t0 = 0xff000000 (set alpha color)
		"mtv		$t0, S000\n"			// S010 = $t0
		".set		pop\n"					// restore assembler option
		:	"=m"(out[vertcount++])
		:	"r"(&light->intensity), "r"(light->color), "r"(light->origin), "r"(vright), "r"(vup), "r"(vpn)
		:	"$t0", "memory"
	);

	// The compiler will apply loop unrolling optimization here
	for (i = 0; i < 17; i++)
	{
		__asm__ volatile (
			".set		push\n"					// save assembler option
			".set		noreorder\n"			// suppress reordering
			"lv.s		S010,  0(%1)\n"			// S010 = dlighttab[i][0]
			"lv.s		S011,  4(%1)\n"			// S011 = dlighttab[i][1]
			"vhdp.t		S001, C010, C100[Z, Y, X]\n"	//S001 = light->origin[0] + vright[0] * dlighttab[i][1] * rad + vup[0] * dlighttab[i][0] * rad;
			"vhdp.t		S002, C010, C110[Z, Y, X]\n"	//S002 = light->origin[1] + vright[1] * dlighttab[i][1] * rad + vup[1] * dlighttab[i][0] * rad;
			"vhdp.t		S003, C010, C120[Z, Y, X]\n"	//S003 = light->origin[2] + vright[2] * dlighttab[i][1] * rad + vup[2] * dlighttab[i][0] * rad;
			"sv.q		C000, %0\n"
			".set		pop\n"					// restore assembler option
			:	"=m"(out[vertcount++])
			:	"r"(dlighttab[i])
			:	"memory"
		);
	}
	sceGuDrawArray (GU_TRIANGLE_FAN, GU_COLOR_8888 | GU_VERTEX_32BITF, vertcount, 0, out);
}

/*
=============
R_RenderDlights
=============
*/
void R_RenderDlights (void)
{
	int		i;
	dlight_t	*l;

	if (!gl_flashblend->value)
		return;

	r_dlightframecount = r_framecount + 1;	// because the count hasn't
											//  advanced yet for this frame

	sceGuDepthMask (GU_TRUE);
	sceGuDisable (GU_TEXTURE_2D);
	sceGuShadeModel (GU_SMOOTH);
	sceGuEnable (GU_BLEND);
	sceGuBlendFunc (GU_ADD, GU_FIX, GU_FIX, GU_HBLEND_ONE, GU_HBLEND_ONE);

	l = r_newrefdef.dlights;
	for (i=0 ; i<r_newrefdef.num_dlights ; i++, l++)
		R_RenderDlight (l);

	sceGuColor (GU_HCOLOR_DEFAULT);
	sceGuDisable (GU_BLEND);
	sceGuEnable (GU_TEXTURE_2D);
	sceGuBlendFunc (GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
	sceGuDepthMask (GU_FALSE);
}


/*
=============================================================================

DYNAMIC LIGHTS

=============================================================================
*/

/*
=============
R_MarkLights
=============
*/
void R_MarkLights (dlight_t *light, int bit, mnode_t *node)
{
	cplane_t	*splitplane;
	float		dist;
	msurface_t	*surf;
	int			i;

	while(1)
	{
		if (node->contents != -1)
			return;

		splitplane = node->plane;
		dist = DotProduct (light->origin, splitplane->normal) - splitplane->dist;

		if (dist > light->intensity-DLIGHT_CUTOFF)
		{
			node = node->children[0];
			continue;
		}
		if (dist < -light->intensity+DLIGHT_CUTOFF)
		{
			node = node->children[1];
			continue;
		}

		// mark the polygons
		surf = r_worldmodel->surfaces + node->firstsurface;
		for (i=0 ; i<node->numsurfaces ; i++, surf++)
		{
			if (surf->dlightframe != r_dlightframecount)
			{
				surf->dlightbits = 0;
				surf->dlightframe = r_dlightframecount;
			}
			surf->dlightbits |= bit;
		}

		R_MarkLights (light, bit, node->children[0]);
		node = node->children[1];
		continue;
	}
}


/*
=============
R_PushDlights
=============
*/
void R_PushDlights (void)
{
	int		i;
	dlight_t	*l;

	if (gl_flashblend->value > 1)
		return;

	r_dlightframecount = r_framecount + 1;	// because the count hasn't
											//  advanced yet for this frame
	l = r_newrefdef.dlights;
	for (i=0 ; i<r_newrefdef.num_dlights ; i++, l++)
		R_MarkLights ( l, 1<<i, r_worldmodel->nodes );
}


/*
=============================================================================

LIGHT SAMPLING

=============================================================================
*/

vec3_t			pointcolor;
cplane_t		*lightplane;		// used as shadow plane
vec3_t			lightspot;

int RecursiveLightPoint (mnode_t *node, vec3_t start, vec3_t end)
{
	float		front, back, frac;
	int			side;
	cplane_t	*plane;
	vec3_t		mid;
	msurface_t	*surf;
	int			s, t, ds, dt;
	int			i;
	mtexinfo_t	*tex;
	byte		*lightmap;
	int			maps;
	int			r;

	if (node->contents != -1)
		return -1;		// didn't hit anything

// calculate mid point

// FIXME: optimize for axial
	plane = node->plane;
	front = DotProduct (start, plane->normal) - plane->dist;
	back = DotProduct (end, plane->normal) - plane->dist;
	side = front < 0;

	if ( (back < 0) == side)
		return RecursiveLightPoint (node->children[side], start, end);

	frac = front / (front-back);
	mid[0] = start[0] + (end[0] - start[0])*frac;
	mid[1] = start[1] + (end[1] - start[1])*frac;
	mid[2] = start[2] + (end[2] - start[2])*frac;

// go down front side
	r = RecursiveLightPoint (node->children[side], start, mid);
	if (r >= 0)
		return r;		// hit something

	if ( (back < 0) == side )
		return -1;		// didn't hit anuthing

// check for impact on this node
	VectorCopy (mid, lightspot);
	lightplane = plane;

	surf = r_worldmodel->surfaces + node->firstsurface;
	for (i=0 ; i<node->numsurfaces ; i++, surf++)
	{
		if (surf->flags&(SURF_DRAWTURB|SURF_DRAWSKY))
			continue;	// no lightmaps

		tex = surf->texinfo;

		s = DotProduct (mid, tex->vecs[0]) + tex->vecs[0][3];
		t = DotProduct (mid, tex->vecs[1]) + tex->vecs[1][3];;

		if (s < surf->texturemins[0] ||
		t < surf->texturemins[1])
			continue;

		ds = s - surf->texturemins[0];
		dt = t - surf->texturemins[1];

		if ( ds > surf->extents[0] || dt > surf->extents[1] )
			continue;

		if (!surf->samples)
			return 0;

		ds >>= 4;
		dt >>= 4;

		lightmap = surf->samples;
		VectorCopy (vec3_origin, pointcolor);
		if (lightmap)
		{
			vec3_t scale;

			lightmap += 3*(dt * ((surf->extents[0]>>4)+1) + ds);

			for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
					maps++)
			{
				for (i=0 ; i<3 ; i++)
					scale[i] = gl_modulate->value*r_newrefdef.lightstyles[surf->styles[maps]].rgb[i];

				pointcolor[0] += lightmap[0] * scale[0] * (1.0/255);
				pointcolor[1] += lightmap[1] * scale[1] * (1.0/255);
				pointcolor[2] += lightmap[2] * scale[2] * (1.0/255);
				lightmap += 3*((surf->extents[0]>>4)+1) *
						((surf->extents[1]>>4)+1);
			}
		}

		return 1;
	}

// go down back side
	return RecursiveLightPoint (node->children[!side], mid, end);
}

/*
===============
R_LightPoint
===============
*/
void R_LightPoint (vec3_t p, vec3_t color)
{
	vec3_t		end;
	float		r;
	int			lnum;
	dlight_t	*dl;
	float		light;
	vec3_t		dist;
	float		add;

	if (!r_worldmodel->lightdata)
	{
		color[0] = color[1] = color[2] = 1.0;
		return;
	}

	end[0] = p[0];
	end[1] = p[1];
	end[2] = p[2] - 2048;

	r = RecursiveLightPoint (r_worldmodel->nodes, p, end);

	if (r == -1)
	{
		VectorCopy (vec3_origin, color);
	}
	else
	{
		VectorCopy (pointcolor, color);
	}

	//
	// add dynamic lights
	//
	light = 0;
	dl = r_newrefdef.dlights;
	for (lnum=0 ; lnum<r_newrefdef.num_dlights ; lnum++, dl++)
	{
		VectorSubtract (currententity->origin,
						dl->origin,
						dist);
		add = dl->intensity - VectorLength(dist);
		add *= (1.0/256);
		if (add > 0)
		{
			VectorMA (color, add, dl->color, color);
		}
	}

	VectorScale (color, gl_modulate->value, color);
}


//===================================================================

static float s_blocklights[34*34*3];
/*
===============
R_AddDynamicLights
===============
*/
void R_AddDynamicLights (msurface_t *surf)
{
	int			lnum;
	int			sd, td;
	float		fdist, frad, fminlight;
	vec3_t		impact, local;
	int			s, t;
	int			i;
	int			smax, tmax;
	mtexinfo_t	*tex;
	dlight_t	*dl;
	float		*pfBL;
	float		fsacc, ftacc;

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;
	tex = surf->texinfo;

	for (lnum=0 ; lnum<r_newrefdef.num_dlights ; lnum++)
	{
		if ( !(surf->dlightbits & (1<<lnum) ) )
			continue;		// not lit by this light

		dl = &r_newrefdef.dlights[lnum];
		frad = dl->intensity;
		fdist = DotProduct (dl->origin, surf->plane->normal) -
				surf->plane->dist;
		frad -= fabs(fdist);
		// rad is now the highest intensity on the plane

		fminlight = DLIGHT_CUTOFF;	// FIXME: make configurable?
		if (frad < fminlight)
			continue;
		fminlight = frad - fminlight;

		for (i=0 ; i<3 ; i++)
		{
			impact[i] = dl->origin[i] -
					surf->plane->normal[i]*fdist;
		}

		local[0] = DotProduct (impact, tex->vecs[0]) + tex->vecs[0][3] - surf->texturemins[0];
		local[1] = DotProduct (impact, tex->vecs[1]) + tex->vecs[1][3] - surf->texturemins[1];

		pfBL = s_blocklights;
		for (t = 0, ftacc = 0 ; t<tmax ; t++, ftacc += 16)
		{
			td = local[1] - ftacc;
			if ( td < 0 )
				td = -td;

			for ( s=0, fsacc = 0 ; s<smax ; s++, fsacc += 16, pfBL += 3)
			{
				sd = Q_ftol( local[0] - fsacc );

				if ( sd < 0 )
					sd = -sd;

				if (sd > td)
					fdist = sd + (td>>1);
				else
					fdist = td + (sd>>1);

				if ( fdist < fminlight )
				{
					pfBL[0] += ( frad - fdist ) * dl->color[0];
					pfBL[1] += ( frad - fdist ) * dl->color[1];
					pfBL[2] += ( frad - fdist ) * dl->color[2];
				}
			}
		}
	}
}


/*
** R_SetCacheState
*/
void R_SetCacheState( msurface_t *surf )
{
	int maps;

	for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
		 maps++)
	{
		surf->cached_light[maps] = r_newrefdef.lightstyles[surf->styles[maps]].white;
	}
}

/*
===============
R_BuildLightMap

Combine and scale multiple lightmaps into the floating format in blocklights
===============
*/
void R_BuildLightMap (msurface_t *surf, byte *dest, int stride)
{
	int			smax, tmax;
	int			r, g, b, a, max;
	int			i, j, size;
	byte		*lightmap;
	float		scale[4];
	int			maps;
	int			nummaps;
	float		*bl;
	int			monolightmap;

	if ( surf->texinfo->flags & (SURF_SKY|SURF_TRANS33|SURF_TRANS66|SURF_WARP) )
		ri.Sys_Error (ERR_DROP, "R_BuildLightMap called for non-lit surface");

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;
	size = smax*tmax;
	if (size > (sizeof(s_blocklights)>>4) )
		ri.Sys_Error (ERR_DROP, "Bad s_blocklights size");

	// set to full bright if no light data
	if (!surf->samples)
	{
		for (i = 0; i < size * 3; i++)
			s_blocklights[i] = 255;
		goto store;
	}

	// count the # of maps
	for (nummaps = 0; nummaps < MAXLIGHTMAPS && surf->styles[nummaps] != 255; nummaps++);

	lightmap = surf->samples;

	// add all the lightmaps
	if ( nummaps == 1 )
	{
		bl = s_blocklights;

		for (i = 0 ; i < 3 ; i++)
			scale[i] = gl_modulate->value*r_newrefdef.lightstyles[surf->styles[0]].rgb[i];

		if ( scale[0] == 1.0F && scale[1] == 1.0F && scale[2] == 1.0F )
		{
			for (i = 0; i < size; i++, bl += 3, lightmap += 3)
			{
				bl[0] = lightmap[0];
				bl[1] = lightmap[1];
				bl[2] = lightmap[2];
			}
		}
		else
		{
			for (i = 0; i < size; i++, bl += 3, lightmap += 3)
			{
				bl[0] = lightmap[0] * scale[0];
				bl[1] = lightmap[1] * scale[1];
				bl[2] = lightmap[2] * scale[2];
			}
		}
	}
	else
	{
		memset( s_blocklights, 0, sizeof( s_blocklights[0] ) * size * 3 );

		for (maps = 0; maps < nummaps; maps++)
		{
			bl = s_blocklights;

			for (i=0 ; i<3 ; i++)
				scale[i] = gl_modulate->value*r_newrefdef.lightstyles[surf->styles[maps]].rgb[i];

			if ( scale[0] == 1.0F && scale[1] == 1.0F && scale[2] == 1.0F )
			{
				for (i = 0; i < size; i++, bl += 3, lightmap += 3)
				{
					bl[0] += lightmap[0];
					bl[1] += lightmap[1];
					bl[2] += lightmap[2];
				}
			}
			else
			{
				for (i = 0; i < size; i++, bl += 3, lightmap += 3)
				{
					bl[0] += lightmap[0] * scale[0];
					bl[1] += lightmap[1] * scale[1];
					bl[2] += lightmap[2] * scale[2];
				}
			}
		}
	}

// add all the dynamic lights
	if (surf->dlightframe == r_framecount)
		R_AddDynamicLights (surf);

// put into texture format
store:
	stride -= (smax<<2);
	bl = s_blocklights;

	monolightmap = gl_monolightmap->string[0];

	if ( monolightmap == '0' )
	{
		for (i = 0; i < tmax; i++, dest += stride)
		{
			for (j = 0; j < smax; j++)
			{

				r = Q_ftol( bl[0] );
				g = Q_ftol( bl[1] );
				b = Q_ftol( bl[2] );

				// catch negative lights
				r = (r < 0) ? 0 : r;
				g = (g < 0) ? 0 : g;
				b = (b < 0) ? 0 : b;

				/*
				** determine the brightest of the three color components
				*/
				max = Q_max3( r, g, b );

				/*
				** alpha is ONLY used for the mono lightmap case.  For this reason
				** we set it to the brightest of the color components so that
				** things don't get too dim.
				*/
				a = max;

				/*
				** rescale all the color components if the intensity of the greatest
				** channel exceeds 1.0
				*/
				if (max > 255)
				{
					float t = 255.0F / max;

					r = r*t;
					g = g*t;
					b = b*t;
					a = a*t;
				}

				dest[0] = gl_state.gammatable[r];
				dest[1] = gl_state.gammatable[g];
				dest[2] = gl_state.gammatable[b];
				dest[3] = a;

				bl += 3;
				dest += 4;
			}
		}
	}
	else
	{
		for (i = 0; i < tmax; i++, dest += stride)
		{
			for (j = 0; j < smax; j++)
			{

				r = Q_ftol( bl[0] );
				g = Q_ftol( bl[1] );
				b = Q_ftol( bl[2] );

				// catch negative lights
				r = (r < 0) ? 0 : r;
				g = (g < 0) ? 0 : g;
				b = (b < 0) ? 0 : b;

				/*
				** determine the brightest of the three color components
				*/
				max = Q_max3( r, g, b );

				/*
				** alpha is ONLY used for the mono lightmap case.  For this reason
				** we set it to the brightest of the color components so that
				** things don't get too dim.
				*/
				a = max;

				/*
				** rescale all the color components if the intensity of the greatest
				** channel exceeds 1.0
				*/
				if (max > 255)
				{
					float t = 255.0F / max;

					r = r*t;
					g = g*t;
					b = b*t;
					a = a*t;
				}

				/*
				** So if we are doing alpha lightmaps we need to set the R, G, and B
				** components to 0 and we need to set alpha to 1-alpha.
				*/
				switch ( monolightmap )
				{
				case 'L':
				case 'I':
					r = a;
					g = b = 0;
					break;
				case 'C':
					// try faking colored lighting
					a = 255 - ((r+g+b)/3);
					r *= a/255.0;
					g *= a/255.0;
					b *= a/255.0;
					break;
				case 'A':
				default:
					r = g = b = 0;
					a = 255 - a;
					break;
				}

				dest[0] = gl_state.gammatable[r];
				dest[1] = gl_state.gammatable[g];
				dest[2] = gl_state.gammatable[b];
				dest[3] = a;

				bl += 3;
				dest += 4;
			}
		}
	}
}

