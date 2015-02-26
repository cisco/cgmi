#include <errno.h>
#include <string.h>
#define _GNU_SOURCE
#include <stdio.h>
#undef _GNU_SOURCE
#include <signal.h>
#include <stdlib.h>
#include <glib.h>
#include <syslog.h>
#include <pthread.h>
#include <semaphore.h>
#include <glib/gprintf.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>
#include <gst/gst.h>
// put in a #define #include "diaglib.h"
#include "dbusPtrCommon.h"
#include "cgmiPlayerApi.h"
#include "cgmi_dbus_server_generated.h"
#include "cgmiDiagsApi.h"


////////////////////////////////////////////////////////////////////////////////
// Defines
////////////////////////////////////////////////////////////////////////////////
#define DEFAULT_SECTION_FILTER_BUFFER_SIZE 256
#define CMGI_FIFO_NAME_MAX 64
#define LOGGING_BUFFER_SIZE 512

////////////////////////////////////////////////////////////////////////////////
// Logging for daemon.  TODO:  Send to syslog when in background
////////////////////////////////////////////////////////////////////////////////
#define DAEMON_NAME "CGMID"

#define LOGGING_ENABLED

#ifdef LOGGING_ENABLED
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define CGMID_AT "DAEMON:" TOSTRING(__LINE__)

#define CGMID_INFO(...)   cgmiDaemonLog( CGMID_AT, LOG_INFO, __VA_ARGS__);
#define CGMID_ERROR(...)  cgmiDaemonLog( CGMID_AT, LOG_ERR, __VA_ARGS__);
#define CGMID_ENTER()     cgmiDaemonLog( CGMID_AT, LOG_DEBUG, \
                            ">>>> Enter %s()\n", __FUNCTION__);
#else
#define CGMID_INFO(...)
#define CGMID_ERROR(...)
#define CGMID_ENTER()
#endif

////////////////////////////////////////////////////////////////////////////////
// typedefs
////////////////////////////////////////////////////////////////////////////////
typedef enum
{
    CGMI_FIFO_STATE_CLOSED,
    CGMI_FIFO_STATE_CREATED,
    CGMI_FIFO_STATE_OPENED,
    CGMI_FIFO_STATE_FAILED
}tcgmi_FifoState;

typedef struct
{
    char fifoName[CMGI_FIFO_NAME_MAX];
    int  fd;
    tcgmi_FifoState state;
    pthread_t thread;
} tcgmi_FifoCallbackSink;

typedef struct
{
    const char        *open;
    int               priority;
} tcgmi_LogggingLevel;

typedef struct
{
    char                  buffer[LOGGING_BUFFER_SIZE];
    int                   bufferUsed;
    tcgmi_LogggingLevel   *curLogLevel;
    pthread_mutex_t       lock;
} tcgmi_LoggingBuffer;

////////////////////////////////////////////////////////////////////////////////
// Globals
////////////////////////////////////////////////////////////////////////////////
static gboolean              gCgmiInited                 = FALSE;
static gboolean              gInForeground               = FALSE;
static GHashTable            *gUserDataCallbackHash      = NULL;
static tcgmi_LoggingBuffer   gLoggingBuffer;

////////////////////////////////////////////////////////////////////////////////
// Logging
////////////////////////////////////////////////////////////////////////////////

static void flushLoggingBuffer();

static void cgmiDaemonLog( const char *open, int priority, const char *format, ... )
{
    gchar *message;
    va_list args;

    flushLoggingBuffer();

    va_start (args, format);
    message = g_strdup_vprintf (format, args);
    va_end (args);

    if( gInForeground == TRUE )
    {
        g_print("%-14s - %s", open, message);
    }else{
        syslog(priority, "%-14s - %s", open, message);
    }

    g_free (message);
}

static void gPrintToSyslog(const gchar *message)
{
    flushLoggingBuffer();
    syslog(LOG_INFO, "%-14s - %s", "G_STDOUT", message);
}
static void gPrintErrToSyslog(const gchar *message)
{
    flushLoggingBuffer();
    syslog(LOG_ERR, "%-14s - %s", "G_STDERR", message);
}

static int noop(void) { return 0; }

static tcgmi_LogggingLevel stdout_log_level = { "STDOUT", LOG_INFO };
static tcgmi_LogggingLevel stderr_log_level = { "STDERR", LOG_INFO };

/* The broadcom libraries call fflush after each fprintf(stderr,...).  This
 * breaks the buffering logic the flag _IOLBF would enable, and requires the
 * verboseness in this function to ensure syslog gets chunks of data separated 
 * by new lines.  Without this the 5-6 fprinf(stderr,...); fflush(); calls made
 * for a single line of debug are spread over 5-6 lines in syslog.
 */
static size_t handleLogMessage( void *cookie, char const *data, size_t leng )
{
    tcgmi_LogggingLevel* logLevel = (tcgmi_LogggingLevel *)cookie;
    bool hasNewLine = false;

    pthread_mutex_lock(&gLoggingBuffer.lock);

    do
    {
        // Flush buffer if log levels don't match...
        if( gLoggingBuffer.curLogLevel != logLevel && gLoggingBuffer.curLogLevel != NULL )
        {
            syslog( gLoggingBuffer.curLogLevel->priority, "%-14s - %.*s", gLoggingBuffer.curLogLevel->open, 
                gLoggingBuffer.bufferUsed, gLoggingBuffer.buffer);

            gLoggingBuffer.curLogLevel = logLevel;
        }

        /*
         * There are 3 ways to exit this do while.
         *
         *  1. No new line, and room in buffer for data.  Save data to be printed later.
         *  2. New line found, or no room in buffer with data in buffer.  Syslog both buffer and data.
         *  3. New line found, or no room in buffer with empty buffer.  Syslog just data data.
         */
        if( leng > 1 && ( data[leng-1] == '\n' || data[leng-2] == '\n' ) )
        {
            hasNewLine = true;
        }

        // Append current buffer if we have room, and data doesn't have a new line
        if( !hasNewLine && gLoggingBuffer.bufferUsed + leng < LOGGING_BUFFER_SIZE )
        {
            memcpy(&gLoggingBuffer.buffer[gLoggingBuffer.bufferUsed], data, leng);
            gLoggingBuffer.bufferUsed += leng;
            break;
        }

        // Syslog buffered data and new data, if we have both
        if( gLoggingBuffer.bufferUsed > 0 )
        {
            syslog(logLevel->priority, "%-14s - %.*s%.*s", logLevel->open, 
                gLoggingBuffer.bufferUsed, gLoggingBuffer.buffer, 
                (int)leng, data);

            gLoggingBuffer.bufferUsed = 0;
        }
        // Syslog new data data
        else
        {
            syslog(logLevel->priority, "%-14s - %.*s", logLevel->open, 
                gLoggingBuffer.bufferUsed, gLoggingBuffer.buffer, 
                (int)leng, data);
        }        

    }while(0);

    pthread_mutex_unlock(&gLoggingBuffer.lock);
    return leng;
}

static cookie_io_functions_t handle_log_fns = {
    (void*) noop, (void*) handleLogMessage, (void*) noop, (void*) noop
};

static void redirectStdStreamsToSyslog()
{
    // Replace stdout and stderr streams with our own
    stdout = fopencookie(&stdout_log_level, "w", handle_log_fns);
    stderr = fopencookie(&stderr_log_level, "w", handle_log_fns);
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);
}

static void flushLoggingBuffer()
{
    if( gLoggingBuffer.curLogLevel == NULL ) return;
    pthread_mutex_lock(&gLoggingBuffer.lock);
    syslog( gLoggingBuffer.curLogLevel->priority, "%-14s - %.*s", gLoggingBuffer.curLogLevel->open, 
        gLoggingBuffer.bufferUsed, gLoggingBuffer.buffer);

    gLoggingBuffer.curLogLevel = NULL;
    gLoggingBuffer.bufferUsed = 0;
    pthread_mutex_unlock(&gLoggingBuffer.lock);
}

