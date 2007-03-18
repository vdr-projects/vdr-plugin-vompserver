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

// This is here else it causes compile errors with something in libdvbmpeg
#include <vdr/menu.h>

MVPClient::MVPClient(char* tconfigDirExtra, int tsocket)
 : tcp(tsocket)
{
  lp = NULL;
  rp = NULL;
  recordingManager = NULL;
  log = Log::getInstance();
  loggedIn = false;
  configDirExtra = tconfigDirExtra;
}

MVPClient::~MVPClient()
{
  log->log("Client", Log::DEBUG, "MVP client destructor");
  if (lp)
  {
    delete lp;
    lp = NULL;
  }
  else if (rp)
  {
    writeResumeData();

    delete rp;
    delete recordingManager;
    rp = NULL;
    recordingManager = NULL;
  }

  if (loggedIn) cleanConfig();
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
                          (char*)rp->getCurrentRecording()->FileName(),
                          rp->frameNumberFromPosition(rp->getLastPosition()) );
}

void MVPClient::sendULONG(ULONG ul)
{
  UCHAR sendBuffer[8];
  *(ULONG*)&sendBuffer[0] = htonl(4);
  *(ULONG*)&sendBuffer[4] = htonl(ul);

  tcp.sendPacket(sendBuffer, 8);
  log->log("Client", Log::DEBUG, "written ULONG %lu", ul);
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

  UCHAR* buffer;
  UCHAR* data;
  int packetLength;
  ULONG opcode;
  int result = 0;

  while(1)
  {
    log->log("Client", Log::DEBUG, "Waiting");
    buffer = (UCHAR*)tcp.receivePacket();
    log->log("Client", Log::DEBUG, "Received packet, length = %u", tcp.getDataLength());
    if (buffer == NULL)
    {
      log->log("Client", Log::DEBUG, "Detected connection closed");
      break;
    }

    packetLength = tcp.getDataLength() - 4;
    opcode = ntohl(*(ULONG*)buffer);
    data = buffer + 4;

    if (!loggedIn && (opcode != 1))
    {
      free(buffer);
      break;
    }

    log->log("Client", Log::DEBUG, "SwitchOp");
    switch(opcode)
    {
      case 1:
        result = processLogin(data, packetLength);
        break;
      case 2:
        result = processGetRecordingsList(data, packetLength);
        break;
      case 3:
        result = processDeleteRecording(data, packetLength);
        break;
      case 5:
        result = processGetChannelsList(data, packetLength);
        break;
      case 6:
        result = processStartStreamingChannel(data, packetLength);
        break;
      case 7:
        result = processGetBlock(data, packetLength);
        break;
      case 8:
        result = processStopStreaming(data, packetLength);
        break;
      case 9:
        result = processStartStreamingRecording(data, packetLength);
        break;
      case 10:
        result = processGetChannelSchedule(data, packetLength);
        break;
      case 11:
        result = processConfigSave(data, packetLength);
        break;
      case 12:
        result = processConfigLoad(data, packetLength);
        break;
      case 13:
        result = processReScanRecording(data, packetLength);         // FIXME obselete
        break;
      case 14:
        result = processGetTimers(data, packetLength);
        break;
      case 15:
        result = processSetTimer(data, packetLength);
        break;
      case 16:
        result = processPositionFromFrameNumber(data, packetLength);
        break;
      case 17:
        result = processFrameNumberFromPosition(data, packetLength);
        break;
      case 18:
        result = processMoveRecording(data, packetLength);
        break;
      case 19:
        result = processGetIFrame(data, packetLength);
        break;
      case 20:
        result = processGetRecInfo(data, packetLength);
        break;
      case 21:
        result = processGetMarks(data, packetLength);
        break;
      case 22:
        result = processGetChannelPids(data, packetLength);
        break;
    }

    free(buffer);
    if (!result) break;
  }
}

