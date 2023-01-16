/*
Copyright (C) 1997-2001 Id Software, Inc.
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
// net_null.c -- for loopback-only networking
#include "../qcommon/qcommon.h"

netadr_t	net_local_adr;

#define	MAX_LOOPBACK	4

typedef struct
{
	byte	data[MAX_MSGLEN];
	int		datalen;
} loopmsg_t;
typedef struct
{
	loopmsg_t	msgs[MAX_LOOPBACK];
	int			get, send;
} loopback_t;
loopback_t	loopbacks[2];

//=============================================================================

qboolean NET_CompareAdr (netadr_t a, netadr_t b)
{
	return true;
}
/*
===================
NET_CompareBaseAdr
Compares without the port
===================
*/
qboolean NET_CompareBaseAdr (netadr_t a, netadr_t b)
{
	return true;
}

char *NET_AdrToString (netadr_t a)
{
	static	char	s[64];
	Com_sprintf (s, sizeof(s), "%i.%i.%i.%i:%i", a.ip[0], a.ip[1], a.ip[2], a.ip[3], a.port);
	return s;
}
/*
=============
NET_StringToAdr
localhost
idnewt
idnewt:28000
192.246.40.70
192.246.40.70:28000
=============
*/
qboolean NET_StringToAdr (char *s, netadr_t *a)
{
	memset (a, 0, sizeof(*a));
	a->type = NA_LOOPBACK;
	return true;
}
qboolean NET_IsLocalAddress (netadr_t adr)
{
	return adr.type == NA_LOOPBACK;
}
/*
=============================================================================
LOOPBACK BUFFERS FOR LOCAL PLAYER
=============================================================================
*/
qboolean NET_GetPacket (netsrc_t sock, netadr_t *net_from, sizebuf_t *net_message)
{
	int		i;
	loopback_t	*loop;
	loop = &loopbacks[sock];
	if (loop->send - loop->get > MAX_LOOPBACK)
		loop->get = loop->send - MAX_LOOPBACK;
	if (loop->get >= loop->send)
		return false;
	i = loop->get & (MAX_LOOPBACK-1);
	loop->get++;
	memcpy (net_message->data, loop->msgs[i].data, loop->msgs[i].datalen);
	net_message->cursize = loop->msgs[i].datalen;
	*net_from = net_local_adr;
	return true;
}

void NET_SendPacket (netsrc_t sock, int length, void *data, netadr_t to)
{
	int		i;
	loopback_t	*loop;
	loop = &loopbacks[sock^1];
	i = loop->send & (MAX_LOOPBACK-1);
	loop->send++;
	memcpy (loop->msgs[i].data, data, length);
	loop->msgs[i].datalen = length;
}

/*
====================
NET_Init
====================
*/
void NET_Init (void)
{

}

/*
====================
NET_Config
A single player game will only use the loopback code
====================
*/
void NET_Config (qboolean multiplayer)
{

}

// sleeps msec or until net socket is ready
void NET_Sleep(int msec)
{

}
