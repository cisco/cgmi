#ifndef __GST_PLAYER_H__
#define __GST_PLAYER_H__

#include "cgmiPlayerApi.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define MAX_AUDIO_LANGUAGE_DESCRIPTORS 32
#define MAX_SUBTITLE_LANGUAGES         16
#define MAX_CLOSED_CAPTION_SERVICES    71
#define MAX_STREAMS                    32
#define SOCKET_RECEIVE_BUFFER_SIZE     1000000
#define UDP_CHUNK_SIZE                 (1316*32)
#define VIDEO_MAX_WIDTH                1920
#define VIDEO_MAX_HEIGHT               1080

typedef struct
{
   guint pid;
   guint index;
   guint streamType;
   gchar isoCode[4];
   gboolean bDiscrete;
}tAudioLang;

typedef struct
{
   gboolean isDigital;
   gint serviceNum;
   gchar isoCode[4];
}tCCLang;

typedef struct
{
   guint    pid;
   guchar   type;
   gushort  compPageId;
   gushort  ancPageId;
   gchar    isoCode[4];
}tSubtitleInfo;

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
   GSource            *sourceWatch;
   GThread            *monitor;
   gchar              *playbackURI; /* URI to playback */
   GMainLoop          *loop;
   GstElement         *pipeline;
   GstElement         *source;
   GstElement         *videoSink;
   GstElement         *audioSink;
   GstElement         *videoDecoder;
   GstElement         *audioDecoder;
   GstElement         *demux;
   GstElement         *udpsrc;   
   GstElement         *hlsDemux;
   GstBus             *bus;
   GstMessage         *msg;
   void*              usrParam;
   tCgmiRect          vidSrcRect;
   tCgmiRect          vidDestRect;
   tAudioLang         audioLanguages[MAX_AUDIO_LANGUAGE_DESCRIPTORS];
   tCCLang            closedCaptionServices[MAX_CLOSED_CAPTION_SERVICES];
   tSubtitleInfo      subtitleInfo[MAX_SUBTITLE_LANGUAGES];
   gchar              defaultAudioLanguage[4];
   gchar              defaultSubtitleLanguage[4];
   gint               numAudioLanguages;
   gint               audioLanguageIndex;
   gint               numClosedCaptionServices;
   gint               numStreams;
   gint               numSubtitleLanguages;
   gint               subtitleLanguageIndex;
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
   gboolean           isAudioMuted;
   gboolean           runMonitor;
   GMutex             *autoPlayMutex;
   GCond              *autoPlayCond; 
   gint               videoStreamIndex;
   gint               audioStreamIndex;
   uint64_t           drmProxyHandle;
   float              rate;
   float              rateBeforePause;
   float              rateAfterPause;
   unsigned int       diagIndex;
   gboolean           pendingSeek;
   float              pendingSeekPosition;
   gboolean           bisDLNAContent;
   gboolean           steadyState;
   guint              steadyStateWindow;
   gboolean           maskRateChangedEvent;
   gboolean           noVideo;
   gboolean           bQueryDiscreteAudioInfo;
   void               *cpblob;
   gchar              currAudioLanguage[4];
   /* used when we reconstruct the pipeline for discrete<->muxed audio language switch */
   gchar              newAudioLanguage[4];
   gboolean           suppressLoadDone;
   gboolean           isPlaying;

}tSession;

gboolean cisco_gst_init( int argc, char *argv[] );
void cisco_gst_deinit( void );
gint cisco_gst_set_pipeline(tSession *pSession, char *uri, const char *manualPipeline );
tSession*  cisco_create_session(void * usrParam);
void cisco_delete_session(tSession *pSession);

gint cisco_gst_play(tSession *pSession  );
gint cisco_gst_pause(tSession *pSession  );

 void debug_cisco_gst_streamDurPos( tSession *pSession );
cgmi_Status cgmi_utils_init(void);
cgmi_Status cgmi_utils_finalize(void);
cgmi_Status cgmi_utils_is_content_dlna(const gchar* url, uint32_t *bisDLNAContent);
#ifdef __cplusplus
}
#endif

#endif 

