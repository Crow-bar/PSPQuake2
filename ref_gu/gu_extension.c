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

static size_t gu_list_size;

#define GE_CMD_ALIGNMENT		4
#define GE_CMD_ALIGNMENT_MASK	(GE_CMD_ALIGNMENT - 1)

#define GE_SET_CMD(cmd)			(cmd << 24)
#define GE_SET_ARG(arg)			(arg & 0x00ffffff)
#define GE_SET_CMDWA(cmd, arg)	(GE_SET_CMD(cmd) | GE_SET_ARG(arg))

void extGuStart (int cid, void* list, size_t size)
{
	gu_list_size = size;
	sceGuStart (cid, list);
}

void *extGuGetAlignedMemory (size_t align, size_t size)
{
	unsigned int *current_ptr;
	unsigned int *start_ptr;
	unsigned int *dest_ptr;

	current_ptr = gu_list->current;
	start_ptr   = (unsigned int *)((((uintptr_t)current_ptr + 8 - 1) | (align - 1)) + 1);
	dest_ptr    = (unsigned int *)((uintptr_t)start_ptr + ((size + GE_CMD_ALIGNMENT_MASK) & ~GE_CMD_ALIGNMENT_MASK));

	// jump cmd
	*(current_ptr++) = GE_SET_CMD(16) | ((((uintptr_t)dest_ptr) >> 8) & 0xf0000); // base 8
	*(current_ptr++) = GE_SET_CMD(8)  | (((uintptr_t)dest_ptr) & 0xffffff); // jump 24

	// set current addr
	gu_list->current = dest_ptr;
	if (!gu_curr_context)
		sceGeListUpdateStallAddr (ge_list_executed[0], dest_ptr);

	return start_ptr;
}

void *extGuBeginMemory (size_t *maxsize)
{
	uintptr_t start_addr;

	// 8 bytes reserved for jump cmd
	start_addr = ((uintptr_t)gu_list->current + 8);

	if (maxsize != NULL)
		*maxsize = gu_list_size - (start_addr - (uintptr_t)gu_list->start);

	return (void *)start_addr; 
}

void extGuEndMemory (void *end_ptr)
{
	size_t       size;
	unsigned int *current_ptr;
	unsigned int *dest_ptr;

	size = (uintptr_t)end_ptr - (uintptr_t)gu_list->current;
	if (size > 8)
	{
		current_ptr = gu_list->current;
		dest_ptr    = (unsigned int *)((uintptr_t)current_ptr + ((size + GE_CMD_ALIGNMENT_MASK) & ~GE_CMD_ALIGNMENT_MASK));

		// jump cmd
		*(current_ptr++) = GE_SET_CMD(16) | ((((uintptr_t)dest_ptr) >> 8) & 0xf0000); // base 8
		*(current_ptr++) = GE_SET_CMD(8)  | (((uintptr_t)dest_ptr) & 0xffffff); // jump 24

		// set current addr
		gu_list->current = dest_ptr;
		if (!gu_curr_context)
			sceGeListUpdateStallAddr (ge_list_executed[0], dest_ptr);
	}
}

/* Begin user packet */
void *extGuBeginPacket (size_t *maxsize)
{
	if (maxsize != NULL)
		*maxsize = gu_list_size - ((uintptr_t)gu_list->current - (uintptr_t)gu_list->start);
	return gu_list->current;
}

/* End user packet */
void extGuEndPacket (void *eaddr)
{
	gu_list->current = eaddr;
}
