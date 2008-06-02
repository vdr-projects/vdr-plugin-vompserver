/*
    Copyright 2008 Chris Tallon

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

#include <stdlib.h>

#ifndef VOMPSTANDALONE
#include <vdr/recording.h>
#include <vdr/channels.h>
#include <vdr/videodir.h>
#include <vdr/plugin.h>
#include <vdr/timers.h>
#include <vdr/menu.h>
#include "recplayer.h"
#include "mvpreceiver.h"
#endif

#include "vompclientrrproc.h"
#include "vompclient.h"
#include "log.h"
#include "media.h"
#include "i18n.h"

VompClientRRProc::VompClientRRProc(VompClient& x)
 : x(x)
{
  log = Log::getInstance();
  req = NULL;
  resp = NULL;
}

VompClientRRProc::~VompClientRRProc()
{
  threadStop();
}

bool VompClientRRProc::init()
{
  return threadStart();
}

bool VompClientRRProc::recvRequest(RequestPacket* newRequest)
{
  /*
     Accept a new request
     Currently only one at once is supported but this
     could be upgraded to a queueing system
  */

  threadLock();

  if (req)
  {
    log->log("RRProc", Log::ERR, "recvReq err 1");     
    threadUnlock();
    return false; 
  }

  req = newRequest;
  threadSignalNoLock();

  log->log("RRProc", Log::DEBUG, "recvReq set req and signalled");     
  
  threadUnlock();
  return true;
}

void VompClientRRProc::threadMethod()
{
  threadLock();
  log->log("RRProc", Log::DEBUG, "threadMethod startup");     

  while(1)  
  {
    if (req)
    {
      log->log("RRProc", Log::ERR, "threadMethod err 1");     
      threadUnlock();
      return;
    }
    
    log->log("RRProc", Log::DEBUG, "threadMethod waiting");     
    threadWaitForSignal();
    if (!req)
    {
      log->log("RRProc", Log::INFO, "threadMethod err 2 or quit");     
      threadUnlock();
      return;
    }
    
    log->log("RRProc", Log::DEBUG, "thread woken with req");     
  
    processPacket();
  }  
}

bool VompClientRRProc::processPacket()
{
  resp = new ResponsePacket();
  if (!resp->init(req->requestID))
  {
    log->log("RRProc", Log::ERR, "response packet init fail");     
    delete resp; 
    return false;
  }
    
  int result = 0;

  switch(req->opcode)
  {
    case 1:
      result = processLogin();
      break;
#ifndef VOMPSTANDALONE        
    case 2:
      result = processGetRecordingsList();
      break;
    case 3:
      result = processDeleteRecording();
      break;
    case 5:
      result = processGetChannelsList();
      break;
    case 6:
      result = processStartStreamingChannel();
      break;
    case 7:
      result = processGetBlock();
      break;
    case 8:
      result = processStopStreaming();
      break;
    case 9:
      result = processStartStreamingRecording();
      break;
#endif     
    case 10:
      result = processGetChannelSchedule();
      break;
    case 11:
      result = processConfigSave();
      break;
    case 12:
      result = processConfigLoad();
      break;
#ifndef VOMPSTANDALONE        
    case 13:
      result = processReScanRecording();         // FIXME obselete
      break;
    case 14:
      result = processGetTimers();
      break;
    case 15:
      result = processSetTimer();
      break;
    case 16:
      result = processPositionFromFrameNumber();
      break;
    case 17:
      result = processFrameNumberFromPosition();
      break;
    case 18:
      result = processMoveRecording();
      break;
    case 19:
      result = processGetIFrame();
      break;
    case 20:
      result = processGetRecInfo();
      break;
    case 21:
      result = processGetMarks();
      break;
    case 22:
      result = processGetChannelPids();
      break;
    case 23:
      result = processDeleteTimer();
      break;
#endif        
    case 30:
      result = processGetMediaList();
      break;
    case 31:
      result = processGetPicture();
      break;
    case 32:
      result = processGetImageBlock();
      break;
    case 33:
      result = processGetLanguageList();
      break;
    case 34:
      result = processGetLanguageContent();
      break;
  }

  delete resp;
  resp = NULL;
  
  if (req->data) free(req->data);
  delete req;
  req = NULL;
  
  if (result) return true;
  return false;
}

int VompClientRRProc::processLogin()
{
  if (req->dataLength != 6) return 0;

  // Open the config

  char configFileName[PATH_MAX];
  snprintf(configFileName, PATH_MAX, "%s/vomp-%02X-%02X-%02X-%02X-%02X-%02X.conf", x.configDir, req->data[0], req->data[1], req->data[2], req->data[3], req->data[4], req->data[5]);
  x.config.init(configFileName);

  // Send the login reply

  time_t timeNow = time(NULL);
  struct tm* timeStruct = localtime(&timeNow);
  int timeOffset = timeStruct->tm_gmtoff;

  resp->addULONG(timeNow);
  resp->addLONG(timeOffset);
  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());
  log->log("RRProc", Log::DEBUG, "written login reply len %lu", resp->getLen());
    
  x.loggedIn = true;
  return 1;
}

