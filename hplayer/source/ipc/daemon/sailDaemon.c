//#include <SessionManager.h>
#include <SailInfCommon.h>
#include <SailErr.h>
#include <errno.h>
#include <string.h>
#include <curl/curl.h>
#include <stdio.h>
#include "hplayer_dbus_server_generated.h"
#include "log.h"
#include <execinfo.h>
#include <signal.h>
#include <stdlib.h>

#ifdef SAIL_LOGGING_SUPPORT
#define DAEMON_NOISE( Message )                        \
   do \
   { \
      if( debugLogVisible( gDaemonLogHandle, DEBUG_LOG_LEVEL_NOISE ) ) \
      { \
         debugLogPrefix( gDaemonLogHandle );  \
         debugLogPrint( "%s(%d) ", __FUNCTION__, __LINE__ );  \
         debugLogPrint Message; \
      }\
   } \
   while( 0 )
#define DAEMON_INFO( Message )                        \
   do \
   { \
      if( debugLogVisible( gDaemonLogHandle, DEBUG_LOG_LEVEL_INFO ) ) \
      { \
         debugLogPrefix( gDaemonLogHandle );  \
         debugLogPrint( "%s(%d) ", __FUNCTION__, __LINE__ );  \
         debugLogPrint Message; \
      }\
   } \
   while( 0 )
#define DAEMON_WARN( Message )                        \
   do \
   { \
      if( debugLogVisible( gDaemonLogHandle, DEBUG_LOG_LEVEL_WARN ) ) \
      { \
         debugLogPrefix( gDaemonLogHandle );  \
         debugLogPrint( "%s(%d) ", __FUNCTION__, __LINE__ );  \
         debugLogPrint Message; \
      }\
   } \
   while( 0 )
#define DAEMON_FATAL( Message )                        \
   do \
   { \
      if( debugLogVisible( gDaemonLogHandle, DEBUG_LOG_LEVEL_FATAL ) ) \
      { \
         debugLogPrefix( gDaemonLogHandle );  \
         debugLogPrint( "%s(%d) ", __FUNCTION__, __LINE__ );  \
         debugLogPrint Message; \
      }\
   } \
   while( 0 )

#else
#define DAEMON_NOISE(Message)
#define DAEMON_INFO(Message)
#define DAEMON_WARN(Message)
#define DAEMON_FATAL(Message)
#endif

void LOCK(int lock)
{

}
void UNLOCK(int lock)
{

}
logHandle gDaemonLogHandle = NULL;

MPCallbackData MPCD[MAX_PLAYER_HANDLES];

/*
 * MediaPlayerCallback
 */
