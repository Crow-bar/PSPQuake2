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
// net_psp.c

#include "../qcommon/qcommon.h"

#include <ctype.h>
#include <pspsdk.h>
#include <psputility.h>
#include <pspwlan.h>
#include <pspnet.h>
#include <pspnet_inet.h>
#include <pspnet_resolver.h>
#include <pspnet_apctl.h>
#include <pspnet_adhoc.h>
#include <pspnet_adhocctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

#define NET_ADHOC_PRODUCT			"ULUS00550"
#define NET_POOLSIZE				(128 * 1024)

static cvar_t	*net_accesspoint;
static cvar_t	*net_mode;
static cvar_t	*net_active;

static netadr_t	net_local_adr;

// Handler
//=============================================================================
#define NET_EVENT_ERROR				0x00000001

typedef struct
{
	int			event;
	int			error;
	int			id;
} net_handler_t;

// Loopback
//=============================================================================
#define	NET_LOOPBACK				0x7f000001
#define	NET_MAX_LOOPBACK			4

typedef struct
{
	byte		data[MAX_MSGLEN];
	int			datalen;
} loopmsg_t;

typedef struct
{
	loopmsg_t	msgs[NET_MAX_LOOPBACK];
	int			get, send;
} loopback_t;

static loopback_t	loopbacks[2];

// Infrastructure
//=============================================================================
#define NET_INET_EVENT_ERROR		NET_EVENT_ERROR
#define NET_INET_EVENT_CONNECT		0x00000002
#define NET_INET_EVENT_DISCONNECT	0x00000004

static char *apctl_states[] =
{
	"Disconnected",
	"Scaning",
	"Joining",
	"IPObtaining",
	"IPObtained",
	"Authenticating",
	"KeyInfoExchanging",
};

static net_handler_t inet_handler_data;

static int		ip_sockets[2];

static qboolean	NET_InetInit (void);
static void		NET_ApctlHandler (int oldstate, int newstate, int event, int code, void *arg);
static qboolean	NET_ApctlConnect(int id);
static qboolean	NET_ApctlDisconnect (void);
static int		NET_InetSocket (char *net_interface, int port);
static void		NET_InetShutdown (void);

// Adhoc
//=============================================================================
#define NET_ADHOC_EVENT_ERROR		NET_EVENT_ERROR
#define NET_ADHOC_EVENT_CONNECT		0x00000002
#define NET_ADHOC_EVENT_DISCONNECT	0x00000004
#define NET_ADHOC_EVENT_SCAN		0x00000008
#define NET_ADHOC_EVENT_GAMEMODE	0x00000010

static char *adhoc_event_str[] =
{
	"Error",
	"Connect",
	"Disconnect",
	"Scan",
	"Gamemode",
};

static net_handler_t adhoc_handler_data;

static int		pdp_sockets[2];
static byte		pdp_broadcastaddr[6] = {0xff,0xff,0xff,0xff,0xff,0xff};

static qboolean	NET_AdhocInit (void);
static void		NET_AdhocctlHandler (int event, int code, void *arg);
static qboolean NET_AdhocctlConnect (char *group);
static qboolean	NET_AdhocctlDisconnect (void);
static int		NET_AdhocSocket (int port);
static void		NET_AdhocShutdown (void);


static char		*NET_ErrorString (void);

//=============================================================================

/*
===================
NET_WaitEvent
===================
*/
static int NET_WaitEvent (net_handler_t *handler, int event, int tms)
{
	int	i;

	for (i = 0; (tms == 0) || (i < tms); i++)
	{
		if (handler->event & event)
		{
			handler->event &= ~event;
			return 0;
		}
		if (handler->event & NET_EVENT_ERROR)
		{
			handler->event &= ~NET_EVENT_ERROR;
			return handler->error;
		}
		sceKernelDelayThread(1000 * 1000); // 1 sec
	}

	return -1; // timeout
}


