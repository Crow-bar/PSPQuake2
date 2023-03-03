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

typedef struct
{
	union
	{
		float xyz[3];

		struct
		{
			float x;
			float y;
			float z;
		};
	};
} gu_vert_fv_t;

typedef struct
{
	union
	{
		float uv[2];

		struct
		{
			float u;
			float v;
		};
	};

	union
	{
		float xyz[3];

		struct
		{
			float x;
			float y;
			float z;
		};
	};
} gu_vert_ftv_t;

typedef struct
{
	union
	{
		unsigned int c;
		unsigned char rgba[4];

		struct
		{
			unsigned char r;
			unsigned char g;
			unsigned char b;
			unsigned char a;
		};
	};

	union
	{
		float xyz[3];

		struct
		{
			float x;
			float y;
			float z;
		};
	};
} gu_vert_fcv_t;

typedef struct
{
	union
	{
		float uv[2];

		struct
		{
			float u;
			float v;
		};
	};

	union
	{
		unsigned int c;
		unsigned char rgba[4];

		struct
		{
			unsigned char r;
			unsigned char g;
			unsigned char b;
			unsigned char a;
		};
	};

	union
	{
		float xyz[3];

		struct
		{
			float x;
			float y;
			float z;
		};
	};
} gu_vert_ftcv_t;

typedef struct
{
	union
	{
		float uv[2];

		struct
		{
			float u;
			float v;
		};
	};

	union
	{
		unsigned int c;
		unsigned char rgba[4];

		struct
		{
			unsigned char r;
			unsigned char g;
			unsigned char b;
			unsigned char a;
		};
	};

	union
	{
		float nxyz[3];

		struct
		{
			float nx;
			float ny;
			float nz;
		};
	};

	union
	{
		float xyz[3];

		struct
		{
			float x;
			float y;
			float z;
		};
	};
} gu_vert_ftcnv_t;

typedef struct
{
	union
	{
		short xyz[3];

		struct
		{
			short x;
			short y;
			short z;
		};
	};
} gu_vert_hv_t;

typedef struct
{
	union
	{
		short uv[2];

		struct
		{
			short u;
			short v;
		};
	};

	union
	{
		short xyz[3];

		struct
		{
			short x;
			short y;
			short z;
		};
	};
} gu_vert_htv_t;

typedef struct
{
	union
	{
		unsigned int c;
		unsigned char rgba[4];

		struct
		{
			unsigned char r;
			unsigned char g;
			unsigned char b;
			unsigned char a;
		};
	};

	union
	{
		short xyz[3];

		struct
		{
			short x;
			short y;
			short z;
		};
	};
} gu_vert_hcv_t;

typedef struct
{
	union
	{
		short uv[2];

		struct
		{
			short u;
			short v;
		};
	};

	union
	{
		unsigned int c;
		unsigned char rgba[4];

		struct
		{
			unsigned char r;
			unsigned char g;
			unsigned char b;
			unsigned char a;
		};
	};

	union
	{
		short xyz[3];

		struct
		{
			short x;
			short y;
			short z;
		};
	};
} gu_vert_htcv_t;
