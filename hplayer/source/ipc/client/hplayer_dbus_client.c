#include <stdio.h>
#include <string.h>
#include <SailInfCommon.h>
//#include "xml/sail_dbus_api.h"
#include "hplayer_dbus_client_generated.h"
#include "saildbusclient.h"


#define dbus_check_error(error) \
do{\
    if (error)\
    {   printf ("%s,%d: Failed in the client call: %s\n", __FUNCTION__, __LINE__, error->message);\
        g_error_free (error);\
        return  SAIL_ERROR_FAILED;\
    }\
}while(0)

static OrgCiscoSail  *gproxy = NULL;
static MediaPlayerCallback gMPCallback = NULL;
static void *gUserParam = NULL;
static char gLang[10];
static tRecordInfo RecordInfo;
static gboolean on_handle_notification (OrgCiscoSail *proxy, gint event)
{
  if (proxy == gproxy)
  {
         gMPCallback((MediaEvent)event,gUserParam);
  }
  return true;
}

int SailDbusInterfaceOpen()
{
    GError *error = NULL;
    if (gproxy != NULL)
    {
     printf ("Already dbus proxy Opened \n");
     return 0; 
    }
    g_type_init();
    gproxy = org_cisco_sail_proxy_new_for_bus_sync(G_BUS_TYPE_SESSION,G_DBUS_PROXY_FLAGS_NONE,"org.Cisco.Sail","/org/Cisco/Sail",NULL,&error);
    if (error)
    {
     printf ("Failed in dbus proxy call: %s\n", error->message);
     g_error_free (error);
     return -1;
    }

    g_signal_connect (gproxy, "sail-media-player-notify", G_CALLBACK (on_handle_notification), NULL);


    return 0;
}

void SailDbusInterfaceClose()
{
    g_object_unref(gproxy);
    gproxy = NULL;
}

SailError MediaPlayerGetVideoProperties( int mediaPlayerId, VideoProperties* properties )
{
  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;

  org_cisco_sail_call_sail_media_player_get_video_properties_sync (gproxy,(gint)mediaPlayerId,(guint *)&properties->xRes,(guint *)&properties->yRes,
             (gint *)&properties->ar,(gint *)&properties->codec,(gint *)&sailErr,NULL,&error);

  dbus_check_error(error);

  return sailErr;
}


SailError MediaPlayerOpen( int mediaPlayerId, MediaPlayerCallback callback, void *userParam )
{
  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;

  if (gMPCallback != NULL)
  {
    printf("MediaPlayer is already opended \n");
    return SAIL_ERROR_FAILED;
  }
  gMPCallback = callback;
  gUserParam = userParam;

  org_cisco_sail_call_sail_media_player_open_sync (gproxy,(gint)mediaPlayerId,(gint *)&sailErr,NULL,&error);

  dbus_check_error(error);

  return sailErr;
}

SailError MediaPlayerClose( int mediaPlayerId )
{
  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;

  gMPCallback = NULL;
  gUserParam = NULL;

  org_cisco_sail_call_sail_media_player_close_sync (gproxy,(gint)mediaPlayerId,(gint *)&sailErr,NULL,&error);

  dbus_check_error(error);
  return sailErr;
}

SailError MediaPlayerSetSource( int mediaPlayerId, const char* source )
{
  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;
 
  org_cisco_sail_call_sail_media_player_set_source_sync(gproxy,(gint)mediaPlayerId,(const gchar *)source,(gint *)&sailErr,NULL,&error);

  dbus_check_error(error);
  return sailErr;
}

SailError MediaPlayerGetSource( int mediaPlayerId, char* source )
{
  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;
  gchar *src = NULL;
  org_cisco_sail_call_sail_media_player_get_source_sync(gproxy,(gint)mediaPlayerId,(gchar **)&src,(gint *)&sailErr,NULL,&error);

  dbus_check_error(error);
  if (sailErr == SAIL_ERROR_SUCCESS)
  {
    strcpy(source,src);
    g_free(src);
  }
  return sailErr;
}


SailError MediaPlayerCloseSource( int mediaPlayerId )
{

  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;

  org_cisco_sail_call_sail_media_player_close_source_sync(gproxy,(gint)mediaPlayerId,(gint *)&sailErr,NULL,&error);

  dbus_check_error(error);
  return sailErr;
}

