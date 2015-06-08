#ifdef HAVE_CONFIG_H
# include <config.h>
#endif


#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gst/gst.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <gst/app/gstappsink.h>
#ifdef USE_DRMPROXY
   #include <drmProxy.h>
   #include <drmProxy_vgdrm.h>
#endif
#include "cgmiPlayerApi.h"
#include "cgmi-priv-player.h"
#include "cgmi-section-filter-priv.h"
#include "cgmiDiagsApi.h"
#include "cgmi-diags-priv.h"

#define MAGIC_COOKIE 0xDEADBEEF
GST_DEBUG_CATEGORY_STATIC (cgmi);
#define GST_CAT_DEFAULT cgmi

#define INVALID_INDEX                  -2
#define INVALID_PID                    (-1)

#define PTS_FLUSH_THRESHOLD            (10 * 45000) //10 secs
#define PTS_ERROR_COUNT_THRESHOLD      20
#define VIDEO_DECODE_DROPS_THRESHOLD   20
#define VIDEO_DECODE_ERRORS_THRESHOLD  20
#define ERROR_WINDOW_SIZE              5
#define STEADY_STATE_WINDOW_SIZE       5

#define DEFAULT_BLOCKSIZE              65536  //Large buffers increase temporary memory pressure since they may
                                              //get queued up in the demux. It also increases channel change time.
                                              //Therefore, don't set this to more than needed

#define GST_DEBUG_STR_MAX_SIZE         256

#define VGDRM_CAPS   "application/x-vgdrm-live-client"

static gboolean cgmi_gst_handle_msg( GstBus *bus, GstMessage *msg, gpointer data );
static GstElement *cgmi_gst_find_element( GstBin *bin, gchar *ename );

static gchar gDefaultAudioLanguage[4];
static gchar gDefaultSubtitleLanguage[4];

static GList *gSessionList = NULL;
static GMutex gSessionListMutex;

static int  cgmi_CheckSessionHandle(tSession *pSess)
{
   if (NULL == pSess || (int)pSess->cookie != MAGIC_COOKIE)
   {
      GST_ERROR("The pointer to the session handle is invalid!!!!\n");
      return FALSE;
   }
   return TRUE;

}

static GstStateChangeReturn cisco_gst_setState( tSession *pSess, GstState state )
{
   GstStateChangeReturn sret;

   sret = gst_element_set_state( pSess->pipeline, state );
   switch ( sret )
   {
      case GST_STATE_CHANGE_FAILURE:
         GST_WARNING("Set %d State Failure\n", state);
         break;
      case GST_STATE_CHANGE_NO_PREROLL:
         GST_WARNING("Set %d State No Preroll\n", state);
         break;
      case GST_STATE_CHANGE_ASYNC:
         GST_WARNING("Set %d State Async\n", state);
         break;
      case GST_STATE_CHANGE_SUCCESS:
         GST_WARNING("Set %d State Succeeded\n", state);
         break;
      default:
         GST_WARNING("Set %d State Unknown\n", state);
         break;
   }

   return sret;
}

static GstState cisco_gst_getState( tSession *pSess )
{
   GstStateChangeReturn sret;
   GstState state = GST_STATE_NULL;

   if ( NULL != pSess->pipeline )
   {
      sret = gst_element_get_state( pSess->pipeline, &state, NULL, GST_SECOND );
      if ( GST_STATE_CHANGE_SUCCESS != sret )
         state = GST_STATE_NULL;
   }

   return state;
}

static void cgmi_flush_pipeline(tSession *pSess)
{
   gboolean ret = FALSE;
   GstEvent *flushStart = NULL;
   GstEvent *flushStop = NULL;

   GST_INFO("%s()\n", __FUNCTION__);

   do 
   {
      if(NULL == pSess)
      {
         GST_ERROR("Session param is NULL\n");
         break;
      }

      if(NULL != pSess->pipeline)
      {
         flushStart = gst_event_new_flush_start();
#if GST_CHECK_VERSION(1,0,0)
         flushStop = gst_event_new_flush_stop(FALSE);
#else
         flushStop = gst_event_new_flush_stop();
#endif

         ret = gst_element_send_event(GST_ELEMENT(pSess->pipeline), flushStart);
         if(FALSE == ret)
         {
            GST_ERROR("Failed to send flush start event!\n");
         }

         ret = gst_element_send_event(GST_ELEMENT(pSess->pipeline), flushStop);
         if(FALSE == ret)
         {
            GST_ERROR("Failed to send flush stop event\n"); 
         }
      }
   }while(0);
   
   return;
}

static gboolean isRateSupported(void *pSession, float rate)
{
   cgmi_Status stat = CGMI_ERROR_FAILED;
   float *rates_array = NULL;
   unsigned int numRates = 32;
   gint ii = 0;
   gboolean rateSupported = FALSE;

   do {
      rates_array = g_malloc0(sizeof(float) * numRates);
      if(NULL == rates_array)
      {
         GST_ERROR("Failed to malloc rates array\n");
         break;
      }

      stat = cgmi_GetRates(pSession, rates_array, &numRates);
      if(stat != CGMI_ERROR_SUCCESS)
      {
         GST_ERROR("cgmi_GetRates() failed\n");
         break;
      }

      for(ii = 0; ii < numRates; ii++)
      {
         if(rate == rates_array[ii])
         {
            rateSupported = TRUE;
            break;
         }
      }

   }while(0);
   
   if(NULL != rates_array)
   {
      g_free(rates_array);
      rates_array = NULL;
   }
  
   return rateSupported;
}

static cgmi_Status cgmi_queryDiscreteAudioInfo(tSession *pSess)
{
   cgmi_Status  stat = CGMI_ERROR_SUCCESS;
   gboolean     ret = FALSE;
   GstStructure *structure = NULL;
   GstQuery     *query = NULL;
   gchar        *muxedLangISO = NULL;
   gchar        *discreteLangISO = NULL;
   gchar        **strArr = NULL;
   gchar        **walk = NULL;

   do
   {
      if(cgmi_CheckSessionHandle(pSess) == FALSE)
      {
         GST_ERROR("Invalid session handle\n");
         stat = CGMI_ERROR_INVALID_HANDLE;
         break;
      }
      if(FALSE == pSess->bQueryDiscreteAudioInfo)
      {
         break;
      }

      /* Query the pipeline for info about discrete audio streams */
      structure = gst_structure_new ("getAudioLangInfo", 
            "commaSepMuxedLangISO", G_TYPE_STRING, NULL, 
            "commaSepDiscreteLangISO", G_TYPE_STRING, NULL, 
            NULL);

#if GST_CHECK_VERSION(1,0,0)
      query = gst_query_new_custom (GST_QUERY_CUSTOM, structure);
#else
      query = gst_query_new_application (GST_QUERY_CUSTOM, structure);
#endif

      ret = gst_element_query (pSess->pipeline, query);
      if(ret)
      {
         muxedLangISO = gst_structure_get_string(structure, "commaSepMuxedLangISO");
         GST_INFO("Comma Separted muxed audio languages: %s\n", muxedLangISO);

         discreteLangISO = gst_structure_get_string(structure, "commaSepDiscreteLangISO");
         GST_INFO("Comma Separted discrete audio languages: %s\n", discreteLangISO);

         /* Are there language descriptors for the muxed audio languages? If no, add
          * the muxed audio languages to the list */
         /* TODO - check for duplicates */
         if(0 == pSess->numAudioLanguages)
         {
            strArr = g_strsplit(muxedLangISO, ",", -1);
            walk = strArr;
            while((walk) && (*walk) && (pSess->numAudioLanguages < MAX_AUDIO_LANGUAGE_DESCRIPTORS))
            {
               if(pSess->numAudioLanguages >= 1)
               {
                  GST_ERROR("There is more than one muxed audio stream without language descriptor\n");
               }
               GST_DEBUG("Muxed Audio Lang ISO: %s\n", *walk);
               g_strlcpy(pSess->audioLanguages[pSess->numAudioLanguages].isoCode, *walk,
                     sizeof(pSess->audioLanguages[pSess->numAudioLanguages].isoCode));
               pSess->audioLanguages[pSess->numAudioLanguages].index = INVALID_INDEX;
               pSess->audioLanguages[pSess->numAudioLanguages].streamType = STREAM_TYPE_AUDIO;
               pSess->audioLanguages[pSess->numAudioLanguages].pid = INVALID_PID;
               pSess->audioLanguages[pSess->numAudioLanguages].bDiscrete = FALSE;
               pSess->numAudioLanguages++;
               walk++;
            }
            g_strfreev(strArr);
            strArr = NULL;
         }

         strArr = g_strsplit(discreteLangISO, ",", -1);
         walk = strArr;
         while((walk) && (*walk) && (pSess->numAudioLanguages < MAX_AUDIO_LANGUAGE_DESCRIPTORS))
         {
            GST_DEBUG("Discrete Audio Lang ISO: %s\n", *walk);
            g_strlcpy(pSess->audioLanguages[pSess->numAudioLanguages].isoCode, *walk,
                  sizeof(pSess->audioLanguages[pSess->numAudioLanguages].isoCode));
            pSess->audioLanguages[pSess->numAudioLanguages].index = INVALID_INDEX;
            pSess->audioLanguages[pSess->numAudioLanguages].streamType = STREAM_TYPE_AUDIO;
            pSess->audioLanguages[pSess->numAudioLanguages].pid = INVALID_PID;
            pSess->audioLanguages[pSess->numAudioLanguages].bDiscrete = TRUE;
            pSess->numAudioLanguages++;
            walk++;
         }
         g_strfreev(strArr);
         strArr = NULL;
      }

      pSess->bQueryDiscreteAudioInfo = FALSE;

   }while(0);

   if( NULL != query )
   {
      gst_query_unref ( query );
      query = NULL;
   }

   return stat;
}

static void cgmi_GetHwDecHandles(tSession *pSess)
{
   if(NULL != pSess->videoDecoder)
   {
      g_object_get(pSess->videoDecoder, "videodecoder", &pSess->hwVideoDecHandle, NULL);
   }

   if(NULL != pSess->audioDecoder)
   {
      g_object_get(pSess->audioDecoder, "audiodecoder", &pSess->hwAudioDecHandle, NULL);
   }

   GST_INFO("hwVideoDecHandle = %p, hwAudioDecHandle = %p\n", pSess->hwVideoDecHandle,
         pSess->hwAudioDecHandle);

   return;
}

void debug_cisco_gst_streamDurPos( tSession *pSess )
{

   gint64 curPos = 0, curDur = 0;
   GstFormat gstFormat = GST_FORMAT_TIME;

#if GST_CHECK_VERSION(1,0,0)
   gst_element_query_position( pSess->pipeline, gstFormat, &curPos );
   gst_element_query_duration( pSess->pipeline, gstFormat, &curDur );
#else
   gst_element_query_position( pSess->pipeline, &gstFormat, &curPos );
   gst_element_query_duration( pSess->pipeline, &gstFormat, &curDur );
#endif

   GST_INFO("Stream: %s\n", pSess->playbackURI );
   GST_INFO("Position: %lld (seconds)\n", (curPos/GST_SECOND), gstFormat );
   GST_INFO("Duration: %lld (seconds)\n", (curDur/GST_SECOND), gstFormat );

}

gpointer cgmi_monitor( gpointer data )
{
   gint64 videoPts, audioPts, endTime;
   guint videoPtsErrors;
   guint videoDecodeDrops;
   guint videoDecodeErrors;
   guint audioPtsErrors;

   guint videoPtsErrorsPrev = 0;
   guint videoDecodeDropsPrev = 0;
   guint videoDecodeErrorsPrev = 0;
   guint audioPtsErrorsPrev = 0;
   guint errorWindow = 0;

   gboolean flushDone = FALSE;
   tSession *pSess = (tSession*)data;
   if ( NULL == pSess )
      return NULL;

   while( pSess->runMonitor )
   {
      if ( cisco_gst_getState(pSess) == GST_STATE_PLAYING && pSess->rate == 1.0 ) 
      {         
         videoPts = 0;
         audioPts = 0;

         if ( NULL != pSess->videoDecoder )
         {
            g_object_get( pSess->videoDecoder, 
                          "video_pts", &videoPts, 
                          "video_pts_errors", &videoPtsErrors,
                          "video_decode_errors", &videoDecodeErrors,
                          "video_decode_drops", &videoDecodeDrops, 
                          NULL);
         }
         if ( NULL != pSess->audioDecoder )
         {
            g_object_get( pSess->audioDecoder, 
                          "audio_pts", &audioPts, 
                          "audio_pts_errors", &audioPtsErrors,
                          NULL );
         }

         //During seeks or transition between trick modes, the monitored counters
         //may have spikes which may cause spurious errors. Therefore, we need monitoring 
         //to take action only during steady 1x playback state
         if ( TRUE == pSess->steadyState )
         {
            flushDone = FALSE;
            if ( videoPts > 0 && audioPts > 0 )
            {
               if ( videoPts - audioPts > PTS_FLUSH_THRESHOLD || videoPts - audioPts < -PTS_FLUSH_THRESHOLD )
               {
                  g_print("Flushing buffers due to large audio-video PTS difference...\n");
                  g_print("videoPts = %lld, audioPts = %lld, diff = %lld\n", videoPts, audioPts, videoPts - audioPts);

                  cgmi_flush_pipeline( pSess );
                  flushDone = TRUE;
                  //reset error window
                  videoPtsErrorsPrev = videoPtsErrors;
                  audioPtsErrorsPrev = audioPtsErrors;
                  videoDecodeDropsPrev = videoDecodeDrops;
                  videoDecodeErrorsPrev = videoDecodeErrors;
                  errorWindow = 0;

                  //A flush may also cause transient unstability
                  pSess->steadyState = FALSE;
                  pSess->steadyStateWindow = 0;
               }
            }
            if ( FALSE == flushDone )
            {
               if ( errorWindow > ERROR_WINDOW_SIZE )
               {
                  //error monitor window reached max size, reset it and resample counters
                  videoPtsErrorsPrev = videoPtsErrors;
                  audioPtsErrorsPrev = audioPtsErrors;
                  videoDecodeDropsPrev = videoDecodeDrops;
                  videoDecodeErrorsPrev = videoDecodeErrors;
                  errorWindow = 0;
               }

               if ( (gint)videoPtsErrors - (gint)videoPtsErrorsPrev > PTS_ERROR_COUNT_THRESHOLD ||
                    (gint)audioPtsErrors - (gint)audioPtsErrorsPrev > PTS_ERROR_COUNT_THRESHOLD ||
                    (gint)videoDecodeDrops - (gint)videoDecodeDropsPrev > VIDEO_DECODE_DROPS_THRESHOLD ||
                    (gint)videoDecodeErrors - (gint)videoDecodeErrorsPrev > VIDEO_DECODE_ERRORS_THRESHOLD )
               {
                  g_print("Decoder error threshold reached within %d ms", errorWindow * 1000);

                  g_print("videoPtsErrors = %u (prev = %u, diff = %u)\n", videoPtsErrors, videoPtsErrorsPrev, videoPtsErrors - videoPtsErrorsPrev);               
                  g_print("videoDecodeErrors = %u (prev = %u, diff = %u)\n", videoDecodeErrors, videoDecodeErrorsPrev, videoDecodeErrors - videoDecodeErrorsPrev);
                  g_print("videoDecodeDrops = %u (prev = %u, diff = %u)\n", videoDecodeDrops, videoDecodeDropsPrev, videoDecodeDrops - videoDecodeDropsPrev);
                  g_print("audioPtsErrors = %u (prev = %u, diff = %u)\n", audioPtsErrors, audioPtsErrorsPrev, audioPtsErrors - audioPtsErrorsPrev);
                  
                  g_print("Flushing buffers due to decode errors...\n");
                  pSess->eventCB(pSess->usrParam, (void*)pSess, NOTIFY_DECODE_ERROR, 0);
                  cgmi_flush_pipeline( pSess );
                  flushDone = TRUE;
                  //reset error window
                  videoPtsErrorsPrev = videoPtsErrors;
                  audioPtsErrorsPrev = audioPtsErrors;
                  videoDecodeDropsPrev = videoDecodeDrops;
                  videoDecodeErrorsPrev = videoDecodeErrors;
                  errorWindow = 0;

                  //A flush may also cause transient unstability
                  pSess->steadyState = FALSE;
                  pSess->steadyStateWindow = 0;
               }

               if ( videoPtsErrors > videoPtsErrorsPrev )
                  g_print("videoPtsErrors = %u (prev = %u, diff = %u)\n", videoPtsErrors, videoPtsErrorsPrev, videoPtsErrors - videoPtsErrorsPrev);
               if ( audioPtsErrors > audioPtsErrorsPrev )
                  g_print("audioPtsErrors = %u (prev = %u, diff = %u)\n", audioPtsErrors, audioPtsErrorsPrev, audioPtsErrors - audioPtsErrorsPrev);
               if ( videoDecodeDrops > videoDecodeDropsPrev )
                  g_print("videoDecodeDrops = %u (prev = %u, diff = %u)\n", videoDecodeDrops, videoDecodeDropsPrev, videoDecodeDrops - videoDecodeDropsPrev);
               if ( videoDecodeErrors > videoDecodeErrorsPrev )
                  g_print("videoDecodeErrors = %u (prev = %u, diff = %u)\n", videoDecodeErrors, videoDecodeErrorsPrev, videoDecodeErrors - videoDecodeErrorsPrev);
            }

            //Increment error monitor window
            errorWindow++;
         }
         else if ( FALSE == pSess->steadyState )
         {
            if ( pSess->steadyStateWindow > STEADY_STATE_WINDOW_SIZE )
            {
               g_print("Steady state reached, enabling CGMI error monitor...\n");
               pSess->steadyStateWindow = 0;
               pSess->steadyState = TRUE;
               //reset error window
               videoPtsErrorsPrev = videoPtsErrors;
               audioPtsErrorsPrev = audioPtsErrors;
               videoDecodeDropsPrev = videoDecodeDrops;
               videoDecodeErrorsPrev = videoDecodeErrors;
               errorWindow = 0;
            }
            else if ( 0 == pSess->steadyStateWindow )
            {
               g_print("Transient state, disabling CGMI error monitor for %d ms...\n", STEADY_STATE_WINDOW_SIZE * 1000 );
            }

            //Increment steady state window
            pSess->steadyStateWindow++;
         }
      }

      // Wait until either cond is signalled or end_time has passed. For handling a spurious wakeup, 
      // retry g_cond_wait_until() until runMonitor becomes false or break if end_time expires
      g_mutex_lock(&pSess->monThreadMutex);
      endTime = g_get_monotonic_time () + G_TIME_SPAN_SECOND;
      while ( pSess->runMonitor )
      {
         if ( FALSE == g_cond_wait_until(&pSess->monThreadCond, &pSess->monThreadMutex, endTime) )
         {
            // timeout occurred
            break;
         }
      }
      g_mutex_unlock(&pSess->monThreadMutex);
   }
}

