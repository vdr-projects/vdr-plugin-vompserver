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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <net/if.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <sys/ioctl.h>
#include <vector>
#include <algorithm>

#include "dsock6.h"

DatagramSocket6::DatagramSocket6()
{
  addrlen = sizeof(struct sockaddr_in6);
  log = Log::getInstance();
  initted = 0;
}

DatagramSocket6::~DatagramSocket6()
{
  shutdown();
}

bool DatagramSocket6::init(USHORT port)
{
  if ((socketnum = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP)) == -1)
  {
    log->log("UDP6", Log::CRIT, "Socket error");
    return false;
  }

  char* one = (char*)1;
  if (setsockopt(socketnum, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)))
  {
    log->log("UDP6", Log::CRIT, "Reuse addr fail");
    return false;
  }

  struct sockaddr_in6 saddr;
  memset(&saddr, 0, sizeof(saddr));
  saddr.sin6_family = AF_INET6;
  saddr.sin6_port = htons(port);
  saddr.sin6_addr = in6addr_any;

  if (bind(socketnum, (struct sockaddr *)&saddr, addrlen) == -1)
  {
    log->log("UDP6", Log::CRIT, "Bind error %u", port);
    close(socketnum);
    return false;
  }

  struct ifaddrs* ifas;
  if(getifaddrs(&ifas))
  {
    log->log("UDP6", Log::CRIT, "getifaddrs error");
    close(socketnum);
    return false;
  }

  struct if_nameindex* ifs = if_nameindex();
  std::vector<unsigned int> mcastIndexes;

  struct ifaddrs* ifaNext = ifas;
  for(int i = 0; ifaNext; i++)
  {
    if ((ifaNext->ifa_flags & IFF_MULTICAST) && (ifaNext->ifa_addr->sa_family == AF_INET6))
    {
      for(int i = 0; ifs[i].if_index > 0; i++)
      {
        if (!strcmp(ifaNext->ifa_name, ifs[i].if_name))
        {
          mcastIndexes.push_back(ifs[i].if_index);
          break;
        }
      }
    }
    ifaNext = ifaNext->ifa_next;
  }

  freeifaddrs(ifas);
  if_freenameindex(ifs);


  std::sort(mcastIndexes.begin(), mcastIndexes.end());
  auto last = std::unique(mcastIndexes.begin(), mcastIndexes.end());
  mcastIndexes.erase(last, mcastIndexes.end());


  for(auto mif : mcastIndexes)
  {
    //log->log("UDP6", Log::DEBUG, "To listen on index %u", mif);

    struct ipv6_mreq mGroup;
    inet_pton(AF_INET6, "ff15:766f:6d70:2064:6973:636f:7665:7279", &mGroup.ipv6mr_multiaddr);
    mGroup.ipv6mr_interface = mif;

    if (!setsockopt(socketnum, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mGroup, sizeof(mGroup)))
      log->log("UDP6", Log::DEBUG, "Listening on IF %u", mif);
    else
      log->log("UDP6", Log::ERR, "Cannot listen on IF %u", mif);
  }


  FD_ZERO(&readfds);
  FD_SET(socketnum, &readfds);
  tv.tv_sec = 0;
  tv.tv_usec = 0;

  initted = 1;
  return true;
}

void DatagramSocket6::shutdown()
{
  if (initted) close(socketnum);
  initted = 0;
}

unsigned char DatagramSocket6::waitforMessage(unsigned char how)
{
  if (!initted) return 0;

  /* how = 0 - block
     how = 1 - start new wait
     how = 2 - continue wait
  */

  struct timeval* passToSelect = NULL;


  if (how == 0)
  {
    passToSelect = NULL;
  }
  else if (how == 1)
  {
    tv.tv_sec = 1;
    tv.tv_usec = 100000;
    passToSelect = &tv;
  }
  else if (how == 2)
  {
    if ((tv.tv_sec == 0) && (tv.tv_usec == 0))  // protection in case timer = 0
    {
      tv.tv_sec = 1;
      tv.tv_usec = 100000;
    }
    passToSelect = &tv;
  }
  FD_ZERO(&readfds);
  FD_SET(socketnum, &readfds);

  if (select(socketnum + 1, &readfds, NULL, NULL, passToSelect) <= 0) return 1;

  struct sockaddr_in6 theirAddr;
  if ((mlength = recvfrom(socketnum, buf, MAXBUFLEN, 0, (struct sockaddr *)&theirAddr, &addrlen)) == -1)
  {
    log->log("UDP6", Log::DEBUG, "recvfrom error");
    return 0;
  }
  else
  {
    memset(&buf[mlength], 0, MAXBUFLEN - mlength);
    inet_ntop(AF_INET6, &theirAddr.sin6_addr, fromIPA, 40);
    fromPort = ntohs(theirAddr.sin6_port);
    log->log("UDP", Log::DEBUG, "%s:%i received length %i", fromIPA, fromPort, mlength);
    return 2;
  }

  /* Return 0, failure
     Return 1, nothing happened, timer expired
     Return 2, packet arrived (timer not expired)
  */
}

void DatagramSocket6::send(const char *ipa, USHORT port, char *message, int length)
{
  int sentLength = 0;

  struct sockaddr_in6 theirAddr;
  memset(&theirAddr, 0, sizeof(struct sockaddr_in6));

  theirAddr.sin6_family = AF_INET6;
  theirAddr.sin6_port = htons(port);
  inet_pton(AF_INET6, ipa, &theirAddr.sin6_addr);

  sentLength = sendto(socketnum, message, length, 0, (struct sockaddr *)&theirAddr, addrlen);
  if (sentLength == length)
  {
    log->log("UDP", Log::DEBUG, "%s:%u sent length %i", ipa, port, length);
    return;
  }

  log->log("UDP", Log::DEBUG, "%s:%u send failed %i", ipa, port, length);

  sentLength = sendto(socketnum, message, length, 0, (struct sockaddr *)&theirAddr, addrlen);
  if (sentLength == length)
  {
    log->log("UDP", Log::DEBUG, "%s:%u sent length %i 2nd try", ipa, port, length);
    return;
  }

  log->log("UDP", Log::DEBUG, "%s:%u send failed %i 2nd try", ipa, port, length);
}
