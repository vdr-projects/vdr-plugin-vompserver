#include "mvpreceiver.h"

MVPReceiver* MVPReceiver::create(cChannel* channel, int priority)
{
#if VDRVERSNUM < 10500
   bool NeedsDetachReceivers;
   cDevice* device = cDevice::GetDevice(channel, priority, &NeedsDetachReceivers);
#else
  cDevice* device = cDevice::GetDevice(channel, priority, true); // last param is live-view
#endif

  if (!device)
  {
    Log::getInstance()->log("MVPReceiver", Log::DEBUG, "No device found to receive this channel at this priority");
    return NULL;
  }

#if VDRVERSNUM < 10500
  if (NeedsDetachReceivers)
  {
    Log::getInstance()->log("MVPReceiver", Log::DEBUG, "Needs detach receivers");

    // Need to detach other receivers or VDR will shut down??
  }
#endif

  MVPReceiver* m = new MVPReceiver(channel, device);
  return m;
}

MVPReceiver::MVPReceiver(cChannel* channel, cDevice* device)
#if VDRVERSNUM < 10300
: cReceiver(channel->Ca(), 0, 7, channel->Vpid(), channel->Ppid(), channel->Apid1(), channel->Apid2(), channel->Dpid1(), channel->Dpid2(), channel->Tpid())
#elif VDRVERSNUM < 10500
: cReceiver(channel->Ca(), 0, channel->Vpid(), channel->Apids(), channel->Dpids(), channel->Spids())
#else
: cReceiver(channel->GetChannelID(), 0, channel->Vpid(), channel->Apids(), channel->Dpids(), channel->Spids())
#endif
{
  logger = Log::getInstance();
  vdrActivated = false;
  inittedOK = 0;
  streamID = 0;
  tcp = NULL;

//  logger->log("MVPReceiver", Log::DEBUG, "Channel has VPID %i APID %i", channel->Vpid(), channel->Apid(0));

  if (!processed.init(1000000)) return;
  pthread_mutex_init(&processedRingLock, NULL);

  // OK

  inittedOK = 1;
  device->SwitchChannel(channel, false);
  device->AttachReceiver(this);
}

int MVPReceiver::init(TCP* ttcp, ULONG tstreamID)
{
  tcp = ttcp;
  streamID = tstreamID;
  return inittedOK;
}

MVPReceiver::~MVPReceiver()
{
  Detach();
  threadStop();
}

void MVPReceiver::Activate(bool on)
{
  vdrActivated = on;
  if (on) 
  {
    logger->log("MVPReceiver", Log::DEBUG, "VDR active");
    threadStart();
  }
  else
  {
    logger->log("MVPReceiver", Log::DEBUG, "VDR inactive");
    threadStop();
  }
}

bool MVPReceiver::isVdrActivated()
{
  return vdrActivated;
}

void MVPReceiver::Receive(UCHAR* data, int length)
{
  pthread_mutex_lock(&processedRingLock);
  processed.put(data, length);
  if (processed.getContent() > streamChunkSize) threadSignal();
  pthread_mutex_unlock(&processedRingLock);
}

void MVPReceiver::threadMethod()
{
  UCHAR buffer[streamChunkSize + 12];
  int amountReceived;

//   threadSetKillable(); ??

  while(1)
  {
    threadWaitForSignal();
    threadCheckExit();
    
    do
    {
      pthread_mutex_lock(&processedRingLock);
      amountReceived = processed.get(buffer+12, streamChunkSize);
      pthread_mutex_unlock(&processedRingLock);
    
      *(ULONG*)&buffer[0] = htonl(2); // stream channel
      *(ULONG*)&buffer[4] = htonl(streamID);
      *(ULONG*)&buffer[8] = htonl(amountReceived);
      tcp->sendPacket(buffer, amountReceived + 12);
    } while(processed.getContent() >= streamChunkSize);
  }  
}

ULONG MVPReceiver::getBlock(unsigned char* buffer, unsigned long amount)
{
/*
  pthread_mutex_lock(&processedRingLock);

  int numTries = 0;

  while ((unsigned long)processed.getContent() < amount)
  {
    pthread_mutex_unlock(&processedRingLock);
    if (++numTries == 30) // 15s
    {
      logger->log("MVPReceiver", Log::DEBUG, "getBlock timeout");
      return 0;
    }
    usleep(500000);
    pthread_mutex_lock(&processedRingLock);
  }

  unsigned long amountReceived = processed.get(buffer, amount);
  pthread_mutex_unlock(&processedRingLock);
  return amountReceived;
  */
  sleep(10);
  return 0;
}
