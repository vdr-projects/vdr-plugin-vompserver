/*
    Copyright 2004-2005 Chris Tallon, 2014 Marten Richter

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

#ifndef PICTUREREADER_H
#define PICTUREREADER_H

#include "defines.h"
#include "log.h"
#include "thread.h"
#include "tcp.h"
#include "thread.h"
#include "vompclient.h"
#include "services/scraper2vdr.h"
#include <queue>
#include <string>

struct TVMediaRequest{
   ULONG streamID;
   ULONG type;
   ULONG primary_id;
   ULONG secondary_id;
   ULONG type_pict;
   ULONG container;
   ULONG container_member;
   std::string primary_name;
};

class PictureReader : public Thread
{
  public:
    PictureReader(VompClient * client);
    virtual ~PictureReader();
    int init(TCP* tcp);
    void detachMVPReceiver();
    void addTVMediaRequest(TVMediaRequest&);
    bool epgImageExists(int event);

  private:

    std::string getPictName(TVMediaRequest&);

    Log* logger;
    int inittedOK;
    pthread_mutex_t pictureLock; // needs outside locking
    std::queue<TVMediaRequest> pictures;

    TCP* tcp;
    VompClient * x;
    cSeries series;
    cMovie movie;
    
  protected:
    void threadMethod();
};

#endif


