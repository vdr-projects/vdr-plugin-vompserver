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

#ifndef DSOCK6_H
#define DSOCK6_H

#include <sys/socket.h>
#include <sys/select.h>

#include "defines.h"
#include "log.h"

#define MAXBUFLEN 2000

class DatagramSocket6
{
  public:
    DatagramSocket6();
    ~DatagramSocket6();
    bool init(USHORT port);
    void shutdown();
    unsigned char waitforMessage(unsigned char); // int =0-block =1-new wait =2-continue wait
    void send(const char *, USHORT, char *, int); // send wants: IP Address ddn style, port,
                                            // data, length of data

    char*  getData()             { return buf; }      // returns a pointer to the data
    char*  getFromIPA()          { return fromIPA; }  // returns a pointer to from IP address
    USHORT getFromPort() const   { return fromPort; } // returns the sender port number
    int    getDataLength() const { return mlength; }  // returns data length

  private:
    Log* log;
    int initted;
    int socketnum;                  // Socket descriptor

    socklen_t addrlen;              // length of sockaddr struct
    char buf[MAXBUFLEN];            // main data buffer
    char fromIPA[40];               // from string (ip address)
    USHORT fromPort;                // which port user sent on
    int mlength;                    // length of message
    struct timeval tv;
    fd_set readfds;
};

#endif