int VompClientRRProc::processConfigSave()
{
  char* section = (char*)req->data;
  char* key = NULL;
  char* value = NULL;

  for (UINT k = 0; k < req->dataLength; k++)
  {
    if (req->data[k] == '\0')
    {
      if (!key)
      {
        key = (char*)&req->data[k+1];
      }
      else
      {
        value = (char*)&req->data[k+1];
        break;
      }
    }
  }

  // if the last string (value) doesnt have null terminator, give up
  if (req->data[req->dataLength - 1] != '\0') return 0;

  log->log("RRProc", Log::DEBUG, "Config save: %s %s %s", section, key, value);
  if (x.config.setValueString(section, key, value))
  {
    resp->addULONG(1);
  }
  else
  {
    resp->addULONG(0);
  }

  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());
  
  return 1;
}

int VompClientRRProc::processConfigLoad()
{
  char* section = (char*)req->data;
  char* key = NULL;

  for (UINT k = 0; k < req->dataLength; k++)
  {
    if (req->data[k] == '\0')
    {
      key = (char*)&req->data[k+1];
      break;
    }
  }

  char* value = x.config.getValueString(section, key);

  if (value)
  {
    resp->addString(value);
    log->log("RRProc", Log::DEBUG, "Written config load packet");
    delete[] value;
  }
  else
  {
    resp->addULONG(0);
    log->log("RRProc", Log::DEBUG, "Written config load failed packet");
  }

  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());
  
  return 1;
}

/**
  * media List Request:
  * 4 length
  * 4 VDR_GETMEDIALIST
  * 4 flags (currently unused)
  * n dirname
  * n+1 0
  * Media List response:
  * 4 length
  * 4 VDR_
  * 4 numentries
  * per entry:
  * 4 media type
  * 4 time stamp
  * 4 flags
  * 4 strlen (incl. 0 Byte)
  * string
  * 0
*/

int VompClientRRProc::processGetMediaList()
{
  if (req->dataLength < 4) {
    log->log("RRProc", Log::ERR, "getMediaList packet too short %d", req->dataLength);
    return 0;
  }
  char * dirname=NULL;
  if (req->dataLength > 4) {
    //we have a dirname provided
    dirname=(char *)&req->data[4];
    log->log("RRProc", Log::DEBUG, "getMediaList for %s", dirname);
  }

  MediaList * ml=MediaList::readList(x.baseConfig, dirname);
  if (ml == NULL) {
     log->log("RRProc", Log::ERR, "getMediaList returned NULL");
     return 0;
  }

  //response code (not yet set)
  resp->addULONG(0);

  //numentries
  resp->addULONG(ml->size());

  for (MediaList::iterator nm=ml->begin(); nm<ml->end(); nm++)
  {
    Media *m=*nm;
    log->log("RRProc", Log::DEBUG, "found media entry %s, type=%d",m->getFilename(),m->getType());
    resp->addULONG(m->getType());
    //time stamp
    resp->addULONG(m->getTime());
    //flags
    resp->addULONG(0);
    int len=strlen(m->getFilename());
    //strlen
    resp->addULONG(len+1);
    resp->addString(m->getFilename());
  }
  delete ml;

  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());
  
  log->log("RRProc", Log::DEBUG, "Written Media list");
  return 1;
}

/**
  * get image Request:
  * 4 flags (currently unused)
  * 4 x size
  * 4 y size
  * n filename
  * n+1 0
  * get image response:
  * 4 length
  * 4 VDR_GETIMAGE
  * 4 len of image
*/

int VompClientRRProc::processGetPicture()
{
  if (req->dataLength < 12) {
    log->log("RRProc", Log::ERR, "getPicture packet too short %d", req->dataLength);
    return 0;
  }
  if (x.imageFile) {
    fclose(x.imageFile);
    x.imageFile=NULL;
  }
  char * filename=NULL;
  if (req->dataLength > 12) {
    //we have a dirname provided
    filename=(char *)&req->data[12];
    log->log("RRProc", Log::DEBUG, "getPicture  %s", filename);
  }
  else {
    log->log("RRProc", Log::ERR, "getPicture  empty filename");
  }
  if (filename) {
    x.imageFile=fopen(filename,"r");
    if (!x.imageFile) log->log("RRProc", Log::ERR, "getPicture unable to open %s",filename);
  }
  int size=0;
  if (x.imageFile) {
    struct stat st;
    if ( fstat(fileno(x.imageFile),&st) == 0) size=st.st_size;
  }
  //response code (not yet set)
  resp->addULONG(31);
  //size
  resp->addULONG(size);

  log->log("RRProc", Log::DEBUG, "getPicture size  %u", size);

  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());

  log->log("RRProc", Log::DEBUG, "Written getPicture");

  return 1;
}


int VompClientRRProc::processGetImageBlock()
{
  if (!x.imageFile)
  {
    log->log("RRProc", Log::DEBUG, "Get image block called when no image active");
    return 0;
  }

  UCHAR* data = req->data;

  ULLONG position = x.ntohll(*(ULLONG*)data);
  data += sizeof(ULLONG);
  ULONG amount = ntohl(*(ULONG*)data);

  log->log("RRProc", Log::DEBUG, "getImageblock pos = %llu length = %lu", position, amount);

  UCHAR sendBuffer[amount];
  ULONG amountReceived = 0; // compiler moan.
  ULLONG cpos=ftell(x.imageFile);
  if (position != cpos) {
    fseek(x.imageFile,position-cpos,SEEK_CUR);
  }
  if (position != (ULLONG)ftell(x.imageFile)) {
    log->log("RRProc", Log::DEBUG, "getImageblock pos = %llu not available", position);
  }
  else {
    amountReceived=fread(&sendBuffer[0],1,amount,x.imageFile);
  }

  if (!amountReceived)
  {
    resp->addULONG(0);
    log->log("RRProc", Log::DEBUG, "written 4(0) as getblock got 0");
  }
  else
  {
    resp->copyin(sendBuffer, amount);
    log->log("RRProc", Log::DEBUG, "written %lu", amountReceived);
  }
  
  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());

  return 1;
}