int MVPClient::processLogin(UCHAR* buffer, int length)
{
  if (length != 6) return 0;

  // Open the config

  const char* configDir = cPlugin::ConfigDirectory(configDirExtra);
  if (!configDir)
  {
    log->log("Client", Log::DEBUG, "No config dir!");
    return 0;
  }

  char configFileName[PATH_MAX];
  snprintf(configFileName, PATH_MAX, "%s/vomp-%02X-%02X-%02X-%02X-%02X-%02X.conf", configDir, buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5]);
  config.init(configFileName);

  // Send the login reply

  time_t timeNow = time(NULL);
  struct tm* timeStruct = localtime(&timeNow);
  int timeOffset = timeStruct->tm_gmtoff;

  UCHAR sendBuffer[12];
  *(ULONG*)&sendBuffer[0] = htonl(8);
  *(ULONG*)&sendBuffer[4] = htonl(timeNow);
  *(signed int*)&sendBuffer[8] = htonl(timeOffset);

  tcp.sendPacket(sendBuffer, 12);
  log->log("Client", Log::DEBUG, "written login reply");

  loggedIn = true;
  return 1;
}

int MVPClient::processGetRecordingsList(UCHAR* data, int length)
{
  UCHAR* sendBuffer = new UCHAR[50000]; // hope this is enough
  int count = 4; // leave space for the packet length
  char* point;


  int FreeMB;
  int Percent = VideoDiskSpace(&FreeMB);
  int Total = (FreeMB / (100 - Percent)) * 100;

  *(ULONG*)&sendBuffer[count] = htonl(Total);
  count += sizeof(ULONG);
  *(ULONG*)&sendBuffer[count] = htonl(FreeMB);
  count += sizeof(ULONG);
  *(ULONG*)&sendBuffer[count] = htonl(Percent);
  count += sizeof(ULONG);


  cRecordings Recordings;
  Recordings.Load();

  for (cRecording *recording = Recordings.First(); recording; recording = Recordings.Next(recording))
  {
    if (count > 49000) break; // just how big is that hard disk?!
    *(ULONG*)&sendBuffer[count] = htonl(recording->start);// + timeOffset);
    count += 4;

    point = (char*)recording->Name();
    strcpy((char*)&sendBuffer[count], point);
    count += strlen(point) + 1;

    point = (char*)recording->FileName();
    strcpy((char*)&sendBuffer[count], point);
    count += strlen(point) + 1;
  }

  *(ULONG*)&sendBuffer[0] = htonl(count - 4); // -4 :  take off the size field

  log->log("Client", Log::DEBUG, "recorded size as %u", ntohl(*(ULONG*)&sendBuffer[0]));

  tcp.sendPacket(sendBuffer, count);
  delete[] sendBuffer;
  log->log("Client", Log::DEBUG, "Written list");

  return 1;
}

int MVPClient::processDeleteRecording(UCHAR* data, int length)
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
        sendULONG(1);
      }
      else
      {
        sendULONG(2);
      }
    }
    else
    {
      sendULONG(3);
    }
  }
  else
  {
    sendULONG(4);
  }

  return 1;
}

int MVPClient::processMoveRecording(UCHAR* data, int length)
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
          sendULONG(5);
          return 1;
        }
      }
      else if ((statret == 0) && (! (dstat.st_mode && S_IFDIR))) // Something exists but it's not a dir
      {
        delete[] dateDirName;
        delete[] titleDirName;
        delete[] newContainer;
        sendULONG(5);
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
        int totalLength = 4 + 4 + strlen(newDir) + 1;
        UCHAR* sendBuffer = new UCHAR[totalLength];
        *(ULONG*)&sendBuffer[0] = htonl(totalLength - 4);
        *(ULONG*)&sendBuffer[4] = htonl(1); // success
        strcpy((char*)&sendBuffer[8], newDir);
        tcp.sendPacket(sendBuffer, totalLength);
        delete[] sendBuffer;
      }
      else
      {
        sendULONG(5);
      }

      delete[] dateDirName;
      delete[] titleDirName;
      delete[] newContainer;
      delete[] newDir;
    }
    else
    {
      sendULONG(3);
    }
  }
  else
  {
    sendULONG(4);
  }

  return 1;
}

