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

#ifndef GU_HELPER_H
#define GU_HELPER_H

// clamping color between 0.0 - 1.0
static inline unsigned int GU_COLOR_CLP(float r, float g, float b, float a)
{
	unsigned int	result;
	__asm__ (
		".set			push\n"					// save assembler option
		".set			noreorder\n"			// suppress reordering
		"mfc1			$8, %1\n"				// FPU->CPU
		"mtv			$8, S000\n"				// CPU->VFPU S000 = r
		"mfc1			$8, %2\n"				// FPU->CPU
		"mtv			$8, S001\n"				// CPU->VFPU S001 = g
		"mfc1			$8, %3\n"				// FPU->CPU
		"mtv			$8, S002\n"				// CPU->VFPU S002 = b
		"mfc1			$8, %4\n"				// FPU->CPU
		"mtv			$8, S003\n"				// CPU->VFPU S003 = a
		"vsat0.q		C000,  c000\n"			// C000 = saturation to [0:1](C000)
		"viim.s			S010, 255\n"			// S010 = 255.0f
		"vscl.q			C000, C000, S010\n"		// C000 = C000 * 255.0f
		"vf2iz.q		C000, C000, 23\n"		// C000 = C000 * 2^23
		"vi2uc.q		S000, C000\n"			// S000 = ((S003>>23)<<24) | ((S002>>23)<<16) | ((S001>>23)<<8) | (S000>>23)
		"mfv			%0,   S000\n"			// result = S000
		".set			pop\n"					// restore assembler option
		:	"=r"(result)
		:	"f"(r),
			"f"(g),
			"f"(b),
			"f"(a)
		:	"$8"
	);
	return result;
}

// clamping color between 0 - 255
static inline unsigned int GU_RGBA_CLP(unsigned int r, unsigned int g, unsigned int b, unsigned int a)
{
	unsigned int	result;
	__asm__ (
		".set			push\n"					// save assembler option
		".set			noreorder\n"			// suppress reordering
		"mtv			%1, S000\n"				// CPU->VFPU S000 = r
		"mtv			%2, S001\n"				// CPU->VFPU S001 = g
		"mtv			%3, S002\n"				// CPU->VFPU S002 = b
		"mtv			%4, S003\n"				// CPU->VFPU S003 = a
		"vi2f.q			C000, C000, 0\n"		// C000 = (float)C000
		"viim.s			S010, 255\n"			// S010 = 255.0f
		"vmin.q			C000, C000, C010[X, X, X, X]\n"	// C000 = min(C000, 255)
		"vf2iz.q		C000, C000, 23\n"		// C000 = C000 * 2^23
		"vi2uc.q		S000, C000\n"			// S000 = ((S003>>23)<<24) | ((S002>>23)<<16) | ((S001>>23)<<8) | (S000>>23)
		"mfv			%0,   S000\n"			// result = S000
		".set			pop\n"					// restore assembler option
		:	"=r"(result)
		:	"r"(r),
			"r"(g),
			"r"(b),
			"r"(a)
	);
	return result;
}

// color functions wrapping
#define GU_HCOLOR_4F( r, g, b, a )		GU_COLOR( ( r ), ( g ), ( b ), ( a ) )
#define GU_HCOLOR_4FV( v )				GU_COLOR( ( v )[0], ( v )[1], ( v )[2], ( v )[3] )
#define GU_HCOLOR_4UB( r, g, b, a )		GU_RGBA( ( r ), ( g ), ( b ), ( a ) )
#define GU_HCOLOR_4UBV( v )				GU_RGBA( ( v )[0], ( v )[1], ( v )[2], ( v )[3] )
#define GU_HCOLOR_3F( r, g, b )			GU_COLOR( ( r ), ( g ), ( b ), 1.0f )
#define GU_HCOLOR_3FV( v )				GU_COLOR( ( v )[0], ( v )[1], ( v )[2], 1.0f )
#define GU_HCOLOR_3UB( r, g, b )		GU_RGBA( ( r ), ( g ), ( b ), 255 )
#define GU_HCOLOR_3UBV( v )				GU_RGBA( ( v )[0], ( v )[1], ( v )[2], 255 )

#define GU_HCOLOR_C4F( r, g, b, a )		GU_COLOR_CLP( ( r ), ( g ), ( b ), ( a ) )
#define GU_HCOLOR_C4FV( v )				GU_COLOR_CLP( ( v )[0], ( v )[1], ( v )[2], ( v )[3] )
#define GU_HCOLOR_C4UB( r, g, b, a )	GU_RGBA_CLP( ( r ), ( g ), ( b ), ( a ) )
#define GU_HCOLOR_C4UBV( v )			GU_RGBA_CLP( ( v )[0], ( v )[1], ( v )[2], ( v )[3] )
#define GU_HCOLOR_C3F( r, g, b )		GU_COLOR_CLP( ( r ), ( g ), ( b ), 1.0f )
#define GU_HCOLOR_C3FV( v )				GU_COLOR_CLP( ( v )[0], ( v )[1], ( v )[2], 1.0f )
#define GU_HCOLOR_C3UB( r, g, b )		GU_RGBA_CLP( ( r ), ( g ), ( b ), 255 )
#define GU_HCOLOR_C3UBV( v )			GU_RGBA_CLP( ( v )[0], ( v )[1], ( v )[2], 255 )

#define GU_HCOLOR_DEFAULT				0xffffffff

// blend function wrapping
#define GU_HBLEND_ONE					0xffffffff
#define GU_HBLEND_ZERO					0x00000000

#endif // GU_HELPER_H
