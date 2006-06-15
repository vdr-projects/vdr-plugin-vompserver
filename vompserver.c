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

#include <vdr/plugin.h>
#include <getopt.h>

#include "mvpserver.h"

static const char *VERSION        = "0.2.4";
static const char *DESCRIPTION    = "VDR on MVP plugin by Chris Tallon";

class cPluginVompserver : public cPlugin
{
public:
  cPluginVompserver(void);
  virtual ~cPluginVompserver();
  virtual const char *Version(void) { return VERSION; }
  virtual const char *Description(void) { return DESCRIPTION; }
  virtual const char *CommandLineHelp(void);
  virtual bool ProcessArgs(int argc, char *argv[]);
  virtual bool Initialize(void);
  virtual bool Start(void);
  virtual bool SetupParse(const char *Name, const char *Value);

private:

  MVPServer mvpserver;
  char* configDir;
};

cPluginVompserver::cPluginVompserver(void)
{
  // Initialize any member variables here.
  // DON'T DO ANYTHING ELSE THAT MAY HAVE SIDE EFFECTS, REQUIRE GLOBAL
  // VDR OBJECTS TO EXIST OR PRODUCE ANY OUTPUT!

  configDir = NULL;
}

bool cPluginVompserver::Start(void)
{
  // Start any background activities the plugin shall perform.
  int success = mvpserver.run(configDir);
  if (success) return true;
  else return false;
}

cPluginVompserver::~cPluginVompserver()
{
  // Clean up after yourself!
  mvpserver.stop();
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
      configDir = optarg;
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

bool cPluginVompserver::SetupParse(const char *Name, const char *Value)
{
  // Parse your own setup parameters and store their values.
  return false;
}

VDRPLUGINCREATOR(cPluginVompserver); // Don't touch this!

