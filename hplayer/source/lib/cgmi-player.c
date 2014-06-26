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
#endif
#include "cgmiPlayerApi.h"
#include "cgmi-priv-player.h"
#include "cgmi-section-filter-priv.h"

#define MAGIC_COOKIE 0xDEADBEEF
GST_DEBUG_CATEGORY_STATIC (cgmi);
#define GST_CAT_DEFAULT cgmi

#define INVALID_INDEX      -2

#define PTS_FLUSH_THRESHOLD     (10 * 45000) //10 secs
#define DEFAULT_BLOCKSIZE       65536

#define GST_DEBUG_STR_MAX_SIZE  256

static gboolean cisco_gst_handle_msg( GstBus *bus, GstMessage *msg, gpointer data );
static GstElement *cgmi_gst_find_element( GstBin *bin, gchar *ename );

static gchar gDefaultAudioLanguage[4];

static int  cgmi_CheckSessionHandle(tSession *pSess)
{
   if (NULL == pSess || (int)pSess->cookie != MAGIC_COOKIE)
   {
      GST_ERROR("The pointer to the session handle is invalid!!!!\n");
      return FALSE;
   }
   return TRUE;

}

static void cisco_gst_setState( tSession *pSess, GstState state )
{
   GstStateChangeReturn sret;

   sret = gst_element_set_state( pSess->pipeline, state );
   switch ( sret )
   {
      case GST_STATE_CHANGE_FAILURE:
         GST_WARNING("Set NULL State Failure\n");
         break;
      case GST_STATE_CHANGE_NO_PREROLL:
         GST_WARNING("Set NULL State No Preroll\n");
         break;
      case GST_STATE_CHANGE_ASYNC:
         GST_WARNING("Set NULL State Async\n");
         break;
      case GST_STATE_CHANGE_SUCCESS:
         GST_WARNING("Set NULL State Succeeded\n");
         break;
      default:
         GST_WARNING("Set NULL State Unknown\n");
         break;
   }

   return;
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

      if(NULL != pSess->demux)
      {
         flushStart = gst_event_new_flush_start();
#if GST_CHECK_VERSION(1,0,0)
         flushStop = gst_event_new_flush_stop(FALSE);
#else
         flushStop = gst_event_new_flush_stop();
#endif

         ret = gst_element_send_event(GST_ELEMENT(pSess->demux), flushStart);
         if(FALSE == ret)
         {
            GST_ERROR("Failed to send flush start event!\n");
         }

         ret = gst_element_send_event(GST_ELEMENT(pSess->demux), flushStop);
         if(FALSE == ret)
         {
            GST_ERROR("Failed to send flush stop event\n"); 
         }
      }
   }while(0);
   
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
   gint64 videoPts, audioPts;
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
            g_object_get( pSess->videoDecoder, "video_pts", &videoPts, NULL );
         }
         if ( NULL != pSess->audioDecoder )
         {
            g_object_get( pSess->audioDecoder, "audio_pts", &audioPts, NULL );
         }

         if ( videoPts > 0 && audioPts > 0 )
         {
            if ( videoPts - audioPts > PTS_FLUSH_THRESHOLD || videoPts - audioPts < -PTS_FLUSH_THRESHOLD )
            {
               gboolean ret = FALSE; 
               GstEvent *flushStart, *flushStop; 

               g_print("Flushing buffers due to large audio-video PTS difference...\n");
               g_print("videoPts = %lld, audioPts = %lld, diff = %lld\n", videoPts, audioPts, videoPts - audioPts);

               if ( NULL != pSess->demux )
               {
                  flushStart = gst_event_new_flush_start(); 
#if GST_CHECK_VERSION(1,0,0)
                  flushStop = gst_event_new_flush_stop(FALSE); 
#else
                  flushStop = gst_event_new_flush_stop(); 
#endif

                  ret = gst_element_send_event(GST_ELEMENT(pSess->demux), flushStart); 
                  if ( FALSE == ret) 
                     GST_WARNING("Failed to send flush start event!\n"); 
                  ret = gst_element_send_event(GST_ELEMENT(pSess->demux), flushStop); 
                  if ( FALSE == ret) 
                     GST_WARNING("Failed to send flush stop event\n"); 
               }
            }
         }
      }
      g_usleep(1000000);
   }
}