int VompClientRRProc::processGetLanguageList()
{
  x.i18n.findLanguages();
  const I18n::lang_code_list& languages = x.i18n.getLanguageList();
  std::string result;
  I18n::lang_code_list::const_iterator iter;
  for (iter = languages.begin(); iter != languages.end(); ++iter)
  {
    resp->addString(iter->first.c_str());
    resp->addString(iter->second.c_str());
  }
  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());
  return 1;
}

int VompClientRRProc::processGetLanguageContent()
{
  if (req->dataLength <= 0) return 0;
  std::string code, result;
  code.assign((char*)req->data, req->dataLength - 1);
  x.i18n.findLanguages();
  I18n::trans_table texts = x.i18n.getLanguageContent(code);
  I18n::trans_table::const_iterator iter;
  for (iter = texts.begin(); iter != texts.end(); ++iter)
  {
    resp->addString(iter->first.c_str());
    resp->addString(iter->second.c_str());
  }
  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());
  return 1;
}

#ifndef VOMPSTANDALONE

int VompClientRRProc::processGetRecordingsList()
{
  int FreeMB;
  int Percent = VideoDiskSpace(&FreeMB);
  int Total = (FreeMB / (100 - Percent)) * 100;
  
  resp->addULONG(Total);
  resp->addULONG(FreeMB);
  resp->addULONG(Percent);

  cRecordings Recordings;
  Recordings.Load();

  for (cRecording *recording = Recordings.First(); recording; recording = Recordings.Next(recording))
  {
    resp->addULONG(recording->start);
    resp->addString(recording->Name());
    resp->addString(recording->FileName());
  }

  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());
  
  log->log("RRProc", Log::DEBUG, "Written recordings list");

  return 1;
}

int VompClientRRProc::processDeleteRecording()
{
  // data is a pointer to the fileName string

  cRecordings Recordings;
  Recordings.Load(); // probably have to do this

  cRecording* recording = Recordings.GetByName((char*)req->data);

  log->log("RRProc", Log::DEBUG, "recording pointer %p", recording);

  if (recording)
  {
    log->log("RRProc", Log::DEBUG, "deleting recording: %s", recording->Name());

    cRecordControl *rc = cRecordControls::GetRecordControl(recording->FileName());
    if (!rc)
    {
      if (recording->Delete())
      {
        // Copy svdrp's way of doing this, see if it works
#if VDRVERSNUM > 10300
        ::Recordings.DelByName(recording->FileName());
#endif
        resp->addULONG(1);
      }
      else
      {
        resp->addULONG(2);
      }
    }
    else
    {
      resp->addULONG(3);
    }
  }
  else
  {
    resp->addULONG(4);
  }

  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());
  
  return 1;
}

