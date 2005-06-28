/*
    Copyright 2004-2005 Chris Tallon
    Copyright 2003-2004 University Of Bradford

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
    along with VOMP; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "tcp.h"

TCP::TCP(int tsocket)
{
  sock = -1;
  connected = 0;
  readTimeoutEnabled = 1;

  if (tsocket)
  {
    sock = tsocket;
    connected = 1;
  }
}

TCP::~TCP()
{
  if (connected) cleanup();
}

void TCP::cleanup()
{
  close(sock);
  sock = -1;
  connected = 0;
  printf("TCP has closed socket\n");
}


void TCP::disableReadTimeout()
{
  readTimeoutEnabled = 0;
}

int TCP::connectTo(char* host, unsigned short port)
{
  sock = socket(PF_INET, SOCK_STREAM, 0);
  if (sock == -1) return 0;

  struct sockaddr_in dest_addr;
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_port = htons(port);

  if (!inet_aton(host, &dest_addr.sin_addr))
  {
    cleanup();
    return 0;
  }

  memset(&(dest_addr.sin_zero), '\0', 8);

  int success = connect(sock, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr));
  if (success == -1)
  {
    cleanup();
    return 0;
  }

  connected = 1;
  return 1;
}

void TCP::setNonBlocking()
{
  int oldflags = fcntl(sock, F_GETFL, 0);
  oldflags |= O_NONBLOCK;
  fcntl(sock, F_SETFL, oldflags);
}

int TCP::setSoKeepTime(int timeOut)
{
  int option;
  int s1, s2, s3, s4;

  option = 1;
  s1 = setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &option, sizeof(option));
  printf("SO_KEEPALIVE = %i\n", s1);

  option = timeOut;
  s2 = setsockopt(sock, SOL_TCP, TCP_KEEPIDLE, &option, sizeof(option));
  printf("TCP_KEEPIDLE = %i\n", s2);

  s3 = setsockopt(sock, SOL_TCP, TCP_KEEPINTVL, &option, sizeof(option));
  printf("TCP_KEEPINTVL = %i\n", s3);

  option = 2;
  s4 = setsockopt(sock, SOL_TCP, TCP_KEEPCNT, &option, sizeof(option));
  printf("TCP_KEEPCNT = %i\n", s4);

  if (s1 || s2 || s3 || s4) return 0;
  return 1;
}

void TCP::assignSocket(int tsocket)
{
  sock = tsocket;
  connected = 1;
}

int TCP::isConnected()
{
  return connected;
}

UCHAR* TCP::receivePacket()
{
  if (!connected) return NULL;

  int packetLength;
  int success;

  success = readData((UCHAR*)&packetLength, sizeof(int));
  if (!success)
  {
    cleanup();
    return NULL;
  }

  packetLength = ntohl(packetLength);

  if (packetLength > 200000) return NULL;
  UCHAR* buffer = (UCHAR*) malloc(packetLength);

  success = readData(buffer, packetLength);
  if (!success)
  {
    cleanup();
    free(buffer);
    return NULL;
  }

  dump((unsigned char*)buffer, packetLength);

  dataLength = packetLength;
  return buffer;
}

int TCP::getDataLength()
{
  return dataLength;
}

int TCP::readData(UCHAR* buffer, int totalBytes)
{
  if (!connected) return 0;

  int bytesRead = 0;
  int thisRead;
  int readTries = 0;
  int success;
  fd_set readSet;
  struct timeval timeout;
  struct timeval* passToSelect;

  if (readTimeoutEnabled) passToSelect = &timeout;
  else passToSelect = NULL;

  while(1)
  {
    FD_ZERO(&readSet);
    FD_SET(sock, &readSet);
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    success = select(sock + 1, &readSet, NULL, NULL, passToSelect);
    if (success < 1)
    {
      cleanup();
      printf("TCP: error or timeout\n");
      return 0;  // error, or timeout
    }

    thisRead = read(sock, &buffer[bytesRead], totalBytes - bytesRead);
    if (!thisRead)
    {
      // if read returns 0 then connection is closed
      // in non-blocking mode if read is called with no data available, it returns -1
      // and sets errno to EGAGAIN. but we use select so it wouldn't do that anyway.
      cleanup();
      return 0;
    }
    bytesRead += thisRead;

    printf("Bytes read now: %u\n", bytesRead);
    if (bytesRead == totalBytes)
    {
      return 1;
    }
    else
    {
      if (++readTries == 100)
      {
        cleanup();
        printf("too many reads\n");
        return 0;
      }
    }
  }
}

int TCP::sendPacket(UCHAR* buf, size_t count)
{
  if (!connected) return 0;

  unsigned int bytesWritten = 0;
  int thisWrite;
  int writeTries = 0;
  int success;
  fd_set writeSet;
  struct timeval timeout;

  while(1)
  {
    FD_ZERO(&writeSet);
    FD_SET(sock, &writeSet);
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    success = select(sock + 1, NULL, &writeSet, NULL, &timeout);
    if (success < 1)
    {
      cleanup();
      printf("TCP: error or timeout\n");
      return 0;  // error, or timeout
    }

    thisWrite = write(sock, &buf[bytesWritten], count - bytesWritten);
    if (!thisWrite)
    {
      // if write returns 0 then connection is closed ?
      // in non-blocking mode if read is called with no data available, it returns -1
      // and sets errno to EGAGAIN. but we use select so it wouldn't do that anyway.
      cleanup();
      printf("Detected connection closed\n");
      return 0;
    }
    bytesWritten += thisWrite;

    printf("Bytes written now: %u\n", bytesWritten);
    if (bytesWritten == count)
    {
      return 1;
    }
    else
    {
      if (++writeTries == 100)
      {
        cleanup();
        printf("too many writes\n");
        return 0;
      }
    }
  }
}




void TCP::dump(unsigned char* data, USHORT size)
{
  printf("Size = %u\n", size);

  USHORT c = 0;
  while(c < size)
  {
    if ((size - c) > 15)
    {
      printf(" %02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X  %c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c\n",
        data[c], data[c+1], data[c+2], data[c+3], data[c+4], data[c+5], data[c+6], data[c+7],
        data[c+8], data[c+9], data[c+10], data[c+11], data[c+12], data[c+13], data[c+14], data[c+15],
        dcc(data[c]), dcc(data[c+1]), dcc(data[c+2]), dcc(data[c+3]), dcc(data[c+4]), dcc(data[c+5]), dcc(data[c+6]), dcc(data[c+7]),
        dcc(data[c+8]), dcc(data[c+9]), dcc(data[c+10]), dcc(data[c+11]), dcc(data[c+12]), dcc(data[c+13]), dcc(data[c+14]), dcc(data[c+15]));
      c += 16;
    }
    else
    {
      switch (size - c)
      {
        case 15:
          printf(" %02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X     %c%c%c%c%c%c%c%c%c%c%c%c%c%c%c\n",
            data[c], data[c+1], data[c+2], data[c+3], data[c+4], data[c+5], data[c+6], data[c+7],
            data[c+8], data[c+9], data[c+10], data[c+11], data[c+12], data[c+13], data[c+14],
            dcc(data[c]), dcc(data[c+1]), dcc(data[c+2]), dcc(data[c+3]), dcc(data[c+4]), dcc(data[c+5]), dcc(data[c+6]), dcc(data[c+7]),
            dcc(data[c+8]), dcc(data[c+9]), dcc(data[c+10]), dcc(data[c+11]), dcc(data[c+12]), dcc(data[c+13]), dcc(data[c+14]));
          c += 15;
          break;
        case 14:
          printf(" %02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X        %c%c%c%c%c%c%c%c%c%c%c%c%c%c\n",
            data[c], data[c+1], data[c+2], data[c+3], data[c+4], data[c+5], data[c+6], data[c+7],
            data[c+8], data[c+9], data[c+10], data[c+11], data[c+12], data[c+13],
            dcc(data[c]), dcc(data[c+1]), dcc(data[c+2]), dcc(data[c+3]), dcc(data[c+4]), dcc(data[c+5]), dcc(data[c+6]), dcc(data[c+7]),
            dcc(data[c+8]), dcc(data[c+9]), dcc(data[c+10]), dcc(data[c+11]), dcc(data[c+12]), dcc(data[c+13]));
          c += 14;
          break;
        case 13:
          printf(" %02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X  %02X           %c%c%c%c%c%c%c%c%c%c%c%c%c\n",
            data[c], data[c+1], data[c+2], data[c+3], data[c+4], data[c+5], data[c+6], data[c+7],
            data[c+8], data[c+9], data[c+10], data[c+11], data[c+12],
            dcc(data[c]), dcc(data[c+1]), dcc(data[c+2]), dcc(data[c+3]), dcc(data[c+4]), dcc(data[c+5]), dcc(data[c+6]), dcc(data[c+7]),
            dcc(data[c+8]), dcc(data[c+9]), dcc(data[c+10]), dcc(data[c+11]), dcc(data[c+12]));
          c += 13;
          break;
        case 12:
          printf(" %02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X               %c%c%c%c%c%c%c%c%c%c%c%c\n",
            data[c], data[c+1], data[c+2], data[c+3], data[c+4], data[c+5], data[c+6], data[c+7],
            data[c+8], data[c+9], data[c+10], data[c+11],
            dcc(data[c]), dcc(data[c+1]), dcc(data[c+2]), dcc(data[c+3]), dcc(data[c+4]), dcc(data[c+5]), dcc(data[c+6]), dcc(data[c+7]),
            dcc(data[c+8]), dcc(data[c+9]), dcc(data[c+10]), dcc(data[c+11]));
          c += 12;
          break;
        case 11:
          printf(" %02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X                  %c%c%c%c%c%c%c%c%c%c%c\n",
            data[c], data[c+1], data[c+2], data[c+3], data[c+4], data[c+5], data[c+6], data[c+7],
            data[c+8], data[c+9], data[c+10],
            dcc(data[c]), dcc(data[c+1]), dcc(data[c+2]), dcc(data[c+3]), dcc(data[c+4]), dcc(data[c+5]), dcc(data[c+6]), dcc(data[c+7]),
            dcc(data[c+8]), dcc(data[c+9]), dcc(data[c+10]));
          c += 11;
          break;
        case 10:
          printf(" %02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X                     %c%c%c%c%c%c%c%c%c%c\n",
            data[c], data[c+1], data[c+2], data[c+3], data[c+4], data[c+5], data[c+6], data[c+7],
            data[c+8], data[c+9],
            dcc(data[c]), dcc(data[c+1]), dcc(data[c+2]), dcc(data[c+3]), dcc(data[c+4]), dcc(data[c+5]), dcc(data[c+6]), dcc(data[c+7]),
            dcc(data[c+8]), dcc(data[c+9]));
          c += 10;
          break;
        case 9:
          printf(" %02X %02X %02X %02X  %02X %02X %02X %02X  %02X                        %c%c%c%c%c%c%c%c%c\n",
            data[c], data[c+1], data[c+2], data[c+3], data[c+4], data[c+5], data[c+6], data[c+7],
            data[c+8],
            dcc(data[c]), dcc(data[c+1]), dcc(data[c+2]), dcc(data[c+3]), dcc(data[c+4]), dcc(data[c+5]), dcc(data[c+6]), dcc(data[c+7]),
            dcc(data[c+8]));
          c += 9;
          break;
        case 8:
          printf(" %02X %02X %02X %02X  %02X %02X %02X %02X                            %c%c%c%c%c%c%c%c\n",
            data[c], data[c+1], data[c+2], data[c+3], data[c+4], data[c+5], data[c+6], data[c+7],
            dcc(data[c]), dcc(data[c+1]), dcc(data[c+2]), dcc(data[c+3]), dcc(data[c+4]), dcc(data[c+5]), dcc(data[c+6]), dcc(data[c+7]));
          c += 8;
          break;
        case 7:
          printf(" %02X %02X %02X %02X  %02X %02X %02X                               %c%c%c%c%c%c%c\n",
            data[c], data[c+1], data[c+2], data[c+3], data[c+4], data[c+5], data[c+6],
            dcc(data[c]), dcc(data[c+1]), dcc(data[c+2]), dcc(data[c+3]), dcc(data[c+4]), dcc(data[c+5]), dcc(data[c+6]));
          c += 7;
          break;
        case 6:
          printf(" %02X %02X %02X %02X  %02X %02X                                  %c%c%c%c%c%c\n",
            data[c], data[c+1], data[c+2], data[c+3], data[c+4], data[c+5],
            dcc(data[c]), dcc(data[c+1]), dcc(data[c+2]), dcc(data[c+3]), dcc(data[c+4]), dcc(data[c+5]));
          c += 6;
          break;
        case 5:
          printf(" %02X %02X %02X %02X  %02X                                     %c%c%c%c%c\n",
            data[c], data[c+1], data[c+2], data[c+3], data[c+4],
            dcc(data[c]), dcc(data[c+1]), dcc(data[c+2]), dcc(data[c+3]), dcc(data[c+4]));
          c += 5;
          break;
        case 4:
          printf(" %02X %02X %02X %02X                                         %c%c%c%c\n",
            data[c], data[c+1], data[c+2], data[c+3],
            dcc(data[c]), dcc(data[c+1]), dcc(data[c+2]), dcc(data[c+3]));
          c += 4;
          break;
        case 3:
          printf(" %02X %02X %02X                                            %c%c%c\n",
            data[c], data[c+1], data[c+2],
            dcc(data[c]), dcc(data[c+1]), dcc(data[c+2]));
          c += 3;
          break;
        case 2:
          printf(" %02X %02X                                               %c%c\n",
            data[c], data[c+1],
            dcc(data[c]), dcc(data[c+1]));
          c += 2;
          break;
        case 1:
          printf(" %02X                                                  %c\n",
            data[c],
            dcc(data[c]));
          c += 1;
          break;
      }
    }
  }
}

unsigned char TCP::dcc(UCHAR c)
{
  if (isspace(c)) return ' ';
  if (isprint(c)) return c;
  return '.';
}
