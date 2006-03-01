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
  remuxer = NULL;
  unprocessed = NULL;

  // Init

  // Get the remuxer for audio or video

#if VDRVERSNUM < 10300
//  if ((channel->Vpid() == 0) || (channel->Vpid() == 1) || (channel->Vpid() == 0x1FFF))
//  {
//    remuxer = new cTS2ESRemux(channel->Apid1());
//    logger->log("MVPReceiver", Log::DEBUG, "Created new < 1.3 TS->ES");
//  }
//  else
//  {
    remuxer = new cTS2PSRemux(channel->Vpid(), channel->Apid1(), 0, 0, 0, 0);
    logger->log("MVPReceiver", Log::DEBUG, "Created new < 1.3 TS->PS");
//  }
#else
//  if ((channel->Vpid() == 0) || (channel->Vpid() == 1) || (channel->Vpid() == 0x1FFF))
//  {
//    remuxer = new cTS2ESRemux(channel->Apid(0));
//    logger->log("MVPReceiver", Log::DEBUG, "Created new > 1.3 TS->ES");
//  }
//  else
//  {
    remuxer = new cTS2PSRemux(channel->Vpid(), channel->Apid(0), 0, 0, 0, 0);
    logger->log("MVPReceiver", Log::DEBUG, "Created new > 1.3 TS->PS");
//  }
#endif

  unprocessed = new cRingBufferLinear(1000000, TS_SIZE * 2, false);

  if (!processed.init(1000000)) return;
  pthread_mutex_init(&processedRingLock, NULL);

  if (!threadStart()) return;

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
  if (threadIsActive()) threadCancel();
  if (unprocessed) delete unprocessed;
  if (remuxer) delete remuxer;
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
  static int receiveCount = 0;

//  int p = unprocessed->Put(data, length);
//  if (p != length) printf("Buffer overrun\n");

  unprocessed->Put(data, length);

  if (++receiveCount == 15)
  {
    threadSignal();
    receiveCount = 0;
  }
}

void MVPReceiver::threadMethod()
{
  int amountGot;
  UCHAR* dataGot;

  int remuxTook;
  UCHAR* remuxedData;
  int outputSize;

  while(1)
  {
    threadWaitForSignal();

    while(1)
    {
      dataGot = unprocessed->Get(amountGot);
      if (dataGot && (amountGot > 0))
      {
        outputSize = 0;
        remuxTook = amountGot;
        remuxedData = remuxer->Process(dataGot, remuxTook, outputSize);
        unprocessed->Del(remuxTook);

        pthread_mutex_lock(&processedRingLock);
        processed.put(remuxedData, outputSize);
        pthread_mutex_unlock(&processedRingLock);

//        logger->log("MVPReceiver", Log::DEBUG, "Got from unprocessed: %i, Got from remux: %p %i, consumed: %i",
//               amountGot, remuxedData, outputSize, remuxTook);
      }
      else
      {
        break;
      }
    }
  }
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
