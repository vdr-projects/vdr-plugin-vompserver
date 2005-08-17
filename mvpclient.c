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

MVPClient::MVPClient(int tsocket)
 : tcp(tsocket)
{
  lp = NULL;
  rp = NULL;
  recordingManager = NULL;
  log = Log::getInstance();

  // Get IP address of client for config module

  char ipa[20];
  struct sockaddr_in peer;
  socklen_t salen = sizeof(struct sockaddr);
  if(getpeername(tsocket, (struct sockaddr*)&peer, &salen) == 0)
  {
    strcpy(ipa, inet_ntoa(peer.sin_addr));
  }
  else
  {
    ipa[0] = '\0';
    log->log("Client", Log::DEBUG, "Cannot get peer name!");
  }

  const char* configDir = cPlugin::ConfigDirectory();
  if (!configDir)
  {
    log->log("Client", Log::DEBUG, "No config dir!");
    return;
  }

  char configFileName[PATH_MAX];
  snprintf(configFileName, PATH_MAX - strlen(configDir) - strlen(ipa) - 20, "%s/vomp-%s.conf", configDir, ipa);
  config.init(configFileName);

  log->log("Client", Log::DEBUG, "Config file name: %s", configFileName);

//  processGetChannelSchedule(NULL, 0);

//test();

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

  cleanConfig();
}

cChannel* MVPClient::channelFromNumber(unsigned long channelNumber)
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
  config.setValueLongLong("ResumeData", (char*)rp->getCurrentRecording()->FileName(), rp->getLastPosition());
}

void MVPClient::sendULONG(ULONG ul)
{
  unsigned char sendBuffer[8];
  *(unsigned long*)&sendBuffer[0] = htonl(4);
  *(unsigned long*)&sendBuffer[4] = htonl(ul);

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

  unsigned char* buffer;
  unsigned char* data;
  int packetLength;
  unsigned long opcode;

  while(1)
  {
    log->log("Client", Log::DEBUG, "Waiting");
    buffer = (unsigned char*)tcp.receivePacket();
    log->log("Client", Log::DEBUG, "Received packet, length = %u", tcp.getDataLength());
    if (buffer == NULL)
    {
      log->log("Client", Log::DEBUG, "Detected connection closed");
      break;
    }

    packetLength = tcp.getDataLength() - 4;
    opcode = ntohl(*(unsigned long*)buffer);
    data = buffer + 4;


    switch(opcode)
    {
      case 1:
        processLogin(data, packetLength);
        break;
      case 2:
        processGetRecordingsList(data, packetLength);
        break;
      case 3:
        processDeleteRecording(data, packetLength);
        break;
      case 4:
        processGetSummary(data, packetLength);
        break;
      case 5:
        processGetChannelsList(data, packetLength);
        break;
      case 6:
        processStartStreamingChannel(data, packetLength);
        break;
      case 7:
        processGetBlock(data, packetLength);
        break;
      case 8:
        processStopStreaming(data, packetLength);
        break;
      case 9:
        processStartStreamingRecording(data, packetLength);
        break;
      case 10:
        processGetChannelSchedule(data, packetLength);
        break;
      case 11:
        processConfigSave(data, packetLength);
        break;
      case 12:
        processConfigLoad(data, packetLength);
        break;
      case 13:
        processReScanRecording(data, packetLength);
        break;
    }

    free(buffer);
  }
}

void MVPClient::processLogin(unsigned char* buffer, int length)
{
  time_t timeNow = time(NULL);
  struct tm* timeStruct = localtime(&timeNow);
  int timeOffset = timeStruct->tm_gmtoff;

  unsigned char sendBuffer[12];
  *(unsigned long*)&sendBuffer[0] = htonl(8);
  *(unsigned long*)&sendBuffer[4] = htonl(timeNow);
  *(signed int*)&sendBuffer[8] = htonl(timeOffset);

  tcp.sendPacket(sendBuffer, 12);
  log->log("Client", Log::DEBUG, "written login reply");
}

void MVPClient::processGetRecordingsList(unsigned char* data, int length)
{
  unsigned char* sendBuffer = new unsigned char[50000]; // hope this is enough
  int count = 4; // leave space for the packet length
  char* point;


  int FreeMB;
  int Percent = VideoDiskSpace(&FreeMB);
  int Total = (FreeMB / (100 - Percent)) * 100;

  *(unsigned long*)&sendBuffer[count] = htonl(Total);
  count += sizeof(unsigned long);
  *(unsigned long*)&sendBuffer[count] = htonl(FreeMB);
  count += sizeof(unsigned long);
  *(unsigned long*)&sendBuffer[count] = htonl(Percent);
  count += sizeof(unsigned long);


  cRecordings Recordings;
  Recordings.Load();

  for (cRecording *recording = Recordings.First(); recording; recording = Recordings.Next(recording))
  {
    if (count > 49000) break; // just how big is that hard disk?!
    *(unsigned long*)&sendBuffer[count] = htonl(recording->start);// + timeOffset);
    count += 4;

    point = (char*)recording->Name();
    strcpy((char*)&sendBuffer[count], point);
    count += strlen(point) + 1;

    point = (char*)recording->FileName();
    strcpy((char*)&sendBuffer[count], point);
    count += strlen(point) + 1;
  }

  *(unsigned long*)&sendBuffer[0] = htonl(count - 4); // -4 :  take off the size field

  log->log("Client", Log::DEBUG, "recorded size as %u", ntohl(*(unsigned long*)&sendBuffer[0]));

  tcp.sendPacket(sendBuffer, count);
  delete[] sendBuffer;
  log->log("Client", Log::DEBUG, "Written list");
}

