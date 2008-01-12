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

#include "mvpclient.h"

#include "responsepacket.h"

// This is here else it causes compile errors with something in libdvbmpeg
//#include <vdr/menu.h>

pthread_mutex_t threadClientMutex;
int MVPClient::nr_clients = 0;


MVPClient::MVPClient(Config* cfgBase, char* tconfigDir, int tsocket)
 : tcp(tsocket), i18n(tconfigDir)
{
#ifndef VOMPSTANDALONE
  lp = NULL;
  recplayer = NULL;
  recordingManager = NULL;
#endif
  imageFile = 0;
  log = Log::getInstance();
  loggedIn = false;
  configDir = tconfigDir;
  log->log("Client", Log::DEBUG, "Config dir: %s", configDir);
  baseConfig = cfgBase;
  incClients();
}

MVPClient::~MVPClient()
{
  log->log("Client", Log::DEBUG, "MVP client destructor");
#ifndef VOMPSTANDALONE  
  if (lp)
  {
    delete lp;
    lp = NULL;
  }
  else if (recplayer)
  {
    writeResumeData();

    delete recplayer;
    delete recordingManager;
    recplayer = NULL;
    recordingManager = NULL;
  }
#endif
  if (loggedIn) cleanConfig();
  decClients();
}

void MVPClient::incClients()
{
  pthread_mutex_lock(&threadClientMutex);
  MVPClient::nr_clients++;
  pthread_mutex_unlock(&threadClientMutex);
}

void MVPClient::decClients()
{
  pthread_mutex_lock(&threadClientMutex);
  MVPClient::nr_clients--;
  pthread_mutex_unlock(&threadClientMutex);
}

int MVPClient::getNrClients()
{
  int nrClients;
  pthread_mutex_lock(&threadClientMutex);
  nrClients = MVPClient::nr_clients;
  pthread_mutex_unlock(&threadClientMutex);
  return nrClients;
}

ULLONG MVPClient::ntohll(ULLONG a)
{
  return htonll(a);
}

ULLONG MVPClient::htonll(ULLONG a)
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

#ifndef VOMPSTANDALONE
cChannel* MVPClient::channelFromNumber(ULONG channelNumber)
{
  cChannel* channel = NULL;

  for (channel = Channels.First(); channel; channel = Channels.Next(channel))
  {
    if (!channel->GroupSep())
    {
      log->log("Client", Log::DEBUG, "Looking for channel %lu::: number: %i name: '%s'", channelNumber, channel->Number(), channel->Name());

      if (channel->Number() == (int)channelNumber)
      {
        int vpid = channel->Vpid();
#if VDRVERSNUM < 10300
        int apid1 = channel->Apid1();
#else
        int apid1 = channel->Apid(0);
#endif
        log->log("Client", Log::DEBUG, "Found channel number %lu, vpid = %i, apid1 = %i", channelNumber, vpid, apid1);
        return channel;
      }
    }
  }

  if (!channel)
  {
    log->log("Client", Log::DEBUG, "Channel not found");
  }

  return channel;
}

void MVPClient::writeResumeData()
{
  config.setValueLong("ResumeData",
                          (char*)recplayer->getCurrentRecording()->FileName(),
                          recplayer->frameNumberFromPosition(recplayer->getLastPosition()) );
}
#endif

void MVPClient::cleanConfig()
{
  log->log("Client", Log::DEBUG, "Clean config");

#ifndef VOMPSTANDALONE

  cRecordings Recordings;
  Recordings.Load();

  int numReturns;
  int length;
  char* resumes = config.getSectionKeyNames("ResumeData", numReturns, length);
  char* position = resumes;
  for(int k = 0; k < numReturns; k++)
  {
    log->log("Client", Log::DEBUG, "EXAMINING: %i %i %p %s", k, numReturns, position, position);

    cRecording* recording = Recordings.GetByName(position);
    if (!recording)
    {
      // doesn't exist anymore
      log->log("Client", Log::DEBUG, "Found a recording that doesn't exist anymore");
      config.deleteValue("ResumeData", position);
    }
    else
    {
      log->log("Client", Log::DEBUG, "This recording still exists");
    }

    position += strlen(position) + 1;
  }

  delete[] resumes;
#endif
}

void MVPClientStartThread(void* arg)
{
  MVPClient* m = (MVPClient*)arg;
  m->run2();
  // Nothing external to this class has a reference to it
  // This is the end of the thread.. so delete m
  delete m;
  pthread_exit(NULL);
}

int MVPClient::run()
{
  if (pthread_create(&runThread, NULL, (void*(*)(void*))MVPClientStartThread, (void *)this) == -1) return 0;
  log->log("Client", Log::DEBUG, "MVPClient run success");
  return 1;
}