int MVPClient::processGetChannelsList(UCHAR* data, int length)
{
  UCHAR* sendBuffer = new UCHAR[50000]; // FIXME hope this is enough
  int count = 4; // leave space for the packet length
  char* point;
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

      if (count > 49000) break;
      *(ULONG*)&sendBuffer[count] = htonl(channel->Number());
      count += 4;

      *(ULONG*)&sendBuffer[count] = htonl(type);
      count += 4;

      point = (char*)channel->Name();
      strcpy((char*)&sendBuffer[count], point);
      count += strlen(point) + 1;
    }
  }

  *(ULONG*)&sendBuffer[0] = htonl(count - 4); // -4 :  take off the size field

  log->log("Client", Log::DEBUG, "recorded size as %u", ntohl(*(ULONG*)&sendBuffer[0]));

  tcp.sendPacket(sendBuffer, count);
  delete[] sendBuffer;
  log->log("Client", Log::DEBUG, "Written channels list");

  return 1;
}

int MVPClient::processGetChannelPids(UCHAR* data, int length)
{
  ULONG channelNumber = ntohl(*(ULONG*)data);

  cChannel* channel = channelFromNumber(channelNumber);
  if (!channel)
  {
    sendULONG(0);
    return 1;
  }

  ULONG numApids = 0;
  ULONG spaceRequired = 12; // 4 for length field, 4 for vpid, 4 for number of apids
  // Work out space required and number of Apids

#if VDRVERSNUM < 10300

  log->log("Client", Log::DEBUG, "Apid1: %i", channel->Apid1());
  log->log("Client", Log::DEBUG, "Apid2: %i", channel->Apid2());

  if (channel->Apid2())
  {
    numApids = 2;
    spaceRequired += 10; // 8 + 2 nulls
  }
  else if (channel->Apid1())
  {
    numApids = 1;
    spaceRequired += 5; // 4 + 1 null
  }
  else
  {
    numApids = 0;
  }

#else

  for (const int *Apid = channel->Apids(); *Apid; Apid++)
  {
    spaceRequired += 4 + strlen(channel->Alang(numApids)) + 1; // 4 for pid, length of string + \0
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

  UCHAR* sendBuffer = new UCHAR[spaceRequired];
  ULONG point = 0;
  *(ULONG*)&sendBuffer[point] = htonl(spaceRequired - 4);   point += 4;   // take off first 4 bytes
  *(ULONG*)&sendBuffer[point] = htonl(channel->Vpid());     point += 4;
  *(ULONG*)&sendBuffer[point] = htonl(numApids);            point += 4;

#if VDRVERSNUM < 10300
  if (numApids >= 1)
  {
    *(ULONG*)&sendBuffer[point] = htonl(channel->Apid1());  point += 4;
    sendBuffer[point] = '\0';                               point += 1;
  }
  if (numApids == 2)
  {
    *(ULONG*)&sendBuffer[point] = htonl(channel->Apid2());  point += 4;
    sendBuffer[point] = '\0';                               point += 1;
  }
#else
  for (ULONG i = 0; i < numApids; i++)
  {
    *(ULONG*)&sendBuffer[point] = htonl(channel->Apid(i));  point += 4;
    strcpy((char*)&sendBuffer[point], channel->Alang(i));   point += strlen(channel->Alang(i)) + 1;
  }
#endif

//  printf("About to send getchannelpids response. length = %u\n", spaceRequired);
  tcp.dump(sendBuffer, spaceRequired);

  tcp.sendPacket(sendBuffer, spaceRequired);
  delete[] sendBuffer;
  log->log("Client", Log::DEBUG, "Written channels pids");

  return 1;
}

int MVPClient::processStartStreamingChannel(UCHAR* data, int length)
{
  log->log("Client", Log::DEBUG, "length = %i", length);
  ULONG channelNumber = ntohl(*(ULONG*)data);

  cChannel* channel = channelFromNumber(channelNumber);
  if (!channel)
  {
    sendULONG(0);
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
    sendULONG(0);
    return 1;
  }

  if (!lp->init())
  {
    delete lp;
    lp = NULL;
    sendULONG(0);
    return 1;
  }

  sendULONG(1);
  return 1;
}

int MVPClient::processStopStreaming(UCHAR* data, int length)
{
  log->log("Client", Log::DEBUG, "STOP STREAMING RECEIVED");
  if (lp)
  {
    delete lp;
    lp = NULL;
  }
  else if (rp)
  {
    writeResumeData();

    delete rp;
    delete recordingManager;
    rp = NULL;
    recordingManager = NULL;
  }

  sendULONG(1);
  return 1;
}

int MVPClient::processGetBlock(UCHAR* data, int length)
{
  if (!lp && !rp)
  {
    log->log("Client", Log::DEBUG, "Get block called when no streaming happening!");
    return 0;
  }

  ULLONG position = ntohll(*(ULLONG*)data);
  data += sizeof(ULLONG);
  ULONG amount = ntohl(*(ULONG*)data);

  log->log("Client", Log::DEBUG, "getblock pos = %llu length = %lu", position, amount);

  UCHAR sendBuffer[amount + 4];
  ULONG amountReceived = 0; // compiler moan.
  if (lp)
  {
    log->log("Client", Log::DEBUG, "getting from live");
    amountReceived = lp->getBlock(&sendBuffer[4], amount);

    if (!amountReceived)
    {
      // vdr has possibly disconnected the receiver
      log->log("Client", Log::DEBUG, "VDR has disconnected the live receiver");
      delete lp;
      lp = NULL;
    }
  }
  else if (rp)
  {
    log->log("Client", Log::DEBUG, "getting from recording");
    amountReceived = rp->getBlock(&sendBuffer[4], position, amount);
  }

  if (!amountReceived)
  {
    sendULONG(0);
    log->log("Client", Log::DEBUG, "written 4(0) as getblock got 0");
  }
  else
  {
    *(ULONG*)&sendBuffer[0] = htonl(amountReceived);
    tcp.sendPacket(sendBuffer, amountReceived + 4);
    log->log("Client", Log::DEBUG, "written ok %lu", amountReceived);
  }

  return 1;
}

int MVPClient::processStartStreamingRecording(UCHAR* data, int length)
{
  // data is a pointer to the fileName string

  recordingManager = new cRecordings;
  recordingManager->Load();

  cRecording* recording = recordingManager->GetByName((char*)data);

  log->log("Client", Log::DEBUG, "recording pointer %p", recording);

  if (recording)
  {
    rp = new RecPlayer(recording);

    UCHAR sendBuffer[16];
    *(ULONG*)&sendBuffer[0] = htonl(12);
    *(ULLONG*)&sendBuffer[4] = htonll(rp->getLengthBytes());
    *(ULONG*)&sendBuffer[12] = htonl(rp->getLengthFrames());

    tcp.sendPacket(sendBuffer, 16);
    log->log("Client", Log::DEBUG, "written totalLength");
  }
  else
  {
    delete recordingManager;
    recordingManager = NULL;
  }
  return 1;
}

int MVPClient::processPositionFromFrameNumber(UCHAR* data, int length)
{
  ULLONG retval = 0;

  ULONG frameNumber = ntohl(*(ULONG*)data);
  data += 4;

  if (!rp)
  {
    log->log("Client", Log::DEBUG, "Rescan recording called when no recording being played!");
  }
  else
  {
    retval = rp->positionFromFrameNumber(frameNumber);
  }

  UCHAR sendBuffer[12];
  *(ULONG*)&sendBuffer[0] = htonl(8);
  *(ULLONG*)&sendBuffer[4] = htonll(retval);

  tcp.sendPacket(sendBuffer, 12);
  log->log("Client", Log::DEBUG, "Wrote posFromFrameNum reply to client");
  return 1;
}

int MVPClient::processFrameNumberFromPosition(UCHAR* data, int length)
{
  ULONG retval = 0;

  ULLONG position = ntohll(*(ULLONG*)data);
  data += 8;

  if (!rp)
  {
    log->log("Client", Log::DEBUG, "Rescan recording called when no recording being played!");
  }
  else
  {
    retval = rp->frameNumberFromPosition(position);
  }

  UCHAR sendBuffer[8];
  *(ULONG*)&sendBuffer[0] = htonl(4);
  *(ULONG*)&sendBuffer[4] = htonl(retval);

  tcp.sendPacket(sendBuffer, 8);
  log->log("Client", Log::DEBUG, "Wrote frameNumFromPos reply to client");
  return 1;
}

int MVPClient::processGetIFrame(UCHAR* data, int length)
{
  bool success = false;

  ULONG frameNumber = ntohl(*(ULONG*)data);
  data += 4;
  ULONG direction = ntohl(*(ULONG*)data);
  data += 4;

  ULLONG rfilePosition = 0;
  ULONG rframeNumber = 0;
  ULONG rframeLength = 0;

  if (!rp)
  {
    log->log("Client", Log::DEBUG, "GetIFrame recording called when no recording being played!");
  }
  else
  {
    success = rp->getNextIFrame(frameNumber, direction, &rfilePosition, &rframeNumber, &rframeLength);
  }

  // returns file position, frame number, length

  UCHAR sendBuffer[20];
  int packetLength;

  if (success)
  {
    packetLength = 20;
    *(ULONG*)&sendBuffer[0] = htonl(16);
    *(ULLONG*)&sendBuffer[4] = htonll(rfilePosition);
    *(ULONG*)&sendBuffer[12] = htonl(rframeNumber);
    *(ULONG*)&sendBuffer[16] = htonl(rframeLength);
  }
  else
  {
    packetLength = 8;
    *(ULONG*)&sendBuffer[0] = htonl(4);
    *(ULONG*)&sendBuffer[4] = 0;
  }

  log->log("Client", Log::DEBUG, "%llu %lu %lu", rfilePosition, rframeNumber, rframeLength);

  tcp.sendPacket(sendBuffer, packetLength);
  log->log("Client", Log::DEBUG, "Wrote GNIF reply to client");
  return 1;
}

int MVPClient::processGetChannelSchedule(UCHAR* data, int length)
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
    sendULONG(0);
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
    sendULONG(0);
    log->log("Client", Log::DEBUG, "written 0 because Schedule!s! = NULL");
    return 1;
  }

  log->log("Client", Log::DEBUG, "Got schedule!s! object");

  const cSchedule *Schedule = Schedules->GetSchedule(channel->GetChannelID());
  if (!Schedule)
  {
    sendULONG(0);
    log->log("Client", Log::DEBUG, "written 0 because Schedule = NULL");
    return 1;
  }

  log->log("Client", Log::DEBUG, "Got schedule object");

  UCHAR* sendBuffer = (UCHAR*)malloc(100000);
  ULONG sendBufferLength = 100000;
  ULONG sendBufferUsed = sizeof(ULONG); // leave a hole for the entire packet length

  char* empty = "";

  // assign all the event info to temp vars then we know exactly what size they are
  ULONG thisEventID;
  ULONG thisEventTime;
  ULONG thisEventDuration;
  const char* thisEventTitle;
  const char* thisEventSubTitle;
  const char* thisEventDescription;

  ULONG constEventLength = sizeof(thisEventID) + sizeof(thisEventTime) + sizeof(thisEventDuration);
  ULONG thisEventLength;

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

    thisEventLength = constEventLength + strlen(thisEventTitle) + 1 + strlen(thisEventSubTitle) + 1 + strlen(thisEventDescription) + 1;

    log->log("Client", Log::DEBUG, "Done s1");

    // now extend the buffer if necessary
    if ((sendBufferUsed + thisEventLength) > sendBufferLength)
    {
      log->log("Client", Log::DEBUG, "Extending buffer");
      sendBufferLength += 100000;
      UCHAR* temp = (UCHAR*)realloc(sendBuffer, sendBufferLength);
      if (temp == NULL)
      {
        free(sendBuffer);
        UCHAR sendBuffer2[8];
        *(ULONG*)&sendBuffer2[0] = htonl(4);
        *(ULONG*)&sendBuffer2[4] = htonl(0);
        tcp.sendPacket(sendBuffer2, 8);
        log->log("Client", Log::DEBUG, "written 0 because failed to realloc packet");
        return 1;
      }
      sendBuffer = temp;
    }

    log->log("Client", Log::DEBUG, "Done s2");

    *(ULONG*)&sendBuffer[sendBufferUsed] = htonl(thisEventID);       sendBufferUsed += sizeof(ULONG);
    *(ULONG*)&sendBuffer[sendBufferUsed] = htonl(thisEventTime);     sendBufferUsed += sizeof(ULONG);
    *(ULONG*)&sendBuffer[sendBufferUsed] = htonl(thisEventDuration); sendBufferUsed += sizeof(ULONG);

    strcpy((char*)&sendBuffer[sendBufferUsed], thisEventTitle);       sendBufferUsed += strlen(thisEventTitle) + 1;
    strcpy((char*)&sendBuffer[sendBufferUsed], thisEventSubTitle);    sendBufferUsed += strlen(thisEventSubTitle) + 1;
    strcpy((char*)&sendBuffer[sendBufferUsed], thisEventDescription); sendBufferUsed += strlen(thisEventDescription) + 1;

    log->log("Client", Log::DEBUG, "Done s3 %lu", sendBufferUsed);
  }

  log->log("Client", Log::DEBUG, "Got all event data");

  if (sendBufferUsed == sizeof(ULONG))
  {
    // No data
    sendULONG(0);
    log->log("Client", Log::DEBUG, "Written 0 because no data");
  }
  else
  {
    // Write the length into the first 4 bytes. It's sendBufferUsed - 4 because of the hole!
    *(ULONG*)&sendBuffer[0] = htonl(sendBufferUsed - sizeof(ULONG));
    tcp.sendPacket(sendBuffer, sendBufferUsed);
    log->log("Client", Log::DEBUG, "written %lu schedules packet", sendBufferUsed);
  }

  free(sendBuffer);

  return 1;
}