void MVPClient::processDeleteRecording(unsigned char* data, int length)
{
  // data is a pointer to the fileName string

  cRecordings Recordings;
  Recordings.Load(); // probably have to do this

  cRecording* recording = Recordings.GetByName((char*)data);

  log->log("Client", Log::DEBUG, "recording pointer %p", recording);

  if (recording)
  {
    log->log("Client", Log::DEBUG, "deleting recording: %s", recording->Name());
    recording->Delete();
    sendULONG(1);
  }
  else
  {
    sendULONG(0);
  }
}

void MVPClient::processGetSummary(unsigned char* data, int length)
{
  // data is a pointer to the fileName string

  cRecordings Recordings;
  Recordings.Load(); // probably have to do this

  cRecording *recording = Recordings.GetByName((char*)data);

  log->log("Client", Log::DEBUG, "recording pointer %p", recording);

  if (recording)
  {
    unsigned char* sendBuffer = new unsigned char[50000]; // hope this is enough
    int count = 4; // leave space for the packet length

    char* point;
#if VDRVERSNUM < 10300
    point = (char*)recording->Summary();
#else
    const cRecordingInfo *Info = recording->Info();
    point = (char*)Info->ShortText();
    log->log("Client", Log::DEBUG, "info pointer %p summary pointer %p", Info, point);
    if (isempty(point))
    {
      point = (char*)Info->Description();
      log->log("Client", Log::DEBUG, "description pointer %p", point);
    }
#endif
    strcpy((char*)&sendBuffer[count], point);
    count += strlen(point) + 1;
    *(unsigned long*)&sendBuffer[0] = htonl(count - 4); // -4 :  take off the size field

    log->log("Client", Log::DEBUG, "recorded size as %u", ntohl(*(unsigned long*)&sendBuffer[0]));

    tcp.sendPacket(sendBuffer, count);
    delete[] sendBuffer;
    log->log("Client", Log::DEBUG, "Written summary");


  }
  else
  {
    sendULONG(0);
  }
}

void MVPClient::processGetChannelsList(unsigned char* data, int length)
{
  unsigned char* sendBuffer = new unsigned char[50000]; // FIXME hope this is enough
  int count = 4; // leave space for the packet length
  char* point;
  unsigned long type;

  for (cChannel *channel = Channels.First(); channel; channel = Channels.Next(channel))
  {

// Re-enable later with some client side option
//#if VDRVERSNUM < 10300
//    if (!channel->GroupSep() && !channel->Ca())
//#else
//    if (!channel->GroupSep() && !channel->Ca(0))
//#endif
//    {

    if (!channel->GroupSep())
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
      *(unsigned long*)&sendBuffer[count] = htonl(channel->Number());
      count += 4;

      *(unsigned long*)&sendBuffer[count] = htonl(type);
      count += 4;

      point = (char*)channel->Name();
      strcpy((char*)&sendBuffer[count], point);
      count += strlen(point) + 1;
    }
  }

  *(unsigned long*)&sendBuffer[0] = htonl(count - 4); // -4 :  take off the size field

  log->log("Client", Log::DEBUG, "recorded size as %u", ntohl(*(unsigned long*)&sendBuffer[0]));

  tcp.sendPacket(sendBuffer, count);
  delete[] sendBuffer;
  log->log("Client", Log::DEBUG, "Written channels list");
}

void MVPClient::processStartStreamingChannel(unsigned char* data, int length)
{
  log->log("Client", Log::DEBUG, "length = %i", length);
  unsigned long channelNumber = ntohl(*(unsigned long*)data);

  cChannel* channel = channelFromNumber(channelNumber);
  if (!channel)
  {
    sendULONG(0);
    return;
  }

  lp = MVPReceiver::create(channel);

  if (!lp)
  {
    sendULONG(0);
    return;
  }

  if (!lp->init())
  {
    delete lp;
    lp = NULL;
    sendULONG(0);
    return;
  }

  sendULONG(1);
}

void MVPClient::processStopStreaming(unsigned char* data, int length)
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
}