////////////////////////////////////////////////////////////////////////////////
// Utility functions
////////////////////////////////////////////////////////////////////////////////
static void * createFifoThreadFnc( void *data )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    tcgmi_FifoCallbackSink *callbackData = data;
    int ret;

    if( NULL == callbackData )
    {
        CGMID_ERROR("Failed start callback fifo thread\n" );
        return NULL;  
    }

    // Open fifo
    callbackData->fd = open(callbackData->fifoName, O_WRONLY);
    if( -1 == callbackData->fd ) 
    {
        callbackData->state = CGMI_FIFO_STATE_FAILED;
        retStat = CGMI_ERROR_FAILED;
        unlink(callbackData->fifoName);
        callbackData->fd = -1;
    }
    else
    {
        // Set to nonblocking
        fcntl(callbackData->fd, F_SETFL, 
            fcntl(callbackData->fd, F_GETFL) | O_NONBLOCK);
    }

    // Success if we reach here.
    callbackData->state = CGMI_FIFO_STATE_OPENED;
}

static cgmi_Status asyncCreateFifo( tcgmi_FifoCallbackSink *cbData )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    int ret;

    if( NULL == cbData )
    {
        CGMID_ERROR("Failed start callback fifo thread with NULL cbData\n" );
        return CGMI_ERROR_FAILED;  
    }

    // Create fifo
    unlink(cbData->fifoName);
    ret = mkfifo(cbData->fifoName, 0777);
    if( 0 != ret ) 
    {
        cbData->state = CGMI_FIFO_STATE_FAILED;
        return CGMI_ERROR_FAILED; 
    }

    cbData->state = CGMI_FIFO_STATE_CREATED;

    if ( 0 != pthread_create(&cbData->thread, NULL, createFifoThreadFnc, cbData) )
    {
        g_print("Error launching thread for CreateFifo\n");
        return CGMI_ERROR_FAILED;
    }
    return CGMI_ERROR_SUCCESS;
}

