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

#ifndef UDPREPLIER_H
#define UDPREPLIER_H

#include <stdio.h>
#include <pthread.h>
#include <signal.h>

#include "dsock.h"

class UDPReplier
{
  public:
    UDPReplier();
    ~UDPReplier();

    int run();
    int stop();

    // not for external use
    void run2();

  private:
    pthread_t runThread;
    int running;

    DatagramSocket ds;
};

#endif
