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

#include "recplayer.h"

RecPlayer::RecPlayer(cRecording* rec)
{
  log = Log::getInstance();
  file = NULL;
  fileOpen = 0;
  lastPosition = 0;
  recording = rec;
  for(int i = 1; i < 1000; i++) segments[i] = NULL;

  // FIXME find out max file path / name lengths

  indexFile = new cIndexFile(recording->FileName(), false);
  if (!indexFile) log->log("RecPlayer", Log::ERR, "Failed to create indexfile!");

  scan();
}

void RecPlayer::scan()
{
  if (file) fclose(file);
  totalLength = 0;
  fileOpen = 0;

  int i = 1;
  while(segments[i++]) delete segments[i];

  char fileName[2048];
  for(i = 1; i < 1000; i++)
  {
    snprintf(fileName, 2047, "%s/%03i.vdr", recording->FileName(), i);
    log->log("RecPlayer", Log::DEBUG, "FILENAME: %s", fileName);
    file = fopen(fileName, "r");
    if (!file) break;

    segments[i] = new Segment();
    segments[i]->start = totalLength;
    fseek(file, 0, SEEK_END);
    totalLength += ftell(file);
    log->log("RecPlayer", Log::DEBUG, "File %i found, totalLength now %llu", i, totalLength);
    segments[i]->end = totalLength;
    fclose(file);
  }

  file = NULL;
}

RecPlayer::~RecPlayer()
{
  log->log("RecPlayer", Log::DEBUG, "destructor");
  int i = 1;
  while(segments[i++]) delete segments[i];
  if (file) fclose(file);
}

int RecPlayer::openFile(int index)
{
  if (file) fclose(file);

  char fileName[2048];
  snprintf(fileName, 2047, "%s/%03i.vdr", recording->FileName(), index);
  log->log("RecPlayer", Log::DEBUG, "openFile called for index %i string:%s", index, fileName);

  file = fopen(fileName, "r");
  if (!file)
  {
    log->log("RecPlayer", Log::DEBUG, "file failed to open");
    fileOpen = 0;
    return 0;
  }
  fileOpen = index;
  return 1;
}

ULLONG RecPlayer::getTotalLength()
{
  return totalLength;
}

unsigned long RecPlayer::getBlock(unsigned char* buffer, ULLONG position, unsigned long amount)
{
  if ((amount > totalLength) || (amount > 500000))
  {
    log->log("RecPlayer", Log::DEBUG, "Amount %lu requested and rejected", amount);
    return 0;
  }

  if (position >= totalLength)
  {
    log->log("RecPlayer", Log::DEBUG, "Client asked for data starting past end of recording!");
    return 0;
  }

  if ((position + amount) > totalLength)
  {
    log->log("RecPlayer", Log::DEBUG, "Client asked for some data past the end of recording, adjusting amount");
    amount = totalLength - position;
  }

  // work out what block position is in
  int segmentNumber;
  for(segmentNumber = 1; segmentNumber < 1000; segmentNumber++)
  {
    if ((position >= segments[segmentNumber]->start) && (position < segments[segmentNumber]->end)) break;
    // position is in this block
  }

  // we could be seeking around
  if (segmentNumber != fileOpen)
  {
    if (!openFile(segmentNumber)) return 0;
  }

  ULLONG currentPosition = position;
  ULONG yetToGet = amount;
  ULONG got = 0;
  ULONG getFromThisSegment = 0;
  ULONG filePosition;

  while(got < amount)
  {
    if (got)
    {
      // if(got) then we have already got some and we are back around
      // advance the file pointer to the next file
      if (!openFile(++segmentNumber)) return 0;
    }

    // is the request completely in this block?
    if ((currentPosition + yetToGet) <= segments[segmentNumber]->end)
      getFromThisSegment = yetToGet;
    else
      getFromThisSegment = segments[segmentNumber]->end - currentPosition;

    filePosition = currentPosition - segments[segmentNumber]->start;
    fseek(file, filePosition, SEEK_SET);
    if (fread(&buffer[got], getFromThisSegment, 1, file) != 1) return 0; // umm, big problem.

    got += getFromThisSegment;
    currentPosition += getFromThisSegment;
    yetToGet -= getFromThisSegment;
  }

  lastPosition = position;
  return got;
}

ULLONG RecPlayer::getLastPosition()
{
  return lastPosition;
}

cRecording* RecPlayer::getCurrentRecording()
{
  return recording;
}

ULLONG RecPlayer::positionFromFrameNumber(ULONG frameNumber)
{
  if (!indexFile) return 0;

  uchar retFileNumber;
  int retFileOffset;
  uchar retPicType;
  int retLength;

  if (!indexFile->Get((int)frameNumber, &retFileNumber, &retFileOffset, &retPicType, &retLength))
  {
    return 0;
  }

  log->log("RecPlayer", Log::DEBUG, "FN: %u FO: %i PT: %u L: %i", retFileNumber, retFileOffset, retPicType, retLength);

  return 0;
}
