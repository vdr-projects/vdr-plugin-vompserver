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
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "mvpserver.h"
#ifdef VOMPSTANDALONE
#include <iostream>
#endif

extern pthread_mutex_t threadClientMutex;

MVPServer::MVPServer()
{
  // MH in case anbody has a better position :-)
  pthread_mutex_init(&threadClientMutex, NULL);
  tcpServerPort = 0;
  logoDir = NULL;
  resourceDir = NULL;
  imageDir = NULL;
  cacheDir = NULL;
}

MVPServer::~MVPServer()
{
  if (logoDir) delete[] logoDir;
  if (resourceDir) delete[] resourceDir;
  if (imageDir) delete[] imageDir;
  if (cacheDir) delete[] cacheDir;
  stop();
}

int MVPServer::stop()
{
  if (threadIsActive()) threadCancel();
  close(listeningSocket);

  udpr.shutdown();
  bootpd.shutdown();
  tftpd.shutdown();
  mvprelay.shutdown();

  log.log("Main", Log::INFO, "Stopped main server thread");
  log.shutdown();

  config.shutdown();

  return 1;
}

int MVPServer::run(char* tconfigDir)
{
  if (threadIsActive()) return 1;

  configDir = tconfigDir;

  // Start config
#ifdef VOMPSTANDALONE
#define dsyslog(x) std::cout << x << std::endl;
#endif

  char configFileName[PATH_MAX];
  snprintf(configFileName, PATH_MAX, "%s/vomp.conf", configDir);

  if (config.init(configFileName))
  {
    dsyslog("VOMP: Config file found");
  }
  else
  {
    dsyslog("VOMP: Config file not found");
  }

  // Start logging

  char* cfgLogFilename = config.getValueString("General", "Log file");
  if (cfgLogFilename)
  {
    log.init(Log::DEBUG, cfgLogFilename);
    delete[] cfgLogFilename;
    log.log("Main", Log::INFO, "Logging started");
  }
  else
  {
    dsyslog("VOMP: Logging disabled");
  }

  const char *bigresdir = cPlugin::ResourceDirectory();  
  const char *bigcachedir = cPlugin::CacheDirectory();  
  // get logo directory
  logoDir =  config.getValueString("General", "Channel logo directory");
  
  if (logoDir) 
  {
    log.log("Main", Log::INFO, "LogoDir set %s", logoDir);
  } else {
    if (bigresdir) {
	logoDir = new char[strlen(bigresdir)+1+7];
	sprintf(logoDir,"%s/logos/",bigresdir);    
	log.log("Main", Log::INFO, "No LogoDir set, default %s",logoDir);
     } else {
	log.log("Main", Log::INFO, "No LogoDir set, no res dir");
     }
        
  }

  // get epg Image directory
  imageDir =  config.getValueString("General", "Epg image directory");
  
  if (imageDir) 
  {
    log.log("Main", Log::INFO, "ImageDir set %s", imageDir);
  } else {
    if (bigcachedir) {
	imageDir = new char[strlen(bigcachedir)+1+11+3];
	sprintf(imageDir,"%s/../epgimages/",bigcachedir);    
	log.log("Main", Log::INFO, "No ImageDir set, default %s",imageDir);
    } else {
      	log.log("Main", Log::INFO, "No ImageDir set, no cache dir");
    }
  }

  if (bigresdir) {
    resourceDir = new char[strlen(bigresdir)+1];
    strcpy(resourceDir,bigresdir);
    log.log("Main", Log::INFO, "Resource directory is  %s",bigresdir);
  } else {
    log.log("Main", Log::INFO, "Resource directory is  not set");
  }
  
  if (bigcachedir) {
    cacheDir = new char[strlen(bigcachedir)+1];
    strcpy(cacheDir,bigcachedir);
    log.log("Main", Log::INFO, "Cache directory is  %s",bigcachedir);
  } else {
    log.log("Main", Log::INFO, "Cache directory is  not set");
  }
  // Get UDP port number for discovery service

  int fail = 1;
  int udpport = config.getValueLong("General", "UDP port", &fail);
  if (fail) udpport = 51051;  
  
  // Work out a name for this server

  char* serverName;

  // Try to get from vomp.conf
  serverName = config.getValueString("General", "Server name");
  if (!serverName) // If not, get the hostname
  {
    serverName = new char[1024];
    if (gethostname(serverName, 1024)) // if not, set default
    {
      strcpy(serverName, "VOMP Server");
    }
  }

  // Get VOMP server TCP port to give to UDP replier to put in packets
  fail = 1;
  tcpServerPort = config.getValueLong("General", "TCP port", &fail);
  if (fail) tcpServerPort = 3024;
  
  int udpSuccess = udpr.run(udpport, serverName, tcpServerPort);

  delete[] serverName;

  if (!udpSuccess)
  {
    log.log("Main", Log::CRIT, "Could not start UDP replier");
    stop();
    return 0;
  }

  // Read config and start bootp and tftp as appropriate

  char* configString;
  int bootpEnabled = 0;
  int tftpEnabled = 0;
  int mvprelayEnabled = 1;

  configString = config.getValueString("General", "Bootp server enabled");
  if (configString && (!strcasecmp(configString, "yes"))) bootpEnabled = 1;
  if (configString) delete[] configString;

  configString = config.getValueString("General", "TFTP server enabled");
  if (configString && (!strcasecmp(configString, "yes"))) tftpEnabled = 1;
  if (configString) delete[] configString;

  configString = config.getValueString("General", "MVPRelay enabled");
  if (configString && (strcasecmp(configString, "yes"))) mvprelayEnabled = 0;
  if (configString) delete[] configString;


  if (bootpEnabled)
  {
    if (!bootpd.run(configDir))
    {
      log.log("Main", Log::CRIT, "Could not start Bootpd");
      stop();
      return 0;
    }
  }
  else
  {
    log.log("Main", Log::INFO, "Not starting Bootpd");
  }

  if (tftpEnabled)
  {
    char tftpPath[PATH_MAX];

    configString = config.getValueString("General", "TFTP directory");
    if (configString)
    {
      snprintf(tftpPath, PATH_MAX, "%s", configString);

      // this will never happen.. surely.
      if ((strlen(tftpPath) + 2) >= PATH_MAX)
      {
        delete[] configString;
        log.log("Main", Log::CRIT, "Could not understand TFTP directory from config");
        stop();
        return 0;
      }

      // if there isn't a / at the end of the dir, add one
      if (tftpPath[strlen(tftpPath) - 1] != '/') strcat(tftpPath, "/");

      delete[] configString;
    }
    else
    {
      snprintf(tftpPath, PATH_MAX, "%s/", configDir);
    }

    log.log("Main", Log::INFO, "TFTP path '%s'", tftpPath);

    if (!tftpd.run(tftpPath))
    {
      log.log("Main", Log::CRIT, "Could not start TFTPd");
      stop();
      return 0;
    }
  }
  else
  {
    log.log("Main", Log::INFO, "Not starting TFTPd");
  }

  // Start mvprelay thread
  if (mvprelayEnabled)
  {
    if (!mvprelay.run())
    {
      log.log("Main", Log::CRIT, "Could not start MVPRelay");
      stop();
      return 0;
    }
    else
    {
      log.log("Main", Log::INFO, "MVPRelay started");
    }
  }
  else
  {
    log.log("Main", Log::INFO, "Not starting MVPRelay");
  }
  
  // start thread here
  if (!threadStart())
  {
    log.log("Main", Log::CRIT, "Could not start MVPServer thread");
    stop();
    return 0;
  }

  log.log("Main", Log::DEBUG, "MVPServer run success");
  return 1;
}

void MVPServer::threadMethod()
{
  // I want to die as soon as I am cancelled because I'll be in accept()
  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

  struct sockaddr_in address;
  address.sin_family = AF_INET;
  address.sin_port = htons(tcpServerPort);
  address.sin_addr.s_addr = INADDR_ANY;
  socklen_t length = sizeof(address);

  listeningSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (listeningSocket < 0)
  {
    log.log("MVPServer", Log::CRIT, "Could not get TCP socket in vompserver");
    return;
  }

  int value=1;
  setsockopt(listeningSocket,SOL_SOCKET,SO_REUSEADDR,&value,sizeof(value));

  if (bind(listeningSocket,(struct sockaddr *)&address,sizeof(address)) < 0)
  {
    log.log("MVPServer", Log::CRIT, "Could not bind to socket in vompserver");
    close(listeningSocket);
    return;
  }

  listen(listeningSocket, 5);

  int clientSocket;

  while(1)
  {
    clientSocket = accept(listeningSocket,(struct sockaddr *)&address, &length);
    VompClient* m = new VompClient(&config, configDir, logoDir, resourceDir, imageDir, cacheDir, clientSocket);
    m->run();
  }
}