static gboolean cisco_gst_handle_msg( GstBus *bus, GstMessage *msg, gpointer data )
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
      {
         pSess->eventCB(pSess->usrParam, (void*)pSess, NOTIFY_SEEK_DONE, 0);
      }
      break;
      case GST_MESSAGE_EOS:
      {
         GST_INFO("End of Stream\n");
         pSess->eventCB(pSess->usrParam, (void*)pSess, NOTIFY_END_OF_STREAM,0);
      }
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

         if (0 == strcmp(ntype, "first_pts_decoded"))
         {    
            pSess->eventCB(pSess->usrParam, (void*)pSess,NOTIFY_FIRST_PTS_DECODED, 0 );
            gst_message_unref (msg);
         }
         else if (0 == strcmp(ntype, "stream_attrib_changed"))
         {    
            pSess->eventCB(pSess->usrParam, (void*)pSess,NOTIFY_VIDEO_RESOLUTION_CHANGED, 0);
            gst_message_unref (msg);
         }
         else if (0 == strcmp(ntype, "BOF"))
         {    
            pSess->eventCB(pSess->usrParam, (void*)pSess,NOTIFY_START_OF_STREAM, 0);
            gst_message_unref (msg);
            cgmi_SetRate(pSess, 0.0); 
         }
         else
         {
            GST_ERROR("Do not know how to handle %s notification\n",ntype);
            gst_message_unref (msg);
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
                        innerSink = cgmi_gst_find_element( videoSink, "videosink" );
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
   unsigned int videoStream, audioStream, programNumber;

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

   g_print("Enabling server side trick mode...\n");
   g_object_set( obj, "server-side-trick-mode", TRUE, NULL );

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
               switch ( string->str[pos] )
               {
                  case 0x0A: /* ISO_639_language_descriptor */
                     g_print("Found audio language descriptor for stream %d\n", j);
                     if ( pSess->numAudioLanguages < MAX_AUDIO_LANGUAGE_DESCRIPTORS && len >= 4 )
                     {
                        pSess->audioLanguages[pSess->numAudioLanguages].pid = esPid;
                        pSess->audioLanguages[pSess->numAudioLanguages].streamType = esType;
                        pSess->audioLanguages[pSess->numAudioLanguages].index = j;
                        strncpy( pSess->audioLanguages[pSess->numAudioLanguages].isoCode, &string->str[pos+2], 3 );
                        pSess->audioLanguages[pSess->numAudioLanguages].isoCode[3] = 0;

                        if ( strlen(pSess->defaultAudioLanguage) > 0 && pSess->audioLanguageIndex == INVALID_INDEX )
                        {
                           if ( strncmp(pSess->audioLanguages[pSess->numAudioLanguages].isoCode, pSess->defaultAudioLanguage, 3) == 0 )
                           {
                              g_print("Stream (%d) audio language matched to default audio lang %s\n", j, pSess->defaultAudioLanguage);
                              pSess->audioLanguageIndex = j;
                           }
                        }
                        pSess->numAudioLanguages++;
                     }
                     else
                     {
                        g_print("Maximum number of audio language descriptors %d has been reached!!!\n",
                                MAX_AUDIO_LANGUAGE_DESCRIPTORS);
                     }
                     break;
               }

               pos += len + 2;
               totalLen = totalLen - len - 2;
            }
         }
      }
      g_print ("------------------------------------------------------------------------- \n");
   }

   if ( pSess->audioLanguageIndex != INVALID_INDEX )
   {
      g_print("Selecting audio language index %d...\n");
      g_object_set( obj, "audio-stream", pSess->audioLanguageIndex, NULL );
   }

   if ( NULL != pSess->eventCB )
      pSess->eventCB(pSess->usrParam, (void*)pSess, NOTIFY_PSI_READY, 0);

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

   g_print("notify-source\n");

   g_object_get( obj, "source", &source, NULL );
   if ( NULL == source )
      return;

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
         g_print("souphttpsrc has been detected, increasing the blocksize to: %u bytes\n",DEFAULT_BLOCKSIZE );
         g_object_set (souphttpsrc, "blocksize",DEFAULT_BLOCKSIZE , NULL);
      }
   }

   gst_object_unref( source );
}