int MVPClient::processConfigSave(UCHAR* buffer, int length)
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
    sendULONG(1);
  }
  else
  {
    sendULONG(0);
  }

  return 1;
}

int MVPClient::processConfigLoad(UCHAR* buffer, int length)
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
    UCHAR sendBuffer[4 + strlen(value) + 1];
    *(ULONG*)&sendBuffer[0] = htonl(strlen(value) + 1);
    strcpy((char*)&sendBuffer[4], value);
    tcp.sendPacket(sendBuffer, 4 + strlen(value) + 1);

    log->log("Client", Log::DEBUG, "Written config load packet");
    delete[] value;
  }
  else
  {
    UCHAR sendBuffer[8];
    *(ULONG*)&sendBuffer[0] = htonl(4);
    *(ULONG*)&sendBuffer[4] = htonl(0);
    tcp.sendPacket(sendBuffer, 8);

    log->log("Client", Log::DEBUG, "Written config load failed packet");
  }

  return 1;
}

void MVPClient::cleanConfig()
{
  log->log("Client", Log::DEBUG, "Clean config");

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
*/

/*
    fprintf(f, "Event %i eventid = %u time = %lu duration = %li\n", eventNumber, event->GetEventID(), event->GetTime(), event->GetDuration());
    fprintf(f, "Event %i title = %s subtitle = %s\n", eventNumber, event->GetTitle(), event->GetSubtitle());
    fprintf(f, "Event %i extendeddescription = %s\n", eventNumber, event->GetExtendedDescription());
    fprintf(f, "Event %i isFollowing = %i, isPresent = %i\n", eventNumber, event->IsFollowing(), event->IsPresent());

    fprintf(f, "\n\n");
*/

/*


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



*/



/*
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

*/


/*
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

*/



/*


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

*/

/*
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
*/

/*
Active seems to be a bool - whether the timer should be done or not. If set to inactive it stays around after its time
recording is a bool, 0 for not currently recording, 1 for currently recording
pending is a bool, 0 for would not be trying to record this right now, 1 for would/is trying to record this right now
*/


int MVPClient::processGetTimers(UCHAR* buffer, int length)
{
  UCHAR* sendBuffer = new UCHAR[50000]; // FIXME hope this is enough
  int count = 4; // leave space for the packet length

  const char* fileName;
  cTimer *timer;
  int numTimers = Timers.Count();

  *(ULONG*)&sendBuffer[count] = htonl(numTimers);    count += 4;

  for (int i = 0; i < numTimers; i++)
  {
    if (count > 49000) break;

    timer = Timers.Get(i);

#if VDRVERSNUM < 10300
    *(ULONG*)&sendBuffer[count] = htonl(timer->Active());                 count += 4;
#else
    *(ULONG*)&sendBuffer[count] = htonl(timer->HasFlags(tfActive));       count += 4;
#endif
    *(ULONG*)&sendBuffer[count] = htonl(timer->Recording());              count += 4;
    *(ULONG*)&sendBuffer[count] = htonl(timer->Pending());                count += 4;
    *(ULONG*)&sendBuffer[count] = htonl(timer->Priority());               count += 4;
    *(ULONG*)&sendBuffer[count] = htonl(timer->Lifetime());               count += 4;
    *(ULONG*)&sendBuffer[count] = htonl(timer->Channel()->Number());      count += 4;
    *(ULONG*)&sendBuffer[count] = htonl(timer->StartTime());              count += 4;
    *(ULONG*)&sendBuffer[count] = htonl(timer->StopTime());               count += 4;

    fileName = timer->File();
    strcpy((char*)&sendBuffer[count], fileName);
    count += strlen(fileName) + 1;
  }

  *(ULONG*)&sendBuffer[0] = htonl(count - 4); // -4 :  take off the size field

  log->log("Client", Log::DEBUG, "recorded size as %u", ntohl(*(ULONG*)&sendBuffer[0]));

  tcp.sendPacket(sendBuffer, count);
  delete[] sendBuffer;
  log->log("Client", Log::DEBUG, "Written timers list");

  return 1;
}

int MVPClient::processSetTimer(UCHAR* buffer, int length)
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
      sendULONG(0);
      return 1;
    }
    else
    {
      sendULONG(1);
    }
  }
  else
  {
     sendULONG(2);
  }
  delete timer;
  return 1;
}

