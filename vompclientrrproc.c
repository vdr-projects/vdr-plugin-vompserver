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
#include <vdr/remote.h>
#include "recplayer.h"
#include "mvpreceiver.h"
#include "services/scraper2vdr.h"
#endif

#include "vompclientrrproc.h"
#include "vompclient.h"
#include "log.h"
#include "media.h"
#include "mediaplayer.h"
#include "servermediafile.h"
#include "i18n.h"
#include "vdrcommand.h"
#include "picturereader.h"

bool ResumeIDLock;

ULONG VompClientRRProc::VOMP_PROTOCOL_VERSION_MIN = 0x00000301;
ULONG VompClientRRProc::VOMP_PROTOCOL_VERSION_MAX = 0x00000400;
// format is aabbccdd
// cc is release protocol version, increase with every release, that changes protocol
// dd is development protocol version, set to zero at every release, 
// increase for every protocol change in git
// bb not equal zero should indicate a non loggytronic protocol
// aa is reserved for future use
// VOMP_PROTOCOL_VERSION_MIN is the protocol version minimal supported by the server
// VOMP_PROTOCOL_VERSION_MAX is the protocol version maximal supported by the server
// This allows to run older clients from a new server
// Increase the minimal protocol version everytime you break compatibility for a certain 
// command. 


/* Locking information from VDR:

  + Instead of directly accessing the global variables Timers, Channels or Recordings,
    they need to set up a cStateKey variable and call the proper getter function,
    as in
      cStateKey StateKey;
      if (const cTimers *Timers = cTimers::GetTimersRead(StateKey)) {
         // access the timers
         StateKey.Remove();
         }
    and
      cStateKey StateKey;
      if (cTimers *Timers = cTimers::GetTimersWrite(StateKey)) {
         // access the timers
         StateKey.Remove();
         }
    See timers.h, thread.h and tools.h for details on this new locking mechanism.
  + There are convenience macros for easily accessing these lists without having
    to explicitly set up a cStateKey and calling its Remove() function. These macros
    have the form LOCK_*_READ/WRITE (with '*' being TIMERS, CHANNELS, SCHEDULES or
    RECORDINGS). Simply put such a macro before the point where you need to access
    the respective list, and there will be a pointer named Timers, Channels, Schedules
    or Recordings, respectively, which is valid until the end of the current block.
  + If a plugin needs to access several of the global lists in parallel, locking must
    always be done in the sequence Timers, Channels, Recordings, Schedules. This is
    necessary to make sure that different threads that need to lock several lists at
    the same time don't end up in a deadlock.

    */

// TODO: Use VDRs recording->ChangeName(option)) for move recording ?

ULONG VompClientRRProc::getProtocolVersionMin()
{
  return VOMP_PROTOCOL_VERSION_MIN;
}

ULONG VompClientRRProc::getProtocolVersionMax()
{
  return VOMP_PROTOCOL_VERSION_MAX;
}

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
  int a = threadStart();
  sleep(1);
  return a;
}

bool VompClientRRProc::recvRequest(RequestPacket* newRequest)
{
  /*
     Accept a new request
     Now we have a queue system is used,
     since on rare occasion the client fire two request at once
     e.g. heavily channel switching 
     then processing only a single request would cause a deadlock in the client
     Marten
  */

  log->log("RRProc", Log::DEBUG, "recvReq");
  threadLock();
  req_queue.push(newRequest);
  threadSignalNoLock();
  log->log("RRProc", Log::DEBUG, "recvReq set req and signalled");     
  threadUnlock();

  return true;
}

void VompClientRRProc::threadMethod()
{
  threadLock();
  log->log("RRProc", Log::DEBUG, "threadMethod startup");     

  if (req_queue.size() != 0)
  {
/*
-    log->log("RRProc", Log::ERR, "threadMethod err 1");     
-    threadUnlock();
-    return;

That was how the code used to be.

TODO: Work out why this happens.
*/  
  
    log->log("RRProc", Log::ERR, "threadMethod startup with already queued packets");     
    while (req_queue.size()) 
    {
      //log->log("RRProc", Log::DEBUG, "thread while");
      req = req_queue.front();
      req_queue.pop();
      
      threadUnlock(); // allow recvRequest to be queuing packets while we are working on this one
      
      if (!processPacket())
      {
        log->log("RRProc", Log::ERR, "processPacket exited with fail");     
        return;
      }
      
      threadLock();
    } 
    log->log("RRProc", Log::ERR, "threadMethod startup with already queued packets done.");     

  }
    
  while(1)  
  {
    log->log("RRProc", Log::DEBUG, "threadMethod waiting");     
    threadWaitForSignal();  // unlocks, waits, relocks
    if (req_queue.size() == 0)
    {
      log->log("RRProc", Log::INFO, "threadMethod err 2 or quit");     
      threadUnlock();
      return;
    }
    
    // signalled with something in queue
    
    log->log("RRProc", Log::DEBUG, "thread woken with req, queue size: %i", req_queue.size());

    while (req_queue.size()) 
    {
      //log->log("RRProc", Log::DEBUG, "thread while");
      req = req_queue.front();
      req_queue.pop();
      
      threadUnlock(); // allow recvRequest to be queuing packets while we are working on this one
      
      if (!processPacket())
      {
        log->log("RRProc", Log::ERR, "processPacket exited with fail");     
        return;
      }
      
      threadLock();
    } 
    
    // locked and run out of packets to process
  }  
}

