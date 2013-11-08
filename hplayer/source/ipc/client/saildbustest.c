#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <SailInfCommon.h>
#include "hplayer_dbus_client.h"
#include "saildbusclient.h"
const char *strError[] =
{

   "SAIL_ERROR_SUCCESS",
   "SAIL_ERROR_FAILED",
   "SAIL_ERROR_NOT_IMPLEMENTED",
   "SAIL_ERROR_NOT_SUPPORTED",
   "SAIL_ERROR_BAD_PARAM",
   "SAIL_ERROR_OUT_OF_MEMORY",
   "SAIL_ERROR_OUT_OF_HANDLES",
   "SAIL_ERROR_TIMEOUT",
   "SAIL_ERROR_INVALID_HANDLE",
   "SAIL_ERROR_NOT_INITIALIZED",
   "SAIL_ERROR_NOT_OPEN",
   "SAIL_ERROR_NOT_ACTIVE",
   "SAIL_ERROR_NOT_READY",
   "SAIL_ERROR_NOT_CONNECTED",
   "SAIL_ERROR_WRONG_STATE"


};
#define DUMP_ERROR(err)    \
   do\
      {  \
         printf("%s\n", strError[err]);  \
      }  \
      while(0) 
static GMainLoop *loop = NULL;
static void mediaPlayerCallback( MediaEvent event, void* userParam );

static void play(char *src)
{
   SailError retCode = SAIL_ERROR_SUCCESS;

     if(MediaPlayerOpen(0,mediaPlayerCallback,NULL) != SAIL_ERROR_SUCCESS)
     {
        printf("MediaPlayer Open Failed \n");
        return;
     }
     retCode =MediaPlayerSetEnabled(0,true);
     if (retCode != SAIL_ERROR_SUCCESS)
     {
        printf("MediaPlayerSetEnabled failed\n");
        DUMP_ERROR(retCode);
        return;
     }
     retCode = MediaPlayerSetSource(0,src);
     if (retCode != SAIL_ERROR_SUCCESS)
     {
        printf("MediaPlayerSetSource failed\n");
        DUMP_ERROR(retCode);
        return;
     }
     loop = g_main_loop_new (NULL, FALSE);
     g_main_loop_run (loop);
     g_main_loop_unref (loop);
}

static void stop(void)
{
     MediaPlayerCloseSource(0);
     MediaPlayerSetEnabled(0,false);
     MediaPlayerClose(0);
}

typedef enum {
VOL_UP,
VOL_DOWN
}tVol;

static void volume(tVol vol)
{
  int right, left;
  MediaPlayerGetVolume( &left, &right );
  if (vol == VOL_UP)
  {
     left++;right++;
  }
  else if (vol == VOL_DOWN)
  {
     if ( right > 0 ) right--;
     if ( left > 0 ) left--;
  }
  printf("Setting volume: left: %d right: %d\n", left, right);
  MediaPlayerSetVolume( left, right );
}

static void mute(bool set)
{
  bool muted = false;
  MediaPlayerIsAudioMuted(&muted);
  if ((muted == set) && (set == false))
  {
    printf("Volume already unmuted \n");
  }
  else if ((muted == set) && (set == true))
  {
    printf("Volume already muted \n");
  }
  else if (set == true)
  {
    MediaPlayerAudioMute(set);
    printf("Volume muted \n");
  }
  else if (set == false)
  {
    MediaPlayerAudioMute(set);
    printf("Volume unmuted \n");
  }
}

static void AVInfo(void)
{
    char source [1024];
    VideoProperties props;
    int langCnt = 0;
    int type= -1;
    char *language=NULL;
    int count=0;int selected = -1;
    if ( MediaPlayerGetVideoProperties(0, &props) == SAIL_ERROR_SUCCESS )
    {
       printf("Video Property: xRes:  %u yRes: %u Aspect: %d Codec: %d \n", props.xRes, props.yRes, (int)props.ar, (int)props.codec);
    }
    memset(source,0,sizeof(source));
    if (MediaPlayerGetSource(0,source) == SAIL_ERROR_SUCCESS )
    {
      printf("Video Source: %s \n",source);
    }
    if ( MediaPlayerGetNumberOfAudioStreams(0, &langCnt) == SAIL_ERROR_SUCCESS )
    {
      printf("Audio Language Count: %d \n",langCnt);
      while(count < langCnt)
      {
       if ( MediaPlayerGetAudioStreamInfo(0, count, &language, &type) == SAIL_ERROR_SUCCESS )
       {
         printf("Audio Index: %d language : %s type: %d\n", count, language, type);
         g_free(language);
       }
       count++;
     }
    }
    if (MediaPlayerGetSelectedAudioStream(0, &selected) == SAIL_ERROR_SUCCESS )
    {
       printf("Audio Stream Selected (Index) : %d \n",selected);
    } 
}

