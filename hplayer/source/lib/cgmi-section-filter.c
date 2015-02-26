#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gst/gst.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <gst/app/gstappsink.h>
#include "cgmi-section-filter-priv.h"

#define FILTER_MAX_LENGTH 16
#define PRINT_HEX_WIDTH 16

//#define CGMI_SECTTION_FILTER_HEX_DUMP

static cgmi_Status cgmiCreateFilter( void *pSession, int pid, void *pFilterPriv, ciscoGstFilterFormat format, void **pFilterId  );

#ifdef CGMI_SECTTION_FILTER_HEX_DUMP
static void printHex( void *buffer, int size )
{
   gint hexBufSize = (PRINT_HEX_WIDTH * 3) + 8;
   guchar asciiBuf[PRINT_HEX_WIDTH + 1];
   guchar hexBuf[hexBufSize];
   guchar *pBuffer = buffer;
   gint i, hexBufIdx = 0;

   for ( i = 0; i < size; i++ )
   {

      // Print the previous chucks ascii
      if ( (i % PRINT_HEX_WIDTH) == 0 )
      {
         if ( i != 0 ) g_print("%s  %s\n", hexBuf, asciiBuf);
         snprintf(&hexBuf[hexBufIdx], hexBufSize - hexBufIdx, "  %04x ", i);
         hexBufIdx = strlen(hexBuf);
      }

      // append current byte to buffer
      snprintf(&hexBuf[hexBufIdx], hexBufSize - hexBufIdx, " %02x", pBuffer[i]);
      hexBufIdx = strlen(hexBuf);

      if ( (pBuffer[i] < 0x20) || (pBuffer[i] > 0x7e) )
      {
         asciiBuf[i % PRINT_HEX_WIDTH] = '.';
      }
      else
      {
         asciiBuf[i % PRINT_HEX_WIDTH] = pBuffer[i];
      }
      asciiBuf[(i % PRINT_HEX_WIDTH) + 1] = '\0';
   }

   // Handle one off last ascii print
   while ( (i % PRINT_HEX_WIDTH) != 0 )
   {
      g_print("   ");
      i++;
   }
   g_print("  %s\n", asciiBuf);
}
#endif


static cgmi_Status charBufToGValueArray( char *buffer,
                                         int bufSize,
                                         GValueArray **valueArray )
{
   int i;

   // Preconditions we need a non-negative bufSize, and a non-NULL buffer
   if ( NULL == buffer || 0 >= bufSize )
   {
      return CGMI_ERROR_BAD_PARAM;
   }

   GValueArray *array = g_value_array_new(bufSize);
   if ( NULL == array )
   {
      return CGMI_ERROR_OUT_OF_MEMORY;
   }

   *valueArray = array;

   for ( i = 0; i < bufSize; i++ )
   {
      GValue value = { 0 };
      g_value_init(&value, G_TYPE_UCHAR);
      g_value_set_uchar(&value, buffer[i]);
      g_value_array_append(array, &value);
      g_value_unset(&value);
   }

   return CGMI_ERROR_SUCCESS;
}