static void cgmi_gst_delayed_seek( tSession *pSess )
{
   GST_INFO("Executing delayed seek...\n");

   pSess->steadyState = FALSE;
   pSess->steadyStateWindow = 0;

   if( !gst_element_seek( pSess->pipeline,
            1.0,
            GST_FORMAT_TIME,
            GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
            GST_SEEK_TYPE_SET,
            (gint64)(pSess->pendingSeekPosition * GST_SECOND),
            GST_SEEK_TYPE_NONE,
            GST_CLOCK_TIME_NONE ))
   {
      GST_WARNING("Seek to Resume Position Failed\n");
   }
   else
   {
      GstState curState;
      //wait for the seek to complete
      gst_element_get_state(pSess->pipeline, &curState, NULL, GST_CLOCK_TIME_NONE);
      GST_INFO("Seek to Resume Position Succeeded\n");
   }
}

static gboolean cgmi_gst_handle_msg( GstBus *bus, GstMessage *msg, gpointer data )
{
   tSession *pSess = (tSession*)data;
   GstStructure *structure;

   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      return FALSE;
   }

   switch( GST_MESSAGE_TYPE(msg) )
   {
      GST_INFO("Got bus message of type: %s\n", GST_MESSAGE_SRC_NAME(msg));

      case GST_MESSAGE_ASYNC_DONE:
         GST_INFO("Async Done message\n");
         //pSess->eventCB(pSess->usrParam, (void*)pSess, NOTIFY_SEEK_DONE, 0);
         break;

      case GST_MESSAGE_EOS:
         GST_INFO("End of Stream\n");
         pSess->eventCB(pSess->usrParam, (void*)pSess, NOTIFY_END_OF_STREAM,0);
         break;

      case GST_MESSAGE_ELEMENT:
      // these are cisco added events.
#if GST_CHECK_VERSION(1,0,0)
         structure = (GstStructure *)gst_message_get_structure(msg);
#else
         structure = msg->structure;
#endif
         if( structure && gst_structure_has_name(structure, "extended_notification"))
         {
            //const GValue *ntype;
            const gchar *ntype;
            //figure out which extended notification that we have recieved.
            GST_INFO("RECEIVED extended notification message\n");
            ntype = gst_structure_get_string(structure, "notification");
            if (NULL == ntype)
            {
               GST_ERROR("Null notification!\n");
            }
            if (0 == strcmp(ntype, "first_pts_received"))
            {
               GST_INFO("RECEIVED first_pts_received\n");
            }
            else if (0 == strcmp(ntype, "first_pts_decoded"))
            {
               GST_INFO("RECEIVED first_pts_decoded\n");
               if ( TRUE == pSess->pendingSeek )
               {
                  cgmi_gst_delayed_seek( pSess );
               }

               if ( FALSE == pSess->pendingSeek )
               {
                  if( NULL != pSess->videoDecoder )
                  {
                     g_print("Unmuting video decoder...\n");
                     g_object_set( G_OBJECT(pSess->videoDecoder), "decoder_mute", FALSE, NULL );
                  }

                  if( NULL != pSess->audioDecoder && pSess->rate == 1.0 )
                  {
                     g_print("Unmuting audio decoder...\n");
                     g_object_set( G_OBJECT(pSess->audioDecoder), "decoder_mute", FALSE, NULL );
                  }

                  cgmiDiag_addTimingEntry(DIAG_TIMING_METRIC_PTS_DECODED, pSess->diagIndex, pSess->playbackURI, 0);
                  pSess->eventCB(pSess->usrParam, (void*)pSess, NOTIFY_FIRST_PTS_DECODED, 0 );
               }
               else
                  pSess->pendingSeek = FALSE;
            }
            else if (0 == strcmp(ntype, "first_audio_frame_found"))
            {
               GST_INFO("RECEIVED first_audio_frame_found\n");
               if ( TRUE == pSess->noVideo )
               {
                  if ( TRUE == pSess->pendingSeek )
                  {
                     cgmi_gst_delayed_seek( pSess );
                  }

                  if ( FALSE == pSess->pendingSeek )
                  {
                     if( NULL != pSess->audioDecoder && pSess->rate == 1.0 )
                     {
                        g_print("Unmuting audio decoder...\n");
                        g_object_set( G_OBJECT(pSess->audioDecoder), "decoder_mute", FALSE, NULL );
                     }

                     cgmiDiag_addTimingEntry(DIAG_TIMING_METRIC_PTS_DECODED, pSess->diagIndex, pSess->playbackURI, 0);
                     pSess->eventCB(pSess->usrParam, (void*)pSess, NOTIFY_FIRST_PTS_DECODED, 0 );
                  }
                  else
                     pSess->pendingSeek = FALSE;
               }
            }
            else if (0 == strcmp(ntype, "stream_attrib_changed"))
            {
               gint width, height;
               gint numerator, denominator;
               uint64_t eventVal;
               if (gst_structure_get_int(structure, "src_width", &width) == FALSE)
               {
                  GST_WARNING("Failed to get video source widht, returning 0");
                  width = 0;
               }
               if (gst_structure_get_int(structure, "src_height", &height) == FALSE)
               {
                  GST_WARNING("Failed to get video source height, returning 0");
                  height = 0;
               }
               if (gst_structure_get_int(structure, "numerator", &numerator) == FALSE)
               {
                   GST_WARNING("Failed to get numerator, returning 1,1");
                   numerator = denominator = 1;
               }
               else
               {
                   if (gst_structure_get_int(structure, "denominator", &denominator) == FALSE)
                   {
                       GST_WARNING("Failed to get denominator, returning 1,1");
                       denominator = numerator = 1;
                   }
               }
               eventVal = (((uint64_t)width) << 48) | (((uint64_t)height) << 32) | (((uint64_t)numerator) << 16) | denominator;
               pSess->eventCB(pSess->usrParam, (void*)pSess, NOTIFY_VIDEO_RESOLUTION_CHANGED, eventVal);
            }
            else if (0 == strcmp(ntype, "rate_changed"))
            {
                gint rate;
                GST_INFO("RECEIVED rate_changed\n");
                if (gst_structure_get_int(structure, "rate", &rate) == FALSE)
                {
                    GST_WARNING("Failed to get new rate, returning 0");
                    rate = 0;
                }
                if(!((TRUE == pSess->maskRateChangedEvent) && (rate != pSess->rateAfterPause)))
                {
                   pSess->eventCB(pSess->usrParam, (void*)pSess, NOTIFY_CHANGED_RATE, (uint64_t)rate );
                }
                pSess->maskRateChangedEvent = FALSE;
                pSess->rateAfterPause = 0.0;
            }
            else if (0 == strcmp(ntype, "tsb_start_near_pause_position"))
            {
               GST_INFO("RECEIVED tsb_start_near_pause_position - sending EOS to app");
               pSess->eventCB(pSess->usrParam, (void*)pSess, NOTIFY_END_OF_STREAM, 0);
            }
            /* BOF/BOS/EOF should be treated as EOS since gstreamer treats all these conditions EOS */
#if 0
            else if (0 == strcmp(ntype, "BOF"))
            {
               pSess->eventCB(pSess->usrParam, (void*)pSess, NOTIFY_START_OF_STREAM, 0);
               cgmi_SetRate(pSess, 0.0); 
            }
#endif
            else if (0 == strcmp(ntype, "network_error"))
            {
               pSess->eventCB(pSess->usrParam, (void*)pSess , NOTIFY_NETWORK_ERROR, 0);
            }
            else
            {
               GST_ERROR("Do not know how to handle %s notification\n",ntype);
            }
         }
         break;
      case GST_MESSAGE_ERROR:
      {
         gchar  *debug;
         GError *error;

         gst_message_parse_error( msg, &error, &debug );
         g_free( debug );

         GST_WARNING("Error:%d:%d: %s - domain:%d\n",error->code, error->domain,  error->message, GST_RESOURCE_ERROR);
         // the error could come from multiple domains.
         if(error->domain == GST_CORE_ERROR)
         {
         }
         else if (error->domain ==  GST_LIBRARY_ERROR)
         {
         }
         else if (error->domain == GST_RESOURCE_ERROR)
         {
            if (error->code == GST_RESOURCE_ERROR_NOT_FOUND)
            {
               pSess->eventCB(pSess->usrParam, (void*)pSess,NOTIFY_MEDIAPLAYER_URL_OPEN_FAILURE, 0);
            }
         }
         else if (error->domain == GST_STREAM_ERROR)
         {
            if (error->code == GST_STREAM_ERROR_FAILED)
            {
               pSess->eventCB(pSess->usrParam, (void*)pSess,NOTIFY_MEDIAPLAYER_URL_OPEN_FAILURE, 0);
            }
         }

         g_error_free( error );

         break;
      }
      case GST_MESSAGE_STATE_CHANGED:
      {
         /* only check message from the pipeline */
         if( GST_MESSAGE_SRC(msg) == GST_OBJECT(pSess->pipeline) )
         {
            GstState old_state, new_state, pending_state;

            gst_message_parse_state_changed( msg, &old_state, &new_state, &pending_state );
            g_print("Pipeline state change from %s to %s\n", gst_element_state_get_name(old_state), gst_element_state_get_name(new_state));

            /* Print position and duration when in playing state */
            if( GST_STATE_PLAYING == new_state )
            {
               //debug_cisco_gst_streamDurPos(pSess);
               if ( NULL == pSess->videoSink )
               {
                  GstElement *videoSink = NULL;
                  GstElement *innerSink = NULL;
                  g_object_get( pSess->pipeline, "video-sink", &videoSink, NULL );
                  if ( NULL != videoSink )
                  {
                     g_print("Found element class (%s), handle = %p\n", G_OBJECT_CLASS_NAME(G_OBJECT_GET_CLASS(videoSink)), videoSink); 
                     if ( GST_IS_BIN(videoSink) )
                     {
                        innerSink = cgmi_gst_find_element( GST_BIN(videoSink), "videosink" );
                        if ( NULL != innerSink )
                        {
                           pSess->videoSink = innerSink;
                        }
                     }
                     else
                     {
                        pSess->videoSink = videoSink;
                     }
                     gst_object_unref( videoSink );
                  }
               }
               if ( NULL != pSess->videoSink )
               {
                  gchar dim[64];
                  snprintf( dim, sizeof(dim), "%d,%d,%d,%d,%d,%d,%d,%d",
                            pSess->vidSrcRect.x, pSess->vidSrcRect.y, pSess->vidSrcRect.w, pSess->vidSrcRect.h,
                            pSess->vidDestRect.x, pSess->vidDestRect.y, pSess->vidDestRect.w, pSess->vidDestRect.h);
                  g_object_set( G_OBJECT(pSess->videoSink), "window_set", dim, NULL );
               }
               if((NULL == pSess->hwVideoDecHandle) && (NULL == pSess->hwAudioDecHandle))
               {
                  cgmi_GetHwDecHandles(pSess);
               }
               /* For RTP live content, the full gst pipeline is only created when set to playing state */
               pSess->hasFullGstPipeline = TRUE;
            }
         }
      }
      break;

      default:
      /* unhandled message */
      break;
   } //end of switch
   return TRUE;
}

