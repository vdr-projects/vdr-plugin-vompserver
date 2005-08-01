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

#include "udpreplier.h"

// undeclared function
void UDPReplierStartThread(void *arg)
{
  UDPReplier *m = (UDPReplier *)arg;
  m->run2();
}


UDPReplier::UDPReplier()
 : ds(3024)
{
  runThread = 0;
  running = 0;
}

UDPReplier::~UDPReplier()
{
  if (running) stop();
}

int UDPReplier::stop()
{
  if (!running) return 0;

  running = 0;
  pthread_cancel(runThread);
  pthread_join(runThread, NULL);

  return 1;
}

int UDPReplier::run()
{
  if (running) return 1;
  running = 1;
  if (pthread_create(&runThread, NULL, (void*(*)(void*))UDPReplierStartThread, (void *)this) == -1) return 0;
  Log::getInstance()->log("UDP", Log::DEBUG, "UDP replier started");
  return 1;
}

void UDPReplier::run2()
{
  // I don't want signals
  sigset_t sigset;
  sigfillset(&sigset);
  pthread_sigmask(SIG_BLOCK, &sigset, NULL);

  int retval;
  while(1)
  {
    retval = ds.waitforMessage(0);
    if (retval == 1) continue;

    if (!strcmp(ds.getData(), "VOMP CLIENT"))
      ds.send(ds.getFromIPA(), 3024, "VOMP SERVER", 11);
  }
}
