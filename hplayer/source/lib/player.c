#include "api.hpp"
#include "SailErr.h"
#include "string.h"
#include "stdlib.h"

#define TTX_PAGEMIN        100
#define TTX_PAGEMAX        1000
#define MIN_URL_LEN        10

//
// This is needed in porting layer to handle get/set requests
// while sessions are not open.
// Don't change this without adjusting AUDIO_VOLUME_STEPS 25 in audioout.c.
// 
#define MAX_VOLUME         20

#define MAX_DVR_SPEEDS     32
static float gDvrSpeeds[MAX_DVR_SPEEDS] = 
{
   -128.0,
   -64.0,
   -32.0,
   -8.0,
   1.0,
   8.0,
   32.0,
   64.0,
   128.0,
   0.0
};

typedef enum
{
   PLAYER_EVENT_NONE,
   PLAYER_EVENT_START_TSB
}tPlayerEvent;

typedef enum
{
   PLAYER_INITIALIZED   = 0x0000,
   PLAYER_OPEN          = 0x0001,
   PLAYER_ENABLED       = 0x0002,
   PLAYER_SOURCE        = 0x0004,
   PLAYER_SESSION       = 0x0008,
   PLAYER_STARTED       = 0x0010,
   PLAYER_ACTIVE        = 0x0020,
   PLAYER_DVR_PLAYBACK  = 0x0040,
   PLAYER_TSB_ACTIVE    = 0x0080,
   PLAYER_TSB_OPEN      = 0x0100,
   PLAYER_TSB_ENABLED   = 0x0200,
   PLAYER_TSB_STARTED   = 0x0400
}tPlayerState;

typedef struct
{
   SessionHandle        session;
   SessionHandle        tsbSession;
   tPlayerState         state;
   int                  duration;
   int                  position;
   int                  lock;
   char                 srcUrl[MAX_URL_LEN];
   char                 source[MAX_URL_LEN];
   char                 sinkUrl[MAX_FILE_LEN];
  // tAudioLangList       langList;
  // tSubtitleLangList    subLangList;
  // tDvbTtxLangList      ttxLangList;
  // Rectangle            srcRect;
  // Rectangle            destRect;
   int                 isMuted;
   MediaPlayerCallback  callback;
   MPCallbackData       *mpcdata;
   //rms char                 tsbUrl[MAX_FILE_LEN];
   //rms ui64                 tsbDuration;
   int                  id;
   float                speed;
   //osTimer              timer;
   ui32                 tsbDelay;
   void                 *recordHandle;
   //tSourceType          srcType;
   unsigned int                 srcId;
   unsigned char                  bandwidth;
   char                 annex;
}tPlayerHandle;

static tPlayerHandle gPlayerHandle[MAX_PLAYER_HANDLES];

logHandle gApiLogHandle = NULL;

static void mediaPlayerThread( void *args );
static tPlayerHandle *getMediaPlayer( SessionHandle session );
#if 0
static tSourceType getSrcType( char *srcUrl )
{
   tSourceType type = SOURCE_UNKNOWN;

#if 0
   if ( !strncmp(srcUrl, SESS_URL_DVR, strlen(SESS_URL_DVR)) )
   {
      type = SOURCE_DVR;
   }
   else if ( !strncmp(srcUrl, SESS_URL_HTTP, strlen(SESS_URL_HTTP)) )
   {
      type = SOURCE_HTTP;
   }
   else if ( !strncmp(srcUrl, SESS_URL_FILE, strlen(SESS_URL_FILE)) )
   {
      type = SOURCE_FILE;
   }
   else if ( !strncmp(srcUrl, SESS_URL_DLNA, strlen(SESS_URL_DLNA)) )
   {
      type = SOURCE_DLNA;
   }
#endif
   return type;
}
#endif
static tPlayerHandle *lockHandle( int Id )
{
   if ( Id < MAX_PLAYER_HANDLES )
   {
      LOCK( gPlayerHandle[Id].lock );
      return &gPlayerHandle[Id];
   }

   return NULL;
}

static void unlockHandle( tPlayerHandle *handle )
{
   UNLOCK( handle->lock );
}

SailError MediaPlayerOpen( int mediaPlayerId, MediaPlayerCallback callback, void *userParam )
{
   SailErr retCode = SailErr_None;

   API_INFO("mediaPlayerId: %d\n", mediaPlayerId);

   if ( mediaPlayerId >= MAX_PLAYER_HANDLES )
   {
      API_WARN("ERROR: mediaPlayerId (%d) out of range\n", mediaPlayerId);
      return SailErr_BadParam;
   }

   tPlayerHandle *handle = lockHandle( mediaPlayerId );
  if ( handle == NULL ) return SailErr_UnKnown;


   if ( handle->state & PLAYER_OPEN )
   {
      API_WARN("ERROR: mediaPlayerId (%d) already opened\n", mediaPlayerId);
    //  unlockHandle( handle );
      return SailErr_WrongState;
   }
   API_NOISE("Entered: \n");

   handle->callback = callback;
   handle->mpcdata = (MPCallbackData *) userParam;
   handle->speed = 0.0;

   handle->state |= PLAYER_OPEN;

   API_NOISE("Sending NOTIFY_MEDIAPLAYER_OPEN event\n");
   handle->callback( NOTIFY_MEDIAPLAYER_OPEN, handle->mpcdata );
#if 0
   memset( &handle->langList, 0, sizeof(tAudioLangList) );
   memset( &handle->subLangList, 0, sizeof(tSubtitleLangList) );
   memset( &handle->ttxLangList, 0, sizeof(tDvbTtxLangList) );

   if ( mediaPlayerId == 0 )
   {
      strcpy( handle->sinkUrl, "avout://main" );
   }
   else
   {
      strcpy( handle->sinkUrl, "avout://pip0" );
   }

   handle->state |= PLAYER_OPEN;

   API_NOISE("Sending NOTIFY_MEDIAPLAYER_OPEN event\n");
   handle->callback( NOTIFY_MEDIAPLAYER_OPEN, handle->mpcdata );
#endif
   unlockHandle( handle );

   API_NOISE("Exit: \n");
   return retCode;
}

SailError MediaPlayerClose( int mediaPlayerId )
{
   SailErr retCode = SailErr_None;

   API_INFO("mediaPlayerId: %d\n", mediaPlayerId);

   if ( mediaPlayerId >= MAX_PLAYER_HANDLES )
   {
      API_WARN("ERROR: mediaPlayerId (%d) out of range\n", mediaPlayerId);
      return SailErr_BadParam;
   }

   tPlayerHandle *handle = lockHandle( mediaPlayerId );

   if ( !(handle->state & PLAYER_OPEN) )
   {
      API_WARN("ERROR: mediaPlayerId (%d) not opened\n", mediaPlayerId);
      unlockHandle( handle );
      return SailErr_WrongState;
   }

   MediaPlayerCloseSource( mediaPlayerId );
   handle->state &= ~PLAYER_OPEN;

   API_NOISE("Sending NOTIFY_MEDIAPLAYER_CLOSE event\n");
   handle->callback( NOTIFY_MEDIAPLAYER_CLOSE, handle->mpcdata );

   unlockHandle( handle );

   return retCode;
}

