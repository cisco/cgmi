#ifdef HAVE_CONFIG_H
# include <config.h>
#endif


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include "gst_player.h"

typedef void* ciscoGstFilter;

#define MAX_NUM_SECTION_FILTERS   3
#define FILTER_DATA_SIZE          16
#define FILTER_MASK_SIZE          16

typedef enum
{
    FILTER_TYPE_IP,
    FILTER_TYPE_TUNER
} ciscoGstFilterType;

typedef enum
{
    FILTER_OPEN  = 1,
    FILTER_START = 2, 
    FILTER_STOP  = 4,
    FILTER_CLOSE = 8,
    FILTER_SET   = 16
} ciscoGstFilterAction;

typedef enum
{
    FILTER_COMP_EQ,
    FILTER_COMP_NE
} ciscoGstFilterComp;

typedef struct 
{
    gint pid;
    struct
    {
        guchar *value;
        guchar *mask;
        guint offset;
        ciscoGstFilterComp comparitor;
        gint length;
    }filter;
} ciscoGstFilterParam;

typedef struct
{
   gchar          *playbackURI; /* URI to playback */
   GMainLoop      *loop;
   GstElement     *pipeline;
   GstElement     *source;
   GstElement     *videoSink;
   GstElement     *demux;
   GstElement     *appsink;
   GstBus         *bus;
   GstMessage     *msg;
   gint64         rate; //Default to play speed

} ciscoGstOptions;


typedef enum {
   GST_PLAY_FLAG_VIDEO                = 0x1,
   GST_PLAY_FLAG_AUDIO                = 0x2,
   GST_PLAY_FLAG_NATIVE_VIDEO         = 0x20,
   GST_PLAY_FLAG_NATIVE_AUDIO         = 0x40,
   GST_PLAY_FLAG_BUFFER_AFTER_DEMUX   = 0x100        
} ciscoGstPlayFlags;

static gboolean cisco_gst_handle_msg( GstBus *bus, GstMessage *msg, gpointer data );
static gpointer cisco_gst_launch_gmainloop (gpointer data)
{
   tSession *pSession = (tSession*) data;
   GMainContext *thread_context;

    thread_context = g_main_context_new ();
   pSession->loop = g_main_loop_new (thread_context, FALSE);
   if (pSession->loop == NULL)
   {
      g_print("Error Cereating a new Loop\n");
   }
   g_main_context_push_thread_default (thread_context);
   // save this context for later use.
   pSession->thread_context = g_main_context_ref_thread_default(); 
   //add the bus watch
   gst_bus_add_watch( pSession->bus, cisco_gst_handle_msg, pSession );
   g_print("about to start the loop\n");
   g_main_loop_run(pSession->loop);
   g_print("Exiting gmainloop\n");
}

static void cisco_gst_setState( tSession *pSession, GstState state )
{
   GstStateChangeReturn sret;

   sret = gst_element_set_state( pSession->pipeline, state );
   switch ( sret )
   {
      case GST_STATE_CHANGE_FAILURE:
         g_print("Set NULL State Failure\n");
         break;
      case GST_STATE_CHANGE_NO_PREROLL:
         g_print("Set NULL State No Preroll\n");
         break;
      case GST_STATE_CHANGE_ASYNC:
         g_print("Set NULL State Async\n");
         break;
      case GST_STATE_CHANGE_SUCCESS:
         g_print("Set NULL State Succeeded\n");
         break;
      default:
         g_print("Set NULL State Unknown\n");
         break;
   }

   return;
}
void debug_cisco_gst_streamDurPos( tSession *pSession )
{
   
   gint64 curPos = 0, curDur = 0;
   GstFormat gstFormat = GST_FORMAT_TIME;
 
   gst_element_query_position( pSession->pipeline, &gstFormat, &curPos );
   gst_element_query_duration( pSession->pipeline, &gstFormat, &curDur );

   g_print("Stream: %s\n", pSession->playbackURI );
   g_print("Position: %lld (seconds)\n", (curPos/GST_SECOND), gstFormat );
   g_print("Duration: %lld (seconds)\n", (curDur/GST_SECOND), gstFormat );
 
}