static void cgmi_gst_psi_info( GObject *obj, guint size, void *context, gpointer data )
{
   tSession *pSess = (tSession*)data;
   unsigned int videoStream;

   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      g_print("%s:Invalid session handle\n", __FUNCTION__);
      return;
   }

   g_print("on_psi_info:\n");

   GValueArray *patInfo = NULL;
   GObject *entry = NULL;
   guint pid;

   GObject *pmtInfo = NULL, *streamInfo = NULL;
   GValueArray *streamInfos = NULL;
   GValueArray *descriptors = NULL;
   GValueArray *languages = NULL;
   gint i, j, z;
   GValue *value;
   guint program, version, pcrPid, esPid, esType;

   if ( NULL == pSess->demux )
   {
      g_print("%s:Invalid demux handle\n", __FUNCTION__);
      return;
   }

   cgmiDiag_addTimingEntry(DIAG_TIMING_METRIC_PAT_PMT_ACQUIRED, pSess->diagIndex, pSess->playbackURI, 0);

   g_print("Enabling server side trick mode...\n");
   g_object_set( obj, "server-side-trick-mode", TRUE, NULL );

   g_rec_mutex_lock(&pSess->psiMutex);
   pSess->bQueryDiscreteAudioInfo = TRUE;
   pSess->numAudioLanguages = 0;

   /* Get PAT Info */
   g_object_get( obj, "pat-info", &patInfo, NULL );

   g_print("PAT: Total Entries: %d \n", patInfo->n_values);
   g_print("------------------------------------------------------------------------- \n");

   for ( i = 0; i < patInfo->n_values; i++ )
   {
      value = g_value_array_get_nth( patInfo, i );
      entry = (GObject*)g_value_get_object( value );
      g_object_get( entry, "program-number", &program, NULL );
      g_object_get( entry, "pid", &pid, NULL );
      g_print("Program: %04x pid: %04x\n", program, pid);

      /* Set the Program Index you want.
         Note that this is not the same as program number. It is the 1-based index from the
         beginning of list of available PMTs in the PAT. Therefore, for instance, if there
         are 3 programs in the PAT with program numbers 11, 12, and 13 in order, to select
         program number 12, program needs to be set to 2 (i.e., second program from the beginning) */

      /* We select the first program by default */
      g_object_set( obj, "program-number", 1, NULL );

      g_object_get( obj, "pmt-info", &pmtInfo, NULL );

      g_object_get( pmtInfo, "program-number", &program, NULL );
      g_object_get( pmtInfo, "version-number", &version, NULL );
      g_object_get( pmtInfo, "pcr-pid", &pcrPid, NULL );
      g_object_get( pmtInfo, "stream-info", &streamInfos, NULL );
      g_object_get( pmtInfo, "descriptors", &descriptors, NULL );

      g_print("Default Audio Language: %s\n", pSess->defaultAudioLanguage);

      g_print("PMT: Program: %04x Version: %d pcr: %04x Streams: %d " "Descriptors: %d\n",
              (guint16)program, version, (guint16)pcrPid, streamInfos->n_values, descriptors->n_values);

      pSess->numStreams = streamInfos->n_values;

      for ( j = 0; j < streamInfos->n_values; j++ )
      {
         value = g_value_array_get_nth( streamInfos, j );
         streamInfo = (GObject*) g_value_get_object( value );
         g_object_get( streamInfo, "pid", &esPid, NULL );
         g_object_get( streamInfo, "stream-type", &esType, NULL);
         g_object_get( streamInfo, "descriptors", &descriptors, NULL);
         g_print("Pid: %04x type: %x Descriptors: %d\n",(guint16)esPid, (guint8) esType, descriptors->n_values);

         pSess->streams[j].pid = esPid;
         pSess->streams[j].streamType = esType;

         for ( z = 0; z < descriptors->n_values; z++ )
         {
            GString *string;
            value = g_value_array_get_nth( descriptors, z );
            string = (GString *)g_value_get_boxed( value );
            gint len, pos;
            gint totalLen = string->len;
            pos = 0;
            while ( totalLen > 2 )
            {
               len = (guint8)string->str[pos + 1];
               g_print("descriptor # %d tag %02x len %d\n", z, (guint8)string->str[pos], len);
               switch ( (guint8)string->str[pos] )
               {
                  case 0x0A: /* ISO_639_language_descriptor */
                  {
                     g_print("Found audio language descriptor for stream %d\n", j);
                     if ( pSess->numAudioLanguages < MAX_AUDIO_LANGUAGE_DESCRIPTORS && len >= 4 )
                     {
                        pSess->audioLanguages[pSess->numAudioLanguages].pid = esPid;
                        pSess->audioLanguages[pSess->numAudioLanguages].streamType = esType;
                        pSess->audioLanguages[pSess->numAudioLanguages].index = j;
                        strncpy( pSess->audioLanguages[pSess->numAudioLanguages].isoCode, &string->str[pos+2], 3 );
                        pSess->audioLanguages[pSess->numAudioLanguages].isoCode[3] = 0;
                        pSess->audioLanguages[pSess->numAudioLanguages].bDiscrete = FALSE;

                        if ( pSess->audioLanguageIndex == INVALID_INDEX && strlen(pSess->newAudioLanguage) > 0)
                        {
                           if ( strncmp(pSess->audioLanguages[pSess->numAudioLanguages].isoCode, pSess->newAudioLanguage, 3) == 0 )
                           {
                              g_print("Stream (%d) audio language matched to selected audio lang %s\n", j, pSess->newAudioLanguage);
                              pSess->audioLanguageIndex = j;
                              g_strlcpy(pSess->currAudioLanguage, pSess->audioLanguages[pSess->numAudioLanguages].isoCode,
                                    sizeof(pSess->currAudioLanguage));
                           }
                        }

                        if ( pSess->audioLanguageIndex == INVALID_INDEX && strlen(pSess->newAudioLanguage) == 0 &&
                              strlen(pSess->defaultAudioLanguage) > 0 )
                        {
                           if ( strncasecmp(pSess->audioLanguages[pSess->numAudioLanguages].isoCode, pSess->defaultAudioLanguage, 3) == 0 )
                           {
                              g_print("Stream (%d) audio language matched to default audio lang %s\n", j, pSess->defaultAudioLanguage);
                              pSess->audioLanguageIndex = j;
                              g_strlcpy(pSess->currAudioLanguage, pSess->audioLanguages[pSess->numAudioLanguages].isoCode,
                                    sizeof(pSess->currAudioLanguage));
                           }
                        }
                        pSess->numAudioLanguages++;
                     }
                     else
                     {
                        g_print("Maximum number of audio language descriptors %d has been reached!!!\n",
                                MAX_AUDIO_LANGUAGE_DESCRIPTORS);
                     }
                  }
                  break;

                  case 0x59: /* Subtitling descriptor */
                  {
                     g_print("Found subtitle language descriptor for stream %d\n", j);
                     if ( pSess->numSubtitleLanguages < MAX_SUBTITLE_LANGUAGES && len >= 8 )
                     {
                        pSess->subtitleInfo[pSess->numSubtitleLanguages].pid = esPid;
                        strncpy(pSess->subtitleInfo[pSess->numSubtitleLanguages].isoCode, &string->str[pos + 2], 3);
                        pSess->subtitleInfo[pSess->numSubtitleLanguages].isoCode[3] = 0;
                        pSess->subtitleInfo[pSess->numSubtitleLanguages].type = string->str[pos + 5];
                        pSess->subtitleInfo[pSess->numSubtitleLanguages].compPageId = (gushort)(((gushort)string->str[pos + 6] << 8) | string->str[pos + 7]);
                        pSess->subtitleInfo[pSess->numSubtitleLanguages].ancPageId = (gushort)(((gushort)string->str[pos + 8] << 8) | string->str[pos + 9]);
                        if ( strlen(pSess->defaultSubtitleLanguage) > 0 && pSess->subtitleLanguageIndex == INVALID_INDEX )
                        {
                           if ( strncmp(pSess->subtitleInfo[pSess->numSubtitleLanguages].isoCode, pSess->defaultSubtitleLanguage, 3) == 0 )
                           {
                              g_print("Stream (%d) subtitle language matched to default subtitle lang %s\n", j, pSess->defaultSubtitleLanguage);
                              pSess->subtitleLanguageIndex = j;
                           }
                        }

                        pSess->numSubtitleLanguages++;
                     }
                  }
                  break;

                  case 0x86: /* Closed Caption Service Descriptor */
                  {
                     gint i;
                     gchar *data;
                     g_print("Found closed caption service descriptor for stream %d\n", j);

                     pSess->numClosedCaptionServices = string->str[pos+2] & 0x1F;

                     if ( pSess->numClosedCaptionServices > MAX_CLOSED_CAPTION_SERVICES )
                     {
                        g_print("Maximum number of closed caption descriptors %d has been reached!!!\n",
                                MAX_CLOSED_CAPTION_SERVICES);
                        pSess->numClosedCaptionServices = MAX_CLOSED_CAPTION_SERVICES;
                     }

                     data = &string->str[pos + 3];

                     g_print("Found %d closed caption languages\n", pSess->numClosedCaptionServices);

                     for ( i = 0; i < pSess->numClosedCaptionServices; i++ )
                     {
                        memset( pSess->closedCaptionServices[i].isoCode, 0, 4 );
                        memcpy( pSess->closedCaptionServices[i].isoCode, data, 3 );

                        data += 3;

                        if ( *data & 0x80 )
                        {
                           pSess->closedCaptionServices[i].isDigital = TRUE;
                           pSess->closedCaptionServices[i].serviceNum = *data & 0x3F;
                        }
                        else
                        {
                           pSess->closedCaptionServices[i].isDigital = FALSE;
                           //Code field number into service number
                           pSess->closedCaptionServices[i].serviceNum = *data & 0x01;
                        }

                        data += 3;

                        g_print("Caption[%d]: lang: %s, Digital: %d, Service num: %d\n", i,
                           pSess->closedCaptionServices[i].isoCode,
                           pSess->closedCaptionServices[i].isDigital,
                           pSess->closedCaptionServices[i].serviceNum);
                     }
                  }
                  break;

                  default:
                     break;

               }

               pos += len + 2;
               totalLen = totalLen - len - 2;
            }
         }
      }
      g_print ("------------------------------------------------------------------------- \n");
   }

   g_rec_mutex_unlock(&pSess->psiMutex);

   if ( pSess->audioLanguageIndex != INVALID_INDEX )
   {
      g_print("Selecting audio language index %d...\n", pSess->audioLanguageIndex);
      g_object_set( obj, "audio-stream", pSess->audioLanguageIndex, NULL );
   }
   else
     g_object_get( obj, "audio-stream", &pSess->audioLanguageIndex, NULL );

   if ( NULL != pSess->eventCB )
      pSess->eventCB(pSess->usrParam, (void*)pSess, NOTIFY_PSI_READY, 0);

   g_object_get( obj, "video-stream", &videoStream, NULL );
   if ( -1 == videoStream )
   {
      g_print("Stream has no video!\n");
      pSess->noVideo = TRUE;
   }
   else
   {
      pSess->noVideo = FALSE;
   }

   /*
   if ( FALSE == pSess->autoPlay )
   {
      g_mutex_lock (pSess->autoPlayMutex);
      if ( pSess->videoStreamIndex == INVALID_INDEX && pSess->audioStreamIndex == INVALID_INDEX )
      {
         pSess->waitingOnPids = TRUE;
         g_cond_wait (pSess->autoPlayCond, pSess->autoPlayMutex);
      }
      g_mutex_unlock (pSess->autoPlayMutex);
   }
   */
}

static GstElement *cgmi_gst_find_element( GstBin *bin, gchar *ename )
{
   GstElement *element = NULL;
   GstElement *handle = NULL;
   GstIterator *iter = NULL;
#if GST_CHECK_VERSION(1,0,0)
   GValue item = G_VALUE_INIT;
#else
   void *item;
#endif
   gchar *name = NULL;
   gboolean done = FALSE;

   iter = gst_bin_iterate_elements( bin );
   while ( FALSE == done )
   {
      switch ( gst_iterator_next(iter, &item) )
      {
         case GST_ITERATOR_OK:
#if GST_CHECK_VERSION(1,0,0)
            element = (GstElement *)g_value_get_object(&item);
#else
            element = (GstElement *)item;
#endif
            if ( NULL != element )
            {
               name = gst_element_get_name( element );
               if ( NULL != name )
               {
                  if ( NULL != strstr(name, ename) )
                  {
                     handle = element;
                     g_print("Found element (%s) handle = %p\n", ename, handle);
                     done = TRUE;
                  }
                  g_free( name );
               }
#if !GST_CHECK_VERSION(1,0,0)
               gst_object_unref( item );
#endif
            }
#if GST_CHECK_VERSION(1,0,0)
            g_value_reset( &item );
#endif
            break;
         case GST_ITERATOR_RESYNC:
            gst_iterator_resync( iter );
            break;
         case GST_ITERATOR_ERROR:
            done = TRUE;
            break;
         case GST_ITERATOR_DONE:
            done = TRUE;
            break;
      }
   }
#if GST_CHECK_VERSION(1,0,0)
   g_value_unset( &item );
#endif
   gst_iterator_free( iter );

   return handle;
}
void cgmi_gst_notify_source( GObject *obj, GParamSpec *param, gpointer data )
{
   GstElement *source = NULL;
   GstElement *souphttpsrc = NULL;
   const gchar *name;
   tSession *pSess = (tSession*)data;

   g_print("notify-source\n");

   g_object_get( obj, "source", &source, NULL );
   if ( NULL == source )
      return;

   pSess->source = source;

   name = G_OBJECT_CLASS_NAME(G_OBJECT_GET_CLASS(source));

   if ( NULL != name )
   {
      g_print("Source element class: %s\n", name);
      if ( NULL != strstr(name, "GstUDPSrc") )
      {
         if ( NULL != g_object_class_find_property(G_OBJECT_GET_CLASS(source), "buffer-size") )
         {
            g_print("Setting socket receiver buffer size to %d\n", SOCKET_RECEIVE_BUFFER_SIZE);
            g_object_set( source, "buffer-size", SOCKET_RECEIVE_BUFFER_SIZE, NULL );
         }
         if ( NULL != g_object_class_find_property(G_OBJECT_GET_CLASS(source), "chunk-size") )
         {
            g_print("Setting UDP chunk size to %d\n", UDP_CHUNK_SIZE);
            g_object_set( source, "chunk-size", UDP_CHUNK_SIZE, NULL );
         }
      }
      else if ( NULL != strstr(name, "GstSoupHTTPSrc") )
      {
         souphttpsrc = source;
         // if we have just souphttpsrc let's set the blocksize to something larger than the default 4k
         g_print("souphttpsrc has been detected, increasing the blocksize to: %u bytes\n", DEFAULT_BLOCKSIZE );
         g_object_set (souphttpsrc, "blocksize", DEFAULT_BLOCKSIZE, NULL);
      }
   }

   gst_object_unref( source );
}

static void cgmi_gst_element_added( GstBin *bin, GstElement *element, gpointer data )
{
   GstElement *hlsDemux = NULL;
   GstElement *demux = NULL;
   GstElement *videoSink = NULL;
   GstElement *videoDecoder = NULL;
   GstElement *audioDecoder = NULL;
   tSession *pSess = (tSession*)data;

   gchar *name = gst_element_get_name( element );
   g_print("Element added: %s\n", name);

   if ( NULL == pSess->hlsDemux )
   {
      hlsDemux = cgmi_gst_find_element( bin, "ciscdemux" );

      if( NULL != hlsDemux )
      {
         pSess->hlsDemux = hlsDemux;
         GST_WARNING("setting audio language: %s\n", pSess->newAudioLanguage);
         if(strlen(pSess->newAudioLanguage) > 0)
         {
            g_object_set( G_OBJECT(pSess->hlsDemux), "audio-language", pSess->newAudioLanguage, NULL );
         }
         else if(strlen(pSess->defaultAudioLanguage) > 0)
         {
            g_object_set( G_OBJECT(pSess->hlsDemux), "audio-language", pSess->defaultAudioLanguage, NULL );
         }
      }
   }

   // If we haven't found the demux investigate this bin
   if ( NULL == pSess->demux )
   {
      demux = cgmi_gst_find_element( bin, "tsdemux" );

      if( NULL != demux )
      {
         pSess->demux = demux;
         g_signal_connect( demux, "psi-info", G_CALLBACK(cgmi_gst_psi_info), data );
      }
   }

   if ( NULL == pSess->videoSink )
   {
      videoSink = cgmi_gst_find_element( bin, "videosink" );

      if( NULL != videoSink )
      {
         pSess->videoSink = videoSink;
      }
   }

   if ( NULL == pSess->videoDecoder )
   {
      videoDecoder = cgmi_gst_find_element( bin, "videodecoder" );

      if( NULL != videoDecoder )
      {
         pSess->videoDecoder = videoDecoder;
         g_print("Muting video decoder...\n");
         g_object_set( G_OBJECT(pSess->videoDecoder), "decoder_mute", TRUE, NULL );
      }
   }

   if ( NULL == pSess->audioDecoder )
   {
      audioDecoder = cgmi_gst_find_element( bin, "audiodecoder" );

      if( NULL != audioDecoder )
      {
         pSess->audioDecoder = audioDecoder;
         g_print("Muting audio decoder...\n");
         g_object_set( G_OBJECT(pSess->audioDecoder), "decoder_mute", TRUE, NULL );
      }
   }

   // If this element is a bin ensure this callback is called when elements
   // are added.
   if ( GST_IS_BIN(element) )
   {
      g_print("Element (%s) is a bin", name);
      g_signal_connect( element, "element-added", G_CALLBACK(cgmi_gst_element_added), pSess );
   }

   if ( NULL != name )
   {
      g_free( name );
   }
}

static GstFlowReturn cgmi_gst_new_user_data_buffer_available (GstAppSink *sink, gpointer data)
{
   GstBuffer *buffer;
#if GST_CHECK_VERSION(1,0,0)
   GstSample *sample;
#endif
   tSession *pSess = (tSession*)data;

   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      return GST_FLOW_OK;
   }

   // Pull the buffer
#if GST_CHECK_VERSION(1,0,0)
   sample = gst_app_sink_pull_sample( GST_APP_SINK(sink) );
   if ( NULL == sample )
   {
      g_print("Error appsink callback failed to pull user data buffer sample.\n");
      return GST_FLOW_OK;
   }

   buffer = gst_sample_get_buffer( sample );
#else
   buffer = gst_app_sink_pull_buffer( GST_APP_SINK(sink) );
#endif

   if ( NULL == buffer )
   {
      g_print("Error appsink callback failed to pull user data buffer.\n");
      return GST_FLOW_OK;
   }

   if ( NULL != pSess->userDataBufferCB )
   {
      //expect the callback to by sync, so buffer/sample can be freed below
      pSess->userDataBufferCB( pSess->userDataBufferParam, (void *)buffer );
   }

   //free buffer/sample when done
#if GST_CHECK_VERSION(1,0,0)
   gst_sample_unref(sample);
#else
   gst_buffer_unref(buffer);
#endif 

   return GST_FLOW_OK;
}

char* cgmi_ErrorString(cgmi_Status stat)
{
   static char *errorString[] =
   {
   "CGMI_ERROR_SUCCESS",           ///<Success
   "CGMI_ERROR_FAILED",            ///<General Error
   "CGMI_ERROR_NOT_IMPLEMENTED",   ///<This feature or function has not yet been implmented
   "CGMI_ERROR_NOT_SUPPORTED",     ///<This feature or funtion is not supported
   "CGMI_ERROR_BAD_PARAM",         ///<One of the parameters passed in is invalid
   "CGMI_ERROR_OUT_OF_MEMORY",     ///<An allocation of memory has failed.
   "CGMI_ERROR_TIMEOUT",           ///<A time out on a filter has occured.
   "CGMI_ERROR_INVALID_HANDLE",    ///<A session handle or filter handle passed in is not correct.
   "CGMI_ERROR_NOT_INITIALIZED",   ///<A function is being called when the system is not ready.
   "CGMI_ERROR_NOT_OPEN",          ///<The Interface has yet to be opened.
   "CGMI_ERROR_NOT_ACTIVE",        ///<This feature is not currently active
   "CGMI_ERROR_NOT_READY",         ///<The feature requested can not be provided at this time.
   "CGMI_ERROR_NOT_CONNECTED",     ///<The pipeline is currently not connected for the request.
   "CGMI_ERROR_URI_NOTFOUND",      ///<The URL passed in could not be resolved.
   "CGMI_ERROR_WRONG_STATE",       ///<The Requested state could not be set
   "CGMI_ERROR_NUM_ERRORS "        ///<Place Holder to know how many errors there are in the struct enum.
   };
   if ((stat > CGMI_ERROR_NUM_ERRORS) || (stat < 0))
   {
      return errorString[CGMI_ERROR_NUM_ERRORS];
   }
   return errorString[stat];
}

cgmi_Status cgmi_Init(void)
{
   cgmi_Status   stat = CGMI_ERROR_SUCCESS;
   gchar    *strVersion = NULL;
   GError   *error      = NULL;
   int argc =0;
   char **argv;
   argv=0;
   guint major;
   guint minor;
   guint micro;
   guint nano;

   do
   {
      /* Initialize gstreamer */
      if( !gst_init_check( &argc, &argv, &error ) )
      {
         g_critical("Failed to initialize gstreamer :%s\n", error->message);
         stat = CGMI_ERROR_NOT_INITIALIZED;
         break;
      }

      /* Verify threading system in up */
      if( !g_thread_supported() )
      {
         g_critical("GLib Thread system not initialized\n");
         stat = CGMI_ERROR_NOT_SUPPORTED;
         break;
      }
      stat = cgmi_utils_init();

      GST_DEBUG_CATEGORY_INIT (cgmi, "cgmi", 0, "Cisco Gstreamer Media Interface -core");
      /* print out the version */
      strVersion = gst_version_string();
      g_message("GStreamer Initialized with Version: %s\n", strVersion);
      g_free( strVersion );

      gst_version(&major,&minor,&micro,&nano);
      //g_message("major = %u, minor =%u, macro=%u, nano=%u\n", major, minor, micro, nano);
      if(major >= 1)
      {
         cgmi_SetLogging("cgmi:2,dlnasrc:2,ciscdemux:4");
      }
      else
      {
         cgmi_SetLogging("cgmi:2,dlnasrc:2,ciscdemux:3");
      }

      //intialize the diag subsytem
      cgmiDiags_Init();

   } while(0);

   return stat;
}

cgmi_Status cgmi_Term (void)
{
   gst_deinit();
   cgmi_utils_finalize();
   cgmiDiags_Term();
   return CGMI_ERROR_SUCCESS;
}