void MPCallback(MediaEvent event, void* userParam)
{
    MPCallbackData* mpcdata = (MPCallbackData *) userParam;

    org_cisco_sail_emit_sail_media_player_notify( (OrgCiscoSail *) mpcdata->userParam,
                                                  mpcdata->playerId,
                                                  mpcdata->source,
                                                  event,
                                                  mpcdata->retData );

    switch (event)
    {
        case NOTIFY_STREAMING_OK:
            DAEMON_INFO(("playerId: %d, NOTIFY_STREAMING_OK\n", mpcdata->playerId));
            break;
        case NOTIFY_STREAMING_NOT_OK:
            DAEMON_INFO(("playerId: %d, NOTIFY_STREAMING_NOT_OK\n", mpcdata->playerId));
            break;
        case NOTIFY_SYNCHRONIZED:
            DAEMON_INFO(("playerId: %d, NOTIFY_SYNCHRONIZED\n", mpcdata->playerId));
            break;
        case NOTIFY_START_OF_FILE:
            DAEMON_INFO(("playerId: %d, NOTIFY_START_OF_FILE\n", mpcdata->playerId));
            break;
        case NOTIFY_END_OF_FILE:
            DAEMON_INFO(("playerId: %d, NOTIFY_END_OF_FILE\n", mpcdata->playerId));
            break;
        case NOTIFY_END_OF_STREAM:
            DAEMON_INFO(("playerId: %d, NOTIFY_END_OF_STREAM\n", mpcdata->playerId));
            break;
        case NOTIFY_DECRYPTION_FAILED:
            DAEMON_INFO(("playerId: %d, NOTIFY_DECRYPTION_FAILED\n", mpcdata->playerId));
            break;
        case NOTIFY_NO_DECRYPTION_KEY:
            DAEMON_INFO(("playerId: %d, NOTIFY_NO_DECRYPTION_KEY\n", mpcdata->playerId));
            break;
        case NOTIFY_EITPF_AVAILABLE:
            DAEMON_INFO(("playerId: %d, NOTIFY_EITPF_AVAILABLE\n", mpcdata->playerId));
            break;
        case NOTIFY_VIDEO_ASPECT_RATIO_CHANGED:
            DAEMON_INFO(("playerId: %d, NOTIFY_VIDEO_ASPECT_RATIO_CHANGED\n", mpcdata->playerId));
            break;
        case NOTIFY_VIDEO_RESOLUTION_CHANGED:
            DAEMON_INFO(("playerId: %d, NOTIFY_VIDEO_RESOLUTION_CHANGED\n", mpcdata->playerId));
            break;
        case NOTIFY_DVB_SIGNAL_OK:
            DAEMON_INFO(("playerId: %d, NOTIFY_DVB_SIGNAL_OK\n", mpcdata->playerId));
            break;
        case NOTIFY_DVB_SIGNAL_NOT_OK:
            DAEMON_INFO(("playerId: %d, NOTIFY_DVB_SIGNAL_NOT_OK\n", mpcdata->playerId));
            break;
        case NOTIFY_DVB_TIME_SET:
            DAEMON_INFO(("playerId: %d, NOTIFY_DVB_TIME_SET\n", mpcdata->playerId));
            break;
        case NOTIFY_CHANGED_LANGUAGE_AUDIO:
            DAEMON_INFO(("playerId: %d, NOTIFY_CHANGED_LANGUAGE_AUDIO\n", mpcdata->playerId));
            break;
        case NOTIFY_CHANGED_LANGUAGE_SUBTITLE:
            DAEMON_INFO(("playerId: %d, NOTIFY_CHANGED_LANGUAGE_SUBTITLE\n", mpcdata->playerId));
            break;
        case NOTIFY_CHANGED_LANGUAGE_TELETEXT:
            DAEMON_INFO(("playerId: %d, NOTIFY_CHANGED_LANGUAGE_TELETEXT\n", mpcdata->playerId));
            break;
        case NOTIFY_MEDIAPLAYER_OPEN:
            DAEMON_INFO(("playerId: %d, NOTIFY_MEDIAPLAYER_OPEN\n", mpcdata->playerId));
            break;
        case NOTIFY_MEDIAPLAYER_SOURCE_SET:
            DAEMON_INFO(("playerId: %d, NOTIFY_MEDIAPLAYER_SOURCE_SET\n", mpcdata->playerId));
            break;
        case NOTIFY_MEDIAPLAYER_CLOSE:
            DAEMON_INFO(("playerId: %d, NOTIFY_MEDIAPLAYER_CLOSE\n", mpcdata->playerId));
            break;
        case NOTIFY_MEDIAPLAYER_BITRATE_CHANGE:
            DAEMON_INFO(("playerId: %d, NOTIFY_MEDIAPLAYER_BITRATE_CHANGE\n", mpcdata->playerId));
            break;
        default:
            DAEMON_INFO(("playerId: %d, Unknown MediaEvent\n", mpcdata->playerId));
            break;
    }
}

/* 
 * MEDIA PLAYER API
 */
static gboolean
on_handle_sail_media_player_get_video_properties (OrgCiscoSail       *interface,
                       GDBusMethodInvocation  *invocation,
                       const gint             arg_playerId)
{
    VideoProperties properties;
    uint16_t xRes = 0;
    uint16_t yRes = 0;
    AspectRatio ar = INVALID_AR;
    VideoCodec codec = VIDEO_CODEC_UNKNOWN;

    DAEMON_INFO(("SailMediaPlayerGetVideoProperties()\n"));

    SailError err = SAIL_ERROR_SUCCESS;

    err = MediaPlayerGetVideoProperties( arg_playerId, &properties );
    if ( err == SAIL_ERROR_SUCCESS )
    {
        xRes = properties.xRes;
        yRes = properties.yRes;
        ar = properties.ar;
        codec = properties.codec;
    }

    org_cisco_sail_complete_sail_media_player_get_video_properties (
                                                    interface, invocation,
                                                    xRes, yRes, ar, codec, err);

   return TRUE;
}

static gboolean
on_handle_sail_media_player_open (OrgCiscoSail          *interface,
                       GDBusMethodInvocation  *invocation,
                       const gint             arg_playerId)
{
    DAEMON_INFO(("SailMediaPlayerOpen()\n"));

    SailError err = SAIL_ERROR_SUCCESS;
    
    if ( arg_playerId >= MAX_PLAYER_HANDLES )
    {
        err = SailErr_BadParam;
    } else {
        MPCD[arg_playerId].userParam = (void *) interface;
        MPCD[arg_playerId].playerId = arg_playerId;
        strcpy( MPCD[arg_playerId].source, "" );

        err = MediaPlayerOpen( arg_playerId, MPCallback,
                               (void *) &MPCD[arg_playerId] );
    }

    org_cisco_sail_complete_sail_media_player_open (interface, invocation, err);

   return TRUE;
}

