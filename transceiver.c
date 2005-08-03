/*
     Edited for VOMP by Chris Tallon
     Edits Copyright 2004-2005 Chris Tallon

     This class will be replaced soon.
*/

/*
 *   MediaMVP Server
 *
 *   (C) 2003 Dominic Morris
 *
 *   $Id$
 *   $Date$
 *
 *   Transceiver stuff - blatantly stolen from streamdev then changed
 *   a bit..
 */





#include "transceiver.h"
#include "ts2ps.h"
#include "ts2es.h"
//#include "setup.h"

#include <vdr/ringbuffer.h>

#include <sys/types.h>
#include <unistd.h>


#define VIDEOBUFSIZE MEGABYTE(1)

/* Disable logging if BUFCOUNT buffer overflows occur within BUFOVERTIME
   milliseconds. Enable logging again if there is no error within BUFOVERTIME
   milliseconds. */
#define BUFOVERTIME  5000
#define BUFOVERCOUNT 100

#if VDRVERSNUM < 10300
cMediamvpTransceiver::cMediamvpTransceiver(const cChannel *Channel, int Priority, int Socket, cDevice *Device) :
                cReceiver(Channel->Ca(), Priority, 7, Channel->Vpid(), Channel->Ppid(),
                                Channel->Apid1(), Channel->Apid2(), Channel->Dpid1(), Channel->Dpid2(),
                                Channel->Tpid()) {
#else
cMediamvpTransceiver::cMediamvpTransceiver(const cChannel *Channel, int Priority, int Socket, cDevice *Device) :
                cReceiver(Channel->Ca(), Priority, Channel->Vpid(),
                                Channel->Apids(), Channel->Dpids(), Channel->Spids()) {
#endif
  m_Active = false;
        m_Socket = Socket;
        m_Remux = NULL;
        m_Device = Device;

//cjt
  log = Log::getInstance();

        m_RingBuffer = new cRingBufferLinear(VIDEOBUFSIZE, TS_SIZE * 2, true);
//        m_RingBuffer = new cRingBufferLinear(VIDEOBUFSIZE, TS_SIZE * 20, true);

    /* Select the correct Muxing depending on whether it's video or not */
#if VDRVERSNUM < 10300
    if ( Channel->Vpid() == 0 || Channel->Vpid() == 1 || Channel->Vpid() == 0x1FFF ) {
        m_Remux = new cTS2ESRemux(Channel->Apid1());
    } else {
                m_Remux = new cTS2PSRemux(Channel->Vpid(), Channel->Apid1(), 0, 0, 0, 0);
    }
#else
    if ( Channel->Vpid() == 0 || Channel->Vpid() == 1 || Channel->Vpid() == 0x1FFF ) {
        m_Remux = new cTS2ESRemux(Channel->Apid(0));
    } else {
                m_Remux = new cTS2PSRemux(Channel->Vpid(), Channel->Apid(0), 0, 0, 0, 0);
    }
#endif
    log->log("Transciever", Log::DEBUG, "Created transceiver at %p, remux @%p ringbuffer %p",this,m_Remux,m_RingBuffer);

    /* Suggested by Peter Wagner to assist single DVB card systems */
#ifdef SINGLE_DEVICE
        m_Device->SwitchChannel(Channel, true);
#else
        m_Device->SwitchChannel(Channel, false);
#endif
        Attach();


        // CJT
        rb.init(1000000);
        pthread_mutex_init(&ringLock, NULL);

}

cMediamvpTransceiver::~cMediamvpTransceiver(void)
{
    log->log("Transciever", Log::DEBUG, "Deleting transceiver at %p, remux @%p ringbuffer %p",this,m_Remux,m_RingBuffer);

        Detach();
        if (m_Remux)
        delete m_Remux;
    m_Remux = NULL;
    if ( m_RingBuffer)
        delete m_RingBuffer;
    m_RingBuffer = NULL;
}

void cMediamvpTransceiver::Activate(bool On)
{
        if (On)
                Start();
        else if (m_Active)
                Stop();
}

void cMediamvpTransceiver::Stop(void)
{
        if (m_Active) {
                m_Active = false;
                usleep(50000);
                Cancel(0);
        }
}

void cMediamvpTransceiver::Receive(uchar *Data, int Length)
{
        static time_t firsterr = 0;
        static int errcnt = 0;
        static bool showerr = true;

        if (m_Active) {
                int p = m_RingBuffer->Put(Data, Length);
                if (p != Length) {
                        ++errcnt;
#if VDRVERSNUM < 10300
      if (showerr) {
                                if (firsterr == 0)
                                        firsterr = time_ms();
                                else if (firsterr + BUFOVERTIME > time_ms() && errcnt > BUFOVERCOUNT) {
                                        esyslog("ERROR: too many buffer overflows, logging stopped");
                                        showerr = false;
                                        firsterr = time_ms();
                                }
                        } else if (firsterr + BUFOVERTIME < time_ms()) {
                                showerr = true;
                                firsterr = 0;
                                errcnt = 0;
                        }

                        if (showerr)
                                esyslog("ERROR: ring buffer overflow (%d bytes dropped)", Length - p);
                        else
                                firsterr = time_ms();
#else
      if (showerr) {
                                if (firsterr == 0) {
                                        firsterr = 1;
          lastTime.Set();
        }
                                else if (lastTime.Elapsed() > BUFOVERTIME && errcnt > BUFOVERCOUNT) {
                                        esyslog("ERROR: too many buffer overflows, logging stopped");
                                        showerr = false;
                                }
                        } else if (lastTime.Elapsed() < BUFOVERTIME) {
                                showerr = true;
                                firsterr = 0;
                                errcnt = 0;
                        }

                        if (showerr)
                                esyslog("ERROR: ring buffer overflow (%d bytes dropped)", Length - p);
                        else
                                firsterr = 1;
#endif
                }
        }
}

void cMediamvpTransceiver::Action(void)
{
        int max = 0;


    log->log("Transciever", Log::DEBUG, "Mediamvp: Transceiver thread started (pid=%d)", getpid());


        m_Active = true;

        while (m_Active) {
                int recvd;
                const uchar *block = m_RingBuffer->Get(recvd);

                if (block && recvd > 0) {
                        const uchar *sendBlock;
                        int bytes = 0;
                        int taken = recvd;

            sendBlock = m_Remux->Process(block, taken, bytes);

                        m_RingBuffer->Del(taken);

                        if (bytes > max)
                                max = bytes;
      // CJT

       //     write(m_Socket,sendBlock,bytes);
      //      printf("Written %i bytes\n", bytes);


      pthread_mutex_lock(&ringLock);
      rb.put((unsigned char*)sendBlock, bytes);
      pthread_mutex_unlock(&ringLock);
//printf("Put %i into buffer\n", bytes);


                } else
                        usleep(1);
        }


    log->log("Transciever", Log::DEBUG, "Mediamvp: Transceiver thread ended");
}

unsigned long cMediamvpTransceiver::getBlock(unsigned char* buffer, unsigned long amount)
{
  pthread_mutex_lock(&ringLock);

  int numTries = 0;

  while ((unsigned long)rb.getContent() < amount)
  {
    pthread_mutex_unlock(&ringLock);
    if (++numTries == 10) // 5s
    {
      log->log("Transciever", Log::DEBUG, "getBlock timeout");
      return 0;
    }
    usleep(500000);
    pthread_mutex_lock(&ringLock);
  }

  unsigned long amountReceived = rb.get(buffer, amount);
  pthread_mutex_unlock(&ringLock);
  return amountReceived;
}
