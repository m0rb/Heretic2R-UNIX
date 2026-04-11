/*
 * net_udp.c -- Unix UDP network support for Heretic2R
 * Copyright 1998 Raven Software
 * Copyright (C) 2010-2024 Yamagi Quake 2 Contributors (GPLv2)
 * Unix port by morb
 */

#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>

#include "../../../qcommon/qcommon.h"

#define MAX_LOOPBACK 4

typedef struct
{
	byte data[MAX_MSGLEN];
	int datalen;
} loopmsg_t;

typedef struct
{
	loopmsg_t msgs[MAX_LOOPBACK];
	int get;
	int send;
} loopback_t;

#define NUM_SOCKETS 3

static loopback_t loopbacks[NUM_SOCKETS];
static int ip_sockets[NUM_SOCKETS];

static cvar_t* noudp;

static void NetadrToSockadr(const netadr_t* a, struct sockaddr_in* s)
{
	memset(s, 0, sizeof(*s));

	switch (a->type)
	{
		case NA_BROADCAST:
			s->sin_family = AF_INET;
			s->sin_port = a->port;
			s->sin_addr.s_addr = INADDR_BROADCAST;
			break;

		case NA_IP:
			s->sin_family = AF_INET;
			s->sin_addr.s_addr = *(int*)&a->ip;
			s->sin_port = a->port;
			break;

		default:
			break;
	}
}

static void SockadrToNetadr(struct sockaddr_in* s, netadr_t* a)
{
	if (s->sin_family == AF_INET)
	{
		a->type = NA_IP;
		memcpy(a->ip, &s->sin_addr.s_addr, sizeof(s->sin_addr.s_addr));
		a->port = s->sin_port;
	}
}

qboolean NET_CompareAdr(const netadr_t* a, const netadr_t* b)
{
	if (a->type != b->type)
		return false;

	switch (a->type)
	{
		case NA_LOOPBACK:
			return true;

		case NA_IP:
			return (a->port == b->port && memcmp(a->ip, b->ip, 4) == 0);

		default:
			return false;
	}
}

qboolean NET_CompareBaseAdr(const netadr_t* a, const netadr_t* b)
{
	if (a->type != b->type)
		return false;

	switch (a->type)
	{
		case NA_LOOPBACK:
			return true;

		case NA_IP:
			return (memcmp(a->ip, b->ip, 4) == 0);

		default:
			return false;
	}
}

char* NET_AdrToString(const netadr_t* a)
{
	static char s[64];

	switch (a->type)
	{
		case NA_LOOPBACK:
			Com_sprintf(s, sizeof(s), "loopback");
			break;

		case NA_IP:
			Com_sprintf(s, sizeof(s), "%i.%i.%i.%i:%i", a->ip[0], a->ip[1], a->ip[2], a->ip[3], ntohs(a->port));
			break;

		default:
			Com_sprintf(s, sizeof(s), "unknown");
			break;
	}

	return s;
}

static qboolean NET_StringToSockaddr(const char* s, struct sockaddr_in* sadr)
{
	memset(sadr, 0, sizeof(*sadr));

	struct hostent* h;
	char* colon;
	char copy[128];

	sadr->sin_family = AF_INET;
	sadr->sin_port = 0;

	strcpy(copy, s);

	// Strip off a trailing :port if present
	for (colon = copy; *colon != 0; colon++)
	{
		if (*colon == ':')
		{
			*colon = 0;
			sadr->sin_port = htons((unsigned short)Q_atoi(colon + 1));
		}
	}

	if (copy[0] >= '0' && copy[0] <= '9')
	{
		sadr->sin_addr.s_addr = inet_addr(copy);
	}
	else
	{
		if ((h = gethostbyname(copy)) == NULL)
			return false;

		sadr->sin_addr.s_addr = *(int*)h->h_addr_list[0];
	}

	return true;
}

qboolean NET_StringToAdr(const char* s, netadr_t* a)
{
	struct sockaddr_in sadr;

	if (strcmp(s, "localhost") == 0)
	{
		memset(a, 0, sizeof(netadr_t));
		a->type = NA_LOOPBACK;
		return true;
	}

	if (NET_StringToSockaddr(s, &sadr))
	{
		SockadrToNetadr(&sadr, a);
		return true;
	}

	return false;
}