int VompClientRRProc::processMoveRecording()
{
  log->log("RRProc", Log::DEBUG, "Process move recording");
  char* fileName = (char*)req->data;
  char* newPath = NULL;

  for (UINT k = 0; k < req->dataLength; k++)
  {
    if (req->data[k] == '\0')
    {
      newPath = (char*)&req->data[k+1];
      break;
    }
  }
  if (!newPath) return 0;

  cRecordings Recordings;
  Recordings.Load(); // probably have to do this

  cRecording* recording = Recordings.GetByName((char*)fileName);

  log->log("RRProc", Log::DEBUG, "recording pointer %p", recording);

  if (recording)
  {
    cRecordControl *rc = cRecordControls::GetRecordControl(recording->FileName());
    if (!rc)
    {
      log->log("RRProc", Log::DEBUG, "moving recording: %s", recording->Name());
      log->log("RRProc", Log::DEBUG, "moving recording: %s", recording->FileName());
      log->log("RRProc", Log::DEBUG, "to: %s", newPath);

      const char* t = recording->FileName();

      char* dateDirName = NULL;   int k;
      char* titleDirName = NULL;  int j;

      // Find the datedirname
      for(k = strlen(t) - 1; k >= 0; k--)
      {
        if (t[k] == '/')
        {
          log->log("RRProc", Log::DEBUG, "l1: %i", strlen(&t[k+1]) + 1);
          dateDirName = new char[strlen(&t[k+1]) + 1];
          strcpy(dateDirName, &t[k+1]);
          break;
        }
      }

      // Find the titledirname

      for(j = k-1; j >= 0; j--)
      {
        if (t[j] == '/')
        {
          log->log("RRProc", Log::DEBUG, "l2: %i", (k - j - 1) + 1);
          titleDirName = new char[(k - j - 1) + 1];
          memcpy(titleDirName, &t[j+1], k - j - 1);
          titleDirName[k - j - 1] = '\0';
          break;
        }
      }

      log->log("RRProc", Log::DEBUG, "datedirname: %s", dateDirName);
      log->log("RRProc", Log::DEBUG, "titledirname: %s", titleDirName);
      log->log("RRProc", Log::DEBUG, "viddir: %s", VideoDirectory);

      char* newPathConv = new char[strlen(newPath)+1];
      strcpy(newPathConv, newPath);
      ExchangeChars(newPathConv, true);
      log->log("RRProc", Log::DEBUG, "EC: %s", newPathConv);

      char* newContainer = new char[strlen(VideoDirectory) + strlen(newPathConv) + strlen(titleDirName) + 1];
      log->log("RRProc", Log::DEBUG, "l10: %i", strlen(VideoDirectory) + strlen(newPathConv) + strlen(titleDirName) + 1);
      sprintf(newContainer, "%s%s%s", VideoDirectory, newPathConv, titleDirName);
      delete[] newPathConv;

      log->log("RRProc", Log::DEBUG, "%s", newContainer);

      struct stat dstat;
      int statret = stat(newContainer, &dstat);
      if ((statret == -1) && (errno == ENOENT)) // Dir does not exist
      {
        log->log("RRProc", Log::DEBUG, "new dir does not exist");
        int mkdirret = mkdir(newContainer, 0755);
        if (mkdirret != 0)
        {
          delete[] dateDirName;
          delete[] titleDirName;
          delete[] newContainer;

          resp->addULONG(5);          
          resp->finalise();
          x.tcp.sendPacket(resp->getPtr(), resp->getLen());
          return 1;
        }
      }
      else if ((statret == 0) && (! (dstat.st_mode && S_IFDIR))) // Something exists but it's not a dir
      {
        delete[] dateDirName;
        delete[] titleDirName;
        delete[] newContainer;

        resp->addULONG(5);          
        resp->finalise();
        x.tcp.sendPacket(resp->getPtr(), resp->getLen());
        return 1;
      }

      // Ok, the directory container has been made, or it pre-existed.

      char* newDir = new char[strlen(newContainer) + 1 + strlen(dateDirName) + 1];
      sprintf(newDir, "%s/%s", newContainer, dateDirName);

      log->log("RRProc", Log::DEBUG, "doing rename '%s' '%s'", t, newDir);
      int renameret = rename(t, newDir);
      if (renameret == 0)
      {
        // Success. Test for remove old dir containter
        char* oldTitleDir = new char[k+1];
        memcpy(oldTitleDir, t, k);
        oldTitleDir[k] = '\0';
        log->log("RRProc", Log::DEBUG, "len: %i, cp: %i, strlen: %i, oldtitledir: %s", k+1, k, strlen(oldTitleDir), oldTitleDir);
        rmdir(oldTitleDir); // can't do anything about a fail result at this point.
        delete[] oldTitleDir;
      }

      if (renameret == 0)
      {
#if VDRVERSNUM > 10311
        // Tell VDR
        ::Recordings.Update();
#endif
        // Success. Send a different packet from just a ulong
        resp->addULONG(1); // success
        resp->addString(newDir);
      }
      else
      {
        resp->addULONG(5);          
      }

      resp->finalise();
      x.tcp.sendPacket(resp->getPtr(), resp->getLen());

      delete[] dateDirName;
      delete[] titleDirName;
      delete[] newContainer;
      delete[] newDir;
    }
    else
    {
      resp->addULONG(3);          
      resp->finalise();
      x.tcp.sendPacket(resp->getPtr(), resp->getLen());
    }
  }
  else
  {
    resp->addULONG(4);          
    resp->finalise();
    x.tcp.sendPacket(resp->getPtr(), resp->getLen());
  }

  return 1;
}

int VompClientRRProc::processGetChannelsList()
{
  ULONG type;

  char* chanConfig = x.config.getValueString("General", "Channels");
  int allChans = 1;
  if (chanConfig) allChans = strcasecmp(chanConfig, "FTA only");

  for (cChannel *channel = Channels.First(); channel; channel = Channels.Next(channel))
  {
#if VDRVERSNUM < 10300
    if (!channel->GroupSep() && (!channel->Ca() || allChans))
#else
    if (!channel->GroupSep() && (!channel->Ca(0) || allChans))
#endif
    {
      log->log("RRProc", Log::DEBUG, "name: '%s'", channel->Name());

      if (channel->Vpid()) type = 1;
#if VDRVERSNUM < 10300
      else type = 2;
#else
      else if (channel->Apid(0)) type = 2;
      else continue;
#endif

      resp->addULONG(channel->Number());
      resp->addULONG(type);      
      resp->addString(channel->Name());
    }
  }

  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());

  log->log("RRProc", Log::DEBUG, "Written channels list");

  return 1;
}

int VompClientRRProc::processGetChannelPids()
{
  ULONG channelNumber = ntohl(*(ULONG*)req->data);

  cChannel* channel = x.channelFromNumber(channelNumber);
  if (!channel)
  {
    resp->addULONG(0);
    resp->finalise();
    x.tcp.sendPacket(resp->getPtr(), resp->getLen());
    return 1;
  }

  ULONG numApids = 0;
  ULONG numDpids = 0;
  ULONG numSpids = 0;


#if VDRVERSNUM < 10300

  log->log("RRProc", Log::DEBUG, "Apid1: %i", channel->Apid1());
  log->log("RRProc", Log::DEBUG, "Apid2: %i", channel->Apid2());

  if (channel->Apid2())
    numApids = 2;
  else if (channel->Apid1())
    numApids = 1;
  else
    numApids = 0;

#else

  for (const int *Apid = channel->Apids(); *Apid; Apid++)
  {
    numApids++;
  }
  for (const int *Dpid = channel->Dpids(); *Dpid; Dpid++)
  {
    numDpids++;
  }
  for (const int *Spid = channel->Spids(); *Spid; Spid++)
  {
    numSpids++;
  }
#endif


  // Format of response
  // vpid
  // number of apids
  // {
  //    apid
  //    lang string
  // }
  // number of dpids
  // {
  //    dpid
  //    lang string
  // }
  // number of spids
  // {
  //    spid
  //    lang string
  // }
  // tpid

  resp->addULONG(channel->Vpid());
  resp->addULONG(numApids);

#if VDRVERSNUM < 10300
  if (numApids >= 1)
  {
    resp->addULONG(channel->Apid1());
    resp->addString("");
  }
  if (numApids == 2)
  {
    resp->addULONG(channel->Apid2());
    resp->addString("");
  }
  resp->addULONG(0);
  resp->addULONG(0); 
#else
  for (ULONG i = 0; i < numApids; i++)
  {
    resp->addULONG(channel->Apid(i));
    resp->addString(channel->Alang(i));
  }
  resp->addULONG(numDpids);
  for (ULONG i = 0; i < numDpids; i++)
  {
    resp->addULONG(channel->Dpid(i));
    resp->addString(channel->Dlang(i));
  }
  resp->addULONG(numSpids);
  for (ULONG i = 0; i < numSpids; i++)
  {
    resp->addULONG(channel->Spid(i));
    resp->addString(channel->Slang(i));
  }
#endif
  resp->addULONG(channel->Tpid());


  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());
  
  log->log("RRProc", Log::DEBUG, "Written channels pids");

  return 1;
}