static gboolean
on_handle_sail_media_player_close (OrgCiscoSail          *interface,
                       GDBusMethodInvocation  *invocation,
                       const gint             arg_playerId)
{
    DAEMON_INFO(("SailMediaPlayerClose()\n"));

    SailError err = SAIL_ERROR_SUCCESS;

    err = MediaPlayerClose( arg_playerId );

    MPCD[arg_playerId].userParam = NULL;
    MPCD[arg_playerId].playerId = arg_playerId;
    strcpy( MPCD[arg_playerId].source, "" );

    org_cisco_sail_complete_sail_media_player_close (interface, invocation, 
                                                     err);

   return TRUE;
}

static gboolean
on_handle_sail_media_player_set_source (OrgCiscoSail          *interface,
                       GDBusMethodInvocation  *invocation,
                       const gint             arg_playerId,
                       const gchar            *arg_source )
{
    DAEMON_INFO(("SailMediaPlayerSetSource()\n"));

    SailError err = SAIL_ERROR_SUCCESS;

    strncpy( MPCD[arg_playerId].source, arg_source, MAX_URL_LEN );

    err = MediaPlayerSetSource( arg_playerId, arg_source );

    org_cisco_sail_complete_sail_media_player_set_source (interface,
                                                          invocation, 
                                                          err);

   return TRUE;
}

static gboolean
on_handle_sail_media_player_get_source (OrgCiscoSail          *interface,
                       GDBusMethodInvocation  *invocation,
                       const gint             arg_playerId)
{
    char source[1024];

    DAEMON_INFO(("SailMediaPlayerGetSource()\n"));

    SailError err = SAIL_ERROR_SUCCESS;

    err = MediaPlayerGetSource( arg_playerId, source );

    org_cisco_sail_complete_sail_media_player_get_source (interface,
                                                          invocation,
                                                          source,
                                                          err);

   return TRUE;
}

static gboolean
on_handle_sail_media_player_close_source (OrgCiscoSail          *interface,
                       GDBusMethodInvocation  *invocation,
                       const gint             arg_playerId)
{
    DAEMON_INFO(("SailMediaPlayerCloseSource()\n"));

    SailError err = SAIL_ERROR_SUCCESS;

    err = MediaPlayerCloseSource( arg_playerId );

    org_cisco_sail_complete_sail_media_player_close_source (interface,
                                                          invocation,
                                                          err);

   return TRUE;
}

static gboolean
on_handle_sail_media_player_set_sink (OrgCiscoSail           *interface,
                                      GDBusMethodInvocation  *invocation,
                                      const gint             arg_playerId,
                                      const gchar            *arg_sink )
{
    DAEMON_INFO(("SailMediaPlayerSetSink()\n"));

    SailError err = SAIL_ERROR_SUCCESS;

    err = MediaPlayerSetSink( arg_playerId, arg_sink );

    org_cisco_sail_complete_sail_media_player_set_sink ( interface,
                                                         invocation, 
                                                         err );

   return TRUE;
}

static gboolean
on_handle_sail_media_player_get_sink ( OrgCiscoSail           *interface,
                                       GDBusMethodInvocation  *invocation,
                                       const gint             arg_playerId )
{
    char sink[1024];

    DAEMON_INFO(("SailMediaPlayerGetSink()\n"));

    SailError err = SAIL_ERROR_SUCCESS;

    err = MediaPlayerGetSink( arg_playerId, sink );

    org_cisco_sail_complete_sail_media_player_get_sink ( interface,
                                                         invocation,
                                                         sink,
                                                         err );

   return TRUE;
}

   static gboolean
on_handle_sail_media_player_set_play_speed (OrgCiscoSail          *interface,
                       GDBusMethodInvocation  *invocation,
                       const gint             arg_playerId,
                       gdouble                arg_speed)
{
    DAEMON_INFO(("SailMediaPlayerSetPlaySpeed()\n"));

    SailError err = SAIL_ERROR_SUCCESS;

    err = MediaPlayerSetPlaySpeed( arg_playerId, arg_speed );

    org_cisco_sail_complete_sail_media_player_set_play_speed (interface,
                                                              invocation,
                                                              err);

   return TRUE;
}

static gboolean
on_handle_sail_media_player_get_play_speed (OrgCiscoSail          *interface,
                       GDBusMethodInvocation  *invocation,
                       const gint             arg_playerId)
{
    float speed = 0.0;

    DAEMON_INFO(("SailMediaPlayerGetPlaySpeed()\n"));

    SailError err = SAIL_ERROR_SUCCESS;

    err = MediaPlayerGetPlaySpeed( arg_playerId, &speed );

    org_cisco_sail_complete_sail_media_player_get_play_speed (interface,
                                                              invocation,
                                                              speed,
                                                              err);

   return TRUE;
}

