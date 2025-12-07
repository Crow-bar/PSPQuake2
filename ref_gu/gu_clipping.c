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

Based on clipping system from PSP Quake by Peter Mackay and Chris Swindle.

*/
/*
	VFPU REGS:

	M700 - current frustum
	M600 - world frustum
	C010 - current plane ( GU_Clip2Plane )
*/

#include "gu_local.h"

#define UNNORMALIZED_PLANES		1

#define MAX_CLIPPED_VERTICES	32

#define GU_REG_FR_SAVE		M600
#define GU_REG_FR_CURR		M700
#define GU_REG_CP_BOTTOM	C700
#define GU_REG_CP_LEFT		C710
#define GU_REG_CP_RIGHT		C720
#define GU_REG_CP_TOP		C730

// Cache
static ScePspFMatrix4	projection_view_matrix;

// The temporary working buffers.
static gu_vert_ftv_t	work_buffer[2][MAX_CLIPPED_VERTICES] __attribute__((aligned(16)));

/*
=================
GU_ClipGetFrustum
=================
*/
static inline void GU_ClipGetAndStoreFrustum (const ScePspFMatrix4 *matrix)
{
	__asm__ (
		".set		push\n"					// save assembler option
		".set		noreorder\n"			// suppress reordering
		"lv.q		C100,  0(%0)\n"			// C100 = matrix->x
		"lv.q		C110, 16(%0)\n"			// C110 = matrix->y
		"lv.q		C120, 32(%0)\n"			// C120 = matrix->z
		"lv.q		C130, 48(%0)\n"			// C130 = matrix->w
#if UNNORMALIZED_PLANES
		// Using unnormalized planes for better precision and to prevent clipping holes
		"vadd.q		"M_ATOS(GU_REG_CP_BOTTOM)", R103, R101\n"	// C000 = R103 + R101                       ( BOTTOM )
		"vadd.q		"M_ATOS(GU_REG_CP_LEFT)",   R103, R100\n"	// C010 = R103 + R100                       ( LEFT )
		"vsub.q		"M_ATOS(GU_REG_CP_RIGHT)",  R103, R100\n"	// C020 = R103 - R100                       ( RIGHT )
		"vsub.q		"M_ATOS(GU_REG_CP_TOP)",    R103, R101\n"	// C030 = R103 - R101                       ( TOP )
#else
		"vzero.q	C210\n"					// set zero vector
		"vadd.q		C000, R103, R101\n"		// C000 = R103 + R101                       ( BOTTOM )
		"vadd.q		C010, R103, R100\n"		// C010 = R103 + R100                       ( LEFT )
		"vsub.q		C020, R103, R100\n"		// C020 = R103 - R100                       ( RIGHT )
		"vsub.q		C030, R103, R101\n"		// C030 = R103 - R101                       ( TOP )
		"vdot.t		S200, C000, C000\n"		// S110 = S100*S100 + S101*S101 + S102*S102 ( BOTTOM )
		"vdot.t		S201, C010, C010\n"		// S110 = S100*S100 + S101*S101 + S102*S102 ( LEFT )
		"vdot.t		S202, C020, C020\n"		// S110 = S100*S100 + S101*S101 + S102*S102 ( RIGHT )
		"vdot.t		S203, C030, C030\n"		// S110 = S100*S100 + S101*S101 + S102*S102 ( TOP )
		"vcmp.q		EZ,   C200\n"			// CC[*] = ( C200 == 0.0f )
		"vrsq.q		C200, C200\n"			// C200 = 1.0 / sqrt( C200 )
		"vcmovt.q	C200, C210, 6\n"		// if ( CC[*] ) C200 = C210
		"vscl.q		"M_ATOS(GU_REG_CP_BOTTOM)", C000, S200\n"		// GU_REG_CP_BOTTOM = C000 * S200
		"vscl.q		"M_ATOS(GU_REG_CP_LEFT)",   C010, S201\n"		// GU_REG_CP_LEFT   = C010 * S201
		"vscl.q		"M_ATOS(GU_REG_CP_RIGHT)",  C020, S202\n"		// GU_REG_CP_RIGHT  = C020 * S202
		"vscl.q		"M_ATOS(GU_REG_CP_TOP)",    C030, S203\n"		// GU_REG_CP_TOP    = C030 * S203
#endif
		".set		pop\n"					// Restore assembler option
		::	"r"(matrix)
	);
}

/*
=================
GU_ClipBeginFrame

Calculate the clipping frustum for static objects
=================
*/
void GU_ClipSetWorldFrustum (void)
{
	ScePspFMatrix4	proj, view;

	// Get the projection matrix.
	sceGumMatrixMode (GU_PROJECTION);
	sceGumStoreMatrix (&proj);

	// Get the view matrix.
	sceGumMatrixMode (GU_VIEW);
	sceGumStoreMatrix (&view);

	// Restore matrix mode.
	sceGumMatrixMode (GU_MODEL);

	// Combine the two matrices (multiply projection by view).
	gumMultMatrix (&projection_view_matrix, &proj, &view);

	// Calculate and cache the clipping frustum.
	GU_ClipGetAndStoreFrustum (&projection_view_matrix);

	// Save the clipping frustum.
	__asm__ volatile(
		".set		push\n"					// save assembler option
		".set		noreorder\n"			// suppress reordering
		"vmmov.q	"M_ATOS(GU_REG_FR_SAVE)"," M_ATOS(GU_REG_FR_CURR)"\n"	// Save frustum
		".set		pop\n"					// Restore assembler option
	);
}