SailError MediaPlayerSetSource( int mediaPlayerId, const char* source )
{
   SailErr retCode = SailErr_None;

   API_INFO("mediaPlayerId: %d source: %s\n", mediaPlayerId, source);


   if ( mediaPlayerId >= MAX_PLAYER_HANDLES )
   {
      API_WARN("ERROR: mediaPlayerId (%d) out of range\n", mediaPlayerId);
      return SailErr_BadParam;
   }

   tPlayerHandle *handle = lockHandle( mediaPlayerId );

   if ( !(handle->state & PLAYER_OPEN) )
   {
      API_WARN("ERROR: mediaPlayerId (%d) not opened\n", mediaPlayerId);
      unlockHandle( handle );
      return SailErr_WrongState;
   }
#if 0

   if ( !(strcmp(handle->source, source) && (handle->state & PLAYER_SESSION) )
   {
      API_WARN("Player(%d) source (%s) already set\n", mediaPlayerId, source);
      unlockHandle( handle );
      return retCode;
   }
   handle->srcType = SOURCE_UNKNOWN;

   if ( handle->session != NULL )
   {
      sessionStop( handle->session );
      sessionClose( handle->session );
      handle->session = NULL;
   }

   memset( &handle->langList, 0, sizeof(tAudioLangList) );
   memset( &handle->subLangList, 0, sizeof(tSubtitleLangList) );
   memset( &handle->ttxLangList, 0, sizeof(tDvbTtxLangList) );
   memset( &handle->subtitle, 0, sizeof(handle->subtitle) );

   handle->state &= ~(PLAYER_SESSION | PLAYER_STARTED | PLAYER_ACTIVE | PLAYER_SOURCE | PLAYER_DVR_PLAYBACK);

   stopTimeshift( handle );

   strcpy( handle->source, source );
   strcpy( handle->srcUrl, source );
   handle->srcId = 0;

   do
   {
      char *next;
      (void)osTokenGet( handle->srcUrl, "tuner://dvb?sid=", &next );

      if ( next != NULL )
      {
#ifdef SAIL_PKEYDVB_SUPPORT
         handle->srcId = (ui32)atoi(next);

         tDvbTuningParams tuning;
         if ( SAIL_SIManager_GetTuningInfo(handle->srcId, &tuning) == SailErr_None )
         {
            snprintf( handle->srcUrl, sizeof(handle->srcUrl), 
                      "tuner://dvb?freq=%u&qam=%u&sym=%u&bw=%u&annex=%c&sid=%u",
                      (tuning.freq * 1000),
                      tuning.qamtype,
                      (tuning.symbolrate * 1000),
                      handle->bandwidth,
                      handle->annex,
                      handle->srcId );
         }
         handle->srcType = SOURCE_TUNER;
#else
         retCode = SailErr_NotSupported;
         break;
#endif
      }

      retCode = sessionOpen( &handle->session, handle->srcUrl, handle->sinkUrl );
      WARNONERROR( retCode );
      if ( retCode != SailErr_None ) break;

      handle->state |= (PLAYER_SOURCE | PLAYER_SESSION);

#ifndef SAIL_BCMCISCO_SHMEM_SUPPORT
      if ( !(handle->state & PLAYER_ENABLED) )
      {
         SessionData data = {SESSION_CMD_SET, SESSION_MUTE};
         data.modopt.session.mute.audio = true;
         data.modopt.session.mute.video = true;
         sessionControl( handle->session, &data );
      }
#endif      

      retCode = sessionStart( handle->session, 0 );
      WARNONERROR( retCode );
      if ( retCode != SailErr_None ) break;

      handle->state |= PLAYER_STARTED;

      API_NOISE("Sending NOTIFY_MEDIAPLAYER_SOURCE_SET event\n");
      handle->callback( NOTIFY_MEDIAPLAYER_SOURCE_SET, handle->mpcdata );

      MediaPlayerSetCopyProtection( mediaPlayerId, 3 );

      if ( handle->srcType == SOURCE_UNKNOWN )
      {
         handle->srcType = getSrcType( handle->srcUrl );
         switch ( handle->srcType )
         {
            case SOURCE_DVR:
            case SOURCE_FILE:
            case SOURCE_HTTP:
            case SOURCE_DLNA:
               handle->state |= PLAYER_DVR_PLAYBACK;
               break;

            default:
               break;
         }
      }

      SystemNotify( EV_CA_UPDATE, 0, 0, 0 );

   } while ( 0 );

#endif
   unlockHandle( handle );

   return retCode;
}

SailError MediaPlayerGetSource( int mediaPlayerId, char* source )
{
   SailErr retCode = SailErr_None;

   if ( source == NULL )
   {
      API_WARN("ERROR: mediaPlayerId (%d) source == NULL\n", mediaPlayerId);
      return SailErr_BadParam;
   }

   if ( mediaPlayerId >= MAX_PLAYER_HANDLES )
   {
      API_WARN("ERROR: mediaPlayerId (%d) out of range\n", mediaPlayerId);
      return SailErr_BadParam;
   }

   API_INFO("mediaPlayerId: %d\n", mediaPlayerId);

   tPlayerHandle *handle = lockHandle( mediaPlayerId );
#if 0

   do
   {
      if ( !(handle->state & PLAYER_OPEN) )
      {
         API_WARN("ERROR: mediaPlayerId (%d) not opened\n", mediaPlayerId);
         retCode = SailErr_WrongState;
         break;
      }

      if ( !(handle->state & PLAYER_SOURCE) )
      {
         API_WARN("ERROR: mediaPlayerId (%d) source not set\n", mediaPlayerId);
         retCode = SailErr_WrongState;
         break;
      }

      if ( handle->srcId != 0 )
      {
         strcpy( source, handle->source );
      }
      else
      {
         strcpy( source, DEFAULT_SRC_URL );
      }

   } while ( 0 );

   if ( retCode != SailErr_None )
   {
      strcpy( source, DEFAULT_SRC_URL );
   }

   API_NOISE("source: %s\n", source);

#endif
   unlockHandle( handle );

   return retCode;
}

SailError MediaPlayerCloseSource( int mediaPlayerId )
{
   SailErr retCode = SailErr_None;

   API_INFO("mediaPlayerId: %d\n", mediaPlayerId);

   if ( mediaPlayerId >= MAX_PLAYER_HANDLES )
   {
      API_WARN("ERROR: mediaPlayerId (%d) out of range\n", mediaPlayerId);
      return SailErr_BadParam;
   }

   tPlayerHandle *handle = lockHandle( mediaPlayerId );

#if 0
   memset( &handle->subtitle, 0, sizeof(handle->subtitle) );

   handle->srcId = 0;
   if ( !(handle->state & PLAYER_OPEN) )
   {
      API_WARN("ERROR: mediaPlayerId (%d) not opened\n", mediaPlayerId);
      unlockHandle( handle );
      return SailErr_WrongState;
   }

   if ( handle->session != NULL )
   {
      retCode = sessionStop( handle->session );
      WARNONERROR( retCode );

      retCode = sessionClose( handle->session );
      WARNONERROR( retCode );
      handle->session = NULL;
   }

   handle->state &= ~(PLAYER_ENABLED | PLAYER_SOURCE | PLAYER_SESSION | PLAYER_STARTED | PLAYER_ACTIVE | PLAYER_DVR_PLAYBACK);

   stopTimeshift( handle );

#endif
   unlockHandle( handle );

   return retCode;
}

SailError MediaPlayerSetSink( int mediaPlayerId, const char* sink )
{
   SailErr retCode = SailErr_None;

   API_INFO("mediaPlayerId: %d sink: %s\n", mediaPlayerId, sink);

   if ( (sink == NULL) || (strlen(sink) < MIN_URL_LEN) )
   {
      API_WARN("ERROR: mediaPlayerId (%d) invalid sink\n", mediaPlayerId);
      return SailErr_BadParam;
   }

   if ( mediaPlayerId >= MAX_PLAYER_HANDLES )
   {
      API_WARN("ERROR: mediaPlayerId (%d) out of range\n", mediaPlayerId);
      return SailErr_BadParam;
   }

   tPlayerHandle *handle = lockHandle( mediaPlayerId );

   do 
   {
      if ( !(handle->state & PLAYER_OPEN) )
      {
         API_WARN("ERROR: mediaPlayerId (%d) not opened\n", mediaPlayerId);
         retCode = SailErr_WrongState;
         break;
      }

      if ( handle->state & PLAYER_SESSION )
      {
         API_WARN("ERROR: mediaPlayerId (%d) has a session open already - "
                   "sink cannot be set after a session is opened \n", 
                  mediaPlayerId);
         retCode = SailErr_WrongState;
         break;
      }

      strcpy( handle->sinkUrl, sink );
   } while ( 0 );
   
   unlockHandle( handle );

   return retCode;
}

SailError MediaPlayerGetSink( int mediaPlayerId, char* sink )
{
   SailErr retCode = SailErr_None;

   if ( sink == NULL )
   {
      API_WARN("ERROR: mediaPlayerId (%d) sink == NULL\n", mediaPlayerId);
      return SailErr_BadParam;
   }

   if ( mediaPlayerId >= MAX_PLAYER_HANDLES )
   {
      API_WARN("ERROR: mediaPlayerId (%d) out of range\n", mediaPlayerId);
      return SailErr_BadParam;
   }

   API_INFO("mediaPlayerId: %d\n", mediaPlayerId);

   tPlayerHandle *handle = lockHandle( mediaPlayerId );

   do
   {
      if ( !(handle->state & PLAYER_OPEN) )
      {
         API_WARN("ERROR: mediaPlayerId (%d) not opened\n", mediaPlayerId);
         retCode = SailErr_WrongState;
         break;
      }

      strcpy( sink, handle->sinkUrl );

   } while ( 0 );

   unlockHandle( handle );

   return retCode;
}

SailError MediaPlayerSetPlaySpeed( int mediaPlayerId, float speed )
{
   API_INFO("mediaPlayerId: %d speed: %f\n", mediaPlayerId, speed);

   SailErr retCode = SailErr_None;

   if ( mediaPlayerId >= MAX_PLAYER_HANDLES )
   {
      API_WARN("ERROR: mediaPlayerId (%d) out of range\n", mediaPlayerId);
      return SailErr_BadParam;
   }

   tPlayerHandle *handle = lockHandle( mediaPlayerId );

   handle->speed = speed;
#if 0
   if ( handle->state & PLAYER_DVR_PLAYBACK )
   {
      SessionData data = {SESSION_CMD_SET, SESSION_PLAY};
      data.modopt.session.play.mode = speed;
      data.modopt.session.play.position = SESSION_POS_INVALID;
      retCode = sessionControl( handle->session, &data );
      WARNONERROR( retCode );
   }
   else if ( handle->state & PLAYER_TSB_OPEN )
   {
      if ( speed != 0.0 )
      {
         sessionStart( handle->session, 0 );
         handle->state |= PLAYER_DVR_PLAYBACK;
         handle->subtitle.enabled = false;
         SessionData data = {SESSION_CMD_SET, SESSION_PLAY};
         data.modopt.session.play.mode = speed;
         data.modopt.session.play.position = SESSION_POS_INVALID;
         retCode = sessionControl( handle->session, &data );
         WARNONERROR( retCode );
      }
   }
   else if ( handle->state & PLAYER_TSB_ACTIVE )
   {
      if ( !(handle->state & PLAYER_TSB_OPEN) && (speed <= 0.0) )
      {
         SessionData data = {SESSION_CMD_SET, SESSION_HOLD_LAST_FRAME};
         data.modopt.session.holdLastFrame = true;
         retCode = sessionControl( handle->session, &data );
         WARNONERROR( retCode );

         //
         // Pause the live video.
         //
         data.actionId = SESSION_PAUSE;
         data.modopt.session.pause = true;
         retCode = sessionControl( handle->session, &data );
         WARNONERROR( retCode );

         sessionStop( handle->session );
         sessionClose( handle->session );

         retCode = sessionOpen( &handle->session, handle->tsbUrl, handle->sinkUrl );
         WARNONERROR( retCode );
         handle->state |= PLAYER_TSB_OPEN;

         retCode = sessionStart( handle->session, 0 );
         WARNONERROR( retCode );
         handle->state |= PLAYER_DVR_PLAYBACK;
         handle->subtitle.enabled = false;

         data.action = SESSION_CMD_SET;
         data.actionId = DVR_SEEK;
         data.modopt.dvr.seekPos = (DVR_POSITION_INVALID - 1);
         retCode = sessionControl( handle->session, &data );
         WARNONERROR( retCode );
      }
      else
      {
         retCode = SAIL_ERROR_WRONG_STATE;
      }

      if ( (handle->state & PLAYER_TSB_OPEN) && (speed <= 0.0) )
      {
         SessionData data = {SESSION_CMD_SET, DVR_PLAY};
         data.modopt.dvr.play.mode = speed;
         data.modopt.dvr.play.position = DVR_POSITION_INVALID;
         retCode = sessionControl( handle->session, &data );
         WARNONERROR( retCode );
      }
   }
   else
   {
      retCode = SAIL_ERROR_WRONG_STATE;
      API_WARN("ERROR: wrong state (%04X)\n", handle->state);
   }

   //
   // Start subtitle callbacks if enabled.
   // 
   if ( (retCode == SailErr_None) && (speed == 1.0) && 
        (handle->subtitle.enabled == false) && (handle->subtitle.cb.drawCb != NULL) )
   {
      setSubtitleEnabled( handle, true, handle->subtitle.cb.drawCb, handle->subtitle.cb.clearCb );
      MediaPlayerSelectSubtitleStream( mediaPlayerId, handle->subtitle.index );
   }
#endif
   unlockHandle( handle );

   return retCode;
}

SailError MediaPlayerGetPlaySpeed( int mediaPlayerId, float *speed )
{
   SailErr retCode = SailErr_None;

   if ( speed == NULL ) return SailErr_BadParam;

   if ( mediaPlayerId >= MAX_PLAYER_HANDLES )
   {
      API_WARN("ERROR: mediaPlayerId (%d) out of range\n", mediaPlayerId);
      return SailErr_BadParam;
   }

   tPlayerHandle *handle = lockHandle( mediaPlayerId );
#if 0

   if ( !(handle->state & PLAYER_DVR_PLAYBACK) )
   {
      retCode = SailErr_WrongState;
   }
   else
   {
      *speed = handle->speed;
   }
#endif
   unlockHandle( handle );

   return retCode;
}

SailError MediaPlayerGetSupportedSpeeds( int mediaPlayerId, float *speeds[] )
{
   SailErr retCode = SailErr_None;

   API_INFO("mediaPlayerId: %d\n", mediaPlayerId);

   *speeds = gDvrSpeeds;
   return retCode;
}

SailError MediaPlayerSeek( int mediaPlayerId, int64_t offset, int whence )
{
   SailErr retCode = SailErr_None;

   API_INFO("mediaPlayerId: %d offset: %lld whence: %d\n", mediaPlayerId, offset, whence);

   if ( mediaPlayerId >= MAX_PLAYER_HANDLES )
   {
      API_WARN("ERROR: mediaPlayerId (%d) out of range\n", mediaPlayerId);
      return SailErr_BadParam;
   }

   tPlayerHandle *handle = lockHandle( mediaPlayerId );
#if 0

   if ( !(handle->state & (PLAYER_DVR_PLAYBACK | PLAYER_TSB_OPEN)) )
   {
      API_WARN("ERROR: mediaPlayerId (%d) is not a DVR session\n", mediaPlayerId);
      unlockHandle( handle );
      return SailErr_WrongState;
   }

   //
   // Special condition to exit TSB.
   // 
   if ( (whence == SEEK_END) && (handle->state & PLAYER_TSB_OPEN) )
   {
      handle->state &= ~(PLAYER_DVR_PLAYBACK | PLAYER_TSB_OPEN);

      //
      // Reset subtitle flag so it can be
      // restarted once live video starts.
      // 
      handle->subtitle.enabled = false;

      if ( handle->session != NULL )
      {
         SessionData data = {SESSION_CMD_SET, SESSION_HOLD_LAST_FRAME};
         data.modopt.session.holdLastFrame = true;
         retCode = sessionControl( handle->session, &data );
         WARNONERROR( retCode );

         sessionStop( handle->session );
         sessionClose( handle->session );

         retCode = sessionOpen( &handle->session, handle->srcUrl, handle->sinkUrl );
         WARNONERROR( retCode );
         retCode = sessionStart( handle->session, 0 );
         WARNONERROR( retCode );
      }
   }
   else
   {
      SessionData data = {SESSION_CMD_GET, SESSION_POSITION};
      retCode = sessionControl( handle->session, &data );
      WARNONERROR( retCode );
      if ( retCode == SailErr_None )
      {
         ui64 begin = 0;
         ui64 end   = (data.modopt.session.pos.end - data.modopt.session.pos.start) / 90ll);
         ui64 pos   = (data.modopt.session.pos.curr - data.modopt.session.pos.start) / 90ll);

         data.action    = SESSION_CMD_SET;
         data.actionId  = SESSION_SEEK;
         i64 seekPos = 0ll;

         switch ( whence )
         {
            case SEEK_SET:
               seekPos = begin + offset;
               break;

            case SEEK_CUR:
               seekPos = pos + offset;
               break;

            case SEEK_END:
               seekPos = end - offset;
               break;

            default:
               seekPos = offset;
               break;
         }

         if ( seekPos < 0 )
         {
            seekPos = 0;
         }
         else if ( seekPos > end )
         {
            seekPos = end;
         }

         //
         // Convert seekPos back to PTS units.
         // 
         seekPos *= 90ll;

         //
         // Add the actual PTS starting offset.
         // 
         seekPos += data.modopt.session.pos.start;

         API_NOISE("start: %llu curr: %llu end: %llu seekPos: %llu\n",
                  (data.modopt.session.pos.start / 90000ll), 
                  (data.modopt.session.pos.curr / 90000ll), 
                  (data.modopt.session.pos.end / 90000ll),
                  (seekPos / 90000ll);

         data.modopt.session.seekPos = seekPos;
         retCode = sessionControl( handle->session, &data );
         WARNONERROR(retCode);
      }
   }
#endif
   unlockHandle( handle );

   return retCode;
}

