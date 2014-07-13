#include "picturereader.h"
#include <vdr/plugin.h>
#include <sstream>


PictureReader::PictureReader(VompClient *client)
{
  logger = Log::getInstance();
  inittedOK = 0;
  tcp = NULL;
  x = client;

  pthread_mutex_init(&pictureLock, NULL);
}  

int PictureReader::init(TCP* ttcp)
{
  tcp = ttcp;
  threadStart();

  return inittedOK;
}

PictureReader::~PictureReader()
{
   threadStop();
}

void PictureReader::addTVMediaRequest(TVMediaRequest& req)
{
    logger->log("PictRead",Log::DEBUG,"Got TVMediaRequest, signal thread!");

    pthread_mutex_lock(&pictureLock);
    pictures.push(req);
    threadSignal(); // Signal, that we have something to do!!!
    pthread_mutex_unlock(&pictureLock);
}

std::string PictureReader::getPictName(TVMediaRequest & req)
{
   logger->log("PictRead",Log::DEBUG,
	"Request %d %d;  %d %d %d %d",req.primary_id,req.secondary_id,
	 req.type,req.type_pict,
	req.container,req.container_member);

   switch (req.type) {
   case 0: { //serie
      if (series.seriesId != req.primary_id ||
          series.episodeId != req.secondary_id) {
          series.actors.clear();
          series.posters.clear();
          series.banners.clear();
          series.fanarts.clear();

          series.seriesId = req.primary_id;
          series.episodeId = req.secondary_id;
          x->scraper->Service("GetSeries",&series);
      }
      if (req.type_pict == 0) {
        switch (req.container) {
        case 0: {
           return series.episode.episodeImage.path;
        } break;
        case 1: {
          if (series.actors.size()>req.container_member) {
        	return series.actors[req.container_member].actorThumb.path;
          }
        } break;
        case 2: {
          if (series.posters.size()>req.container_member) {
        	return series.posters[req.container_member].path;
          }
        } break;
        case 3: {
          if (series.banners.size()>req.container_member) {
        	return series.banners[req.container_member].path;
          }
        } break;
        case 4: {
          if (series.fanarts.size()>req.container_member) {
        	return series.fanarts[req.container_member].path;
          }
        } break;
        case 5: {
           return series.seasonPoster.path;
        } break;
        default: {
           return std::string("");
           } break;
        };
      } else if (req.type_pict == 1 && series.posters.size()) { //poster
           std::string str=series.posters[0].path;
           size_t  pos=str.rfind('/');
           if (pos!=std::string::npos) {
              str.resize(pos);
              str=str+"poster_thumb.jpg";
              return str;
           }
        } else if (req.type_pict == 2) { //poster
           std::string str=series.seasonPoster.path;
           size_t  pos=str.rfind('/');
           if (pos!=std::string::npos) {
              str.resize(pos);
              std::ostringstream out;
              out << str << "season_" <<series.episode.season <<"_thumb.jpg";
              return out.str();
           }
        } 
       return std::string("");
   } break;
   case 1: { //movie
      if (movie.movieId != req.primary_id ) {
          movie.actors.clear();
          movie.movieId = req.primary_id;
          x->scraper->Service("GetMovie",&movie);
      }
      if (req.type_pict == 0) {

        switch (req.container) {
        case 0: {
           return movie.poster.path;
        } break;
        case 1: {
           return movie.fanart.path;
        } break;
        case 2: {
           return movie.collectionPoster.path;
        } break;
        case 3: {
           return movie.collectionFanart.path;
        } break;
        case 4: {
          if (movie.actors.size()>req.container_member) {
        	return movie.actors[req.container_member].actorThumb.path;
          }
        } break;
        default: {
           return std::string("");
           } break;
        };
    } else if (req.type_pict == 1) { //poster
        std::string str=movie.poster.path;
        size_t  pos=str.rfind('/');
        if (pos!=std::string::npos) {
           str.resize(pos);
           str=str+"poster_thumb.jpg";
           return str;
        }
    } 
    return std::string("");
      
   
   } break;
   
   default:
     return std::string("");
     break;
   };
     
   
}


void PictureReader::threadMethod()
{
  ULONG *p;
  ULONG headerLength = sizeof(ULONG) * 4;
  UCHAR buffer[headerLength];
  int amountReceived;

//   threadSetKillable(); ??

  logger->log("PictRead",Log::DEBUG,"PictureReaderThread started");
  while(1)
  {
    threadLock();
    threadWaitForSignal();
    threadUnlock();
    threadCheckExit();
    bool newpicture;
    logger->log("PictRead",Log::DEBUG,"Thread was signaled, wake up");

    do
    {
      newpicture = false;
      TVMediaRequest req;
      pthread_mutex_lock(&pictureLock);
      if (!pictures.empty()) {
         newpicture = true;
         req = pictures.front();
         pictures.pop();
      }
      pthread_mutex_unlock(&pictureLock);
      if (!newpicture) break;
      std::string pictname = getPictName(req);
      UCHAR * mem = NULL;
      ULONG memsize = 0;
      ULONG flag = 2;
      logger->log("PictRead",Log::DEBUG,"Load Pict %s",pictname.c_str());

      
      if (pictname.length()) {
         struct stat st;
         ULONG filesize = 0 ;

         stat(pictname.c_str(), &st);
         filesize = st.st_size;
         memsize = filesize + headerLength;

         if (memsize && memsize < 1000000) { // No pictures over 1 MB
            mem = (UCHAR*)malloc(memsize);
            if (mem) {
        	FILE * file=fopen(pictname.c_str(),"r");

        	if (file) {
        	    size_t size=fread(mem+headerLength,1,filesize,file);

        	    fclose(file);
        	    if (size!=filesize) memsize=headerLength; // error
        	    else flag = 0;
        	}
             }
           } 
       } 
       if (!mem) {
          mem = buffer;
       }

         
    
      p = (ULONG*)&mem[0]; *p = htonl(5); // stream channel
      p = (ULONG*)&mem[4]; *p = htonl(req.streamID);
      p = (ULONG*)&mem[8]; *p = htonl(flag); // here insert flag: 0 = ok, data follows
      p = (ULONG*)&mem[12]; *p = htonl(memsize);

      if (!tcp->sendPacket(mem, memsize + headerLength)) {
          logger->log("PictRead",Log::DEBUG,"Sending Picture failed");
      }

      if (mem != buffer && mem) free(mem);

    } while (newpicture);
  }
  logger->log("PictRead",Log::DEBUG,"PictureReaderThread ended");
  
}