qboolean NET_IsLocalAddress(const netadr_t* a)
{
	return a->type == NA_LOOPBACK;
}

static char* NET_ErrorString(void)
{
	return strerror(errno);
}

#pragma region ========================== LOOPBACK BUFFERS FOR LOCAL PLAYER ==========================

static qboolean NET_GetLoopPacket(const netsrc_t sock, netadr_t* n_from, sizebuf_t* n_message)
{
	loopback_t* loop = &loopbacks[sock];

	if (loop->send - loop->get > MAX_LOOPBACK)
		loop->get = loop->send - MAX_LOOPBACK;

	if (loop->get >= loop->send)
		return false;

	const int i = loop->get & (MAX_LOOPBACK - 1);
	loop->get++;

	memcpy(n_message->data, loop->msgs[i].data, loop->msgs[i].datalen);
	n_message->cursize = loop->msgs[i].datalen;
	memset(n_from, 0, sizeof(*n_from));
	n_from->type = NA_LOOPBACK;

	return true;
}

static void NET_SendLoopPacket(const netsrc_t sock, const int length, const void* data)
{
	loopback_t* loop = &loopbacks[sock ^ 1];
	const int index = loop->send & (MAX_LOOPBACK - 1);
	loop->send++;

	memcpy(loop->msgs[index].data, data, length);
	loop->msgs[index].datalen = length;
}

#pragma endregion

qboolean NET_GetPacket(const netsrc_t sock, netadr_t* n_from, sizebuf_t* n_message)
{
	struct sockaddr_in from;
	socklen_t fromlen;

	if (NET_GetLoopPacket(sock, n_from, n_message))
		return true;

	if (ip_sockets[sock] == 0)
		return false;

	fromlen = sizeof(from);
	const int ret = recvfrom(ip_sockets[sock], (char*)n_message->data, n_message->maxsize, 0, (struct sockaddr*)&from, &fromlen);

	if (ret == -1)
	{
		if (errno != EWOULDBLOCK && errno != EAGAIN)
		{
			// Let dedicated servers continue after errors
			if ((int)dedicated->value)
				Com_Printf("NET_GetPacket: %s from %s\n", NET_ErrorString(), NET_AdrToString(n_from));
			else
				Com_Error(ERR_DROP, "NET_GetPacket: %s from %s", NET_ErrorString(), NET_AdrToString(n_from));
		}

		return false;
	}

	SockadrToNetadr(&from, n_from);

	if (ret == n_message->maxsize)
	{
		Com_Printf("Oversize packet from %s\n", NET_AdrToString(n_from));
		return false;
	}

	n_message->cursize = ret;
	return true;
}

void NET_SendPacket(const netsrc_t sock, const int length, const void* data, const netadr_t* to)
{
	struct sockaddr_in addr;
	int net_socket;

	switch (to->type)
	{
		case NA_LOOPBACK:
			NET_SendLoopPacket(sock, length, data);
			return;

		case NA_IP:
		case NA_BROADCAST:
			net_socket = ip_sockets[sock];
			break;

		// morb was here. NA_IPX / NA_BROADCAST_IPX are not supported on Unix — skip silently.
		// Previously these fell to the fatal error below, crashing on "Join a server".
		case NA_IPX:
		case NA_BROADCAST_IPX:
			return;

		default:
			Com_Error(ERR_FATAL, "NET_SendPacket: bad address type");
			return;
	}

	if (net_socket == 0)
		return;

	NetadrToSockadr(to, &addr);

	if (sendto(net_socket, data, length, 0, (struct sockaddr*)&addr, sizeof(addr)) == -1)
	{
		const int err = errno;

		// Wouldblock is silent
		if (err == EWOULDBLOCK || err == EAGAIN)
			return;

		// Some PPP links don't allow broadcasts
		if (err == EADDRNOTAVAIL && to->type == NA_BROADCAST)
			return;

		// Let dedicated servers continue after errors
		if ((int)dedicated->value)
			Com_Printf("NET_SendPacket ERROR: %s\n", NET_ErrorString());
		else if (err == EADDRNOTAVAIL)
			Com_DPrintf("NET_SendPacket Warning: %s : %s\n", NET_ErrorString(), NET_AdrToString(to));
		else
			Com_Error(ERR_DROP, "NET_SendPacket ERROR: %s\n", NET_ErrorString());
	}
}

