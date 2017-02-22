/*
 *    Copyright 2004-2013 Chris Tallon
 * 
 *    This file is part of VOMP.
 * 
 *    VOMP is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 * 
 *    VOMP is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 * 
 *    You should have received a copy of the GNU General Public License
 *    along with VOMP; if not, write to the Free Software
 *    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef VOMPSTANDALONE
#include <vdr/plugin.h>
#endif
#include <iostream>
#include <getopt.h>

#include "mvpserver.h"
//#include "vompclient.h"

static const char *VERSION        = "0.5.0";
static const char *DESCRIPTION    = "Vompserver plugin by Chris Tallon";
static const char *MAINMENUENTRY  = "Vompserver";

#ifndef VOMPSTANDALONE

class cPluginVompserver : public cPlugin {
private:
  MVPServer mvpserver;
  char* configDir;
public:
  cPluginVompserver(void);
  virtual ~cPluginVompserver();
  virtual const char *Version(void) { return VERSION; }
  virtual const char *Description(void) { return DESCRIPTION; }
  virtual const char *CommandLineHelp(void);
  virtual bool ProcessArgs(int argc, char *argv[]);
  virtual bool Initialize(void);
  virtual bool Start(void);
  virtual void Stop(void);
  virtual void Housekeeping(void);
  virtual void MainThreadHook(void);
  virtual cString Active(void);
  virtual time_t WakeupTime(void);
  virtual const char *MainMenuEntry(void) { return MAINMENUENTRY; }
  virtual cOsdObject *MainMenuAction(void);
  virtual cMenuSetupPage *SetupMenu(void);
  virtual bool SetupParse(const char *Name, const char *Value);
  virtual bool Service(const char *Id, void *Data = NULL);
  virtual const char **SVDRPHelpPages(void);
  virtual cString SVDRPCommand(const char *Command, const char *Option, int &ReplyCode);
  };

cPluginVompserver::cPluginVompserver(void)
{
  // Initialize any member variables here.
  // DON'T DO ANYTHING ELSE THAT MAY HAVE SIDE EFFECTS, REQUIRE GLOBAL
  // VDR OBJECTS TO EXIST OR PRODUCE ANY OUTPUT!
  
  configDir = NULL;
}

cPluginVompserver::~cPluginVompserver()
{
  // Clean up after yourself!
  mvpserver.stop();
  if (configDir) delete[] configDir;  

}

const char *cPluginVompserver::CommandLineHelp(void)
{
  // Return a string that describes all known command line options.
  return "  -c dir    config path relative to VDR plugins config path\n";
}

bool cPluginVompserver::ProcessArgs(int argc, char *argv[])
{
  // Implement command line argument processing here if applicable.
  
  int c;
  while ((c = getopt(argc, argv, "c:")) != -1)
  {
    if (c == 'c')
    {
      const char* vdrret = cPlugin::ConfigDirectory(optarg);
      if (!vdrret)
      {
        dsyslog("VOMP: Could not get config dir from VDR");
        return false;
      }
      
      configDir = new char[strlen(vdrret)+1];
      strcpy(configDir, vdrret);
    }
    else
    {
      return false;
    }
  }
  
  return true;
}

bool cPluginVompserver::Initialize(void)
{
  // Initialize any background activities the plugin shall perform.
  return true;
}

bool cPluginVompserver::Start(void)
{
  // Start any background activities the plugin shall perform.
  
  if (!configDir)
  {
    const char* vdrret = cPlugin::ConfigDirectory("vompserver");
    if (!vdrret)
    {
      dsyslog("VOMP: Could not get config dir from VDR");
      return false;
    }
    configDir = new char[strlen(vdrret)+1];
    strcpy(configDir, vdrret);
  }
  
  int success = mvpserver.run(configDir);
  if (success) return true;
  else return false;
}

void cPluginVompserver::Stop(void)
{
  // Stop any background activities the plugin is performing.
}

void cPluginVompserver::Housekeeping(void)
{
  // Perform any cleanup or other regular tasks.
}

void cPluginVompserver::MainThreadHook(void)
{
  // Perform actions in the context of the main program thread.
  // WARNING: Use with great care - see PLUGINS.html!
}

cString cPluginVompserver::Active(void)
{
  // Return a message string if shutdown should be postponed
  if(VompClient::getNrClients() != 0) return tr("VOMP client(s) connected");
  return NULL;
}

time_t cPluginVompserver::WakeupTime(void)
{
  // Return custom wakeup time for shutdown script
  return 0;
}

cOsdObject *cPluginVompserver::MainMenuAction(void)
{
  // Perform the action when selected from the main VDR menu.
  return NULL;
}

cMenuSetupPage *cPluginVompserver::SetupMenu(void)
{
  // Return a setup menu in case the plugin supports one.
  return NULL;
}

bool cPluginVompserver::SetupParse(const char *Name, const char *Value)
{
  // Parse your own setup parameters and store their values.
  return false;
}

bool cPluginVompserver::Service(const char *Id, void *Data)
{
  // Handle custom service requests from other plugins
  return false;
}

const char **cPluginVompserver::SVDRPHelpPages(void)
{
  // Return help text for SVDRP commands this plugin implements
  return NULL;
}

cString cPluginVompserver::SVDRPCommand(const char *Command, const char *Option, int &ReplyCode)
{
  // Process SVDRP commands this plugin implements
  return NULL;
}

VDRPLUGINCREATOR(cPluginVompserver); // Don't touch this!


#else //VOMPSTANDALONE

int main(int argc, char **argv) {
  char *cdir=".";
  if (argc > 1) {
    cdir=argv[1];
  }
  std::cout << "Vompserver starting Version " << VERSION << " " << DESCRIPTION << std::endl;
  MVPServer server;
  if ( server.run(cdir) != 1) {
    std::cerr << "unable to start vompserver" << std::endl;
    return 1;
  }
  while (1) sleep(1);
  return 0;
}

#endif //VOMPSTANDALONE