SailError MediaPlayerPlaybackInfo( int mediaPlayerId, uint64_t* pActualPlayPosition,
                                   uint64_t* pSizeInMS, uint64_t* pSizeInByte,
                                   bool* pIsActiveRecording )
{
   SailErr retCode = SailErr_None;

   if ( (pActualPlayPosition == NULL) || (pSizeInMS == NULL) || (pSizeInByte == NULL) || (pIsActiveRecording == NULL) )
   {
      API_WARN("ERROR: Bad parameter for mediaPlayerId (%d)\n", mediaPlayerId);
      return SailErr_BadParam;
   }

   if ( mediaPlayerId >= MAX_PLAYER_HANDLES )
   {
      API_WARN("ERROR: mediaPlayerId (%d) out of range\n", mediaPlayerId);
      return SailErr_BadParam;
   }

   tPlayerHandle *handle = lockHandle( mediaPlayerId );
#if 0

   if ( handle->state & (PLAYER_DVR_PLAYBACK | PLAYER_TSB_OPEN) )
   {
      if ( (handle->srcType == SOURCE_DVR) || (handle->state & PLAYER_TSB_OPEN) )
      {
         SessionData data = {SESSION_CMD_GET, DVR_INFO};
         retCode = sessionControl( handle->session, &data );
         if ( retCode == SailErr_None )
         {
            *pActualPlayPosition = (data.modopt.dvr.info.pos.curr - data.modopt.dvr.info.pos.start) / 90ll);
            *pSizeInMS = (data.modopt.dvr.info.pos.end - data.modopt.dvr.info.pos.start) / 90ll);
            *pSizeInByte = data.modopt.dvr.info.size;
            *pIsActiveRecording = data.modopt.dvr.info.activeRecording;

            API_NOISE("pos: %llu sizeMs: %llu sizeBytes: %llu isRecording: %d\n", *pActualPlayPosition, *pSizeInMS, *pSizeInByte, (int)*pIsActiveRecording);
         }
      }
      else
      {
         SessionData data = {SESSION_CMD_GET, SESSION_POSITION};
         retCode = sessionControl( handle->session, &data );
         if ( retCode == SailErr_None )
         {
            *pActualPlayPosition = (data.modopt.session.pos.curr - data.modopt.session.pos.start) / 90ll);
            *pSizeInMS = (data.modopt.session.pos.end - data.modopt.session.pos.start) / 90ll);
            *pSizeInByte = 0;
            *pIsActiveRecording = false;

            API_NOISE("pos: %llu sizeMs: %llu sizeBytes: %llu isRecording: %d\n", *pActualPlayPosition, *pSizeInMS, *pSizeInByte, (int)*pIsActiveRecording);
         }
      }
   }
   else
   {
      API_WARN("ERROR: mediaPlayerId (%d) is not DVR playback\n", mediaPlayerId);
      retCode = SailErr_WrongState;
   }
