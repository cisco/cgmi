#ifdef HAVE_CONFIG_H
# include <config.h>
#endif


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gst/gst.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <gst/app/gstappsink.h>
#include "cgmiPlayerApi.h"
#include "cgmi-priv-player.h"
#include "cgmi-section-filter-priv.h"

#define MAGIC_COOKIE 0xDEADBEEF
GST_DEBUG_CATEGORY_STATIC (cgmi); 
#define GST_CAT_DEFAULT cgmi

static gboolean cisco_gst_handle_msg( GstBus *bus, GstMessage *msg, gpointer data );

static gchar gDefaultAudioLanguage[4];

static int  cgmi_CheckSessionHandle(tSession *pSess)
{
   if (pSess->cookie != MAGIC_COOKIE)
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
void debug_cisco_gst_streamDurPos( tSession *pSess )
{
   
   gint64 curPos = 0, curDur = 0;
   GstFormat gstFormat = GST_FORMAT_TIME;
 
   gst_element_query_position( pSess->pipeline, &gstFormat, &curPos );
   gst_element_query_duration( pSess->pipeline, &gstFormat, &curDur );

   GST_INFO("Stream: %s\n", pSess->playbackURI );
   GST_INFO("Position: %lld (seconds)\n", (curPos/GST_SECOND), gstFormat );
   GST_INFO("Duration: %lld (seconds)\n", (curDur/GST_SECOND), gstFormat );
 
}

static gboolean cisco_gst_handle_msg( GstBus *bus, GstMessage *msg, gpointer data )
{
   tSession *pSess = (tSession*)data;
  
   if (cgmi_CheckSessionHandle(pSess) == FALSE) {return FALSE;}

   switch( GST_MESSAGE_TYPE(msg) )
   {
      GST_INFO("Got bus message of type: %s\n", GST_MESSAGE_SRC_NAME(msg));

      case GST_MESSAGE_ASYNC_DONE:
      GST_INFO("Async Done message\n");
      {
         pSess->eventCB(pSess->usrParam, (void*)pSess, NOTIFY_SEEK_DONE);
      }
      break;
      case GST_MESSAGE_EOS:
      {
         GST_INFO("End of Stream\n");
         pSess->eventCB(pSess->usrParam, (void*)pSess, NOTIFY_END_OF_STREAM);
      }
      break;
      case GST_MESSAGE_ELEMENT:
      // these are cisco added events.
      if( gst_structure_has_name(msg->structure, "extended_notification"))
      {    
         //const GValue *ntype;
         char  *ntype;
         //figure out which extended notification that we have recieved.
         GST_INFO("RECEIVED extended notification message\n"); 
         ntype = gst_structure_get_string(msg->structure, "notification");

         if (0 == strcmp(ntype, "first_pts_decoded"))
         {    
            pSess->eventCB(pSess->usrParam, (void*)pSess,NOTIFY_FIRST_PTS_DECODED );
            gst_message_unref (msg);
         }    
         else if (0 == strcmp(ntype, "stream_attrib_changed"))
         {    
            pSess->eventCB(pSess->usrParam, (void*)pSess,NOTIFY_VIDEO_RESOLUTION_CHANGED);
            gst_message_unref (msg);
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
               pSess->eventCB(pSess->usrParam, (void*)pSess,NOTIFY_MEDIAPLAYER_URL_OPEN_FAILURE);
            }
         }
         else if (error->domain == GST_STREAM_ERROR)
         {
            if (error->code == GST_STREAM_ERROR_FAILED) 
            {
               pSess->eventCB(pSess->usrParam, (void*)pSess,NOTIFY_MEDIAPLAYER_URL_OPEN_FAILURE);
            }
         }
         else if (error->domain == GST_ERROR_SYSTEM)
         {
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
                  g_object_get( pSess->pipeline, "video-sink", &videoSink, NULL );
                  if ( NULL != videoSink )
                  {
                     pSess->videoSink = videoSink;                     
                     gst_object_unref( videoSink );
                  }
               }
               if ( NULL != pSess->videoSink )
               {
                  gchar dim[64];
                  snprintf( dim, sizeof(dim), "%d,%d,%d,%d", 
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
   
   if ( NULL == pSess )
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
      g_object_set( G_OBJECT(pSess->demux), "program-number", 1, NULL );  

      g_object_get( obj, "pmt-info", &pmtInfo, NULL );

      g_object_get( pmtInfo, "program-number", &program, NULL );
      g_object_get( pmtInfo, "version-number", &version, NULL );
      g_object_get( pmtInfo, "pcr-pid", &pcrPid, NULL );
      g_object_get( pmtInfo, "stream-info", &streamInfos, NULL );
      g_object_get( pmtInfo, "descriptors", &descriptors, NULL );

      g_print("PMT: Program: %04x Version: %d pcr: %04x Streams: %d " "Descriptors: %d\n",
              (guint16)program, version, (guint16)pcrPid, streamInfos->n_values, descriptors->n_values);
      for ( j = 0; j < streamInfos->n_values; j++ ) 
      {
         value = g_value_array_get_nth( streamInfos, j );
         streamInfo = (GObject*) g_value_get_object( value );
         g_object_get( streamInfo, "pid", &esPid, NULL );
         g_object_get( streamInfo, "stream-type", &esType, NULL);
         g_object_get( streamInfo, "descriptors", &descriptors, NULL);
         g_print("Pid: %04x type: %x Descriptors: %d\n",(guint16)esPid, (guint8) esType, descriptors->n_values);
         for ( z = 0; z < descriptors->n_values; z++ )
         {
            GString *string;
            value = g_value_array_get_nth( descriptors, z );
            string = (GString *)g_value_get_boxed( value );
            gint len;
            if ( string->len > 2 ) 
            {
               len = (guint8)string->str[1];
               g_print("descriptor # %d tag %02x len %d\n", z, (guint8)string->str[0], len);
               switch ( string->str[0] )
               {
                  case 0x0A: /* ISO_639_language_descriptor */
                     g_print("Found audio language descriptor for stream %d\n", j);
                     if ( pSess->numAudioLanguages < MAX_AUDIO_LANGUAGE_DESCRIPTORS && len >= 4 )
                     {
                        pSess->audioLanguages[pSess->numAudioLanguages].pid = esPid;
                        pSess->audioLanguages[pSess->numAudioLanguages].streamType = esType;
                        pSess->audioLanguages[pSess->numAudioLanguages].index = j;
                        strncpy( pSess->audioLanguages[pSess->numAudioLanguages].isoCode, &string->str[2], 3 );
                        pSess->audioLanguages[pSess->numAudioLanguages].isoCode[3] = 0;
                        
                        if ( strlen(pSess->defaultAudioLanguage) > 0 && pSess->audioStreamIndex == -1 )
                        {
                           if ( strncmp(pSess->audioLanguages[pSess->numAudioLanguages].isoCode, pSess->defaultAudioLanguage, 3) == 0 )
                           {
                              g_print("Stream (%d) audio language matched to default audio lang %s\n", j, pSess->defaultAudioLanguage);
                              pSess->audioStreamIndex = j;
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
            }
         }
      }
      g_print ("------------------------------------------------------------------------- \n");
   }

   if ( pSess->audioStreamIndex != -1 )
   {
      g_print("Selecting audio stream index %d...\n");
      g_object_set( G_OBJECT(pSess->demux), "audio-stream", pSess->audioStreamIndex, NULL );
   }
}

static GstElement *cgmi_gst_find_element( GstBin *bin, gchar *ename )
{
   GstElement *element = NULL;
   GstElement *handle = NULL;
   GstIterator *iter = NULL;
   void *item;  
   gchar *name = NULL;
   gboolean done = FALSE;
  
   iter = gst_bin_iterate_elements( bin );
   while ( FALSE == done )
   {
      switch ( gst_iterator_next(iter, &item) ) 
      {
         case GST_ITERATOR_OK:
            element = (GstElement *)item;
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
               gst_object_unref( item );
            }            
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
   gst_iterator_free( iter );

   return handle;
}

void cgmi_gst_notify_source( GObject *obj, GParamSpec *param, gpointer data ) 
{
   GstElement *source = NULL;
   gchar *name;

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
   }

   gst_object_unref( source );
}                                                        

static void cgmi_gst_element_added( GstBin *bin, GstElement *element, gpointer data )
{
   GstElement *demux = NULL;
   GstElement *videoSink = NULL;
   GstElement *videoDecoder = NULL;
   tSession *pSess = (tSession*)data;

   gchar *name = gst_element_get_name( element );
   g_print("Element added: %s\n", name);   

   // If we haven't found the demux investigate this bin
   if ( NULL == pSess->demux )
   {
      demux = cgmi_gst_find_element( bin, "demux" );

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
   tSession *pSess = (tSession*)data;

   if ( NULL == pSess )
   {
      return GST_FLOW_OK;
   }

   // Pull the buffer
   buffer = gst_app_sink_pull_buffer( GST_APP_SINK(sink) );   

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
   return CGMI_ERROR_SUCCESS;
} 

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
   pSess->playbackURI = NULL;
   pSess->manualPipeline = NULL;
   pSess->eventCB = eventCB;
   pSess->demux = NULL;
   pSess->udpsrc = NULL;
   pSess->videoSink = NULL;
   pSess->videoDecoder = NULL;
   pSess->vidDestRect.x = 0;
   pSess->vidDestRect.y = 0;
   pSess->vidDestRect.w = VIDEO_MAX_WIDTH;
   pSess->vidDestRect.h = VIDEO_MAX_HEIGHT;

   strncpy( pSess->defaultAudioLanguage, gDefaultAudioLanguage, sizeof(pSess->defaultAudioLanguage) );
   pSess->defaultAudioLanguage[sizeof(pSess->defaultAudioLanguage) - 1] = 0;
   pSess->audioStreamIndex = -1;

   pSess->thread_ctx = g_main_context_new();
   pSess->loop = g_main_loop_new (pSess->thread_ctx, FALSE);
   if (pSess->loop == NULL)
   {
      GST_WARNING("Error creating a new Loop\n");
   }

   //pSess->thread =  g_thread_create ((GThreadFunc) g_main_loop_run,(void*)pSess->loop, TRUE, NULL); 
   pSess->thread = g_thread_new("signal_thread", g_main_loop_run, (void*)pSess->loop);
   if (!pSess->thread)
   {
      GST_INFO("Error launching thread for gmainloop\n");
   }
   //need a blocking semaphore here
   // but instead I will spin until the 
   // gmainloop is running
   while (FALSE == g_main_loop_is_running(pSess->loop))
   {
      g_usleep(100); 
   }
   GST_INFO("ok the gmainloop for the thread ctx is running\n");

   return CGMI_ERROR_SUCCESS;
}

cgmi_Status cgmi_DestroySession (void *pSession)
{
   cgmi_Status stat = CGMI_ERROR_SUCCESS;
   tSession *pSess = (tSession*)pSession;

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
   if (pSess->loop) {g_main_loop_unref(pSess->loop);}
   g_free(pSess);
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
   gchar manualPipeline[1024];

   tSession *pSess = (tSession*)pSession;

   pPipeline = g_strnfill(1024, '\0');
   if (pPipeline == NULL)
   {
      GST_WARNING("Error allocating memory\n");
      return CGMI_ERROR_OUT_OF_MEMORY;
   }


   pSess->playbackURI = g_strdup(uri);
   if (pSess->playbackURI == NULL)
   {
      printf("Not able to allocate memory\n");
      return CGMI_ERROR_OUT_OF_MEMORY;
   }
   g_print("URI: %s\n", pSess->playbackURI);
   /* Create playback pipeline */

   //
   // This section makes DLNA content hardcoded.  Need to optimize.
   //
   if (g_strrstr(pSess->playbackURI, ".mpeg") != NULL)
   {
      // This url is pointing to DLNA content, build a manual pipeline.
      memset(manualPipeline, 0, 1024);
      g_print("DLNA content, using Manual Pipeline");
      g_sprintf(manualPipeline, "rovidmp location=%s %s", pSess->playbackURI,"! decodebin2 name=dec ! brcmvideosink dec. ! brcmaudiosink");
      g_strlcpy(pPipeline, manualPipeline,1024);
      pSess->manualPipeline = g_strdup(manualPipeline);
   }
   else
   {
      // let's see if we are running on broadcom hardware if we are let's see if we can find there
      // video sink.  If it's there we need to set the flags variable so the pipeline knows to do 
      // color transformation and scaling in hardware
      g_strlcpy(pPipeline, "playbin2 uri=", 1024);
      g_strlcat(pPipeline, uri, 1024);
      if (gst_registry_find_plugin( gst_registry_get_default() , "brcmvideosink"))
      {
        g_print("Autoplugging on real broadcom hardware\n");
        g_strlcat(pPipeline," flags= 0x63", 1024);
      }
   }

   do
   {
      GST_INFO("Launching the pipeline with :\n%s\n",pPipeline);

      ctx = gst_parse_context_new ();
      pSess->pipeline = gst_parse_launch_full(pPipeline,
                                              ctx,
                                              GST_PARSE_FLAG_FATAL_ERRORS,
                                              &g_error_str);
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
         GstBin *decodebin2 = cgmi_gst_find_element( (GstBin *)pSess->pipeline, "dec" );

         if( NULL != decodebin2 )
         {
            g_signal_connect( decodebin2, "element-added", 
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

   do
   {
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
      pSess->audioStreamIndex = -1;

   }while (0);
   g_print("Exiting %s pipeline is now null\n",__FUNCTION__);
   return stat;
}
cgmi_Status cgmi_Play    (void *pSession)
{
   tSession *pSess = (tSession*)pSession;
   cgmi_Status stat = CGMI_ERROR_SUCCESS;

   cisco_gst_setState( pSess, GST_STATE_PLAYING);
   return stat;
}
cgmi_Status cgmi_SetRate (void *pSession,  float rate)
{

   tSession *pSess = (tSession*)pSession;
   cgmi_Status stat = CGMI_ERROR_SUCCESS;
   GstFormat format = GST_FORMAT_TIME;
   GstEvent *seek_event=NULL;
   gint64 position; 
   GstState  curState;
   GstQuery *streamInfo;

   streamInfo = gst_query_new_segment (GST_FORMAT_TIME);
   gst_element_get_state(pSess->pipeline, &curState, NULL, GST_CLOCK_TIME_NONE); 

   if (rate == 0.0)
   {
      cisco_gst_setState( pSess, GST_STATE_PAUSED );
      return stat;
   }
   else if (( rate == 1.0) && (curState == GST_STATE_PAUSED))
   {
      cisco_gst_setState( pSess, GST_STATE_PLAYING );
      return stat;
   }

   /* Obtain the current position, needed for the seek event */
   if (!gst_element_query_position (pSess->pipeline, &format, &position)) 
   {
      GST_ERROR ("Unable to retrieve current position.\n");
      return CGMI_ERROR_FAILED;
   }


   if (rate >=1.0)
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
      gst_element_send_event (pSess->pipeline, seek_event);
   }
   return stat;
}

cgmi_Status cgmi_SetPosition  (void *pSession,  float position)
{

   tSession *pSess = (tSession*)pSession;
   cgmi_Status stat = CGMI_ERROR_SUCCESS;
   do
   {
      g_print("Setting position to %f (ns)  \n", (position* GST_SECOND));
      if( !gst_element_seek( pSess->pipeline,
               1.0,
               GST_FORMAT_TIME,
               GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
               GST_SEEK_TYPE_SET,
               (position*GST_SECOND),
               GST_SEEK_TYPE_NONE,
               GST_CLOCK_TIME_NONE ))
      {
         GST_ERROR("Seek Failed\n");
      }

   } while (0);
   return stat;

}

cgmi_Status cgmi_GetPosition  (void *pSession,  float *pPosition)
{

   tSession *pSess = (tSession*)pSession;
   gint64 curPos = 0; 
   GstFormat gstFormat = GST_FORMAT_TIME;
   cgmi_Status stat = CGMI_ERROR_SUCCESS;

   // this returns nano seconds, change it to seconds.
   do
   {
      gst_element_query_position( pSess->pipeline, &gstFormat, &curPos );

      GST_INFO("Position: %lld (seconds)\n", (curPos/GST_SECOND) );
      *pPosition = (curPos/GST_SECOND);

   } while (0);
   return stat;

}
cgmi_Status cgmi_GetDuration  (void *pSession,  float *pDuration, cgmi_SessionType *type)
{

   tSession *pSess = (tSession*)pSession;
   cgmi_Status stat = CGMI_ERROR_SUCCESS;
   gint64 Duration = 0; 
   GstFormat gstFormat = GST_FORMAT_TIME;
   *type = FIXED;
   do
   {
      gst_element_query_duration( pSess->pipeline, &gstFormat, &Duration );

      GST_INFO("Stream: %s\n", pSess->playbackURI );
      GST_INFO("Position: %lld (seconds)\n", (Duration/GST_SECOND) );
      *pDuration = (Duration/GST_SECOND);

   } while (0);
   return stat;

}
cgmi_Status cgmi_GetRateRange (void *pSession,  float *pRewind, float *pFFoward )
{

   tSession *pSess = (tSession*)pSession;
   cgmi_Status stat =CGMI_ERROR_NOT_IMPLEMENTED ;

   do
   {


   } while (0);
   return stat;

}

cgmi_Status cgmi_canPlayType(const char *type, int *pbCanPlay )
{
   return CGMI_ERROR_NOT_IMPLEMENTED;
}

cgmi_Status cgmi_SetVideoRectangle (void *pSession, int x, int y, int w, int h )
{
   char *ptr;
   tSession *pSess = (tSession*)pSession;

   if ( NULL == pSess )
   {
      g_print("Invalid session handle!\n");
      return CGMI_ERROR_INVALID_HANDLE;
   }

   if ( x < 0 )
      x = 0;
   if ( x >= VIDEO_MAX_WIDTH )
      x = VIDEO_MAX_WIDTH - 1;

   if ( y < 0 )
      y = 0;
   if ( y >= VIDEO_MAX_HEIGHT )
      y = VIDEO_MAX_HEIGHT - 1;

   if ( x + w > VIDEO_MAX_WIDTH )
      w = VIDEO_MAX_WIDTH - x;
   if ( y + h > VIDEO_MAX_HEIGHT )
      h = VIDEO_MAX_HEIGHT - y;

   if ( w < 10 || h < 10 )
   {
      g_print("Adjusted video size too small, must be bigger than 10x10 pixels!");
      return CGMI_ERROR_BAD_PARAM;
   }

   pSess->vidDestRect.x = x;
   pSess->vidDestRect.y = y;
   pSess->vidDestRect.w = w;
   pSess->vidDestRect.h = h;

   if ( NULL != pSess->videoSink )
   {
      gchar dim[64];
      snprintf( dim, sizeof(dim), "%d,%d,%d,%d", 
                pSess->vidDestRect.x, pSess->vidDestRect.y, pSess->vidDestRect.w, pSess->vidDestRect.h);
      g_object_set( G_OBJECT(pSess->videoSink), "window_set", dim, NULL );
   }

   return CGMI_ERROR_SUCCESS;
}

cgmi_Status cgmi_GetNumAudioLanguages (void *pSession,  int *count)
{
   tSession *pSess = (tSession*)pSession;
   if ( NULL == pSess )
   {
      g_print("Invalid session handle!\n");
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

   if ( NULL == pSess )
   {
      g_print("Invalid session handle!\n");
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



cgmi_Status cgmi_SetAudioStream (void *pSession,  int index )
{
   cgmi_Status stat;
   tSession *pSess = (tSession*)pSession;
   if ( NULL == pSess )
   {
      g_print("Invalid session handle!\n");
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

   stat = cgmi_Unload( pSess );
   if ( stat != CGMI_ERROR_SUCCESS )
       return stat;

   g_print("Setting audio stream index to %d for language %s\n", 
           pSess->audioLanguages[index].index, pSess->audioLanguages[index].isoCode);

   pSess->audioStreamIndex = pSess->audioLanguages[index].index;

   stat = cgmi_Load( pSess, pSess->playbackURI );
   if ( stat != CGMI_ERROR_SUCCESS )
       return stat;

   stat = cgmi_Play( pSess );

   return stat;
}

cgmi_Status cgmi_SetDefaultAudioLang (void *pSession, const char *language )
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

   tSession *pSess = (tSession*)pSession;
   if ( NULL == pSess )
   {
      g_print("Invalid session handle!\n");
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
   gst_bin_add_many( GST_ELEMENT_PARENT(pSess->videoDecoder), pSess->userDataAppsink, NULL );

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

   gst_element_sync_state_with_parent( pSess->userDataAppsink );

   pSess->userDataBufferCB = bufferCB;
   pSess->userDataBufferParam = pUserData;

   g_print("Successfully started user data filter\n");

   return CGMI_ERROR_SUCCESS;
}

cgmi_Status cgmi_stopUserDataFilter( void *pSession, userDataBufferCB bufferCB )
{
   tSession *pSess = (tSession*)pSession;
   if ( NULL == pSess )
   {
      g_print("Invalid session handle!\n");
      return CGMI_ERROR_INVALID_HANDLE;
   }

   if ( NULL != pSess->userDataAppsinkPad && NULL != pSess->userDataAppsink )
   {
      gst_pad_unlink( pSess->userDataPad, pSess->userDataAppsinkPad );
   }

   if ( NULL != pSess->userDataAppsink )
   {
      gst_element_set_state( pSess->userDataAppsink, GST_STATE_NULL );
      gst_bin_remove( GST_ELEMENT_PARENT(pSess->videoDecoder), (GstElement *)pSess->userDataAppsink );
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