int VompClientRRProc::processStartStreamingChannel()
{
  if (x.lp)
  {
    log->log("RRProc", Log::ERR, "Client called start streaming twice");
    return 0;
  }
  
  log->log("RRProc", Log::DEBUG, "req->dataLength = %i", req->dataLength);
  ULONG channelNumber = ntohl(*(ULONG*)req->data);

  cChannel* channel = x.channelFromNumber(channelNumber);
  if (!channel)
  {
    resp->addULONG(0);
    resp->finalise();
    x.tcp.sendPacket(resp->getPtr(), resp->getLen());
    return 1;
  }

  // get the priority we should use
  int fail = 1;
  int priority = x.config.getValueLong("General", "Live priority", &fail);
  if (!fail)
  {
    log->log("RRProc", Log::DEBUG, "Config: Live TV priority: %i", priority);
  }
  else
  {
    log->log("RRProc", Log::DEBUG, "Config: Live TV priority config fail");
    priority = 0;
  }

  // a bit of sanity..
  if (priority < 0) priority = 0;
  if (priority > 99) priority = 99;

  log->log("RRProc", Log::DEBUG, "Using live TV priority %i", priority);
  x.lp = MVPReceiver::create(channel, priority);

  if (!x.lp)
  {
    resp->addULONG(0);
    resp->finalise();
    x.tcp.sendPacket(resp->getPtr(), resp->getLen());
    return 1;
  }

  if (!x.lp->init(&x.tcp, req->requestID))
  {
    delete x.lp;
    x.lp = NULL;
    resp->addULONG(0);
    resp->finalise();
    x.tcp.sendPacket(resp->getPtr(), resp->getLen());
    return 1;
  }

  resp->addULONG(1);
  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());
  return 1;
}

int VompClientRRProc::processStopStreaming()
{
  log->log("RRProc", Log::DEBUG, "STOP STREAMING RECEIVED");
  if (x.lp)
  {
    delete x.lp;
    x.lp = NULL;
  }
  else if (x.recplayer)
  {
    x.writeResumeData();

    delete x.recplayer;
    delete x.recordingManager;
    x.recplayer = NULL;
    x.recordingManager = NULL;
  }

  resp->addULONG(1);
  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());
  return 1;
}

int VompClientRRProc::processGetBlock()
{
  if (!x.lp && !x.recplayer)
  {
    log->log("RRProc", Log::DEBUG, "Get block called when no streaming happening!");
    return 0;
  }

  UCHAR* data = req->data;

  ULLONG position = x.ntohll(*(ULLONG*)data);
  data += sizeof(ULLONG);
  ULONG amount = ntohl(*(ULONG*)data);

  log->log("RRProc", Log::DEBUG, "getblock pos = %llu length = %lu", position, amount);

  UCHAR sendBuffer[amount];
  ULONG amountReceived = 0; // compiler moan.
  if (x.lp)
  {
    log->log("RRProc", Log::DEBUG, "getting from live");
    amountReceived = x.lp->getBlock(&sendBuffer[0], amount);

    if (!amountReceived)
    {
      // vdr has possibly disconnected the receiver
      log->log("RRProc", Log::DEBUG, "VDR has disconnected the live receiver");
      delete x.lp;
      x.lp = NULL;
    }
  }
  else if (x.recplayer)
  {
    log->log("RRProc", Log::DEBUG, "getting from recording");
    amountReceived = x.recplayer->getBlock(&sendBuffer[0], position, amount);
  }

  if (!amountReceived)
  {
    resp->addULONG(0);
    log->log("RRProc", Log::DEBUG, "written 4(0) as getblock got 0");
  }
  else
  {
    resp->copyin(sendBuffer, amountReceived);
    log->log("RRProc", Log::DEBUG, "written %lu", amountReceived);
  }

  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());
  log->log("RRProc", Log::DEBUG, "Finished getblock, have sent %lu", resp->getLen());
  return 1;
}