bool VompClientRRProc::processPacket()
{
  resp = new ResponsePacket();
  if (!resp->init(req->requestID))
  {
    log->log("RRProc", Log::ERR, "response packet init fail");     
    delete resp; 
    
    if (req->data) free(req->data);
    delete req;
    req = NULL;

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
    case 10:
      result = processGetChannelSchedule();
      break;
#endif     
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
    case 666:
      result = processVDRShutdown();
      break;
    case VDR_GETRECSCRAPEREVENTTYPE:
      result = processGetRecScraperEventType();
    break;
    case VDR_GETSCRAPERMOVIEINFO:
      result = processGetScraperMovieInfo();
    break;
    case VDR_GETSCRAPERSERIESINFO:
      result = processGetScraperSeriesInfo();
    break;
    case VDR_LOADTVMEDIA:
      result = processLoadTvMedia();
    break;
    case VDR_LOADTVMEDIARECTHUMB:
      result = processLoadTvMediaRecThumb();
    break;
    case VDR_GETEVENTSCRAPEREVENTTYPE:
      result = processGetEventScraperEventType();
    break;
    case VDR_LOADTVMEDIAEVENTTHUMB:
      result = processLoadTvMediaEventThumb();
    break;
    case VDR_LOADCHANNELLOGO:
      result = processLoadChannelLogo();
    break;
#endif
    case VDR_GETMEDIALIST:
      result = processGetMediaList();
      break;
    case VDR_OPENMEDIA:
      result = processOpenMedia();
      break;
    case VDR_GETMEDIABLOCK:
      result = processGetMediaBlock();
      break;
    case 33:
      result = processGetLanguageList();
      break;
    case 34:
      result = processGetLanguageContent();
      break;
    case VDR_GETMEDIAINFO:
	    result = processGetMediaInfo();
	    break;
    case VDR_CLOSECHANNEL:
	    result = processCloseMediaChannel();
	    break;
    case 37:
      result = processSetCharset();
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
  resp->addULONG(VOMP_PROTOCOL_VERSION_MIN);
  resp->addULONG(VOMP_PROTOCOL_VERSION_MAX);
  
  // also send information about languages
  resp->addULONG(I18nLanguages()->Size());
  resp->addLONG(Setup.DisplaySubtitles);
  for (int i=0;i < I18nLanguages()->Size(); i++) {
    resp->addLONG(Setup.AudioLanguages[i]);
    resp->addLONG(Setup.SubtitleLanguages[i]);
    resp->addString(I18nLanguageCode(i));
  }
  
  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());
  log->log("RRProc", Log::DEBUG, "written login reply len %lu", resp->getLen());
    
  x.loggedIn = true;
  x.netLog(); // safe to run here since the client won't start net logging for a while yet
  
  return 1;
}

int VompClientRRProc::processSetCharset()
{
  int charset = ntohl(*(ULONG*)req->data);
  if (charset>0 && charset<3)
  {
    log->log("RRProc", Log::DEBUG, "Set charset to %d", charset);
    x.setCharset(charset);
    resp->addULONG(1);
  }
  else
  {
    log->log("RRProc", Log::DEBUG, "Invalid charset %d", charset);
    resp->addULONG(0);
  }
  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());
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
    resp->addString(value);//client coding, do not touch
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


//helper for sending from a serialize buffer
//insert the used len into the first 4 Bytes of the buffer
void VompClientRRProc::sendPacket(SerializeBuffer *b) {
  resp->copyin(b->getStart(),b->getCurrent()-b->getStart());
  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());
}

/**
  * media List Request:
  * Media List response:
  * flags, mediaList
*/
#define MLISTBUF 500000
int VompClientRRProc::processGetMediaList()
{
  SerializeBuffer buffer(req->data,req->dataLength);
  MediaURI uri(0,NULL,NULL);
  VDR_GetMediaListRequest request(&uri);
  if (request.deserialize(&buffer) != 0) {
    log->log("Client", Log::ERR, "getMediaList unable to deserialize");
    return 0;
  }
  const char *dirname=uri.getName();
  log->log("Client", Log::DEBUG, "getMediaList for %s", dirname);

  MediaList * ml=NULL;
  if (dirname == NULL) {
    ml=x.media->getRootList();
  } else {
    ml=x.media->getMediaList(&uri);
  }
  if (ml == NULL) {
     log->log("Client", Log::ERR, "getMediaList returned NULL");
     return 0;
  }
  SerializeBuffer rbuf(MLISTBUF,false,true);
  ULONG flags=0; //TODO: real error handling by setting flags
  VDR_GetMediaListResponse response(&flags,ml);
  if (response.serialize(&rbuf) != 0) {
     log->log("Client", Log::ERR, "getMediaList returned NULL");
     delete ml;
     return 0;
  }
  log->log("Client", Log::DEBUG, "getMediaList size  %u", ml->size());
  delete ml;
  

  sendPacket(&rbuf);
  log->log("Client", Log::DEBUG, "Written Media list");
  return 1;
}
/**
  * openMedia Request:
  * openMedia response:
*/
int VompClientRRProc::processOpenMedia()
{
  SerializeBuffer buffer(req->data,req->dataLength);
  MediaURI uri(0,NULL,NULL);
  ULONG channel=0;
  ULONG xs=0;
  ULONG ys=0;
  VDR_OpenMediumRequest request(&channel,&uri,&xs,&ys);
  if (request.deserialize(&buffer) != 0) {
    log->log("Client", Log::ERR, "openMediaRequest unable to deserialize");
    return 0;
  }
  const char *name=uri.getName();
  log->log("Client", Log::DEBUG, "openMediaRequest for %s", name);
  ULLONG size=0;
  int rt=x.media->openMedium(channel,&uri,&size,xs,ys);
  ULONG flags=0;
  if (rt != 0) {
    size=0;
    flags=1;
    log->log("Client", Log::ERR, "openMediaRequest unable to open");
  }
  VDR_OpenMediumResponse response(&flags,&size);
  SerializeBuffer rbuf(response.getSerializedLen()+4,false,true);
  if (response.serialize(&rbuf) != 0) {
     log->log("Client", Log::ERR, "openMediaRequest cannot serialize");
     return 0;
  }
  log->log("Client", Log::DEBUG, "openMediaRequest size  %llu", size);
  sendPacket(&rbuf);
  return 1;
}
/**
  * VDR_GETMEDIABLOCK
  * resp
  * packet - no serialized response!
  */