int MVPClient::processGetRecInfo(UCHAR* data, int length)
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
    sendULONG(0);
    return 1;
  }

  ULONG sendBufferSize = 10000;
  UCHAR* sendBuffer = (UCHAR*)malloc(sendBufferSize);
  ULONG pos = 4; // leave first 4 bytes for size field


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

  *(time_t*)&sendBuffer[pos] = htonl(timerStart);    pos += 4;
  *(time_t*)&sendBuffer[pos] = htonl(timerStop);     pos += 4;

  // Get resume point

  char* value = config.getValueString("ResumeData", (char*)data);
  if (value)
  {
    resumePoint = strtoul(value, NULL, 10);
    delete[] value;
  }
  log->log("Client", Log::DEBUG, "GRI: RP: %lu", resumePoint);

  *(ULONG*)&sendBuffer[pos] = htonl(resumePoint);    pos += 4;


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
    // memory insanity...
    if ((sendBufferSize - pos) < (strlen(summary) + 500)) // random
    {
      UCHAR* newBuffer = (UCHAR*)realloc(sendBuffer, sendBufferSize + strlen(summary) + 10000);
      if (newBuffer)
      {
        sendBuffer = newBuffer;
        sendBufferSize += strlen(summary) + 10000;
      }
      else
      {
        free(sendBuffer);
        sendULONG(0);
        return 1;
      }
    }

    strcpy((char*)&sendBuffer[pos], summary);
    pos += strlen(summary) + 1;
  }
  else
  {
    strcpy((char*)&sendBuffer[pos], "");
    pos += 1;
  }


  // Get channels

