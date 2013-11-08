#ifndef __MEDIA_PLAYER_API_H__
#define __MEDIA_PLAYER_API_H__
/* ****************************************************************************
*
*                   Copyright 2011 Cisco Systems, Inc.
*
*                           Subscriber Engineering
*                           5030 Sugarloaf Parkway
*                               P.O.Box 465447
*                          Lawrenceville, GA 30042
*
*                        Proprietary and Confidential
*              Unauthorized distribution or copying is prohibited
*                            All rights reserved
*
* No part of this computer software may be reprinted, reproduced or utilized
* in any form or by any electronic, mechanical, or other means, now known or
* hereafter invented, including photocopying and recording, or using any
* information storage and retrieval system, without permission in writing
* from Cisco Systems, Inc.
*
******************************************************************************/

#include <SailInf.h>
//rms #include <SailTypes.h>
#define uint32_t  unsigned int
#define MAX_URL_LEN 1024

#define MAX_PLAYER_HANDLES 2

typedef enum
{
   NOTIFY_STREAMING_OK,
   NOTIFY_STREAMING_NOT_OK,
   NOTIFY_SYNCHRONIZED,
   NOTIFY_START_OF_FILE,
   NOTIFY_END_OF_FILE,
   NOTIFY_END_OF_STREAM,
   NOTIFY_DECRYPTION_FAILED,
   NOTIFY_NO_DECRYPTION_KEY,
   NOTIFY_EITPF_AVAILABLE,
   NOTIFY_VIDEO_ASPECT_RATIO_CHANGED,
   NOTIFY_VIDEO_RESOLUTION_CHANGED,
   NOTIFY_DVB_SIGNAL_OK,
   NOTIFY_DVB_SIGNAL_NOT_OK,
   NOTIFY_DVB_TIME_SET,
   NOTIFY_CHANGED_LANGUAGE_AUDIO,
   NOTIFY_CHANGED_LANGUAGE_SUBTITLE,
   NOTIFY_CHANGED_LANGUAGE_TELETEXT,
   NOTIFY_MEDIAPLAYER_OPEN,
   NOTIFY_MEDIAPLAYER_SOURCE_SET,
   NOTIFY_MEDIAPLAYER_CLOSE,
   NOTIFY_MEDIAPLAYER_BITRATE_CHANGE
}MediaEvent;

typedef struct
{
    int                 playerId;
    char                source[MAX_URL_LEN];
    uint32_t            retData;
    void               *userParam;
}MPCallbackData;

typedef void (*MediaPlayerCallback)(MediaEvent event, void* userParam);

typedef struct
{
   uint16_t       xRes;       ///< Stream width
   uint16_t       yRes;       ///< Stream height
   AspectRatio    ar;         ///< Stream aspect ratio
   VideoCodec     codec;      ///< Stream encoding
}VideoProperties;

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * 
 * @param mediaPlayerId
 * 
 * @param properties
 * 
 * @return SailError
 */
SailError MediaPlayerGetVideoProperties( int mediaPlayerId, VideoProperties* properties );

/**
 * 
 * @param mediaPlayerId
 * 
 * @param callback
 * 
 * @param userParam
 * 
 * @return SailError
 */
SailError MediaPlayerOpen( int mediaPlayerId, MediaPlayerCallback callback, void *userParam );

/**
 * 
 * @param mediaPlayerId
 * 
 * @return SailError
 */
SailError MediaPlayerClose( int mediaPlayerId );

/**
 * 
 * @param mediaPlayerId
 * @param source
 * 
 * @return SailError
 */
SailError MediaPlayerSetSource(int mediaPlayerId, const char* source);

/**
 * 
 * @param mediaPlayerId
 * @param source
 * 
 * @return SailError
 */
SailError MediaPlayerGetSource(int mediaPlayerId, char* source);

/**
 * 
 * @param mediaPlayerId
 * 
 * @return SailError
 */
SailError MediaPlayerCloseSource( int mediaPlayerId );

/**
 * 
 * @param mediaPlayerId
 * @param sink
 * 
 * @return SailError
 */
SailError MediaPlayerSetSink( int mediaPlayerId, const char* sink );

/**
 * 
 * @param mediaPlayerId
 * @param sink
 * 
 * @return SailError
 */
SailError MediaPlayerGetSink( int mediaPlayerId, char* sink );