/*
=================
GU_ClipRestoreWorldFrustum

Restore the clipping frustum
=================
*/
void GU_ClipRestoreWorldFrustum (void)
{
	// Restore the clipping frustum.
	__asm__ volatile(
		".set		push\n"					// save assembler option
		".set		noreorder\n"			// suppress reordering
		"vmmov.q	"M_ATOS(GU_REG_FR_CURR)", "M_ATOS(GU_REG_FR_SAVE)"\n"	// Restore frustum
		".set		pop\n"					// Restore assembler option
	);
}

/*
=================
GU_ClipSetModelFrustum

Calculate the clipping frustum for dynamic objects
=================
*/
void GU_ClipSetModelFrustum (void)
{
	ScePspFMatrix4	model_matrix;
	ScePspFMatrix4	projection_view_model_matrix;

	// Get matrix.
	sceGumStoreMatrix (&model_matrix);

	// Combine the matrices (multiply projection-view by model).
	gumMultMatrix (&projection_view_model_matrix, &projection_view_matrix, &model_matrix);

	// Calculate and cache the clipping frustum.
	GU_ClipGetAndStoreFrustum (&projection_view_model_matrix);
}

/*
=================
GU_ClipIsRequired

Is clipping required?
=================
*/
int GU_ClipIsRequired (gu_vert_ftv_t* uv, int uvc)
{
	int				result = 1;
	gu_vert_ftv_t	*uv_end = uv + (uvc - 1);

	__asm__ (
		".set		push\n"					// save assembler option
		".set		noreorder\n"			// suppress reordering
		"vzero.q	C000\n"					// C000 = [0.0f, 0.0f, 0.0f. 0.0f]
	"0:\n"									// loop
		"lv.s		S010,  8(%1)\n"			// S010 = v[i].xyz[0]
		"lv.s		S011,  12(%1)\n"		// S011 = v[i].xyz[1]
		"lv.s		S012,  16(%1)\n"		// S012 = v[i].xyz[2]
		"vhtfm4.q	C020, "M_ATOS(GU_REG_FR_CURR)", C010\n"		// C020 = frustrum * v[i].xyz
		"vcmp.q		LT,   C020, C000\n"		// S020 < 0.0f || S021 < 0.0f || S022 < 0.0f || S023 < 0.0f
		"bvt		4,    1f\n"				// if ( CC[4] == 1 ) jump to exit
		"nop\n"								// 												( delay slot )
		"bne		%1,   %2,   0b\n"		// if ( $10 != $8 ) jump to loop
		"addiu		%1,   %1,   %3\n"		// $8 = $8 + sizeof( gu_vert_ftv_t )			( delay slot )
		"move		%0,   $0\n"				// res = 0
	"1:\n"									// exit
		".set		pop\n"					// Restore assembler option
		:	"=r"(result), "+r"(uv)
		:	"r"(uv_end),
			"n"(sizeof(gu_vert_ftv_t))
		:	"$8"
	);
	return result;
}

/*
=================
GU_ClipPolygon

Clips a polygon against a plane.
=================
*/
#define GU_ClipPolygonSetPlaneReg(reg)		\
	__asm__ volatile(						\
		".set		push\n"					\
		".set		noreorder\n"			\
		"vmov.q		C010, "M_TOS(reg)"\n"	\
		".set		pop\n"					\
	)