#if VDRVERSNUM < 10300

  // Send 0 for numchannels - this signals the client this info is not available
  *(ULONG*)&sendBuffer[pos] = 0;    pos += 4;

#else
  const cComponents* components = Info->Components();

  log->log("Client", Log::DEBUG, "GRI: D1: %p", components);

  if (!components)
  {
    *(ULONG*)&sendBuffer[pos] = htonl(0);    pos += 4;
  }
  else
  {
    *(ULONG*)&sendBuffer[pos] = htonl(components->NumComponents());    pos += 4;

    tComponent* component;
    for (int i = 0; i < components->NumComponents(); i++)
    {
      component = components->Component(i);

      // memory insanity...
      ULONG extraNeeded = 2 + (component->language ? strlen(component->language) : 0)
                            + (component->description ? strlen(component->description) : 0) + 2;

      if ((sendBufferSize - pos) < extraNeeded)
      {
        UCHAR* newBuffer = (UCHAR*)realloc(sendBuffer, sendBufferSize + extraNeeded + 10000);
        if (newBuffer)
        {
          sendBuffer = newBuffer;
          sendBufferSize += extraNeeded + 10000;
        }
        else
        {
          free(sendBuffer);
          sendULONG(0);
          return 1;
        }
      }

      log->log("Client", Log::DEBUG, "GRI: C: %i %u %u %s %s", i, component->stream, component->type, component->language, component->description);
      sendBuffer[pos] = component->stream;  pos += 1;
      sendBuffer[pos] = component->type;    pos += 1;
      if (component->language)
      {
        strcpy((char*)&sendBuffer[pos], component->language);
        pos += strlen(component->language) + 1;
      }
      else
      {
        strcpy((char*)&sendBuffer[pos], "");
        pos += 1;
      }
      if (component->description)
      {
        strcpy((char*)&sendBuffer[pos], component->description);
        pos += strlen(component->description) + 1;
      }
      else
      {
        strcpy((char*)&sendBuffer[pos], "");
        pos += 1;
      }

    }
  }