#endif
   unlockHandle( handle );

   return retCode;
}

SailError MediaPlayerSetEnabled( int mediaPlayerId, bool enabled )
{
   SailErr retCode = SailErr_None;

   API_INFO("mediaPlayerId: %d enabled: %s\n", mediaPlayerId, (enabled == true)?"true":"false");

   if ( mediaPlayerId >= MAX_PLAYER_HANDLES )
   {
      API_WARN("ERROR: mediaPlayerId (%d) out of range\n", mediaPlayerId);
      return SailErr_BadParam;
   }

   tPlayerHandle *handle = lockHandle( mediaPlayerId );

   if ( !(handle->state & PLAYER_OPEN) )
   {
      API_WARN("ERROR: mediaPlayerId (%d) not opened\n", mediaPlayerId);
      unlockHandle( handle );
      return SailErr_WrongState;
   }
      handle->state |= PLAYER_ENABLED;

#if 0
   SessionData data = {SESSION_CMD_SET, SESSION_MUTE};
   if ( enabled )
   {
      if ( handle->state & PLAYER_SOURCE )
      {
         if ( !(handle->state & PLAYER_SESSION) )
         {
            retCode = sessionOpen( &handle->session, handle->srcUrl, handle->sinkUrl );
            WARNONERROR( retCode );
            handle->state |= PLAYER_SESSION;
         }

         if ( !(handle->state & PLAYER_STARTED) )
         {
            if ( handle->session != NULL )
            {
               retCode = sessionStart( handle->session, 0 );
               WARNONERROR( retCode );
               handle->state |= PLAYER_STARTED;
            }
         }

         if ( !(handle->state & PLAYER_ENABLED) )
         {
            data.modopt.session.mute.video = false;
            data.modopt.session.mute.audio = handle->isMuted;
            retCode = sessionControl( handle->session, &data );
            WARNONERROR( retCode );
         }
      }

      handle->state |= PLAYER_ENABLED;
   }
   else
   {
      if ( handle->state & PLAYER_SESSION )
      {
         if ( handle->state & PLAYER_ENABLED )
         {
            data.modopt.session.mute.audio = true;
            data.modopt.session.mute.video = true;
            retCode = sessionControl( handle->session, &data );
            WARNONERROR( retCode );
         }
      }
      handle->state &= ~PLAYER_ENABLED;
   }
#endif
   unlockHandle( handle );

   return retCode;
}