/*
===================
NET_NetadrToSockadr
===================
*/
static void NET_NetadrToSockadr (netadr_t *a, struct sockaddr_in *s)
{
	memset (s, 0, sizeof(*s));

	if (a->type == NA_BROADCAST)
	{
		s->sin_family = AF_INET;

		s->sin_port = a->port;
		*(int *)&s->sin_addr = -1;
	}
	else if (a->type == NA_IP)
	{
		s->sin_family = AF_INET;

		*(int *)&s->sin_addr = *(int *)&a->ip;
		s->sin_port = a->port;
	}
}


/*
===================
NET_SockadrToNetadr
===================
*/
static void NET_SockadrToNetadr (struct sockaddr_in *s, netadr_t *a)
{
	*(int *)&a->ip = *(int *)&s->sin_addr;
	a->port = s->sin_port;
	a->type = NA_IP;
}


/*
===================
NET_CompareAdr

Compares with the port
===================
*/
qboolean NET_CompareAdr (netadr_t a, netadr_t b)
{
	if (a.type == NA_PDP)
	{
		if (!memcmp(a.mac, b.mac, 6) && a.port == b.port)
			return true;
	}
	else
	{
		if (a.ip[0] == b.ip[0] && a.ip[1] == b.ip[1] && a.ip[2] == b.ip[2] && a.ip[3] == b.ip[3] && a.port == b.port)
			return true;
	}

	return false;
}


/*
===================
NET_CompareBaseAdr

Compares without the port
===================
*/
qboolean NET_CompareBaseAdr (netadr_t a, netadr_t b)
{
	if (a.type == b.type)
	{
		if (a.type == NA_LOOPBACK)
		{
			return true;
		}
		else if (a.type == NA_IP)
		{
			if (a.ip[0] == b.ip[0] && a.ip[1] == b.ip[1] && a.ip[2] == b.ip[2] && a.ip[3] == b.ip[3])
				return true;

		}
		else if (a.type == NA_PDP)
		{
			if (!memcmp(a.mac, b.mac, 6))
				return true;
		}
	}

	return false;
}


/*
=============
NET_AdrToString
=============
*/
char *NET_AdrToString (netadr_t a)
{
	static char	s[64];
	int			len;

	if(a.type == NA_PDP)
	{
		sceNetEtherNtostr (a.mac, s);
		Com_sprintf (s, sizeof(s), "%s:%i", s, a.port);
	}
	else
	{
		Com_sprintf (s, sizeof(s), "%i.%i.%i.%i:%i", a.ip[0], a.ip[1], a.ip[2], a.ip[3], ntohs(a.port));
	}

	return s;
}


/*
=============
NET_BaseAdrToString
=============
*/
char *NET_BaseAdrToString (netadr_t a)
{
	static char	s[64];

	if(a.type == NA_PDP)
	{
		sceNetEtherNtostr(a.mac, s);
	}
	else
	{
		Com_sprintf (s, sizeof(s), "%i.%i.%i.%i", a.ip[0], a.ip[1], a.ip[2], a.ip[3]);
	}

	return s;
}