static void cgmi_gst_element_added( GstBin *bin, GstElement *element, gpointer data )
{
   GstElement *demux = NULL;
   GstElement *videoSink = NULL;
   GstElement *videoDecoder = NULL;
   GstElement *audioDecoder = NULL;
   tSession *pSess = (tSession*)data;

   gchar *name = gst_element_get_name( element );
   g_print("Element added: %s\n", name);

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
      }
   }

   if ( NULL == pSess->audioDecoder )
   {
      audioDecoder = cgmi_gst_find_element( bin, "audiodecoder" );

      if( NULL != audioDecoder )
      {
         pSess->audioDecoder = audioDecoder;
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
      pSess->userDataBufferCB( pSess->userDataBufferParam, (void *)buffer );
   }

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

   } while(0);

   return stat;
}

cgmi_Status cgmi_Term (void)
{
   gst_deinit();
   cgmi_utils_finalize();
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

#ifdef USE_DRMPROXY
   tProxyErr proxy_err = {};
   uint64_t privateData;
#endif

   pSess = g_malloc0(sizeof(tSession));
   if (pSess == NULL)
   {
      return CGMI_ERROR_OUT_OF_MEMORY;
   }
   *pSession = pSess;
   pSess->cookie = (void*)MAGIC_COOKIE;
   pSess->usrParam = pUserData;
   pSess->playbackURI = NULL;
   pSess->manualPipeline = NULL;
   pSess->eventCB = eventCB;
   pSess->demux = NULL;
   pSess->udpsrc = NULL;
   pSess->videoSink = NULL;
   pSess->videoDecoder = NULL;
   pSess->audioDecoder = NULL;
   pSess->vidDestRect.x = 0;
   pSess->vidDestRect.y = 0;
   pSess->vidDestRect.w = VIDEO_MAX_WIDTH;
   pSess->vidDestRect.h = VIDEO_MAX_HEIGHT;
   pSess->audioStreamIndex = INVALID_INDEX;
   pSess->videoStreamIndex = INVALID_INDEX;
   pSess->isAudioMuted = FALSE;

   strncpy( pSess->defaultAudioLanguage, gDefaultAudioLanguage, sizeof(pSess->defaultAudioLanguage) );
   pSess->defaultAudioLanguage[sizeof(pSess->defaultAudioLanguage) - 1] = 0;
   pSess->audioLanguageIndex = INVALID_INDEX;

   pSess->thread_ctx = g_main_context_new();

   pSess->autoPlayMutex = g_mutex_new ();
   pSess->autoPlayCond = g_cond_new ();

   pSess->runMonitor = TRUE;
   pSess->monitor = g_thread_new("monitoring_thread", cgmi_monitor, pSess);
   if (!pSess->thread)
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

#ifdef USE_DRMPROXY
   DRMPROXY_CreateSession(&pSess->drmProxyHandle, &proxy_err, asyncCB, privateData);
   if (proxy_err.errCode != 0)
   {
      GST_ERROR ("DRMPROXY_CreateSession error %lu \n", proxy_err.errCode);
      GST_ERROR ("%s \n", proxy_err.errString);
      eventCB(pUserData, (void *)pSess, 0, proxy_err.errCode);
   }
#endif

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
      GST_INFO("loop is running, setting it to quit\n");
      g_source_remove(pSess->tag);
      GST_INFO("about to quit the main loop\n");
      g_main_loop_quit (pSess->loop);
   }


   //TODO fix
   g_print("There is race condtion in the glib library, having to sleep to let glib clean up\n");
   sleep(3);

   pSess->runMonitor = FALSE;
   if (pSess->monitor)
   {
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

   if (pSess->playbackURI) {g_free(pSess->playbackURI);}
   if (pSess->manualPipeline) {g_free(pSess->manualPipeline);}
   if (pSess->pipeline) {gst_object_unref (GST_OBJECT (pSess->pipeline));}
   if (pSess->autoPlayCond) {g_cond_free(pSess->autoPlayCond);}
   if (pSess->autoPlayMutex) {g_mutex_free(pSess->autoPlayMutex);}
   if (pSess->loop) {g_main_loop_unref(pSess->loop);}
   g_free(pSess);

#ifdef USE_DRMPROXY
   DRMPROXY_DestroySession(pSess->drmProxyHandle);
   if (proxy_err.errCode != 0)
   {
     GST_ERROR ("DRMPROXY_DestroySession error %lu \n", proxy_err.errCode);
     GST_ERROR ("%s \n", proxy_err.errString);
     if (pSess != NULL)
        pSess->eventCB(pSess->usrParam, (void *)pSess, 0, proxy_err.errCode);
   }
#endif

   return stat;
}