SailError MediaPlayerSetPlaySpeed( int mediaPlayerId, float speed )
{

  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;
  gdouble local = speed;
  org_cisco_sail_call_sail_media_player_set_play_speed_sync(gproxy,(gint)mediaPlayerId,local,(gint *)&sailErr,NULL,&error);

  dbus_check_error(error);
  return sailErr;
}

SailError MediaPlayerGetPlaySpeed( int mediaPlayerId, float *speed )
{

  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;
  gdouble local;
  org_cisco_sail_call_sail_media_player_get_play_speed_sync(gproxy,(gint)mediaPlayerId,&local,(gint *)&sailErr,NULL,&error);

  dbus_check_error(error);
  *speed = (float)local;
  return sailErr;
}

#define MAX_DVR_SPEEDS     32
float gDvrSpeeds[MAX_DVR_SPEEDS];

SailError MediaPlayerGetSupportedSpeeds( int mediaPlayerId, float *speeds[] )
{
  int count=0;
  GVariant *value = NULL;
  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;

  org_cisco_sail_call_sail_media_player_get_supported_speeds_sync(gproxy,(gint)mediaPlayerId,&value,(gint *)&sailErr,NULL,&error);

  dbus_check_error(error);
  if (sailErr == SAIL_ERROR_SUCCESS)
  {
    GVariantIter *iter;
    g_variant_get (value, "ad", &iter);
    while (g_variant_iter_loop (iter, "d", &gDvrSpeeds[count]))
    {
      count++;
    }
    g_variant_iter_free (iter);
    g_variant_unref (value);
  }
  *speeds = gDvrSpeeds;
  return sailErr;
}

SailError MediaPlayerSeek( int mediaPlayerId, int64_t offset, int whence )
{

  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;

  org_cisco_sail_call_sail_media_player_seek_sync(gproxy,(gint)mediaPlayerId,(gint64)offset,(gint)whence,(gint *)&sailErr,NULL,&error);

  dbus_check_error(error);
  return sailErr;
}

SailError MediaPlayerPlaybackInfo( int mediaPlayerId, uint64_t* pActualPlayPosition,
                                   uint64_t* pSizeInMS, uint64_t* pSizeInByte,
                                   bool* pIsActiveRecording )
{

  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;

  org_cisco_sail_call_sail_media_player_playback_info_sync(gproxy,(gint)mediaPlayerId,(guint64 *)pActualPlayPosition,(guint64 *)pSizeInMS,(
         guint64 *)pSizeInByte,(gboolean *)pIsActiveRecording,(gint *)&sailErr,NULL,&error);

  dbus_check_error(error);
  return sailErr;

}

SailError MediaPlayerSetEnabled( int mediaPlayerId, bool enabled )
{

  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;

  org_cisco_sail_call_sail_media_player_set_enabled_sync(gproxy,(gint)mediaPlayerId,(gboolean)enabled,(gint *)&sailErr,NULL,&error);

  dbus_check_error(error);
  return sailErr;
}

SailError MediaPlayerIsEnabled( int mediaPlayerId, bool* enabled )
{

  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;

  org_cisco_sail_call_sail_media_player_is_enabled_sync(gproxy,(gint)mediaPlayerId,(gboolean*)&enabled,(gint *)&sailErr,NULL,&error);

  dbus_check_error(error);
  return sailErr;
}