static gboolean
on_handle_sail_media_player_get_supported_speeds (OrgCiscoSail       *interface,
                       GDBusMethodInvocation  *invocation,
                       const gint             arg_playerId)
{
    float *speeds = NULL;
    GVariantBuilder *builder;
    GVariant *value;
    int i = 0;

    DAEMON_INFO(("SailMediaPlayerGetSupportedSpeeds()\n"));

    SailError err = SAIL_ERROR_SUCCESS;

    err = MediaPlayerGetSupportedSpeeds( arg_playerId, &speeds );

    if ( err == SAIL_ERROR_SUCCESS )
    {
        builder = g_variant_builder_new (G_VARIANT_TYPE ("ad"));
        while ( (double) speeds[i] != 0.0 )
        {
            g_variant_builder_add (builder, "d", speeds[i]);
            i++;
        }
        g_variant_builder_add (builder, "d", 0.0);
        value = g_variant_new ("ad", builder);
        g_variant_builder_unref (builder);
    }

    org_cisco_sail_complete_sail_media_player_get_supported_speeds (interface,
                                                              invocation,
                                                              value,
                                                              err);

   g_variant_unref (value);

   return TRUE;
}

static gboolean
on_handle_sail_media_player_seek (OrgCiscoSail          *interface,
                       GDBusMethodInvocation  *invocation,
                       const gint             arg_playerId,
                       const gint64           arg_offset,
                       const gint             arg_whence)
{
    DAEMON_INFO(("SailMediaPlayerSeek()\n"));

    SailError err = SAIL_ERROR_SUCCESS;

    err = MediaPlayerSeek( arg_playerId, arg_offset, arg_whence );

    org_cisco_sail_complete_sail_media_player_seek (interface,
                                                    invocation,
                                                    err);

   return TRUE;
}

static gboolean
on_handle_sail_media_player_playback_info (OrgCiscoSail          *interface,
                       GDBusMethodInvocation  *invocation,
                       const gint             arg_playerId)
{
    uint64_t ActualPlayPosition = 0;
    uint64_t SizeInMS = 0;
    uint64_t SizeInByte = 0;
    bool IsActiveRecording = false;

    DAEMON_INFO(("SailMediaPlayerPlaybackInfo()\n"));

    SailError err = SAIL_ERROR_SUCCESS;

    err = MediaPlayerPlaybackInfo( arg_playerId, &ActualPlayPosition, &SizeInMS,
                                   &SizeInByte, &IsActiveRecording );

    org_cisco_sail_complete_sail_media_player_playback_info (interface,
                                                    invocation,
                                                    ActualPlayPosition,
                                                    SizeInMS,
                                                    SizeInByte,
                                                    IsActiveRecording,
                                                    err);

   return TRUE;
}

static gboolean
on_handle_sail_media_player_set_enabled (OrgCiscoSail          *interface,
                       GDBusMethodInvocation  *invocation,
                       const gint             arg_playerId,
                       const gboolean         arg_enabled)
{
    DAEMON_INFO(("SailMediaPlayerSetEnabled()\n"));

    SailError err = SAIL_ERROR_SUCCESS;

    err = MediaPlayerSetEnabled( arg_playerId, arg_enabled );

    org_cisco_sail_complete_sail_media_player_set_enabled (interface,
                                                    invocation,
                                                    err);

   return TRUE;
}

static gboolean
on_handle_sail_media_player_is_enabled (OrgCiscoSail          *interface,
                       GDBusMethodInvocation  *invocation,
                       const gint             arg_playerId)
{
    bool enabled = false;
    DAEMON_INFO(("SailMediaPlayerIsEnabled()\n"));

    SailError err = SAIL_ERROR_SUCCESS;

    err = MediaPlayerIsEnabled( arg_playerId, &enabled );

    org_cisco_sail_complete_sail_media_player_is_enabled (interface,
                                                    invocation,
                                                    enabled,
                                                    err);

   return TRUE;
}