static cgmi_Status closeFifo( tcgmi_FifoCallbackSink *cbData )
{
    close(cbData->fd);
    unlink(cbData->fifoName);

    return CGMI_ERROR_SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// Callbacks called by CGMI core to message client via DBUS
////////////////////////////////////////////////////////////////////////////////
static void cgmiEventCallback( void *pUserData, void *pSession, tcgmi_Event event, uint64_t code )
{
    GVariant *sessVar = NULL, *sessDbusVar = NULL;

    do{
        // Marshal filter id pointer
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        sessDbusVar = g_variant_new ( "v", sessVar );
        if( sessDbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            break;
        }
        sessDbusVar = g_variant_ref_sink(sessDbusVar);

        org_cisco_cgmi_emit_player_notify( (OrgCiscoCgmi *) pUserData,
                                           sessDbusVar,
                                           event,
                                           0,      //data
                                           code );

    }while(0);

    //Clean up
    if( sessDbusVar != NULL ) { g_variant_unref(sessDbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    CGMID_INFO("cgmiEventCallback -- pSession: %lu, event%d \n",
            (tCgmiDbusPointer)pSession, event);
}

static cgmi_Status cgmiQueryBufferCallback(
    void *pUserData,
    void *pFilterPriv,
    void *pFilterId,
    char **ppBuffer,
    int *pBufferSize )
{
    //CGMID_INFO("cgmiQueryBufferCallback -- pFilterId: %lu, pFilterPriv %lu \n",
    //        (guint64)pFilterId, (guint64)pFilterPriv);

    // Preconditions
    if( NULL == ppBuffer )
    {
        CGMID_ERROR("NULL buffer pointer passed to cgmiQueryBufferCallback.\n");
        return CGMI_ERROR_BAD_PARAM;
    }

    // Check if a size of greater than zero was provided, use default if not
    if( *pBufferSize <= 0 )
    {
        *pBufferSize = DEFAULT_SECTION_FILTER_BUFFER_SIZE;
    }

    //CGMID_INFO("Allocating buffer of size (%d)\n", *pBufferSize);
    *ppBuffer = g_malloc0(*pBufferSize);

    if( NULL == *ppBuffer )
    {
        *pBufferSize = 0;
        return CGMI_ERROR_OUT_OF_MEMORY;
    }

    return CGMI_ERROR_SUCCESS;
}

static cgmi_Status cgmiSectionBufferCallback(
    void *pUserData,
    void *pFilterPriv,
    void *pFilterId,
    cgmi_Status sectionStatus,
    char *pSection,
    int sectionSize)
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GVariantBuilder *sectionBuilder = NULL;
    GVariant *sectionArray = NULL;
    GVariant *filterIdVar = NULL, *filterDbusVar = NULL;
    int idx;

    //CGMID_INFO("cgmiSectionBufferCallback -- pFilterId: %lu, pFilterPriv: %lu \n",
    //        (guint64)pFilterId, (guint64)pFilterPriv);

    // Preconditions
    if( NULL == pSection )
    {
        CGMID_ERROR("NULL buffer passed to cgmiSectionBufferCallback.\n");
        return CGMI_ERROR_BAD_PARAM;
    }

    do{
        // Marshal gvariant buffer
        sectionBuilder = g_variant_builder_new( G_VARIANT_TYPE("ay") );
        if( sectionBuilder == NULL )
        {
            g_print("Failed to create new variant builder\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        for( idx = 0; idx < sectionSize; idx++ )
        {
            g_variant_builder_add( sectionBuilder, "y", pSection[idx] );
        }
        sectionArray = g_variant_builder_end( sectionBuilder );

        // Marshal filter id pointer
        filterIdVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pFilterId );
        if( filterIdVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        filterIdVar = g_variant_ref_sink(filterIdVar);

        filterDbusVar = g_variant_new ( "v", filterIdVar );
        if( filterDbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        filterDbusVar = g_variant_ref_sink(filterDbusVar);

        //CGMID_INFO("Sending pFilterId: 0x%lx, sectionSize %d\n", pFilterId, sectionSize);
        org_cisco_cgmi_emit_section_buffer_notify( (OrgCiscoCgmi *) pUserData,
                filterDbusVar,
                (gint)sectionStatus,
                sectionArray,
                sectionSize );

    }while(0);

    //Clean up
    if( filterDbusVar != NULL ) { g_variant_unref(filterDbusVar); }
    if( filterIdVar != NULL ) { g_variant_unref(filterIdVar); }
    if( sectionBuilder != NULL ) { g_variant_builder_unref( sectionBuilder ); }

    // Free buffer allocated in cgmiQueryBufferCallback
    g_free( pSection );

    return retStat;
}

/* This function will write exactly count bytes */
static cgmi_fifoCompleteWrite(int fd, void *buffer, size_t count)
{
    int bytesWritten = 0;
    int totalWritten = 0;

    do
    {
        bytesWritten = write( fd, buffer + totalWritten, count - totalWritten );
        if (0 >= bytesWritten)
        {
             return bytesWritten; 
        }
        totalWritten += bytesWritten;
    }while( totalWritten < count );
    return totalWritten;    
}

static cgmi_Status cgmiUserDataBufferCB (void *pUserData, void *pBuffer)
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    tcgmi_FifoCallbackSink *callbackData;
    GstBuffer *pGstbuffer;
#if GST_CHECK_VERSION(1,0,0)
    GstMapInfo map;
#endif
    int bytesWritten;
    guint8 *bufferData;
    guint bufferSize;

    if( pUserData == NULL || pBuffer == NULL )
    {
        CGMID_ERROR("NULL userData/buffer passed to cgmiUserDataBufferCB.\n");
        return CGMI_ERROR_BAD_PARAM;  
    }

    //CGMID_INFO("cgmiUserDataBufferCB -- pUserData: %lu, pBuffer: %lu \n",
    //        (tCgmiDbusPointer)pUserData, (tCgmiDbusPointer)pBuffer);

    // Casting fun
    callbackData = (tcgmi_FifoCallbackSink *)pUserData;
    pGstbuffer = (GstBuffer *)pBuffer;

    // Verify fifo is open
    if( callbackData->state != CGMI_FIFO_STATE_OPENED )
    {
        return CGMI_ERROR_WRONG_STATE;
    }

#if GST_CHECK_VERSION(1,0,0)
    if ( gst_buffer_map(pGstbuffer, &map, GST_MAP_READ) == FALSE )
    {
        g_print("Failed in mapping buffer for reading userdata!\n");
        return CGMI_ERROR_FAILED;
    }

    bufferData = map.data;
    bufferSize = map.size;
#else
    bufferData = GST_BUFFER_DATA( pGstbuffer );
    bufferSize = GST_BUFFER_SIZE( pGstbuffer );
#endif

    if( bufferSize < 0 )
    {
        CGMID_ERROR("Empty buffer?.\n");
#if GST_CHECK_VERSION(1,0,0)
        gst_buffer_unmap( pGstbuffer, &map );
#endif
        return CGMI_ERROR_BAD_PARAM;  
    }

    // Prepend bufferSize to the fifo. This way the client will be able to read
    // the whole buffer before passing it to the CC callback. 
    bytesWritten = cgmi_fifoCompleteWrite( callbackData->fd, &bufferSize, sizeof(bufferSize) );
    if( 0 >= bytesWritten ) {
        CGMID_ERROR("Failed writing to fifo with (%d).\n", errno);
        retStat = CGMI_ERROR_FAILED;
        callbackData->state = CGMI_FIFO_STATE_FAILED;
    }
    else
    {
        // Write the real data to fifo
        bytesWritten = cgmi_fifoCompleteWrite( callbackData->fd, bufferData, bufferSize );
        if( 0 >= bytesWritten ) {
            CGMID_ERROR("Failed writing to fifo with (%d).\n", errno);
            retStat = CGMI_ERROR_FAILED;
            callbackData->state = CGMI_FIFO_STATE_FAILED;
        }
    }
    
#if GST_CHECK_VERSION(1,0,0)
    gst_buffer_unmap( pGstbuffer, &map );
#endif

    return retStat;
}

////////////////////////////////////////////////////////////////////////////////
// CGMI Player APIs
////////////////////////////////////////////////////////////////////////////////
static gboolean
on_handle_cgmi_init (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;

    gLoggingBuffer.curLogLevel = NULL;

    CGMID_ENTER();

    /* We only call core Init on first IPC Init call.  Because Init and Deinit
     * were designed to be called only once in an app context the deamon must
     * maintain this state.
     */
    if( gCgmiInited == TRUE )
    {
        CGMID_INFO("cgmi_Init already called.\n");
    }else{
        retStat = cgmi_Init( );
        if( retStat == CGMI_ERROR_SUCCESS ) { gCgmiInited = TRUE; }
    }

    org_cisco_cgmi_complete_init (object,
                                  invocation,
                                  retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_term (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation )
{
    //cgmi_Status retStat = CGMI_ERROR_FAILED;

    CGMID_ENTER();

    // MZW:  The deamon shouldn't call Term (which calls gst_deinit breaking 
    // all gstreamer apis), so just nod and smile (return success).
    //retStat = cgmi_Term( );

    org_cisco_cgmi_complete_term (object,
                                  invocation,
                                  CGMI_ERROR_SUCCESS);

    return TRUE;
}

static gboolean
on_handle_cgmi_error_string (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    gint arg_status )
{
    gchar *statusString = NULL;

    //CGMID_ENTER();

    statusString = cgmi_ErrorString( arg_status );

    org_cisco_cgmi_complete_error_string ( object,
                                  invocation,
                                  statusString) ;

    return TRUE;
}

static gboolean
on_handle_cgmi_create_session (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    void *pSessionId = NULL;
    GVariant *sessVar = NULL, *dbusVar = NULL;

    CGMID_ENTER();

    retStat = cgmi_CreateSession( cgmiEventCallback, (void *)object, &pSessionId );

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSessionId );
        if( sessVar == NULL )
        {
            CGMID_ERROR("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        dbusVar = g_variant_new ( "v", sessVar );
        if( dbusVar == NULL )
        {
            CGMID_ERROR("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

        org_cisco_cgmi_complete_create_session (object,
                                                invocation,
                                                dbusVar,
                                                retStat);

    }while(0);

    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    return TRUE;
}

static gboolean
on_handle_cgmi_destroy_session (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    GVariant *sessVar = NULL;
    tCgmiDbusPointer pSession = 0;

    CGMID_ENTER();

    do{
        g_variant_get( arg_sessionId, "v", &sessVar );
        if( sessVar == NULL ) 
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
        g_variant_unref( sessVar );

        retStat = cgmi_DestroySession( (void *)pSession );

    }while(0);

    org_cisco_cgmi_complete_destroy_session (object,
            invocation,
            retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_can_play_type (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    const gchar *arg_type )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    gint bCanPlay = 0;

    //CGMID_ENTER();

    retStat = cgmi_canPlayType( arg_type, &bCanPlay );

    org_cisco_cgmi_complete_can_play_type (object,
                                           invocation,
                                           bCanPlay,
                                           retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_load (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId,
    const gchar *uri,
	GVariant *arg_cpBlobStruct,
    guint64 arg_cpBlobStructSize
	)
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
	gchar * cpBlob=NULL;
	 GVariantIter *iter = NULL;
	 gchar        byte;
	 uint32_t     ii = 0;
    GVariant *sessVar = NULL;
    tCgmiDbusPointer pSession;

    CGMID_ENTER();

    do{
        g_variant_get( arg_sessionId, "v", &sessVar );
        if( sessVar == NULL ) 
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
        g_variant_unref( sessVar );
		if (arg_cpBlobStructSize>0)
		{
			cpBlob = (gchar *)malloc(arg_cpBlobStructSize);
			if(NULL == cpBlob)
			{
				retStat = CGMI_ERROR_FAILED;
				break;
			}

		   g_variant_get(arg_cpBlobStruct, "ay", &iter); 
		   if(NULL != iter)
		   {    
			  while(g_variant_iter_loop(iter, "y", &byte) && (ii < arg_cpBlobStructSize))
			  {    
				 cpBlob[ii++] = byte;
			  }    
			  g_variant_iter_free(iter);
		   }
		  else
		  {
			retStat = CGMI_ERROR_FAILED;
			free(cpBlob);
            break;
		  }
		}	
        retStat = cgmi_Load( (void *)pSession, uri,(cpBlobStruct *)cpBlob );
        if (NULL != cpBlob)
        {
            free(cpBlob);
        }
    }while(0);

    org_cisco_cgmi_complete_load (object,
                                  invocation,
                                  retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_unload (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    GVariant *sessVar = NULL;
    tCgmiDbusPointer pSession;

    CGMID_ENTER();

    do{
        g_variant_get( arg_sessionId, "v", &sessVar );
        if( sessVar == NULL ) 
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
        g_variant_unref( sessVar );

        retStat = cgmi_Unload( (void *)pSession );

    }while(0);

    //FIX Chris
    org_cisco_cgmi_complete_unload (object,
                                  invocation,
                                  retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_play (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId,
    gint arg_autoPlay)
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    GVariant *sessVar = NULL;
    tCgmiDbusPointer pSession;

    CGMID_ENTER();

    do{
        g_variant_get( arg_sessionId, "v", &sessVar );
        if( sessVar == NULL ) 
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
        g_variant_unref( sessVar );

        retStat = cgmi_Play( (void *)pSession, arg_autoPlay );

    }while(0);

    org_cisco_cgmi_complete_play (object,
                                  invocation,
                                  retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_set_rate (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId,
    gdouble arg_rate )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    GVariant *sessVar = NULL;
    tCgmiDbusPointer pSession;

    CGMID_ENTER();

    do{
        g_variant_get( arg_sessionId, "v", &sessVar );
        if( sessVar == NULL ) 
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
        g_variant_unref( sessVar );

        retStat = cgmi_SetRate( (void *)pSession, arg_rate );

    }while(0);

    org_cisco_cgmi_complete_set_rate (object,
                                      invocation,
                                      retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_set_position (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId,
    gdouble arg_position )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    GVariant *sessVar = NULL;
    tCgmiDbusPointer pSession;

    CGMID_ENTER();

    do{
        g_variant_get( arg_sessionId, "v", &sessVar );
        if( sessVar == NULL ) 
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
        g_variant_unref( sessVar );

        retStat = cgmi_SetPosition( (void *)pSession, arg_position );

    }while(0);

    org_cisco_cgmi_complete_set_position (object,
                                          invocation,
                                          retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_get_position (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    float position = 0;
    GVariant *sessVar = NULL;
    tCgmiDbusPointer pSession;

    //CGMID_ENTER();

    do{
        g_variant_get( arg_sessionId, "v", &sessVar );
        if( sessVar == NULL ) 
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
        g_variant_unref( sessVar );

        retStat = cgmi_GetPosition( (void *)pSession, &position );

    }while(0);

    org_cisco_cgmi_complete_get_position (object,
                                          invocation,
                                          position,
                                          retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_get_duration (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    float duration = 0;
    cgmi_SessionType type = 0;
    GVariant *sessVar = NULL;
    tCgmiDbusPointer pSession;

    //CGMID_ENTER();

    do{
        g_variant_get( arg_sessionId, "v", &sessVar );
        if( sessVar == NULL ) 
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
        g_variant_unref( sessVar );

        retStat = cgmi_GetDuration( (void *)pSession, &duration, &type );

    }while(0);

    org_cisco_cgmi_complete_get_duration (object,
                                          invocation,
                                          duration,
                                          type,
                                          retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_get_rates (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId,
    unsigned int numRates )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    GVariant *sessVar = NULL;
    tCgmiDbusPointer pSession;
    float *pRates = NULL;
    gdouble rate = 0.0; 
    GVariantBuilder *ratesBuilder = NULL;
    GVariant *rateArr = NULL;
    int ii = 0;

    CGMID_ENTER();

    do {
       g_variant_get( arg_sessionId, "v", &sessVar );
       if( sessVar == NULL ) 
       {
          retStat = CGMI_ERROR_FAILED;
          break;
       }

       g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
       g_variant_unref( sessVar );

       pRates = (float *)malloc(sizeof(float) * numRates);
       if(NULL == pRates)
       {
          CGMID_ERROR("Failed to alloc memory for rates array\n" );
          retStat = CGMI_ERROR_FAILED;
          break;
       }

       retStat = cgmi_GetRates( (void *)pSession, pRates, &numRates );
       //CGMID_INFO("numRates = %u\n", numRates);

       ratesBuilder = g_variant_builder_new( G_VARIANT_TYPE("ad") );
       if(NULL == ratesBuilder)
       {
          CGMID_ERROR("Failed to create ratesBuilder\n");
          retStat = CGMI_ERROR_FAILED;
          break;
       }
       
       for( ii = 0; ii < numRates; ii++ )
       {
          //CGMID_INFO("pRates[%d] = %f\n", ii, pRates[ii]);
          rate = pRates[ii];
          g_variant_builder_add( ratesBuilder, "d", rate );
       }
       rateArr = g_variant_builder_end( ratesBuilder );

       org_cisco_cgmi_complete_get_rates (object,
                                          invocation,
                                          rateArr, 
                                          retStat);
    
    }while(0);
    
    if(NULL != ratesBuilder)
    {
       g_variant_builder_unref(ratesBuilder);
    }
    if(NULL != pRates)
    {
       free(pRates);
       pRates = NULL;
    }
    return TRUE;
}

static gboolean
on_handle_cgmi_set_video_rectangle (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId,
    int srcx,
    int srcy, 
    int srcw,
    int srch,
    int dstx,
    int dsty,
    int dstw,
    int dsth)
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    GVariant *sessVar = NULL;
    tCgmiDbusPointer pSession;

    CGMID_ENTER();

    do{
        g_variant_get( arg_sessionId, "v", &sessVar );
        if( sessVar == NULL ) 
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
        g_variant_unref( sessVar );

        retStat = cgmi_SetVideoRectangle( (void *)pSession, srcx, srcy, srcw, srch, dstx, dsty, dstw, dsth );

    }while(0);

    org_cisco_cgmi_complete_set_video_rectangle (object,
            invocation,
            retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_get_video_resolution (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    gint srcw = 0, srch = 0;
    GVariant *sessVar = NULL;
    tCgmiDbusPointer pSession;

    CGMID_ENTER();

    do{
        g_variant_get( arg_sessionId, "v", &sessVar );
        if( sessVar == NULL )
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
        g_variant_unref( sessVar );

        retStat = cgmi_GetVideoResolution( (void *)pSession, &srcw, &srch );

    }while(0);

    org_cisco_cgmi_complete_get_video_resolution (object,
            invocation,
            srcw,
            srch,
            retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_get_video_decoder_index (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    gint idx = 0;
    GVariant *sessVar = NULL;
    tCgmiDbusPointer pSession;

    CGMID_ENTER();

    do{
        g_variant_get( arg_sessionId, "v", &sessVar );
        if( sessVar == NULL )
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
        g_variant_unref( sessVar );

        retStat = cgmi_GetVideoDecoderIndex( (void *)pSession, &idx );

    }while(0);

    org_cisco_cgmi_complete_get_video_decoder_index (object,
            invocation,
            idx,
            retStat); 

    return TRUE;
}

static gboolean
on_handle_cgmi_get_num_audio_languages (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    gint count = 0;
    GVariant *sessVar = NULL;
    tCgmiDbusPointer pSession;

    CGMID_ENTER();

    do{
        g_variant_get( arg_sessionId, "v", &sessVar );
        if( sessVar == NULL ) 
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
        g_variant_unref( sessVar );

        retStat = cgmi_GetNumAudioLanguages( (void *)pSession, &count );

    }while(0);    

    org_cisco_cgmi_complete_get_num_audio_languages (object,
            invocation,
            count,
            retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_get_audio_lang_info (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId,
    gint index,
    gint bufSize )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    char *buffer = NULL;
    GVariant *sessVar = NULL;
    tCgmiDbusPointer pSession;

    CGMID_ENTER();
    
    do{
        if ( bufSize <= 0 )
        {
            retStat = CGMI_ERROR_BAD_PARAM;
            break;
    
        }

        buffer = g_malloc0(bufSize);    
        if ( NULL == buffer )
        {
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
         
        g_variant_get( arg_sessionId, "v", &sessVar );
        if( sessVar == NULL ) 
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
        g_variant_unref( sessVar );

        retStat = cgmi_GetAudioLangInfo( (void *)pSession, index, buffer, bufSize );

    }while(0);
       
    org_cisco_cgmi_complete_get_audio_lang_info (object,
            invocation,
            buffer,
            retStat);

    if ( NULL != buffer )
        g_free( buffer );

    return TRUE;
}

static gboolean
on_handle_cgmi_set_audio_stream (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId,
    gint index )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    GVariant *sessVar = NULL;
    tCgmiDbusPointer pSession;

    CGMID_ENTER();

    do{
        g_variant_get( arg_sessionId, "v", &sessVar );
        if( sessVar == NULL ) 
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
        g_variant_unref( sessVar );

        retStat = cgmi_SetAudioStream( (void *)pSession, index );

    }while(0);

    org_cisco_cgmi_complete_set_audio_stream (object,
            invocation,
            retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_set_default_audio_lang (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId,
    const char *language )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    GVariant *sessVar = NULL;
    tCgmiDbusPointer pSession;

    CGMID_ENTER();

    do{
        g_variant_get( arg_sessionId, "v", &sessVar );
        if( sessVar == NULL ) 
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
        g_variant_unref( sessVar );

        retStat = cgmi_SetDefaultAudioLang( (void *)pSession, language );

    }while(0);

    org_cisco_cgmi_complete_set_default_audio_lang (object,
            invocation,
            retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_get_num_closed_caption_services (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    gint count = 0;
    GVariant *sessVar = NULL;
    tCgmiDbusPointer pSession;

    CGMID_ENTER();

    do{
        g_variant_get( arg_sessionId, "v", &sessVar );
        if( sessVar == NULL )
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
        g_variant_unref( sessVar );

        retStat = cgmi_GetNumClosedCaptionServices( (void *)pSession, &count );

    }while(0);

    org_cisco_cgmi_complete_get_num_closed_caption_services (object,
            invocation,
            count,
            retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_get_closed_caption_service_info (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId,
    gint index,
    gint bufSize )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    char *buffer = NULL;
    char isDigital;
    int serviceNum;
    GVariant *sessVar = NULL;
    tCgmiDbusPointer pSession;

    CGMID_ENTER();

    do{
        if ( bufSize <= 0 )
        {
            retStat = CGMI_ERROR_BAD_PARAM;
            break;
        }

        buffer = g_malloc0(bufSize);
        if ( NULL == buffer )
        {
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }

        g_variant_get( arg_sessionId, "v", &sessVar );
        if( sessVar == NULL )
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
        g_variant_unref( sessVar );

        retStat = cgmi_GetClosedCaptionServiceInfo( (void *)pSession, index, buffer, bufSize, &serviceNum, &isDigital );

    }while(0);

    org_cisco_cgmi_complete_get_closed_caption_service_info (object,
            invocation,
            buffer,
            serviceNum,
            isDigital,
            retStat);

    if ( NULL != buffer )
        g_free( buffer );

    return TRUE;
}

static gboolean
on_handle_cgmi_create_section_filter (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId,
    gint arg_filterPid )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    void *pFilterId;
    GVariant *sessVar = NULL;
    GVariant *filterIdVar = NULL, *dbusVar = NULL;
    tCgmiDbusPointer pSession;

    CGMID_ENTER();

    do{
        g_variant_get( arg_sessionId, "v", &sessVar );
        if( sessVar == NULL ) 
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }
        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
        g_variant_unref( sessVar );

        // Provide a pointer to the sessionId as the private data.
        retStat = cgmi_CreateSectionFilter( (void *)pSession,
                                         arg_filterPid,
                                         (void *)object,
                                         &pFilterId );

        // Build GVariant to return filter ID pointer
        filterIdVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pFilterId );
        if( filterIdVar == NULL )
        {
            CGMID_INFO("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        filterIdVar = g_variant_ref_sink(filterIdVar);

        dbusVar = g_variant_new ( "v", filterIdVar );
        if( dbusVar == NULL )
        {
            CGMID_INFO("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

        // Return results
        org_cisco_cgmi_complete_create_section_filter (object,
                invocation,
                dbusVar,
                retStat);

    }while(0);

    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( filterIdVar != NULL ) { g_variant_unref(filterIdVar); }

    if( retStat != CGMI_ERROR_SUCCESS )
    {
        CGMID_ERROR("Failed with error %s.", cgmi_ErrorString(retStat) );
    }

    return TRUE;
}

static gboolean
on_handle_cgmi_destroy_section_filter (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId,
    GVariant *arg_filterId )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    GVariant *sessVar = NULL, *filterIdVar = NULL;
    tCgmiDbusPointer pSession, pFilterId;

    CGMID_ENTER();

    do{
        g_variant_get( arg_sessionId, "v", &sessVar );
        if( sessVar == NULL ) 
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }
        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
        g_variant_unref( sessVar );

        g_variant_get( arg_filterId, "v", &filterIdVar );
        if( filterIdVar == NULL ) 
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }
        g_variant_get( filterIdVar, DBUS_POINTER_TYPE, &pFilterId );
        g_variant_unref( filterIdVar );

        retStat = cgmi_DestroySectionFilter( (void *)pSession,
                                          (void *)pFilterId );


    }while(0);

    org_cisco_cgmi_complete_destroy_section_filter (object,
            invocation,
            retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_set_section_filter (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId,
    GVariant *arg_filterId,
    GVariant *arg_filterValue,
    GVariant *arg_filterMask,
    gint arg_filterLength,
    guint arg_filterOffset,
    gint arg_filterComparitor )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    GVariant *sessVar = NULL, *filterIdVar = NULL;
    tCgmiDbusPointer pSession, pFilterId;
    tcgmi_FilterData pFilter;
    GVariantIter *iter = NULL;
    guchar *filterValue = NULL;
    guchar *filterMask = NULL;
    gint idx;

    CGMID_ENTER();

    do{

        // If we have a mask and value to unmarshal do so
        if( arg_filterLength > 0 )
        {
            filterValue = g_malloc0(arg_filterLength);
            filterMask = g_malloc0(arg_filterLength);

            if( NULL == filterValue || NULL == filterMask )
            {
                CGMID_INFO("Error allocating memory.\n");
                retStat = CGMI_ERROR_OUT_OF_MEMORY;
                break;
            }   
        }

        // Unmarshal value and mask
        g_variant_get( arg_filterValue, "ay", &iter );
        idx = 0;
        while( idx < arg_filterLength && 
            g_variant_iter_loop(iter, "y", &filterValue[idx]) )
        {
            idx++;
        }

        g_variant_get( arg_filterMask, "ay", &iter );
        idx = 0;
        while( idx < arg_filterLength && 
            g_variant_iter_loop(iter, "y", &filterMask[idx]) )
        {
            idx++;
        }

        // Populate the filter struct
        pFilter.value = filterValue;
        pFilter.mask = filterMask;
        pFilter.length = arg_filterLength;
        pFilter.comparitor = arg_filterComparitor;

        // Unmarshal id pointers
        g_variant_get( arg_sessionId, "v", &sessVar );
        if( sessVar == NULL ) 
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }
        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
        g_variant_unref( sessVar );

        g_variant_get( arg_filterId, "v", &filterIdVar );
        if( filterIdVar == NULL ) 
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }
        g_variant_get( filterIdVar, DBUS_POINTER_TYPE, &pFilterId );
        g_variant_unref( filterIdVar );

        retStat = cgmi_SetSectionFilter( (void *)pSession,
                                      (void *)pFilterId, 
                                      &pFilter );

    }while(0);

    org_cisco_cgmi_complete_set_section_filter (object,
            invocation,
            retStat);

    // Clean up
    if( NULL != filterValue ) { g_free(filterValue); }
    if( NULL != filterMask ) { g_free(filterMask); }

    return TRUE;
}

static gboolean
on_handle_cgmi_start_section_filter (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId,
    GVariant *arg_filterId,
    gint timeout,
    gint oneShot,
    gint enableCRC )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    GVariant *sessVar = NULL, *filterIdVar = NULL;
    tCgmiDbusPointer pSession, pFilterId;

    CGMID_ENTER();

    do{
        // Unmarshal id pointers
        g_variant_get( arg_sessionId, "v", &sessVar );
        if( sessVar == NULL ) 
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }
        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
        g_variant_unref( sessVar );

        g_variant_get( arg_filterId, "v", &filterIdVar );
        if( filterIdVar == NULL ) 
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }
        g_variant_get( filterIdVar, DBUS_POINTER_TYPE, &pFilterId );
        g_variant_unref( filterIdVar );

        retStat = cgmi_StartSectionFilter( (void *)pSession,
                                        (void *)pFilterId,
                                        timeout,
                                        oneShot,
                                        enableCRC,
                                        cgmiQueryBufferCallback,
                                        cgmiSectionBufferCallback );

    }while(0);

    //TODO handle callbacks
    org_cisco_cgmi_complete_start_section_filter (object,
            invocation,
            retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_stop_section_filter (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId,
    GVariant *arg_filterId )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    GVariant *sessVar = NULL, *filterIdVar = NULL;
    tCgmiDbusPointer pSession, pFilterId;

    CGMID_ENTER();

    do{
        // Unmarshal id pointers
        g_variant_get( arg_sessionId, "v", &sessVar );
        if( sessVar == NULL ) 
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }
        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
        g_variant_unref( sessVar );

        g_variant_get( arg_filterId, "v", &filterIdVar );
        if( filterIdVar == NULL ) 
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }
        g_variant_get( filterIdVar, DBUS_POINTER_TYPE, &pFilterId );
        g_variant_unref( filterIdVar );

        retStat = cgmi_StopSectionFilter( (void *)pSession,
                                       (void *)pFilterId );

    }while(0);

    org_cisco_cgmi_complete_stop_section_filter (object,
            invocation,
            retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_start_user_data_filter (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    GVariant *sessVar = NULL;
    tCgmiDbusPointer pSession;
    tcgmi_FifoCallbackSink *callbackData;

    CGMID_ENTER();

    do{
        // Unmarshal id pointers
        g_variant_get( arg_sessionId, "v", &sessVar );
        if( sessVar == NULL ) 
        {
            CGMID_ERROR("Failed to get variant\n");
            retStat = CGMI_ERROR_FAILED;
            break;
        }
        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
        g_variant_unref( sessVar );


        // Create entry to track CC callbacks
        callbackData = g_malloc0(sizeof(tcgmi_FifoCallbackSink));
        if( NULL == callbackData )
        {
            CGMID_ERROR("Failed to allocate memory\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        g_hash_table_insert( gUserDataCallbackHash, (gpointer)pSession,
                             (gpointer)callbackData );

        // Create unique fifo name
        snprintf(callbackData->fifoName, CMGI_FIFO_NAME_MAX, 
            "/tmp/cgmiUserData-%016lx.fifo", pSession);

        // Create thread to open fifo
        CGMID_INFO("Creating fifo\n");
        callbackData->state = CGMI_FIFO_STATE_CLOSED;
        // Opening the fifo blocks until the reading side of the fifo is opened.
        // The DBUS client opens the reading side following the return call to
        // DBUS below.
        retStat = asyncCreateFifo( callbackData );
        if( CGMI_ERROR_SUCCESS != retStat )
        {
            CGMID_ERROR("Failed to create fifo (%s)\n", callbackData->fifoName );
            break; 
        }

        CGMID_INFO("Calling lib cgmi_startUserDataFilter\n");
        retStat = cgmi_startUserDataFilter( (void *)pSession,
                                            cgmiUserDataBufferCB,
                                            (void *)callbackData );

    }while(0);

    org_cisco_cgmi_complete_start_user_data_filter (object,
            invocation,
            callbackData->fifoName,
            retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_stop_user_data_filter (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    GVariant *sessVar = NULL;
    tCgmiDbusPointer pSession;
    tcgmi_FifoCallbackSink *callbackData;

    CGMID_ENTER();

    do{
        // Unmarshal id pointers
        g_variant_get( arg_sessionId, "v", &sessVar );
        if( sessVar == NULL ) 
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }
        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
        g_variant_unref( sessVar );

        retStat = cgmi_stopUserDataFilter( (void *)pSession,
                                           (void *)cgmiUserDataBufferCB );

        // Look up the callbackData
        callbackData = (tcgmi_FifoCallbackSink *) g_hash_table_lookup(
            gUserDataCallbackHash, (gpointer)pSession );

        if (callbackData == NULL)
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        // Clean up file descriptor and fifo file
        retStat = closeFifo( callbackData );

        // Remove callbackData from hash... this will free callbackData
        g_hash_table_remove( gUserDataCallbackHash, (gpointer)pSession );

    }while(0);

    org_cisco_cgmi_complete_stop_user_data_filter (object,
            invocation,
            retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_get_num_pids (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    gint count = 0;
    GVariant *sessVar = NULL;
    tCgmiDbusPointer pSession;

    CGMID_ENTER();

    do{
        g_variant_get( arg_sessionId, "v", &sessVar );
        if( sessVar == NULL ) 
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
        g_variant_unref( sessVar );

        retStat = cgmi_GetNumPids( (void *)pSession, &count );

    }while(0);    

    org_cisco_cgmi_complete_get_num_pids (object,
            invocation,
            count,
            retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_get_pid_info (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId,
    gint index )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    GVariant *sessVar = NULL;
    tCgmiDbusPointer pSession;
    tcgmi_PidData pidData;

    CGMID_ENTER();
    
    do{       
        g_variant_get( arg_sessionId, "v", &sessVar );
        if( sessVar == NULL ) 
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
        g_variant_unref( sessVar );

        retStat = cgmi_GetPidInfo( (void *)pSession, index, &pidData );

    }while(0);
       
    org_cisco_cgmi_complete_get_pid_info (object,
            invocation,
            pidData.pid,
            pidData.streamType,
            retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_set_pid_info (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId,
    gint index,
    gint type,
    gint enable )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    GVariant *sessVar = NULL;
    tCgmiDbusPointer pSession;

    CGMID_ENTER();

    do{
        g_variant_get( arg_sessionId, "v", &sessVar );
        if( sessVar == NULL ) 
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
        g_variant_unref( sessVar );

        retStat = cgmi_SetPidInfo( (void *)pSession, index, type, enable );

    }while(0);

    org_cisco_cgmi_complete_set_pid_info (object,
            invocation,
            retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_set_logging (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    const gchar *arg_gstDebugStr )
{

	cgmi_Status retStat = CGMI_ERROR_SUCCESS;

	retStat = cgmi_SetLogging( arg_gstDebugStr );

    org_cisco_cgmi_complete_set_logging ( object, invocation) ;

    return TRUE;
}

static gboolean 
on_handle_cgmiDiags_get_timing_metrics_max_count (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation)
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    gint count = 0;

    CGMID_ENTER();

	retStat = cgmiDiags_GetTimingMetricsMaxCount(&count);

    org_cisco_cgmi_complete_get_timing_metrics_max_count(object, invocation, count, retStat);

    return TRUE;
}

static gboolean 
on_handle_cgmiDiags_get_timing_metrics(
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    gint arg_bufSizeIn)
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    gint count = arg_bufSizeIn;
    GVariantBuilder *outBufBuilder = NULL;
    GVariant *outBuf = NULL;
    int idx,inBufSize, outBufSize;
    char *pInBuf = NULL;

    CGMID_ENTER();

    do{
        inBufSize = sizeof(tCgmiDiags_timingMetric)*count;
        pInBuf = g_malloc(inBufSize);

        if(NULL == pInBuf) 
        {
           g_print("Failed to create input buffer\n");
           retStat = CGMI_ERROR_OUT_OF_MEMORY;
           break;
        }

        retStat = cgmiDiags_GetTimingMetrics((tCgmiDiags_timingMetric *)pInBuf, &count);

        outBufSize = sizeof(tCgmiDiags_timingMetric)*count;

        // Marshal gvariant buffer
        outBufBuilder = g_variant_builder_new( G_VARIANT_TYPE("ay") );
        if( outBufBuilder == NULL )
        {
            g_print("Failed to create new variant builder\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        for( idx = 0; (idx < inBufSize) && (idx < outBufSize); idx++ )
        {
            g_variant_builder_add( outBufBuilder, "y", pInBuf[idx] );
        }
        outBuf = g_variant_builder_end( outBufBuilder );
    }while(0);

    org_cisco_cgmi_complete_get_timing_metrics (object, invocation, count, outBuf, retStat);

    g_print("%s: count = %d; inBufSize = %d; outBufSize = %d; idx = %d\n", __FUNCTION__, count, inBufSize, outBufSize, idx);

    //Clean up
    if( outBufBuilder != NULL ) { g_variant_builder_unref(outBufBuilder); }
    if( pInBuf != NULL ) { g_free(pInBuf); }

    return TRUE;
}

static gboolean 
on_handle_cgmiDiags_reset_timing_metrics (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation)
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;

    CGMID_ENTER();

    retStat = cgmiDiags_ResetTimingMetrics();

    org_cisco_cgmi_complete_reset_timing_metrics(object, invocation, retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_get_tsb_slide (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    unsigned long tsbSlide = 0;
    GVariant *sessVar = NULL;
    tCgmiDbusPointer pSession;

    //CGMID_ENTER();

    do{
        g_variant_get( arg_sessionId, "v", &sessVar );
        if( sessVar == NULL ) 
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
        g_variant_unref( sessVar );

        retStat = cgmi_GetTsbSlide( (void *)pSession, &tsbSlide );

    }while(0);

    org_cisco_cgmi_complete_get_tsb_slide ( object,
                                            invocation,
                                            tsbSlide,
                                            retStat );

    return TRUE;
}

static gboolean
on_handle_cgmi_get_num_subtitle_languages (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    gint count = 0;
    GVariant *sessVar = NULL;
    tCgmiDbusPointer pSession;

    CGMID_ENTER();

    do{
        g_variant_get( arg_sessionId, "v", &sessVar );
        if( sessVar == NULL ) 
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
        g_variant_unref( sessVar );

        retStat = cgmi_GetNumSubtitleLanguages( (void *)pSession, &count );

    }while(0);    

    org_cisco_cgmi_complete_get_num_subtitle_languages (object,
            invocation,
            count,
            retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_get_subtitle_info (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId,
    gint index,
    gint bufSize )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    char *buffer = NULL;
    GVariant *sessVar = NULL;
    tCgmiDbusPointer pSession;
    gushort pid = 0;
    gushort compPageId = 0;
    gushort ancPageId = 0;
    guchar type = 0;


    CGMID_ENTER();
    
    do{
        if ( bufSize <= 0 )
        {
            retStat = CGMI_ERROR_BAD_PARAM;
            break;
    
        }

        buffer = g_malloc0(bufSize);    
        if ( NULL == buffer )
        {
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
         
        g_variant_get( arg_sessionId, "v", &sessVar );
        if( sessVar == NULL ) 
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
        g_variant_unref( sessVar );

        retStat = cgmi_GetSubtitleInfo( (void *)pSession, index, buffer, bufSize, &pid, &type, &compPageId, &ancPageId );

    }while(0);
       
    org_cisco_cgmi_complete_get_subtitle_info( object,
                                               invocation,
                                               buffer,
                                               pid,
                                               type,
                                               compPageId,
                                               ancPageId,
                                               retStat );

    if ( NULL != buffer )
        g_free( buffer );

    return TRUE;
}

static gboolean
on_handle_cgmi_set_default_subtitle_lang (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    GVariant *arg_sessionId,
    const char *language )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    GVariant *sessVar = NULL;
    tCgmiDbusPointer pSession;

    CGMID_ENTER();

    do{
        g_variant_get( arg_sessionId, "v", &sessVar );
        if( sessVar == NULL ) 
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
        g_variant_unref( sessVar );

        retStat = cgmi_SetDefaultSubtitleLang( (void *)pSession, language );

    }while(0);

    org_cisco_cgmi_complete_set_default_subtitle_lang (object,
            invocation,
            retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_create_filter( OrgCiscoCgmi *object,
                              GDBusMethodInvocation *invocation,
                              GVariant *arg_sessionId,
                              gint arg_filterPid,
                              gint arg_filterFormat )
{
   cgmi_Status retStat = CGMI_ERROR_FAILED;
   void *pFilterId;
   GVariant *sessVar = NULL;
   GVariant *filterIdVar = NULL, *dbusVar = NULL;
   tCgmiDbusPointer pSession;

   CGMID_ENTER();

   do
   {
      g_variant_get( arg_sessionId, "v", &sessVar );
      if ( sessVar == NULL )
      {
         retStat = CGMI_ERROR_FAILED;
         break;
      }
      g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSession );
      g_variant_unref( sessVar );

      // Provide a pointer to the sessionId as the private data.
      retStat = cgmi_CreateFilter( (void *)pSession,
                                   arg_filterPid,
                                   (void *)object,
                                   arg_filterFormat,
                                   &pFilterId );

      // Build GVariant to return filter ID pointer
      filterIdVar = g_variant_new( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pFilterId );
      if ( filterIdVar == NULL )
      {
         CGMID_INFO( "Failed to create new variant\n" );
         retStat = CGMI_ERROR_OUT_OF_MEMORY;
         break;
      }
      filterIdVar = g_variant_ref_sink( filterIdVar );

      dbusVar = g_variant_new( "v", filterIdVar );
      if ( dbusVar == NULL )
      {
         CGMID_INFO( "Failed to create new variant\n" );
         retStat = CGMI_ERROR_OUT_OF_MEMORY;
         break;
      }
      dbusVar = g_variant_ref_sink( dbusVar );

      // Return results
      org_cisco_cgmi_complete_create_filter( object,
                                             invocation,
                                             dbusVar,
                                             retStat );

   }while ( 0 );

   if ( dbusVar != NULL )
   {g_variant_unref( dbusVar ); }
   if ( filterIdVar != NULL )
   {g_variant_unref( filterIdVar ); }

   if ( retStat != CGMI_ERROR_SUCCESS )
   {
      CGMID_ERROR( "Failed with error %s.", cgmi_ErrorString( retStat ) );
   }

   return TRUE;
}

////////////////////////////////////////////////////////////////////////////////
// DBUS setup callbacks
////////////////////////////////////////////////////////////////////////////////
static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
    CGMID_INFO("Acquired the name %s\n", name);
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
    CGMID_INFO("Lost the name %s\n", name);
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
    GError *error = NULL;
    OrgCiscoCgmi *interface = org_cisco_cgmi_skeleton_new();

    if ( NULL == interface )
    {
        CGMID_ERROR("org_cisco_dbustest_skeleton_new() FAILED.\n");
        return;
    }

    org_cisco_cgmi_set_verbose (interface, TRUE);

    CGMID_INFO("Bus acquired\n");

    g_signal_connect (interface,
                      "handle-init",
                      G_CALLBACK (on_handle_cgmi_init),
                      NULL);

    g_signal_connect (interface,
                      "handle-term",
                      G_CALLBACK (on_handle_cgmi_term),
                      NULL);

    g_signal_connect (interface,
                      "handle-error-string",
                      G_CALLBACK (on_handle_cgmi_error_string),
                      NULL);

    g_signal_connect (interface,
                      "handle-create-session",
                      G_CALLBACK (on_handle_cgmi_create_session),
                      NULL);

    g_signal_connect (interface,
                      "handle-destroy-session",
                      G_CALLBACK (on_handle_cgmi_destroy_session),
                      NULL);

    g_signal_connect (interface,
                      "handle-can-play-type",
                      G_CALLBACK (on_handle_cgmi_can_play_type),
                      NULL);

    g_signal_connect (interface,
                      "handle-load",
                      G_CALLBACK (on_handle_cgmi_load),
                      NULL);

    g_signal_connect (interface,
                      "handle-unload",
                      G_CALLBACK (on_handle_cgmi_unload),
                      NULL);

    g_signal_connect (interface,
                      "handle-play",
                      G_CALLBACK (on_handle_cgmi_play),
                      NULL);

    g_signal_connect (interface,
                      "handle-set-rate",
                      G_CALLBACK (on_handle_cgmi_set_rate),
                      NULL);

    g_signal_connect (interface,
                      "handle-set-position",
                      G_CALLBACK (on_handle_cgmi_set_position),
                      NULL);

    g_signal_connect (interface,
                      "handle-get-position",
                      G_CALLBACK (on_handle_cgmi_get_position),
                      NULL);

    g_signal_connect (interface,
                      "handle-get-duration",
                      G_CALLBACK (on_handle_cgmi_get_duration),
                      NULL);

    g_signal_connect (interface,
                      "handle-get-rates",
                      G_CALLBACK (on_handle_cgmi_get_rates),
                      NULL);

    g_signal_connect (interface,
                      "handle-set-video-rectangle",
                      G_CALLBACK (on_handle_cgmi_set_video_rectangle),
                      NULL);

    g_signal_connect (interface,
                      "handle-get-video-resolution",
                      G_CALLBACK (on_handle_cgmi_get_video_resolution),
                      NULL);

    g_signal_connect (interface,
                      "handle-get-video-decoder-index",
                      G_CALLBACK (on_handle_cgmi_get_video_decoder_index),
                      NULL);

    g_signal_connect (interface,
                      "handle-get-num-audio-languages",
                      G_CALLBACK (on_handle_cgmi_get_num_audio_languages),
                      NULL);

    g_signal_connect (interface,
                      "handle-get-audio-lang-info",
                      G_CALLBACK (on_handle_cgmi_get_audio_lang_info),
                      NULL);

    g_signal_connect (interface,
                      "handle-set-audio-stream",
                      G_CALLBACK (on_handle_cgmi_set_audio_stream),
                      NULL);

    g_signal_connect (interface,
                      "handle-set-default-audio-lang",
                      G_CALLBACK (on_handle_cgmi_set_default_audio_lang),
                      NULL);

    g_signal_connect (interface,
                      "handle-create-section-filter",
                      G_CALLBACK (on_handle_cgmi_create_section_filter),
                      NULL);

    g_signal_connect (interface,
                      "handle-get-num-closed-caption-services",
                      G_CALLBACK (on_handle_cgmi_get_num_closed_caption_services),
                      NULL);

    g_signal_connect (interface,
                      "handle-get-closed-caption-service-info",
                      G_CALLBACK (on_handle_cgmi_get_closed_caption_service_info),
                      NULL);

    g_signal_connect (interface,
                      "handle-destroy-section-filter",
                      G_CALLBACK (on_handle_cgmi_destroy_section_filter),
                      NULL);

    g_signal_connect (interface,
                      "handle-set-section-filter",
                      G_CALLBACK (on_handle_cgmi_set_section_filter),
                      NULL);

    g_signal_connect (interface,
                      "handle-start-section-filter",
                      G_CALLBACK (on_handle_cgmi_start_section_filter),
                      NULL);

    g_signal_connect (interface,
                      "handle-stop-section-filter",
                      G_CALLBACK (on_handle_cgmi_stop_section_filter),
                      NULL);

    g_signal_connect (interface,
                      "handle-start-user-data-filter",
                      G_CALLBACK (on_handle_cgmi_start_user_data_filter),
                      NULL);

    g_signal_connect (interface,
                      "handle-stop-user-data-filter",
                      G_CALLBACK (on_handle_cgmi_stop_user_data_filter),
                      NULL);

    g_signal_connect (interface,
                      "handle-get-num-pids",
                      G_CALLBACK (on_handle_cgmi_get_num_pids),
                      NULL);

    g_signal_connect (interface,
                      "handle-get-pid-info",
                      G_CALLBACK (on_handle_cgmi_get_pid_info),
                      NULL);

    g_signal_connect (interface,
                      "handle-set-pid-info",
                      G_CALLBACK (on_handle_cgmi_set_pid_info),
                      NULL);

    g_signal_connect (interface,
                      "handle-set-logging",
                      G_CALLBACK (on_handle_cgmi_set_logging),
                      NULL);

    g_signal_connect (interface,
                      "handle-get-timing-metrics-max-count",
                      G_CALLBACK (on_handle_cgmiDiags_get_timing_metrics_max_count),
                      NULL);

    g_signal_connect (interface,
                      "handle-get-timing-metrics",
                      G_CALLBACK (on_handle_cgmiDiags_get_timing_metrics),
                      NULL);

    g_signal_connect (interface,
                      "handle-reset-timing-metrics",
                      G_CALLBACK (on_handle_cgmiDiags_reset_timing_metrics),
                      NULL);
    
    g_signal_connect (interface,
                      "handle-get-tsb-slide",
                      G_CALLBACK (on_handle_cgmi_get_tsb_slide),
                      NULL);

    g_signal_connect (interface,
                      "handle-get-num-subtitle-languages",
                      G_CALLBACK (on_handle_cgmi_get_num_subtitle_languages),
                      NULL);

    g_signal_connect (interface,
                      "handle-get-subtitle-info",
                      G_CALLBACK (on_handle_cgmi_get_subtitle_info),
                      NULL);

    g_signal_connect (interface,
                      "handle-set-default-subtitle-lang",
                      G_CALLBACK (on_handle_cgmi_set_default_subtitle_lang),
                      NULL);

    g_signal_connect (interface,
                      "handle-create-filter",
                      G_CALLBACK (on_handle_cgmi_create_filter),
                      NULL);

    if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (interface),
                                           connection,
                                           "/org/cisco/cgmi",
                                           &error))
    {
        /* handle error ?*/
        CGMID_ERROR( "Failed in g_dbus_interface_skeleton_export.\n" );
    }
}


////////////////////////////////////////////////////////////////////////////////
// DBUS environment verification/setup
////////////////////////////////////////////////////////////////////////////////
#define DBUS_SESS_BUS_ADDR "DBUS_SESSION_BUS_ADDRESS"
#define DBUS_SESS_BUS_ADDR_FILE "/var/run/dbus/SessionBusAddress.txt"
#define DBUS_SESS_BUS_ADDR_MAX 1024

/**
 *  Verify the DBUS_SESSION_BUS_ADDRESS env variable is set.  If it is not set
 *  this function will attempt to set it by reading a vssrdk specific file.
 *
 *  returns:  0 on success (DBUS_SESSION_BUS_ADDRESS is set to something)
 */
static int verify_dbus_env()
{
    char dbus_addr_buffer[DBUS_SESS_BUS_ADDR_MAX];
    char *dbus_session_bus_address;
    FILE* fp;
    size_t readCount;

    dbus_session_bus_address = getenv(DBUS_SESS_BUS_ADDR);
    if( dbus_session_bus_address != NULL )
    {
        // A dbus session is set... return success.
        return 0;
    }

    g_print( "Warning: %s was not set in the env.  Looking for default.\n", DBUS_SESS_BUS_ADDR );
    
    fp = fopen("/var/run/dbus/SessionBusAddress.txt", "r");
    if( fp == NULL )
    {
        g_print( "Error: Failed to find %s in %s.\n", DBUS_SESS_BUS_ADDR, DBUS_SESS_BUS_ADDR_FILE );
        return -1;
    }

    readCount = fread(dbus_addr_buffer, 1, DBUS_SESS_BUS_ADDR_MAX, fp);
    if( readCount < 1 )
    {
        g_print( "Error: Failed to find %s in %s.\n", DBUS_SESS_BUS_ADDR, DBUS_SESS_BUS_ADDR_FILE );
        return -1;
    }
    dbus_addr_buffer[DBUS_SESS_BUS_ADDR_MAX-1] = '\0';

    g_print("Found %s == %s (%d)\n", DBUS_SESS_BUS_ADDR, dbus_addr_buffer, readCount);

    return setenv(DBUS_SESS_BUS_ADDR, dbus_addr_buffer, 1);
}

////////////////////////////////////////////////////////////////////////////////
// Usage
////////////////////////////////////////////////////////////////////////////////
void printUsage( int argc, char *argv[] )
{
    g_print("Usage: %s [-f]|\n", argv[0]);
    g_print("   -f -- Run in foreground.\n");
    g_print("\n");
}


////////////////////////////////////////////////////////////////////////////////
// Entry point for DBUS daemon
////////////////////////////////////////////////////////////////////////////////
int main( int argc, char *argv[] )
{
    int c = 0;
    GMainLoop *loop;
    guint id;

    /* Argument handling */
    opterr = 0;

    while ((c = getopt (argc, argv, "f")) != -1)
    {
        switch (c)
        {
        case 'f':
            gInForeground = TRUE;
            break;
        default:
            g_print( "Invalid option (%c).\n", c );
            printUsage(argc, argv);
            return -1;
        }
    }

    if( 0 != verify_dbus_env() )
    {
        CGMID_INFO( "Error: Failed to find DBUS_SESSION_BUS_ADDRESS in the environment.\n" );
        return -1;
    }

    /* Fork into background depending on args provided */
    if ( gInForeground == FALSE )
    {
        /* Setup syslog when in background */
        openlog( DAEMON_NAME, LOG_CONS | LOG_PID | LOG_NDELAY, LOG_DAEMON );
        g_set_print_handler(gPrintToSyslog);
        g_set_printerr_handler(gPrintErrToSyslog);


        g_print("Redirecting stdout/stderr to syslog.\n");
        redirectStdStreamsToSyslog();

        if ( 0 != daemon( 0, 0 ) )
        {
            CGMID_ERROR( "Failed to fork background daemon. Abort.\n" );
            return errno;
        }
    }

    /* Init Callback Globals */
    gUserDataCallbackHash = g_hash_table_new_full(g_direct_hash, 
        g_direct_equal,
        NULL,
        g_free);

    //rms put in to a #define diagInit (DIAGTYPE_DEFAULT, NULL, 0);
    /* DBUS Code */
    loop = g_main_loop_new( NULL, FALSE );

#if !GLIB_CHECK_VERSION(2,35,0)
    g_type_init();
#endif

    id = g_bus_own_name( G_BUS_TYPE_SESSION,
                         "org.cisco.cgmi",
                         G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                         G_BUS_NAME_OWNER_FLAGS_REPLACE,
                         on_bus_acquired,
                         on_name_acquired,
                         on_name_lost,
                         loop,
                         NULL );


    g_main_loop_run( loop );

    /* Call term for CGMI core when daemon is stopped. */
    cgmi_Term( );

    g_bus_unown_name( id );
    g_main_loop_unref( loop );

    /* Clean-up global callbacks */
    if ( gUserDataCallbackHash != NULL )
    {
        g_hash_table_destroy(gUserDataCallbackHash);
        gUserDataCallbackHash = NULL;
    }

    return 0;
}