#ifdef USE_DRMPROXY
static void asyncCB(uint64_t  privateData,
                    uint64_t  licenseID,
                    void      *data,
                    uint32_t  dataSize,
                    tProxyErr *err)
{
   printf("%s() called - privateData %" PRIu64 " licenseID: %"PRIu64
         "\n", __FUNCTION__, privateData, licenseID);

}
#endif

cgmi_Status cgmi_CreateSession (cgmi_EventCallback eventCB, void* pUserData, void **pSession ) {
   tSession *pSess = NULL;

   pSess = g_malloc0(sizeof(tSession));
   if (pSess == NULL)
   {
      return CGMI_ERROR_OUT_OF_MEMORY;
   }

   *pSession = pSess;
   pSess->cookie = (void*)MAGIC_COOKIE;
   pSess->usrParam = pUserData;
   pSess->eventCB = eventCB;
   pSess->demux = NULL;
   pSess->udpsrc = NULL;
   pSess->videoSink = NULL;
   pSess->audioSink = NULL;
   pSess->videoDecoder = NULL;
   pSess->audioDecoder = NULL;
   pSess->vidDestRect.x = 0;
   pSess->vidDestRect.y = 0;
   pSess->vidDestRect.w = VIDEO_MAX_WIDTH;
   pSess->vidDestRect.h = VIDEO_MAX_HEIGHT;
   pSess->audioStreamIndex = INVALID_INDEX;
   pSess->videoStreamIndex = INVALID_INDEX;
   pSess->isAudioMuted = FALSE;
   pSess->diagIndex = 0;
   pSess->drmProxyHandle = 0;
   pSess->suppressLoadDone = FALSE;

   strncpy( pSess->defaultAudioLanguage, gDefaultAudioLanguage, sizeof(pSess->defaultAudioLanguage) );
   pSess->defaultAudioLanguage[sizeof(pSess->defaultAudioLanguage) - 1] = 0;
   pSess->newAudioLanguage[0] = '\0';
   pSess->currAudioLanguage[0] = '\0';
   pSess->audioLanguageIndex = INVALID_INDEX;

   strncpy( pSess->defaultSubtitleLanguage, gDefaultSubtitleLanguage, sizeof(pSess->defaultSubtitleLanguage) );
   pSess->defaultSubtitleLanguage[sizeof(pSess->defaultSubtitleLanguage) - 1] = 0;
   pSess->subtitleLanguageIndex = INVALID_INDEX;

   pSess->thread_ctx = g_main_context_new();

   pSess->autoPlayMutex = g_mutex_new ();
   pSess->autoPlayCond = g_cond_new ();

   g_mutex_init(&pSess->monThreadMutex);
   g_cond_init(&pSess->monThreadCond);
   g_rec_mutex_init(&pSess->psiMutex);

   pSess->runMonitor = TRUE;
   pSess->monitor = g_thread_new("monitoring_thread", cgmi_monitor, pSess);
   if (!pSess->monitor)
   {
      GST_WARNING("Error launching thread for monitoring timestamp errors\n");
   }

   pSess->loop = g_main_loop_new (pSess->thread_ctx, FALSE);
   if (pSess->loop == NULL)
   {
      GST_WARNING("Error creating a new Loop\n");
   }

   //pSess->thread =  g_thread_create ((GThreadFunc) g_main_loop_run,(void*)pSess->loop, TRUE, NULL);
   pSess->thread = g_thread_new("signal_thread", g_main_loop_run, (void*)pSess->loop);
   if (!pSess->thread)
   {
      GST_WARNING("Error launching thread for gmainloop\n");
   }
   //need a blocking semaphore here
   // but instead I will spin until the
   // gmainloop is running
   while (FALSE == g_main_loop_is_running(pSess->loop))
   {
      g_usleep(100);
   }
   GST_INFO("ok the gmainloop for the thread ctx is running\n");

   g_mutex_lock(&gSessionListMutex);
   gSessionList = g_list_append(gSessionList, pSess);
   g_mutex_unlock(&gSessionListMutex);

   return CGMI_ERROR_SUCCESS;
}

cgmi_Status cgmi_DestroySession (void *pSession)
{
#ifdef USE_DRMPROXY
   tProxyErr proxy_err = {};
   uint64_t * privateData = NULL;
#endif
   cgmi_Status stat = CGMI_ERROR_SUCCESS;
   tSession *pSess = (tSession*)pSession;

   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      g_print("%s:Invalid session handle\n", __FUNCTION__);
      return CGMI_ERROR_INVALID_HANDLE;
   }

   GST_INFO("Entered Destroy\n");
   if (g_main_loop_is_running(pSess->loop))
   {
      if ( NULL != pSess->sourceWatch )
      {
         GST_INFO("removing source from main loop context\n");
         g_source_destroy( pSess->sourceWatch );
         g_source_unref( pSess->sourceWatch );
         pSess->sourceWatch = NULL;
      }

      GST_INFO("about to quit the main loop\n");
      g_main_loop_quit (pSess->loop);
   }

   g_mutex_lock(&pSess->monThreadMutex);
   pSess->runMonitor = FALSE;
   g_mutex_unlock(&pSess->monThreadMutex);

   if (pSess->monitor)
   {
      g_cond_signal(&pSess->monThreadCond);

      GST_INFO("about to join monitoring thread\n");
      g_thread_join (pSess->monitor);
   }
   else
   {
      g_print("No monitoring thread to join\n");
   }

   if (pSess->thread)
   {
      GST_INFO("about to join threads\n");
      g_thread_join (pSess->thread);
   }
   else
   {
      g_print("No thread to free... odd\n");
   }

   if (pSess->pipeline) {gst_object_unref (GST_OBJECT (pSess->pipeline));}
   if (pSess->autoPlayCond) {g_cond_free(pSess->autoPlayCond);}
   if (pSess->autoPlayMutex) {g_mutex_free(pSess->autoPlayMutex);}
   if (pSess->loop) {g_main_loop_unref(pSess->loop);}
   g_rec_mutex_clear(&pSess->psiMutex);
   g_cond_clear(&pSess->monThreadCond);
   g_mutex_clear(&pSess->monThreadMutex);
   if(NULL != pSess->thread_ctx)
   {
      g_main_context_unref(pSess->thread_ctx);
      pSess->thread_ctx = NULL;
   }

#ifdef USE_DRMPROXY
   if (0 != pSess->drmProxyHandle) 
   {
       g_print("Calling DRMPROXY_DestroySession. pSess->drmProxyHandle = %lld", pSess->drmProxyHandle);
       DRMPROXY_DestroySession(pSess->drmProxyHandle);
       if (proxy_err.errCode != 0)
       {
          GST_ERROR ("DRMPROXY_DestroySession error %lu \n", proxy_err.errCode);
          GST_ERROR ("%s \n", proxy_err.errString);
          if (pSess != NULL)
             pSess->eventCB(pSess->usrParam, (void *)pSess, 0, proxy_err.errCode);
       }
   }
#endif
   g_mutex_lock(&gSessionListMutex);
   gSessionList = g_list_remove(gSessionList, pSess);
   g_mutex_unlock(&gSessionListMutex);
   g_free(pSess);

   return stat;
}

static void cgmi_gst_have_type( GstElement *typefind, guint probability, GstCaps *caps, gpointer user_data )
{
   tSession *pSess = (tSession*)user_data;
   gchar *caps_string;
   GstElement *drmStreamParser = NULL;
   GstElement *drmMgr = NULL;

   g_print ("FOUND_TYPE\n");

   if ( NULL == pSess )
      return;

   caps_string = gst_caps_to_string( caps );
   if ( NULL != caps_string )
   {
      g_print("Caps: %s\n", caps_string);
   }

   if ( NULL != caps_string && NULL != g_strstr_len(caps_string, -1, VGDRM_CAPS) )
   {
      drmStreamParser = gst_element_factory_make("ciscvgdrmstreamparser", "drm-stream-parser");
      if ( NULL == drmStreamParser )
      {
         GST_WARNING("Could not obtain a DRM stream parser element!\n");
         return;
      }
      drmMgr = gst_element_factory_make("ciscdrmmgr", "drmmgr");
      if ( NULL == drmMgr )
      {
         GST_WARNING("Could not obtain a DRM Manager element!\n");
         gst_object_unref( GST_OBJECT(drmStreamParser) );
         return;
      }

      gst_bin_add_many( GST_BIN (pSess->pipeline), drmStreamParser, drmMgr, NULL );

      if ( TRUE != gst_element_link_many(typefind, drmStreamParser, drmMgr, pSess->demux, NULL) )
      {
         GST_WARNING("Could not link source to DRM streamer parser to DRM manager to demux\n");
         return;
      }

      gst_element_set_state(drmStreamParser, GST_STATE_PLAYING);
      gst_element_set_state(drmMgr, GST_STATE_PLAYING);
   }
   else
   {
      if ( TRUE != gst_element_link_many(typefind, pSess->demux, NULL) )
      {
         GST_WARNING("Could not link source to demux\n");
         return;
      }
   }
}

static void cgmi_gst_pad_added( GstElement *element, GstPad *pad, gpointer data )
{
   tSession *pSess = (tSession*)data;
   GstCaps *caps = NULL;
   GstPad *sinkpad;

   g_print("ON_PAD_ADDED\n");

   if ( NULL == pSess )
      return;

   if ( NULL == pad )
      return;
   
#if GST_CHECK_VERSION(1,0,0)
   caps = gst_pad_query_caps( pad, NULL );
#else
   caps = gst_pad_get_caps( pad );
#endif
   
   if ( NULL != caps )
   {
      gchar *caps_string = gst_caps_to_string(caps);      
      g_print("Caps: %s\n", caps_string);
      if ( NULL != caps_string )
      {
         if ( NULL != strstr(caps_string, "video") )
         {
            sinkpad = gst_element_get_static_pad (pSess->videoDecoder, "sink");
            g_print("Got static pad (%p) of peer\n", sinkpad);
            if ( GST_PAD_LINK_OK != gst_pad_link (pad, sinkpad) )
            {
               g_print("Could not link demux to decoder!\n");
            }           
            gst_object_unref( sinkpad );
         }
         else if ( NULL != strstr(caps_string, "audio") )
         {
            sinkpad = gst_element_get_static_pad (pSess->audioDecoder, "sink");
            g_print("Got static pad (%p) of peer\n", sinkpad);
            if ( GST_PAD_LINK_OK != gst_pad_link (pad, sinkpad) )
            {
               g_print("Could not link demux to decoder!\n");
            }           
            gst_object_unref( sinkpad );
         }
         g_free( caps_string );
      }

      gst_caps_unref( caps );
   }
}

static void cgmi_gst_no_more_pads(GstElement *element, gpointer data)
{
   tSession *pSess = (tSession*)data;

   GST_WARNING("Received NO_MORE_PADS signal\n");

   if (NULL != pSess)
   {
      if(FALSE == pSess->suppressLoadDone)
      {
         pSess->eventCB(pSess->usrParam, (void*)pSess, NOTIFY_LOAD_DONE, 0);
      }
      else
      {
         pSess->suppressLoadDone = FALSE;
      }
      cgmi_GetHwDecHandles(pSess);
      pSess->hasFullGstPipeline = TRUE;
   }
   else
   {
      GST_ERROR("Invalid user data\n");
   }
}

cgmi_Status cgmi_Load (void *pSession, const char *uri, cpBlobStruct * cpblob, const char *sessionSettings)
{
   GError               *g_error_str =NULL;
   GstParseContext      *ctx;
   GstPlugin            *plugin;
   gchar                **arr;
   gchar                *pPipeline;
   cgmi_Status          stat = CGMI_ERROR_SUCCESS;
   GSource              *source;
   uint32_t             bisBroadcomHw = FALSE;
   int                  drmStatus = 1;
   GstStateChangeReturn sret;
#ifdef USE_DRMPROXY
   gchar                **array = NULL;
   tProxyErr            proxy_err = {};
   uint64_t             licenseId=0;
   char                 buffer[50];
   tDRM_TYPE            drmType=UNKNOWN;
   void *               DRMPrivateData;
   uint32_t             DRMPrivateDataSize;
   char                 contentURL[128] = "";
   int                  ret;
   uint64_t             privateData;
#endif

   tSession *pSess = (tSession*)pSession;
   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      g_print("%s:Invalid session handle\n", __FUNCTION__);
      return CGMI_ERROR_INVALID_HANDLE;
   }

   cgmiDiags_GetNextSessionIndex(&pSess->diagIndex);

   pPipeline = g_strnfill(MAX_PIPELINE_SIZE, '\0');
   if (pPipeline == NULL)
   {
      GST_WARNING("Error allocating memory\n");
      return CGMI_ERROR_OUT_OF_MEMORY;
   }

   pSess->numAudioLanguages = 0;
   pSess->numClosedCaptionServices = 0;
   pSess->numSubtitleLanguages = 0;
   pSess->numStreams = 0;
   pSess->videoStreamIndex = INVALID_INDEX;
   pSess->audioStreamIndex = INVALID_INDEX;
   pSess->audioLanguageIndex = INVALID_INDEX;
   pSess->subtitleLanguageIndex = INVALID_INDEX;
   pSess->isAudioMuted = FALSE;
   pSess->rate = 0.0;
   pSess->rateBeforePause = 0.0;
   pSess->pendingSeek = FALSE;
   pSess->pendingSeekPosition = 0.0;
   pSess->bisDLNAContent = FALSE;
   pSess->steadyState = TRUE;
   pSess->steadyStateWindow = 0;
   pSess->rateAfterPause = 0.0;
   pSess->maskRateChangedEvent = FALSE;
   pSess->noVideo = FALSE;
   pSess->bQueryDiscreteAudioInfo = TRUE;
   pSess->isPlaying = FALSE;
   pSess->hasFullGstPipeline = FALSE;
   pSess->hwVideoDecHandle = NULL;
   pSess->hwAudioDecHandle = NULL;

   cgmiDiag_addTimingEntry(DIAG_TIMING_METRIC_LOAD, pSess->diagIndex, uri, 0);

   // 
   // check to see if this is a DLNA url.
   //
   if (0 == strncmp(uri, "http", 4))
   {
      stat = cgmi_utils_is_content_dlna(uri, &pSess->bisDLNAContent);
      if(CGMI_ERROR_SUCCESS != stat)
      {
         g_free(pPipeline);
         printf("Not able to determine whether the content is DLNA\n");
         return stat;
      }
   }

   if (pSess->bisDLNAContent == TRUE)
   {
      //for the gstreamer pipeline to autoplug we have to add
      //dlna+ to the protocol.
      g_snprintf(pSess->playbackURI, MAX_URI_SIZE, "%s%s","dlna+", uri);
   }
   else
   {
      g_strlcpy(pSess->playbackURI, uri, MAX_URI_SIZE);
   }

   g_print("URI: %s\n", pSess->playbackURI);
   if (sessionSettings != NULL)
      g_print("Settings: %s\n", sessionSettings);
   /* Create playback pipeline */

#if GST_CHECK_VERSION(1,0,0)
   g_strlcpy(pPipeline, "playbin uri=", MAX_PIPELINE_SIZE);
#else
   g_strlcpy(pPipeline, "playbin2 uri=", MAX_PIPELINE_SIZE);
#endif