static gboolean
on_handle_sail_media_player_video_scale (OrgCiscoSail       *interface,
                       GDBusMethodInvocation *invocation,
                       const gint             arg_playerId,
                       GVariant              *arg_sourceRectangle,
                       GVariant              *arg_destinationRectangle)
{
    DAEMON_INFO(("SailMediaPlayerVideoScale()\n"));

    Rectangle sourceRectangle;
    Rectangle destinationRectangle;

    GVariantIter *iter;

    SailError err = SAIL_ERROR_SUCCESS;

    g_variant_get (arg_sourceRectangle, "ai", &iter);
    g_variant_iter_loop (iter, "i", &(sourceRectangle.x));
    g_variant_iter_loop (iter, "i", &(sourceRectangle.y));
    g_variant_iter_loop (iter, "i", &(sourceRectangle.width));
    g_variant_iter_loop (iter, "i", &(sourceRectangle.height));
    g_variant_iter_free (iter);

    g_variant_get (arg_destinationRectangle, "ai", &iter);
    g_variant_iter_loop (iter, "i", &(destinationRectangle.x));
    g_variant_iter_loop (iter, "i", &(destinationRectangle.y));
    g_variant_iter_loop (iter, "i", &(destinationRectangle.width));
    g_variant_iter_loop (iter, "i", &(destinationRectangle.height));
    g_variant_iter_free (iter);

    err = MediaPlayerVideoScale( arg_playerId, &sourceRectangle,
                                 &destinationRectangle );

    org_cisco_sail_complete_sail_media_player_video_scale (interface,
                                                           invocation,
                                                           err);

   return TRUE;
}

static gboolean
on_handle_sail_media_player_video_full_scale (OrgCiscoSail       *interface,
                       GDBusMethodInvocation *invocation,
                       const gint             arg_playerId,
                       GVariant              *arg_sourceRectangle)
{
    DAEMON_INFO(("SailMediaPlayerVideoFullScale()\n"));

    Rectangle sourceRectangle;

    GVariantIter *iter;

    SailError err = SAIL_ERROR_SUCCESS;

    g_variant_get (arg_sourceRectangle, "ai", &iter);
    g_variant_iter_loop (iter, "i", &(sourceRectangle.x));
    g_variant_iter_loop (iter, "i", &(sourceRectangle.y));
    g_variant_iter_loop (iter, "i", &(sourceRectangle.width));
    g_variant_iter_loop (iter, "i", &(sourceRectangle.height));
    g_variant_iter_free (iter);

    err = MediaPlayerVideoFullScale( arg_playerId, &sourceRectangle );

    org_cisco_sail_complete_sail_media_player_video_full_scale (interface,
                                                                invocation,
                                                                err);

   return TRUE;
}

/* Audio */
static gboolean
on_handle_sail_media_player_get_number_of_audio_streams (
                       OrgCiscoSail           *interface,
                       GDBusMethodInvocation  *invocation,
                       const gint              arg_playerId )
{
    int Count = 0;
    DAEMON_INFO(("SailMediaPlayerGetNumberOfAudioStreams()\n"));

    SailError err = SAIL_ERROR_SUCCESS;

    err = MediaPlayerGetNumberOfAudioStreams( arg_playerId, &Count );

    org_cisco_sail_complete_sail_media_player_get_number_of_audio_streams 
                                            (interface,
                                             invocation,
                                             Count,
                                             err);

   return TRUE;
}

static gboolean
on_handle_sail_media_player_get_audio_stream_info(
                       OrgCiscoSail           *interface,
                       GDBusMethodInvocation  *invocation,
                       const gint              arg_playerId,
                       const gint              arg_audioIndex )
{
    char *pAudioLanguage;
    int AudioType;
    DAEMON_INFO(("SailMediaPlayerGetAudioStreamInfo()\n"));

    SailError err = SAIL_ERROR_SUCCESS;

    err = MediaPlayerGetAudioStreamInfo( arg_playerId, arg_audioIndex,
                                         &pAudioLanguage, &AudioType );

    org_cisco_sail_complete_sail_media_player_get_audio_stream_info
                                            (interface,
                                             invocation,
                                             pAudioLanguage,
                                             AudioType,
                                             err);

   return TRUE;
}

static gboolean
on_handle_sail_media_player_get_selected_audio_stream(
                       OrgCiscoSail           *interface,
                       GDBusMethodInvocation  *invocation,
                       const gint              arg_playerId )
{
    int AudioIndex = 0;
    DAEMON_INFO(("SailMediaPlayerGetSelectedAudioStream()\n"));

    SailError err = SAIL_ERROR_SUCCESS;

    err = MediaPlayerGetSelectedAudioStream( arg_playerId, &AudioIndex );

    org_cisco_sail_complete_sail_media_player_get_selected_audio_stream
                                            (interface,
                                             invocation,
                                             AudioIndex,
                                             err);

   return TRUE;
}

static gboolean
on_handle_sail_media_player_select_audio_stream(
                       OrgCiscoSail           *interface,
                       GDBusMethodInvocation  *invocation,
                       const gint              arg_playerId,
                       const gint              arg_AudioIndex )
{
    DAEMON_INFO(("SailMediaPlayerSelectAudioStream()\n"));

    SailError err = SAIL_ERROR_SUCCESS;

    err = MediaPlayerSelectAudioStream( arg_playerId, arg_AudioIndex );

    org_cisco_sail_complete_sail_media_player_select_audio_stream
                                            (interface,
                                             invocation,
                                             err);

   return TRUE;
}