static gboolean cisco_gst_handle_msg( GstBus *bus, GstMessage *msg, gpointer data )
{
   tSession *pSession = (tSession*)data;

   switch( GST_MESSAGE_TYPE(msg) )
   {
      g_print("Got bus message of type: %s\n", GST_MESSAGE_SRC_NAME(msg));
      
      case GST_MESSAGE_EOS:
         g_print("End of Stream\n");
         break;
      case GST_MESSAGE_ERROR:
         {
            gchar  *debug;
            GError *error;

            gst_message_parse_error( msg, &error, &debug );
            g_free( debug );

            g_printerr("Error: %s\n", error->message);
            g_error_free( error );

            break;
         }
      case GST_MESSAGE_STATE_CHANGED:
         {
            /* only check message from the pipeline */
            if( GST_MESSAGE_SRC(msg) == GST_OBJECT(pSession->pipeline) )
            {
               GstState old_state, new_state, pending_state;

               gst_message_parse_state_changed( msg, &old_state, &new_state, &pending_state );
               g_print("Pipeline state change from %s to %s\n", gst_element_state_get_name(old_state), gst_element_state_get_name(new_state));


               /* Print position and duration when in playing state */
               if( GST_STATE_PLAYING == new_state )
               {
                  debug_cisco_gst_streamDurPos(pSession);
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



gboolean cisco_gst_init( int argc, char *argv[] )
{
   gboolean bReturn     = TRUE;
   gchar    *strVersion = NULL;
   GError   *error      = NULL;
   
   do 
   {
      /* Initialize gstreamer */
      if( !gst_init_check( &argc, &argv, &error ) )
      {
         g_critical("Failed to initialize gstreamer :%s\n", error->message);
         bReturn = FALSE;
         break;
      }

      /* Verify threading system in up */
      if( !g_thread_supported() )
      {
         g_critical("GLib Thread system not initialized\n");
         bReturn = FALSE;
         break;
      }

      /* print out the version */
      strVersion = gst_version_string();
      g_message("GStreamer Initialized with Version: %s\n", strVersion);
      g_free( strVersion );

   
   } while(0);

   return bReturn;
}

void cisco_gst_deinit( void )
{
   gst_deinit();

   return;
}


tSession*  cisco_create_session(void * usrParam)
{
   tSession *pSession = NULL;

   pSession = g_malloc0(sizeof(tSession));
   if (pSession == NULL){return NULL;}
   pSession->usrParam = usrParam;
   pSession->playbackURI = NULL;
   pSession->manualPipeline = NULL;


  // if ((pSession->thread =g_thread_create (cisco_gst_launch_gmainloop, pSession, TRUE, &err1)) == NULL)
//   {
//      GST_ERROR("Not able to launch the gmainloop thread: %s\n", err1->message);
//      //How do we handle this failure?
//      //
//   }
   //need a blocking semaphore here
   // but instead I will spin until the 
   // gmainloop is running

   return pSession;
}

void cisco_delete_session(tSession *pSession)
{
   GstState curState, pendState;

   gst_element_get_state( pSession->pipeline, &curState, &pendState, GST_CLOCK_TIME_NONE );
   if( GST_STATE_NULL != curState )
   {
      g_print("The pipeline is in the wrong state to delete this session. Are you still playing?\n");
      return;
   }

   g_main_loop_quit (pSession->loop);
   g_thread_join (pSession->thread);
   if (pSession->playbackURI) {g_free(pSession->playbackURI);}
   if (pSession->manualPipeline) {g_free(pSession->manualPipeline);}
   if (pSession->pipeline) {gst_object_unref (GST_OBJECT (pSession->pipeline));}
   if (pSession->loop) {g_main_loop_unref(pSession->loop);}
   g_free(pSession);

}


gint cisco_gst_set_pipeline(tSession *pSession, char *uri, const char *manualPipeline )
{
   GError *g_error_str =NULL; 
   GstParseFlags flags;
   GstParseContext *ctx;
   gchar **arr;
   gint ret =0;
   gchar *pPipeline;
   GError   *err1 = NULL ;


   pPipeline = g_strnfill(1024, '\0');
   if (pPipeline == NULL)
   {
      g_print("Error allocating memory\n");
      return -1;
   }
   

    pSession->playbackURI = g_strdup(uri);
    if (pSession->playbackURI == NULL)
    {
       printf("Not able to allocate memory\n");
    }
    printf("URI: %s\n", pSession->playbackURI);
   /* Create playback pipeline */

   if (manualPipeline)
   {
      GST_INFO("Setting up a manual Pipeline %s \n", manualPipeline);
      g_strlcpy(pPipeline, manualPipeline, 1024);
      pSession->manualPipeline = g_strdup(manualPipeline);
   }
   else
   {
      g_strlcpy(pPipeline, "playbin2 uri=", 1024);
      g_strlcat(pPipeline, uri, 1024);
      //TODO: set teh flags=0x63 here if it's running on real hardware.
   }

   do
   {
      g_print("Launching the pipeline with :\n%s\n",pPipeline);

      ctx = gst_parse_context_new ();
      pSession->pipeline = gst_parse_launch_full(pPipeline,
                                           ctx,
                                           GST_PARSE_FLAG_FATAL_ERRORS,
                                           &g_error_str);
      if (pSession->pipeline == NULL)
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
         ret = -1;
         break;
      }
   }while(0);

   /* Add bus watch for events and messages */
   pSession->bus = gst_element_get_bus( pSession->pipeline );
   if (pSession->bus == NULL)
   {
      GST_ERROR("THe bus is null in the pipeline\n");
   }
   if ((pSession->thread =g_thread_create (cisco_gst_launch_gmainloop, pSession, TRUE, &err1)) == NULL)
   {
      GST_ERROR("Not able to launch the gmainloop thread: %s\n", err1->message);
      //
   }
   while (FALSE == g_main_loop_is_running(pSession->loop))
   {
      g_usleep(100); 
   }
   g_print("ok the gmainloop for the thread ctx is running\n");
   // free memory
   gst_parse_context_free(ctx);
   if(g_error_str){g_error_free(g_error_str);}
   return ret;
}



gint cisco_gst_play(tSession *pSession  )
{
   cisco_gst_setState( pSession, GST_STATE_PLAYING);
   return 0;
}
gint cisco_gst_pause(tSession *pSession  )
{
   cisco_gst_setState( pSession, GST_STATE_PAUSED);
   return 0;
}