static void PlayerInfo(void)
{
    float  speed = 0;
    float *speeds = NULL;
    bool enabled = true;
    int count=0;
    if (MediaPlayerGetPlaySpeed(0, &speed) == SAIL_ERROR_SUCCESS )
    {
      printf ("Player Speed: %f \n",speed);
    }
    if (MediaPlayerIsEnabled(0, &enabled) == SAIL_ERROR_SUCCESS )
    {
      if (enabled)
        printf("Player status: Enabled \n");
      else
        printf("Player status: Disabled \n");
    }
    if (MediaPlayerGetSupportedSpeeds(0,&speeds) == SAIL_ERROR_SUCCESS )
    {
       printf("Player Speeds supported: ");
       while(speeds[count] != 0.0)
       {
         printf(" %f",speeds[count]);
         count++;
       } 
       printf("\n");
    }
}

static void mediaPlayerCallback( MediaEvent event, void* userParam)
{
  userParam = 0;
  printf("Media Player Event Recevied : %d \n",event);
  g_main_loop_quit (loop);
}

static void RecordStart(char *asset, char *url)
{
   if (MediaRecorderStartAssetRecord(asset,url,0) != SAIL_ERROR_SUCCESS)
   {
      printf("Record Start Failed \n");
   }
}
 
static void RecordStop(void)
{
   tRecordInfo *info;
   if ( MediaRecorderGetAssetInfo(0, &info) == SAIL_ERROR_SUCCESS )
   {
      if (MediaRecorderStopAssetRecord(info->path) != SAIL_ERROR_SUCCESS )
      {
         printf("Unable to Stop recording %s \n",info->path);
      }
   }
}

static void RecordList(void) 
{
   int count = 0;
   if ( MediaRecorderGetNumberOfAssets(&count) == SAIL_ERROR_SUCCESS )
   {
      tRecordInfo *info;
      if ( count > 0 )
      {
         int idx = 0;
         for ( idx = 0; idx < count; idx++ )
         {
            if ( MediaRecorderGetAssetInfo(idx, &info) == SAIL_ERROR_SUCCESS)
            {
               printf("%02d. %-30s\t( size: %05llu MB %04d seconds )\n",
                      idx, info->path, (info->size / 1000000), info->seconds);
            }
         }
      }
      else
      {
         printf("No assets found.\n");
      }
   }
}

static void RecordDel(char *asset)
{
  char path[256];
  snprintf(path,sizeof(path),"/video/%s",asset);
  if (MediaRecorderDeleteAssetRecord(path) != SAIL_ERROR_SUCCESS)
  {
       printf("Record Deleted Failed for asset %s \n",asset);
  }
}
static void RecordInfo(char *asset)
{
  char path[256];
  uint64_t pSizeInMS, pSizeInByte;
  snprintf(path,sizeof(path),"/video/%s",asset);
  if (MediaRecorderGetAssetRecordInfo(path,&pSizeInMS, &pSizeInByte) == SAIL_ERROR_SUCCESS)
  {
    printf("asset: %s size in ms: %llu size in bytes: %llu\n", asset, pSizeInMS, pSizeInByte);
  }
}

int main(int argc, char **argv)
{
  if (argc < 2)
  {
      printf("usage: %s play <url> | stop | vol+ | vol- | mute | unmute | avinfo | playerinfo | rec_start <url> <asset-name> | rec_stop | rec_list | rec_del <asset-name> | rec_info <asset-name> \n", argv[0]);
      return -1;
  }

  if (SailDbusInterfaceOpen() != 0)
  {
     printf("Sail DBus attached failed \n");
     return -1;
  } 
  if (strcmp(argv[1],"play") == 0)
  {
     play(argv[2]);
  }
  else if (strcmp(argv[1],"stop") == 0)
  {
     stop();
  }
  else if (strcmp(argv[1],"vol+") == 0)
  {
     volume(VOL_UP);
  }
  else if (strcmp(argv[1],"vol-") == 0)
  {
     volume(VOL_DOWN);
  }
  else if (strcmp(argv[1],"mute") == 0)
  {
     mute(true);
  }
  else if (strcmp(argv[1],"unmute") == 0)
  {
     mute(false);
  }
  else if (strcmp(argv[1],"avinfo") == 0)
  {
     AVInfo();
  }
  else if (strcmp(argv[1],"playerinfo") == 0)
  {
     PlayerInfo();
  }
  else if (strcmp(argv[1],"rec_start") == 0)
  {
    RecordStart(argv[3],argv[2]); 
  }
  else if (strcmp(argv[1],"rec_stop") == 0)
  {
    RecordStop();
  }
  else if (strcmp(argv[1],"rec_list") == 0)
  {
    RecordList();
  }
  else if (strcmp(argv[1],"rec_del") == 0)
  {
    RecordDel(argv[2]);
  }
  else if (strcmp(argv[1],"rec_info") == 0)
  {
    RecordInfo(argv[2]);
  }

  SailDbusInterfaceClose();
  return 0;
}