void MVPClient::run2()
{
  // Thread stuff
  sigset_t sigset;
  sigfillset(&sigset);
  pthread_sigmask(SIG_BLOCK, &sigset, NULL);
  pthread_detach(runThread);  // Detach

  tcp.disableReadTimeout();

  tcp.setSoKeepTime(3);
  tcp.setNonBlocking();

  ULONG channelID;
  ULONG requestID;
  ULONG opcode;
  ULONG extraDataLength;
  UCHAR* data;
  int result;

  while(1)
  {
    log->log("Client", Log::DEBUG, "Waiting");
    result = 0;
    
    if (!tcp.readData((UCHAR*)&channelID, sizeof(ULONG))) break;
    channelID = ntohl(channelID);
    if (channelID != 1)
    {
      log->log("Client", Log::ERR, "Incoming channel number not 1!");
      break;
    }

    log->log("Client", Log::DEBUG, "Got chan");
    
    if (!tcp.readData((UCHAR*)&requestID, sizeof(ULONG))) break;
    requestID = ntohl(requestID);

    log->log("Client", Log::DEBUG, "Got ser");

    if (!tcp.readData((UCHAR*)&opcode, sizeof(ULONG))) break;
    opcode = ntohl(opcode);

    log->log("Client", Log::DEBUG, "Got op %lu", opcode);

    if (!tcp.readData((UCHAR*)&extraDataLength, sizeof(ULONG))) break;
    extraDataLength = ntohl(extraDataLength);
    if (extraDataLength > 200000)
    {
      log->log("Client", Log::ERR, "ExtraDataLength > 200000!");
      break;
    }

    log->log("Client", Log::DEBUG, "Got edl %lu", extraDataLength);

    if (extraDataLength)
    {
      data = (UCHAR*)malloc(extraDataLength);
      if (!data)
      {
        log->log("Client", Log::ERR, "Extra data buffer malloc error");
        break;
      }
      
      if (!tcp.readData(data, extraDataLength))
      {
        log->log("Client", Log::ERR, "Could not read extradata");
        free(data);
        break;
      }      
    }
    else
    {
      data = NULL;
    }

    log->log("Client", Log::DEBUG, "Received chan=%lu, ser=%lu, op=%lu, edl=%lu", channelID, requestID, opcode, extraDataLength);

    if (!loggedIn && (opcode != 1))
    {
      log->log("Client", Log::ERR, "Not logged in and opcode != 1");
      if (data) free(data);
      break;
    }

    ResponsePacket* rp = new ResponsePacket();
    if (!rp->init(requestID))
    {
      log->log("Client", Log::ERR, "response packet init fail");     
      delete rp; 
      break;
    }
    
    switch(opcode)
    {
      case 1:
        result = processLogin(data, extraDataLength, rp);
        break;
#ifndef VOMPSTANDALONE        
      case 2:
        result = processGetRecordingsList(data, extraDataLength, rp);
        break;
      case 3:
        result = processDeleteRecording(data, extraDataLength, rp);
        break;
      case 5:
        result = processGetChannelsList(data, extraDataLength, rp);
        break;
      case 6:
        result = processStartStreamingChannel(data, extraDataLength, requestID, rp);
        break;
      case 7:
        result = processGetBlock(data, extraDataLength, rp);
        break;
      case 8:
        result = processStopStreaming(data, extraDataLength, rp);
        break;
      case 9:
        result = processStartStreamingRecording(data, extraDataLength, rp);
        break;
#endif     
      case 10:
        result = processGetChannelSchedule(data, extraDataLength, rp);
        break;
      case 11:
        result = processConfigSave(data, extraDataLength, rp);
        break;
      case 12:
        result = processConfigLoad(data, extraDataLength, rp);
        break;
#ifndef VOMPSTANDALONE        
      case 13:
        result = processReScanRecording(data, extraDataLength, rp);         // FIXME obselete
        break;
      case 14:
        result = processGetTimers(data, extraDataLength, rp);
        break;
      case 15:
        result = processSetTimer(data, extraDataLength, rp);
        break;
      case 16:
        result = processPositionFromFrameNumber(data, extraDataLength, rp);
        break;
      case 17:
        result = processFrameNumberFromPosition(data, extraDataLength, rp);
        break;
      case 18:
        result = processMoveRecording(data, extraDataLength, rp);
        break;
      case 19:
        result = processGetIFrame(data, extraDataLength, rp);
        break;
      case 20:
        result = processGetRecInfo(data, extraDataLength, rp);
        break;
      case 21:
        result = processGetMarks(data, extraDataLength, rp);
        break;
      case 22:
        result = processGetChannelPids(data, extraDataLength, rp);
        break;
      case 23:
        result = processDeleteTimer(data, extraDataLength, rp);
        break;
#endif        
      case 30:
        result = processGetMediaList(data, extraDataLength, rp);
        break;
      case 31:
        result = processGetPicture(data, extraDataLength, rp);
        break;
      case 32:
        result = processGetImageBlock(data, extraDataLength, rp);
        break;
      case 33:
        result = processGetLanguageList(data, extraDataLength, rp);
        break;
      case 34:
        result = processGetLanguageContent(data, extraDataLength, rp);
        break;
    }

    delete rp;
    if (data) free(data);
    if (!result) break;
  }
}

int MVPClient::processLogin(UCHAR* buffer, int length, ResponsePacket* rp)
{
  if (length != 6) return 0;

  // Open the config

  char configFileName[PATH_MAX];
  snprintf(configFileName, PATH_MAX, "%s/vomp-%02X-%02X-%02X-%02X-%02X-%02X.conf", configDir, buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5]);
  config.init(configFileName);

  // Send the login reply

  time_t timeNow = time(NULL);
  struct tm* timeStruct = localtime(&timeNow);
  int timeOffset = timeStruct->tm_gmtoff;

  rp->addULONG(timeNow);
  rp->addLONG(timeOffset);
  rp->finalise();
  tcp.sendPacket(rp->getPtr(), rp->getLen());
  log->log("Client", Log::DEBUG, "written login reply len %lu", rp->getLen());
    
  loggedIn = true;
  return 1;
}

#ifndef VOMPSTANDALONE
int MVPClient::processGetRecordingsList(UCHAR* data, int length, ResponsePacket* rp)
{
  int FreeMB;
  int Percent = VideoDiskSpace(&FreeMB);
  int Total = (FreeMB / (100 - Percent)) * 100;
  
  rp->addULONG(Total);
  rp->addULONG(FreeMB);
  rp->addULONG(Percent);

  cRecordings Recordings;
  Recordings.Load();

  for (cRecording *recording = Recordings.First(); recording; recording = Recordings.Next(recording))
  {
    rp->addULONG(recording->start);
    rp->addString(recording->Name());
    rp->addString(recording->FileName());
  }

  rp->finalise();
  tcp.sendPacket(rp->getPtr(), rp->getLen());
  
  log->log("Client", Log::DEBUG, "Written recordings list");

  return 1;
}

int MVPClient::processDeleteRecording(UCHAR* data, int length, ResponsePacket* rp)
{
  // data is a pointer to the fileName string

  cRecordings Recordings;
  Recordings.Load(); // probably have to do this

  cRecording* recording = Recordings.GetByName((char*)data);

  log->log("Client", Log::DEBUG, "recording pointer %p", recording);

  if (recording)
  {
    log->log("Client", Log::DEBUG, "deleting recording: %s", recording->Name());

    cRecordControl *rc = cRecordControls::GetRecordControl(recording->FileName());
    if (!rc)
    {
      if (recording->Delete())
      {
        // Copy svdrp's way of doing this, see if it works
#if VDRVERSNUM > 10300
        ::Recordings.DelByName(recording->FileName());
#endif
        rp->addULONG(1);
      }
      else
      {
        rp->addULONG(2);
      }
    }
    else
    {
      rp->addULONG(3);
    }
  }
  else
  {
    rp->addULONG(4);
  }

  rp->finalise();
  tcp.sendPacket(rp->getPtr(), rp->getLen());
  
  return 1;
}