/**
 * 
 * @param mediaPlayerId
 * @param speed
 * 
 * @return SailError
 */
SailError MediaPlayerSetPlaySpeed(int mediaPlayerId, float speed);

/**
 * 
 * @param mediaPlayerId
 * @param speed
 * 
 * @return SailError
 */
SailError MediaPlayerGetPlaySpeed(int mediaPlayerId, float *speed);

/**
 * 
 * @param mediaPlayerId
 * @param speeds
 * 
 * @return SailError
 */
SailError MediaPlayerGetSupportedSpeeds(int mediaPlayerId, float *speeds[]);

/**
 * 
 * @param mediaPlayerId
 * @param offset
 * @param whence
 * 
 * @return SailError
 */
SailError MediaPlayerSeek(int mediaPlayerId, int64_t offset, int whence);

/**
 * 
 * @param mediaPlayerId
 * @param pActualPlayPosition
 * @param pSizeInMS
 * @param pSizeInByte
 * @param pIsActiveRecording
 * 
 * @return SailError
 */
SailError MediaPlayerPlaybackInfo(int mediaPlayerId, uint64_t* pActualPlayPosition,
                                  uint64_t* pSizeInMS, uint64_t* pSizeInByte,
                                  bool* pIsActiveRecording);

/**
 * 
 * @param mediaPlayerId
 * @param enabled
 * 
 * @return SailError
 */
SailError MediaPlayerSetEnabled(int mediaPlayerId, bool enabled);

/**
 * 
 * @param mediaPlayerId
 * @param enabled
 * 
 * @return SailError
 */
SailError MediaPlayerIsEnabled(int mediaPlayerId, bool* enabled);

/**
 * 
 * @param mediaPlayerId
 * @param sourceRectangle
 * @param destinationRectangle
 * 
 * @return SailError
 */
SailError MediaPlayerVideoScale(int mediaPlayerId, Rectangle* sourceRectangle, Rectangle* destinationRectangle);

/**
 * 
 * @param mediaPlayerId
 * @param sourceRectangle
 * 
 * @return SailError
 */
SailError MediaPlayerVideoFullScale(int mediaPlayerId, Rectangle* sourceRectangle);


/************************************************************************************
 * 										Audio									  	*
 ************************************************************************************/

/**
 * 
 * @param mediaPlayerId
 * @param pCount
 * 
 * @return SailError
 */
SailError MediaPlayerGetNumberOfAudioStreams(int mediaPlayerId, int* pCount);

/**
 * 
 * @param mediaPlayerId
 * @param audioIndex
 * @param ppAudioLanguage
 * @param pAudioType
 * 
 * @return SailError
 */
SailError MediaPlayerGetAudioStreamInfo(int mediaPlayerId, int audioIndex,
                                        char** ppAudioLanguage, int* pAudioType);

/**
 * 
 * @param mediaPlayerId
 * @param pAudioIndex
 * 
 * @return SailError
 */
SailError MediaPlayerGetSelectedAudioStream(int mediaPlayerId, int* pAudioIndex);

/**
 * 
 * @param mediaPlayerId
 * @param audioIndex
 * 
 * @return SailError
 */
SailError MediaPlayerSelectAudioStream(int mediaPlayerId, int audioIndex);

/**
 * 
 * @param enable
 * 
 * @return SailError
 */
SailError MediaPlayerAudioMute(bool enable);

/**
 * 
 * @param left
 * @param right
 * 
 * @return SailError
 */
SailError MediaPlayerGetVolume(int* left, int* right);

/**
 * 
 * @param left
 * @param right
 * 
 * @return SailError
 */
SailError MediaPlayerSetVolume(int left, int right);

/**
 * 
 * @param pIsAudioMuted
 * 
 * @return SailError
 */
SailError MediaPlayerIsAudioMuted(bool* pIsAudioMuted);

/************************************************************************************
 * 										Subtitle								  	*
 ************************************************************************************/
/*
 * Bitmap based subtitles which are rendered on eg DirectFB surface.
 */
/*
 * MediaPlayerSetSubtitleEnabled
 * This function enables subtitle rendering.
 * Since no timestamps are returned in callback, SAIL determines when to show/clear subtitles.
 * This means subtitles will be shown/cleared at the time callback is called.
 *
 * The SubtitleEventDraw struct is returned in the drawSubtitleCallback
 *
 * TODO/Note : discussion needed on who initialises DirectFB as this will have an impact here.
*/
typedef struct
{
   Rectangle window; // subtitle window
   uint8_t *data;    // subtitle pixel data in raw 32-bit ARGB format
} SubtitleEventDraw;