int VompClientRRProc::processStartStreamingRecording()
{
  // data is a pointer to the fileName string

  x.recordingManager = new cRecordings;
  x.recordingManager->Load();

  cRecording* recording = x.recordingManager->GetByName((char*)req->data);

  log->log("RRProc", Log::DEBUG, "recording pointer %p", recording);

  if (recording)
  {
    x.recplayer = new RecPlayer(recording);

    resp->addULLONG(x.recplayer->getLengthBytes());
    resp->addULONG(x.recplayer->getLengthFrames());
    resp->finalise();
    x.tcp.sendPacket(resp->getPtr(), resp->getLen());
    
    log->log("RRProc", Log::DEBUG, "written totalLength");
  }
  else
  {
    delete x.recordingManager;
    x.recordingManager = NULL;
  }
  return 1;
}

int VompClientRRProc::processPositionFromFrameNumber()
{
  ULLONG retval = 0;

  ULONG frameNumber = ntohl(*(ULONG*)req->data);

  if (!x.recplayer)
  {
    log->log("RRProc", Log::DEBUG, "Rescan recording called when no recording being played!");
  }
  else
  {
    retval = x.recplayer->positionFromFrameNumber(frameNumber);
  }

  resp->addULLONG(retval);
  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());

  log->log("RRProc", Log::DEBUG, "Wrote posFromFrameNum reply to client");
  return 1;
}

int VompClientRRProc::processFrameNumberFromPosition()
{
  ULONG retval = 0;

  ULLONG position = x.ntohll(*(ULLONG*)req->data);

  if (!x.recplayer)
  {
    log->log("RRProc", Log::DEBUG, "Rescan recording called when no recording being played!");
  }
  else
  {
    retval = x.recplayer->frameNumberFromPosition(position);
  }

  resp->addULONG(retval);
  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());

  log->log("RRProc", Log::DEBUG, "Wrote frameNumFromPos reply to client");
  return 1;
}

int VompClientRRProc::processGetIFrame()
{
  bool success = false;

  ULONG* data = (ULONG*)req->data;

  ULONG frameNumber = ntohl(*data);
  data++;
  ULONG direction = ntohl(*data);

  ULLONG rfilePosition = 0;
  ULONG rframeNumber = 0;
  ULONG rframeLength = 0;

  if (!x.recplayer)
  {
    log->log("RRProc", Log::DEBUG, "GetIFrame recording called when no recording being played!");
  }
  else
  {
    success = x.recplayer->getNextIFrame(frameNumber, direction, &rfilePosition, &rframeNumber, &rframeLength);
  }

  // returns file position, frame number, length

  if (success)
  {
    resp->addULLONG(rfilePosition);
    resp->addULONG(rframeNumber);
    resp->addULONG(rframeLength);
  }
  else
  {
    resp->addULONG(0);
  }

  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());
  
  log->log("RRProc", Log::DEBUG, "Wrote GNIF reply to client %llu %lu %lu", rfilePosition, rframeNumber, rframeLength);
  return 1;
}

int VompClientRRProc::processGetChannelSchedule()
{
  ULONG* data = (ULONG*)req->data;

  ULONG channelNumber = ntohl(*data);
  data++;
  ULONG startTime = ntohl(*data);
  data++;
  ULONG duration = ntohl(*data);

  log->log("RRProc", Log::DEBUG, "get schedule called for channel %lu", channelNumber);

  cChannel* channel = x.channelFromNumber(channelNumber);
  if (!channel)
  {
    resp->addULONG(0);
    resp->finalise();
    x.tcp.sendPacket(resp->getPtr(), resp->getLen());
  
    log->log("RRProc", Log::DEBUG, "written 0 because channel = NULL");
    return 1;
  }

  log->log("RRProc", Log::DEBUG, "Got channel");

#if VDRVERSNUM < 10300
  cMutexLock MutexLock;
  const cSchedules *Schedules = cSIProcessor::Schedules(MutexLock);
#else
  cSchedulesLock MutexLock;
  const cSchedules *Schedules = cSchedules::Schedules(MutexLock);
#endif
  if (!Schedules)
  {
    resp->addULONG(0);
    resp->finalise();
    x.tcp.sendPacket(resp->getPtr(), resp->getLen());
    
    log->log("RRProc", Log::DEBUG, "written 0 because Schedule!s! = NULL");
    return 1;
  }

  log->log("RRProc", Log::DEBUG, "Got schedule!s! object");

  const cSchedule *Schedule = Schedules->GetSchedule(channel->GetChannelID());
  if (!Schedule)
  {
    resp->addULONG(0);
    resp->finalise();
    x.tcp.sendPacket(resp->getPtr(), resp->getLen());
    
    log->log("RRProc", Log::DEBUG, "written 0 because Schedule = NULL");
    return 1;
  }

  log->log("RRProc", Log::DEBUG, "Got schedule object");

  const char* empty = "";
  bool atLeastOneEvent = false;

  ULONG thisEventID;
  ULONG thisEventTime;
  ULONG thisEventDuration;
  const char* thisEventTitle;
  const char* thisEventSubTitle;
  const char* thisEventDescription;

#if VDRVERSNUM < 10300

  const cEventInfo *event;
  for (int eventNumber = 0; eventNumber < Schedule->NumEvents(); eventNumber++)
  {
    event = Schedule->GetEventNumber(eventNumber);

    thisEventID = event->GetEventID();
    thisEventTime = event->GetTime();
    thisEventDuration = event->GetDuration();
    thisEventTitle = event->GetTitle();
    thisEventSubTitle = event->GetSubtitle();
    thisEventDescription = event->GetExtendedDescription();

#else

  for (const cEvent* event = Schedule->Events()->First(); event; event = Schedule->Events()->Next(event))
  {
    thisEventID = event->EventID();
    thisEventTime = event->StartTime();
    thisEventDuration = event->Duration();
    thisEventTitle = event->Title();
    thisEventSubTitle = NULL;
    thisEventDescription = event->Description();

#endif

    //in the past filter
    if ((thisEventTime + thisEventDuration) < (ULONG)time(NULL)) continue;

    //start time filter
    if ((thisEventTime + thisEventDuration) <= startTime) continue;

    //duration filter
    if (thisEventTime >= (startTime + duration)) continue;

    if (!thisEventTitle) thisEventTitle = empty;
    if (!thisEventSubTitle) thisEventSubTitle = empty;
    if (!thisEventDescription) thisEventDescription = empty;

    resp->addULONG(thisEventID);
    resp->addULONG(thisEventTime);
    resp->addULONG(thisEventDuration);

    resp->addString(thisEventTitle);
    resp->addString(thisEventSubTitle);
    resp->addString(thisEventDescription);

    atLeastOneEvent = true;
  }

  log->log("RRProc", Log::DEBUG, "Got all event data");

  if (!atLeastOneEvent)
  {
    resp->addULONG(0);
    log->log("RRProc", Log::DEBUG, "Written 0 because no data");
  }
  
  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());
    
  log->log("RRProc", Log::DEBUG, "written schedules packet");

  return 1;
}

