#ifndef VDR_STREAMDEV_TSREMUX_H
#define VDR_STREAMDEV_TSREMUX_H

#include "transform.h"
#include <vdr/remux.h>

#include "../log.h"

#define IPACKS 2048

#define PROTECTIONSIZE 32768

#ifndef RESULTBUFFERSIZE
#define RESULTBUFFERSIZE KILOBYTE(256)
#endif
#ifndef MINVIDEODATA
#define MINVIDEODATA (16*1024)
#endif

class cTSRemux {
protected:
  uchar m_PROTECTION1[PROTECTIONSIZE]; // something sometimes overwrites vtbl without this buffer
  uchar m_ResultBuffer[RESULTBUFFERSIZE];
        int m_ResultCount;
        int m_ResultDelivered;
        int m_Synced;
        int m_Skipped;

  int GetPacketLength(const uchar *Data, int Count, int Offset);
  int ScanVideoPacket(const uchar *Data, int Count, int Offset, uchar &PictureType);

        virtual void PutTSPacket(int Pid, const uint8_t *Data) = 0;

public:
        int m_Sync;// CJT moved from protected

        cTSRemux(bool Sync = true);
        virtual ~cTSRemux();

        virtual uchar *Process(const uchar *Data, int &Count, int &Result);

        static void SetBrokenLink(uchar *Data, int Length);
};

#endif // VDR_STREAMDEV_TSREMUX_H