int MVPClient::processMoveRecording(UCHAR* data, int length, ResponsePacket* rp)
{
  log->log("Client", Log::DEBUG, "Process move recording");
  char* fileName = (char*)data;
  char* newPath = NULL;

  for (int k = 0; k < length; k++)
  {
    if (data[k] == '\0')
    {
      newPath = (char*)&data[k+1];
      break;
    }
  }
  if (!newPath) return 0;

  cRecordings Recordings;
  Recordings.Load(); // probably have to do this

  cRecording* recording = Recordings.GetByName((char*)fileName);

  log->log("Client", Log::DEBUG, "recording pointer %p", recording);

  if (recording)
  {
    cRecordControl *rc = cRecordControls::GetRecordControl(recording->FileName());
    if (!rc)
    {
      log->log("Client", Log::DEBUG, "moving recording: %s", recording->Name());
      log->log("Client", Log::DEBUG, "moving recording: %s", recording->FileName());
      log->log("Client", Log::DEBUG, "to: %s", newPath);

      const char* t = recording->FileName();

      char* dateDirName = NULL;   int k;
      char* titleDirName = NULL;  int j;

      // Find the datedirname
      for(k = strlen(t) - 1; k >= 0; k--)
      {
        if (t[k] == '/')
        {
          log->log("Client", Log::DEBUG, "l1: %i", strlen(&t[k+1]) + 1);
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
          log->log("Client", Log::DEBUG, "l2: %i", (k - j - 1) + 1);
          titleDirName = new char[(k - j - 1) + 1];
          memcpy(titleDirName, &t[j+1], k - j - 1);
          titleDirName[k - j - 1] = '\0';
          break;
        }
      }

      log->log("Client", Log::DEBUG, "datedirname: %s", dateDirName);
      log->log("Client", Log::DEBUG, "titledirname: %s", titleDirName);

      log->log("Client", Log::DEBUG, "viddir: %s", VideoDirectory);

      char* newContainer = new char[strlen(VideoDirectory) + strlen(newPath) + strlen(titleDirName) + 1];
      log->log("Client", Log::DEBUG, "l10: %i", strlen(VideoDirectory) + strlen(newPath) + strlen(titleDirName) + 1);
      sprintf(newContainer, "%s%s%s", VideoDirectory, newPath, titleDirName);

      // FIXME Check whether this already exists before mkdiring it

      log->log("Client", Log::DEBUG, "%s", newContainer);


      struct stat dstat;
      int statret = stat(newContainer, &dstat);
      if ((statret == -1) && (errno == ENOENT)) // Dir does not exist
      {
        log->log("Client", Log::DEBUG, "new dir does not exist");
        int mkdirret = mkdir(newContainer, 0755);
        if (mkdirret != 0)
        {
          delete[] dateDirName;
          delete[] titleDirName;
          delete[] newContainer;

          rp->addULONG(5);          
          rp->finalise();
          tcp.sendPacket(rp->getPtr(), rp->getLen());
          return 1;
        }
      }
      else if ((statret == 0) && (! (dstat.st_mode && S_IFDIR))) // Something exists but it's not a dir
      {
        delete[] dateDirName;
        delete[] titleDirName;
        delete[] newContainer;

        rp->addULONG(5);          
        rp->finalise();
        tcp.sendPacket(rp->getPtr(), rp->getLen());
        return 1;
      }

      // Ok, the directory container has been made, or it pre-existed.

      char* newDir = new char[strlen(newContainer) + 1 + strlen(dateDirName) + 1];
      sprintf(newDir, "%s/%s", newContainer, dateDirName);

      log->log("Client", Log::DEBUG, "doing rename '%s' '%s'", t, newDir);
      int renameret = rename(t, newDir);
      if (renameret == 0)
      {
        // Success. Test for remove old dir containter
        char* oldTitleDir = new char[k+1];
        memcpy(oldTitleDir, t, k);
        oldTitleDir[k] = '\0';
        log->log("Client", Log::DEBUG, "len: %i, cp: %i, strlen: %i, oldtitledir: %s", k+1, k, strlen(oldTitleDir), oldTitleDir);
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
        rp->addULONG(1); // success
        rp->addString(newDir);
      }
      else
      {
        rp->addULONG(5);          
      }

      rp->finalise();
      tcp.sendPacket(rp->getPtr(), rp->getLen());

      delete[] dateDirName;
      delete[] titleDirName;
      delete[] newContainer;
      delete[] newDir;
    }
    else
    {
      rp->addULONG(3);          
      rp->finalise();
      tcp.sendPacket(rp->getPtr(), rp->getLen());
    }
  }
  else
  {
    rp->addULONG(4);          
    rp->finalise();
    tcp.sendPacket(rp->getPtr(), rp->getLen());
  }

  return 1;
}

int MVPClient::processGetChannelsList(UCHAR* data, int length, ResponsePacket* rp)
{
  ULONG type;

  char* chanConfig = config.getValueString("General", "Channels");
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
      log->log("Client", Log::DEBUG, "name: '%s'", channel->Name());

      if (channel->Vpid()) type = 1;
#if VDRVERSNUM < 10300
      else type = 2;
#else
      else if (channel->Apid(0)) type = 2;
      else continue;
#endif

      rp->addULONG(channel->Number());
      rp->addULONG(type);      
      rp->addString(channel->Name());
    }
  }

  rp->finalise();
  tcp.sendPacket(rp->getPtr(), rp->getLen());

  log->log("Client", Log::DEBUG, "Written channels list");

  return 1;
}

int MVPClient::processGetChannelPids(UCHAR* data, int length, ResponsePacket* rp)
{
  ULONG channelNumber = ntohl(*(ULONG*)data);

  cChannel* channel = channelFromNumber(channelNumber);
  if (!channel)
  {
    rp->addULONG(0);
    rp->finalise();
    tcp.sendPacket(rp->getPtr(), rp->getLen());
    return 1;
  }

  ULONG numApids = 0;

#if VDRVERSNUM < 10300

  log->log("Client", Log::DEBUG, "Apid1: %i", channel->Apid1());
  log->log("Client", Log::DEBUG, "Apid2: %i", channel->Apid2());

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
#endif


  // Format of response
  // vpid
  // number of apids
  // {
  //    apid
  //    lang string
  // }

  rp->addULONG(channel->Vpid());
  rp->addULONG(numApids);

#if VDRVERSNUM < 10300
  if (numApids >= 1)
  {
    rp->addULONG(channel->Apid1());
    rp->addString("");
  }
  if (numApids == 2)
  {
    rp->addULONG(channel->Apid2());
    rp->addString("");
  }
#else
  for (ULONG i = 0; i < numApids; i++)
  {
    rp->addULONG(channel->Apid(i));
    rp->addString(channel->Alang(i));
  }
#endif

  rp->finalise();
  tcp.sendPacket(rp->getPtr(), rp->getLen());
  
  log->log("Client", Log::DEBUG, "Written channels pids");

  return 1;
}

int MVPClient::processStartStreamingChannel(UCHAR* data, int length, ULONG streamID, ResponsePacket* rp)
{
  if (lp)
  {
    log->log("Client", Log::ERR, "Client called start streaming twice");
    return 0;
  }
  
  log->log("Client", Log::DEBUG, "length = %i", length);
  ULONG channelNumber = ntohl(*(ULONG*)data);

  cChannel* channel = channelFromNumber(channelNumber);
  if (!channel)
  {
    rp->addULONG(0);
    rp->finalise();
    tcp.sendPacket(rp->getPtr(), rp->getLen());
    return 1;
  }

  // get the priority we should use
  int fail = 1;
  int priority = config.getValueLong("General", "Live priority", &fail);
  if (!fail)
  {
    log->log("Client", Log::DEBUG, "Config: Live TV priority: %i", priority);
  }
  else
  {
    log->log("Client", Log::DEBUG, "Config: Live TV priority config fail");
    priority = 0;
  }

  // a bit of sanity..
  if (priority < 0) priority = 0;
  if (priority > 99) priority = 99;

  log->log("Client", Log::DEBUG, "Using live TV priority %i", priority);
  lp = MVPReceiver::create(channel, priority);

  if (!lp)
  {
    rp->addULONG(0);
    rp->finalise();
    tcp.sendPacket(rp->getPtr(), rp->getLen());
    return 1;
  }

  if (!lp->init(&tcp, streamID))
  {
    delete lp;
    lp = NULL;
    rp->addULONG(0);
    rp->finalise();
    tcp.sendPacket(rp->getPtr(), rp->getLen());
    return 1;
  }

  rp->addULONG(1);
  rp->finalise();
  tcp.sendPacket(rp->getPtr(), rp->getLen());
  return 1;
}

