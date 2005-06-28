/*
    Copyright 2004-2005 Chris Tallon

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

#include "dsock.h"

DatagramSocket::DatagramSocket(short port)
{
  myPort = port;
  addrlen = sizeof(struct sockaddr);

  if ((socketnum = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
  { perror("socket"); exit(1); }

  myAddr.sin_family = AF_INET;         // host byte order
  myAddr.sin_port = htons(myPort);     // short, network byte order
  myAddr.sin_addr.s_addr = INADDR_ANY; // auto-fill with my IP
  memset(&(myAddr.sin_zero), 0, 8);    // zero the rest of the struct
  if (bind(socketnum, (struct sockaddr *)&myAddr, addrlen) == -1)
  { perror("bind"); printf(" %s ", strerror(errno)); exit(1); }

  FD_ZERO(&readfds);
  FD_SET(socketnum, &readfds);
  tv.tv_sec = 0;
  tv.tv_usec = 0;
}

DatagramSocket::~DatagramSocket()
{
  close(socketnum);
}

unsigned char DatagramSocket::waitforMessage(unsigned char how)
{
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

  if (select(socketnum + 1, &readfds, NULL, NULL, passToSelect) <= 0)
  {  return 1;  }

  if ((mlength = recvfrom(socketnum, buf, MAXBUFLEN, 0,
      (struct sockaddr *)&theirAddr, &addrlen)) == -1)
  { perror("recvfrom"); return 0; }
  else
  {
    memset(&buf[mlength], 0, MAXBUFLEN - mlength);
    strcpy(fromIPA, inet_ntoa(theirAddr.sin_addr));
    fromPort = ntohs(theirAddr.sin_port);

    if (DSOCKDEBUG)
    {
      printf("%s:%i\tIN  %i\t", fromIPA, fromPort, mlength);
      int k;
      for(k = 0; k < mlength; k++)
        printf("%u ", (unsigned char)buf[k]);
      printf("\n");
    }
    return 2;
  }

  /* Return 0, failure
     Return 1, nothing happened, timer expired
     Return 2, packet arrived (timer not expired)
  */
}

int DatagramSocket::getDataLength(void) const
{
  return mlength;
}

char *DatagramSocket::getData(void)             {  return buf;  }
char *DatagramSocket::getFromIPA(void)          {  return fromIPA;  }
short DatagramSocket::getFromPort(void) const   {  return fromPort; }

void DatagramSocket::send(char *ipa, short port, char *message, int length)
{
  if (DSOCKDEBUG)
  {
    printf("%s:%i\tOUT %i\t", ipa, port, length);
    int k;
    uchar l;
    for (k = 0; k < length; k++)
      { l = (uchar)message[k]; printf("%u ", l); }
  }

  int sentLength = 0;

  theirAddr.sin_family = AF_INET;      // host byte order
  theirAddr.sin_port = htons(port);    // short, network byte order
  struct in_addr tad;                  // temp struct tad needed to pass to theirAddr.sin_addr
  tad.s_addr = inet_addr(ipa);
  theirAddr.sin_addr = tad;            // address
  memset(&(theirAddr.sin_zero), 0, 8); // zero the rest of the struct

  unsigned char crypt[MAXBUFLEN];
  memcpy(crypt, message, length);

  sentLength = sendto(socketnum, crypt, length, 0, (struct sockaddr *)&theirAddr, addrlen);
  if (sentLength == length)
  {
    printf(" GOOD\n");
  }
  else
  {
    printf(" --BAD--");  fflush(stdout);
    sentLength = sendto(socketnum, crypt, length, 0, (struct sockaddr *)&theirAddr, addrlen);
    if (sentLength == length)
      printf(" GOOD\n");
    else
    {
      printf(" -#-FAILED-#-\n");

      if (DSOCKDEBUG && (sentLength != length))
      {
        printf("--------------\n");
        printf("Sendto failure\n");
        printf("--------------\n");
        printf("%s:%i\tOUT %i %i ...\n", ipa, port, length, sentLength);
        perror("Perror reports");
        printf("errno value: %d\n", errno);
        printf("errno translated: %s\n", strerror(errno));
      //  printf("h_errno value: %d\n", h_errno);
      //  printf("\nActual address: %s\n", inet_ntoa(tad));
      //  printf("Actual port: %i\n", ntohs(theirAddr.sin_port));
        printf("continuing...\n\n");
      }
    }
  }
}