typedef void (*drawSubtitleCallback)(int mediaPlayerId, SubtitleEventDraw *);
typedef void (*clearSubtitleCallback)(int mediaPlayerId);

SailError MediaPlayerSetSubtitleEnabled( int mediaPlayerId, bool enable,
                                         drawSubtitleCallback drawCb, clearSubtitleCallback clearCb );

/**
 * 
 * @param mediaPlayerId
 * @param pEnable
 * 
 * @return SailError
 */
SailError MediaPlayerIsSubtitleEnabled( int mediaPlayerId, bool* pEnable );

/**
 * 
 * @param mediaPlayerId
 * @param pCount
 * 
 * @return SailError
 */
SailError MediaPlayerGetNumberOfSubtitleStreams(int mediaPlayerId, int* pCount);

/**
 * 
 * @param mediaPlayerId
 * @param subtitleIndex
 * 
 * @return SailError
 */
SailError MediaPlayerSelectSubtitleStream(int mediaPlayerId, int subtitleIndex);

/**
 * 
 * @param mediaPlayerId
 * @param subtitleIndex
 * @param ppSubtitleLanguage
 * @param pMpegSubtitleType
 * 
 * @return SailError
 */
SailError MediaPlayerGetSubtitleStreamInfo(int mediaPlayerId, int subtitleIndex,
                                           char** ppSubtitleLanguage, int* pMpegSubtitleType);

/**
 * 
 * @param mediaPlayerId
 * @param psubtitleIndex
 * 
 * @return SailError
 */
SailError MediaPlayerGetSelectedSubtitleStream(int mediaPlayerId, int* psubtitleIndex);

/************************************************************************************
 * 										Teletext								  	*
 ************************************************************************************/

typedef struct
{
   Rectangle window; // teletext window
   uint8_t *data;    // teletext pixel data in raw 32-bit ARGB format
}TeletextEventDraw;

typedef void (*drawTeletextCallback)(int mediaPlayerId, TeletextEventDraw *);
typedef void (*clearTeletextCallback)(int mediaPlayerId);

/**
 * 
 * @param mediaPlayerId
 * @param enable
 * @param drawCb
 * @param clearCb
 * 
 * @return SailError
 */
SailError MediaPlayerSetTeletextEnabled( int mediaPlayerId, bool enable,
                                         drawTeletextCallback drawCb, clearTeletextCallback clearCb );

/**
 * 
 * @param mediaPlayerId
 * @param pEnable
 * 
 * @return SailError
 */
SailError MediaPlayerIsTeletextEnabled(int mediaPlayerId, bool* pEnable);

/**
 * 
 * @param mediaPlayerId
 * @param pCount
 * 
 * @return SailError
 */
SailError MediaPlayerGetNumberOfTeletextStreams(int mediaPlayerId, int* pCount);
SailError MediaPlayerSelectTeletextStream(int mediaPlayerId, int teletextIndex);
SailError MediaPlayerGetTeletextStreamInfo(int mediaPlayerId, int teletextIndex,
                                           char** ppTeletextLanguage, int* pMpegTeletextType);
SailError MediaPlayerGetSelectedTeletextStream(int mediaPlayerId, int* pteletextIndex);
SailError MediaPlayerSelectTeletextPage( int mediaPlayerId, int pageNumber);
SailError MediaPlayerNextTeletextPage( int mediaPlayerId );
SailError MediaPlayerPrevTeletextPage( int mediaPlayerId );
SailError MediaPlayerHandleTeletextRedButton( int mediaPlayerId );
SailError MediaPlayerHandleTeletextGreenButton( int mediaPlayerId );
SailError MediaPlayerHandleTeletextYellowButton( int mediaPlayerId );
SailError MediaPlayerHandleTeletextBlueButton( int mediaPlayerId );
SailError MediaPlayerHandleTeletextDigit( int mediaPlayerId, int digit );
SailError MediaPlayerTeletextSubtitleMode( int mediaPlayerId, bool enable );

#ifdef __cplusplus
}
#endif

#endif 