int MVPClient::processStopStreaming(UCHAR* data, int length, ResponsePacket* rp)
{
  log->log("Client", Log::DEBUG, "STOP STREAMING RECEIVED");
  if (lp)
  {
    delete lp;
    lp = NULL;
  }
  else if (recplayer)
  {
    writeResumeData();

    delete recplayer;
    delete recordingManager;
    recplayer = NULL;
    recordingManager = NULL;
  }

  rp->addULONG(1);
  rp->finalise();
  tcp.sendPacket(rp->getPtr(), rp->getLen());
  return 1;
}

int MVPClient::processGetBlock(UCHAR* data, int length, ResponsePacket* rp)
{
  if (!lp && !recplayer)
  {
    log->log("Client", Log::DEBUG, "Get block called when no streaming happening!");
    return 0;
  }

  ULLONG position = ntohll(*(ULLONG*)data);
  data += sizeof(ULLONG);
  ULONG amount = ntohl(*(ULONG*)data);

  log->log("Client", Log::DEBUG, "getblock pos = %llu length = %lu", position, amount);

  UCHAR sendBuffer[amount];
  ULONG amountReceived = 0; // compiler moan.
  if (lp)
  {
    log->log("Client", Log::DEBUG, "getting from live");
    amountReceived = lp->getBlock(&sendBuffer[0], amount);

    if (!amountReceived)
    {
      // vdr has possibly disconnected the receiver
      log->log("Client", Log::DEBUG, "VDR has disconnected the live receiver");
      delete lp;
      lp = NULL;
    }
  }
  else if (recplayer)
  {
    log->log("Client", Log::DEBUG, "getting from recording");
    amountReceived = recplayer->getBlock(&sendBuffer[0], position, amount);
  }

  if (!amountReceived)
  {
    rp->addULONG(0);
    log->log("Client", Log::DEBUG, "written 4(0) as getblock got 0");
  }
  else
  {
    rp->copyin(sendBuffer, amountReceived);
    log->log("Client", Log::DEBUG, "written %lu", amountReceived);
  }

  rp->finalise();
  tcp.sendPacket(rp->getPtr(), rp->getLen());
  log->log("Client", Log::DEBUG, "Finished getblock, have sent %lu", rp->getLen());
  return 1;
}

int MVPClient::processStartStreamingRecording(UCHAR* data, int length, ResponsePacket* rp)
{
  // data is a pointer to the fileName string

  recordingManager = new cRecordings;
  recordingManager->Load();

  cRecording* recording = recordingManager->GetByName((char*)data);

  log->log("Client", Log::DEBUG, "recording pointer %p", recording);

  if (recording)
  {
    recplayer = new RecPlayer(recording);

    rp->addULLONG(recplayer->getLengthBytes());
    rp->addULONG(recplayer->getLengthFrames());
    rp->finalise();
    tcp.sendPacket(rp->getPtr(), rp->getLen());
    
    log->log("Client", Log::DEBUG, "written totalLength");
  }
  else
  {
    delete recordingManager;
    recordingManager = NULL;
  }
  return 1;
}

int MVPClient::processPositionFromFrameNumber(UCHAR* data, int length, ResponsePacket* rp)
{
  ULLONG retval = 0;

  ULONG frameNumber = ntohl(*(ULONG*)data);
  data += 4;

  if (!recplayer)
  {
    log->log("Client", Log::DEBUG, "Rescan recording called when no recording being played!");
  }
  else
  {
    retval = recplayer->positionFromFrameNumber(frameNumber);
  }

  rp->addULLONG(retval);
  rp->finalise();
  tcp.sendPacket(rp->getPtr(), rp->getLen());

  log->log("Client", Log::DEBUG, "Wrote posFromFrameNum reply to client");
  return 1;
}

int MVPClient::processFrameNumberFromPosition(UCHAR* data, int length, ResponsePacket* rp)
{
  ULONG retval = 0;

  ULLONG position = ntohll(*(ULLONG*)data);
  data += 8;

  if (!recplayer)
  {
    log->log("Client", Log::DEBUG, "Rescan recording called when no recording being played!");
  }
  else
  {
    retval = recplayer->frameNumberFromPosition(position);
  }

  rp->addULONG(retval);
  rp->finalise();
  tcp.sendPacket(rp->getPtr(), rp->getLen());

  log->log("Client", Log::DEBUG, "Wrote frameNumFromPos reply to client");
  return 1;
}

int MVPClient::processGetIFrame(UCHAR* data, int length, ResponsePacket* rp)
{
  bool success = false;

  ULONG frameNumber = ntohl(*(ULONG*)data);
  data += 4;
  ULONG direction = ntohl(*(ULONG*)data);
  data += 4;

  ULLONG rfilePosition = 0;
  ULONG rframeNumber = 0;
  ULONG rframeLength = 0;

  if (!recplayer)
  {
    log->log("Client", Log::DEBUG, "GetIFrame recording called when no recording being played!");
  }
  else
  {
    success = recplayer->getNextIFrame(frameNumber, direction, &rfilePosition, &rframeNumber, &rframeLength);
  }

  // returns file position, frame number, length

  if (success)
  {
    rp->addULLONG(rfilePosition);
    rp->addULONG(rframeNumber);
    rp->addULONG(rframeLength);
  }
  else
  {
    rp->addULONG(0);
  }

  rp->finalise();
  tcp.sendPacket(rp->getPtr(), rp->getLen());
  
  log->log("Client", Log::DEBUG, "Wrote GNIF reply to client %llu %lu %lu", rfilePosition, rframeNumber, rframeLength);
  return 1;
}

