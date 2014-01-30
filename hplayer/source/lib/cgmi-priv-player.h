#ifndef __GST_PLAYER_H__
#define __GST_PLAYER_H__

#include "cgmiPlayerApi.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define MAX_AUDIO_LANGUAGE_DESCRIPTORS 32
#define MAX_STREAMS                    32
#define SOCKET_RECEIVE_BUFFER_SIZE     1000000
#define UDP_CHUNK_SIZE                 (1316*32)
#define VIDEO_MAX_WIDTH                1280
#define VIDEO_MAX_HEIGHT               720

typedef struct
{
   guint pid;
   guint index;
   guint streamType;
   gchar isoCode[4];
}tAudioLang;

typedef struct
{
   gint x;
   gint y;
   gint w;
   gint h;
}tCgmiRect;

typedef struct
{
   gint pid;
   gint streamType;
}tCgmiStream;

typedef struct
{
   void*              cookie;
   GMainContext       *thread_ctx; 
   GThread            *thread;
   gchar              *playbackURI; /* URI to playback */
   gchar              *manualPipeline; /* URI to playback */
   GMainLoop          *loop;
   GstElement         *pipeline;
   GstElement         *source;
   guint              tag;
   GstElement         *videoSink;
   GstElement         *videoDecoder;
   GstElement         *demux;
   GstElement         *udpsrc;   
   GstBus             *bus;
   GstMessage         *msg;
   void*              usrParam;
   tCgmiRect          vidDestRect;
   tAudioLang         audioLanguages[MAX_AUDIO_LANGUAGE_DESCRIPTORS];
   gchar              defaultAudioLanguage[4];
   gint               numAudioLanguages;
   gint               audioLanguageIndex;
   gint               numStreams;
   tCgmiStream        streams[MAX_STREAMS];
   /* user registered data */ 
   cgmi_EventCallback eventCB;
   GstElement         *userDataAppsink;
   GstPad             *userDataPad;
   GstPad             *userDataAppsinkPad;
   userDataBufferCB   userDataBufferCB;
   void               *userDataBufferParam;
   gint               autoPlay;
   gboolean           waitingOnPids;
   GMutex             *autoPlayMutex;
   GCond              *autoPlayCond; 
   gint               videoStreamIndex;
   gint               audioStreamIndex;

}tSession;

gboolean cisco_gst_init( int argc, char *argv[] );
void cisco_gst_deinit( void );
gint cisco_gst_set_pipeline(tSession *pSession, char *uri, const char *manualPipeline );
tSession*  cisco_create_session(void * usrParam);
void cisco_delete_session(tSession *pSession);

gint cisco_gst_play(tSession *pSession  );
gint cisco_gst_pause(tSession *pSession  );

 void debug_cisco_gst_streamDurPos( tSession *pSession );
#ifdef __cplusplus
}
#endif

#endif 