int VompClientRRProc::processGetTimers()
{
  cTimer *timer;
  int numTimers = Timers.Count();

  resp->addULONG(numTimers);

  for (int i = 0; i < numTimers; i++)
  {
    timer = Timers.Get(i);

#if VDRVERSNUM < 10300
    resp->addULONG(timer->Active());
#else
    resp->addULONG(timer->HasFlags(tfActive));
#endif
    resp->addULONG(timer->Recording());
    resp->addULONG(timer->Pending());
    resp->addULONG(timer->Priority());
    resp->addULONG(timer->Lifetime());
    resp->addULONG(timer->Channel()->Number());
    resp->addULONG(timer->StartTime());
    resp->addULONG(timer->StopTime());
    resp->addULONG(timer->Day());
    resp->addULONG(timer->WeekDays());
    resp->addString(timer->File());
  }

  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());
  
  log->log("RRProc", Log::DEBUG, "Written timers list");

  return 1;
}

int VompClientRRProc::processSetTimer()
{
  char* timerString = new char[strlen((char*)req->data) + 1];
  strcpy(timerString, (char*)req->data);

#if VDRVERSNUM < 10300

  // If this is VDR 1.2 the date part of the timer string must be reduced
  // to just DD rather than YYYY-MM-DD

  int s = 0; // source
  int d = 0; // destination
  int c = 0; // count
  while(c != 2) // copy up to date section, including the second ':'
  {
    timerString[d] = req->data[s];
    if (req->data[s] == ':') c++;
    ++s;
    ++d;
  }
  // now it has copied up to the date section
  c = 0;
  while(c != 2) // waste YYYY-MM-
  {
    if (req->data[s] == '-') c++;
    ++s;
  }
  // now source is at the DD
  memcpy(&timerString[d], &req->data[s], req->dataLength - s);
  d += req->dataLength - s;
  timerString[d] = '\0';

  log->log("RRProc", Log::DEBUG, "Timer string after 1.2 conversion:");
  log->log("RRProc", Log::DEBUG, "%s", timerString);

#endif

  cTimer *timer = new cTimer;
  if (timer->Parse((char*)timerString))
  {
    cTimer *t = Timers.GetTimer(timer);
    if (!t)
    {
      Timers.Add(timer);
#if VDRVERSNUM < 10300
      Timers.Save();
#else
      Timers.SetModified();
#endif
      resp->addULONG(0);
      resp->finalise();
      x.tcp.sendPacket(resp->getPtr(), resp->getLen());
      return 1;
    }
    else
    {
      resp->addULONG(1);
      resp->finalise();
      x.tcp.sendPacket(resp->getPtr(), resp->getLen());
    }
  }
  else
  {
    resp->addULONG(2);
    resp->finalise();
    x.tcp.sendPacket(resp->getPtr(), resp->getLen());
  }
  delete timer;
  return 1;
}

int VompClientRRProc::processDeleteTimer()
{
  log->log("RRProc", Log::DEBUG, "Delete timer called");
  // get timer
  
  int position = 0;
  
  INT delChannel = ntohl(*(ULONG*)&req->data[position]); position += 4;
  INT delWeekdays = ntohl(*(ULONG*)&req->data[position]); position += 4;
  INT delDay = ntohl(*(ULONG*)&req->data[position]); position += 4;  
  INT delStart = ntohl(*(ULONG*)&req->data[position]); position += 4;  
  INT delStop = ntohl(*(ULONG*)&req->data[position]); position += 4;
    
  cTimer* ti = NULL;
  for (ti = Timers.First(); ti; ti = Timers.Next(ti))
  {
    if  ( (ti->Channel()->Number() == delChannel)
     &&   ((ti->WeekDays() && (ti->WeekDays() == delWeekdays)) || (!ti->WeekDays() && (ti->Day() == delDay)))
     &&   (ti->StartTime() == delStart)
     &&   (ti->StopTime() == delStop) )
       break;
  }
  
  if (!ti)
  {
    resp->addULONG(4);
    resp->finalise();
    x.tcp.sendPacket(resp->getPtr(), resp->getLen());
    return 1;
  }
          
  if (!Timers.BeingEdited())
  {
    if (!ti->Recording())
    {
      Timers.Del(ti);
      Timers.SetModified();
      resp->addULONG(10);
      resp->finalise();
      x.tcp.sendPacket(resp->getPtr(), resp->getLen());
      return 1;
    }
    else
    {
      log->log("RRProc", Log::ERR, "Unable to delete timer - timer is running");
      resp->addULONG(3);
      resp->finalise();
      x.tcp.sendPacket(resp->getPtr(), resp->getLen());
      return 1;
    }  
  }
  else
  {
    log->log("RRProc", Log::ERR, "Unable to delete timer - timers being edited at VDR");
    resp->addULONG(1);
    resp->finalise();
    x.tcp.sendPacket(resp->getPtr(), resp->getLen());
    return 1;
  }  
}

