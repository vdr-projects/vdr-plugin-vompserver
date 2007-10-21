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
#include <signal.h>
#include <endian.h>

#include <unistd.h> // sleep

#include <vdr/recording.h>
#include <vdr/channels.h>
#include <vdr/videodir.h>
#include <vdr/plugin.h>
#include <vdr/timers.h>
#include <vdr/menu.h>

#include "defines.h"
#include "tcp.h"
#include "mvpreceiver.h"
#include "recplayer.h"
#include "config.h"
#include "media.h"

class MVPClient
{
  public:
    MVPClient(Config* baseConfig, char* configDirExtra, int tsocket);
    ~MVPClient();

    int run();
    // not for external use
    void run2();
    static int getNrClients();

  private:
    static int nr_clients;
    pthread_t runThread;
    int initted;
    TCP tcp;
    Config config;
    Config* baseConfig;
    MVPReceiver* lp;
    bool loggedIn;
    char* configDirExtra;
    FILE* imageFile;


    cRecordings* recordingManager;
    RecPlayer* rp;
    Log* log;

    int processLogin(UCHAR* buffer, int length);
    int processGetRecordingsList(UCHAR* data, int length);
    int processDeleteRecording(UCHAR* data, int length);
    int processMoveRecording(UCHAR* data, int length);
    int processGetChannelsList(UCHAR* data, int length);
    int processStartStreamingChannel(UCHAR* data, int length);
    int processGetBlock(UCHAR* data, int length);
    int processStopStreaming(UCHAR* data, int length);
    int processStartStreamingRecording(UCHAR* data, int length);
    int processGetChannelSchedule(UCHAR* data, int length);
    int processConfigSave(UCHAR* data, int length);
    int processConfigLoad(UCHAR* data, int length);
    int processGetTimers(UCHAR* data, int length);
    int processSetTimer(UCHAR* data, int length);
    int processPositionFromFrameNumber(UCHAR* data, int length);
    int processFrameNumberFromPosition(UCHAR* data, int length);
    int processGetIFrame(UCHAR* data, int length);
    int processGetRecInfo(UCHAR* data, int length);
    int processGetMarks(UCHAR* data, int length);
    int processGetChannelPids(UCHAR* data, int length);
    int processGetMediaList(UCHAR* data, int length);
    int processGetPicture(UCHAR* data, int length);
    int processGetImageBlock(UCHAR* data, int length);
    int processDeleteTimer(UCHAR* buffer, int length);

    int processReScanRecording(UCHAR* data, int length);           // FIXME obselete

    void incClients();
    void decClients();

    cChannel* channelFromNumber(ULONG channelNumber);
    void writeResumeData();
    void cleanConfig();
    void sendULONG(ULONG ul);

    ULLONG ntohll(ULLONG a);
    ULLONG htonll(ULLONG a);

    void test(int num);
    void test2();
};

#endif
