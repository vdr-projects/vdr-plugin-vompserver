/*
    Copyright 2004-2005 Chris Tallon, Andreas Vogel

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

#ifndef MEDIA_H
#define MEDIA_H

#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <endian.h>

#include <unistd.h> // sleep
#include <vector>

using namespace std;
//#include "defines.h"
//#include "tcp.h"
//#include "mvpreceiver.h"
//#include "recplayer.h"
#include "config.h"

//the following defines must be consisten to the client side
/* media types form a bitmask
   so you can add them to have > 1*/
#define MEDIA_TYPE_DIR 1
#define MEDIA_TYPE_AUDIO 2
#define MEDIA_TYPE_VIDEO 4
#define MEDIA_TYPE_PICTURE 8
#define MEDIA_TYPE_UNKNOWN 256

#define MEDIA_TYPE_ALL (1+2+4+8)

class Media 
{
  public:
    /**
      * create a media entry
      * filename will get copied
      */
    Media(int type, const char * filename, int time);
    ~Media();
    const char * getFilename();
    int getType();
    int getTime();

  private:
    char * _filename;
    int _type;
    int _time;


};

class MediaList : public vector<Media*> {
  public:
    static MediaList *readList(Config *cfg,const char * dirname,int type=MEDIA_TYPE_ALL);
    ~MediaList();
  };

#endif
