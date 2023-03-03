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

#include <pspkernel.h>
#include <pspge.h>
#include <pspgu.h>
#include <stdio.h>
#include "gu_extension.h"

/* from guInternal.h */
typedef struct
{
	unsigned int* start;
	unsigned int* current;
	int parent_context;
} GuDisplayList;

extern GuDisplayList* gu_list;
extern int gu_curr_context;
extern int ge_list_executed[];

static unsigned int gu_list_size;

void extGuStart (int cid, void* list, int size)
{
	gu_list_size = size;
	sceGuStart (cid, list);
}

void *extGuBeginMemory (unsigned int *maxsize)
{
	if (maxsize != NULL)
		*maxsize = gu_list_size - ((unsigned int)gu_list->current - (unsigned int)gu_list->start) - 8;

	return gu_list->current + 2; // 8 bytes reserved for jump cmd
}

void extGuEndMemory (void *eaddr)
{
	unsigned int	*jumpaddr;
	unsigned int	size;

	size = (unsigned int)eaddr - (unsigned int)gu_list->current;

	if (size > 8)
	{
		// align to 4-byte boundary
		size = (size + 3) & ~3;

		jumpaddr = (unsigned int*)((unsigned int)gu_list->current + size);

		// jump cmd
		gu_list->current[0] = (16 << 24) | ((((unsigned int)jumpaddr) >> 8) & 0xf0000); // base 8
		gu_list->current[1] = (8 << 24) | (((unsigned int)jumpaddr) & 0xffffff); // jump 24

		// set current addr
		gu_list->current = jumpaddr;

		if (!gu_curr_context)
			sceGeListUpdateStallAddr (ge_list_executed[0], jumpaddr);
	}
}

/* Begin user packet */
void *extGuBeginPacket (unsigned int *maxsize)
{
	if (maxsize != NULL)
		*maxsize = gu_list_size - ((unsigned int)gu_list->current - (unsigned int)gu_list->start);

	return gu_list->current;
}

/* End user packet */
void extGuEndPacket (void *eaddr)
{
	gu_list->current = eaddr;

	if (!gu_curr_context)
		sceGeListUpdateStallAddr (ge_list_executed[0], eaddr);
}
