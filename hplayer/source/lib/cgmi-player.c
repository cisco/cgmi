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
#include "cgmi-priv-player.h"
#include "cgmiPlayerApi.h"




static gboolean cisco_gst_handle_msg( GstBus *bus, GstMessage *msg, gpointer data );


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

   switch( GST_MESSAGE_TYPE(msg) )
   {
      GST_INFO("Got bus message of type: %s\n", GST_MESSAGE_SRC_NAME(msg));
      
      case GST_MESSAGE_EOS:
         GST_INFO("End of Stream\n");
         break;
      case GST_MESSAGE_ERROR:
         {
            gchar  *debug;
            GError *error;

            gst_message_parse_error( msg, &error, &debug );
            g_free( debug );

            GST_WARNING("Error: %s\n", error->message);
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
                  debug_cisco_gst_streamDurPos(pSess);
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

char* cgmi_ErrorString(cgmi_Status stat)
{
   const char *errorString[] = 
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


cgmi_Status cgmi_CreateSession (cgmi_EventCallback eventCB, void* pUserData, void **pSession )
{
   tSession *pSess = NULL;

   pSess = g_malloc0(sizeof(tSession));
   if (pSess == NULL)
   {
      return CGMI_ERROR_OUT_OF_MEMORY;
   }
   *pSession = pSess; 
   pSess->usrParam = pUserData;
   pSess->playbackURI = NULL;
   pSess->manualPipeline = NULL;

   pSess->thread_ctx = g_main_context_new();
   pSess->loop = g_main_loop_new (pSess->thread_ctx, FALSE);
   if (pSess->loop == NULL)
   {
      GST_WARNING("Error creating a new Loop\n");
   }

   pSess->thread =  g_thread_create ((GThreadFunc) g_main_loop_run,(void*)pSess->loop, TRUE, NULL); 
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
   GstState curState, pendState;


   g_main_loop_quit (pSess->loop);
   //TODO fix
   g_thread_join (pSess->thread);
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
   if (g_str_has_suffix(pSess->playbackURI, "mpeg"))
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
            g_warning("Error with pipeline:%s \n", g_error_str->message);
         }

         //now if there are missing elements that are needed in the pipeline, lets' dump
         //those out.
         if(g_error_matches(g_error_str, GST_PARSE_ERROR, GST_PARSE_ERROR_NO_SUCH_ELEMENT))
         {
            guint i=0; 
            g_warning("Missing these plugins:\n");
            arr = gst_parse_context_get_missing_elements(ctx);
            while (arr[i] != NULL)
            {
               g_warning("%s\n",arr[i]);
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
         GST_ERROR("THe bus is null in the pipeline\n");
      }

      // enable the notifications.
      source = gst_bus_create_watch(pSess->bus);
      g_source_set_callback(source, (GSourceFunc)cisco_gst_handle_msg, pSess, NULL);
      g_source_attach(source, pSess->thread_ctx);
      g_source_unref(source);


   }while(0);

   // free memory
   gst_parse_context_free(ctx);
   if(g_error_str){g_error_free(g_error_str);}
   return stat;
}


cgmi_Status cgmi_Unload  (void *pSession )
{
   
   tSession *pSess = (tSession*)pSession;
   cgmi_Status stat = CGMI_ERROR_SUCCESS;
   // we need to tear down the pipeline

   do
   {
      gst_element_set_state (pSess->pipeline, GST_STATE_NULL);
      // I dont' think that we have to free any memory associated
      // with the source ( for the bus watch as I already freed it when I set
      // it up.  Once the reference count goes to 0 it should be freed

      g_print ("Deleting pipeline\n");
      gst_object_unref (GST_OBJECT (pSess->pipeline));
      pSess->pipeline = NULL;

   }while (0);
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

   if (rate == 0.0)
   {
      cisco_gst_setState( pSess, GST_STATE_PAUSED);
   }
   else if ( rate == 1.0)
   {
      cisco_gst_setState( pSess, GST_STATE_PLAYING);
   }
   else
   {
      GST_WARNING("This rate is not supported\n");
      stat = CGMI_ERROR_NOT_SUPPORTED;
   }

   return stat;
}

cgmi_Status cgmi_SetPosition  (void *pSession,  float position)
{

   tSession *pSess = (tSession*)pSession;
   cgmi_Status stat = CGMI_ERROR_SUCCESS;

   do
   {

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

      GST_INFO("Stream: %s\n", pSess->playbackURI );
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


cgmi_Status cgmi_GetNumAudioStreams (void *pSession,  int *count)
{
   return CGMI_ERROR_NOT_IMPLEMENTED;
}

cgmi_Status cgmi_GetAudioStreamInfo (void *pSession,  int index, char* buf, int bufSize)
{
   return CGMI_ERROR_NOT_IMPLEMENTED;
}

cgmi_Status cgmi_SetAudioStream (void *pSession,  int index )

{
   return CGMI_ERROR_NOT_IMPLEMENTED;
}

cgmi_Status cgmi_SetDefaultAudioLang (void *pSession,  const char *language )
{
   return CGMI_ERROR_NOT_IMPLEMENTED;
}

cgmi_Status cgmi_CreateSectionFilter(void *pSession, void* pFilterPriv, void** pFilterId  )
{
   return CGMI_ERROR_NOT_IMPLEMENTED;
}

cgmi_Status cgmi_DestroySectionFilter(void *pSession, void* pFilterId  )
{
   return CGMI_ERROR_NOT_IMPLEMENTED;
}

cgmi_Status cgmi_SetSectionFilter(void *pSession, void* pFilterId, tcgmi_FilterData *pFilter  )
{
   return CGMI_ERROR_NOT_IMPLEMENTED;
}

cgmi_Status cgmi_StartSectionFilter(void *pSession, void* pFilterId, int timeout, int bOneShot , int bEnableCRC, queryBufferCB bufferCB,  sectionBufferCB sectionCB)
{
   return CGMI_ERROR_NOT_IMPLEMENTED;
}

cgmi_Status cgmi_StopSectionFilter(void *pSession, void* pFilterId )
{
   return CGMI_ERROR_NOT_IMPLEMENTED;
}


