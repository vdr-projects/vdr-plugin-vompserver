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

#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "defines.h"

class Ringbuffer
{
  public:
    Ringbuffer();
    ~Ringbuffer();
    int init(size_t size);
    int put(const UCHAR* from, size_t amount);
    int get(UCHAR* to, size_t amount);
    int getContent();

  private:
    UCHAR* buffer;
    UCHAR* start;
    UCHAR* end;
    size_t capacity;
    size_t content;
};

#endif
