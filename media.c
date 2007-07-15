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

#include "media.h"
//#include "tools.h"
#include <sys/stat.h>
#include <sys/types.h>



Media::Media(int type, const char * filename, int time){
  _filename=NULL;
  if (filename) {
    int len=strlen(filename)+1;
    _filename=new char[len];
    strcpy(_filename,filename);
    }
  _type=type;
  _time=time;
  };

Media::~Media(){
  delete _filename;
  _filename=NULL;
  };

const char * Media::getFilename() {
  return _filename;
  }

int Media::getType() {
  return _type;
  }
int Media::getTime() {
  return _time;
  }


static struct mtype{
   const char* extension;
   int type;
   } mediatypes[]= {
     {".mp3",MEDIA_TYPE_AUDIO},
     {".MP3",MEDIA_TYPE_AUDIO},
     {".jpg",MEDIA_TYPE_PICTURE},
     {".JPG",MEDIA_TYPE_PICTURE},
     {".jpeg",MEDIA_TYPE_PICTURE},
     {".JPEG",MEDIA_TYPE_PICTURE}
     };
//#define NUMTYPES (sizeof(mediatypes)/sizeof(mtype))
#define NUMTYPES 6

MediaList * MediaList::readList(Config * cfg,const char * dirname, int type) {
  MediaList *rt=NULL;
  if (dirname == NULL) {
    //the configured Media List
    //for the moment up to 10 entries [Media] Dir.1 ...Dir.10
    for (int nr=1;nr<=10;nr++){
      char buffer[20];
      sprintf(buffer,"Dir.%d",nr);
      const char * dn=cfg->getValueString("Media",buffer);
      if (dn != NULL) {
        if (rt == NULL) rt=new MediaList();
        Media *m=new Media(MEDIA_TYPE_DIR,dn,0);
        rt->Add(m);
        Log::getInstance()->log("Media",Log::DEBUG,"added base dir %s",dn);
       }
     }
   }
  if (rt != NULL) return rt;
  //if nothing is configured, we start at /
  if (dirname == NULL) dirname="/";
  rt=new MediaList();
  //open the directory and read out the entries
  cReadDir d(dirname);
  struct dirent *e;
  while ((e=d.Next()) != NULL) {
    {
    const char * fname=e->d_name;
    if ( fname == NULL) continue;
    if (strcmp(fname,".") == 0) continue;
    char *buffer;
    asprintf(&buffer, "%s/%s", dirname, e->d_name);
    struct stat st;
    int mtype=MEDIA_TYPE_UNKNOWN;
    if (stat(buffer, &st) == 0) {
      if (S_ISDIR(st.st_mode)) {
         mtype=MEDIA_TYPE_DIR;
   }
      else {
         for (int i=0;i<NUMTYPES;i++) {
            if (endswith(fname,mediatypes[i].extension)) {
              mtype=mediatypes[i].type;
              break;
              }
           }
         }
      }
    free(buffer);
    //only consider entries we accept by type here
    if (mtype & type) {
     Media * m =new Media(mtype,fname,(int)(st.st_mtime));
     rt->Add(m);
     Log::getInstance()->log("Media",Log::DEBUG,"added entry %s, type=%d",fname,mtype);
     }
    }
  }
  //test
  //Media *m=new Media(MEDIA_TYPE_DIR,"testMedia1",0);
  //rt->Add(m);
  return rt;
  }