SailError MediaPlayerVideoScale( int mediaPlayerId, Rectangle* srcRect, Rectangle* destRect )
{  
  GVariantBuilder *builder=NULL;
  GVariant *value1=NULL;
  GVariant *value2=NULL;
  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;
  if (destRect == NULL) 
  {
    printf("Destination Rect is NULL \n");
    return SAIL_ERROR_FAILED;
  }
 
  builder = g_variant_builder_new (G_VARIANT_TYPE ("ai"));
  if (srcRect != NULL)
  {
   g_variant_builder_add (builder, "i", srcRect->x);
   g_variant_builder_add (builder, "i", srcRect->y);
   g_variant_builder_add (builder, "i", srcRect->width);
   g_variant_builder_add (builder, "i", srcRect->height);
  }
  value1 = g_variant_new ("ai", builder); 
  g_variant_builder_unref (builder);

  builder = g_variant_builder_new (G_VARIANT_TYPE ("ai"));
  g_variant_builder_add (builder, "i", destRect->x);
  g_variant_builder_add (builder, "i", destRect->y);
  g_variant_builder_add (builder, "i", destRect->width);
  g_variant_builder_add (builder, "i", destRect->height);
  value2 = g_variant_new ("ai", builder); 
  g_variant_builder_unref (builder);

  org_cisco_sail_call_sail_media_player_video_scale_sync(gproxy,(gint)mediaPlayerId,value1,value2,(gint *)&sailErr,NULL,&error);
  
  g_variant_unref (value1);
  g_variant_unref (value2);

  dbus_check_error(error);
  return sailErr;
}

SailError MediaPlayerVideoFullScale( int mediaPlayerId, Rectangle* srcRect )
{
  GVariantBuilder *builder=NULL;
  GVariant *value=NULL;
  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;

  builder = g_variant_builder_new (G_VARIANT_TYPE ("ai"));
  g_variant_builder_add (builder, "i", srcRect->x);
  g_variant_builder_add (builder, "i", srcRect->y);
  g_variant_builder_add (builder, "i", srcRect->width);
  g_variant_builder_add (builder, "i", srcRect->height);
  value = g_variant_new ("ai", builder); 
  g_variant_builder_unref (builder);

  org_cisco_sail_call_sail_media_player_video_full_scale_sync(gproxy,(gint)mediaPlayerId,value,(gint *)&sailErr,NULL,&error);

  g_variant_unref (value);

  dbus_check_error(error);
  return sailErr;
}

SailError MediaPlayerGetNumberOfAudioStreams( int mediaPlayerId, int* pCount )
{

  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;

  org_cisco_sail_call_sail_media_player_get_number_of_audio_streams_sync(gproxy,(gint)mediaPlayerId,(gint*)pCount,(gint *)&sailErr,NULL,&error);

  dbus_check_error(error);
  return sailErr;
}


SailError MediaPlayerGetAudioStreamInfo( int mediaPlayerId, int audioIndex,
                                         char** ppAudioLanguage, int* pAudioType )
{   

  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;
  gchar *pLang = NULL;
  org_cisco_sail_call_sail_media_player_get_audio_stream_info_sync(gproxy,(gint)mediaPlayerId,(gint)audioIndex,(gchar**)&pLang,
               (gint *)pAudioType,(gint *)&sailErr,NULL,&error);

  dbus_check_error(error);
  if (sailErr == SAIL_ERROR_SUCCESS)
  {
    strcpy(gLang,pLang); 
    *ppAudioLanguage = gLang;
    g_free(pLang);
  }
  return sailErr;
}

SailError MediaPlayerGetSelectedAudioStream( int mediaPlayerId, int* pAudioIndex )
{   

  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;

  org_cisco_sail_call_sail_media_player_get_selected_audio_stream_sync(gproxy,(gint)mediaPlayerId,(gint *)pAudioIndex,(gint *)&sailErr,NULL,&error);

  dbus_check_error(error);
  return sailErr;
}


SailError MediaPlayerSelectAudioStream( int mediaPlayerId, int audioIndex )
{   

  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;

  org_cisco_sail_call_sail_media_player_select_audio_stream_sync(gproxy,(gint)mediaPlayerId,(gint)audioIndex,(gint *)&sailErr,NULL,&error);

  dbus_check_error(error);
  return sailErr;
}

SailError MediaPlayerAudioMute( bool enable ) 
{           

  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;

  org_cisco_sail_call_sail_media_player_audio_mute_sync(gproxy,(gboolean)enable,(gint *)&sailErr,NULL,&error);

  dbus_check_error(error);
  return sailErr;
}

SailError MediaPlayerGetVolume( int* left, int* right )
{   

  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;

  org_cisco_sail_call_sail_media_player_get_volume_sync(gproxy,(gint *)left,(gint *)right,(gint *)&sailErr,NULL,&error);

  dbus_check_error(error);
  return sailErr;
}

