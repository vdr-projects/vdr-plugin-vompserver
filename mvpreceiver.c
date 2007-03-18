#include "mvpreceiver.h"

MVPReceiver* MVPReceiver::create(cChannel* channel, int priority)
{
  bool NeedsDetachReceivers;
  cDevice* device = cDevice::GetDevice(channel, priority, &NeedsDetachReceivers);

  if (!device)
  {
    Log::getInstance()->log("MVPReceiver", Log::DEBUG, "No device found to receive this channel at this priority");
    return NULL;
  }

  if (NeedsDetachReceivers)
  {
    Log::getInstance()->log("MVPReceiver", Log::DEBUG, "Needs detach receivers");

    // Need to detach other receivers or VDR will shut down
  }

  MVPReceiver* m = new MVPReceiver(channel, device);
  return m;
}

MVPReceiver::MVPReceiver(cChannel* channel, cDevice* device)
#if VDRVERSNUM < 10300
: cReceiver(channel->Ca(), 0, 7, channel->Vpid(), channel->Ppid(), channel->Apid1(), channel->Apid2(), channel->Dpid1(), channel->Dpid2(), channel->Tpid())
#else
: cReceiver(channel->Ca(), 0, channel->Vpid(), channel->Apids(), channel->Dpids(), channel->Spids())
#endif
{
  logger = Log::getInstance();
  vdrActivated = false;
  inittedOK = 0;

//  logger->log("MVPReceiver", Log::DEBUG, "Channel has VPID %i APID %i", channel->Vpid(), channel->Apid(0));

  if (!processed.init(1000000)) return;
  pthread_mutex_init(&processedRingLock, NULL);

  // OK

  inittedOK = 1;
  device->SwitchChannel(channel, false);
  device->AttachReceiver(this);
}

int MVPReceiver::init()
{
  return inittedOK;
}

MVPReceiver::~MVPReceiver()
{
  Detach();
}

void MVPReceiver::Activate(bool on)
{
  vdrActivated = on;
  if (on) logger->log("MVPReceiver", Log::DEBUG, "VDR active");
  else logger->log("MVPReceiver", Log::DEBUG, "VDR inactive");
}

bool MVPReceiver::isVdrActivated()
{
  return vdrActivated;
}

void MVPReceiver::Receive(UCHAR* data, int length)
{
  pthread_mutex_lock(&processedRingLock);
  processed.put(data, length);
  pthread_mutex_unlock(&processedRingLock);
}

unsigned long MVPReceiver::getBlock(unsigned char* buffer, unsigned long amount)
{
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
}