static gboolean
on_handle_sail_media_player_audio_mute(
                       OrgCiscoSail           *interface,
                       GDBusMethodInvocation  *invocation,
                       const gboolean          arg_enable )
{
    DAEMON_INFO(("SailMediaPlayerAudioMute()\n"));

    SailError err = SAIL_ERROR_SUCCESS;

    err = MediaPlayerAudioMute( arg_enable );

    org_cisco_sail_complete_sail_media_player_audio_mute
                                            (interface,
                                             invocation,
                                             err);

   return TRUE;
}

static gboolean
on_handle_sail_media_player_get_volume(
                       OrgCiscoSail           *interface,
                       GDBusMethodInvocation  *invocation )
{
    int left = 0;
    int right = 0;
    DAEMON_INFO(("SailMediaPlayerGetVolume()\n"));

    SailError err = SAIL_ERROR_SUCCESS;

    err = MediaPlayerGetVolume( &left, &right );

    org_cisco_sail_complete_sail_media_player_get_volume
                                            (interface,
                                             invocation,
                                             left,
                                             right,
                                             err);

   return TRUE;
}

static gboolean
on_handle_sail_media_player_set_volume(
                       OrgCiscoSail           *interface,
                       GDBusMethodInvocation  *invocation,
                       const gint              arg_left,
                       const gint              arg_right )
{
    DAEMON_INFO(("SailMediaPlayerSetVolume()\n"));

    SailError err = SAIL_ERROR_SUCCESS;

    err = MediaPlayerSetVolume( arg_left, arg_right );

    org_cisco_sail_complete_sail_media_player_set_volume
                                            (interface,
                                             invocation,
                                             err);

   return TRUE;
}

static gboolean
on_handle_sail_media_player_is_audio_muted(
                       OrgCiscoSail           *interface,
                       GDBusMethodInvocation  *invocation )
{
    bool IsAudioMuted = false;
    DAEMON_INFO(("SailMediaPlayerIsAudioMuted()\n"));

    SailError err = SAIL_ERROR_SUCCESS;

    err = MediaPlayerIsAudioMuted( &IsAudioMuted );

    org_cisco_sail_complete_sail_media_player_is_audio_muted
                                            (interface,
                                             invocation,
                                             IsAudioMuted,
                                             err);

   return TRUE;
}

/*
 * MEDIA RECORDER API
 */
static gboolean
on_handle_sail_is_media_recorder_available (OrgCiscoSail          *interface,
                       GDBusMethodInvocation  *invocation)
{
    bool available = false;
    DAEMON_INFO(("SailIsMediaRecorderAvailable()\n"));

    SailError err = SAIL_ERROR_SUCCESS;

    err = IsMediaRecorderAvailable( &available );

    org_cisco_sail_complete_sail_is_media_recorder_available (interface,
                                                             invocation,
                                                             available,
                                                             err);

   return TRUE;
}

static gboolean
on_handle_sail_media_recorder_start_asset_record (OrgCiscoSail       *interface,
                       GDBusMethodInvocation  *invocation,
                       gchar            *arg_asset,
                       gchar            *arg_source,
                       const gint             time_len_ms)
{
    DAEMON_INFO(("SailMediaRecorderStartAssetRecord()\n"));

    SailError err = SAIL_ERROR_SUCCESS;

    err = MediaRecorderStartAssetRecord( arg_asset, arg_source, time_len_ms);

    org_cisco_sail_complete_sail_media_recorder_start_asset_record (interface,
                                                                    invocation,
                                                                    err);

   return TRUE;
}

static gboolean
on_handle_sail_media_recorder_stop_asset_record (OrgCiscoSail       *interface,
                       GDBusMethodInvocation  *invocation,
                       const gchar            *arg_asset)
{
    DAEMON_INFO(("SailMediaRecorderStopAssetRecord()\n"));

    SailError err = SAIL_ERROR_SUCCESS;

    err = MediaRecorderStopAssetRecord( arg_asset );

    org_cisco_sail_complete_sail_media_recorder_stop_asset_record (interface,
                                                                   invocation,
                                                                   err);

   return TRUE;
}

static gboolean
on_handle_sail_media_recorder_delete_asset_record (OrgCiscoSail      *interface,
                       GDBusMethodInvocation  *invocation,
                       const gchar            *arg_asset)
{
    DAEMON_INFO(("SailMediaRecorderDeleteAssetRecord()\n"));

    SailError err = SAIL_ERROR_SUCCESS;

    err = MediaRecorderDeleteAssetRecord( arg_asset );

    org_cisco_sail_complete_sail_media_recorder_delete_asset_record (interface,
                                                                     invocation,
                                                                     err);

   return TRUE;
}