#endif

  // Done. send it

  *(ULONG*)&sendBuffer[0] = htonl(pos - 4); // -4 :  take off the size field

  log->log("Client", Log::DEBUG, "recorded size as %u", ntohl(*(ULONG*)&sendBuffer[0]));

  tcp.sendPacket(sendBuffer, pos);
  delete[] sendBuffer;
  log->log("Client", Log::DEBUG, "Written getrecinfo");

  return 1;
}




// FIXME obselete

int MVPClient::processReScanRecording(UCHAR* data, int length)
{
  if (!rp)
  {
    log->log("Client", Log::DEBUG, "Rescan recording called when no recording being played!");
    return 0;
  }

  rp->scan();

  UCHAR sendBuffer[16];
  *(ULONG*)&sendBuffer[0] = htonl(12);
  *(ULLONG*)&sendBuffer[4] = htonll(rp->getLengthBytes());
  *(ULONG*)&sendBuffer[12] = htonl(rp->getLengthFrames());

  tcp.sendPacket(sendBuffer, 16);
  log->log("Client", Log::DEBUG, "Rescan recording, wrote new length to client");
  return 1;
}

// FIXME without client calling rescan, getblock wont work even tho more data is avail


int MVPClient::processGetMarks(UCHAR* data, int length)
{
  // data is a pointer to the fileName string

  UCHAR* sendBuffer = new UCHAR[50000]; // FIXME hope this is enough
  int count = 4; // leave space for the packet length


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

        if (count > 49000) break;
        *(ULONG*)&sendBuffer[count] = htonl(m->position);
        count += 4;
      }
    }
    else
    {
      log->log("Client", Log::DEBUG, "no marks found, sending 0-mark");
      *(ULONG*)&sendBuffer[count] = htonl(0);
      count += 4;
    }
  }

  *(ULONG*)&sendBuffer[0] = htonl(count - 4); // -4 :  take off the size field

  log->log("Client", Log::DEBUG, "recorded size as %u", ntohl(*(ULONG*)&sendBuffer[0]));

  tcp.sendPacket(sendBuffer, count);
  delete[] sendBuffer;
  log->log("Client", Log::DEBUG, "Written Marks list");

  return 1;
}