#ifdef USE_DRMPROXY
   /*
   NOTE: This is a temporary solution, as the file extensions cannot be relied upon.
   The entire area and handling of this will change as we move to new paradigms (URL will not change because it's signed, DRM information will arrive from H/E etc).
   */
   if (NULL != strstr(uri, ".m3u8"))
   {

       DRMPROXY_CreateSession(&pSess->drmProxyHandle, &proxy_err, asyncCB, privateData);
       if (proxy_err.errCode != 0)
       {
          GST_ERROR ("DRMPROXY_CreateSession error %lu \n", proxy_err.errCode);
          GST_ERROR ("%s \n", proxy_err.errString);
          pSess->eventCB(pSess->usrParam, (void *)pSess, 0, proxy_err.errCode);
       }
       if (cpblob==NULL)
       {
          DRMPROXY_ParseURL(pSess->drmProxyHandle, pSess->playbackURI, &drmType, &proxy_err);
          if (proxy_err.errCode != 0)
          {
             GST_ERROR ("DRMPROXY_ParseURL error %lu \n", proxy_err.errCode);
             GST_ERROR ("%s \n", proxy_err.errString);
             pSess->eventCB(pSess->usrParam, (void *)pSess, 0, proxy_err.errCode);
          }
          if (drmType == CLEAR || drmType == UNKNOWN)
          {
             g_strlcat(pPipeline, pSess->playbackURI, MAX_PIPELINE_SIZE);
          }
          else
          {
             array = g_strsplit(pSess->playbackURI, "?",  MAX_URI_SIZE);
             g_strlcat(pPipeline, array[0], MAX_PIPELINE_SIZE );
             g_strfreev (array);

             DRMPROXY_Activate(pSess->drmProxyHandle, pSess->playbackURI, strnlen(pSess->playbackURI, MAX_URI_SIZE), NULL, 0, &licenseId, &proxy_err );
             if (proxy_err.errCode != 0)
             {
                GST_ERROR ("DRMPROXY_Activate error %lu \n", proxy_err.errCode);
                GST_ERROR ("%s \n", proxy_err.errString);
                pSess->eventCB(pSess->usrParam, (void *)pSess, 0, proxy_err.errCode);
                drmStatus = 0;
             }
             if ( drmType == VERIMATRIX)
             {
                g_strlcat(pPipeline,"?drmType=verimatrix", MAX_PIPELINE_SIZE);
                g_strlcat(pPipeline, "&LicenseID=",MAX_PIPELINE_SIZE);
                licenseId = 0;
                // LicenseID going to verimatrix plugin is 0 so don't bother with what comes back
                sprintf(buffer, "%" PRIu64, licenseId);
                g_strlcat(pPipeline, buffer, MAX_PIPELINE_SIZE);
             }
             else if (drmType == VGDRM)
             {

                //  FIXME: missing VGDRM specifics, may need to tweak the uri before sending it downstream
                g_strlcat(pPipeline,"?drmType=vgdrm", MAX_PIPELINE_SIZE);
                g_strlcat(pPipeline, "&LicenseID=", MAX_PIPELINE_SIZE);
                sprintf(buffer,"%08llX" , licenseId);
                g_strlcat(pPipeline, buffer, MAX_PIPELINE_SIZE);
             }
          }
      }
      else
      {
          drmType=cpblob->drmType;
          GST_INFO("OTT drmType %d\n",drmType);
          GST_INFO("OTT CPBLOB = %s", cpblob->cpBlob);
          if (drmType==VGDRM)
          {
            do
            {
              ret = vgdrm_construct_ott_cp_blob_data(pSess->drmProxyHandle, cpblob->cpBlob,&DRMPrivateData, &DRMPrivateDataSize);
              if(0 != ret)
              {
                 GST_ERROR("vgdrm_construct_OTT_CPBlob_data (and_assign_asset_id()) failed\n");  
                 drmStatus = 0;
                 break;
              }
              ret = DRMPROXY_Activate(pSess->drmProxyHandle, contentURL, sizeof(contentURL), DRMPrivateData, DRMPrivateDataSize, &licenseId, &proxy_err);
              if(0 != ret)
              {
                  GST_ERROR("DRMPROXY_Activate() failed\n");
                  if (proxy_err.errCode != 0)
                  {
                      GST_ERROR ("DRMPROXY_Activate error: %016llX - %s \n", proxy_err.errCode, proxy_err.errString);
                  }
                  drmStatus = 0;
                  break;
              }

              g_print("DRMPROXY_Activate() Passed."
                      "returned licenseID  : %" PRIu64"\n", 
                      licenseId);
              g_strlcat(pPipeline, pSess->playbackURI, MAX_PIPELINE_SIZE);
              g_strlcat(pPipeline,"?drmType=vgdrm", MAX_PIPELINE_SIZE);
              g_strlcat(pPipeline, "&LicenseID=", MAX_PIPELINE_SIZE);
              sprintf(buffer,"%08llX" , licenseId);
              g_strlcat(pPipeline, buffer, MAX_PIPELINE_SIZE);
            } while (0);
          }
          else
          { 
               g_print("drmType is not VGDRM -not supported!" );
               g_free(pPipeline);
               return CGMI_ERROR_NOT_IMPLEMENTED;
          }
       }

   }
   else
   {
      g_strlcat(pPipeline, pSess->playbackURI, MAX_PIPELINE_SIZE);
   }
#else
   g_strlcat(pPipeline, pSess->playbackURI, MAX_PIPELINE_SIZE);
#endif

   // let's see if we are running on broadcom hardware if we are let's see if we can find there
   // video sink.  If it's there we need to set the flags variable so the pipeline knows to do
   // color transformation and scaling in hardware
#if GST_CHECK_VERSION(1,0,0)
   plugin = gst_registry_find_plugin( gst_registry_get() , "brcmvideosink");
#else
   plugin = gst_registry_find_plugin( gst_registry_get_default() , "brcmvideosink");
#endif
   if ( NULL != plugin )
   {
      bisBroadcomHw = TRUE;
      g_print("Autoplugging on real broadcom hardware\n");
      g_strlcat(pPipeline, " flags= 0x63", MAX_PIPELINE_SIZE);
      gst_object_unref( plugin );
   }

   do
   {
      if (NULL != cpblob)
      {
         pSess->cpblob = g_malloc0(sizeof(cpBlobStruct));
         if (NULL != pSess->cpblob)
            memcpy(pSess->cpblob, cpblob, sizeof(cpBlobStruct));
         else
            GST_WARNING("Could not allocate memory for copying cpblob!\n");
      }
      else
         pSess->cpblob = NULL;

      memset(&pSess->sessionSettings, 0, sizeof(pSess->sessionSettings));

      if (NULL != sessionSettings)
      {
         pSess->sessionSettingsStr = g_strdup(sessionSettings);
         if (NULL == pSess->sessionSettingsStr)
            GST_WARNING("Could not allocate memory for copying session settings!\n");

         if (cgmi_utils_get_json_value(pSess->sessionSettings.audioLanguage, sizeof(pSess->sessionSettings.audioLanguage), sessionSettings, "AudioLanguage") == CGMI_ERROR_SUCCESS)
         {
            g_print("cgmiPlayer: audioLanguage: %s\n", pSess->sessionSettings.audioLanguage);
            strncpy( gDefaultAudioLanguage, pSess->sessionSettings.audioLanguage, sizeof(gDefaultAudioLanguage) );
            gDefaultAudioLanguage[sizeof(gDefaultAudioLanguage) - 1] = 0;
            strncpy( pSess->defaultAudioLanguage, pSess->sessionSettings.audioLanguage, sizeof(pSess->defaultAudioLanguage) );
            pSess->defaultAudioLanguage[sizeof(pSess->defaultAudioLanguage) - 1] = 0;
         }
      }
      else
         pSess->sessionSettingsStr = NULL;

      ctx = gst_parse_context_new ();

      if (0 == drmStatus)  // Check whether DRM failed with license creation (if needed)
      {
         g_print("ERROR: DRM License Creation failed, pipeline will not launch");
         stat = CGMI_ERROR_FAILED;
         break;
      }

      //Using manual pipeline saves at least 10% CPU cycles compared to playbin on Broadcom
      //Investigate how playbin performance can be improved
      if ( TRUE == pSess->bisDLNAContent )
      {
         GST_INFO("Launching manual pipeline\n");

         GstElement *typefind;
         g_free(pPipeline);
         pPipeline = NULL;

         pSess->pipeline = gst_pipeline_new("pipeline");
         pSess->source = gst_element_factory_make("dlnasrc", "dlna-source");
         pSess->demux  = gst_element_factory_make("brcmtsdemux", "tsdemux");
         pSess->videoDecoder = gst_element_factory_make("brcmvideodecoder", "videodecoder");
         pSess->audioDecoder = gst_element_factory_make("brcmaudiodecoder", "audiodecoder");
         pSess->videoSink = gst_element_factory_make("brcmvideosink", "videosink");
         pSess->audioSink = gst_element_factory_make("brcmaudiosink", "audiosink");
         typefind = gst_element_factory_make ("typefind", "typefind");

         if ( NULL == pSess->pipeline ||
              NULL == pSess->source ||
              NULL == pSess->demux ||
              NULL == pSess->videoDecoder ||
              NULL == pSess->audioDecoder || 
              NULL == pSess->videoSink ||
              NULL == pSess->audioSink ||
              NULL == typefind)
         {
            if ( NULL != pSess->source )
               gst_object_unref( GST_OBJECT(pSess->source) );
            if ( NULL != pSess->demux )
               gst_object_unref( GST_OBJECT(pSess->demux) );
            if ( NULL != pSess->videoDecoder )
               gst_object_unref( GST_OBJECT(pSess->videoDecoder) );
            if ( NULL != pSess->audioDecoder )
               gst_object_unref(GST_OBJECT(pSess->audioDecoder) );
            if ( NULL != pSess->audioSink )
               gst_object_unref( GST_OBJECT(pSess->audioSink) );
            stat = CGMI_ERROR_FAILED;
            break;
         }

         g_object_set( G_OBJECT (pSess->source), "uri", pSess->playbackURI, NULL );
#ifdef USE_INFINITE_SOUP_TIMEOUT
         g_print("Setting timeout property of dlnasrc element to 0\n");
         g_object_set( G_OBJECT (pSess->source), "timeout", 0, NULL );
#endif
         g_print("Muting video decoder...\n");
         g_object_set( G_OBJECT(pSess->videoDecoder), "decoder_mute", TRUE, NULL );
         g_print("Muting audio decoder...\n");
         g_object_set( G_OBJECT(pSess->audioDecoder), "decoder_mute", TRUE, NULL );

         gst_bin_add_many( GST_BIN (pSess->pipeline),
                           pSess->source,
                           typefind,
                           pSess->demux, 
                           pSess->videoDecoder, 
                           pSess->audioDecoder, 
                           pSess->videoSink, 
                           pSess->audioSink, 
                           NULL );

         if ( TRUE != gst_element_link(pSess->source, typefind) )
         {
            GST_WARNING("Could not link source to demux\n");
            stat = CGMI_ERROR_FAILED;
            break;
         }
         if ( TRUE != gst_element_link(pSess->videoDecoder, pSess->videoSink) )
         {
            GST_WARNING("Could not link video decoder to video sink\n");
            stat = CGMI_ERROR_FAILED;
            break;
         }
         if ( TRUE != gst_element_link(pSess->audioDecoder, pSess->audioSink) )
         {
            GST_WARNING("Could not link audio decoder to audio sink\n");
            stat = CGMI_ERROR_FAILED;
            break;
         }

         g_signal_connect (G_OBJECT (typefind), "have_type", G_CALLBACK (cgmi_gst_have_type), pSess);
         g_signal_connect( pSess->demux, "pad-added", G_CALLBACK (cgmi_gst_pad_added), pSess );
         g_signal_connect( pSess->demux, "no-more-pads", G_CALLBACK (cgmi_gst_no_more_pads), pSess );
         g_signal_connect( pSess->demux, "psi-info", G_CALLBACK(cgmi_gst_psi_info), pSess );
      }

      else
      {
         GST_INFO("Launching the pipeline with :\n%s\n", pPipeline);

         pSess->pipeline = gst_parse_launch_full(pPipeline,
                                                 ctx,
                                                 GST_PARSE_FLAG_FATAL_ERRORS,
                                                 &g_error_str);

         g_free(pPipeline);
         pPipeline = NULL;

         if (pSess->pipeline == NULL)
         {
            GST_WARNING("PipeLine was not able to be created\n");
            if (g_error_str)
            {
               g_print("Error with pipeline:%s \n", g_error_str->message);
            }

            //now if there are missing elements that are needed in the pipeline, lets' dump
            //those out.
            if(g_error_matches(g_error_str, GST_PARSE_ERROR, GST_PARSE_ERROR_NO_SUCH_ELEMENT))
            {
               guint i=0;
               g_print("Missing these plugins:\n");
               arr = gst_parse_context_get_missing_elements(ctx);
               while (arr[i] != NULL)
               {
                  g_print("%s\n",arr[i]);
                  i++;
               }
               g_strfreev(arr);
            }
            stat = CGMI_ERROR_NOT_IMPLEMENTED;
            break;
         }
      }

      /* Add bus watch for events and messages */
      pSess->bus = gst_element_get_bus( pSess->pipeline );

      if (pSess->bus == NULL)
      {
         GST_ERROR("The bus is null in the pipeline\n");
      }

      // enable the notifications.
      pSess->sourceWatch = gst_bus_create_watch(pSess->bus);
      g_source_set_callback(pSess->sourceWatch, (GSourceFunc)cgmi_gst_handle_msg, pSess, NULL);
      g_source_attach(pSess->sourceWatch, pSess->thread_ctx);

      if (pSess->bisDLNAContent == FALSE || bisBroadcomHw == FALSE)
      {
         g_signal_connect( pSess->pipeline, "element-added",
            G_CALLBACK(cgmi_gst_element_added), pSess );

         g_signal_connect( pSess->pipeline, "notify::source",
            G_CALLBACK(cgmi_gst_notify_source), pSess );
      }

      sret = cisco_gst_setState( pSess, GST_STATE_PAUSED );
      if(GST_STATE_CHANGE_ASYNC == sret)
      {
         /* Wait for the async sate change to complete */
         sret = gst_element_get_state(pSess->pipeline, NULL, NULL, 10 * GST_SECOND);
         if(GST_STATE_CHANGE_SUCCESS != sret)
         {
            GST_ERROR("State change to PAUSED failed\n");
            stat = CGMI_ERROR_FAILED;
         }
         else
         {
            if(FALSE == pSess->suppressLoadDone)
            {
               pSess->eventCB(pSess->usrParam, (void*)pSess, NOTIFY_LOAD_DONE, 0);
            }
            else
            {
               pSess->suppressLoadDone = FALSE;
            }

            cgmi_GetHwDecHandles(pSess);
            pSess->hasFullGstPipeline = TRUE;
         }
      }
      /* This has been added for rtp live stream - it could add issues with rtsp/rtp vod playing */
      else if(GST_STATE_CHANGE_NO_PREROLL== sret)
      {
         if(FALSE == pSess->suppressLoadDone)
         {
            pSess->eventCB(pSess->usrParam, (void*)pSess, NOTIFY_LOAD_DONE, 0);
         }
         else
         {
            pSess->suppressLoadDone = FALSE;
         }
      }

   }while(0);

   // free memory
   gst_parse_context_free(ctx);
   if (g_error_str)
   {
      g_error_free(g_error_str);
   }

   if ( CGMI_ERROR_SUCCESS != stat )
   {
      if ( NULL != pPipeline )
      {
         g_free(pPipeline);
         pPipeline = NULL;
      }
      if ( NULL != pSess->cpblob )
      {
         g_free(pSess->cpblob);
         pSess->cpblob = NULL;
      }
      if ( NULL != pSess->sessionSettingsStr )
      {
         g_free(pSess->sessionSettingsStr);
         pSess->sessionSettingsStr = NULL;
      }
   }

   return stat;
}


cgmi_Status cgmi_Unload  ( void *pSession )
{

   tSession *pSess = (tSession*)pSession;
   cgmi_Status stat = CGMI_ERROR_SUCCESS;
   // we need to tear down the pipeline

   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      g_print("%s:Invalid session handle\n", __FUNCTION__);
      return CGMI_ERROR_INVALID_HANDLE;
   }

   cgmiDiag_addTimingEntry(DIAG_TIMING_METRIC_UNLOAD, pSess->diagIndex, pSess->playbackURI, 0);

   do
   {
      if ( NULL != pSess->sourceWatch )
      {
         GST_INFO("removing source from main loop context\n");
         g_source_destroy( pSess->sourceWatch );
         g_source_unref( pSess->sourceWatch );
         pSess->sourceWatch = NULL;
      }

      //Signal psi callback on unload in case it is blocked on PID selection
      g_mutex_lock( pSess->autoPlayMutex );
      if ( TRUE == pSess->waitingOnPids )
         g_cond_signal( pSess->autoPlayCond );
      g_mutex_unlock( pSess->autoPlayMutex );

      if (pSess->pipeline)
      {
         int refcount;
         g_print ("Changing state of pipeline to NULL \n");
         gst_element_set_state (pSess->pipeline, GST_STATE_NULL);

         g_print ("Deleting pipeline\n");
         refcount = GST_OBJECT_REFCOUNT(pSess->pipeline);
         g_print ("Pipeline ref count on tear down is %d (should be 1)\n", refcount);
         gst_object_unref( GST_OBJECT(pSess->pipeline) );
         pSess->pipeline = NULL;
      }
      if (pSess->bus)
      {
         gst_object_unref( GST_OBJECT(pSess->bus) );
         pSess->bus = NULL;
      }

      if ( NULL != pSess->cpblob )
      {
         g_free(pSess->cpblob);
         pSess->cpblob = NULL;
      }

      if ( NULL != pSess->sessionSettingsStr )
      {
         g_free(pSess->sessionSettingsStr);
         pSess->sessionSettingsStr = NULL;
      }

      pSess->demux = NULL;
      pSess->udpsrc = NULL;
      pSess->hlsDemux = NULL;
      pSess->videoSink = NULL;
      pSess->audioSink = NULL;
      pSess->videoDecoder = NULL;
      pSess->audioDecoder = NULL;
      pSess->numAudioLanguages = 0;
      pSess->newAudioLanguage[0] = '\0';
      pSess->currAudioLanguage[0] = '\0';
      pSess->hasFullGstPipeline = FALSE;
      pSess->playbackURI[0] = '\0';
      pSess->hwAudioDecHandle = NULL;
      pSess->hwVideoDecHandle = NULL;

   }while (0);
   g_print("Exiting %s pipeline is now null\n",__FUNCTION__);
   return stat;
}

cgmi_Status cgmi_Play (void *pSession, int autoPlay)
{
   tSession *pSess = (tSession*)pSession;
   cgmi_Status stat = CGMI_ERROR_SUCCESS;

   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      g_print("%s:Invalid session handle\n", __FUNCTION__);
      return CGMI_ERROR_INVALID_HANDLE;
   }
   pSess->isPlaying = TRUE;
   cgmiDiag_addTimingEntry(DIAG_TIMING_METRIC_PLAY, pSess->diagIndex, pSess->playbackURI, 0);

   pSess->autoPlay = autoPlay;

   if ( TRUE == autoPlay )
   {
      g_mutex_lock( pSess->autoPlayMutex );
      if ( TRUE == pSess->waitingOnPids )
         g_cond_signal( pSess->autoPlayCond );
      g_mutex_unlock( pSess->autoPlayMutex );
   }

   pSess->rate = 1.0;

   cisco_gst_setState( pSess, GST_STATE_PLAYING );

   return stat;
}

