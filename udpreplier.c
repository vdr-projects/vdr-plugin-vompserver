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

#include "udpreplier.h"

UDPReplier::UDPReplier()
 : ds(3024)
{
}

UDPReplier::~UDPReplier()
{
  if (threadIsActive()) stop();
}

int UDPReplier::stop()
{
  if (!threadIsActive()) return 0;
  threadCancel();
  return 1;
}

int UDPReplier::run()
{
  if (threadIsActive()) return 1;

  if (!threadStart()) return 0;

  Log::getInstance()->log("UDP", Log::DEBUG, "UDP replier started");
  return 1;
}

void UDPReplier::threadMethod()
{
  int retval;
  while(1)
  {
    retval = ds.waitforMessage(0);
    if (retval == 1) continue;

    if (!strcmp(ds.getData(), "VOMP"))
      ds.send(ds.getFromIPA(), 3024, "VOMP SERVER", 11);
  }
}