int VompClientRRProc::processGetMediaBlock()
{
  SerializeBuffer buffer(req->data,req->dataLength);
  ULLONG position = 0;
  ULONG amount = 0;
  ULONG channel = 0;
  VDR_GetMediaBlockRequest request(&channel,&position,&amount);
  if (request.deserialize(&buffer) != 0) {
    log->log("Client", Log::ERR, "getMediaBlock unable to deserialize");
    return 0;
  }
  log->log("Client", Log::DEBUG, "getMediaBlock pos = %llu length = %lu,chan=%lu", position, amount,channel);

  UCHAR sendBuffer[amount ];
  ULONG amountReceived = 0; 
  UCHAR *rbuf=sendBuffer;
  int rt=x.media->getMediaBlock(channel,position,amount,&amountReceived,&rbuf);
  if (!amountReceived || rt != 0)
  {
    log->log("Client", Log::DEBUG, "written 4(0) as getblock got 0");
  }
  else
  {
    if (rbuf != sendBuffer) {
      //the provider did not use the optimized handling with using my buffer
      resp->copyin(rbuf,amountReceived);
      free(rbuf);
    } else {
      // the provider did not allocate a new buffer
      resp->copyin(sendBuffer,amountReceived);
    }
  }
  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());
  log->log("Client", Log::DEBUG, "written ok %lu", amountReceived);
  return 1;
}
/**
  * VDR_GETMEDIAINFO
  */

int VompClientRRProc::processGetMediaInfo()
{
  SerializeBuffer buffer(req->data,req->dataLength);
  ULONG channel=0;
  VDR_GetMediaInfoRequest request(&channel);
  if (request.deserialize(&buffer) != 0) {
    log->log("Client", Log::ERR, "getMediaInfo unable to deserialize");
    return 0;
  }
  log->log("Client", Log::DEBUG, "getMediaInfo chan=%lu", channel);
  ULONG flags=0;
  MediaInfo mi;
  int rt=x.media->getMediaInfo(channel,&mi);
  if (rt != 0) {
    flags=1;
    log->log("Client", Log::ERR, "getMediaInfo unable to get");
  }
  VDR_GetMediaInfoResponse response(&flags,&mi);
  SerializeBuffer rbuf(response.getSerializedLen()+4,false,true);
  if (response.serialize(&rbuf) != 0) {
     log->log("Client", Log::ERR, "getMediaInfo cannot serialize");
     return 0;
  }
  sendPacket(&rbuf);
  return 1;
}

/**
  * VDR_CLOSECHANNEL
  */


int VompClientRRProc::processCloseMediaChannel()
{
  SerializeBuffer buffer(req->data,req->dataLength);
  ULONG channel=0;
  VDR_CloseMediaChannelRequest request(&channel);
  if (request.deserialize(&buffer) != 0) {
    log->log("Client", Log::ERR, "closeMediaChannel unable to deserialize");
    return 0;
  }
  ULONG flags=0;
  log->log("Client", Log::DEBUG, "closeMediaChannel chan=%lu", channel);
  int rt=x.media->closeMediaChannel(channel);
  if (rt != 0) {
    flags=1;
    log->log("Client", Log::ERR, "closeMediaChannel unable to get");
  }
  VDR_CloseMediaChannelResponse response(&flags);
  SerializeBuffer rbuf(response.getSerializedLen()+4,false,true);
  if (response.serialize(&rbuf) != 0) {
     log->log("Client", Log::ERR, "closeMediaChannel cannot serialize");
     return 0;
  }
  sendPacket(&rbuf);
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
    resp->addString(iter->first.c_str()); // Source code is acsii
    resp->addString(x.charconvutf8->Convert(iter->second.c_str())); //translate string can be any utf-8 character
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
    resp->addString(iter->first.c_str());// source code is acsii since it is english
    resp->addString(x.charconvutf8->Convert(iter->second.c_str())); // translate text can be any unicode string, it is stored as UTF-8
  }
  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());
  return 1;
}

#ifndef VOMPSTANDALONE

int VompClientRRProc::processGetRecordingsList()
{
  int FreeMB;
#if APIVERSNUM > 20101
  int Percent = cVideoDirectory::VideoDiskSpace(&FreeMB);
#else
  int Percent = VideoDiskSpace(&FreeMB);
#endif
  int Total = (FreeMB / (100 - Percent)) * 100;
  
  resp->addULONG(Total);
  resp->addULONG(FreeMB);
  resp->addULONG(Percent);

#if VDRVERSNUM >= 20301
  LOCK_RECORDINGS_READ;
  const cRecordings* tRecordings = Recordings;
#else
  cThreadLock RecordingsLock(&Recordings);
  const cRecordings* tRecordings = &Recordings;
#endif

  for (const cRecording *recording = tRecordings->First(); recording; recording = tRecordings->Next(recording))
  {
#if VDRVERSNUM < 10721
    resp->addULONG(recording->start);
#else
    resp->addULONG(recording->Start());
#endif
    resp->addUCHAR(recording->IsNew() ? 1 : 0);
    resp->addString(x.charconvsys->Convert(recording->Name())); //coding of recording name is system dependent
    resp->addString(recording->FileName());//file name are not  visible by user do not touch
  }

  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());
  
  log->log("RRProc", Log::DEBUG, "Written recordings list");

  return 1;
}