static inline void GU_ClipPolygon (gu_vert_ftv_t *uv, int uvc, gu_vert_ftv_t *cv, int *cvc)
{
	gu_vert_ftv_t	*uv_end = uv + (uvc - 1);
	gu_vert_ftv_t	*cv_start = cv;

	__asm__ (
		".set		push\n"					// save assembler option
		".set		noreorder\n"			// suppress reordering
		"vzero.q	C000\n"					// set zero vector
		"lv.s		S200,  8(%2)\n"			// Load vertex P XYZ(4b) X into register
		"lv.s		S201, 12(%2)\n"			// Load vertex P XYZ(4b) Y into register
		"lv.s		S202, 16(%2)\n"			// Load vertex P XYZ(4b) Z into register
		"lv.s		S210,  0(%2)\n"			// Load vertex P TEX(4b) U into register
		"lv.s		S211,  4(%2)\n"			// Load vertex P TEX(4b) V into register
		"vhdp.q		S212, C200, C010\n"		// distance P  -> dP
	"0:\n"
		"lv.s		S220,  8(%1)\n"			// Load vertex S XYZ(4b) X into register
		"lv.s		S221, 12(%1)\n"			// Load vertex S XYZ(4b) Y into register
		"lv.s		S222, 16(%1)\n"			// Load vertex S XYZ(4b) Z into register
		"lv.s		S230,  0(%1)\n"			// Load vertex S TEX(4b) U into register
		"lv.s		S231,  4(%1)\n"			// Load vertex S TEX(4b) V into register
		"vhdp.q		S232, C220, C010\n"		// distance S -> dS
		"vcmp.s		GE, S212, S000\n"		// if (dP >= 0)
		"bvf		0, 1f\n"				// goto 1:
		"nop\n"
		"sv.s		S210,  0(%0)\n"			// cv->uv[0]  = S210 U
		"sv.s		S211,  4(%0)\n"			// cv->uv[1]  = S211 V
		"sv.s		S200,  8(%0)\n"			// cv->xyz[0] = S200 X
		"sv.s		S201, 12(%0)\n"			// cv->xyz[1] = S201 Y
		"sv.s		S202, 16(%0)\n"			// cv->xyz[2] = S202 Z
		"addiu		%0, %0, %3\n"			// cv + sizeof(gu_vert_ftv_t)
	"1:\n"
		"vmul.s		S020, S232, S212\n"		// (dS * dP)
		"vcmp.s		LT, S020, S000\n"		// if (dS * dP < 0)
		"bvf		0, 2f\n"				// goto 2:
		"vsub.s		S021, S232, S212\n"		// (dS - dP)
		"vrcp.s		S021, S021\n"
		"vmul.s		S021, S021, S232\n"		// R = dS / ( dS - dP )
#if 0
		"vsub.t		C200, C220, C200\n"		// (S - P) XYZ
		"vsub.p		C210, C230, C210\n"		// (S - P) UV
		"vscl.t		C200, C200, S021\n"		// ((S - P) * R) XYZ
		"vscl.p		C210, C210, S021\n"		// ((S - P) * R) UV
		"vsub.t		C200, C220, C200\n"		// (S - (S - P) * R) XYZ
		"vsub.p		C210, C230, C210\n"		// (S - (S - P) * R) UV
#else
		"vsub.t		C200, C200, C220\n"		// (P - S) XYZ
		"vsub.p		C210, C210, C230\n"		// (P - S) UV
		"vscl.t		C200, C200, S021\n"		// ((P - S) * R) XYZ
		"vscl.p		C210, C210, S021\n"		// ((P - S) * R) UV
		"vadd.t		C200, C220, C200\n"		// (S + (P - S) * R) XYZ
		"vadd.p		C210, C230, C210\n"		// (S + (P - S) * R) UV
#endif
		"sv.s		S210,  0(%0)\n"			// cv->uv[0]  = S210 U
		"sv.s		S211,  4(%0)\n"			// cv->uv[1]  = S211 V
		"sv.s		S200,  8(%0)\n"			// cv->xyz[0] = S200 X
		"sv.s		S201, 12(%0)\n"			// cv->xyz[1] = S201 Y
		"sv.s		S202, 16(%0)\n"			// cv->xyz[2] = S202 Z
		"addiu		%0, %0, %3\n"			// cv + sizeof(gu_vert_ftv_t)
	"2:\n"
		"vmov.t		C200, C220\n"			// P = S XYZ
		"vmov.t		C210, C230\n"			// P = S UV and dS
		"bne		%1, %2, 0b\n"			// if (uv != uv_end) goto 0:
		"addiu		%1, %1, %3\n"			// uv + sizeof(gu_vert_ftv_t)					( delay slot )
		".set		pop\n"					// suppress reordering
		:	"+r"(cv), "+r"(uv)
		:	"r"(uv_end),
			"n"(sizeof(gu_vert_ftv_t))
		:	"memory"
	);
	*cvc = (cv - cv_start);
}

/*
=================
GU_Clip

Clips a polygon against the frustum
=================
*/
void GU_Clip (gu_vert_ftv_t *uv, int uvc, gu_vert_ftv_t **cv, int* cvc)
{
	int	vc;

	if (!uvc)	// no vertices to clip?
		Sys_Error ("GU_Clip: calling clip with zero vertices!");

	vc = uvc;
	*cvc = 0;

	GU_ClipPolygonSetPlaneReg (GU_REG_CP_BOTTOM);
	GU_ClipPolygon (uv, vc, work_buffer[0], &vc);
	if (!vc) return;

	GU_ClipPolygonSetPlaneReg (GU_REG_CP_LEFT);
	GU_ClipPolygon (work_buffer[0], vc, work_buffer[1], &vc);
	if (!vc) return;

	GU_ClipPolygonSetPlaneReg (GU_REG_CP_RIGHT);
	GU_ClipPolygon (work_buffer[1], vc, work_buffer[0], &vc);
	if (!vc) return;

	GU_ClipPolygonSetPlaneReg (GU_REG_CP_TOP);
	*cv = extGuBeginMemory (NULL); // uncached
	GU_ClipPolygon (work_buffer[0], vc, *cv, cvc);
	if (!(*cvc)) return;

	extGuEndMemory (*cv + *cvc);
}
