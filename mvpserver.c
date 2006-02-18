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

#include "mvpserver.h"

MVPServer::MVPServer()
{
}

MVPServer::~MVPServer()
{
  stop();
}

int MVPServer::stop()
{
  if (threadIsActive()) threadCancel();
  close(listeningSocket);

  udpr.shutdown();
  bootpd.shutdown();
  tftpd.shutdown();

  log.log("Main", Log::INFO, "Stopped main server thread");
  log.shutdown();

  config.shutdown();

  return 1;
}

int MVPServer::run()
{
  if (threadIsActive()) return 1;

  // Start config
  const char* configDir = cPlugin::ConfigDirectory();
  if (!configDir)
  {
    dsyslog("VOMP: Could not get config dir from VDR");
  }
  else
  {
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

  // Work out a name for this server

  char* serverName;

  // Try to get from vomp.conf
  serverName = config.getValueString("General", "Server name");
  if (!serverName) // If not, get the hostname
  {
    serverName = new char[1024];
    if (gethostname(serverName, 1024)) // if not, just use "-"
    {
      strcpy(serverName, "-");
    }
  }

  int udpSuccess = udpr.run(serverName);

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

  configString = config.getValueString("General", "Bootp server enabled");
  if (configString && (!strcasecmp(configString, "yes"))) bootpEnabled = 1;
  if (configString) delete[] configString;

  configString = config.getValueString("General", "TFTP server enabled");
  if (configString && (!strcasecmp(configString, "yes"))) tftpEnabled = 1;
  if (configString) delete[] configString;


  if (bootpEnabled)
  {
    if (!bootpd.run())
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
//    snprintf(configFileName, PATH_MAX, "%s/vomp.conf", configDir);

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
  address.sin_port = htons(3024);
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
    MVPClient* m = new MVPClient(clientSocket);
    m->run();
  }
}