int MVPClient::processGetChannelSchedule(UCHAR* data, int length, ResponsePacket* rp)
{
  ULONG channelNumber = ntohl(*(ULONG*)data);
  data += 4;
  ULONG startTime = ntohl(*(ULONG*)data);
  data += 4;
  ULONG duration = ntohl(*(ULONG*)data);

  log->log("Client", Log::DEBUG, "get schedule called for channel %lu", channelNumber);

  cChannel* channel = channelFromNumber(channelNumber);
  if (!channel)
  {
    rp->addULONG(0);
    rp->finalise();
    tcp.sendPacket(rp->getPtr(), rp->getLen());
  
    log->log("Client", Log::DEBUG, "written 0 because channel = NULL");
    return 1;
  }

  log->log("Client", Log::DEBUG, "Got channel");

#if VDRVERSNUM < 10300
  cMutexLock MutexLock;
  const cSchedules *Schedules = cSIProcessor::Schedules(MutexLock);
#else
  cSchedulesLock MutexLock;
  const cSchedules *Schedules = cSchedules::Schedules(MutexLock);
#endif
  if (!Schedules)
  {
    rp->addULONG(0);
    rp->finalise();
    tcp.sendPacket(rp->getPtr(), rp->getLen());
    
    log->log("Client", Log::DEBUG, "written 0 because Schedule!s! = NULL");
    return 1;
  }

  log->log("Client", Log::DEBUG, "Got schedule!s! object");

  const cSchedule *Schedule = Schedules->GetSchedule(channel->GetChannelID());
  if (!Schedule)
  {
    rp->addULONG(0);
    rp->finalise();
    tcp.sendPacket(rp->getPtr(), rp->getLen());
    
    log->log("Client", Log::DEBUG, "written 0 because Schedule = NULL");
    return 1;
  }

  log->log("Client", Log::DEBUG, "Got schedule object");

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

    log->log("Client", Log::DEBUG, "Got an event object %p", event);

    //in the past filter
    if ((thisEventTime + thisEventDuration) < (ULONG)time(NULL)) continue;

    //start time filter
    if ((thisEventTime + thisEventDuration) <= startTime) continue;

    //duration filter
    if (thisEventTime >= (startTime + duration)) continue;

    if (!thisEventTitle) thisEventTitle = empty;
    if (!thisEventSubTitle) thisEventSubTitle = empty;
    if (!thisEventDescription) thisEventDescription = empty;

    rp->addULONG(thisEventID);
    rp->addULONG(thisEventTime);
    rp->addULONG(thisEventDuration);

    rp->addString(thisEventTitle);
    rp->addString(thisEventSubTitle);
    rp->addString(thisEventDescription);

    atLeastOneEvent = true;
    log->log("Client", Log::DEBUG, "Done s3");
  }

  log->log("Client", Log::DEBUG, "Got all event data");

  if (!atLeastOneEvent)
  {
    rp->addULONG(0);
    log->log("Client", Log::DEBUG, "Written 0 because no data");
  }
  
  rp->finalise();
  tcp.sendPacket(rp->getPtr(), rp->getLen());
    
  log->log("Client", Log::DEBUG, "written schedules packet");

  return 1;
}

#endif //VOMPSTANDALONE

int MVPClient::processConfigSave(UCHAR* buffer, int length, ResponsePacket* rp)
{
  char* section = (char*)buffer;
  char* key = NULL;
  char* value = NULL;

  for (int k = 0; k < length; k++)
  {
    if (buffer[k] == '\0')
    {
      if (!key)
      {
        key = (char*)&buffer[k+1];
      }
      else
      {
        value = (char*)&buffer[k+1];
        break;
      }
    }
  }

  // if the last string (value) doesnt have null terminator, give up
  if (buffer[length - 1] != '\0') return 0;

  log->log("Client", Log::DEBUG, "Config save: %s %s %s", section, key, value);
  if (config.setValueString(section, key, value))
  {
    rp->addULONG(1);
  }
  else
  {
    rp->addULONG(0);
  }

  rp->finalise();
  tcp.sendPacket(rp->getPtr(), rp->getLen());
  
  return 1;
}

int MVPClient::processConfigLoad(UCHAR* buffer, int length, ResponsePacket* rp)
{
  char* section = (char*)buffer;
  char* key = NULL;

  for (int k = 0; k < length; k++)
  {
    if (buffer[k] == '\0')
    {
      key = (char*)&buffer[k+1];
      break;
    }
  }

  char* value = config.getValueString(section, key);

  if (value)
  {
    rp->addString(value);
    log->log("Client", Log::DEBUG, "Written config load packet");
    delete[] value;
  }
  else
  {
    rp->addULONG(0);
    log->log("Client", Log::DEBUG, "Written config load failed packet");
  }

  rp->finalise();
  tcp.sendPacket(rp->getPtr(), rp->getLen());
  
  return 1;
}

#ifndef VOMPSTANDALONE

int MVPClient::processGetTimers(UCHAR* buffer, int length, ResponsePacket* rp)
{
  cTimer *timer;
  int numTimers = Timers.Count();

  rp->addULONG(numTimers);

  for (int i = 0; i < numTimers; i++)
  {
    timer = Timers.Get(i);

#if VDRVERSNUM < 10300
    rp->addULONG(timer->Active());
#else
    rp->addULONG(timer->HasFlags(tfActive));
#endif
    rp->addULONG(timer->Recording());
    rp->addULONG(timer->Pending());
    rp->addULONG(timer->Priority());
    rp->addULONG(timer->Lifetime());
    rp->addULONG(timer->Channel()->Number());
    rp->addULONG(timer->StartTime());
    rp->addULONG(timer->StopTime());
    rp->addULONG(timer->Day());
    rp->addULONG(timer->WeekDays());
    rp->addString(timer->File());
  }

  rp->finalise();
  tcp.sendPacket(rp->getPtr(), rp->getLen());
  
  log->log("Client", Log::DEBUG, "Written timers list");

  return 1;
}

int MVPClient::processSetTimer(UCHAR* buffer, int length, ResponsePacket* rp)
{
  char* timerString = new char[strlen((char*)buffer) + 1];
  strcpy(timerString, (char*)buffer);

#if VDRVERSNUM < 10300

  // If this is VDR 1.2 the date part of the timer string must be reduced
  // to just DD rather than YYYY-MM-DD

  int s = 0; // source
  int d = 0; // destination
  int c = 0; // count
  while(c != 2) // copy up to date section, including the second ':'
  {
    timerString[d] = buffer[s];
    if (buffer[s] == ':') c++;
    ++s;
    ++d;
  }
  // now it has copied up to the date section
  c = 0;
  while(c != 2) // waste YYYY-MM-
  {
    if (buffer[s] == '-') c++;
    ++s;
  }
  // now source is at the DD
  memcpy(&timerString[d], &buffer[s], length - s);
  d += length - s;
  timerString[d] = '\0';

  log->log("Client", Log::DEBUG, "Timer string after 1.2 conversion:");
  log->log("Client", Log::DEBUG, "%s", timerString);

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
      rp->addULONG(0);
      rp->finalise();
      tcp.sendPacket(rp->getPtr(), rp->getLen());
      return 1;
    }
    else
    {
      rp->addULONG(1);
      rp->finalise();
      tcp.sendPacket(rp->getPtr(), rp->getLen());
    }
  }
  else
  {
    rp->addULONG(2);
    rp->finalise();
    tcp.sendPacket(rp->getPtr(), rp->getLen());
  }
  delete timer;
  return 1;
}

int MVPClient::processDeleteTimer(UCHAR* buffer, int length, ResponsePacket* rp)
{
  log->log("Client", Log::DEBUG, "Delete timer called");
  // get timer
  
  int position = 0;
  
  INT delChannel = ntohl(*(ULONG*)&buffer[position]); position += 4;
  INT delWeekdays = ntohl(*(ULONG*)&buffer[position]); position += 4;
  INT delDay = ntohl(*(ULONG*)&buffer[position]); position += 4;  
  INT delStart = ntohl(*(ULONG*)&buffer[position]); position += 4;  
  INT delStop = ntohl(*(ULONG*)&buffer[position]); position += 4;
    
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
    rp->addULONG(4);
    rp->finalise();
    tcp.sendPacket(rp->getPtr(), rp->getLen());
    return 1;
  }
          
  if (!Timers.BeingEdited())
  {
    if (!ti->Recording())
    {
      Timers.Del(ti);
      Timers.SetModified();
      rp->addULONG(10);
      rp->finalise();
      tcp.sendPacket(rp->getPtr(), rp->getLen());
      return 1;
    }
    else
    {
      log->log("Client", Log::ERR, "Unable to delete timer - timer is running");
      rp->addULONG(3);
      rp->finalise();
      tcp.sendPacket(rp->getPtr(), rp->getLen());
      return 1;
    }  
  }
  else
  {
    log->log("Client", Log::ERR, "Unable to delete timer - timers being edited at VDR");
    rp->addULONG(1);
    rp->finalise();
    tcp.sendPacket(rp->getPtr(), rp->getLen());
    return 1;
  }  
}