int VompClientRRProc::processDeleteRecording()
{
  // data is a pointer to the fileName string

#if VDRVERSNUM >= 20301
  LOCK_RECORDINGS_WRITE;
  cRecordings* tRecordings = Recordings;
#else
  cThreadLock RecordingsLock(&Recordings);
  cRecordings* tRecordings = &Recordings;
#endif

  cRecording* recording = tRecordings->GetByName((char*)req->data);

  log->log("RRProc", Log::DEBUG, "recording pointer %p", recording);

  if (recording)
  {
    log->log("RRProc", Log::DEBUG, "deleting recording: %s", recording->Name());

// TODO: Switch to using: cRecording::IsInUse(void) const
    cRecordControl *rc = cRecordControls::GetRecordControl(recording->FileName());
    if (!rc)
    {
      if (recording->Delete())
      {
#if VDRVERSNUM >= 20301
         tRecordings->DelByName(recording->FileName());
         tRecordings->SetModified();
#elif VDRVERSNUM > 10300
        // Copy svdrp's way of doing this, see if it works
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


#if VDRVERSNUM >= 20301
  LOCK_RECORDINGS_WRITE;
  cRecordings* tRecordings = Recordings;
#else
  cThreadLock RecordingsLock(&Recordings);
  cRecordings* tRecordings = &Recordings;
#endif

  cRecording* recording = tRecordings->GetByName((char*)fileName);

  log->log("RRProc", Log::DEBUG, "recording pointer %p", recording);

  if (recording)
  {
    // TODO: Switch to using: int cRecording::IsInUse(void) const
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
#if APIVERSNUM > 20101
      log->log("RRProc", Log::DEBUG, "viddir: %s", cVideoDirectory::Name());
#else
      log->log("RRProc", Log::DEBUG, "viddir: %s", VideoDirectory);
#endif

      char* newPathConv = new char[strlen(newPath)+1];
      strcpy(newPathConv, newPath);
      ExchangeChars(newPathConv, true);
      log->log("RRProc", Log::DEBUG, "EC: %s", newPathConv);

#if APIVERSNUM > 20101
      char* newContainer = new char[strlen(cVideoDirectory::Name()) + strlen(newPathConv) + strlen(titleDirName) + 1];
      log->log("RRProc", Log::DEBUG, "l10: %i", strlen(cVideoDirectory::Name()) + strlen(newPathConv) + strlen(titleDirName) + 1);
      sprintf(newContainer, "%s%s%s", cVideoDirectory::Name(), newPathConv, titleDirName);
#else
      char* newContainer = new char[strlen(VideoDirectory) + strlen(newPathConv) + strlen(titleDirName) + 1];
      log->log("RRProc", Log::DEBUG, "l10: %i", strlen(VideoDirectory) + strlen(newPathConv) + strlen(titleDirName) + 1);
      sprintf(newContainer, "%s%s%s", VideoDirectory, newPathConv, titleDirName);
#endif
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

#if VDRVERSNUM >= 20301
        tRecordings->SetModified();
#elif VDRVERSNUM > 10311
        ::Recordings.Update();
#endif
        // Success. Send a different packet from just a ulong
        resp->addULONG(1); // success
        resp->addString(newDir); //system depent do not convert
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

#if VDRVERSNUM >= 20301
  LOCK_CHANNELS_READ;
  const cChannels* tChannels = Channels;
#else
  const cChannels* tChannels = &Channels;
#endif

  for (const cChannel *channel = tChannels->First(); channel; channel = tChannels->Next(channel))
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
      resp->addString(x.charconvsys->Convert(channel->Name()));
#if VDRVERSNUM < 10703
      resp->addULONG(2);
#else
      resp->addULONG(channel->Vtype());
#endif      
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

#if VDRVERSNUM >= 20301
  LOCK_CHANNELS_READ;
  const cChannels* tChannels = Channels;
#else
  cChannels* tChannels = &Channels;
#endif

  const cChannel* channel = tChannels->GetByNumber(channelNumber);
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
#if VDRVERSNUM < 10703
  resp->addULONG(2);
#else
  resp->addULONG(channel->Vtype());
#endif
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
    resp->addString(x.charconvsys->Convert(channel->Alang(i)));
  }
  resp->addULONG(numDpids);
  for (ULONG i = 0; i < numDpids; i++)
  {
    resp->addULONG(channel->Dpid(i));
    resp->addString(x.charconvsys->Convert(channel->Dlang(i)));
  }
  resp->addULONG(numSpids);
  for (ULONG i = 0; i < numSpids; i++)
  {
    resp->addULONG(channel->Spid(i));
    resp->addString(x.charconvsys->Convert(channel->Slang(i)));
  }
#endif
  resp->addULONG(channel->Tpid());
  // Format of extended response, for compatibility with older client at the end
  // {
  //    atypes
  // }
  // {
  //    dtypes
  // }
  // {
  //    stypes
  //    comppageid
  //    ancpageids
  // }
#if VDRVERSNUM < 10300
  if (numApids >= 1)
  {
    resp->addULONG(4);
  }
  if (numApids == 2)
  {
    resp->addULONG(4);
  }
#else
  for (ULONG i = 0; i < numApids; i++)
  {
#if VDRVERSNUM < 10715
    resp->addULONG(4);
#else
    resp->addULONG(channel->Atype(i));
#endif
  }
  for (ULONG i = 0; i < numDpids; i++)
  {
#if VDRVERSNUM < 10715
    resp->addULONG(0x6A /*AC3*/);
#else
    resp->addULONG(channel->Dtype(i));
#endif
  }
  for (ULONG i = 0; i < numSpids; i++)
  {
#if VDRVERSNUM < 10715
    resp->addULONG(0);
    resp->addULONG(0);
    resp->addULONG(0);
#else
    resp->addULONG(channel->SubtitlingType(i));
    resp->addULONG(channel->CompositionPageId(i));
    resp->addULONG(channel->AncillaryPageId(i));
#endif
  }
#endif


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

#if VDRVERSNUM >= 20301
  LOCK_CHANNELS_READ;
  const cChannels* tChannels = Channels;
#else
  cChannels* tChannels = &Channels;
#endif

  const cChannel* channel = tChannels->GetByNumber(channelNumber);
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
#if VDRVERSNUM < 10725
  if (priority < 0) priority = 0;
#else 
  if (priority < -99) priority = -99;
#endif
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
    x.lp->detachMVPReceiver();
    delete x.lp;
    x.lp = NULL;
  }
  else if (x.recplayer)
  {
    x.writeResumeData();

    delete x.recplayer;
    x.recplayer = NULL;
  }

  resp->addULONG(1);
  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());
  return 1;
}

int VompClientRRProc::processGetBlock()
{
  if (x.lp)
  {
    log->log("RRProc", Log::ERR, "Get block called during live streaming");
    return 0;
  }

  if (!x.recplayer)
  {
    log->log("RRProc", Log::ERR, "Get block called when no recording open");
    return 0;
  }

  UCHAR* data = req->data;

  ULLONG position = x.ntohll(*(ULLONG*)data);
  data += sizeof(ULLONG);
  ULONG amount = ntohl(*(ULONG*)data);

  log->log("RRProc", Log::DEBUG, "getblock pos = %llu length = %lu", position, amount);

  UCHAR sendBuffer[amount];
  ULONG amountReceived = x.recplayer->getBlock(&sendBuffer[0], position, amount);

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

#if VDRVERSNUM >= 20301
  LOCK_RECORDINGS_READ;
  const cRecordings* tRecordings = Recordings;
#else
  cThreadLock RecordingsLock(&Recordings);
  cRecordings* tRecordings = &Recordings;
#endif

  const cRecording* recording = tRecordings->GetByName((char*)req->data);

  log->log("RRProc", Log::DEBUG, "recording pointer %p", recording);

  if (recording)
  {
    x.recplayer = new RecPlayer(recording);

    resp->addULLONG(x.recplayer->getLengthBytes());
    resp->addULONG(x.recplayer->getLengthFrames());
    
#if VDRVERSNUM < 10703
    resp->addUCHAR(true);//added for TS    
#else
    resp->addUCHAR(recording->IsPesRecording());//added for TS
#endif

    resp->finalise();
    x.tcp.sendPacket(resp->getPtr(), resp->getLen());
    
    log->log("RRProc", Log::DEBUG, "written totalLength");
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

#if VDRVERSNUM >= 20301
  LOCK_CHANNELS_READ;
  const cChannels* tChannels = Channels;
#else
  cChannels* tChannels = &Channels;
#endif

  const cChannel* channel = tChannels->GetByNumber(channelNumber);
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
  const cSchedules *tSchedules = cSIProcessor::Schedules(MutexLock);
#elif VDRVERSNUM < 20301
  cSchedulesLock MutexLock;
  const cSchedules *tSchedules = cSchedules::Schedules(MutexLock);
#else
  LOCK_SCHEDULES_READ;
  const cSchedules *tSchedules = Schedules;
#endif

  if (!tSchedules)
  {
    resp->addULONG(0);
    resp->finalise();
    x.tcp.sendPacket(resp->getPtr(), resp->getLen());
    
    log->log("RRProc", Log::DEBUG, "written 0 because Schedule!s! = NULL");
    return 1;
  }

  log->log("RRProc", Log::DEBUG, "Got schedule!s! object");

  const cSchedule *Schedule = tSchedules->GetSchedule(channel->GetChannelID());
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

    resp->addString(x.charconvsys->Convert(thisEventTitle));
    resp->addString(x.charconvsys->Convert(thisEventSubTitle));
    resp->addString(x.charconvsys->Convert(thisEventDescription));

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
#if VDRVERSNUM >= 20301
  LOCK_TIMERS_READ;
  const cTimers* tTimers = Timers;
#else
  const cTimers* tTimers = &Timers;
#endif

  const cTimer *timer;
  int numTimers = tTimers->Count();

  resp->addULONG(numTimers);

  for (int i = 0; i < numTimers; i++)
  {
    timer = tTimers->Get(i);

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
    resp->addString(timer->File()); //Filename is system specific and not visible by user
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

#endif
  log->log("RRProc", Log::DEBUG, "%s", timerString);

  cTimer *timer = new cTimer;
  if (!timer->Parse((char*)timerString))
  {
    resp->addULONG(2);
    resp->finalise();
    x.tcp.sendPacket(resp->getPtr(), resp->getLen());
    delete timer;
    return 1;
  }

#if VDRVERSNUM >= 20301
  LOCK_TIMERS_WRITE;
  cTimers* tTimers = Timers;
#else
  cTimers* tTimers = &Timers;
#endif

  cTimer *t = tTimers->GetTimer(timer);
  if (t)
  {
    resp->addULONG(1);
    resp->finalise();
    x.tcp.sendPacket(resp->getPtr(), resp->getLen());
    delete timer;
    return 1;
  }

  timer->ClrFlags(tfRecording);
  tTimers->Add(timer);
#if VDRVERSNUM < 10300
  tTimers->Save();
#elif VDRVERSNUM < 20301
  tTimers->SetModified();
#endif

  resp->addULONG(0);
  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());
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

#if VDRVERSNUM >= 20301
  LOCK_TIMERS_WRITE;
  cTimers* tTimers = Timers;
#else
  cTimers* tTimers = &Timers;
#endif

  cTimer* ti = NULL;
  for (ti = tTimers->First(); ti; ti = tTimers->Next(ti))
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

#if VDRVERSNUM < 20301
// I suppose with the new locking this just can't happen
  if (tTimers->BeingEdited())
  {
    log->log("RRProc", Log::ERR, "Unable to delete timer - timers being edited at VDR");
    resp->addULONG(1);
    resp->finalise();
    x.tcp.sendPacket(resp->getPtr(), resp->getLen());
    return 1;
  }
#endif

  if (ti->Recording())
  {
    log->log("RRProc", Log::ERR, "Unable to delete timer - timer is running");
    resp->addULONG(3);
    resp->finalise();
    x.tcp.sendPacket(resp->getPtr(), resp->getLen());
    return 1;
  }

  tTimers->Del(ti);
  tTimers->SetModified();

  resp->addULONG(10);
  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());
  return 1;
}

int VompClientRRProc::processGetRecInfo()
{
  // data is a pointer to the fileName string
#if VDRVERSNUM >= 20301
  LOCK_RECORDINGS_READ;
  const cRecordings* tRecordings = Recordings;
#else
  cThreadLock RecordingsLock(&Recordings);
  cRecordings* tRecordings = &Recordings;
#endif

  const cRecording *recording = tRecordings->GetByName((char*)req->data);

  time_t timerStart = 0;
  time_t timerStop = 0;
  char* summary = NULL;
  char* shorttext = NULL;
  char* description = NULL;
  char* title = NULL;
  bool newsummary=false;
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
  8 bytes: frames per second
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

/*  char* value = x.config.getValueString("ResumeData", (char*)req->data);
  if (value)
  {
    resumePoint = strtoul(value, NULL, 10);
    delete[] value;
  }*/

  char* ResumeIdC = x.config.getValueString("General", "ResumeId");
  int ResumeId;
  if (ResumeIdC) {
    ResumeId = atoi(ResumeIdC);
    delete[] ResumeIdC;
  }
  else
    ResumeId = 0;  //default if not defined in vomp-MAC.conf

  while (ResumeIDLock)
    cCondWait::SleepMs(100);
  ResumeIDLock = true;
  int OldSetupResumeID = Setup.ResumeID;
  Setup.ResumeID = ResumeId;				//UGLY: quickly change resumeid
#if VDRVERSNUM < 10703
  cResumeFile ResumeFile(recording->FileName());	//get corresponding resume file
#else
  cResumeFile ResumeFile(recording->FileName(), recording->IsPesRecording()); //get corresponding resume file
#endif
  Setup.ResumeID = OldSetupResumeID;			//and restore it back
  ResumeIDLock = false;

  int resume = ResumeFile.Read();
  //isyslog("VOMPDEBUG: resumePoint = %i, resume = %i, ResumeId = %i",resumePoint, resume, ResumeId);
  if (resume >= 0) 
    resumePoint = ResumeFile.Read();

  log->log("RRProc", Log::DEBUG, "GRI: RP: %lu", resumePoint);

  resp->addULONG(resumePoint);

  // Get summary

#if VDRVERSNUM < 10300
  summary = (char*)recording->Summary();
#else
  const cRecordingInfo *Info = recording->Info();
  shorttext = (char*)Info->ShortText();
  description = (char*) (char*)Info->Description();
  if (isempty(shorttext)) summary=description;
  else if (isempty(description)) summary=shorttext;
  else {
     int length=strlen(description)+strlen(shorttext)+4;
     summary=new char[length];
     snprintf(summary,length,"%s\n\n%s",shorttext,description);
     newsummary=true;
  }
  
  if (isempty(summary)) summary = (char*)Info->Description();
#endif
  log->log("RRProc", Log::DEBUG, "GRI: S: %s", summary);
  if (summary)
  {
    resp->addString(x.charconvsys->Convert(summary));
    if (newsummary) delete [] summary;
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
        resp->addString(x.charconvsys->Convert(component->language));
      }
      else
      {
        resp->addString("");
      }
      if (component->description)
      {
        resp->addString(x.charconvsys->Convert(component->description));
      }
      else
      {
        resp->addString("");
      }
    }
  }

