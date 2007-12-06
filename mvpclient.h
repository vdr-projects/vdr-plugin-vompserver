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

#include "defines.h"
#include "tcp.h"
#include "config.h"
#include "media.h"
#include "i18n.h"

class ResponsePacket;

class MVPClient
{
  public:
    MVPClient(Config* baseConfig, char* configDir, int tsocket);
    ~MVPClient();

    int run();
    // not for external use
    void run2();
    static int getNrClients();

  private:
    static int nr_clients;
    pthread_t runThread;
    int initted;
    Log* log;
    TCP tcp;
    Config config;
    Config* baseConfig;
    I18n i18n;
    bool loggedIn;
    char* configDir;
    FILE* imageFile;

#ifndef VOMPSTANDALONE
    MVPReceiver* lp;
    cRecordings* recordingManager;
    RecPlayer* recplayer;
#endif

    int processLogin(UCHAR* buffer, int length, ResponsePacket* rp);
#ifndef VOMPSTANDALONE
    int processGetRecordingsList(UCHAR* data, int length, ResponsePacket* rp);
    int processDeleteRecording(UCHAR* data, int length, ResponsePacket* rp);
    int processMoveRecording(UCHAR* data, int length, ResponsePacket* rp);
    int processGetChannelsList(UCHAR* data, int length, ResponsePacket* rp);
    int processStartStreamingChannel(UCHAR* data, int length, ULONG streamID, ResponsePacket* rp);
    int processGetBlock(UCHAR* data, int length, ResponsePacket* rp);
    int processStopStreaming(UCHAR* data, int length, ResponsePacket* rp);
    int processStartStreamingRecording(UCHAR* data, int length, ResponsePacket* rp);
    int processGetChannelSchedule(UCHAR* data, int length, ResponsePacket* rp);
    int processGetTimers(UCHAR* data, int length, ResponsePacket* rp);
    int processSetTimer(UCHAR* data, int length, ResponsePacket* rp);
    int processPositionFromFrameNumber(UCHAR* data, int length, ResponsePacket* rp);
    int processFrameNumberFromPosition(UCHAR* data, int length, ResponsePacket* rp);
    int processGetIFrame(UCHAR* data, int length, ResponsePacket* rp);
    int processGetRecInfo(UCHAR* data, int length, ResponsePacket* rp);
    int processGetMarks(UCHAR* data, int length, ResponsePacket* rp);
    int processGetChannelPids(UCHAR* data, int length, ResponsePacket* rp);
    int processDeleteTimer(UCHAR* buffer, int length, ResponsePacket* rp);

    int processReScanRecording(UCHAR* data, int length, ResponsePacket* rp);           // FIXME obselete
#endif
    int processConfigSave(UCHAR* data, int length, ResponsePacket* rp);
    int processConfigLoad(UCHAR* data, int length, ResponsePacket* rp);
    int processGetMediaList(UCHAR* data, int length, ResponsePacket* rp);
    int processGetPicture(UCHAR* data, int length, ResponsePacket* rp);
    int processGetImageBlock(UCHAR* data, int length, ResponsePacket* rp);
    int processGetLanguageList(UCHAR* data, int length, ResponsePacket* rp);
    int processGetLanguageContent(UCHAR* data, int length, ResponsePacket* rp);

    void incClients();
    void decClients();

#ifndef VOMPSTANDALONE
    cChannel* channelFromNumber(ULONG channelNumber);
    void writeResumeData();
#endif
    void cleanConfig();
    void sendULONG(ULONG ul);

    ULLONG ntohll(ULLONG a);
    ULLONG htonll(ULLONG a);

    void test(int num);
    void test2();
};

#endif