int MVPClient::processGetRecInfo(UCHAR* data, int length, ResponsePacket* rp)
{
  // data is a pointer to the fileName string

  cRecordings Recordings;
  Recordings.Load(); // probably have to do this

  cRecording *recording = Recordings.GetByName((char*)data);

  time_t timerStart = 0;
  time_t timerStop = 0;
  char* summary = NULL;
  ULONG resumePoint = 0;

  if (!recording)
  {
    log->log("Client", Log::ERR, "GetRecInfo found no recording");
    rp->addULONG(0);
    rp->finalise();
    tcp.sendPacket(rp->getPtr(), rp->getLen());
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
    log->log("Client", Log::DEBUG, "GRI: RC: %lu %lu", timerStart, timerStop);
  }

  rp->addULONG(timerStart);
  rp->addULONG(timerStop);

  // Get resume point

  char* value = config.getValueString("ResumeData", (char*)data);
  if (value)
  {
    resumePoint = strtoul(value, NULL, 10);
    delete[] value;
  }
  log->log("Client", Log::DEBUG, "GRI: RP: %lu", resumePoint);

  rp->addULONG(resumePoint);


  // Get summary

#if VDRVERSNUM < 10300
  summary = (char*)recording->Summary();
#else
  const cRecordingInfo *Info = recording->Info();
  summary = (char*)Info->ShortText();
  if (isempty(summary)) summary = (char*)Info->Description();
#endif
  log->log("Client", Log::DEBUG, "GRI: S: %s", summary);
  if (summary)
  {
    rp->addString(summary);
  }
  else
  {
    rp->addString("");
  }

  // Get channels

#if VDRVERSNUM < 10300

  // Send 0 for numchannels - this signals the client this info is not available
  rp->addULONG(0);

#else
  const cComponents* components = Info->Components();

  log->log("Client", Log::DEBUG, "GRI: D1: %p", components);

  if (!components)
  {
    rp->addULONG(0);
  }
  else
  {
    rp->addULONG(components->NumComponents());
  
    tComponent* component;
    for (int i = 0; i < components->NumComponents(); i++)
    {
      component = components->Component(i);

      log->log("Client", Log::DEBUG, "GRI: C: %i %u %u %s %s", i, component->stream, component->type, component->language, component->description);
      
      rp->addUCHAR(component->stream);
      rp->addUCHAR(component->type);

      if (component->language)
      {
        rp->addString(component->language);
      }
      else
      {
        rp->addString("");
      }
      if (component->description)
      {
        rp->addString(component->description);
      }
      else
      {
        rp->addString("");
      }
    }
  }

#endif

  // Done. send it

  rp->finalise();
  tcp.sendPacket(rp->getPtr(), rp->getLen());

  log->log("Client", Log::DEBUG, "Written getrecinfo");

  return 1;
}




// FIXME obselete

int MVPClient::processReScanRecording(UCHAR* data, int length, ResponsePacket* rp)
{
  if (!recplayer)
  {
    log->log("Client", Log::DEBUG, "Rescan recording called when no recording being played!");
    return 0;
  }

  recplayer->scan();

  rp->addULLONG(recplayer->getLengthBytes());
  rp->addULONG(recplayer->getLengthFrames());
  rp->finalise();
  tcp.sendPacket(rp->getPtr(), rp->getLen());
  log->log("Client", Log::DEBUG, "Rescan recording, wrote new length to client");
  return 1;
}

// FIXME without client calling rescan, getblock wont work even tho more data is avail


int MVPClient::processGetMarks(UCHAR* data, int length, ResponsePacket* rp)
{
  // data is a pointer to the fileName string

  cMarks Marks;
  cRecordings Recordings;
  Recordings.Load(); // probably have to do this

  cRecording *recording = Recordings.GetByName((char*)data);

  log->log("Client", Log::DEBUG, "recording pointer %p", recording);

  if (recording)
  {
    Marks.Load(recording->FileName());
    if (Marks.Count())
    {
      for (const cMark *m = Marks.First(); m; m = Marks.Next(m))
      {
        log->log("Client", Log::DEBUG, "found Mark %i", m->position);

        rp->addULONG(m->position);
      }
    }
    else
    {
      log->log("Client", Log::DEBUG, "no marks found, sending 0-mark");
      rp->addULONG(0);
    }
  }

  rp->finalise();
  tcp.sendPacket(rp->getPtr(), rp->getLen());
  
  log->log("Client", Log::DEBUG, "Written Marks list");

  return 1;
}

#endif //VOMPSTANDALONE

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