#endif
  double framespersec;
#if VDRVERSNUM < 10703
  framespersec = FRAMESPERSEC;
#else
  framespersec = Info->FramesPerSecond();
#endif
  resp->adddouble(framespersec);
  title = (char*)Info->Title();
  if (title) 
  {
    resp->addString(x.charconvsys->Convert(title));
  }
  else
  {
      resp->addString(x.charconvsys->Convert(recording->Name()));
  }
  
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
#if VDRVERSNUM >= 20301
  LOCK_RECORDINGS_READ;
  const cRecordings* tRecordings = Recordings;
#else
  cThreadLock RecordingsLock(&Recordings);
  cRecordings* tRecordings = &Recordings;
#endif

  const cRecording *recording = tRecordings->GetByName((char*)req->data);
  log->log("RRProc", Log::DEBUG, "recording pointer %p", recording);

  if (recording)
  {
    cMarks Marks;
#if VDRVERSNUM < 10703
    Marks.Load(recording->FileName());
#else
    Marks.Load(recording->FileName(), recording->FramesPerSecond(), recording->IsPesRecording());
#endif
    if (Marks.Count())
    {
      for (const cMark *m = Marks.First(); m; m = Marks.Next(m))
      {
#if VDRVERSNUM < 10721
        ULLONG mposition = m->position;
#else
        ULLONG mposition = m->Position();
#endif
        log->log("RRProc", Log::DEBUG, "found Mark %i", mposition);

        resp->addULONG(mposition);
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

int VompClientRRProc::processVDRShutdown()
{
  log->log("RRProc", Log::DEBUG, "Trying to shutdown VDR");
  VompClient::decClients(); // Temporarily make this client disappear
  cRemote::Put(kPower);
  VompClient::incClients();
  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());
  return 1;
}

int VompClientRRProc::processGetRecScraperEventType()
{
#if VDRVERSNUM >= 20301
  LOCK_RECORDINGS_READ;
  const cRecordings* tRecordings = Recordings;
#else
  cThreadLock RecordingsLock(&Recordings);
  cRecordings* tRecordings = &Recordings;
#endif

  const cRecording *recording = tRecordings->GetByName((char*)req->data);
  ScraperGetEventType call;
  call.type = tNone;

  if (recording && x.scrapQuery()) 
  {
     call.recording = recording;
     x.scraper->Service("GetEventType", &call);
  }
  resp->addUCHAR(call.type);
  if (call.type == tMovie)
  {
     resp->addLONG(call.movieId);
  }
  else if (call.type == tSeries)
  {
     resp->addLONG(call.seriesId);
     resp->addLONG(call.episodeId);
  }
  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());

  return 1;
}

int VompClientRRProc::processGetEventScraperEventType()
{
  ScraperGetEventType call;
  call.type = tNone;
  ULONG channelid = ntohl(*(ULONG*)req->data);
  ULONG eventid = ntohl(*(ULONG*)(req->data+4));
  const cEvent *event = NULL; 

#if VDRVERSNUM >= 20301
  LOCK_CHANNELS_READ;
  const cChannels* tChannels = Channels;
#else
  cChannels* tChannels = &Channels;
#endif

  const cChannel* channel = tChannels->GetByNumber(channelid);

#if VDRVERSNUM < 10300
  cMutexLock MutexLock;
  const cSchedules *tSchedules = cSIProcessor::Schedules(MutexLock);
#elif VDRVERSNUM < 20301
  cSchedulesLock MutexLock;
  const cSchedules *tSchedules = cSchedules::Schedules(MutexLock);
#else
  LOCK_SCHEDULES_READ;
  const cSchedules *tSchedules = Schedules;
#endif

  if (tSchedules && channel)
  {
     const cSchedule *Schedule = tSchedules->GetSchedule(channel->GetChannelID());
     if (Schedule) {
        event = Schedule->GetEvent(eventid);
    }
  }
    
  if (event && x.scrapQuery()) 
  {
     call.event = event;
     x.scraper->Service("GetEventType",&call);
  }
  resp->addUCHAR(call.type);
  if (call.type == tMovie)
  {
     resp->addLONG(call.movieId);
  } else if (call.type == tSeries){
     resp->addLONG(call.seriesId);
     resp->addLONG(call.episodeId);
  }
  if (x.pict->epgImageExists(eventid)) {
     resp->addLONG(1);
  } else {
     resp->addLONG(0);
  }
    
  resp->finalise();
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());

  return 1;
}