static gboolean
on_handle_sail_media_recorder_get_asset_record_info (OrgCiscoSail    *interface,
                       GDBusMethodInvocation  *invocation,
                       const gchar            *arg_asset)
{
    uint64_t SizeInMS = 0;
    uint64_t SizeInByte = 0;

    DAEMON_INFO(("SailMediaRecorderGetAssetRecordInfo()\n"));

    SailError err = SAIL_ERROR_SUCCESS;

    err = MediaRecorderGetAssetRecordInfo( arg_asset, &SizeInMS,
                                           &SizeInByte );

    org_cisco_sail_complete_sail_media_recorder_get_asset_record_info (
                                                                interface,
                                                                invocation,
                                                                SizeInMS,
                                                                SizeInByte,
                                                                err);

   return TRUE;
}

static gboolean
on_handle_sail_media_recorder_start_timeshift (OrgCiscoSail    *interface,
                       GDBusMethodInvocation  *invocation,
                       const gint              arg_mediaPlayerId,
                       const guint64           arg_bufferSizeInMs )
{
    DAEMON_INFO(("SailMediaRecorderStartTimeshift()\n"));

    SailError err = SAIL_ERROR_SUCCESS;

    err = MediaRecorderStartTimeshift( arg_mediaPlayerId, arg_bufferSizeInMs );

    org_cisco_sail_complete_sail_media_recorder_start_timeshift ( interface,
                                                                  invocation,
                                                                  err);

   return TRUE;
}

static gboolean
on_handle_sail_media_recorder_stop_timeshift (OrgCiscoSail    *interface,
                       GDBusMethodInvocation  *invocation,
                       const gint              arg_mediaPlayerId)
{
    DAEMON_INFO(("SailMediaRecorderStopTimeshift()\n"));

    SailError err = SAIL_ERROR_SUCCESS;

    err = MediaRecorderStopTimeshift( arg_mediaPlayerId );

    org_cisco_sail_complete_sail_media_recorder_stop_timeshift ( interface,
                                                                 invocation,
                                                                 err);

   return TRUE;
}

static gboolean
on_handle_sail_media_recorder_get_number_of_assets (OrgCiscoSail    *interface,
                       GDBusMethodInvocation  *invocation)
{
    gint Count = 0;

    DAEMON_INFO(("SailMediaRecorderGetNumberOfAssets()\n"));

    SailError err = SAIL_ERROR_SUCCESS;

    err = MediaRecorderGetNumberOfAssets( &Count );

    org_cisco_sail_complete_sail_media_recorder_get_number_of_assets (
                                                                interface,
                                                                invocation,
                                                                Count,
                                                                err);

   return TRUE;
}

static gboolean
on_handle_sail_media_recorder_get_asset_info (OrgCiscoSail    *interface,
                       GDBusMethodInvocation  *invocation,
                       const gint             arg_recordIndex)
{
    tRecordInfo *info = NULL;

    DAEMON_INFO(("SailMediaRecorderGetAssetInfo()\n"));

    SailError err = SAIL_ERROR_SUCCESS;

    err = MediaRecorderGetAssetInfo( arg_recordIndex, &info );

    if ( err == SAIL_ERROR_SUCCESS )
    {
        org_cisco_sail_complete_sail_media_recorder_get_asset_info( interface,
                                                                    invocation,
                                                                  info->seconds,
                                                                    info->size,
                                                                    info->path,
                                                                    err);
    } else {
        org_cisco_sail_complete_sail_media_recorder_get_asset_info( interface,
                                                                    invocation,
                                                                    0,
                                                                    0,
                                                                    NULL,
                                                                    err);
    }

   return TRUE;
}