/*
=============
NET_GetHostByName
=============
*/
static qboolean NET_GetHostByName (char *hostname, struct in_addr *addr)
{
	int		rid, ret;
	char	buf[768];

	ret = sceNetResolverCreate (&rid, buf, (SceSize)sizeof(buf));
	if (ret < 0)
	{
		Com_DPrintf ("NET_GetHostAddr: sceNetResolverCreate error (0x%x)\n", ret);
		return false;
	}

	ret = sceNetResolverStartNtoA (rid, hostname, addr, 5 * 1000 * 1000, 5);
	if (ret < 0)
		Com_DPrintf ("NET_GetHostAddr: sceNetResolverStartNtoA error (0x%x)\n", ret);

	sceNetResolverDelete (rid);

	return (ret == 0);
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
static qboolean NET_StringToSockaddr (char *s, struct sockaddr *sadr)
{
	struct hostent	*h;
	char	*colon;
	char	copy[128];

	memset (sadr, 0, sizeof(*sadr));
	((struct sockaddr_in *)sadr)->sin_family = AF_INET;
	((struct sockaddr_in *)sadr)->sin_port = 0;

	strcpy (copy, s);

	// strip off a trailing :port if present
	for (colon = copy ; *colon ; colon++)
	{
		if (*colon == ':')
		{
			*colon = 0;
			((struct sockaddr_in *)sadr)->sin_port = htons ((short)atoi(colon + 1));
		}
	}

	if (copy[0] >= '0' && copy[0] <= '9')
	{
		*(int *)&((struct sockaddr_in *)sadr)->sin_addr = sceNetInetInetAddr (copy);
	}
	else
	{
		if (!NET_GetHostByName (copy, &((struct sockaddr_in *)sadr)->sin_addr))
			return false;
	}

	return true;
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
qboolean NET_StringToAdr (char *s, netadr_t *a, unsigned short dport)
{
	int		i, coloncount, colonend;
	char	copy[128];

	if (!strcmp (s, "localhost"))
	{
		memset (a, 0, sizeof(*a));
		a->type = NA_LOOPBACK;
		return true;
	}

	coloncount = 0;
	colonend = 0;

	for (i = 0; s[i] != 0; i++)
	{
		if (s[i] == ':')
		{
			colonend = i;
			coloncount++;
		}
	}

	strcpy (copy, s);

	if (coloncount > 0)
	{
		copy[colonend] = 0;
		a->port = (unsigned short)atoi (&copy[colonend + 1]);
	}
	else
		a->port = dport;

	if (coloncount >= 5) // mac
	{
		sceNetEtherStrton (copy, a->mac);
		a->type = NA_PDP;
	}
	else if (coloncount <= 1) // ip
	{
		if (isdigit (copy[0]))
			*(int *)&a->ip = sceNetInetInetAddr (copy);
		else if (!NET_GetHostByName (copy, (struct in_addr *)a->ip))
			return false;

		a->port = htons (a->port);
		a->type = NA_IP;
	}
	else // bad addr
	{
		return false;
	}

	return true;
}


/*
=============
NET_IsLocalAddress
=============
*/
qboolean NET_IsLocalAddress (netadr_t adr)
{
	return NET_CompareAdr (adr, net_local_adr);
}


/*
=============================================================================

LOOPBACK BUFFERS FOR LOCAL PLAYER

=============================================================================
*/

/*
====================
NET_GetLoopPacket
====================
*/
static qboolean NET_GetLoopPacket (netsrc_t sock, netadr_t *net_from, sizebuf_t *net_message)
{
	int		i;
	loopback_t	*loop;

	loop = &loopbacks[sock];

	if (loop->send - loop->get > NET_MAX_LOOPBACK)
		loop->get = loop->send - NET_MAX_LOOPBACK;

	if (loop->get >= loop->send)
		return false;

	i = loop->get & (NET_MAX_LOOPBACK-1);
	loop->get++;

	memcpy (net_message->data, loop->msgs[i].data, loop->msgs[i].datalen);
	net_message->cursize = loop->msgs[i].datalen;
	*net_from = net_local_adr;
	return true;

}

/*
====================
NET_SendLoopPacket
====================
*/
static void NET_SendLoopPacket (netsrc_t sock, int length, void *data, netadr_t to)
{
	int		i;
	loopback_t	*loop;

	loop = &loopbacks[sock^1];

	i = loop->send & (NET_MAX_LOOPBACK-1);
	loop->send++;

	memcpy (loop->msgs[i].data, data, length);
	loop->msgs[i].datalen = length;
}

//=============================================================================

/*
====================
NET_GetPacket
====================
*/
qboolean NET_GetPacket (netsrc_t sock, netadr_t *net_from, sizebuf_t *net_message)
{
	int 	ret;
	struct sockaddr_in	from;
	int		fromlen;
	int		datalen;
	int		net_socket;
	int		err;

	// Loopback
	if (NET_GetLoopPacket (sock, net_from, net_message))
		return true;

	// Infrastructure
	if (ip_sockets[sock])
	{
		fromlen = sizeof(from);
		ret = sceNetInetRecvfrom (ip_sockets[sock], net_message->data, net_message->maxsize, 0,
			(struct sockaddr *)&from, (socklen_t *)&fromlen);
		if (ret == -1)
		{
			err = sceNetInetGetErrno ();
			if (err != EWOULDBLOCK && err != ECONNREFUSED)
				Com_Printf ("NET_GetPacket: %s", NET_ErrorString ());
		}
		else
		{
			if (ret == net_message->maxsize)
				Com_Printf ("NET_SendPacket: Oversize packet from %s\n", NET_AdrToString (*net_from));
			else
			{
				NET_SockadrToNetadr (&from, net_from);
				net_message->cursize = ret;

				return true; // Successful
			}
		}
	}

	// Adhoc
	if (pdp_sockets[sock])
	{
		sceKernelDelayThread (1);

		datalen = net_message->maxsize; // IN buffer size, OUT receive data length
		ret = sceNetAdhocPdpRecv (pdp_sockets[sock], net_from->mac, &net_from->port,
			net_message->data, &datalen, 0, 1);
		if (ret)
		{
			if (ret != 0x80410709) // !WOULD BLOCK
			{
				if (ret == 0x80400706) // NOT ENOUGH SPACE
					Com_Printf ("NET_SendPacket: Oversize packet\n");
				else
					Com_Printf ("NET_SendPacket: (sceNetAdhocPdpRecv) error (0x%x)\n", ret);
			}
		}
		else
		{
			net_from->type = NA_PDP;
			net_message->cursize = datalen;

			return true; // Successful
		}
	}

	return false;
}

/*
====================
NET_SendPacket
====================
*/
void NET_SendPacket (netsrc_t sock, int length, void *data, netadr_t to)
{
	int		ret;
	struct sockaddr_in	addr;
	byte	*macaddr;

	if (to.type == NA_LOOPBACK)
	{
		NET_SendLoopPacket (sock, length, data, to);
	}
	else if (to.type == NA_BROADCAST || to.type == NA_IP)
	{
		if (!ip_sockets[sock])
			return;

		NET_NetadrToSockadr (&to, &addr);

		ret = sceNetInetSendto (ip_sockets[sock], data, length, 0, (struct sockaddr *)&addr, sizeof(addr));
		if (ret == -1)
			Com_Printf ("NET_SendPacket: %s\n", NET_ErrorString ());
	}
	else if(to.type == NA_PDP || to.type == NA_BROADCAST_PDP)
	{
		if (!pdp_sockets[sock])
			return;

		macaddr = (to.type == NA_BROADCAST_PDP) ? pdp_broadcastaddr : to.mac;
		ret = sceNetAdhocPdpSend (pdp_sockets[sock], macaddr, to.port, data, length, 0, 1); // nonblock flag
		if (ret)
			Com_Printf ("NET_SendPacket: (sceNetAdhocPdpSend) error (0x%x)\n", ret);
	}
	else
		Com_Error (ERR_FATAL, "NET_SendPacket: bad address type %i", to.type);
}

//=============================================================================

/*
====================
NET_OpenIP
====================
*/
static void NET_OpenIP (void)
{
	cvar_t	*port, *ip;

	port = Cvar_Get ("port", va("%i", PORT_SERVER), CVAR_NOSET);
	ip = Cvar_Get ("ip", "localhost", CVAR_NOSET);

	if (!ip_sockets[NS_SERVER])
		ip_sockets[NS_SERVER] = NET_InetSocket (ip->string, port->value);
	if (!ip_sockets[NS_CLIENT])
		ip_sockets[NS_CLIENT] = NET_InetSocket (ip->string, PORT_ANY);
}


/*
====================
NET_OpenPDP
====================
*/
static void NET_OpenPDP (void)
{
	cvar_t	*port;

	port = Cvar_Get ("port", va("%i", PORT_SERVER), CVAR_NOSET);

	if (!pdp_sockets[NS_SERVER])
		pdp_sockets[NS_SERVER] = NET_AdhocSocket (port->value);
	if (!pdp_sockets[NS_CLIENT])
		pdp_sockets[NS_CLIENT] = NET_AdhocSocket (PORT_ANY);
}


/*
====================
NET_Config

A single player game will only use the loopback code
====================
*/
void NET_Config (qboolean multiplayer)
{
	int				i;
	static qboolean	inet_initialized = false;
	static qboolean	adhoc_initialized = false;
	static float	last_mode = 0.0f;

	if (!multiplayer || (net_mode->modified && last_mode != net_mode->value))
	{	// shut down any existing sockets
		if (inet_initialized)
		{
			for (i = 0; i < 2; i++)
			{
				if (ip_sockets[i])
				{
					sceNetInetClose (ip_sockets[i]);
					ip_sockets[i] = 0;
				}
			}

			NET_ApctlDisconnect ();
			NET_InetShutdown ();

			inet_initialized = false;
		}

		if (adhoc_initialized)
		{
			for (i = 0; i < 2; i++)
			{
				if (pdp_sockets[i])
				{
					sceNetAdhocPdpDelete (pdp_sockets[i], 0);
					pdp_sockets[i] = 0;
				}
			}

			NET_AdhocctlDisconnect ();
			NET_AdhocShutdown ();

			adhoc_initialized = false;
		}

		Cvar_Set ("net_active", "0");
	}

	net_mode->modified = false;
	last_mode = net_mode->value;

	if (!multiplayer)
		return;

	// open sockets
	if (inet_initialized || adhoc_initialized)
		return;

	if (net_mode->value == 1.0f && net_accesspoint->value > 0.0f) // infrastructure mode
	{
		if (NET_InetInit ())
		{
			inet_initialized = NET_ApctlConnect((int)net_accesspoint->value);
			if (inet_initialized)
				NET_OpenIP ();
			else
				NET_InetShutdown ();
		}
	}
	else if (net_mode->value == 2.0f) // adhoc mode
	{
		if (NET_AdhocInit ())
		{
			adhoc_initialized = NET_AdhocctlConnect("quake2");
			if (adhoc_initialized)
				NET_OpenPDP ();
			else
				NET_AdhocShutdown ();
		}
	} // else loopback only

	if (inet_initialized || adhoc_initialized)
		Cvar_Set ("net_active", "1");

}

/*
=============================================================================

Infrastructure

=============================================================================
*/


/*
====================
NET_Init
====================
*/
void NET_Init (void)
{
	net_accesspoint = Cvar_Get ("net_accesspoint", "0", CVAR_ARCHIVE);
	net_mode = Cvar_Get ("net_mode", "0", CVAR_ARCHIVE); // 0 - Loopback, 1 - Infrastructure, 2 - Adhoc
	net_active = Cvar_Get ("net_active", "0", 0);

	// clear
	memset (&loopbacks, 0, sizeof(loopbacks));
	memset (&ip_sockets, 0, sizeof(ip_sockets));
	memset (&pdp_sockets, 0, sizeof(pdp_sockets));
	memset (&inet_handler_data, 0, sizeof(inet_handler_data));
	memset (&adhoc_handler_data, 0, sizeof(adhoc_handler_data));
}

//===================================================================


/*
====================
NET_InetInit
====================
*/
qboolean NET_InetInit (void)
{
	int	ret;

	if (!sceWlanGetSwitchState ())
		return false;

	sceUtilityLoadNetModule (PSP_NET_MODULE_COMMON);
	sceUtilityLoadNetModule (PSP_NET_MODULE_INET);

	ret = sceNetInit (NET_POOLSIZE, 0x20, 0, 0x20, 0);
	if (!ret) ret = sceNetInetInit ();
	if (!ret) ret = sceNetResolverInit ();
	if (!ret) ret = sceNetApctlInit (0x1600, 0x30);
	if (!ret)
	{
		ret = sceNetApctlAddHandler (NET_ApctlHandler, (void *)&inet_handler_data);
		if (ret > 0) inet_handler_data.id = ret;
	}

	if (ret < 0)
	{
		sceUtilityUnloadNetModule (PSP_NET_MODULE_INET);
		sceUtilityUnloadNetModule (PSP_NET_MODULE_COMMON);

		Com_Printf ("NET_ApctlInit: error (0x%x)\n", ret);
	}

	return (ret >= 0);
}


/*
====================
NET_ApctlHandler
====================
*/
static void NET_ApctlHandler (int oldstate, int newstate, int event, int code, void *arg)
{
	net_handler_t	*handler_data;

	Com_DPrintf ("NET_ApctlHandler: %i -> %i event %i - 0x%x\n", oldstate, newstate, event, code);

	handler_data = (net_handler_t *)arg;

	if (newstate == 4) // IP Obtained
	{
		handler_data->event |= NET_INET_EVENT_CONNECT;
	}
	else if (newstate == 0) // Disconnected
	{
		if (event == 5) // Disconnected request
			handler_data->event |= NET_INET_EVENT_DISCONNECT;

		if (event == 6) // error
			handler_data->event |= NET_INET_EVENT_ERROR;

		handler_data->event &= ~NET_INET_EVENT_CONNECT;
	}

	handler_data->error = code;
}


/*
====================
NET_ApctlConnect
====================
*/
qboolean NET_ApctlConnect (int id)
{
	int	ret;

	if (!id)
		return false;

	ret = sceNetApctlConnect (id);
	if (ret)
	{
		Com_Printf ("NET_ApctlConnect: error (0x%x)\n", ret);
		return false;
	}

	ret = NET_WaitEvent (&inet_handler_data, NET_INET_EVENT_CONNECT, 10); // 10 sec
	if (ret)
	{
		if(ret == -1)
			Com_Printf ("NET_ApctlConnect: timeout\n", ret);
		else
			Com_Printf ("NET_ApctlConnect: error (0x%x)\n", ret);
		return false;
	}

	return true;
}


/*
====================
NET_ApctlDisconnect
====================
*/
qboolean NET_ApctlDisconnect (void)
{
	int	ret;

	ret = sceNetApctlDisconnect ();
	if (ret)
	{
		Com_Printf ("NET_ApctlDisconnect: error (0x%x)\n", ret);
		return false;
	}

	ret = NET_WaitEvent (&inet_handler_data, NET_INET_EVENT_DISCONNECT, 10); // 10 sec
	if (ret)
	{
		if(ret == -1)
			Com_Printf ("NET_ApctlDisconnect: timeout\n", ret);
		else
			Com_Printf ("NET_ApctlDisconnect: error (0x%x)\n", ret);
		return false;
	}

	return true;
}


/*
====================
NET_InetSocket
====================
*/
static int NET_InetSocket (char *net_interface, int port)
{
	int		newsocket;
	struct sockaddr_in address;
	int		i = 1;

	if ((newsocket = sceNetInetSocket (PF_INET, SOCK_DGRAM, 0)) == -1)
	{
		Com_Printf ("NET_InetSocket: (sceNetInetSocket) %s\n", NET_ErrorString ());
		return 0;
	}

	// make it non-blocking
	if (sceNetInetSetsockopt (newsocket, SOL_SOCKET, SO_NONBLOCK, &i, sizeof(i)) == -1)
	{
		Com_Printf ("NET_InetSocket: (sceNetInetSetsockopt) %s\n", NET_ErrorString ());
		sceNetInetClose (newsocket);
		return 0;
	}

	// make it broadcast capable
	if (sceNetInetSetsockopt (newsocket, SOL_SOCKET, SO_BROADCAST, (char *)&i, sizeof(i)) == -1)
	{
		Com_Printf ("NET_InetSocket: (sceNetInetSetsockopt) %s\n", NET_ErrorString ());
		sceNetInetClose (newsocket);
		return 0;
	}

	if (!net_interface || !net_interface[0] || !stricmp (net_interface, "localhost"))
		address.sin_addr.s_addr = INADDR_ANY;
	else
		NET_StringToSockaddr (net_interface, (struct sockaddr *)&address);

	if (port == PORT_ANY)
		address.sin_port = 0;
	else
		address.sin_port = htons ((short)port);

	address.sin_family = AF_INET;

	if (sceNetInetBind (newsocket, (void *)&address, sizeof(address)) == -1)
	{
		Com_Printf ("NET_InetSocket: (sceNetInetBind) %s\n", NET_ErrorString());
		sceNetInetClose (newsocket);
		return 0;
	}

	return newsocket;
}


/*
====================
NET_InetShutdown
====================
*/
void NET_InetShutdown (void)
{
	if (inet_handler_data.id)
	{
		sceNetApctlDelHandler (inet_handler_data.id);
		inet_handler_data.id = 0;
	}

	sceNetApctlTerm();
	sceNetResolverTerm();
	sceNetInetTerm();
	sceNetTerm();

	sceUtilityUnloadNetModule(PSP_NET_MODULE_INET);
	sceUtilityUnloadNetModule(PSP_NET_MODULE_COMMON);
}

/*
=============================================================================

Adhoc

=============================================================================
*/


/*
====================
NET_AdhocInit
====================
*/
qboolean NET_AdhocInit (void)
{
	int	ret;
	struct productStruct temp;

	if (!sceWlanGetSwitchState ())
		return false;

	memset (&adhoc_handler_data, 0, sizeof(adhoc_handler_data));

	strcpy(temp.product, NET_ADHOC_PRODUCT);
	temp.unknown = 0;

	sceUtilityLoadNetModule (PSP_NET_MODULE_COMMON);
	sceUtilityLoadNetModule (PSP_NET_MODULE_ADHOC);

	ret = sceNetInit (NET_POOLSIZE, 0x20, 0, 0x20, 0);
	if (!ret) ret = sceNetAdhocInit ();
	if (!ret) ret = sceNetAdhocctlInit (0x2000, 0x20, &temp);
	if (!ret)
	{
		ret = sceNetAdhocctlAddHandler (NET_AdhocctlHandler, (void *)&adhoc_handler_data);
		if (ret > 0) adhoc_handler_data.id = ret;
	}

	if (ret < 0)
	{
		sceUtilityUnloadNetModule (PSP_NET_MODULE_ADHOC);
		sceUtilityUnloadNetModule (PSP_NET_MODULE_COMMON);

		Com_Printf ("NET_ApctlInit: error (0x%x)\n", ret);
	}

	return (ret >= 0);
}


/*
====================
NET_AdhocctlHandler
====================
*/
void NET_AdhocctlHandler (int event, int code, void *arg)
{
	net_handler_t	*handler_data;

	if(event >= 0 && event <= 4)
		Com_DPrintf ("NET_AdhocctlHandler: %s - 0x%x\n", adhoc_event_str[event], code);
	else
		Com_DPrintf ("NET_AdhocctlHandler: %i - 0x%x\n", event, code);

	handler_data = (net_handler_t *)arg;

	switch (event)
	{
	case 0: // error
		handler_data->event |= NET_ADHOC_EVENT_ERROR;
		break;
	case 1: // connect
		handler_data->event |= NET_ADHOC_EVENT_CONNECT;
		break;
	case 2: // disconnect
		handler_data->event |= NET_ADHOC_EVENT_DISCONNECT;
		break;
	case 3: // scan
		handler_data->event |= NET_ADHOC_EVENT_SCAN;
		break;
	case 4: // gamemode
		handler_data->event |= NET_ADHOC_EVENT_GAMEMODE;
		break;
	default: // other adhoc, wlan events
		break;
	}

	handler_data->error = code;
}


/*
====================
NET_AdhocctlConnect
====================
*/
qboolean NET_AdhocctlConnect (char *group)
{
	int	ret;

	ret = sceNetAdhocctlConnect (group); // length 8!
	if (ret)
	{
		Com_Printf ("NET_AdhocctlConnect: error (0x%x)\n", ret);
		return false;
	}

	ret = NET_WaitEvent (&adhoc_handler_data, NET_ADHOC_EVENT_CONNECT, 0);
	if (ret)
	{
		Com_Printf ("NET_AdhocctlConnect: error (0x%x)\n", ret);
		return false;
	}

	return true;
}


/*
====================
NET_ApctlDisconnect
====================
*/
qboolean NET_AdhocctlDisconnect (void)
{
	int	ret;

	ret = sceNetAdhocctlDisconnect ();
	if (ret)
	{
		Com_Printf ("NET_AdhocctlDisconnect: error (0x%x)\n", ret);
		return false;
	}
	ret = NET_WaitEvent (&adhoc_handler_data, NET_ADHOC_EVENT_DISCONNECT, 0);
	if (ret)
	{
		Com_Printf ("NET_AdhocctlDisconnect: error (0x%x)\n", ret);
		return false;
	}

	return true;
}


/*
====================
NET_AdhocSocket
====================
*/
static int NET_AdhocSocket (int port)
{
	int				newsocket;
	byte			mac[8];
	unsigned short	sport;

	sceWlanGetEtherAddr (mac);
	sport = (port == PORT_ANY) ? 0 : port;

	newsocket = sceNetAdhocPdpCreate (mac, sport, 0x2000, 0);
	if(newsocket < 0)
	{
		Com_Printf ("NET_AdhocSocket: (sceNetAdhocPdpCreate) error (0x%x)\n", newsocket);
		return 0;
	}

	return newsocket;
}


/*
====================
NET_AdhocShutdown
====================
*/
void NET_AdhocShutdown (void)
{
	if (adhoc_handler_data.id)
	{
		sceNetAdhocctlDelHandler (adhoc_handler_data.id);
		adhoc_handler_data.id = 0;
	}

	sceNetAdhocctlTerm ();
	sceNetAdhocTerm ();
	sceNetTerm ();

	sceUtilityUnloadNetModule (PSP_NET_MODULE_ADHOC);
	sceUtilityUnloadNetModule (PSP_NET_MODULE_COMMON);
}

//===================================================================


/*
====================
NET_Shutdown
====================
*/
void NET_Shutdown (void)
{
	NET_Config (false);	// close sockets
}


/*
====================
NET_ErrorString
====================
*/
static char *NET_ErrorString (void)
{
	int	code;
	code = sceNetInetGetErrno ();
	return strerror (code);
}


/*
====================
NET_Sleep

sleeps msec or until net socket is ready
====================
*/
void NET_Sleep(int msec)
{
#if 0
	struct timeval timeout;
	fd_set	fdset;
	extern cvar_t *dedicated;
	extern qboolean stdin_active;

	if (!ip_sockets[NS_SERVER] || (dedicated && !dedicated->value))
		return; // we're not a server, just run full speed

	FD_ZERO(&fdset);
	if (stdin_active)
		FD_SET(0, &fdset); // stdin is processed too
	FD_SET(ip_sockets[NS_SERVER], &fdset); // network socket
	timeout.tv_sec = msec/1000;
	timeout.tv_usec = (msec%1000)*1000;
	sceNetInetSelect (ip_sockets[NS_SERVER]+1, &fdset, NULL, NULL, &timeout);
#endif
}