#define ADDSTRING_TO_PAKET(y) if ((y)!=0)  resp->addString(x.charconvutf8->Convert(y)); else resp->addString(""); 

int VompClientRRProc::processGetScraperMovieInfo()
{
   
   cMovie movie;
   movie.movieId = ntohl(*(ULONG*)req->data);
   if (!x.scrapQuery()) {
      log->log("RRProc", Log::DEBUG, "No Scraper, get SeriesInfo");
      return 0; //stupid, I have no scraper why are you still asking
   }
   x.scraper->Service("GetMovie",&movie);
   

   ADDSTRING_TO_PAKET(movie.title.c_str());
   ADDSTRING_TO_PAKET(movie.originalTitle.c_str());
   ADDSTRING_TO_PAKET(movie.tagline.c_str());
   ADDSTRING_TO_PAKET(movie.overview.c_str());
   resp->addUCHAR(movie.adult);
   ADDSTRING_TO_PAKET(movie.collectionName.c_str());

   resp->addLONG(movie.budget);
   resp->addLONG(movie.revenue);
   ADDSTRING_TO_PAKET(movie.genres.c_str());
   ADDSTRING_TO_PAKET(movie.homepage.c_str());
   ADDSTRING_TO_PAKET(movie.releaseDate.c_str());
   resp->addLONG(movie.runtime);
   resp->adddouble(movie.popularity);
   resp->adddouble(movie.voteAverage);
   resp->addULONG(movie.poster.width);
   resp->addULONG(movie.poster.height);
   resp->addULONG(movie.fanart.width);
   resp->addULONG(movie.fanart.height);
   resp->addULONG(movie.collectionPoster.width);
   resp->addULONG(movie.collectionPoster.height);
   resp->addULONG(movie.collectionFanart.width);
   resp->addULONG(movie.collectionFanart.height);
   resp->addULONG(movie.actors.size());
   for (ULONG acty=0; acty < movie.actors.size(); acty++) {
       ADDSTRING_TO_PAKET(movie.actors[acty].name.c_str());
       ADDSTRING_TO_PAKET(movie.actors[acty].role.c_str());
       resp->addULONG(movie.actors[acty].actorThumb.width);
       resp->addULONG(movie.actors[acty].actorThumb.height);
   }
  resp->finalise();
  
  x.tcp.sendPacket(resp->getPtr(), resp->getLen());

  
  return 1;

}