cgmi_Status cgmi_SetRate (void *pSession, float rate)
{
   tSession *pSess = (tSession*)pSession;
   cgmi_Status stat = CGMI_ERROR_SUCCESS;
   GstFormat format = GST_FORMAT_TIME;
   GstEvent *seek_event=NULL;
   gint64 position;
   GstState  curState;
   gboolean is_live = FALSE;
   gboolean in_tsb = FALSE;

   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      GST_ERROR("%s:Invalid session handle\n", __FUNCTION__);
      return CGMI_ERROR_INVALID_HANDLE;
   }

#if 0
   if(TRUE != isRateSupported(pSession, rate))
   {
      GST_ERROR("rate %f is not supported. Call getRates API to get the list of supported rates\n", rate);
      return CGMI_ERROR_BAD_PARAM;
   }
#endif
   if ((!pSess->isPlaying)&&(rate==1.0))
   {
       pSess->rate = 1.0;
       g_print("%s: cgmi_SetRate was called with rate %f before cgmi_Play was called.No need to proceed in this function.Returning now.\n",__FUNCTION__,rate);
       return stat;
   }
   gst_element_get_state(pSess->pipeline, &curState, NULL, GST_CLOCK_TIME_NONE);
   
   /* Obtain the current position, needed for the seek event before a flush.
    * Position returned is sometimes incorrect after a flush
    */
#if GST_CHECK_VERSION(1,0,0)
   if (!gst_element_query_position (pSess->pipeline, format, &position))
#else
   if (!gst_element_query_position (pSess->pipeline, &format, &position))
#endif
   {
      GST_ERROR ("Unable to retrieve current position.\n");
      return CGMI_ERROR_FAILED;
   }

   if (rate == 0.0)
   {
      cisco_gst_setState( pSess, GST_STATE_PAUSED );
      /* For example: nx->0x->0x->nx */
      if(0.0 != pSess->rate)
      {
         pSess->rateBeforePause = pSess->rate;
      }
      pSess->rate = rate;
      return stat;
   }
   else if (curState == GST_STATE_PAUSED)
   {
      /* if not (nx->0x->nx) then unpause and seek to set the new trick rate.
       * else if (nx-0x->nx)
       *   if 1x and live and not playing from TSB then
       *      flush, unpause and seek to the pause pos to switch to TSB with rate 1x.
       *   else just unpause and return.
       */
      
      if(rate == pSess->rateBeforePause)
      {
         if(TRUE == pSess->bisDLNAContent)
         {
            g_object_get(pSess->source, "is-live", &is_live, NULL);
            g_object_get(pSess->source, "in-tsb", &in_tsb, NULL);
         }

         /* The switch to TSB logic is implement here instead of dlnasrc because
          * dlnasrc cannot differentiate pause/unpause from pipeline state transition 
          */
         if((1.0 == rate) && (TRUE == is_live) && (FALSE == in_tsb))
         {
            GST_WARNING("Switching to TSB by seeking to the pause pos: %lld at 1x\n", position);
            /* Flush to avoid displaying few more frames before the seek to pause pos */
            cgmi_flush_pipeline(pSess);
         }
         //If the rate before pause was 1.0, we just want to unpause and return
         //because otherwise we will execute a seek following unpause and it will cause an
         //unnecessary flush and position skip. Note that we need to execute the seek for any
         //rate other than 1.0 even if the rate stays the same before and after pause. This is
         //because some elements in the pipeline may reset their internal rate state going through
         //pause (such as HLS plugin element which resets its internal rate to 0x on pause) and
         //we want to restore internal rate state of such elements to the rate we had before pause
         //by executing the seek below
         else if (1.0 == rate)
         {
            cisco_gst_setState( pSess, GST_STATE_PLAYING );
            pSess->rate = rate;
            return stat;
         }
      }
      else
      {
         /* We have to put the pipeline in playing state before sending a seek to transition to the 
          * new requested play speed. On pause to play transition, a rate changed event with
          * rateBeforePause is sent by the pipeline followed by an another rate changed event with 
          * the request rate when it handles the seek event to transition to the new requested play 
          * speed. So mask the unexpected rate changed event with rateBeforePause.
          */
         pSess->maskRateChangedEvent = TRUE;
         pSess->rateAfterPause = rate;
      }
      /* Go to playing state for non 0x speed */
      cisco_gst_setState( pSess, GST_STATE_PLAYING );
   }
   pSess->steadyState = FALSE;
   pSess->steadyStateWindow = 0;
   
   seek_event = gst_event_new_seek (rate, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
                                    GST_SEEK_TYPE_SET, position, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
   if (seek_event)
   {
      /* Send the event */
      if(TRUE != gst_element_send_event (pSess->pipeline, seek_event))
      {
         GST_ERROR("gst_element_send_event() failed");
         return CGMI_ERROR_FAILED;
      }
      
      if(((0.0 == pSess->rate) || (1.0 == pSess->rate)) && ((rate > 1.0) || (rate < -1.0)) ||
         ((0.0 == rate) || (1.0 == rate)) && ((pSess->rate > 1.0) || (pSess->rate < -1.0)))
      {
         if (rate > 1.0 || rate < -1.0)
         {
            GST_WARNING("Muting audio decoder...\n");
            g_object_set( G_OBJECT(pSess->audioDecoder), "decoder_mute", TRUE, NULL );
         }
         else
         {
            GST_WARNING("Unmuting audio decoder...\n");
            g_object_set( G_OBJECT(pSess->audioDecoder), "decoder_mute", FALSE, NULL );
         }
      }
   }
   else
   {
      GST_ERROR ("gst_event_new_seek() failed\n");
      return CGMI_ERROR_FAILED;
   }
   
   pSess->rate = rate;
   
   return stat;
}

cgmi_Status cgmi_SetPosition (void *pSession, float position)
{

   tSession *pSess = (tSession*)pSession;
   cgmi_Status stat = CGMI_ERROR_SUCCESS;
   GstState state;

   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      g_print("%s:Invalid session handle\n", __FUNCTION__);
      return CGMI_ERROR_INVALID_HANDLE;
   }

   do
   {
      state = cisco_gst_getState( pSess );

      g_print("Setting position to %f (ns), pipeline state: %d\n", (position* GST_SECOND), state);

      if ( state != GST_STATE_PLAYING )
      {
         g_print("Pipeline not playing yet, delaying seek...\n");
         pSess->pendingSeekPosition = position;
         pSess->pendingSeek = TRUE;
         //If the seek request came in when the pipeline was in ready state 
         //(e.g., resume from an offset), set it to paused to complete the seek         
         if ( 0.0 == pSess->rate )
            cisco_gst_setState( pSess, GST_STATE_PAUSED );
         return stat;
      }

      pSess->pendingSeek = FALSE;

      pSess->steadyState = FALSE;
      pSess->steadyStateWindow = 0;

      if( !gst_element_seek( pSess->pipeline,
               pSess->rate,
               GST_FORMAT_TIME,
               GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
               GST_SEEK_TYPE_SET,
               (gint64)(position * GST_SECOND),
               GST_SEEK_TYPE_NONE,
               GST_CLOCK_TIME_NONE ))
      {
         GST_ERROR("Seek Failed\n");
         stat = CGMI_ERROR_FAILED; 
      }

   } while (0);
   return stat;
}

cgmi_Status cgmi_GetPosition (void *pSession, float *pPosition)
{

   tSession *pSess = (tSession*)pSession;
   gint64 curPos = 0;
   GstFormat gstFormat = GST_FORMAT_TIME;
   cgmi_Status stat = CGMI_ERROR_SUCCESS;

   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      g_print("%s:Invalid session handle\n", __FUNCTION__);
      return CGMI_ERROR_INVALID_HANDLE;
   }

   // this returns nano seconds, change it to seconds.
   do
   {
#if GST_CHECK_VERSION(1,0,0)
      gst_element_query_position( pSess->pipeline, gstFormat, &curPos );
#else
      gst_element_query_position( pSess->pipeline, &gstFormat, &curPos );
#endif

      GST_INFO("Position: %lld (seconds)\n", (curPos/GST_SECOND) );
      *pPosition = (curPos/GST_SECOND);

   } while (0);
   return stat;

}
cgmi_Status cgmi_GetDuration (void *pSession, float *pDuration, cgmi_SessionType *type)
{

   tSession *pSess = (tSession*)pSession;
   cgmi_Status stat = CGMI_ERROR_SUCCESS;
   gint64 Duration = 0;
   GstFormat gstFormat = GST_FORMAT_TIME;
   gboolean is_live = FALSE;

   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      g_print("%s:Invalid session handle\n", __FUNCTION__);
      return CGMI_ERROR_INVALID_HANDLE;
   }

   do
   {
#if GST_CHECK_VERSION(1,0,0)
      gst_element_query_duration( pSess->pipeline, gstFormat, &Duration );
#else
      gst_element_query_duration( pSess->pipeline, &gstFormat, &Duration );
#endif

      GST_INFO("Stream: %s\n", pSess->playbackURI );
      GST_INFO("Position: %lld (seconds)\n", (Duration/GST_SECOND) );
      *pDuration = (float)(Duration/GST_SECOND);
      
      if(TRUE == pSess->bisDLNAContent)
      {
         g_object_get(pSess->source, "is-live", &is_live, NULL);
      }

      if(TRUE == is_live)
      {
         *type = LIVE;
      }
      else
      {
         *type = FIXED;
      }

   } while (0);
   return stat;
}

cgmi_Status cgmi_GetRates (void *pSession, float pRates[], unsigned int *pNumRates)
{
   tSession *pSess = (tSession*)pSession;
   cgmi_Status stat = CGMI_ERROR_FAILED ;

   GstStructure *structure = NULL;
   GstQuery     *query = NULL;
   char         *pTrickSpeedsStr = NULL;
   gboolean     ret = FALSE;
   unsigned int inNumRates = 0;
   char         *token = NULL;
   char         seps[] = ",";
   int          ii = 0;

   g_print("%s: ENTER %s >>>>\n", __FILE__, __FUNCTION__);

   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      g_print("%s:Invalid session handle\n", __FUNCTION__);
      return CGMI_ERROR_INVALID_HANDLE;
   }

   do
   {
      if(NULL == pRates)
      {
         g_print("%s: %s(%d): param pRates is NULL\n", __FILE__, __FUNCTION__, __LINE__);
         stat = CGMI_ERROR_BAD_PARAM;
         break;
      }
      if(NULL == pNumRates)
      {
         g_print("%s: %s(%d): param pNumSpeeds is NULL\n", __FILE__, __FUNCTION__, __LINE__);
         stat = CGMI_ERROR_BAD_PARAM;
         break;
      }
      if(*pNumRates == 0)
      {
         g_print("%s: %s(%d): *pNumRates is 0 which is invalid\n", __FILE__, __FUNCTION__, __LINE__);
         stat = CGMI_ERROR_BAD_PARAM;
         break;
      }

      inNumRates = *pNumRates;
      g_print("%s: %s(%d): inNumRates = %u\n", __FILE__, __FUNCTION__, __LINE__, inNumRates);

      pRates[0] = 1;
      *pNumRates = 1;
      stat = CGMI_ERROR_SUCCESS;

      structure = gst_structure_new ("getTrickSpeeds",
                                     "trickSpeedsStr", G_TYPE_STRING, NULL,
                                     NULL);

#if GST_CHECK_VERSION(1,0,0)
      query = gst_query_new_custom (GST_QUERY_CUSTOM, structure);
#else
      query = gst_query_new_application (GST_QUERY_CUSTOM, structure);
#endif

      ret = gst_element_query (pSess->pipeline, query);
      if(ret)
      {
         structure = (GstStructure *)gst_query_get_structure (query);

         pTrickSpeedsStr = g_value_dup_string (gst_structure_get_value (structure,
                                               "trickSpeedsStr"));
         if(NULL == pTrickSpeedsStr)
         {
            g_print("%s: %s(%d): g_value_dup_string() failed\n", __FILE__, __FUNCTION__, __LINE__);
            break;
         }

         g_print("%s: %s(%d): pTrickSpeedsStr: %s\n", __FILE__, __FUNCTION__, __LINE__, pTrickSpeedsStr);

         if(0 == strlen(pTrickSpeedsStr))
         {
            g_print("%s: %s(%d): getTrickSpeeds GST_QUERY_CUSTOM returned an empty string" , __FILE__, __FUNCTION__, __LINE__);
            break;
         }

         for(ii = 0; ii < strlen(pTrickSpeedsStr); ii++)
         {
            if((!isdigit(pTrickSpeedsStr[ii])) && (pTrickSpeedsStr[ii] != '.') &&
               (pTrickSpeedsStr[ii] != ',') && (pTrickSpeedsStr[ii] != '-'))
            {
               g_print("%s: %s(%d): pTrickSpeedsStr: %s is invalid. It has a char(%c) that is not a digit, '.' and ','\n",
                       __FILE__, __FUNCTION__, __LINE__, pTrickSpeedsStr, pTrickSpeedsStr[ii]);
               break;
            }
         }

         if(ii >= strlen(pTrickSpeedsStr))
         {
            *pNumRates = 0;
            token = strtok(pTrickSpeedsStr, seps);
            while((token != NULL) && (*pNumRates < inNumRates))
            {
               sscanf(token, "%f", &pRates[(*pNumRates)++]);
               /*g_print("%s: %s(%d): pRates[%d] = %f\n", __FILE__, __FUNCTION__, __LINE__,
                       *pNumRates - 1, pRates[*pNumRates - 1]);*/
               token = strtok(NULL, seps);
            }
         }
      }
      else
      {
         g_print("%s: %s(%d): GST_QUERY_CUSTOM - getTrickSpeeds failed / unsupported\n", __FILE__, __FUNCTION__, __LINE__);
      }

   } while (0);

   if( NULL != query )
   {
      gst_query_unref ( query );
      query = NULL;
   }
   if( NULL != pTrickSpeedsStr )
   {
      g_free( pTrickSpeedsStr );
      pTrickSpeedsStr = NULL;
   }

   return stat;
}

cgmi_Status cgmi_canPlayType(const char *type, int *pbCanPlay )
{
   return CGMI_ERROR_NOT_IMPLEMENTED;
}

cgmi_Status cgmi_SetVideoRectangle( void *pSession, int srcx, int srcy, int srcw, int srch, int dstx, int dsty, int dstw, int dsth )
{
   char *ptr;
   tSession *pSess = (tSession*)pSession;

   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      g_print("%s:Invalid session handle\n", __FUNCTION__);
      return CGMI_ERROR_INVALID_HANDLE;
   }

   if ( srcx < 0 )
      srcx = 0;
   if ( srcx >= VIDEO_MAX_WIDTH )
      srcx = VIDEO_MAX_WIDTH - 1;

   if ( srcy < 0 )
      srcy = 0;
   if ( srcy >= VIDEO_MAX_HEIGHT )
      srcy = VIDEO_MAX_HEIGHT - 1;

   if ( srcx + srcw > VIDEO_MAX_WIDTH )
      srcw = VIDEO_MAX_WIDTH - srcx;
   if ( srcy + srch > VIDEO_MAX_HEIGHT )
      srch = VIDEO_MAX_HEIGHT - srcy;

   if ( dstx < 0 )
      dstx = 0;
   if ( dstx >= VIDEO_MAX_WIDTH )
      dstx = VIDEO_MAX_WIDTH - 1;

   if ( dsty < 0 )
      dsty = 0;
   if ( dsty >= VIDEO_MAX_HEIGHT )
      dsty = VIDEO_MAX_HEIGHT - 1;

   if ( dstx + dstw > VIDEO_MAX_WIDTH )
      dstw = VIDEO_MAX_WIDTH - dstx;
   if ( dsty + dsth > VIDEO_MAX_HEIGHT )
      dsth = VIDEO_MAX_HEIGHT - dsty;

   if ( dstw < 10 || dsth < 10 )
   {
      g_print("Adjusted video dest size too small, must be bigger than 10x10 pixels!");
      return CGMI_ERROR_BAD_PARAM;
   }

   pSess->vidSrcRect.x = srcx;
   pSess->vidSrcRect.y = srcy;
   pSess->vidSrcRect.w = srcw;
   pSess->vidSrcRect.h = srch;

   pSess->vidDestRect.x = dstx;
   pSess->vidDestRect.y = dsty;
   pSess->vidDestRect.w = dstw;
   pSess->vidDestRect.h = dsth;

   if ( NULL != pSess->videoSink )
   {
      gchar dim[64];
      snprintf( dim, sizeof(dim), "%d,%d,%d,%d,%d,%d,%d,%d",
                pSess->vidSrcRect.x, pSess->vidSrcRect.y, pSess->vidSrcRect.w, pSess->vidSrcRect.h,
                pSess->vidDestRect.x, pSess->vidDestRect.y, pSess->vidDestRect.w, pSess->vidDestRect.h);
      g_object_set( G_OBJECT(pSess->videoSink), "window_set", dim, NULL );
   }

   return CGMI_ERROR_SUCCESS;
}

cgmi_Status cgmi_GetVideoResolution( void *pSession, int *srcw, int *srch )
{
   gint64 res;
   tSession *pSess = (tSession*)pSession;

   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      g_print("%s:Invalid session handle\n", __FUNCTION__);
      return CGMI_ERROR_INVALID_HANDLE;
   }

   if ( NULL == srcw || NULL == srch )
   {
      g_print("%s: Invalid parameters passed!\n", __FUNCTION__);
      return CGMI_ERROR_BAD_PARAM;
   }

   if ( NULL == pSess->videoDecoder )
   {
      g_print("%s:No decoder associated with session!\n", __FUNCTION__);
      return CGMI_ERROR_BAD_PARAM;
   }

   g_object_get( pSess->videoDecoder, "source_res", &res, NULL );

   if (res == 0)
   {
      return CGMI_ERROR_NOT_ACTIVE;
   }

   *srcw = (int)(res >> 32);
   *srch = (int)(res & 0xFFFFFFFF);

   return CGMI_ERROR_SUCCESS;
}