SailError MediaPlayerIsEnabled( int mediaPlayerId, bool* enabled )
{
   SailErr retCode = SailErr_None;

   if ( (mediaPlayerId >= MAX_PLAYER_HANDLES) || (enabled == NULL) )
   {
      API_WARN("ERROR: mediaPlayerId (%d) out of range\n", mediaPlayerId);
      return SailErr_BadParam;
   }

   tPlayerHandle *handle = lockHandle( mediaPlayerId );
#if 0

   do
   {
      if ( !(handle->state & PLAYER_OPEN) )
      {
         API_WARN("ERROR: mediaPlayerId (%d) not opened\n", mediaPlayerId);
         retCode = SailErr_WrongState;
         break;
      }

      *enabled = (handle->state & PLAYER_ENABLED)?true:false;

   } while ( 0 );
#endif
   unlockHandle( handle );

   return retCode;
}


SailError MediaPlayerVideoScale( int mediaPlayerId, Rectangle* srcRect, Rectangle* destRect )
{
   SailErr retCode = SailErr_None;

   tPlayerHandle *handle = lockHandle( mediaPlayerId );

#if 0
   if ( !(handle->state & PLAYER_STARTED) )
   {
      API_WARN("ERROR: mediaPlayerId (%d) not active\n", mediaPlayerId);
      unlockHandle( handle );
      return SailErr_WrongState;
   }

   if ( srcRect != NULL )
   {
      API_NOISE("srcRect x: %d y: %d w: %d h: %d\n", srcRect->x, srcRect->y, srcRect->width, srcRect->height);
   }

   API_NOISE("destRect x: %d y: %d w: %d h: %d\n", destRect->x, destRect->y, destRect->width, destRect->height);

   //
   // Zappware passes a rectangle based on the directfb screen size,
   // so we need to scale the rectangle to SAILNG_WINDOW_WIDTH X SAILNG_WINDOW_HEIGHT
   //    
#ifdef SAIL_DIRECTFB_SUPPORT
   IDirectFB *dfb = SailDfbGet();
   if ( dfb != NULL )
   {
      IDirectFBScreen* screen = 0;
      dfb->GetScreen( dfb, DSCID_PRIMARY, &screen );
      screen->GetSize( screen, &screen_width, &screen_height );
   }
#endif

   if ( destRect->width > screen_width )
   {
      destRect->width = screen_width;
   }
   if ( destRect->height > screen_height )
   {
      destRect->height = screen_height;
   }

   fp32 xres = getXRes( clipRect.width );
   fp32 yres = getYRes( clipRect.height );

   multi = (fp32)(fp32)SAILNG_WINDOW_WIDTH / (fp32)screen_width);
   destRect->x = (short)(fp32)destRect->x * multi);
   destRect->width = (short)(fp32)destRect->width * multi);
   if ( (clipRect.x != 0) && (clipRect.width > destRect->width) )
   {
      if ( clipRect.width > SAILNG_WINDOW_WIDTH )
      {
         multi = (fp32)(fp32)SAILNG_WINDOW_WIDTH / xres);
      }

      clipRect.x = (short)(fp32)clipRect.x * multi);
      clipRect.width = (short)(fp32)clipRect.width * multi);
   }
   else if ( clipRect.x == 0 )
   {
      clipRect.width = SAILNG_WINDOW_WIDTH;
   }

   multi = (fp32)(fp32)SAILNG_WINDOW_HEIGHT / (fp32)screen_height);
   destRect->y = (short)(fp32)destRect->y * multi);
   destRect->height = (short)(fp32)destRect->height * multi);
   if ( (clipRect.y != 0) && (clipRect.height > destRect->height) )
   {
      if ( clipRect.height > SAILNG_WINDOW_HEIGHT )
      {
         multi = (fp32)(fp32)SAILNG_WINDOW_HEIGHT / yres);
      }
      clipRect.y = (short)(fp32)clipRect.y * multi);
      clipRect.height = (short)(fp32)clipRect.height * multi);
   }
   else if ( clipRect.y == 0 )
   {
      clipRect.height = SAILNG_WINDOW_HEIGHT;
   }

   //
   // When scaling SD video and source is not clipped, clip a few lines to avoid artifacts.
   // 
   if ( (destRect->y != 0) && (clipRect.y == 0) && (ui32)yres == CLIENT_WINDOW_HEIGHT) )
   {
      clipRect.y = 60; //60 in SAILNG_WINDOW_HEIGHT coordinates = 4 lines in PAL coordinates, 
      clipRect.height = (SAILNG_WINDOW_HEIGHT - (clipRect.y * 2);
   }

   if ( (clipRect.x != 0) || (clipRect.y != 0) )
   {
      data.modopt.avout.videoCfg.windowPos.srcRect.x = clipRect.x;
      data.modopt.avout.videoCfg.windowPos.srcRect.y = clipRect.y;
      data.modopt.avout.videoCfg.windowPos.srcRect.width = clipRect.width;
      data.modopt.avout.videoCfg.windowPos.srcRect.height = clipRect.height;
   }
   else
   {
      data.modopt.avout.videoCfg.windowPos.srcRect.x = 
      data.modopt.avout.videoCfg.windowPos.srcRect.y =
      data.modopt.avout.videoCfg.windowPos.srcRect.width =
      data.modopt.avout.videoCfg.windowPos.srcRect.height = 0;
   }

   //
   // Scale and position the window to coordinates relative to current SD display
   // 
   data.action = SESSION_CMD_SET;
   data.actionId = AVOUT_SCALE_WINDOW;
   data.modopt.avout.videoCfg.videoOutPortSelection = SD_VIDEO_PORTS;
   data.modopt.avout.videoCfg.windowPos.destRect.x = destRect->x;
   data.modopt.avout.videoCfg.windowPos.destRect.y = destRect->y;
   data.modopt.avout.videoCfg.windowPos.destRect.width = destRect->width;
   data.modopt.avout.videoCfg.windowPos.destRect.height = destRect->height;

   retCode = sessionControl( handle->session, &data );
   WARNONERROR( retCode );

   //
   // Scale and position the window to coordinates relative to current HD display
   //
   data.modopt.avout.videoCfg.videoOutPortSelection = HD_VIDEO_PORTS;
   retCode = sessionControl( handle->session, &data );
   WARNONERROR( retCode );

   handle->destRect = *destRect;

#endif
   unlockHandle( handle );

   return retCode;
}