int VompClientRRProc::processGetScraperSeriesInfo()
{
   cSeries series;
   series.seriesId = ntohl(*(ULONG*)req->data);
   series.episodeId = ntohl(*(ULONG*)(req->data+4));
   if (!x.scrapQuery()) {
      log->log("RRProc", Log::DEBUG, "No Scraper, get SeriesInfo");
      return 0; //stupid, I have no scraper why are you still asking
   }
   x.scraper->Service("GetSeries",&series);
   
   ADDSTRING_TO_PAKET(series.name.c_str());
   ADDSTRING_TO_PAKET(series.overview.c_str());
   ADDSTRING_TO_PAKET(series.firstAired.c_str());
   ADDSTRING_TO_PAKET(series.network.c_str());
   ADDSTRING_TO_PAKET(series.genre.c_str());
   resp->adddouble(series.rating);
   ADDSTRING_TO_PAKET(series.status.c_str());
   
   resp->addLONG(series.episode.number);
   resp->addLONG(series.episode.season);
   ADDSTRING_TO_PAKET(series.episode.name.c_str());
   ADDSTRING_TO_PAKET(series.episode.firstAired.c_str());
   ADDSTRING_TO_PAKET(series.episode.guestStars.c_str());
   ADDSTRING_TO_PAKET(series.episode.overview.c_str());
   resp->adddouble(series.episode.rating);
   resp->addULONG(series.episode.episodeImage.width);
   resp->addULONG(series.episode.episodeImage.height);
   
   ULONG num_actors = series.actors.size();
   resp->addULONG(num_actors);
   for (ULONG acty=0; acty < num_actors; acty++) {
       ADDSTRING_TO_PAKET(series.actors[acty].name.c_str());
       ADDSTRING_TO_PAKET(series.actors[acty].role.c_str());
       resp->addULONG(series.actors[acty].actorThumb.width);
       resp->addULONG(series.actors[acty].actorThumb.height);
   }
   ULONG num_posters = series.posters.size();
   resp->addULONG(num_posters);
   for (ULONG medias = 0; medias < num_posters; medias++ ) {
       cTvMedia media=series.posters[medias];
       resp->addULONG(media.width);
       resp->addULONG(media.height);
   }

   ULONG num_banners = series.banners.size();
   resp->addULONG(num_banners);
   for (ULONG medias = 0; medias < num_banners; medias++ ) {
       cTvMedia media=series.banners[medias];
       resp->addULONG(media.width);
       resp->addULONG(media.height);
   }
   ULONG num_fanarts = series.fanarts.size();
   resp->addULONG(num_fanarts);
   for (ULONG medias = 0; medias < num_fanarts; medias++ ) {
       cTvMedia media=series.fanarts[medias];
       resp->addULONG(media.width);
       resp->addULONG(media.height);
   }
   resp->addULONG(series.seasonPoster.width);
   resp->addULONG(series.seasonPoster.height);
   
   resp->finalise();

   x.tcp.sendPacket(resp->getPtr(), resp->getLen());
   
   return 1;
}