static GstFlowReturn cgmi_filter_gst_appsink_new_buffer( GstAppSink *sink, gpointer user_data )
{
   cgmi_Status retStat = CGMI_ERROR_FAILED;
   tSectionFilter *secFilter = (tSectionFilter *)user_data;
   tSession *pSess;
   char *retBuffer = NULL;
   int retBufferSize;
   guint8 *sinkData;
   guint sinkDataSize;
   GstBuffer *buffer;
#if GST_CHECK_VERSION(1,0,0)
   GstSample *sample;
   GstMapInfo map;
#endif


   // Check preconditions
   if ( NULL == secFilter )
   {
      g_print("Error appsink callback has invalid user_data.\n");
      return GST_FLOW_OK;
   }

   if ( NULL == secFilter->bufferCB || NULL == secFilter->sectionCB )
   {
      g_print("Error appsink callback failed to find CGMI callback(s).\n");
      return GST_FLOW_OK;
   }

   pSess = (tSession *)secFilter->parentSession;
   if ( NULL == pSess )
   {
      g_print("Error appsink callback lacks session reference.\n");
      return GST_FLOW_OK;
   }

   // Pull the buffer
#if GST_CHECK_VERSION(1,0,0)
   sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
   if ( NULL == sample )
   {
      g_print("Error appsink callback failed to pull sample.\n");
      return GST_FLOW_OK;
   }
   buffer = gst_sample_get_buffer(sample);
#else
   buffer = gst_app_sink_pull_buffer(GST_APP_SINK(sink));
#endif

   if ( NULL == buffer )
   {
      g_print("Error appsink callback failed to pull buffer.\n");
      return GST_FLOW_OK;
   }

   do
   {

      // Check this filter for the correct state
      if ( secFilter->lastAction != FILTER_START ) break;

#if GST_CHECK_VERSION(1,0,0)
      if ( gst_buffer_map(buffer, &map, GST_MAP_READ) == FALSE )
      {
         g_print("Failed in mapping appsink buffer for reading section data!\n");
         break;
      }

      sinkData = map.data;
      sinkDataSize = map.size;
#else
      sinkData = GST_BUFFER_DATA(buffer);
      sinkDataSize = GST_BUFFER_SIZE(buffer);
#endif

      // Init the buffer size to the size of the current buffer.  The app should
      // provide a buffer of this size or larger.
      retBufferSize = (int)sinkDataSize;

      // Ask nicely for a buffer from the app
      retStat = secFilter->bufferCB(pSess->usrParam, secFilter->filterPrivate,
                                    (void *)secFilter, &retBuffer, &retBufferSize);

      // Verify the app provided a useful buffer
      if ( retStat != CGMI_ERROR_SUCCESS )
      {
         g_print("Failed in queryBufferCB with error (%s)\n",
                 cgmi_ErrorString(retStat));
#if GST_CHECK_VERSION(1,0,0)
         gst_buffer_unmap(buffer, &map);
#endif
         break;
      }
      if ( retBufferSize < sinkDataSize )
      {
         g_print("Error buffer returned from queryBufferCB is too small.\n");
#if GST_CHECK_VERSION(1,0,0)
         gst_buffer_unmap(buffer, &map);
#endif
         break;
      }
      if ( NULL == retBuffer )
      {
         g_print("Error NULL buffer returned from queryBufferCB.\n");
#if GST_CHECK_VERSION(1,0,0)
         gst_buffer_unmap(buffer, &map);
#endif
         break;
      }

      //g_print("Filling buffer of size (%d)\n", sinkDataSize);
      memcpy(retBuffer, sinkData, sinkDataSize);

#ifdef CGMI_SECTTION_FILTER_HEX_DUMP
      g_print("Sending buffer:\n");
      printHex(retBuffer, sinkDataSize);
      g_print("\n\n");
#endif

      // Return filled buffer to the app
      retStat = secFilter->sectionCB(pSess->usrParam, secFilter->filterPrivate,
                                     secFilter, CGMI_ERROR_SUCCESS, retBuffer, (int)sinkDataSize);
#if GST_CHECK_VERSION(1,0,0)
      gst_buffer_unmap(buffer, &map);
#endif

   }while ( 0 );

#if GST_CHECK_VERSION(1,0,0)
   gst_sample_unref(sample);
#else
   gst_buffer_unref(buffer);
#endif

   return GST_FLOW_OK;
}