cgmi_Status cgmi_Load    (void *pSession, const char *uri )
{
   GError *g_error_str =NULL;
   GstParseContext *ctx;
   gchar **arr;
   gchar *pPipeline;
   cgmi_Status stat = CGMI_ERROR_SUCCESS;
   GSource *source;
   uint32_t  bisDLNAContent = FALSE;
   gchar manualPipeline[1024];
#ifdef USE_DRMPROXY
   gchar **array = NULL;
   tProxyErr proxy_err = {};
   uint64_t licenseId=0;
   char buffer[50];
   tDRM_TYPE drmType=UNKNOWN;
#endif

   tSession *pSess = (tSession*)pSession;
   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      g_print("%s:Invalid session handle\n", __FUNCTION__);
      return CGMI_ERROR_INVALID_HANDLE;
   }

   pPipeline = g_strnfill(1024, '\0');
   if (pPipeline == NULL)
   {
      GST_WARNING("Error allocating memory\n");
      return CGMI_ERROR_OUT_OF_MEMORY;
   }

   pSess->numAudioLanguages = 0;
   pSess->numStreams = 0;
   pSess->videoStreamIndex = INVALID_INDEX;
   pSess->audioStreamIndex = INVALID_INDEX;
   pSess->audioLanguageIndex = INVALID_INDEX;
   pSess->isAudioMuted = FALSE;

   // 
   // check to see if this is a DLNA url.
   //
   if (0 ==strncmp(uri,"http",4))
   {
      stat = cgmi_utils_is_content_dlna(uri, &bisDLNAContent);
      if(CGMI_ERROR_SUCCESS != stat)
      {
         g_free(pPipeline);
         printf("Not able to determine whether the content is DLNA\n");
         return stat;
      }
   }
   
   if (bisDLNAContent == TRUE)
   {
      //for the gstreamer pipeline to autoplug we have to add
      //dlna+ to the protocol.
      pSess->playbackURI = g_strdup_printf("%s%s","dlna+", uri);
   }
   else 
   {
      pSess->playbackURI = g_strdup_printf("%s", uri);
   }

   if(NULL == pSess->playbackURI)
   {
      g_free(pPipeline);
      printf("Not able to allocate memory\n");
      return CGMI_ERROR_OUT_OF_MEMORY;
   }
   
   g_print("URI: %s\n", pSess->playbackURI);
   /* Create playback pipeline */

#if GST_CHECK_VERSION(1,0,0)
   g_strlcpy(pPipeline, "playbin uri=", 1024);
#else
   g_strlcpy(pPipeline, "playbin2 uri=", 1024);
#endif

