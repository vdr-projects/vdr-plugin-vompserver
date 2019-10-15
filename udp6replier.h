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

#ifndef UDP6REPLIER_H
#define UDP6REPLIER_H

#include "log.h"
#include "dsock6.h"
#include "thread.h"

class UDP6Replier : public Thread
{
  public:
    UDP6Replier();
    virtual ~UDP6Replier();

    int run(USHORT udpPort, char* serverName, USHORT serverPort);
    int shutdown();

  private:
    void threadMethod();

    DatagramSocket6 ds;
    char* message;
    int messageLen;
};

#endif