static void cgmi_filter_gst_pad_added( GstElement *element, GstPad *pad, gpointer data )
{
   tSectionFilter *secFilter = (tSectionFilter *)data;
   tSession *pSess;
   GstCaps *caps = NULL;
   GstState state;

   g_print("ON_PAD_ADDED\n");

   if ( NULL == pad )
   {
      g_print("NULL pad received in pad_added callback\n");
      return;
   }

   if ( NULL == secFilter )
   {
      g_print("NULL user data (secFilter) received in pad_added callback\n");
      return;
   }

   pSess = (tSession *)secFilter->parentSession;
   if ( NULL == pSess )
   {
      g_print("NULL user data (pSess) received in pad_added callback\n");
      return;
   }

#if GST_CHECK_VERSION(1,0,0)
   caps = gst_pad_query_caps(pad, NULL);
#else
   caps = gst_pad_get_caps(pad);
#endif

   if ( NULL != caps )
   {
      gchar *caps_string = gst_caps_to_string(caps);
      g_print("Caps: %s\n", caps_string);
      if ( NULL != caps_string )
      {
         if ( NULL != strstr(caps_string, "x-mpegts-private-section") )
         {
            g_print("Adding appsink and linking it to demux for section filtering...\n");
            GstAppSinkCallbacks appsink_cbs = { NULL, NULL, cgmi_filter_gst_appsink_new_buffer, NULL };
            secFilter->appsink = gst_element_factory_make("appsink", NULL);
            g_print("Obtained new appsink (%p)\n", secFilter->appsink);
            g_object_set(secFilter->appsink, "emit-signals", TRUE, "caps", caps, NULL);

            gst_app_sink_set_callbacks(GST_APP_SINK(secFilter->appsink), &appsink_cbs, secFilter, NULL);
            gst_bin_add_many(GST_BIN(GST_ELEMENT_PARENT(pSess->demux)), secFilter->appsink, NULL);
            g_print("Linking appsink (%p) to demux (%p)\n", secFilter->appsink, pSess->demux);
            if ( TRUE != gst_element_link(pSess->demux, secFilter->appsink) )
            {
               g_print("Could not link demux to appsink!\n");
            }

            // This sync state is required when the appsink element is added to
            // the pipeline after it has started playback.
            //if ( TRUE != gst_element_sync_state_with_parent(secFilter->appsink) )
            gst_element_get_state(pSess->demux, &state, NULL, 0);
            if ( TRUE != gst_element_set_state(secFilter->appsink, state) );
            {
               g_print("Could not sync appsink state with its parent!\n");
            }

            // Once we have connected the appsink this callback can be disconnected
            g_signal_handler_disconnect(pSess->demux, secFilter->padAddedCbId);

         }
         g_free(caps_string);
      }

      gst_caps_unref(caps);
   }
}

cgmi_Status cgmi_CreateSectionFilter( void *pSession, int pid, void *pFilterPriv, void **pFilterId  )
{
   return cgmiCreateFilter(pSession, pid, pFilterPriv, FILTER_PSI, pFilterId);
}

cgmi_Status cgmi_CreateFilter( void *pSession, int pid, void *pFilterPriv, tcgmi_FilterFormat format, void **pFilterId )
{
   return cgmiCreateFilter(pSession, pid, pFilterPriv, format, pFilterId);
}

cgmi_Status cgmi_DestroySectionFilter( void *pSession, void *pFilterId )
{
   cgmi_Status retStat = CGMI_ERROR_SUCCESS;
   tSession *pSess = (tSession *)pSession;
   tSectionFilter *secFilter = (tSectionFilter *)pFilterId;

   // Check preconditions
   if ( pSession == NULL || pFilterId == NULL )
   {
      return CGMI_ERROR_BAD_PARAM;
   }

   if ( pSess->demux == NULL )
   {
      g_print("Failed with NULL demux.\n");
      return CGMI_ERROR_FAILED;
   }

   if ( secFilter->handle == NULL )
   {
      g_print("Failed with NULL section-filter handle.  Is section filter open?\n");
      return CGMI_ERROR_FAILED;
   }

   g_print("Destroying section filter (%p), appsink (%p)...\n", secFilter->handle, secFilter->appsink);

   secFilter->lastAction = FILTER_CLOSE;

   // Unlink and remove app sink from pipeline
   gst_element_unlink(pSess->demux, secFilter->appsink);

   gst_element_set_state(secFilter->appsink, GST_STATE_NULL);

   gst_bin_remove_many(GST_BIN(GST_ELEMENT_PARENT(pSess->demux)), secFilter->appsink, NULL);

   // Close the section filter
   g_object_set(secFilter->handle, "filter-action", FILTER_CLOSE, NULL);
   g_object_set(G_OBJECT(pSess->demux), "section-filter", secFilter->handle, NULL);

   // Clean house
   secFilter->appsink = NULL;

   g_free(secFilter);

   return retStat;
}