SailError MediaPlayerVideoFullScale( int mediaPlayerId, Rectangle* srcRect )
{
   Rectangle destRect = {0, 0, MAX_CLIENT_WIDTH, MAX_CLIENT_HEIGHT};

   return MediaPlayerVideoScale( mediaPlayerId, srcRect, &destRect );
}

SailError MediaPlayerGetNumberOfAudioStreams( int mediaPlayerId, int* pCount )
{
   SailErr retCode = SailErr_None;

   if ( (mediaPlayerId >= MAX_PLAYER_HANDLES) || (pCount == NULL) )
   {
      API_WARN("ERROR: mediaPlayerId (%d) out of range\n", mediaPlayerId);
      return SailErr_BadParam;
   }

   tPlayerHandle *handle = lockHandle( mediaPlayerId );

#if 0
   do
   {
      if ( !(handle->state & PLAYER_STARTED) )
      {
         API_WARN("ERROR: mediaPlayerId (%d) not active\n", mediaPlayerId);
         retCode = SailErr_WrongState;
         break;
      }

      SessionData data = {SESSION_CMD_GET, TRANSPORT_AUDIO_LANG_LIST};
      retCode = sessionControl( handle->session, &data );
      if ( retCode == SailErr_None )
      {
         memcpy( &handle->langList, &data.modopt.transport.audioLangList, sizeof(tAudioLangList) );
         *pCount = handle->langList.count;
      }
      else
      {
         *pCount = 0;
      }

   } while ( 0 );

   API_NOISE("mediaPlayerId: %d count: %d\n", mediaPlayerId, (int)*pCount);

#endif
   unlockHandle( handle );

   return retCode;
}

SailError MediaPlayerGetAudioStreamInfo( int mediaPlayerId, int audioIndex,
                                         char** ppAudioLanguage, int* pAudioType )
{
   SailErr retCode = SailErr_None;

   if ( (mediaPlayerId >= MAX_PLAYER_HANDLES) || (ppAudioLanguage == NULL) || (pAudioType == NULL) )
   {
      API_WARN("ERROR: mediaPlayerId (%d) out of range\n", mediaPlayerId);
      return SailErr_BadParam;
   }

   tPlayerHandle *handle = lockHandle( mediaPlayerId );

#if 0
   do
   {
      if ( !(handle->state & PLAYER_STARTED) )
      {
         API_WARN("ERROR: mediaPlayerId (%d) not active\n", mediaPlayerId);
         retCode = SailErr_WrongState;
         break;
      }

      if ( handle->langList.count == 0 )
      {
         int cnt = 0;
         MediaPlayerGetNumberOfAudioStreams( mediaPlayerId, &cnt );
      }

      if ( audioIndex >= handle->langList.count )
      {
         API_WARN("ERROR: audioIndex (%d) audio count (%d)\n", audioIndex, handle->langList.count);
         retCode = SailErr_BadParam;
         break;
      }

      *ppAudioLanguage = (char *)handle->langList.item[audioIndex].isoCode;

      if ( (handle->langList.item[audioIndex].streamType == 0x6a) || // AC-3
           (handle->langList.item[audioIndex].streamType == 0x7a) )  // Enhanced AC-3
      {
         //
         // Switch to AC-3 type
         // 
         *pAudioType = 0x81;
      }
      else
      {
         *pAudioType = (int)handle->langList.item[audioIndex].streamType;
      }

   } while ( 0 );

   API_NOISE("audioIndex: %d lang: %s type: %d\n", audioIndex, *ppAudioLanguage, *pAudioType);

#endif
   unlockHandle( handle );

   return retCode;
}

SailError MediaPlayerGetSelectedAudioStream( int mediaPlayerId, int* pAudioIndex )
{
   SailErr retCode = SailErr_None;

   if ( (mediaPlayerId >= MAX_PLAYER_HANDLES) || (pAudioIndex == NULL) )
   {
      API_WARN("ERROR: mediaPlayerId (%d) pAudioIndex (%p)\n", mediaPlayerId, pAudioIndex);
      return SailErr_BadParam;
   }

   tPlayerHandle *handle = lockHandle( mediaPlayerId );

#if 0
   do
   {
      if ( !(handle->state & PLAYER_STARTED) )
      {
         API_WARN("ERROR: mediaPlayerId (%d) not active\n", mediaPlayerId);
         retCode = SailErr_WrongState;
         break;
      }

      SessionData data = {SESSION_CMD_GET, TRANSPORT_ACTIVE_AUDIO_INDEX};
      retCode = sessionControl( handle->session, &data );
      WARNONERROR( retCode );
      if ( retCode == SailErr_None )
      {
         *pAudioIndex = data.modopt.transport.activeAudioIndex;
      }

   } while ( 0 );

   API_NOISE("mediaPlayerId: %d index: %d\n", mediaPlayerId, *pAudioIndex);

#endif
   unlockHandle( handle );

   return retCode;
}

SailError MediaPlayerSelectAudioStream( int mediaPlayerId, int audioIndex )
{
   SailErr retCode = SailErr_None;

   if ( mediaPlayerId >= MAX_PLAYER_HANDLES )
   {
      API_WARN("ERROR: mediaPlayerId (%d) out of range\n", mediaPlayerId);
      return SailErr_BadParam;
   }

   API_NOISE("mediaPlayerId: %d audioIndex: %d\n", mediaPlayerId, audioIndex);

   tPlayerHandle *handle = lockHandle( mediaPlayerId );

#if 0
   do
   {
      if ( !(handle->state & PLAYER_STARTED) )
      {
         API_WARN("ERROR: mediaPlayerId (%d) not active\n", mediaPlayerId);
         retCode = SailErr_WrongState;
         break;
      }

      if ( audioIndex >= handle->langList.count )
      {
         API_WARN("ERROR: mediaPlayerId (%d) audioIndex: %d stream count: %d\n", 
                  mediaPlayerId, audioIndex, handle->langList.count);
         retCode = SailErr_BadParam;
         break;
      }

      SessionData data = {SESSION_CMD_SET, TRANSPORT_ACTIVE_AUDIO_INDEX};
      data.modopt.transport.activeAudioIndex = audioIndex;
      retCode = sessionControl( handle->session, &data );
      WARNONERROR( retCode );

   } while ( 0 );

#endif
   unlockHandle( handle );

   return retCode;
}