static int NET_IPSocket(const char* net_interface, const int port)
{
	int optval = 1;

	const int newsocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (newsocket == -1)
	{
		if (errno != EAFNOSUPPORT)
			Com_Printf("WARNING: UDP_OpenSocket: socket: %s\n", NET_ErrorString());

		return 0;
	}

	// Make it non-blocking
	if (ioctl(newsocket, FIONBIO, &optval) == -1)
	{
		Com_Printf("WARNING: UDP_OpenSocket: ioctl FIONBIO: %s\n", NET_ErrorString());
		close(newsocket);
		return 0;
	}

	// Make it broadcast capable
	if (setsockopt(newsocket, SOL_SOCKET, SO_BROADCAST, &optval, sizeof(optval)) == -1)
	{
		Com_Printf("WARNING: UDP_OpenSocket: setsockopt SO_BROADCAST: %s\n", NET_ErrorString());
		close(newsocket);
		return 0;
	}

	struct sockaddr_in address;
	if (net_interface == NULL || net_interface[0] == 0 || Q_stricmp(net_interface, "localhost") == 0)
		address.sin_addr.s_addr = INADDR_ANY;
	else
		NET_StringToSockaddr(net_interface, &address);

	if (port == PORT_ANY)
		address.sin_port = 0;
	else
		address.sin_port = htons((unsigned short)port);

	address.sin_family = AF_INET;

	if (bind(newsocket, (struct sockaddr*)&address, sizeof(address)) == -1)
	{
		Com_Printf("WARNING: UDP_OpenSocket: bind: %s\n", NET_ErrorString());
		close(newsocket);
		return 0;
	}

	return newsocket;
}

static void NET_OpenIP(void)
{
	const cvar_t* ip = Cvar_Get("ip", "localhost", CVAR_NOSET);
	const qboolean is_dedicated = Cvar_IsSet("dedicated");

	if (ip_sockets[NS_SERVER] == 0)
	{
		int port = (int)Cvar_Get("ip_hostport", "0", CVAR_NOSET)->value;
		if (port == 0)
		{
			port = (int)Cvar_Get("hostport", "0", CVAR_NOSET)->value;
			if (port == 0)
				port = (int)Cvar_Get("port", va("%i", PORT_SERVER), CVAR_NOSET)->value;
		}

		ip_sockets[NS_SERVER] = NET_IPSocket(ip->string, port);
		if (is_dedicated && ip_sockets[NS_SERVER] == 0)
			Com_Error(ERR_FATAL, "Couldn't allocate dedicated server IP port");
	}

	// Dedicated servers don't need client ports
	if (is_dedicated)
		return;

	if (ip_sockets[NS_CLIENT] == 0)
	{
		int port = (int)Cvar_Get("ip_clientport", "0", CVAR_NOSET)->value;
		if (port == 0)
		{
			port = (int)Cvar_Get("clientport", va("%i", PORT_CLIENT), CVAR_NOSET)->value;
			if (port == 0)
				port = PORT_ANY;
		}

		ip_sockets[NS_CLIENT] = NET_IPSocket(ip->string, port);
		if (ip_sockets[NS_CLIENT] == 0)
			ip_sockets[NS_CLIENT] = NET_IPSocket(ip->string, PORT_ANY);
	}
}

void NET_Config(const qboolean multiplayer)
{
	static qboolean old_config;

	if (old_config == multiplayer)
		return;

	old_config = multiplayer;

	if (multiplayer)
	{
		// Open sockets
		if (!(int)noudp->value)
			NET_OpenIP();
	}
	else
	{
		// Shut down any existing sockets
		for (int i = 0; i < NUM_SOCKETS; i++)
		{
			if (ip_sockets[i] != 0)
			{
				close(ip_sockets[i]);
				ip_sockets[i] = 0;
			}
		}
	}
}

void NET_Init(void)
{
	noudp = Cvar_Get("noudp", "0", CVAR_NOSET);

	Com_Printf("Unix UDP networking initialized\n");
}

void NET_Shutdown(void)
{
	NET_Config(false); // Close sockets
}