cgmi_Status cgmi_SetSectionFilter( void *pSession, void *pFilterId, tcgmi_FilterData *pFilterData )
{
   cgmi_Status retStat = CGMI_ERROR_SUCCESS;
   tSession *pSess = (tSession *)pSession;
   tSectionFilter *secFilter = (tSectionFilter *)pFilterId;
   GValueArray *valueArray;

   // Check preconditions
   if ( pSession == NULL || pFilterId == NULL )
   {
      return CGMI_ERROR_BAD_PARAM;
   }

   if ( NULL == pFilterData )
   {
      g_print("Error param is NULL.\n");
      return CGMI_ERROR_BAD_PARAM;
   }

   if ( NULL == pSess->demux )
   {
      g_print("Failed with NULL demux.\n");
      return CGMI_ERROR_FAILED;
   }

   if ( NULL == secFilter->handle )
   {
      g_print("Failed with NULL section-filter handle.  Is section filter open?\n");
      return CGMI_ERROR_FAILED;
   }


   do
   {

      // If a value/mask was provided marshal values for gstreamer
      if ( pFilterData->length > 0 )
      {

         // Broadcom bug workaround.  The section filter drivers/hardware
         // doesn't mask the 3rd byte correctly, so mask it out
         if ( pFilterData->length > 2 )
         {
            pFilterData->mask[2] = 0x00;
         }

         // If the mask is larger than what is supported by broadcom print warning.
         if ( pFilterData->length > FILTER_MAX_LENGTH )
         {
            g_print("Warning the filter mask (%d) is larger than supported (%d).",
                    pFilterData->length, FILTER_MAX_LENGTH);
         }

         // Convert the filter data to a glib compatible format
         retStat = charBufToGValueArray(pFilterData->value,
                                        pFilterData->length,
                                        &valueArray);

         if ( CGMI_ERROR_SUCCESS != retStat )
         {
            g_print("Failed setting section filter value.\n");
            break;
         }

         g_object_set(secFilter->handle, "filter-data", valueArray, NULL);
         g_value_array_free(valueArray);


         // Convert the filter data to a glib compatible format
         retStat = charBufToGValueArray(pFilterData->mask,
                                        pFilterData->length,
                                        &valueArray);

         if ( CGMI_ERROR_SUCCESS != retStat )
         {
            g_print("Failed setting section filter mask.\n");
            break;
         }

         g_object_set(secFilter->handle, "filter-mask", valueArray, NULL);
         g_value_array_free(valueArray);

      }

      secFilter->lastAction = FILTER_SET;

      // Set other filter params
      g_print("Filtering for pid: 0x%04x, with secFilter: %p format: %d\n",
              secFilter->pid, secFilter->handle, secFilter->format);

      g_object_set(secFilter->handle, "filter-pid", secFilter->pid, NULL);
      g_object_set(secFilter->handle, "filter-format", secFilter->format, NULL);
      g_object_set(secFilter->handle, "filter-mode", pFilterData->comparitor, NULL);
      g_object_set(secFilter->handle, "filter-type", FILTER_TYPE_IP, NULL);
      g_object_set(secFilter->handle, "filter-action", FILTER_SET, NULL);
      g_object_set(G_OBJECT(pSess->demux), "section-filter", secFilter->handle, NULL);
   }while ( 0 );

   return retStat;
}

cgmi_Status cgmi_StartSectionFilter( void *pSession,
                                     void *pFilterId,
                                     int timeout,
                                     int bOneShot,
                                     int bEnableCRC,
                                     queryBufferCB bufferCB,
                                     sectionBufferCB sectionCB )
{
   cgmi_Status retStat = CGMI_ERROR_SUCCESS;
   tSession *pSess = (tSession *)pSession;
   tSectionFilter *secFilter = (tSectionFilter *)pFilterId;

   // Check preconditions
   if ( pSession == NULL || pFilterId == NULL )
   {
      return CGMI_ERROR_BAD_PARAM;
   }

   if ( NULL == pSess->demux )
   {
      g_print("Failed with NULL demux.\n");
      return CGMI_ERROR_FAILED;
   }

   if ( NULL == secFilter->handle )
   {
      g_print("Failed with NULL section-filter handle.  Is section filter open?\n");
      return CGMI_ERROR_FAILED;
   }

   secFilter->timeout = timeout;
   secFilter->bOneShot = bOneShot;
   secFilter->bEnableCRC = bEnableCRC;
   secFilter->bufferCB = bufferCB;
   secFilter->sectionCB = sectionCB;

   secFilter->lastAction = FILTER_START;

   // Tell the demux to start the section filter
   g_object_set(secFilter->handle, "filter-action", FILTER_START, NULL);

   g_object_set(G_OBJECT(pSess->demux), "section-filter", secFilter->handle, NULL);


   return retStat;
}