int VompClientRRProc::processGetRecInfo()
{
  // data is a pointer to the fileName string

  cRecordings Recordings;
  Recordings.Load(); // probably have to do this

  cRecording *recording = Recordings.GetByName((char*)req->data);

  time_t timerStart = 0;
  time_t timerStop = 0;
  char* summary = NULL;
  ULONG resumePoint = 0;

  if (!recording)
  {
    log->log("RRProc", Log::ERR, "GetRecInfo found no recording");
    resp->addULONG(0);
    resp->finalise();
    x.tcp.sendPacket(resp->getPtr(), resp->getLen());
    return 1;
  }

  /* Return packet:
  4 bytes: start time for timer
  4 bytes: end time for timer
  4 bytes: resume point
  string: summary
  4 bytes: num components
  {
    1 byte: stream
    1 byte: type
    string: language
    string: description
  }

  */

  // Get current timer

  cRecordControl *rc = cRecordControls::GetRecordControl(recording->FileName());
  if (rc)
  {
    timerStart = rc->Timer()->StartTime();
    timerStop = rc->Timer()->StopTime();
    log->log("RRProc", Log::DEBUG, "GRI: RC: %lu %lu", timerStart, timerStop);
  }

  resp->addULONG(timerStart);
  resp->addULONG(timerStop);

  // Get resume point

  char* value = x.config.getValueString("ResumeData", (char*)req->data);
  if (value)
  {
    resumePoint = strtoul(value, NULL, 10);
    delete[] value;
  }
  log->log("RRProc", Log::DEBUG, "GRI: RP: %lu", resumePoint);

  resp->addULONG(resumePoint);


  // Get summary

#if VDRVERSNUM < 10300
  summary = (char*)recording->Summary();
#else
  const cRecordingInfo *Info = recording->Info();
  summary = (char*)Info->ShortText();
  if (isempty(summary)) summary = (char*)Info->Description();
#endif
  log->log("RRProc", Log::DEBUG, "GRI: S: %s", summary);
  if (summary)
  {
    resp->addString(summary);
  }
  else
  {
    resp->addString("");
  }

  // Get channels

#if VDRVERSNUM < 10300

  // Send 0 for numchannels - this signals the client this info is not available
  resp->addULONG(0);

#else
  const cComponents* components = Info->Components();

  log->log("RRProc", Log::DEBUG, "GRI: D1: %p", components);

  if (!components)
  {
    resp->addULONG(0);
  }
  else
  {
    resp->addULONG(components->NumComponents());
  
    tComponent* component;
    for (int i = 0; i < components->NumComponents(); i++)
    {
      component = components->Component(i);

      log->log("RRProc", Log::DEBUG, "GRI: C: %i %u %u %s %s", i, component->stream, component->type, component->language, component->description);
      
      resp->addUCHAR(component->stream);
      resp->addUCHAR(component->type);

      if (component->language)
      {
        resp->addString(component->language);
      }
      else
      {
        resp->addString("");
      }
      if (component->description)
      {
        resp->addString(component->description);
      }
      else
      {
        resp->addString("");
      }
    }
  }

#endif

  // Done. send it

  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());

  log->log("RRProc", Log::DEBUG, "Written getrecinfo");

  return 1;
}




// FIXME obselete

int VompClientRRProc::processReScanRecording()
{
  if (!x.recplayer)
  {
    log->log("RRProc", Log::DEBUG, "Rescan recording called when no recording being played!");
    return 0;
  }

  x.recplayer->scan();

  resp->addULLONG(x.recplayer->getLengthBytes());
  resp->addULONG(x.recplayer->getLengthFrames());
  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());
  log->log("RRProc", Log::DEBUG, "Rescan recording, wrote new length to client");
  return 1;
}

// FIXME without client calling rescan, getblock wont work even tho more data is avail

int VompClientRRProc::processGetMarks()
{
  // data is a pointer to the fileName string

  cMarks Marks;
  cRecordings Recordings;
  Recordings.Load(); // probably have to do this

  cRecording *recording = Recordings.GetByName((char*)req->data);

  log->log("RRProc", Log::DEBUG, "recording pointer %p", recording);

  if (recording)
  {
    Marks.Load(recording->FileName());
    if (Marks.Count())
    {
      for (const cMark *m = Marks.First(); m; m = Marks.Next(m))
      {
        log->log("RRProc", Log::DEBUG, "found Mark %i", m->position);

        resp->addULONG(m->position);
      }
    }
    else
    {
      log->log("RRProc", Log::DEBUG, "no marks found, sending 0-mark");
      resp->addULONG(0);
    }
  }

  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());
  
  log->log("RRProc", Log::DEBUG, "Written Marks list");

  return 1;
}

#endif // !VOMPSTANDALONE