int MVPClient::processGetMediaList(UCHAR* data, int length, ResponsePacket* rp)
{
  if (length < 4) {
    log->log("Client", Log::ERR, "getMediaList packet too short %d", length);
    return 0;
  }
  char * dirname=NULL;
  if (length > 4) {
    //we have a dirname provided
    dirname=(char *)&data[4];
    log->log("Client", Log::DEBUG, "getMediaList for %s", dirname);
  }

  MediaList * ml=MediaList::readList(baseConfig,dirname);
  if (ml == NULL) {
     log->log("Client", Log::ERR, "getMediaList returned NULL");
     return 0;
  }

  //response code (not yet set)
  rp->addULONG(0);

  //numentries
  rp->addULONG(ml->size());

  for (MediaList::iterator nm=ml->begin(); nm<ml->end(); nm++)
  {
    Media *m=*nm;
    log->log("Client", Log::DEBUG, "found media entry %s, type=%d",m->getFilename(),m->getType());
    rp->addULONG(m->getType());
    //time stamp
    rp->addULONG(m->getTime());
    //flags
    rp->addULONG(0);
    int len=strlen(m->getFilename());
    //strlen
    rp->addULONG(len+1);
    rp->addString(m->getFilename());
  }
  delete ml;

  rp->finalise();
  tcp.sendPacket(rp->getPtr(), rp->getLen());
  
  log->log("Client", Log::DEBUG, "Written Media list");
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

int MVPClient::processGetPicture(UCHAR* data, int length, ResponsePacket* rp)
{
  if (length < 12) {
    log->log("Client", Log::ERR, "getPicture packet too short %d", length);
    return 0;
  }
  if (imageFile) {
    fclose(imageFile);
    imageFile=NULL;
  }
  char * filename=NULL;
  if (length > 12) {
    //we have a dirname provided
    filename=(char *)&data[12];
    log->log("Client", Log::DEBUG, "getPicture  %s", filename);
  }
  else {
    log->log("Client", Log::ERR, "getPicture  empty filename");
  }
  if (filename) {
    imageFile=fopen(filename,"r");
    if (!imageFile) log->log("Client", Log::ERR, "getPicture unable to open %s",filename);
  }
  int size=0;
  if (imageFile) {
    struct stat st;
    if ( fstat(fileno(imageFile),&st) == 0) size=st.st_size;
  }
  //response code (not yet set)
  rp->addULONG(31);
  //size
  rp->addULONG(size);

  log->log("Client", Log::DEBUG, "getPicture size  %u", size);

  rp->finalise();
  tcp.sendPacket(rp->getPtr(), rp->getLen());

  log->log("Client", Log::DEBUG, "Written getPicture");

  return 1;
}


int MVPClient::processGetImageBlock(UCHAR* data, int length, ResponsePacket* rp)
{
  if (!imageFile)
  {
    log->log("Client", Log::DEBUG, "Get image block called when no image active");
    return 0;
  }

  ULLONG position = ntohll(*(ULLONG*)data);
  data += sizeof(ULLONG);
  ULONG amount = ntohl(*(ULONG*)data);

  log->log("Client", Log::DEBUG, "getImageblock pos = %llu length = %lu", position, amount);

  UCHAR sendBuffer[amount];
  ULONG amountReceived = 0; // compiler moan.
  ULLONG cpos=ftell(imageFile);
  if (position != cpos) {
    fseek(imageFile,position-cpos,SEEK_CUR);
  }
  if (position != (ULLONG)ftell(imageFile)) {
    log->log("Client", Log::DEBUG, "getImageblock pos = %llu not available", position);
  }
  else {
    amountReceived=fread(&sendBuffer[0],1,amount,imageFile);
  }

  if (!amountReceived)
  {
    rp->addULONG(0);
    log->log("Client", Log::DEBUG, "written 4(0) as getblock got 0");
  }
  else
  {
    rp->copyin(sendBuffer, amount);
    log->log("Client", Log::DEBUG, "written %lu", amountReceived);
  }
  
  rp->finalise();
  tcp.sendPacket(rp->getPtr(), rp->getLen());

  return 1;
}


int MVPClient::processGetLanguageList(UCHAR* data, int length, ResponsePacket* rp)
{
  i18n.findLanguages();
  const I18n::lang_code_list& languages = i18n.getLanguageList();
  std::string result;
  I18n::lang_code_list::const_iterator iter;
  for (iter = languages.begin(); iter != languages.end(); ++iter)
  {
    rp->addString(iter->first.c_str());
    rp->addString(iter->second.c_str());
  }
  rp->finalise();
  tcp.sendPacket(rp->getPtr(), rp->getLen());
  return 1;
}

int MVPClient::processGetLanguageContent(UCHAR* data, int length, ResponsePacket* rp)
{
  if (length <= 0) return 0;
  std::string code, result;
  code.assign((char*)data, length - 1);
  i18n.findLanguages();
  I18n::trans_table texts = i18n.getLanguageContent(code);
  I18n::trans_table::const_iterator iter;
  for (iter = texts.begin(); iter != texts.end(); ++iter)
  {
    rp->addString(iter->first.c_str());
    rp->addString(iter->second.c_str());
  }
  rp->finalise();
  tcp.sendPacket(rp->getPtr(), rp->getLen());
  return 1;
}














/*
    event = Schedule->GetPresentEvent();

    fprintf(f, "\n\nCurrent event\n\n");

    fprintf(f, "Event %i eventid = %u time = %lu duration = %li\n", 0, event->GetEventID(), event->GetTime(), event->GetDuration());
    fprintf(f, "Event %i title = %s subtitle = %s\n", 0, event->GetTitle(), event->GetSubtitle());
    fprintf(f, "Event %i extendeddescription = %s\n", 0, event->GetExtendedDescription());
    fprintf(f, "Event %i isFollowing = %i, isPresent = %i\n", 0, event->IsFollowing(), event->IsPresent());

    event = Schedule->GetFollowingEvent();

    fprintf(f, "\n\nFollowing event\n\n");

    fprintf(f, "Event %i eventid = %u time = %lu duration = %li\n", 0, event->GetEventID(), event->GetTime(), event->GetDuration());
    fprintf(f, "Event %i title = %s subtitle = %s\n", 0, event->GetTitle(), event->GetSubtitle());
    fprintf(f, "Event %i extendeddescription = %s\n", 0, event->GetExtendedDescription());
    fprintf(f, "Event %i isFollowing = %i, isPresent = %i\n", 0, event->IsFollowing(), event->IsPresent());

    fprintf(f, "\n\n");

    fprintf(f, "Event %i eventid = %u time = %lu duration = %li\n", eventNumber, event->GetEventID(), event->GetTime(), event->GetDuration());
    fprintf(f, "Event %i title = %s subtitle = %s\n", eventNumber, event->GetTitle(), event->GetSubtitle());
    fprintf(f, "Event %i extendeddescription = %s\n", eventNumber, event->GetExtendedDescription());
    fprintf(f, "Event %i isFollowing = %i, isPresent = %i\n", eventNumber, event->IsFollowing(), event->IsPresent());

    fprintf(f, "\n\n");



void MVPClient::test2()
{
  FILE* f = fopen("/tmp/s.txt", "w");

#if VDRVERSNUM < 10300
  cMutexLock MutexLock;
  const cSchedules *Schedules = cSIProcessor::Schedules(MutexLock);
#else
  cSchedulesLock MutexLock;
  const cSchedules *Schedules = cSchedules::Schedules(MutexLock);
#endif

  if (!Schedules)
  {
    fprintf(f, "Schedules = NULL\n");
    fclose(f);
    return;
  }

  fprintf(f, "Schedules dump:\n");
  Schedules->Dump(f);


  const cSchedule *Schedule;
  int scheduleNumber = 0;

  tChannelID tchid;
  cChannel *thisChannel;

#if VDRVERSNUM < 10300
  const cEventInfo *event;
  int eventNumber = 0;
#else
  const cEvent *event;
#endif

//    Schedule = Schedules->GetSchedule(channel->GetChannelID());
//    Schedule = Schedules->GetSchedule();
  Schedule = Schedules->First();
  if (!Schedule)
  {
    fprintf(f, "First Schedule = NULL\n");
    fclose(f);
    return;
  }

  while (Schedule)
  {
    fprintf(f, "Schedule #%i\n", scheduleNumber);
    fprintf(f, "-------------\n\n");

#if VDRVERSNUM < 10300
    tchid = Schedule->GetChannelID();
#else
    tchid = Schedule->ChannelID();
#endif

#if VDRVERSNUM < 10300
    fprintf(f, "ChannelID.ToString() = %s\n", tchid.ToString());
    fprintf(f, "NumEvents() = %i\n", Schedule->NumEvents());
#else
//  put the count at the end.
#endif

    thisChannel = Channels.GetByChannelID(tchid, true);
    if (thisChannel)
    {
      fprintf(f, "Channel Number: %p %i\n", thisChannel, thisChannel->Number());
    }
    else
    {
      fprintf(f, "thisChannel = NULL for tchid\n");
    }

#if VDRVERSNUM < 10300
    for (eventNumber = 0; eventNumber < Schedule->NumEvents(); eventNumber++)
    {
      event = Schedule->GetEventNumber(eventNumber);
      fprintf(f, "Event %i tableid = %i timestring = %s endtimestring = %s\n", eventNumber, event->GetTableID(), event->GetTimeString(), event->GetEndTimeString());
      fprintf(f, "Event %i date = %s isfollowing = %i ispresent = %i\n", eventNumber, event->GetDate(), event->IsFollowing(), event->IsPresent());
      fprintf(f, "Event %i extendeddescription = %s\n", eventNumber, event->GetExtendedDescription());
      fprintf(f, "Event %i subtitle = %s title = %s\n", eventNumber, event->GetSubtitle(), event->GetTitle());
      fprintf(f, "Event %i eventid = %u duration = %li time = %lu channelnumber = %i\n", eventNumber, event->GetEventID(), event->GetDuration(), event->GetTime(), event->GetChannelNumber());
      fprintf(f, "Event %u dump:\n", eventNumber);
      event->Dump(f);
      fprintf(f, "\n\n");
    }
#else
//  This whole section needs rewriting to walk the list.
    event = Schedule->Events()->First();
    while (event) {
      event = Schedule->Events()->Next(event);
    }
#endif


    fprintf(f, "\nDump from object:\n");
    Schedule->Dump(f);
    fprintf(f, "\nEND\n");




    fprintf(f, "End of current Schedule\n\n\n");

    Schedule = (const cSchedule *)Schedules->Next(Schedule);
    scheduleNumber++;
  }

  fclose(f);
}


  const cEventInfo *GetPresentEvent(void) const;
  const cEventInfo *GetFollowingEvent(void) const;
  const cEventInfo *GetEvent(unsigned short uEventID, time_t tTime = 0) const;
  const cEventInfo *GetEventAround(time_t tTime) const;
  const cEventInfo *GetEventNumber(int n) const { return Events.Get(n); }


  const unsigned char GetTableID(void) const;
  const char *GetTimeString(void) const;
  const char *GetEndTimeString(void) const;
  const char *GetDate(void) const;
  bool IsFollowing(void) const;
  bool IsPresent(void) const;
  const char *GetExtendedDescription(void) const;
  const char *GetSubtitle(void) const;
  const char *GetTitle(void) const;
  unsigned short GetEventID(void) const;
  long GetDuration(void) const;
  time_t GetTime(void) const;
  tChannelID GetChannelID(void) const;
  int GetChannelNumber(void) const { return nChannelNumber; }
  void SetChannelNumber(int ChannelNumber) const { ((cEventInfo *)this)->nChannelNumber = ChannelNumber; } // doesn't modify the EIT data, so it's ok to make it 'const'
  void Dump(FILE *f, const char *Prefix = "") const;



void MVPClient::test(int channelNumber)
{
  FILE* f = fopen("/tmp/test.txt", "w");

  cMutexLock MutexLock;
  const cSchedules *Schedules = cSIProcessor::Schedules(MutexLock);

  if (!Schedules)
  {
    fprintf(f, "Schedules = NULL\n");
    fclose(f);
    return;
  }

  fprintf(f, "Schedules dump:\n");
//  Schedules->Dump(f);

  const cSchedule *Schedule;
  cChannel *thisChannel;
  const cEventInfo *event;

  thisChannel = channelFromNumber(channelNumber);
  if (!thisChannel)
  {
    fprintf(f, "thisChannel = NULL\n");
    fclose(f);
    return;
  }

  Schedule = Schedules->GetSchedule(thisChannel->GetChannelID());
//    Schedule = Schedules->GetSchedule();
//  Schedule = Schedules->First();
  if (!Schedule)
  {
    fprintf(f, "First Schedule = NULL\n");
    fclose(f);
    return;
  }

  fprintf(f, "NumEvents() = %i\n\n", Schedule->NumEvents());

  // For some channels VDR seems to pick a random point in time to
  // start dishing out events, but they are in order
  // at some point in the list the time snaps to the current event




  for (int eventNumber = 0; eventNumber < Schedule->NumEvents(); eventNumber++)
  {
    event = Schedule->GetEventNumber(eventNumber);
    fprintf(f, "Event %i eventid = %u time = %lu duration = %li\n", eventNumber, event->GetEventID(), event->GetTime(), event->GetDuration());
    fprintf(f, "Event %i title = %s subtitle = %s\n", eventNumber, event->GetTitle(), event->GetSubtitle());
    fprintf(f, "Event %i extendeddescription = %s\n", eventNumber, event->GetExtendedDescription());
    fprintf(f, "\n\n");
  }

  fprintf(f, "\nEND\n");

  fclose(f);
}



Right, so

Schedules = the collection of all the Schedule objects
Schedule  = One schedule, contants all the events for a channel
Event     = One programme


Want:

Event ID
Time
Duration
Title
Subtitle (used for "Programmes resume at ...")
Description

IsPresent ? easy to work out tho. Oh it doesn't always work


void MVPClient::test2()
{
  log->log("-", Log::DEBUG, "Timers List");

  for (int i = 0; i < Timers.Count(); i++)
  {
    cTimer *timer = Timers.Get(i);
    //Reply(i < Timers.Count() - 1 ? -250 : 250, "%d %s", timer->Index() + 1, timer->ToText());
    log->log("-", Log::DEBUG, "i=%i count=%i index=%d", i, Timers.Count(), timer->Index() + 1);
#if VDRVERSNUM < 10300
    log->log("-", Log::DEBUG, "active=%i recording=%i pending=%i start=%li stop=%li priority=%i lifetime=%i", timer->Active(), timer->Recording(), timer->Pending(), timer->StartTime(), timer->StopTime(), timer->Priority(), timer->Lifetime());
#else
    log->log("-", Log::DEBUG, "active=%i recording=%i pending=%i start=%li stop=%li priority=%i lifetime=%i", timer->HasFlags(tfActive), timer->Recording(), timer->Pending(), timer->StartTime(), timer->StopTime(), timer->Priority(), timer->Lifetime());
#endif
    log->log("-", Log::DEBUG, "channel=%i file=%s summary=%s", timer->Channel()->Number(), timer->File(), timer->Summary());
    log->log("-", Log::DEBUG, "");
  }

  // asprintf(&buffer, "%d:%s:%s  :%04d:%04d:%d:%d:%s:%s\n",
//            active, (UseChannelID ? Channel()->GetChannelID().ToString() : itoa(Channel()->Number())),
//            PrintDay(day, firstday), start, stop, priority, lifetime, file, summary ? summary : "");
}


Active seems to be a bool - whether the timer should be done or not. If set to inactive it stays around after its time
recording is a bool, 0 for not currently recording, 1 for currently recording
pending is a bool, 0 for would not be trying to record this right now, 1 for would/is trying to record this right now
*/

