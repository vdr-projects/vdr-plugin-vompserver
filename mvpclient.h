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

#ifndef MVPCLIENT_H
#define MVPCLIENT_H

#include <stdio.h>
#include <pthread.h>
#include <netinet/in.h>
#include <signal.h>

#include <unistd.h> // sleep

#include <vdr/recording.h>
#include <vdr/channels.h>
#include <vdr/videodir.h>
#include <vdr/plugin.h>

#include "tcp.h"
#include "transceiver.h"
#include "recplayer.h"
#include "config.h"

class MVPClient
{
  public:
    MVPClient(int tsocket);
    ~MVPClient();

    int run();
    // not for external use
    void run2();

  private:
    pthread_t runThread;
    int initted;
    TCP tcp;
    Config config;
    cMediamvpTransceiver* cm;
    cRecordings* recordingManager;
    RecPlayer* rp;

    void processLogin(unsigned char* buffer, int length);
    void processGetRecordingsList(unsigned char* data, int length);
    void processDeleteRecording(unsigned char* data, int length);
    void processGetSummary(unsigned char* data, int length);
    void processGetChannelsList(unsigned char* data, int length);
    void processStartStreamingChannel(unsigned char* data, int length);
    void processGetBlock(unsigned char* data, int length);
    void processStopStreaming(unsigned char* data, int length);
    void processStartStreamingRecording(unsigned char* data, int length);
    void processGetChannelSchedule(unsigned char* data, int length);
    void processConfigSave(unsigned char* data, int length);
    void processConfigLoad(unsigned char* data, int length);

    cChannel* channelFromNumber(unsigned long channelNumber);
    void writeResumeData();
    void cleanConfig();

    void sendULONG(ULONG ul);

    void testChannelSchedule(unsigned char* data, int length);
};

#endif