#ifdef USE_DRMPROXY
   DRMPROXY_ParseURL(pSess->drmProxyHandle, pSess->playbackURI, &drmType, &proxy_err);
   if (proxy_err.errCode != 0)
   {
     GST_ERROR ("DRMPROXY_ParseURL error %lu \n", proxy_err.errCode);
     GST_ERROR ("%s \n", proxy_err.errString);
     pSess->eventCB(pSess->usrParam, (void *)pSess, 0, proxy_err.errCode);
   }

   if (drmType == CLEAR || drmType == UNKNOWN)
   {
   	g_strlcat(pPipeline,pSess->playbackURI,1024);
   }
   else
   {
		array = g_strsplit(pSess->playbackURI, "?",  1024);
		g_strlcat(pPipeline, array[0], 1024 );

		DRMPROXY_Activate(pSess->drmProxyHandle, pSess->playbackURI, sizeof(pSess->playbackURI), &licenseId, &proxy_err );
		if (proxy_err.errCode != 0)
		{
			GST_ERROR ("DRMPROXY_Activate error %lu \n", proxy_err.errCode);
			GST_ERROR ("%s \n", proxy_err.errString);
			pSess->eventCB(pSess->usrParam, (void *)pSess, 0, proxy_err.errCode);
		}
		if ( drmType == VERIMATRIX)
		{
			g_strlcat(pPipeline,"?drmType=verimatrix", 1024);
			g_strlcat(pPipeline, "&LicenseID=",1024);
			licenseId = 0;
		 // LicenseID going to verimatrix plugin is 0 so don't bother with what comes back
			sprintf(buffer, "%" PRIu64, licenseId);
			g_strlcat(pPipeline, buffer, 1024);
		}
		else if (drmType == VGDRM)
		{

	//  FIXME: missing VGDRM specifics, may need to tweak the uri before sending it downstream
			g_strlcat(pPipeline,"?drmType=vgdrm", 1024);
			g_strlcat(pPipeline, "&LicenseID=",1024);
			sprintf(buffer,"%08llX" , licenseId);
			g_strlcat(pPipeline, buffer, 1024);
		}
   }
#else
   g_strlcat(pPipeline,pSess->playbackURI,1024);
#endif

	// let's see if we are running on broadcom hardware if we are let's see if we can find there
	// video sink.  If it's there we need to set the flags variable so the pipeline knows to do
	// color transformation and scaling in hardware
#if GST_CHECK_VERSION(1,0,0)
	if (gst_registry_find_plugin( gst_registry_get() , "brcmvideosink"))
#else
	if (gst_registry_find_plugin( gst_registry_get_default() , "brcmvideosink"))
