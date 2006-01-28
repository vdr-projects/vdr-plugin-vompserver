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

#ifndef DSOCK_H
#define DSOCK_H

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "log.h"

#define MAXBUFLEN 2000
const char DSOCKDEBUG = 0;
typedef unsigned char uchar;

class DatagramSocket
{
  public:
    DatagramSocket();
    ~DatagramSocket();
    bool init(short);                 // port
    unsigned char waitforMessage(unsigned char); // int =0-block =1-new wait =2-continue wait
    int getDataLength(void) const;
    char *getData(void);               // returns a pointer to the data
    char *getFromIPA(void);            // returns a pointer to from IP address
    short getFromPort(void) const;
    void send(char *, short, char *, int); // send wants: IP Address ddn style, port,
                                           // data, length of data
  private:
    Log* log;
    int initted;
    int socketnum;                  // Socket descriptor
    short myPort;                   // My port number
    struct sockaddr_in myAddr;      // My address
    struct sockaddr_in theirAddr;   // User address
    socklen_t addrlen;              // length of sockaddr struct
    char buf[MAXBUFLEN];            // main data buffer
    char fromIPA[20];               // from string (ip address)
    short fromPort;                 // which port user sent on
    int mlength;                    // length of message
    struct timeval tv;
    fd_set readfds;
};

#endif
