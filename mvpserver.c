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

#include "mvpserver.h"

// undeclared function
void MVPServerStartThread(void *arg)
{
  MVPServer *m = (MVPServer *)arg;
  m->run2();
}


MVPServer::MVPServer()
{
  runThread = 0;
  running = 0;
}

MVPServer::~MVPServer()
{
  if (running) stop();
}

int MVPServer::stop()
{
  if (!running) return 0;

  udpr.stop();

  pthread_cancel(runThread);
  pthread_join(runThread, NULL);

  close(listeningSocket);

  return 1;
}

int MVPServer::run()
{
  if (running) return 1;

//  logger.init(Log::DEBUG, "/tmp/vompserver.log");

  if (udpr.run() == 0) return 0;

  if (pthread_create(&runThread, NULL, (void*(*)(void*))MVPServerStartThread, (void *)this) == -1) return 0;
  printf("MVPServer run success\n");
  return 1;
}

void MVPServer::run2()
{
  // Thread stuff
  // I don't want signals and I want to die as soon as I am cancelled because I'll be in accept()
  sigset_t sigset;
  sigfillset(&sigset);
  pthread_sigmask(SIG_BLOCK, &sigset, NULL);
  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

  struct sockaddr_in address;
  address.sin_family = AF_INET;
  address.sin_port = htons(3024);
  address.sin_addr.s_addr = INADDR_ANY;
  socklen_t length = sizeof(address);

  listeningSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (listeningSocket < 0)
  {
    printf("Could not get TCP socket in vompserver\n");
    return;
  }

  int value=1;
  setsockopt(listeningSocket,SOL_SOCKET,SO_REUSEADDR,&value,sizeof(value));

  if (bind(listeningSocket,(struct sockaddr *)&address,sizeof(address)) < 0)
  {
    printf("Could not bind to socket in vompserver\n");
    close(listeningSocket);
    return;
  }

  listen(listeningSocket, 5);

  int clientSocket;

  while(1)
  {
    clientSocket = accept(listeningSocket,(struct sockaddr *)&address, &length);
    MVPClient* m = new MVPClient(clientSocket);
    m->run();
  }
}


ULLONG ntohll(ULLONG a)
{
  return htonll(a);
}

ULLONG htonll(ULLONG a)
{
  #if BYTE_ORDER == BIG_ENDIAN
    return a;
  #else
    ULLONG b = 0;

    b = ((a << 56) & 0xFF00000000000000ULL)
      | ((a << 40) & 0x00FF000000000000ULL)
      | ((a << 24) & 0x0000FF0000000000ULL)
      | ((a <<  8) & 0x000000FF00000000ULL)
      | ((a >>  8) & 0x00000000FF000000ULL)
      | ((a >> 24) & 0x0000000000FF0000ULL)
      | ((a >> 40) & 0x000000000000FF00ULL)
      | ((a >> 56) & 0x00000000000000FFULL) ;

    return b;
  #endif
}