#endif
	{
		g_print("Autoplugging on real broadcom hardware\n");
		g_strlcat(pPipeline," flags= 0x63", 1024);
	}

   do
   {
      GST_INFO("Launching the pipeline with :\n%s\n",pPipeline);

      ctx = gst_parse_context_new ();
      pSess->pipeline = gst_parse_launch_full(pPipeline,
                                              ctx,
                                              GST_PARSE_FLAG_FATAL_ERRORS,
                                              &g_error_str);

      g_free(pPipeline);

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
      /* Add bus watch for events and messages */
      pSess->bus = gst_element_get_bus( pSess->pipeline );
      if (pSess->bus == NULL)
      {
         GST_ERROR("The bus is null in the pipeline\n");
      }

      // enable the notifications.
      source = gst_bus_create_watch(pSess->bus);
      g_source_set_callback(source, (GSourceFunc)cisco_gst_handle_msg, pSess, NULL);
      pSess->tag = g_source_attach(source, pSess->thread_ctx);
      g_source_unref(source);

      // Track when elements are added to the pipeline.
      // Primarily acquires a reference to demux for section filter support.
      if ( NULL != pSess->manualPipeline )
      {
         GstBin *decodebin = (GstBin *)cgmi_gst_find_element( (GstBin *)pSess->pipeline, "dec" );

         if( NULL != decodebin )
         {
            g_signal_connect( decodebin, "element-added",
               G_CALLBACK(cgmi_gst_element_added), pSess );
         }
      }
      else
      {
         g_signal_connect( pSess->pipeline, "element-added",
            G_CALLBACK(cgmi_gst_element_added), pSess );

         g_signal_connect( pSess->pipeline, "notify::source",
            G_CALLBACK(cgmi_gst_notify_source), pSess );
      }

   }while(0);

   // free memory
   gst_parse_context_free(ctx);
   if(g_error_str){g_error_free(g_error_str);}
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

   do
   {
      //Signal psi callback on unload in case it is blocked on PID selection
      g_mutex_lock( pSess->autoPlayMutex );
      if ( TRUE == pSess->waitingOnPids )
		   g_cond_signal( pSess->autoPlayCond );
		g_mutex_unlock( pSess->autoPlayMutex );

      g_print ("Changing state of pipeline to NULL \n");
      gst_element_set_state (pSess->pipeline, GST_STATE_NULL);
      // I dont' think that we have to free any memory associated
      // with the source ( for the bus watch as I already freed it when I set
      // it up.  Once the reference count goes to 0 it should be freed

      g_print ("Deleting pipeline\n");
      gst_object_unref (GST_OBJECT (pSess->pipeline));
      pSess->pipeline = NULL;
      pSess->demux = NULL;
      pSess->udpsrc = NULL;
      pSess->videoSink = NULL;
      pSess->videoDecoder = NULL;
      pSess->audioDecoder = NULL;
      pSess->rate = 0.0;

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

   pSess->autoPlay = autoPlay;

   cisco_gst_setState( pSess, GST_STATE_PLAYING);
      
   pSess->rate = 1.0;
   
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
   GstQuery *streamInfo;

   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      GST_ERROR("%s:Invalid session handle\n", __FUNCTION__);
      return CGMI_ERROR_INVALID_HANDLE;
   }

   streamInfo = gst_query_new_segment (GST_FORMAT_TIME);
   gst_element_get_state(pSess->pipeline, &curState, NULL, GST_CLOCK_TIME_NONE);

   if (rate == 0.0)
   {
      cisco_gst_setState( pSess, GST_STATE_PAUSED );
      
      if((pSess->rate > 1.0) || (pSess->rate < -1.0))
      {
         cgmi_flush_pipeline(pSess);

         GST_WARNING("%s - Un Set Low Delay Mode\n",__FUNCTION__);
         g_object_set(G_OBJECT(pSess->videoDecoder),"low_delay",FALSE,NULL);
         g_object_set(G_OBJECT(pSess->videoDecoder),"video_mask","all",NULL);
      }
      
      pSess->rate = rate;
      return stat;
   }
   else if (( rate == 1.0) && (curState == GST_STATE_PAUSED))
   {
      cisco_gst_setState( pSess, GST_STATE_PLAYING );
      pSess->rate = rate;
      return stat;
   }

   /* Obtain the current position, needed for the seek event */
#if GST_CHECK_VERSION(1,0,0)
   if (!gst_element_query_position (pSess->pipeline, format, &position))
#else
   if (!gst_element_query_position (pSess->pipeline, &format, &position))
#endif
   {
      GST_ERROR ("Unable to retrieve current position.\n");
      return CGMI_ERROR_FAILED;
   }

   if (rate >= 1.0)
   {
      seek_event = gst_event_new_seek (rate, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
            GST_SEEK_TYPE_SET, position, GST_SEEK_TYPE_NONE, 0);

   }
   else if (rate <= -1.0)
   {
      seek_event = gst_event_new_seek (rate, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
            GST_SEEK_TYPE_SET,0 , GST_SEEK_TYPE_NONE, position);

   }
   else if (rate == 1.0)
   {
      seek_event = gst_event_new_seek(1.0, GST_FORMAT_TIME, 0, GST_SEEK_TYPE_NONE, -1, GST_SEEK_TYPE_NONE, -1);
   }

   /* Send the event */
   if (seek_event)
   {
      if(((0.0 == pSess->rate) || (1.0 == pSess->rate)) && ((rate > 1.0) || (rate < -1.0)) ||
         ((0.0 == rate) || (1.0 == rate)) && ((pSess->rate > 1.0) || (pSess->rate < -1.0)))
      {
         if ( NULL == pSess->videoDecoder )
         {
            GST_ERROR("videoDecoder handle is NULL\n");
            return CGMI_ERROR_FAILED;
         }

         if (rate > 1.0 || rate < -1.0)
         {
            cgmi_flush_pipeline(pSess);

            GST_WARNING("%s - Set Low Delay Mode\n", __FUNCTION__);
            g_object_set(G_OBJECT(pSess->videoDecoder),"low_delay",TRUE,NULL);
            g_object_set(G_OBJECT(pSess->videoDecoder),"video_mask","i_only",NULL);

            if(curState == GST_STATE_PAUSED)
            {
               cisco_gst_setState( pSess, GST_STATE_PLAYING );
            }
         }
         else
         {
            cgmi_flush_pipeline(pSess);

            GST_WARNING("%s - Un Set Low Delay Mode\n", __FUNCTION__);
            g_object_set(G_OBJECT(pSess->videoDecoder),"low_delay",FALSE,NULL);
            g_object_set(G_OBJECT(pSess->videoDecoder),"video_mask","all",NULL);
         }
      }

      gst_element_send_event (pSess->pipeline, seek_event);
   }
   
   pSess->rate = rate;
   
   return stat;
}