cgmi_Status cgmi_GetVideoDecoderIndex(void *pSession, int *idx)
{
   tSession *pSess = (tSession*)pSession;
   int videoDecoderIndex = 0;

   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      g_print("%s:Invalid session handle\n", __FUNCTION__);
      return CGMI_ERROR_INVALID_HANDLE;
   }

   if ( NULL == idx )
   {
      g_print("%s:Null index pointer passed for video decoder!\n", __FUNCTION__);
      return CGMI_ERROR_BAD_PARAM;
   }

   if ( NULL == pSess->videoDecoder )
   {
      g_print("%s:No decoder associated with session!\n", __FUNCTION__);
      return CGMI_ERROR_BAD_PARAM;
   }

   g_object_get( pSess->videoDecoder, "videodecoder_id", &videoDecoderIndex, NULL );

   if (videoDecoderIndex == 0)  // 0 is the uninitialized value.
   {
      return CGMI_ERROR_NOT_ACTIVE; 
   }

   *idx = videoDecoderIndex;

   return CGMI_ERROR_SUCCESS;
}

cgmi_Status cgmi_GetNumAudioLanguages (void *pSession,  int *count)
{
   cgmi_Status  stat = CGMI_ERROR_FAILED;
   tSession     *pSess = (tSession*)pSession;

   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      GST_ERROR("%s:Invalid session handle\n", __FUNCTION__);
      stat = CGMI_ERROR_INVALID_HANDLE;
      return stat;
   }

   g_rec_mutex_lock(&pSess->psiMutex);

   do
   {
      if ( NULL == count )
      {
         GST_ERROR("Null count pointer passed for audio language!\n");
         stat = CGMI_ERROR_BAD_PARAM;
         break;
      }

      stat = cgmi_queryDiscreteAudioInfo(pSess);
      if(CGMI_ERROR_SUCCESS != stat)
      {
         GST_ERROR("Discrete audio stream(s) info query failed\n");
         break;
      }

      *count = pSess->numAudioLanguages;

      stat = CGMI_ERROR_SUCCESS;
   }while(0);

   g_rec_mutex_unlock(&pSess->psiMutex);

   return stat;
}

cgmi_Status cgmi_GetAudioLangInfo (void *pSession, int index, char* buf, int bufSize, char *isEnabled)
{
   cgmi_Status  stat = CGMI_ERROR_FAILED;
   tSession *pSess = (tSession*)pSession;

   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      g_print("%s:Invalid session handle\n", __FUNCTION__);
      stat = CGMI_ERROR_INVALID_HANDLE;
      return stat;
   }

   g_rec_mutex_lock(&pSess->psiMutex);

   do
   {
      if ( NULL == buf )
      {
         g_print("Null buffer pointer passed for audio language!\n");
         stat = CGMI_ERROR_BAD_PARAM;
         break;
      }

      if ( NULL == isEnabled )
      {
         g_print("Null pointer passed for enabled status!\n");
         stat = CGMI_ERROR_BAD_PARAM;
         break;
      }

      stat = cgmi_queryDiscreteAudioInfo(pSess);
      if(CGMI_ERROR_SUCCESS != stat)
      {
         GST_ERROR("Discrete audio stream(s) info query failed\n");
         break;
      }

      if ( index > pSess->numAudioLanguages - 1 || index < 0 )
      {
         g_print("Bad index value passed for audio language!\n");
         stat = CGMI_ERROR_BAD_PARAM;
         break;
      }

      if (pSess->audioLanguageIndex != INVALID_INDEX &&
          pSess->audioLanguages[index].index == pSess->audioLanguageIndex)
      {
         *isEnabled = TRUE;
      }
      else
      {
         *isEnabled = FALSE;
      }

      strncpy( buf, pSess->audioLanguages[index].isoCode, bufSize );
      buf[bufSize - 1] = 0;

      stat = CGMI_ERROR_SUCCESS;

   }while(0);

   g_rec_mutex_unlock(&pSess->psiMutex);

   return stat;
}

cgmi_Status cgmi_GetActiveSessionsInfo(sessionInfo *sessInfoArr[], int *numSessOut)
{
   cgmi_Status stat = CGMI_ERROR_FAILED;
   tSession    *pSess = NULL;
   GList       *list = NULL;
   guint       listLen = 0;

   g_mutex_lock(&gSessionListMutex);
   do
   {
      if(NULL == sessInfoArr)
      {
         GST_ERROR("sessInfoArr param is NULL\n");
         break;
      }

      if(NULL == numSessOut)
      {
         GST_ERROR("numSessOut param is NULL\n");
         break;
      }
      *sessInfoArr = NULL;
      *numSessOut = 0;

      if(NULL == gSessionList)
      {
         GST_WARNING("No Active CGMI sessions\n");
         stat = CGMI_ERROR_SUCCESS;
         break;
      }

      listLen = g_list_length(gSessionList);
      GST_WARNING("Total CGMI sessions: %d\n", listLen);

      *sessInfoArr = g_malloc0(sizeof(sessionInfo) * listLen);
      if(NULL == *sessInfoArr)
      {
         GST_ERROR("Failed to alloc sessInfoArr array\n");
         break;
      }

      for(list = gSessionList; list != NULL; list = list->next)
      {
         pSess = (tSession *)list->data;

         if(FALSE == pSess->hasFullGstPipeline)
         {
            continue;
         }
         if(strncmp("dlna+", pSess->playbackURI, strlen("dlna+")))
         {
            g_strlcpy((*sessInfoArr)[*numSessOut].uri, pSess->playbackURI,
                  sizeof((*sessInfoArr)[*numSessOut].uri));
         }
         else
         {
            g_strlcpy((*sessInfoArr)[*numSessOut].uri, &(pSess->playbackURI[strlen("dlna+")]),
                  sizeof((*sessInfoArr)[*numSessOut].uri));
         }
         (*sessInfoArr)[*numSessOut].hwAudioDecHandle = pSess->hwAudioDecHandle;
         (*sessInfoArr)[*numSessOut].hwVideoDecHandle = pSess->hwVideoDecHandle;

         (*numSessOut)++;
      }

      GST_WARNING("Total active CGMI sessions: %d\n", *numSessOut);

      stat = CGMI_ERROR_SUCCESS;
   }while(0);

   g_mutex_unlock(&gSessionListMutex);

   return stat;
}

cgmi_Status cgmi_SetAudioStream ( void *pSession, int index )
{
   cgmi_Status stat = CGMI_ERROR_FAILED;
   tSession    *pSess = (tSession*)pSession;
   gint        ii = 0;
   gchar       audioLanguage[4] = "";
   gchar       *pAudioLanguage = NULL;
   gint        autoPlay;
   gchar       *uri = NULL;
   void        *cpblob = NULL;
   float       position = 0.0;
   gint        currAudioLangArrIdx = INVALID_INDEX;

   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      GST_ERROR("%s:Invalid session handle\n", __FUNCTION__);
      stat = CGMI_ERROR_INVALID_HANDLE;
      return stat;
   }

   g_rec_mutex_lock(&pSess->psiMutex);

   do
   {
      stat = cgmi_queryDiscreteAudioInfo(pSess);
      if(CGMI_ERROR_SUCCESS != stat)
      {
         GST_ERROR("Discrete audio stream(s) info query failed\n");
         break;
      }

      if ( index > pSess->numAudioLanguages - 1 || index < 0 )
      {
         GST_ERROR("Bad index value passed for audio language!\n");
         stat = CGMI_ERROR_BAD_PARAM;
         break;
      }

      if((strlen(pSess->currAudioLanguage) == 0) && (NULL != pSess->hlsDemux))
      {
         g_object_get( G_OBJECT(pSess->hlsDemux), "audio-language", &pAudioLanguage, NULL );
         g_strlcpy(pSess->currAudioLanguage, pAudioLanguage, sizeof(pSess->currAudioLanguage));
      }

      for(ii = 0; ii < pSess->numAudioLanguages; ii++)
      {
         if(!strncmp(pSess->currAudioLanguage, pSess->audioLanguages[ii].isoCode,
                  sizeof(pSess->audioLanguages[ii].isoCode)))
         {
            currAudioLangArrIdx = ii;
            break;
         }
      }

      if(INVALID_INDEX != currAudioLangArrIdx)
      {
         /* Log for debugging */
         GST_WARNING("Switching from %s audio language %s to %s audio language %s\n",
               (pSess->audioLanguages[currAudioLangArrIdx].bDiscrete == TRUE)? "discrete":"muxed",
               pSess->audioLanguages[currAudioLangArrIdx].isoCode,
               (pSess->audioLanguages[index].bDiscrete == TRUE)? "discrete":"muxed",
               pSess->audioLanguages[index].isoCode);
      }

      if((INVALID_INDEX != currAudioLangArrIdx) && (pSess->audioLanguages[index].bDiscrete !=
               pSess->audioLanguages[currAudioLangArrIdx].bDiscrete))
      {
         /* Muxed <-> Discrete */
         autoPlay = pSess->autoPlay;
         cpblob = pSess->cpblob;
         uri = g_strdup(pSess->playbackURI);
         g_strlcpy(audioLanguage, pSess->audioLanguages[index].isoCode, sizeof(audioLanguage));

         stat = cgmi_GetPosition(pSess, &position);
         if(CGMI_ERROR_SUCCESS != stat)
         {
            GST_ERROR("cgmi_GetPosition() failed\n");
            break;
         }

         stat = cgmi_Unload(pSess);
         if(CGMI_ERROR_SUCCESS != stat)
         {
            GST_ERROR("cgmi_Unload() failed\n");
            break;
         }

         g_strlcpy(pSess->newAudioLanguage, audioLanguage, sizeof(pSess->newAudioLanguage));
         pSess->suppressLoadDone = TRUE;

         g_rec_mutex_unlock(&pSess->psiMutex);

         stat = cgmi_Load(pSess, uri, cpblob, pSess->sessionSettingsStr);
         if(CGMI_ERROR_SUCCESS != stat)
         {
            GST_ERROR("cgmi_Load() failed\n");
            g_rec_mutex_lock(&pSess->psiMutex);
            break;
         }

         stat = cgmi_SetPosition(pSess, position);
         if(CGMI_ERROR_SUCCESS != stat)
         {
            GST_ERROR("cgmi_SetPosition() failed\n");
            g_rec_mutex_lock(&pSess->psiMutex);
            break;
         }

         stat = cgmi_Play(pSess, autoPlay);
         if(CGMI_ERROR_SUCCESS != stat)
         {
            GST_ERROR("cgmi_Play() failed\n");
            g_rec_mutex_lock(&pSess->psiMutex);
            break;
         }

         g_rec_mutex_lock(&pSess->psiMutex);

         g_strlcpy(pSess->currAudioLanguage, pSess->newAudioLanguage, sizeof(pSess->currAudioLanguage));
         stat = CGMI_ERROR_SUCCESS;
      }
      else if((INVALID_INDEX != currAudioLangArrIdx) &&
              (TRUE == pSess->audioLanguages[index].bDiscrete) &&
              (TRUE == pSess->audioLanguages[currAudioLangArrIdx].bDiscrete))
      {
         /* Discrete <-> Discrete */
         if(NULL != pSess->hlsDemux)
         {
            g_object_set( G_OBJECT(pSess->hlsDemux), "audio-language", pSess->audioLanguages[index].isoCode, NULL);
            g_strlcpy(pSess->currAudioLanguage, pSess->audioLanguages[index].isoCode, sizeof(pSess->currAudioLanguage));
            pSess->audioLanguageIndex = pSess->audioLanguages[index].index;
            stat = CGMI_ERROR_SUCCESS;
         }
      }
      else if(FALSE == pSess->audioLanguages[index].bDiscrete)
      {
         if(INVALID_INDEX != pSess->audioLanguages[index].index)
         {
            GST_WARNING("Setting audio stream index to %d for language %s\n",
                  pSess->audioLanguages[index].index, pSess->audioLanguages[index].isoCode);

            pSess->audioLanguageIndex = pSess->audioLanguages[index].index;

            if ( NULL == pSess->demux )
            {
               GST_ERROR("Demux is not ready yet, cannot set audio stream!\n");
               stat = CGMI_ERROR_NOT_READY;
               break;
            }

            g_object_set( G_OBJECT(pSess->demux), "audio-stream", pSess->audioLanguageIndex, NULL );
            g_strlcpy(pSess->currAudioLanguage, pSess->audioLanguages[index].isoCode, sizeof(pSess->currAudioLanguage));
            stat = CGMI_ERROR_SUCCESS;
         }
         else
         {
            GST_ERROR("Switching to a muxed audio stream without a language descriptor is currently unsupported\n");
            stat = CGMI_ERROR_NOT_SUPPORTED;
            break;
         }
      }

   }while(0);

   g_rec_mutex_unlock(&pSess->psiMutex);

   if(NULL != uri)
   {
      g_free(uri);
      uri = NULL;
   }

   if(NULL != pAudioLanguage)
   {
      g_free(pAudioLanguage);
      pAudioLanguage = NULL;
   }

   if(NULL != pSess)
   {
      pSess->newAudioLanguage[0] = '\0';
   }

   return stat;
}

cgmi_Status cgmi_SetDefaultAudioLang ( void *pSession, const char *language )
{
   char *ptr;
   tSession *pSess = (tSession*)pSession;

   if ( NULL == language )
   {
      g_print("Bad audio language pointer passed!\n");
      return CGMI_ERROR_BAD_PARAM;
   }

   if ( NULL == pSess )
   {
      strncpy( gDefaultAudioLanguage, language, sizeof(gDefaultAudioLanguage) );
      gDefaultAudioLanguage[sizeof(gDefaultAudioLanguage) - 1] = 0;
   }
   else
   {
      strncpy( pSess->defaultAudioLanguage, language, sizeof(pSess->defaultAudioLanguage) );
      pSess->defaultAudioLanguage[sizeof(pSess->defaultAudioLanguage) - 1] = 0;
   }

   return CGMI_ERROR_SUCCESS;
}

cgmi_Status cgmi_GetNumClosedCaptionServices (void *pSession,  int *count)
{
   tSession *pSess = (tSession*)pSession;

   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      g_print("%s:Invalid session handle\n", __FUNCTION__);
      return CGMI_ERROR_INVALID_HANDLE;
   }

   if ( NULL == count )
   {
      g_print("Null count pointer passed for closed caption language!\n");
      return CGMI_ERROR_BAD_PARAM;
   }

   *count = pSess->numClosedCaptionServices;

   return CGMI_ERROR_SUCCESS;
}

cgmi_Status cgmi_GetClosedCaptionServiceInfo (void *pSession, int index, char* isoCode, int isoCodeSize, int *serviceNum, char *isDigital)
{
   tSession *pSess = (tSession*)pSession;

   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      g_print("%s:Invalid session handle\n", __FUNCTION__);
      return CGMI_ERROR_INVALID_HANDLE;
   }

   if ( NULL == isoCode )
   {
      g_print("Null buffer pointer passed for closed caption language!\n");
      return CGMI_ERROR_BAD_PARAM;
   }

   if ( index > pSess->numClosedCaptionServices - 1 || index < 0 )
   {
      g_print("Bad index value passed for closed caption language!\n");
      return CGMI_ERROR_BAD_PARAM;
   }

   strncpy( isoCode, pSess->closedCaptionServices[index].isoCode, isoCodeSize );
   isoCode[isoCodeSize - 1] = 0;

   *serviceNum = pSess->closedCaptionServices[index].serviceNum;

   *isDigital = pSess->closedCaptionServices[index].isDigital;

   return CGMI_ERROR_SUCCESS;
}

cgmi_Status cgmi_startUserDataFilter( void *pSession, userDataBufferCB bufferCB, void *pUserData )
{
   GstCaps *caps = NULL;
   GstState pipelineState;
   GstStateChangeReturn stateChangeRet;

   tSession *pSess = (tSession*)pSession;
   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      g_print("%s:Invalid session handle\n", __FUNCTION__);
      return CGMI_ERROR_INVALID_HANDLE;
   }

   if ( NULL == pSess->videoDecoder )
   {
      g_print("Pipeline is not ready for user data filtering yet!\n");
      return CGMI_ERROR_NOT_READY;
   }

   if ( NULL != pSess->userDataAppsink )
   {
      g_print("Please stop the existing user data filter before starting a new one");
      return CGMI_ERROR_WRONG_STATE;
   }

   g_print("Adding an appsink and linking it to the decoder for retrieving MPEG user data...\n");
   GstAppSinkCallbacks appsink_cbs = { NULL, NULL, cgmi_gst_new_user_data_buffer_available, NULL };
   pSess->userDataAppsink = gst_element_factory_make( "appsink", NULL );
   if ( NULL == pSess->userDataAppsink )
   {
      g_print("Failed to obtain an appsink for user data!\n");
      return CGMI_ERROR_FAILED;
   }

   caps = gst_caps_new_simple( "application/x-video-user-data", NULL );
   g_object_set( pSess->userDataAppsink, "emit-signals", TRUE, "caps", caps, NULL );
   gst_caps_unref( caps );

   gst_app_sink_set_callbacks( GST_APP_SINK(pSess->userDataAppsink), &appsink_cbs, pSess, NULL);
   gst_bin_add_many( GST_BIN(GST_ELEMENT_PARENT(pSess->videoDecoder)), pSess->userDataAppsink, NULL );

   pSess->userDataAppsinkPad = gst_element_get_static_pad((GstElement *)pSess->userDataAppsink, "sink");
   if ( NULL == pSess->userDataAppsinkPad )
   {
      g_print("Failed to obtain a user data pad from the appsink!\n");
      return CGMI_ERROR_FAILED;
   }

   pSess->userDataPad = gst_element_get_request_pad((GstElement *)(pSess->videoDecoder), "user-data-pad");
   if ( NULL == pSess->userDataPad )
   {
      g_print("Failed to obtain a user data pad from the video decoder!\n");
      return CGMI_ERROR_FAILED;
   }

   if ( gst_pad_link(pSess->userDataPad, pSess->userDataAppsinkPad) != GST_PAD_LINK_OK )
   {
      g_print("Could not link video decoder to appsink!\n");
      return CGMI_ERROR_FAILED;
   }

   /*
   if ( TRUE != gst_element_link( pSess->videoDecoder, pSess->userDataAppsink) )
   {
      g_print("Could not link video decoder to appsink!\n");
      return CGMI_ERROR_FAILED;
   }
   */

   // Sync appsink state with pipeline
   // NOTE:  The convenient sync_state_with_parent API was would not consistently work here
   stateChangeRet = gst_element_set_state( pSess->userDataAppsink, GST_STATE(pSess->pipeline) );
   if( stateChangeRet == GST_STATE_CHANGE_FAILURE )
   {
      g_print("Failed to sync appsink state to pipeline!\n");
      return CGMI_ERROR_FAILED;
   }

   g_print("userDataAppsink in state (%s) ret (%d).\n",
      gst_element_state_get_name(GST_STATE(pSess->userDataAppsink)), stateChangeRet );


   pSess->userDataBufferCB = bufferCB;
   pSess->userDataBufferParam = pUserData;

   g_print("Successfully started user data filter\n");

   return CGMI_ERROR_SUCCESS;
}

