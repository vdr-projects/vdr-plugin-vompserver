/*
     Edited for VOMP by Chris Tallon
     Edits Copyright 2004-2005 Chris Tallon
*/

/*
 *   MediaMVP Plugin
 *
 *   (C) 2003 Dominic Morris
 *
 *   $Id$
 *   $Date$
 *
 *
 *   Transceiver stuff, stolen from streamdev again...
 */

#ifndef VDR_MEDIAMVP_TRANSCEIVER_H
#define VDR_MEDIAMVP_TRANSCEIVER_H

#include <vdr/receiver.h>

#include <vdr/thread.h>
#include <vdr/status.h>

class cRingBufferLinear;
class cRemux;
class cTSRemux;
class cServerConnection;
class cChannel;


#include <pthread.h>
#include "ringbuffer.h"

class cMediamvpTransceiver: public cReceiver, public cThread {
//    friend class cMediamvpVdrURL;
private:
        cDevice *m_Device;
        cRingBufferLinear *m_RingBuffer;
        cTSRemux *m_Remux;
    int       m_Socket;

        bool m_Active;

        // CJT
        Ringbuffer rb;
        pthread_mutex_t ringLock;


protected:
        virtual void Receive(uchar *Data, int Length);
        virtual void Action(void);

public:
        cMediamvpTransceiver(const cChannel *Channel, int Priority, int Socket, cDevice *Device);
        virtual ~cMediamvpTransceiver(void);

        bool Attach(void) { return m_Device->AttachReceiver(this); }
        void Detach(void) { cReceiver::Detach(); }

        void Stop(void);


        // CJT
        unsigned long getBlock(unsigned char* buffer, unsigned long amount);
        virtual void Activate(bool On);

};

#endif // VDR_MEDIAMVP_TRANSCEIVER_H