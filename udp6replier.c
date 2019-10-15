/*
    Copyright 2019 Chris Tallon

    This file is part of VOMP.

    VOMP is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    VOMP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with VOMP.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

#include "vompclientrrproc.h"

#include "udp6replier.h"

UDP6Replier::UDP6Replier()
{
  message = NULL;
  messageLen = 0;
}

UDP6Replier::~UDP6Replier()
{
  shutdown();
}

int UDP6Replier::shutdown()
{
  if (threadIsActive()) threadCancel();

  if (message) delete[] message;
  message = NULL;
  return 1;
}

int UDP6Replier::run(USHORT port, char* serverName, USHORT serverPort)
{
  if (threadIsActive()) return 1;

  /*
  VOMP Discovery Protocol V1

  Client transmits: "VDP-0001\0<6-byte MAC>"...
  ... for IPv4: broadcasts on ports 51051-51055
  ... for IPv6: multicasts to ff15:766f:6d70:2064:6973:636f:7665:7279 port 51056

  Server responds:

  Field 1 p0: 9 bytes "VDP-0002\0"
  
  Field 2 p9, 1 byte:

  0 = no IP specified
  4 = first 4 bytes of field 3 are IPv4 address of server
  6 = field 3 16 bytes are IPv6 address of server

  Field 3, p10, 16 bytes:
  As described above. If field 2 is 0, this should be all zeros. If this is an IPv4 address, remaining bytes should be zeros.

  Field 4 p26, 2 bytes:
  Port number of server

  Field 5 p28, 4 bytes:
  VOMP protocol version (defined in vdr.cc)
  
  Field 6 p32, variable length
  String of server name, null terminated
  */
  
  messageLen = strlen(serverName) + 33;
  message = new char[messageLen];
  memset(message, 0, messageLen);
  // by zeroing the packet, this sets no ip address return information
  
  strcpy(message, "VDP-0002");
  
  USHORT temp = htons(serverPort);
  memcpy(&message[26], &temp, 2);
  
  ULONG temp2 = htonl(VompClientRRProc::getProtocolVersionMin());
  memcpy(&message[28], &temp2, 4);
  
  strcpy(&message[32], serverName);
  // Fix Me add also the maximum version somewhere
  if (!ds.init(port))
  {
    shutdown();
    return 0;
  }

  if (!threadStart())
  {
    shutdown();
    return 0;
  }

  Log::getInstance()->log("UDP6Replier", Log::DEBUG, "UDP replier started");
  return 1;
}

void UDP6Replier::threadMethod()
{
  int retval;
  while(1)
  {
    retval = ds.waitforMessage(0);
    if (retval == 1) continue;

    if (!strncmp(ds.getData(), "VDP-0001", 8))
    {
      Log::getInstance()->log("UDP6Replier", Log::DEBUG, "UDP6 request from %s", ds.getFromIPA());
      ds.send(ds.getFromIPA(), ds.getFromPort(), message, messageLen);
    }
  }
}