int VompClientRRProc::processLoadTvMedia()
{
   TVMediaRequest tvreq;
   tvreq.streamID = req->requestID;
   tvreq.type = ntohl(*(ULONG*)req->data);
   tvreq.primary_id = ntohl(*(ULONG*)(req->data+4));
   tvreq.secondary_id = ntohl(*(ULONG*)(req->data+8));
   tvreq.type_pict = ntohl(*(ULONG*)(req->data+12));
   tvreq.container = ntohl(*(ULONG*)(req->data+16));
   tvreq.container_member = ntohl(*(ULONG*)(req->data+20));
   log->log("RRProc", Log::DEBUG, "TVMedia request %d",req->requestID);
   x.pict->addTVMediaRequest(tvreq);

   
   resp->finalise();

   x.tcp.sendPacket(resp->getPtr(), resp->getLen());
   
   return 1;
}

int VompClientRRProc::processLoadTvMediaRecThumb()
{
   TVMediaRequest tvreq;
   tvreq.streamID = req->requestID;
   tvreq.type = 3; // unknown but primary_name is set
   tvreq.primary_id = 0;
   tvreq.primary_name = std::string((const char*) req->data);
   tvreq.secondary_id = 0;
   tvreq.type_pict = 1;
   tvreq.container = 0;
   tvreq.container_member = 0;
   log->log("RRProc", Log::DEBUG, "TVMedia request %d %s",req->requestID,req->data);
   x.pict->addTVMediaRequest(tvreq);

   
   resp->finalise();

   x.tcp.sendPacket(resp->getPtr(), resp->getLen());
   
   return 1;
}

int VompClientRRProc::processLoadTvMediaEventThumb()
{
   TVMediaRequest tvreq;
   tvreq.streamID = req->requestID;
   tvreq.type = 4; // unknown but primary_id is set
   UINT channelid = ntohl(*(ULONG*)req->data);
   tvreq.primary_id = ntohl(*(ULONG*)(req->data+4));
   tvreq.secondary_id = 0;

#if VDRVERSNUM >= 20301
  LOCK_CHANNELS_READ;
  const cChannels* tChannels = Channels;
#else
  cChannels* tChannels = &Channels;
#endif

   const cChannel* channel = tChannels->GetByNumber(channelid);

   if (channel) tvreq.primary_name = std::string((const char*)channel->GetChannelID().ToString());
   tvreq.type_pict = 1;
   tvreq.container = 0;
   tvreq.container_member = 0;
   log->log("RRProc", Log::DEBUG, "TVMedia request %d %s",req->requestID,req->data);
   x.pict->addTVMediaRequest(tvreq);

   
   resp->finalise();

   x.tcp.sendPacket(resp->getPtr(), resp->getLen());
   
   return 1;
}

int VompClientRRProc::processLoadChannelLogo()
{
   TVMediaRequest tvreq;
   tvreq.streamID = req->requestID;
   tvreq.type = 5; // channel logo
   UINT channelid = ntohl(*(ULONG*)req->data);
   tvreq.primary_id = channelid;
   tvreq.secondary_id = 0;

#if VDRVERSNUM >= 20301
  LOCK_CHANNELS_READ;
  const cChannels* tChannels = Channels;
#else
  cChannels* tChannels = &Channels;
#endif

   const cChannel* channel = tChannels->GetByNumber(channelid);

   if (channel) tvreq.primary_name = std::string((const char*)channel->Name());
   tvreq.type_pict = 1;
   tvreq.container = 0;
   tvreq.container_member = 0;
   if (channel) log->log("RRProc", Log::DEBUG, "TVMedia request %d %d %s",req->requestID,channelid, channel->Name());
   x.pict->addTVMediaRequest(tvreq);

   
   resp->finalise();

   x.tcp.sendPacket(resp->getPtr(), resp->getLen());
   
   return 1;
}



  
#endif // !VOMPSTANDALONE