cgmi_Status cgmi_SetPosition (void *pSession, float position)
{

   tSession *pSess = (tSession*)pSession;
   cgmi_Status stat = CGMI_ERROR_SUCCESS;

   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      g_print("%s:Invalid session handle\n", __FUNCTION__);
      return CGMI_ERROR_INVALID_HANDLE;
   }

   do
   {
      g_print("Setting position to %f (ns)  \n", (position* GST_SECOND));
      if( !gst_element_seek( pSess->pipeline,
               pSess->rate,
               GST_FORMAT_TIME,
               GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
               GST_SEEK_TYPE_SET,
               (position*GST_SECOND),
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

   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      g_print("%s:Invalid session handle\n", __FUNCTION__);
      return CGMI_ERROR_INVALID_HANDLE;
   }

   *type = FIXED;

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
   tSession *pSess = (tSession*)pSession;

   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      g_print("%s:Invalid session handle\n", __FUNCTION__);
      return CGMI_ERROR_INVALID_HANDLE;
   }

   if ( NULL == count )
   {
      g_print("Null count pointer passed for audio language!\n");
      return CGMI_ERROR_BAD_PARAM;
   }

   *count = pSess->numAudioLanguages;

   return CGMI_ERROR_SUCCESS;
}

cgmi_Status cgmi_GetAudioLangInfo (void *pSession, int index, char* buf, int bufSize)
{
   tSession *pSess = (tSession*)pSession;

   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      g_print("%s:Invalid session handle\n", __FUNCTION__);
      return CGMI_ERROR_INVALID_HANDLE;
   }

   if ( NULL == buf )
   {
      g_print("Null buffer pointer passed for audio language!\n");
      return CGMI_ERROR_BAD_PARAM;
   }

   if ( index > pSess->numAudioLanguages - 1 || index < 0 )
   {
      g_print("Bad index value passed for audio language!\n");
      return CGMI_ERROR_BAD_PARAM;
   }

   strncpy( buf, pSess->audioLanguages[index].isoCode, bufSize );
   buf[bufSize - 1] = 0;

   return CGMI_ERROR_SUCCESS;
}

cgmi_Status cgmi_SetAudioStream (void *pSession, int index )
{
   cgmi_Status stat;
   tSession *pSess = (tSession*)pSession;

   if ( cgmi_CheckSessionHandle(pSess) == FALSE )
   {
      g_print("%s:Invalid session handle\n", __FUNCTION__);
      return CGMI_ERROR_INVALID_HANDLE;
   }

   if ( index > pSess->numAudioLanguages - 1 || index < 0 )
   {
      g_print("Bad index value passed for audio language!\n");
      return CGMI_ERROR_BAD_PARAM;
   }

   if ( NULL == pSess->demux )
   {
      g_print("Demux is not ready yet, cannot set audio stream!\n");
      return CGMI_ERROR_NOT_READY;
   }

   g_print("Setting audio stream index to %d for language %s\n",
           pSess->audioLanguages[index].index, pSess->audioLanguages[index].isoCode);

   pSess->audioLanguageIndex = pSess->audioLanguages[index].index;

   g_object_set( G_OBJECT(pSess->demux), "audio-stream", pSess->audioLanguageIndex, NULL );

   return CGMI_ERROR_SUCCESS;
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
      gst_object_unref ( pSess->userDataAppsinkPad );
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