SailError MediaPlayerSetVolume( int left, int right )
{

  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;

  org_cisco_sail_call_sail_media_player_set_volume_sync(gproxy,(gint)left,(gint)right,(gint *)&sailErr,NULL,&error);

  dbus_check_error(error);
  return sailErr;
}

SailError MediaPlayerIsAudioMuted( bool* pIsAudioMuted )
{

  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;

  org_cisco_sail_call_sail_media_player_is_audio_muted_sync(gproxy,(gboolean *)pIsAudioMuted,(gint *)&sailErr,NULL,&error);

  dbus_check_error(error);
  return sailErr;
}

SailError IsMediaRecorderAvailable( bool* available )
{

  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;

  org_cisco_sail_call_sail_is_media_recorder_available_sync(gproxy,(gboolean *)available,(gint *)&sailErr,NULL,&error);

  dbus_check_error(error);
  return sailErr;
}

SailError MediaRecorderStartAssetRecord( const char* asset, char* source, int time_len_ms )
{  

  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;

  org_cisco_sail_call_sail_media_recorder_start_asset_record_sync(gproxy,(const gchar *)asset,(const gchar *)source,(gint)time_len_ms,(gint *)&sailErr,NULL,&error);

  dbus_check_error(error);
  return sailErr;
}

SailError MediaRecorderStopAssetRecord( const char* asset )
{  

  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;

  org_cisco_sail_call_sail_media_recorder_stop_asset_record_sync(gproxy,(const gchar *)asset,(gint *)&sailErr,NULL,&error);

  dbus_check_error(error);
  return sailErr;
}

SailError MediaRecorderDeleteAssetRecord( const char* asset )
{ 

  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;

  org_cisco_sail_call_sail_media_recorder_delete_asset_record_sync(gproxy,(const gchar *)asset,(gint *)&sailErr,NULL,&error);

  dbus_check_error(error);
  return sailErr;
}

SailError MediaRecorderGetAssetRecordInfo( const char* asset, uint64_t* pSizeInMS, uint64_t* pSizeInByte )
{  

  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;

  org_cisco_sail_call_sail_media_recorder_get_asset_record_info_sync(gproxy,(const gchar *)asset,(guint64 *)pSizeInMS,(guint64 *)pSizeInByte,(gint *)&sailErr,NULL,&error);

  dbus_check_error(error);
  return sailErr;
}

SailError MediaRecorderStartTimeshift( int mediaPlayerId, uint64_t bufferSizeInMs )
{ 

  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;

  org_cisco_sail_call_sail_media_recorder_start_timeshift_sync(gproxy,(gint)mediaPlayerId,(guint64)bufferSizeInMs,(gint *)&sailErr,NULL,&error);

  dbus_check_error(error);
  return sailErr;
}

SailError MediaRecorderStopTimeshift( int mediaPlayerId )
{ 

  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;

  org_cisco_sail_call_sail_media_recorder_stop_timeshift_sync(gproxy,(gint)mediaPlayerId,(gint *)&sailErr,NULL,&error);

  dbus_check_error(error);
  return sailErr;
}


SailError MediaRecorderGetNumberOfAssets( int* pCount )         
{

  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;

  org_cisco_sail_call_sail_media_recorder_get_number_of_assets_sync(gproxy, (gint *)pCount,(gint *)&sailErr,NULL,&error);

  dbus_check_error(error);
  return sailErr;
}

SailError MediaRecorderGetAssetInfo( int assetIndex, tRecordInfo **info )
{   

  GError *error = NULL;
  SailError sailErr = SAIL_ERROR_SUCCESS;
  gchar *ppath = NULL;

  org_cisco_sail_call_sail_media_recorder_get_asset_info_sync(gproxy,(gint)assetIndex,(guint *)&(RecordInfo.seconds),
                 (guint64 *)&(RecordInfo.size),(gchar**)&ppath,(gint *)&sailErr,NULL,&error);

  dbus_check_error(error);
  if (sailErr == SAIL_ERROR_SUCCESS)
  {
     strcpy(RecordInfo.path,ppath);
     g_free(ppath);
     *info = &RecordInfo;
  }
  return sailErr;
}