cgmi_Status cgmi_stopUserDataFilter( void *pSession, userDataBufferCB bufferCB )
{
   tSession *pSess = (tSession*)pSession;
   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      g_print("%s:Invalid session handle\n", __FUNCTION__);
      return CGMI_ERROR_INVALID_HANDLE;
   }

   if ( NULL != pSess->userDataAppsinkPad && NULL != pSess->userDataAppsink )
   {
      gst_pad_unlink( pSess->userDataPad, pSess->userDataAppsinkPad );
   }

   if ( NULL != pSess->userDataAppsink )
   {
      gst_element_set_state( pSess->userDataAppsink, GST_STATE_NULL );
      gst_bin_remove( GST_BIN(GST_ELEMENT_PARENT(pSess->videoDecoder)), (GstElement *)pSess->userDataAppsink );
      pSess->userDataAppsink = NULL;
   }

   if ( NULL != pSess->userDataPad )
   {
      gst_element_release_request_pad((GstElement *)(pSess->videoDecoder), pSess->userDataPad );
      pSess->userDataPad = NULL;
   }

   if ( NULL != pSess->userDataAppsinkPad )
   {
      gst_object_unref( pSess->userDataAppsinkPad );
   }

   pSess->userDataBufferCB = NULL;
   pSess->userDataBufferParam = NULL;

   g_print("Successfully stopped user data filter\n");

   return CGMI_ERROR_SUCCESS;
}

cgmi_Status cgmi_GetNumPids( void *pSession, int *pCount )
{
   tSession *pSess = (tSession*)pSession;
   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      g_print("%s:Invalid session handle\n", __FUNCTION__);
      return CGMI_ERROR_INVALID_HANDLE;
   }

   if ( NULL == pCount )
   {
      g_print("Bad pCount pointer passed!\n");
      return CGMI_ERROR_BAD_PARAM;
   }

   *pCount = pSess->numStreams;

   return CGMI_ERROR_SUCCESS;
}

cgmi_Status cgmi_GetPidInfo( void *pSession, int index, tcgmi_PidData *pPidData )
{
   tSession *pSess = (tSession*)pSession;
   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      g_print("%s:Invalid session handle\n", __FUNCTION__);
      return CGMI_ERROR_INVALID_HANDLE;
   }

   if ( NULL == pPidData )
   {
      g_print("Bad pPidData pointer passed!\n");
      return CGMI_ERROR_BAD_PARAM;
   }

   if ( index < 0 || index > pSess->numStreams - 1 )
   {
      g_print("Index out of range [0, %d]!\n", pSess->numStreams - 1);
      return CGMI_ERROR_BAD_PARAM;
   }

   pPidData->pid = pSess->streams[index].pid;
   pPidData->streamType = pSess->streams[index].streamType;

   return CGMI_ERROR_SUCCESS;
}

cgmi_Status cgmi_SetPidInfo( void *pSession, int index, tcgmi_StreamType type, int enable )
{
   tSession *pSess = (tSession*)pSession;
   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      g_print("%s:Invalid session handle\n", __FUNCTION__);
      return CGMI_ERROR_INVALID_HANDLE;
   }

   if ( NULL == pSess->demux )
   {
      g_print("Demux is not ready yet, cannot set pid!\n");
      return CGMI_ERROR_NOT_READY;
   }

   if ( STREAM_TYPE_VIDEO == type )
   {
      if ( AUTO_SELECT_STREAM != index && pSess->videoStreamIndex != index )
      {
         g_object_set( G_OBJECT(pSess->demux), "video-stream", index, NULL );
         g_mutex_lock (pSess->autoPlayMutex);
         pSess->videoStreamIndex = index;
         g_print("Waiting on pids: %s\n", pSess->waitingOnPids?"TRUE":"FALSE");
         if ( TRUE == pSess->waitingOnPids )
         {
            g_print("Signalling playback start...\n");
            g_cond_signal( pSess->autoPlayCond );
         }
         g_mutex_unlock (pSess->autoPlayMutex);
      }
   }
   else if ( STREAM_TYPE_AUDIO == type )
   {
      if ( AUTO_SELECT_STREAM != index && pSess->audioStreamIndex != index )
      {
         g_object_set( G_OBJECT(pSess->demux), "audio-stream", index, NULL );
          g_mutex_lock (pSess->autoPlayMutex);
         pSess->audioStreamIndex = index;
          g_mutex_unlock (pSess->autoPlayMutex);
      }

      if ( pSess->isAudioMuted == enable )
      {
         if ( NULL != pSess->audioDecoder )
         {
            g_print("%s audio decoder...\n", enable?"Unmuting":"Muting");
            g_object_set( G_OBJECT(pSess->audioDecoder), "decoder-mute", !enable, NULL );
         }
         else
         {
            g_print("Audio decoder is not opened, cannot change audio mute state!\n");
         }
         pSess->isAudioMuted = !enable;
      }
   }
   else
   {
      g_print("Invalid stream type!\n");
      return CGMI_ERROR_BAD_PARAM;
   }

   return CGMI_ERROR_SUCCESS;
}


static gboolean
parse_debug_category (gchar * str, const gchar ** category)
{
  if (!str)
    return FALSE;

  /* works in place */
  g_strstrip (str);

  if (str[0] != '\0')
  {
    *category = str;
    return TRUE;
  }

  return FALSE;
}

static gboolean
parse_debug_level (gchar * str, GstDebugLevel * level)
{
  unsigned long l;
  char *endptr;

  if (!str)
    return FALSE;

  /* works in place */
  g_strstrip (str);

  if (g_ascii_isdigit (str[0]))
  {
    l = strtoul (str, &endptr, 10);
    if (endptr > str && endptr[0] == 0)
      *level = (GstDebugLevel) l;
    else
      return FALSE;
  }
  else
  {
    return FALSE;
  }
  return TRUE;
}

cgmi_Status cgmi_SetLogging(const char *gstDebugStr)
{
   cgmi_Status   stat = CGMI_ERROR_SUCCESS;
   gchar **split;
   gchar **walk;
   GstDebugLevel level;
   gchar **values;
   const gchar *category;

#if GST_CHECK_VERSION(1,0,0)
   gst_debug_set_colored(GST_DEBUG_COLOR_MODE_OFF);
#endif

   if (gstDebugStr == NULL  ||  strnlen(gstDebugStr, GST_DEBUG_STR_MAX_SIZE) >= GST_DEBUG_STR_MAX_SIZE)
   {
      g_print("Invalid GST debug string!\n");
      return CGMI_ERROR_BAD_PARAM;
   }   

   split = g_strsplit (gstDebugStr, ",", 0);

   for (walk = split; *walk; walk++)
   {
      if (strchr (*walk, ':'))
      {
         values = g_strsplit (*walk, ":", 2);
         if (values[0] && values[1])
         {
            if (parse_debug_category (values[0], &category)
             && parse_debug_level (values[1], &level))
            gst_debug_set_threshold_for_name (category, level);
         }

         g_strfreev (values);
      }
      else
      {
         if (parse_debug_level (*walk, &level))
         {
           gst_debug_set_default_threshold (level);
         }
      }
   }

   g_strfreev (split);
   return stat;
}

cgmi_Status cgmi_GetTsbSlide(void *pSession, unsigned long *pTsbSlide)
{
   cgmi_Status   stat = CGMI_ERROR_FAILED;
   gboolean is_live = FALSE;
   tSession *pSess = (tSession*)pSession;

   do 
   {
      if ( cgmi_CheckSessionHandle(pSess) == FALSE )
      {
         g_print("%s:Invalid session handle\n", __FUNCTION__);
         stat = CGMI_ERROR_INVALID_HANDLE;
         break;
      }

      if(NULL == pTsbSlide)
      {
         g_print("%s: pTsbSlide param is NULL\n", __FUNCTION__);
         stat = CGMI_ERROR_BAD_PARAM;
         break;
      }   

      if(TRUE == pSess->bisDLNAContent)
      {
         g_object_get(pSess->source, "is-live", &is_live, NULL);
         if(FALSE == is_live)
         {
            stat = CGMI_ERROR_NOT_SUPPORTED;
            break;
         }
         g_object_get(pSess->source, "tsb-slide", pTsbSlide, NULL);
      
         stat = CGMI_ERROR_SUCCESS;
      }
      else
      {
         stat = CGMI_ERROR_NOT_SUPPORTED;
         break;
      }
   }while(0);

   return stat;
}

cgmi_Status cgmi_GetNumSubtitleLanguages( void *pSession, int *count )
{
   tSession *pSess = (tSession *)pSession;

   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      g_print("%s:Invalid session handle\n", __FUNCTION__);
      return CGMI_ERROR_INVALID_HANDLE;
   }

   if ( NULL == count )
   {
      g_print("Null count pointer passed for subtitle language!\n");
      return CGMI_ERROR_BAD_PARAM;
   }

   *count = pSess->numSubtitleLanguages;

   return CGMI_ERROR_SUCCESS;
}

cgmi_Status cgmi_GetSubtitleInfo( void *pSession, int index, char *buf, int bufSize, unsigned short *pid,
                                  unsigned char *type, unsigned short *compPageId, unsigned short *ancPageId )
{
   tSession *pSess = (tSession *)pSession;

   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      g_print("%s:Invalid session handle\n", __FUNCTION__);
      return CGMI_ERROR_INVALID_HANDLE;
   }

   if ( NULL == buf )
   {
      g_print("Null buffer pointer passed for subtitle language!\n");
      return CGMI_ERROR_BAD_PARAM;
   }

   if ( index > pSess->numSubtitleLanguages - 1 || index < 0 )
   {
      g_print("Bad index value passed for subtitle language!\n");
      return CGMI_ERROR_BAD_PARAM;
   }

   strncpy(buf, pSess->subtitleInfo[index].isoCode, bufSize);
   buf[bufSize - 1] = 0;

   if ( pid != NULL )
   {
      *pid = pSess->subtitleInfo[index].pid;
   }

   if ( type != NULL )
   {
      *type = pSess->subtitleInfo[index].type;
   }

   if (  compPageId != NULL )
   {
      *compPageId = pSess->subtitleInfo[index].compPageId;
   }

   if ( ancPageId != NULL )
   {
      *ancPageId = pSess->subtitleInfo[index].ancPageId;
   }

   return CGMI_ERROR_SUCCESS;
}

cgmi_Status cgmi_SetDefaultSubtitleLang( void *pSession, const char *language )
{
   char *ptr;
   tSession *pSess = (tSession *)pSession;

   if ( NULL == language )
   {
      g_print("Bad subtitle language pointer passed!\n");
      return CGMI_ERROR_BAD_PARAM;
   }

   strncpy(gDefaultSubtitleLanguage, language, sizeof(gDefaultSubtitleLanguage));
   gDefaultSubtitleLanguage[sizeof(gDefaultSubtitleLanguage) - 1] = 0;

   if ( NULL != pSess )
   {
      strncpy(pSess->defaultSubtitleLanguage, language, sizeof(pSess->defaultSubtitleLanguage));
      pSess->defaultSubtitleLanguage[sizeof(pSess->defaultSubtitleLanguage) - 1] = 0;
   }

   return CGMI_ERROR_SUCCESS;
}

cgmi_Status cgmi_GetStc(void *pSession, uint64_t *pStc)
{
   cgmi_Status stat = CGMI_ERROR_FAILED;
   tSession *pSess = (tSession*)pSession;

   do
   {
      if(NULL == pStc)
      {
         g_print("%s: pTsbSlide param is NULL\n", __FUNCTION__);
         stat = CGMI_ERROR_BAD_PARAM;
         break;
      }

      *pStc = 0;

      if ( cgmi_CheckSessionHandle(pSess) == FALSE )
      {
         g_print("%s:Invalid session handle\n", __FUNCTION__);
         stat = CGMI_ERROR_INVALID_HANDLE;
         break;
      }

      if (NULL == pSess->demux)
      {
         stat = CGMI_ERROR_NOT_INITIALIZED;
         break;
      }

      g_object_get(pSess->demux, "stc", pStc, NULL);
      stat = CGMI_ERROR_SUCCESS;

   }while(0);

   return stat;
}

cgmi_Status cgmi_SetPictureSetting( void *pSession, tcgmi_PictureCtrl pctl, int value )
{
   tSession *pSess = (tSession*)pSession;

   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      g_print("%s:Invalid session handle\n", __FUNCTION__);
      return CGMI_ERROR_INVALID_HANDLE;
   }

   if ( (value < -32768) || (value > 32767) )
   {
      g_print("%s:Invalid value for picture setting. -32768 < %d < 32767!\n", __FUNCTION__, value);
      return CGMI_ERROR_BAD_PARAM;
   }

   switch (pctl)
   {
      case PICTURE_CTRL_SATURATION:
         if (NULL != pSess->videoDecoder)
             g_object_set(G_OBJECT(pSess->videoDecoder), "video-saturation", value, NULL );
         break;
      case PICTURE_CTRL_CONTRAST:
         if (NULL != pSess->videoDecoder)
             g_object_set(G_OBJECT(pSess->videoDecoder), "video-contrast", value, NULL );
         break;
      case PICTURE_CTRL_HUE:
         if (NULL != pSess->videoDecoder)
             g_object_set(G_OBJECT(pSess->videoDecoder), "video-hue", value, NULL );
         break;
      case PICTURE_CTRL_BRIGHTNESS:
         if (NULL != pSess->videoDecoder)
             g_object_set(G_OBJECT(pSess->videoDecoder), "video-brightness", value, NULL );
         break;
      case PICTURE_CTRL_COLORTEMP:
         if (NULL != pSess->videoDecoder)
             g_object_set(G_OBJECT(pSess->videoDecoder), "video-colortemp", value, NULL );
         break;
      case PICTURE_CTRL_SHARPNESS:
         if (NULL != pSess->videoDecoder)
             g_object_set(G_OBJECT(pSess->videoDecoder), "video-sharpness", value, NULL );
         break;
      default:
         return CGMI_ERROR_BAD_PARAM;
   }

   return CGMI_ERROR_SUCCESS;
}

cgmi_Status cgmi_GetPictureSetting( void *pSession, tcgmi_PictureCtrl pctl, int *pvalue )
{
   tSession *pSess = (tSession*)pSession;

   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      g_print("%s:Invalid session handle\n", __FUNCTION__);
      return CGMI_ERROR_INVALID_HANDLE;
   }

   if ( pvalue == NULL )
   {
       return CGMI_ERROR_BAD_PARAM;
   }

   switch (pctl)
   {
       case PICTURE_CTRL_SATURATION:
           if (NULL != pSess->videoDecoder)
               g_object_get(G_OBJECT(pSess->videoDecoder), "video-saturation", pvalue, NULL );
           break;
       case PICTURE_CTRL_CONTRAST:
           if (NULL != pSess->videoDecoder)
               g_object_get(G_OBJECT(pSess->videoDecoder), "video-contrast", pvalue, NULL );
           break;
       case PICTURE_CTRL_HUE:
           if (NULL != pSess->videoDecoder)
               g_object_get(G_OBJECT(pSess->videoDecoder), "video-hue", pvalue, NULL );
           break;
       case PICTURE_CTRL_BRIGHTNESS:
           if (NULL != pSess->videoDecoder)
               g_object_get(G_OBJECT(pSess->videoDecoder), "video-brightness", pvalue, NULL );
           break;
       case PICTURE_CTRL_COLORTEMP:
           if (NULL != pSess->videoDecoder)
               g_object_get(G_OBJECT(pSess->videoDecoder), "video-colortemp", pvalue, NULL );
           break;
       case PICTURE_CTRL_SHARPNESS:
           if (NULL != pSess->videoDecoder)
               g_object_get(G_OBJECT(pSess->videoDecoder), "video-sharpness", pvalue, NULL );
           break;
       default:
           return CGMI_ERROR_BAD_PARAM;
   }

   return CGMI_ERROR_SUCCESS;
}
