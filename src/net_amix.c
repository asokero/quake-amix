/*
Copyright (C) 1996-1997 Id Software, Inc.

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
net_amix.c - network driver table for AMIX SVR4, loopback only.

This is net_bsd.c with the Datagram driver and the UDP LAN driver removed.

AMIX does have BSD sockets (libsocket.so, libnsl.so, <netinet/in.h>), and
src/net_udp.c compiles against them unchanged.  What is missing is the other
end: libsocket.so itself refers to select(), syslog() and seteuid(), none of
which AMIX exports from libc.so.1 -- they exist only in the static libc.a and
in libucb.a.  Linking UDP therefore means either dragging BSD-compatibility
objects into a program that otherwise uses the SVR4 libc, or linking libucb
wholesale, which overrides libc functions with BSD semantics.

Neither belongs in the first bring-up.  Loopback alone runs a listen server
and a dedicated server, which is all the engine needs to be exercised.
Restoring UDP is a self-contained later step: build net_udp.o, put the
Datagram entry and the UDP landriver back, and solve libsocket's three
symbols on their own terms.
*/

#include "quakedef.h"

#include "net_loop.h"
#include "net_dgrm.h"

net_driver_t net_drivers[MAX_NET_DRIVERS] =
{
	{
	"Loopback",
	false,
	Loop_Init,
	Loop_Listen,
	Loop_SearchForHosts,
	Loop_Connect,
	Loop_CheckNewConnections,
	Loop_GetMessage,
	Loop_SendMessage,
	Loop_SendUnreliableMessage,
	Loop_CanSendMessage,
	Loop_CanSendUnreliableMessage,
	Loop_Close,
	Loop_Shutdown
	}
};

int net_numdrivers = 1;

net_landriver_t	net_landrivers[MAX_NET_DRIVERS];

int net_numlandrivers = 0;