static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
  DAEMON_INFO(("Acquired the name %s\n", name));
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
  DAEMON_INFO(("Lost the name %s\n", name));
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
    GError *error;
    OrgCiscoSail *interface = org_cisco_sail_skeleton_new();
    org_cisco_sail_set_verbose (interface, TRUE);

    DAEMON_INFO(("bus acquired\n"));

    g_signal_connect (interface,
                      "handle-sail-media-player-get-video-properties",
                      G_CALLBACK (on_handle_sail_media_player_get_video_properties),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-player-open",
                      G_CALLBACK (on_handle_sail_media_player_open),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-player-close",
                      G_CALLBACK (on_handle_sail_media_player_close),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-player-set-source",
                      G_CALLBACK (on_handle_sail_media_player_set_source),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-player-get-source",
                      G_CALLBACK (on_handle_sail_media_player_get_source),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-player-close-source",
                      G_CALLBACK (on_handle_sail_media_player_close_source),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-player-set-sink",
                      G_CALLBACK (on_handle_sail_media_player_set_sink),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-player-get-sink",
                      G_CALLBACK (on_handle_sail_media_player_get_sink),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-player-set-play-speed",
                      G_CALLBACK (on_handle_sail_media_player_set_play_speed),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-player-get-play-speed",
                      G_CALLBACK (on_handle_sail_media_player_get_play_speed),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-player-get-supported-speeds",
                      G_CALLBACK (on_handle_sail_media_player_get_supported_speeds),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-player-seek",
                      G_CALLBACK (on_handle_sail_media_player_seek),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-player-playback-info",
                      G_CALLBACK (on_handle_sail_media_player_playback_info),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-player-set-enabled",
                      G_CALLBACK (on_handle_sail_media_player_set_enabled),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-player-is-enabled",
                      G_CALLBACK (on_handle_sail_media_player_is_enabled),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-player-video-scale",
                      G_CALLBACK (on_handle_sail_media_player_video_scale),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-player-video-full-scale",
                      G_CALLBACK (on_handle_sail_media_player_video_full_scale),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-player-get-number-of-audio-streams",
                      G_CALLBACK (on_handle_sail_media_player_get_number_of_audio_streams),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-player-get-audio-stream-info",
                      G_CALLBACK (on_handle_sail_media_player_get_audio_stream_info),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-player-get-selected-audio-stream",
                      G_CALLBACK (on_handle_sail_media_player_get_selected_audio_stream),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-player-select-audio-stream",
                      G_CALLBACK (on_handle_sail_media_player_select_audio_stream),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-player-audio-mute",
                      G_CALLBACK (on_handle_sail_media_player_audio_mute),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-player-get-volume",
                      G_CALLBACK (on_handle_sail_media_player_get_volume),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-player-set-volume",
                      G_CALLBACK (on_handle_sail_media_player_set_volume),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-player-is-audio-muted",
                      G_CALLBACK (on_handle_sail_media_player_is_audio_muted),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-is-media-recorder-available",
                      G_CALLBACK (on_handle_sail_is_media_recorder_available),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-recorder-start-asset-record",
                      G_CALLBACK (on_handle_sail_media_recorder_start_asset_record),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-recorder-stop-asset-record",
                      G_CALLBACK (on_handle_sail_media_recorder_stop_asset_record),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-recorder-delete-asset-record",
                      G_CALLBACK (on_handle_sail_media_recorder_delete_asset_record),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-recorder-get-asset-record-info",
                      G_CALLBACK (on_handle_sail_media_recorder_get_asset_record_info),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-recorder-start-timeshift",
                      G_CALLBACK (on_handle_sail_media_recorder_start_timeshift),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-recorder-stop-timeshift",
                      G_CALLBACK (on_handle_sail_media_recorder_stop_timeshift),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-recorder-get-number-of-assets",
                      G_CALLBACK (on_handle_sail_media_recorder_get_number_of_assets),
                      NULL);

    g_signal_connect (interface,
                      "handle-sail-media-recorder-get-asset-info",
                      G_CALLBACK (on_handle_sail_media_recorder_get_asset_info),
                      NULL);

    error = NULL;
    if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (interface),
                                           connection,
                                           "/org/Cisco/Sail",
                                           &error))
    {
      /* handle error */
    }
}


int main( int argc, char *argv[] )
{
    int c = 0;
    int foreground = 0;
    
    SailError ret = SAIL_ERROR_SUCCESS;
    GMainLoop *loop;
    //we are using printf so don't do buffering
    //
    setbuf(stdout, NULL);
    /* Argument handling */
    opterr = 0;

    while ((c = getopt (argc, argv, "f")) != -1)
    {
        switch (c)
        {
            case 'f':
                foreground = 1;
                break;
            default:
                fprintf( stderr, "I shouldn't be here.\n" );
        }
    }

    if ( foreground == 0 )
    {
        daemon( 0, 0 );
    }

    //TODO: put the gstreamer system init here.

    /* Setup SAIL Logging */
    //debugLogAddModule( &gDaemonLogHandle, "saildaemon" );
    //debugLogLevelSet( gDaemonLogHandle, DEBUG_LOG_LEVEL_INFO );

    /* DBUS Code */
    loop = g_main_loop_new( NULL, FALSE );

    g_type_init();
    guint id = g_bus_own_name( G_BUS_TYPE_SESSION,
                               "org.Cisco.Sail",
                               G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                               G_BUS_NAME_OWNER_FLAGS_REPLACE,
                               on_bus_acquired,
                               on_name_acquired,
                               on_name_lost,
                               loop,
                               NULL );

    g_main_loop_run( loop );

    g_bus_unown_name( id );
    g_main_loop_unref( loop );
    //TODO: shutdown gstreamer here.

#if 0
    appInputFinalize();
    appGfxFinalize();
    SailSystemFinalize();
#endif
    return 0;
}