cgmi_Status cgmi_StopSectionFilter( void *pSession, void *pFilterId )
{
   cgmi_Status retStat = CGMI_ERROR_SUCCESS;
   tSession *pSess = (tSession *)pSession;
   tSectionFilter *secFilter = (tSectionFilter *)pFilterId;

   // Check preconditions
   if ( pSession == NULL || pFilterId == NULL )
   {
      return CGMI_ERROR_BAD_PARAM;
   }

   if ( NULL == pSess->demux )
   {
      g_print("Failed with NULL demux.\n");
      return CGMI_ERROR_FAILED;
   }

   if ( NULL == secFilter->handle )
   {
      g_print("Failed with NULL section-filter handle.  Is section filter open?\n");
      return CGMI_ERROR_FAILED;
   }

   g_print("Stopping section filter (%p)...\n", secFilter->handle);

   // If the filter is already stopped, ignore the command.
   if ( FILTER_STOP == secFilter->lastAction )
   {
      g_print("Filter already stopped!\n");
      return CGMI_ERROR_SUCCESS;
   }

   secFilter->lastAction = FILTER_STOP;

   g_object_set(secFilter->handle, "filter-action", FILTER_STOP, NULL);

   g_object_set(G_OBJECT(pSess->demux), "section-filter", secFilter->handle, NULL);

   return retStat;
}

static cgmi_Status cgmiCreateFilter( void *pSession, int pid, void *pFilterPriv, ciscoGstFilterFormat format, void **pFilterId )
{
   cgmi_Status retStat = CGMI_ERROR_SUCCESS;
   tSession *pSess = (tSession *)pSession;
   tSectionFilter *secFilter = NULL;
   void *filterHandle = NULL;
   int filterId = -1;
   
   // Check preconditions
   if ( NULL == pSession )
   {
      return CGMI_ERROR_BAD_PARAM;
   }

   secFilter = g_malloc0(sizeof(tSectionFilter));
   if ( secFilter == NULL )
   {
      return CGMI_ERROR_OUT_OF_MEMORY;
   }

   // Set return pointer if we were able to allocate one
   *pFilterId = secFilter;

   // Init sectionFilter values
   secFilter->pid = pid;
   secFilter->format = format;
   secFilter->parentSession = pSession;
   secFilter->filterPrivate = pFilterPriv;
   secFilter->handle = NULL;
   secFilter->padAddedCbId = 0;
   secFilter->timeout = 0;
   secFilter->bOneShot = 0;
   secFilter->bEnableCRC = 0;
   secFilter->bufferCB = NULL;
   secFilter->sectionCB = NULL;
   secFilter->appsink = NULL;

   do
   {

      // We must have a demux reference.  The demux is found/set by the
      // element-added callback in cgmi-player.c
      if ( pSess->demux == NULL )
      {
         g_print("Failed to find demux.\n");
         retStat = CGMI_ERROR_FAILED;
         break;
      }

      // Setup callback
      secFilter->padAddedCbId = g_signal_connect(pSess->demux, "pad-added",
                                                 G_CALLBACK(cgmi_filter_gst_pad_added), secFilter);


      // Get a section filter handle
      g_object_get(G_OBJECT(pSess->demux), "section-filter", &filterHandle, NULL);

      if ( filterHandle == NULL )
      {
         g_print("Failed to get section-filter from demux.  Do we support section filters?\n");
         retStat = CGMI_ERROR_FAILED;
         break;
      }

      secFilter->handle = filterHandle;

      secFilter->lastAction = FILTER_OPEN;

      g_object_set(filterHandle, "filter-action", FILTER_OPEN, NULL);
      g_object_set(filterHandle, "filter-pid", 0x1FFF, NULL);
      g_object_set(filterHandle, "filter-format", secFilter->format, NULL);

      g_object_set(G_OBJECT(pSess->demux), "section-filter", filterHandle, NULL);
      g_object_get(G_OBJECT(filterHandle), "filter-id", &filterId, NULL);

      if ( -1 == filterId )
      {
         g_print("Failed to open section filter handle.\n");
         retStat = CGMI_ERROR_FAILED;
      }

      g_print("Created filter with handle = %p \n", secFilter->handle);

   }while ( 0 );

   // Clean up if there was an error
   if ( retStat != CGMI_ERROR_SUCCESS )
   {
      g_signal_handler_disconnect(pSess->demux, secFilter->padAddedCbId);
      g_free(secFilter);
      *pFilterId = NULL;
   }

   return retStat;
}