SailError MediaPlayerAudioMute( bool enable )
{
   SailErr retCode = SailErr_None;

   API_NOISE("enable: %d\n", (int)enable);

   tPlayerHandle *handle = lockHandle( 0 );

   handle->isMuted = enable;

#if 0
   if ( handle->state & PLAYER_ENABLED )
   {
      SessionData data = {SESSION_CMD_SET, AVOUT_AUDIO_MUTE};
      data.modopt.avout.audioCfg.mute = enable;
      retCode = sessionControl( handle->session, &data );
      WARNONERROR( retCode );
   }

#endif
   unlockHandle( handle );

   return retCode;
}

SailError MediaPlayerGetVolume( int* left, int* right )
{
   SailErr retCode; 
   if ( (left == NULL) || (right == NULL) ) return SAIL_ERROR_BAD_PARAM;

   tPlayerHandle *handle = lockHandle( 0 );

#if 0
   SessionData data = {SESSION_CMD_GET, AVOUT_AUDIO_VOLUME_LEVEL};
   SailErr retCode = sessionControl( handle->session, &data );
   WARNONERROR( retCode );
   if ( retCode == SailErr_None )
   {
      *right = data.modopt.avout.audioCfg.volRight;
      *left = data.modopt.avout.audioCfg.volLeft;

      API_NOISE("left (%d) right (%d)\n", *left, *right); 
   }

#endif
   unlockHandle( handle );

   return retCode;
}

SailError MediaPlayerSetVolume( int left, int right )
{
   SailErr retCode;
   API_NOISE("left (%d) right (%d)\n", left, right);

   tPlayerHandle *handle = lockHandle( 0 );

   if ( left > MAX_VOLUME ) left = MAX_VOLUME;
   else if ( left < 0 ) left = 0;

   if ( right > MAX_VOLUME ) right = MAX_VOLUME;
   else if ( right < 0 ) right = 0;

#if 0
   SessionData data = {SESSION_CMD_SET, AVOUT_AUDIO_VOLUME_LEVEL};
   data.modopt.avout.audioCfg.volLeft = left;
   data.modopt.avout.audioCfg.volRight = right;
   SailErr retCode = sessionControl( handle->session, &data );
   WARNONERROR( retCode );

#endif
   unlockHandle( handle );

   return retCode;
}

SailError MediaPlayerIsAudioMuted( bool* pIsAudioMuted )
{
   SailErr retCode = SailErr_None;

   if ( pIsAudioMuted == NULL )
   {
      API_WARN("ERROR: pIsAudioMuted == NULL)\n");
      return SailErr_BadParam;
   }

   tPlayerHandle *handle = lockHandle( 0 );

//rms    *pIsAudioMuted = handle->isMuted;

   unlockHandle( handle );

   return retCode;
}

SailError MediaPlayerSetSubtitleEnabled( int mediaPlayerId, bool enable,
                                         drawSubtitleCallback drawCb, clearSubtitleCallback clearCb )
{
   SailErr retCode = SailErr_None;

   if ( mediaPlayerId >= MAX_PLAYER_HANDLES )
   {
      API_WARN("ERROR: mediaPlayerId (%d)\n", mediaPlayerId);
      return SailErr_BadParam;
   }

   API_NOISE("mediaPlayerId: %d enable: %d drawCb: %08X clearCb: %08X\n", 
             mediaPlayerId, (int)enable, (int)drawCb, (int)clearCb);

   tPlayerHandle *handle = lockHandle( mediaPlayerId );

#if 0
   do
   {
      if ( !(handle->state & PLAYER_STARTED) )
      {
         API_WARN("ERROR: mediaPlayerId (%d) not active\n", mediaPlayerId);
         retCode = SailErr_WrongState;
         break;
      }

      handle->subtitle.cb.context = mediaPlayerId;

      if ( enable == true )
      {
         handle->subtitle.cb.drawCb = (tDrawSubtitleCb)drawCb;
         handle->subtitle.cb.clearCb = clearCb;
      }
      else
      {
         handle->subtitle.cb.drawCb = NULL;
         handle->subtitle.cb.clearCb = NULL;
      }

      retCode = setSubtitleEnabled( handle, enable, (tDrawSubtitleCb)drawCb, clearCb );

   } while ( 0 );

#endif
   unlockHandle( handle );

   return retCode;
}

SailError MediaPlayerIsSubtitleEnabled( int mediaPlayerId, bool* pEnable )
{
   if ( pEnable == NULL ) return SailErr_BadParam;

   SailErr retCode = SailErr_None;

   if ( mediaPlayerId >= MAX_PLAYER_HANDLES )
   {
      API_WARN("ERROR: mediaPlayerId (%d)\n", mediaPlayerId);
      return SailErr_BadParam;
   }

   tPlayerHandle *handle = lockHandle( mediaPlayerId );

#if 0
   do
   {
      if ( !(handle->state & PLAYER_STARTED) )
      {
         API_WARN("ERROR: mediaPlayerId (%d) not active\n", mediaPlayerId);
         retCode = SailErr_WrongState;
         break;
      }

      SessionData data = {SESSION_CMD_GET, AVOUT_DVBSUB_STATE};
      retCode = sessionControl( handle->session, &data );
      if ( retCode != SailErr_None )
      {
         API_WARN("ERROR: could not get subtitles state\n");
         break;
      }

      *pEnable = data.modopt.avout.subtitleCmd.enabled;

   } while ( 0 );

   API_NOISE("mediaPlayerId: %d enabled: %d\n", mediaPlayerId, (int)*pEnable);

#endif
   unlockHandle( handle );

   return retCode;
}

SailError MediaPlayerGetNumberOfSubtitleStreams( int mediaPlayerId, int* pCount )
{
   SailErr retCode = SailErr_None;

   if ( (mediaPlayerId >= MAX_PLAYER_HANDLES) || (pCount == NULL) )
   {
      API_WARN("ERROR: mediaPlayerId (%d) pCount (%p)\n", mediaPlayerId, pCount);
      return SailErr_BadParam;
   }

   tPlayerHandle *handle = lockHandle( mediaPlayerId );

#if 0
   do
   {
      if ( !(handle->state & PLAYER_STARTED) )
      {
         API_WARN("ERROR: mediaPlayerId (%d) not active\n", mediaPlayerId);
         retCode = SailErr_WrongState;
         break;
      }

      SessionData data = {SESSION_CMD_GET, TRANSPORT_SUBTITLE_LANG_LIST};
      retCode = sessionControl( handle->session, &data );
      if ( retCode == SailErr_None )
      {
         memcpy( &handle->subLangList, &data.modopt.transport.subtitleLangList, sizeof(tSubtitleLangList) );
         *pCount = handle->subLangList.count;
      }
      else
      {
         *pCount = 0;
      }

   } while ( 0 );

   API_NOISE("mediaPlayerId: %d count: %d\n", mediaPlayerId, (int)*pCount);

#endif
   unlockHandle( handle );

   return retCode;
}

