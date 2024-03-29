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
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#ifndef MVPSERVER_H
#define MVPSERVER_H

#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

#include "defines.h"
#include "udpreplier.h"
#include "udp6replier.h"
#include "mvprelay.h"
#include "bootpd.h"
#include "tftpd.h"
#include "vompclient.h"
#include "thread.h"
#include "config.h"

class MVPServer : public Thread
{
  public:
    MVPServer();
    virtual ~MVPServer();

    int run(char* configDir);
    int stop();

  private:
    void threadMethod();

    Log log;
    Config config;
    UDPReplier udpr;
    UDP6Replier udpr6;
    Bootpd bootpd;
    Tftpd tftpd;
    MVPRelay mvprelay;
    int listeningSocket;
    char* configDir;
    char* logoDir;
    char* imageDir;
    char* resourceDir;
    char* cacheDir;
    USHORT tcpServerPort;
};

#endif