void MVPClient::processGetBlock(unsigned char* data, int length)
{
  if (!lp && !rp)
  {
    log->log("Client", Log::DEBUG, "Get block called when no streaming happening!");
    return;
  }

  ULLONG position = ntohll(*(ULLONG*)data);
  data += sizeof(ULLONG);
  unsigned long amount = ntohl(*(unsigned long*)data);

  log->log("Client", Log::DEBUG, "getblock pos = %llu length = %lu", position, amount);

  unsigned char sendBuffer[amount + 4];
  unsigned long amountReceived = 0; // compiler moan.
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

  *(unsigned long*)&sendBuffer[0] = htonl(amountReceived);
  tcp.sendPacket(sendBuffer, amountReceived + 4);
  log->log("Client", Log::DEBUG, "written ok %lu", amountReceived);
}

void MVPClient::processStartStreamingRecording(unsigned char* data, int length)
{
  // data is a pointer to the fileName string

  recordingManager = new cRecordings;
  recordingManager->Load();

  cRecording* recording = recordingManager->GetByName((char*)data);

  log->log("Client", Log::DEBUG, "recording pointer %p", recording);

  if (recording)
  {
    rp = new RecPlayer(recording);

    unsigned char sendBuffer[12];
    *(unsigned long*)&sendBuffer[0] = htonl(8);
    *(ULLONG*)&sendBuffer[4] = htonll(rp->getTotalLength());

    tcp.sendPacket(sendBuffer, 12);
    log->log("Client", Log::DEBUG, "written totalLength");
  }
  else
  {
    delete recordingManager;
    recordingManager = NULL;
  }
}

void MVPClient::processReScanRecording(unsigned char* data, int length)
{
  ULLONG retval = 0;

  if (!rp)
  {
    log->log("Client", Log::DEBUG, "Rescan recording called when no recording being played!");
  }
  else
  {
    rp->scan();
    retval = rp->getTotalLength();
  }

  unsigned char sendBuffer[12];
  *(unsigned long*)&sendBuffer[0] = htonl(8);
  *(ULLONG*)&sendBuffer[4] = htonll(retval);

  tcp.sendPacket(sendBuffer, 12);
  log->log("Client", Log::DEBUG, "Rescan recording, wrote new length to client");
}

void MVPClient::processGetChannelSchedule(unsigned char* data, int length)
{
  ULONG channelNumber = ntohl(*(ULLONG*)data);
  log->log("Client", Log::DEBUG, "get schedule called for channel %lu", channelNumber);

  cChannel* channel = channelFromNumber(channelNumber);
  if (!channel)
  {
    unsigned char sendBuffer[4];
    *(unsigned long*)&sendBuffer[0] = htonl(0);
    tcp.sendPacket(sendBuffer, 4);
    log->log("Client", Log::DEBUG, "written null");
    return;
  }

#if VDRVERSNUM < 10300
  cMutexLock MutexLock;
  const cSchedules *Schedules = cSIProcessor::Schedules(MutexLock);
#else
  cSchedulesLock MutexLock;
  const cSchedules *Schedules = cSchedules::Schedules(MutexLock);
#endif
  if (!Schedules)
  {
    unsigned char sendBuffer[8];
    *(unsigned long*)&sendBuffer[0] = htonl(4);
    *(unsigned long*)&sendBuffer[4] = htonl(0);
    tcp.sendPacket(sendBuffer, 8);
    log->log("Client", Log::DEBUG, "written 0");
    return;
  }

  unsigned char sendBuffer[8];
  *(unsigned long*)&sendBuffer[0] = htonl(4);
  *(unsigned long*)&sendBuffer[4] = htonl(1);
  tcp.sendPacket(sendBuffer, 8);
  log->log("Client", Log::DEBUG, "written 1");


}

void MVPClient::testChannelSchedule(unsigned char* data, int length)
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





    fprintf(f, "End of current Schedule\n\n\n");

    Schedule = (const cSchedule *)Schedules->Next(Schedule);
    scheduleNumber++;
  }

  fclose(f);
}

void MVPClient::processConfigSave(unsigned char* buffer, int length)
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
  if (buffer[length - 1] != '\0') return;

  log->log("Client", Log::DEBUG, "Config save: %s %s %s", section, key, value);
  if (config.setValueString(section, key, value))
  {
    sendULONG(1);
  }
  else
  {
    sendULONG(0);
  }
}

void MVPClient::processConfigLoad(unsigned char* buffer, int length)
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
    unsigned char sendBuffer[4 + strlen(value) + 1];
    *(unsigned long*)&sendBuffer[0] = htonl(strlen(value) + 1);
    strcpy((char*)&sendBuffer[4], value);
    tcp.sendPacket(sendBuffer, 4 + strlen(value) + 1);

    log->log("Client", Log::DEBUG, "Written config load packet");
    delete[] value;
  }
  else
  {
    unsigned char sendBuffer[8];
    *(unsigned long*)&sendBuffer[0] = htonl(4);
    *(unsigned long*)&sendBuffer[4] = htonl(0);
    tcp.sendPacket(sendBuffer, 8);

    log->log("Client", Log::DEBUG, "Written config load failed packet");
  }
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