SailError MediaPlayerSelectSubtitleStream( int mediaPlayerId, int subtitleIndex )
{
   SailErr retCode = SailErr_None;

   if ( mediaPlayerId >= MAX_PLAYER_HANDLES )
   {
      API_WARN("ERROR: mediaPlayerId (%d)\n", mediaPlayerId);
      return SailErr_BadParam;
   }

   API_NOISE("mediaPlayerId: %d index: %d\n", mediaPlayerId, subtitleIndex);

   tPlayerHandle *handle = lockHandle( mediaPlayerId );

#if 0
   do
   {
      if ( !(handle->state & PLAYER_STARTED) )
      {
         API_WARN("ERROR: mediaPlayerId (%d) not active\n", mediaPlayerId);
         retCode = SailErr_WrongState;
         break;
      }

      if ( subtitleIndex >= handle->subLangList.count )
      {
         API_WARN("ERROR: mediaPlayerId (%d) subtitleIndex: %d stream count: %d\n", 
                  mediaPlayerId, subtitleIndex, handle->subLangList.count);
         retCode = SailErr_BadParam;
         break;
      }

      handle->subtitle.index = subtitleIndex;
      SessionData data = {SESSION_CMD_SET, TRANSPORT_ACTIVE_SUBTITLE_INDEX};
      data.modopt.transport.activeSubtitleIndex = subtitleIndex;
      retCode = sessionControl( handle->session, &data );
      WARNONERROR( retCode );

   } while ( 0 );

#endif
   unlockHandle( handle );

   return retCode;
}

SailError MediaPlayerGetSubtitleStreamInfo( int mediaPlayerId, int subtitleIndex,
                                            char** ppSubtitleLanguage, int* pMpegSubtitleType )
{
   SailErr retCode = SailErr_None;

   if ( (mediaPlayerId >= MAX_PLAYER_HANDLES) || (pMpegSubtitleType == NULL) || (ppSubtitleLanguage == NULL) )
   {
      API_WARN("ERROR: mediaPlayerId (%d) pMpegSubtitleType (%p) ppSubtitleLanguage (%p)\n", mediaPlayerId, pMpegSubtitleType, ppSubtitleLanguage);
      return SailErr_BadParam;
   }

   tPlayerHandle *handle = lockHandle( mediaPlayerId );

#if 0
   do
   {
      if ( !(handle->state & PLAYER_STARTED) )
      {
         API_WARN("ERROR: mediaPlayerId (%d) not active\n", mediaPlayerId);
         retCode = SailErr_WrongState;
         break;
      }

      if ( handle->subLangList.count == 0 )
      {
         int cnt = 0;
         MediaPlayerGetNumberOfSubtitleStreams( mediaPlayerId, &cnt );
      }

      if ( subtitleIndex >= handle->subLangList.count )
      {
         API_WARN("ERROR: subtitleIndex (%d) subLangList.count (%d)\n", subtitleIndex, handle->subLangList.count);
         retCode = SailErr_BadParam;
         break;
      }

      *pMpegSubtitleType = handle->subLangList.item[subtitleIndex].subtitlingType;
      *ppSubtitleLanguage = (char *)handle->subLangList.item[subtitleIndex].isoCode;

   } while ( 0 );

   API_NOISE("mediaPlayerId: %d lang: %s type: %d\n", mediaPlayerId, *ppSubtitleLanguage, *pMpegSubtitleType);

#endif
   unlockHandle( handle );

   return retCode;
}

SailError MediaPlayerGetSelectedSubtitleStream( int mediaPlayerId, int* psubtitleIndex )
{
   SailErr retCode = SailErr_None;

   if ( (mediaPlayerId >= MAX_PLAYER_HANDLES) || (psubtitleIndex == NULL) )
   {
      API_WARN("ERROR: mediaPlayerId (%d) psubtitleIndex (%p)\n", mediaPlayerId, psubtitleIndex);
      return SailErr_BadParam;
   }

   tPlayerHandle *handle = lockHandle( mediaPlayerId );

#if 0
   do
   {
      if ( !(handle->state & PLAYER_STARTED) )
      {
         API_WARN("ERROR: mediaPlayerId (%d) not active\n", mediaPlayerId);
         retCode = SailErr_WrongState;
         break;
      }

      SessionData data = {SESSION_CMD_GET, TRANSPORT_ACTIVE_SUBTITLE_INDEX};
      retCode = sessionControl( handle->session, &data );
      WARNONERROR( retCode );
      if ( retCode == SailErr_None )
      {
         *psubtitleIndex = data.modopt.transport.activeSubtitleIndex;
      }

   } while ( 0 );

   API_NOISE("mediaPlayerId: %d index: %d\n", mediaPlayerId, *psubtitleIndex);

#endif
   unlockHandle( handle );

   return retCode;
}

SailError MediaPlayerGetVideoProperties( int mediaPlayerId, VideoProperties* properties )
{
   SailErr retCode = SailErr_None;

   if ( (mediaPlayerId >= MAX_PLAYER_HANDLES) || (properties == NULL) )
   {
      API_WARN("ERROR: bad parameter -- mediaPlayerId (%d) properties (%p)\n", mediaPlayerId, properties);
      return SailErr_BadParam;
   }

   tPlayerHandle *handle = lockHandle( mediaPlayerId );


#if 0
   memset( properties, 0, sizeof(VideoProperties) );

   if ( !(handle->state & PLAYER_STARTED) )
   {
      API_WARN("ERROR: mediaPlayerId (%d) not active\n", mediaPlayerId);
      unlockHandle( handle );
      return SailErr_WrongState;
   }

   SessionData data = {SESSION_CMD_GET, AVPLAYER_VIDEO_DIAG};

   retCode = sessionControl( handle->session, &data );
   WARNONERROR( retCode );
   if ( retCode == SailErr_None )
   {
      properties->ar = data.modopt.avplayer.diag.ar;
      properties->codec = data.modopt.avplayer.diag.vCodec;
      properties->xRes = data.modopt.avplayer.diag.pictureWidth;
      properties->yRes = data.modopt.avplayer.diag.pictureHeight;
   }

   API_NOISE("mediaPlayerId: %d ar: %d xRes: %u yRes: %u\n", 
             mediaPlayerId, (int)properties->ar, properties->xRes, properties->yRes);

#endif
   unlockHandle( handle );

   return retCode;
}

SailError MediaPlayerSetCopyProtection( int mediaPlayerId, int code )
{
   SailErr retCode = SailErr_None;

   if ( mediaPlayerId >= MAX_PLAYER_HANDLES )
   {
      API_WARN("ERROR: bad parameter -- mediaPlayerId (%d)\n", mediaPlayerId);
      return SailErr_BadParam;
   }

   tPlayerHandle *handle = lockHandle( mediaPlayerId );

#if 0
   if ( !(handle->state & PLAYER_STARTED) )
   {
      API_WARN("ERROR: mediaPlayerId (%d) not active\n", mediaPlayerId);
      unlockHandle( handle );
      return SailErr_WrongState;
   }

   API_NOISE("mediaPlayerId: %d code: %X\n", mediaPlayerId, code);

   SessionData data = {SESSION_CMD_SET, AVOUT_COPY_PROTECTION};
   data.modopt.avout.securityCode = (code << 16) | (code & 1) << 8) | code);

   retCode = sessionControl( handle->session, &data );
   WARNONERROR( retCode );

#endif
   unlockHandle( handle );

   return retCode;
}

