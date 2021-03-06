/*
    CGMI
    Copyright (C) {2015}  {Cisco System}

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
    USA

    Contributing Authors: Matt Snoby, Kris Kersey, Zack Wine, Chris Foster,
                          Tankut Akgul, Saravanakumar Periyaswamy

*/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <gst/gst.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>

#include "dbusPtrCommon.h"
#include "cgmiPlayerApi.h"
#include "cgmi_dbus_client_generated.h"
#include "cgmiDiagsApi.h"

////////////////////////////////////////////////////////////////////////////////
// Defines
////////////////////////////////////////////////////////////////////////////////
#define USER_DATA_CALLBACK_BUFFER_SIZE 2048

////////////////////////////////////////////////////////////////////////////////
// Macros
////////////////////////////////////////////////////////////////////////////////
#define dbus_check_error(error) \
    do{\
        if (error)\
        {   g_print("%s:%d: IPC failure: %s\n", __FUNCTION__, __LINE__, error->message);\
            g_error_free (error);\
            return  CGMI_ERROR_FAILED;\
        }\
    }while(0)

#define enforce_dbus_preconditions() \
    if ( gProxy == NULL ) \
    { \
        g_print("%s:%d - %s Error CGMI DBUS-IPC not connected.\n", \
                __FILE__, __LINE__, __FUNCTION__); \
        return CGMI_ERROR_WRONG_STATE; \
    }

#define enforce_session_preconditions(pSess) \
    if ( pSess == NULL || gPlayerEventCallbacks == NULL || \
        g_hash_table_lookup(gPlayerEventCallbacks, (gpointer)pSess) == NULL ) \
    { \
        g_print("%s:%d - %s Error invalid sessionId %lu.\n", \
                __FILE__, __LINE__, __FUNCTION__, (tCgmiDbusPointer)pSess); \
        return CGMI_ERROR_BAD_PARAM; \
    }


////////////////////////////////////////////////////////////////////////////////
// Typedefs
////////////////////////////////////////////////////////////////////////////////
typedef struct
{
    cgmi_EventCallback  callback;
    void                *userParam;
    gchar               *fifoName;
    gboolean            userDataCbRunning;
    pthread_t           userDataCbThread;
    userDataBufferCB    userDataCallback;
    userDataRawBufferCB userDataRawCallback;
    int                 userDataFifoDesc;
    void                *userDataPrivate;

} tcgmi_PlayerEventCallbackData;


typedef struct
{
    queryBufferCB bufferCB;
    sectionBufferCB sectionCB;
    void *pFilterPriv;
    void *pUserData;  // From session
    gboolean running;

} tcgmi_SectionFilterCbData;


////////////////////////////////////////////////////////////////////////////////
// Globals
////////////////////////////////////////////////////////////////////////////////
static gboolean         gClientInited             = FALSE;
static GMainLoop        *gLoop                    = NULL;
static GMainContext     *gMainContext             = NULL;
static OrgCiscoCgmi     *gProxy                   = NULL;
static GHashTable       *gPlayerEventCallbacks    = NULL;
static GHashTable       *gSectionFilterCbs        = NULL;
static cgmi_Status      gInitStatus               = CGMI_ERROR_NOT_INITIALIZED;
static GThread          *gMainLoopGthread;
static sem_t            gMainThreadStartSema;
static pthread_mutex_t  gEventCallbackMutex;


////////////////////////////////////////////////////////////////////////////////
// DBUS Callbacks
////////////////////////////////////////////////////////////////////////////////
static gboolean on_handle_notification (  OrgCiscoCgmi *proxy,
        GVariant *sessionHandle,
        gint event,
        gint data,
        guint64 code)
{
    tcgmi_PlayerEventCallbackData *playerCb = NULL;
    GVariant *sessVar = NULL;
    tCgmiDbusPointer pSess = 0;

    g_print("Enter on_handle_notification sessionHandle = %lu, event = %d code = %" G_GINT64_MODIFIER "d \n",
            (tCgmiDbusPointer)sessionHandle, event, code);

    // Preconditions
    if ( proxy != gProxy )
    {
        g_print("DBUS failure proxy doesn't match.\n");
        return TRUE;
    }

    pthread_mutex_lock(&gEventCallbackMutex);

    do
    {
        // Unmarshal session id pointer
        g_variant_get( sessionHandle, "v", &sessVar );
        if( sessVar == NULL )
        {
            break;
        }
        g_variant_get( sessVar, DBUS_POINTER_TYPE, &pSess );
        g_variant_unref( sessVar );

        // Find callback for this session
        if ( gPlayerEventCallbacks == NULL )
        {
            g_print("Internal error:  Callback hash table not initialized.\n");
            break;
        }
        playerCb = g_hash_table_lookup(gPlayerEventCallbacks, (gpointer)pSess);
        if ( playerCb == NULL || playerCb->callback == NULL )
        {
            g_print("Failed to find callback for sessionId (%lu) in hash table.\n",
                    pSess );

            break;
        }

        // Execute callback
        playerCb->callback( playerCb->userParam,
                            (void *)pSess,
                            (tcgmi_Event)event,
                            (uint64_t)code );

    }
    while (0);

    pthread_mutex_unlock(&gEventCallbackMutex);

    return TRUE;
}

static gboolean on_handle_section_buffer_notify (  OrgCiscoCgmi *proxy,
        GVariant *filterId,
        gint sectionStatus,
        GVariant *arg_section,
        gint sectionSize)
{
    tcgmi_SectionFilterCbData *filterCbs = NULL;
    char *retBuffer = NULL;
    int retBufferSize = sectionSize;
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    GVariantIter *iter = NULL;
    GVariant *filterIdVar = NULL;
    tCgmiDbusPointer pFilterId = 0;
    int bufIdx = 0;

    //g_print("Enter on_handle_section_buffer_notify filterId = 0x%08lx...\n", (void *)filterId);

    // Preconditions
    if ( proxy != gProxy )
    {
        g_print("DBUS failure proxy doesn't match.\n");
        return TRUE;
    }

    do
    {
        // Unmarshal filter id pointer
        g_variant_get( filterId, "v", &filterIdVar );
        if( filterIdVar == NULL )
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        g_variant_get( filterIdVar, DBUS_POINTER_TYPE, &pFilterId );
        g_variant_unref( filterIdVar );

        // Find callbacks
        filterCbs = g_hash_table_lookup( gSectionFilterCbs, (gpointer)pFilterId );
        if( NULL == filterCbs || NULL == filterCbs->bufferCB || NULL == filterCbs->sectionCB )
        {
            //g_print("Failed to find callback(s) for pFilterId (0x%08lx) in hash table.\n",
            //        (void *)pFilterId );
            break;
        }

        // Ignore tardy signals
        if( FALSE == filterCbs->running ) { break; }

        // Ask for a buffer from the app
        retStat = filterCbs->bufferCB( filterCbs->pUserData,
            filterCbs->pFilterPriv,
            (void *)pFilterId,
            &retBuffer,
            &retBufferSize );

        if( CGMI_ERROR_SUCCESS != retStat )
        {
            g_print("Failed to get buffer from app with error (%s)\n",
                    cgmi_ErrorString(retStat) );
            break;
        }
        if( retBuffer == NULL || retBufferSize < sectionSize )
        {
            g_print("Error:  The app failed to provide a valid section buffer\n");
            break;
        }

        // Unmarshal section buffer into app buffer
        g_variant_get( arg_section, "ay", &iter );
        if( NULL == iter )
        {
            g_print("Error:  Failed to get iterator from gvariant\n");
            break;
        }
        bufIdx = 0;
        while( bufIdx < sectionSize && g_variant_iter_loop(iter, "y", &retBuffer[bufIdx]) )
        {
            bufIdx++;
        }
        g_variant_iter_free( iter );

        // Send buffer
        retStat = filterCbs->sectionCB( filterCbs->pUserData,
            filterCbs->pFilterPriv,
            (void *)pFilterId,
            sectionStatus,
            retBuffer,
            sectionSize );

        if( CGMI_ERROR_SUCCESS != retStat )
        {
            g_print("Failed sending buffer to the app with error (%s)\n",
                    cgmi_ErrorString(retStat) );
            break;
        }

    }while(0);

    return TRUE;
}

/* This function will read exactly count bytes */
static int cgmi_fifoCompleteRead(int fd, void *buffer, size_t count)
{
    int bytesRead = 0;
    int totalRead = 0;
    do
    {
        bytesRead = read(fd, buffer + totalRead, count - totalRead);
        if( 0 >= bytesRead )
        {
            return bytesRead;
        }
        totalRead += bytesRead;
    } while (totalRead < count);
    return totalRead;
}

////////////////////////////////////////////////////////////////////////////////
// Threads for handling streams to app
////////////////////////////////////////////////////////////////////////////////

/* Used for user data (CC) callbacks. */
//TODO add documentation
//
static void *cgmi_UserDataCbThread(void *data)
{
    tcgmi_PlayerEventCallbackData *cbData = (tcgmi_PlayerEventCallbackData *)data;
    guint8 *dataBuf = NULL;
    int bytesRead = 0;
    GstBuffer *pGstBuff = NULL;
    struct timeval selectTimeout;
    fd_set selectFdSet;
    int readyFd;
    guint bufferSize = 0;

    // Preconditions
    if( NULL == cbData || ((NULL == cbData->userDataCallback) && (NULL == cbData->userDataRawCallback)) )
    {
        g_print("Bad parameter (NULL) sent to UserDataCbThread.\n");
        return NULL;
    }

    // Open fifo
    cbData->userDataFifoDesc = open(cbData->fifoName, O_RDONLY);
    if( -1 == cbData->userDataFifoDesc )
    {
        g_print("Failed to open fifo (%s)\n", cbData->fifoName);
        return NULL;
    }

    cbData->userDataCbRunning = TRUE;

    while( cbData->userDataCbRunning == TRUE )
    {

        // Init fd_set for select statement
        FD_ZERO(&selectFdSet);
        FD_SET(cbData->userDataFifoDesc, &selectFdSet);

        // Timeout in 1 second.  Note this must be reset each loop,
        // because select updates the struct to time left.
        selectTimeout.tv_sec = 1;
        selectTimeout.tv_usec = 0;

        // Wait for data with timeout to ensure this read thread exits gracefully
        readyFd = select( cbData->userDataFifoDesc + 1, &selectFdSet, NULL, NULL, &selectTimeout );

        // Handle select timeout
        if( readyFd == 0 ) { continue; }

        // Handle select error
        if( readyFd == -1 )
        {
            g_print("Failed to call select on pipe with error (%d).\n", errno);
            continue;
        }

        // Read size from fifo
        bytesRead = cgmi_fifoCompleteRead(
            cbData->userDataFifoDesc, &bufferSize, sizeof(bufferSize) );

        // Did we actually get some data?
        if( 0 >= bytesRead )
        {
            g_print("Failed to read size from UserDataCbThread fifo.\n");
            continue;
        }

        // Verify the blocksize
        if ( USER_DATA_CALLBACK_BUFFER_SIZE < bufferSize )
        {
            g_print("Block size %d is too big.\n", bufferSize);
            continue;
        }

        // Cannot reuse the previously unused buffer
        if( NULL != dataBuf )
        {
            g_free(dataBuf);
            dataBuf = NULL;
        }

        dataBuf = g_malloc0(bufferSize);
        if( NULL == dataBuf )
        {
            g_print("Failed to allocate memory.\n");
            break;
        }

        // Read the body
        bytesRead = cgmi_fifoCompleteRead(
            cbData->userDataFifoDesc, dataBuf, bufferSize );

        // If main thread has asked nicely to stop running, then bail
        if( cbData->userDataCbRunning == FALSE ) break;

        // Did we actually get some data?
        if( 0 >= bytesRead )
        {
            g_print("Failed to read body from UserDataCbThread fifo.\n");
            continue;
        }

        //g_print("Read (%d) bytes.\n", bytesRead );

        if( NULL != cbData->userDataCallback )
        {
            // Wrap data with GstBuffer
#if GST_CHECK_VERSION(1,0,0)
            pGstBuff = gst_buffer_new_wrapped (dataBuf, bytesRead);
            if( NULL == pGstBuff )
            {
                g_print("Failed to create new gst buffer.\n");
                continue;
            }
#else
            pGstBuff = gst_buffer_new( );
            if( NULL == pGstBuff )
            {
                g_print("Failed to create new gst buffer.\n");
                continue;
            }
            gst_buffer_set_data( pGstBuff, dataBuf, bytesRead );
#endif
            // Notify callback
            cbData->userDataCallback(cbData->userDataPrivate, (void *)pGstBuff);
        }
        else if( NULL != cbData->userDataRawCallback )
        {
            // Notify callback
            cbData->userDataRawCallback(cbData->userDataPrivate, dataBuf, bytesRead);
        }
        else
        {
            g_print("Failed to send data back to user!\n");
            g_free( dataBuf );
        }
        // Set dataBuf to NULL so we know not to reuse/free it.  Ownership has passed to the app.
        dataBuf = NULL;
    }

    // Clean up

    if( dataBuf != NULL ) { g_free( dataBuf ); }
    close(cbData->userDataFifoDesc);
    cbData->userDataCbRunning = FALSE;
    cbData->userDataCallback = NULL;
    cbData->userDataRawCallback = NULL;
    g_free(cbData->fifoName);
    g_print("Exiting thread.\n");

    return NULL;
}

////////////////////////////////////////////////////////////////////////////////
// DBUS client specific setup and tear down APIs
////////////////////////////////////////////////////////////////////////////////

static void cgmi_ResetClientState()
{
    /* Forget known sessions and callbacks */
    pthread_mutex_lock(&gEventCallbackMutex);
    if ( gPlayerEventCallbacks != NULL )
    {
        g_hash_table_destroy(gPlayerEventCallbacks);
        gPlayerEventCallbacks = NULL;
    }
    pthread_mutex_unlock(&gEventCallbackMutex);

    if ( gSectionFilterCbs != NULL )
    {
        g_hash_table_destroy(gSectionFilterCbs);
        gSectionFilterCbs = NULL;
    }

    /* Destroy the DBUS connection */
    if ( gProxy != NULL )
    {
        g_object_unref(gProxy);
        gProxy = NULL;
    }
}

/* Called whem the server aquires name on dbus */
static void on_name_appeared (GDBusConnection *connection,
                  const gchar     *name,
                  const gchar     *name_owner,
                  gpointer         user_data)
{
    GError *error = NULL;

    g_print ("Name %s on the session bus is owned by %s\n", name, name_owner);

    // Connect proxy to the server
    gProxy = org_cisco_cgmi_proxy_new_for_bus_sync( G_BUS_TYPE_SESSION,
             G_DBUS_PROXY_FLAGS_NONE, "org.cisco.cgmi", "/org/cisco/cgmi", NULL, &error );

    if (error)
    {
        g_print("Failed in dbus proxy call: %s\n", error->message);
        g_error_free(error);
        gProxy = NULL;
        gInitStatus = CGMI_ERROR_FAILED;
        return;
    }

    // Listen for player callbacks
    g_signal_connect( gProxy, "player-notify",
                      G_CALLBACK (on_handle_notification), NULL );

    // Listen for section filter callbacks
    g_signal_connect( gProxy, "section-buffer-notify",
                      G_CALLBACK (on_handle_section_buffer_notify), NULL );

    // Create hash tables for tracking sessions and callbacks.
    // NOTE:  Tell the hash table to free each value on removal via g_free
    gPlayerEventCallbacks = g_hash_table_new_full(g_direct_hash,
        g_direct_equal,
        NULL,
        g_free);

    gSectionFilterCbs = g_hash_table_new_full(g_direct_hash,
        g_direct_equal,
        NULL,
        g_free);

    // Call init when the server comes on the dbus.
    org_cisco_cgmi_call_init_sync( gProxy, (gint *)&gInitStatus, NULL, &error );

    if (error)
    {   g_print("%s,%d: Failed to init CGMI: %s\n", __FUNCTION__, __LINE__, error->message);
        g_error_free (error);
        return;
    }

    gInitStatus = CGMI_ERROR_SUCCESS;

    if( gClientInited == FALSE )
    {
        g_print("%s g_main_loop is running, and DBUS name has been found.\n", __FUNCTION__);
        sem_post(&gMainThreadStartSema);
    }
}

/* Called whem the server losses name on dbus */
static void on_name_vanished (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
    g_print ("Name %s does not exist on the session bus\n", name);

    cgmi_ResetClientState();
}


/* This is spawned as a thread and starts the glib main loop for dbus. */
static gpointer cgmi_DbusMainLoopGthread(gpointer data)
{
    guint watcher_id;

#if !GLIB_CHECK_VERSION(2,35,0)
    g_type_init();
#endif

    gMainContext = g_main_context_new();
    g_main_context_push_thread_default(gMainContext);

    gLoop = g_main_loop_new (gMainContext, FALSE);
    if ( gLoop == NULL )
    {
        g_print("Error creating a new main_loop\n");
        return NULL;
    }

    // Add callbacks for when the dbus server goes up/down
    watcher_id = g_bus_watch_name ( G_BUS_TYPE_SESSION,
                                 "org.cisco.cgmi",
                                 G_BUS_NAME_WATCHER_FLAGS_NONE,
                                 on_name_appeared,
                                 on_name_vanished,
                                 NULL,
                                 NULL);

    g_main_loop_run( gLoop );

    g_bus_unwatch_name( watcher_id );

    return NULL;
}

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
    char *newline = NULL;

    dbus_session_bus_address = (char *)getenv(DBUS_SESS_BUS_ADDR);
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
    if( readCount >= DBUS_SESS_BUS_ADDR_MAX )
    {
        g_print( "Error: Buffer too small.  Failed to find %s in %s.\n", DBUS_SESS_BUS_ADDR, DBUS_SESS_BUS_ADDR_FILE );
        return -1;
    }

    // Terminate the string, and replace the first newline with a null byte.
    dbus_addr_buffer[readCount] = '\0';
    newline = strchr(dbus_addr_buffer, '\n');
    if( newline != NULL ) { *newline = '\0'; }

    g_print("Found %s == %s (%d)\n", DBUS_SESS_BUS_ADDR, dbus_addr_buffer, (gint)readCount);

    return setenv(DBUS_SESS_BUS_ADDR, dbus_addr_buffer, 1);
}

/**
 *  Needs to be called prior to any DBUS APIs (called by CGMI Init)
 */
cgmi_Status cgmi_DbusInterfaceInit()
{
    struct timespec ts;
    int semaResult;

    if ( gClientInited == TRUE )
    {
        g_print("Dbus proxy is already open.\n");
        return CGMI_ERROR_WRONG_STATE;
    }

    sem_init(&gMainThreadStartSema, 0, 0);

    do{

        if( 0 != verify_dbus_env() )
        {
            gInitStatus = CGMI_ERROR_FAILED;
            break;
        }

        gInitStatus = CGMI_ERROR_NOT_INITIALIZED;
        gMainLoopGthread = g_thread_new("cgmi-client", cgmi_DbusMainLoopGthread, NULL);
        if( gMainLoopGthread == NULL )
        {
            g_print("Error launching thread for gmainloop\n");
            gInitStatus = CGMI_ERROR_FAILED;
            break;
        }

        if (clock_gettime(CLOCK_REALTIME, &ts) == -1)
        {
            g_print("Error setting timeout for sem_timedwait()\n");
            gInitStatus = CGMI_ERROR_FAILED;
            break;
        }

        // Timeout in 5 seconds
        ts.tv_sec += 5;

        while ((semaResult = sem_timedwait(&gMainThreadStartSema, &ts)) == -1 &&
            errno == EINTR)
        {
            continue;       /* If semaphore is interupted, then restart */
        }

        if( semaResult == -1 )
        {
            g_print("Error failed to connect DBUS, or g_main_loop failed to run?.\n");

            if( gLoop != NULL )
            {
                if( g_main_loop_is_running(gLoop) )
                {
                    g_print("Main loop says it is running, but it failed to fire DBUS callback.\n");
                    g_main_loop_quit( gLoop );
                    g_thread_join(gMainLoopGthread);
                }
                g_main_loop_unref( gLoop );
                gLoop = NULL;
            }

            gInitStatus = CGMI_ERROR_FAILED;
            break;
        }

    }while(0);

    // Set init flag if success
    if( gInitStatus == CGMI_ERROR_SUCCESS )
    {
        gClientInited = TRUE;
    }

    // Clean up semaphore
    sem_destroy(&gMainThreadStartSema);

    return gInitStatus;
}

/**
 *  Called to clean up prior to shutdown (called by GCMI Term)
 */
void cgmi_DbusInterfaceTerm()
{
    if ( gClientInited == FALSE ) return;

    // Reset the state of client session IDs and callbacks.
    cgmi_ResetClientState();

    // Kill the thread/loop utilzied by dbus
    if( gLoop != NULL ) {
        g_main_loop_quit( gLoop );
        g_thread_join(gMainLoopGthread);
        g_main_loop_unref( gLoop );
    }

    gClientInited = FALSE;
}


////////////////////////////////////////////////////////////////////////////////
// CGMI DBUS client APIs
////////////////////////////////////////////////////////////////////////////////
cgmi_Status cgmi_Init (void)
{
    cgmi_Status   retStat = CGMI_ERROR_SUCCESS;
    int argc =0;
    char **argv = NULL;
    GError   *error      = NULL;

    do{
        /* Initialize gstreamer */
        if( !gst_init_check( &argc, &argv, &error ) )
        {
            g_critical("Failed to initialize gstreamer :%s\n", error->message);
            retStat = CGMI_ERROR_NOT_INITIALIZED;
            break;
        }

        /* Verify threading system in up */
        if( !g_thread_supported() )
        {
            g_critical("GLib Thread system not initialized\n");
            retStat = CGMI_ERROR_NOT_SUPPORTED;
            break;
        }

        retStat = cgmi_DbusInterfaceInit();

    }while(0);

    return retStat;
}

cgmi_Status cgmi_Term (void)
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;

    enforce_dbus_preconditions();

    org_cisco_cgmi_call_term_sync( gProxy, (gint *)&retStat, NULL, &error );

    dbus_check_error(error);

    cgmi_DbusInterfaceTerm();

    return retStat;
}

char* cgmi_ErrorString ( cgmi_Status retStat )
{
    gchar *statusString = NULL;
    GError *error = NULL;

    if ( gProxy == NULL )
    {
        g_print("Error CGMI not initialized.\n");
        return NULL;
    }

    org_cisco_cgmi_call_error_string_sync( gProxy, retStat, &statusString, NULL, &error );

    if(error)
    {
        g_print("%s,%d: Failed in the client call: %s\n", __FUNCTION__, __LINE__, error->message);
        g_error_free (error);
        return  NULL;
    }

    return statusString;
}

cgmi_Status cgmi_CreateSession ( cgmi_EventCallback eventCB,
                                 void *pUserData,
                                 void **pSession )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    tCgmiDbusPointer sessionId;
    GVariant *sessVar = NULL, *dbusVar = NULL;
    tcgmi_PlayerEventCallbackData *eventCbData = NULL;


    // Preconditions
    if( pSession == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_dbus_preconditions();

    // This mutex ensures callbacks aren't handled while the session is still
    // being created.  Primarily protecting the event callback hash table.
    pthread_mutex_lock(&gEventCallbackMutex);

    do
    {

        org_cisco_cgmi_call_create_session_sync( gProxy,
                &dbusVar,
                (gint *)&retStat,
                NULL,
                &error );

        if (error)
        {
            g_print("%s,%d: Failed in the client call: %s\n", __FUNCTION__, __LINE__,
                    error->message);
            g_error_free (error);
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        g_variant_get( dbusVar, "v", &sessVar );
        if( sessVar == NULL )
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }
        g_variant_get( sessVar, DBUS_POINTER_TYPE, &sessionId );
        g_variant_unref( sessVar );
        g_variant_unref( dbusVar );

        *pSession = (void *)sessionId;

        eventCbData = g_malloc0(sizeof(tcgmi_PlayerEventCallbackData));
        if (eventCbData == NULL)
        {
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }

        eventCbData->callback = eventCB;
        eventCbData->userParam = pUserData;
        eventCbData->userDataCbRunning = FALSE;
        eventCbData->userDataCallback = NULL;
        eventCbData->userDataRawCallback = NULL;
        eventCbData->userDataPrivate = NULL;

        if ( gPlayerEventCallbacks == NULL )
        {
            g_print("Internal error:  Callback hash table not initialized.\n");
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        g_hash_table_insert( gPlayerEventCallbacks, (gpointer)sessionId,
                             (gpointer)eventCbData );

    }
    while (0);

    pthread_mutex_unlock(&gEventCallbackMutex);

    return retStat;
}

cgmi_Status cgmi_DestroySession( void *pSession )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    GVariant *sessVar = NULL, *dbusVar = NULL;

    // Preconditions
    if( pSession == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        dbusVar = g_variant_new ( "v", sessVar );
        if( dbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

        org_cisco_cgmi_call_destroy_session_sync( gProxy,
                dbusVar,
                (gint *)&retStat,
                NULL,
                &error );

    }while(0);

    //Clean up
    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    // Error check DBUS, this macro may return
    dbus_check_error(error);

    // Sync operations on the event callback hash
    pthread_mutex_lock(&gEventCallbackMutex);

    if ( gPlayerEventCallbacks != NULL )
    {
        g_hash_table_remove( gPlayerEventCallbacks, GINT_TO_POINTER((tCgmiDbusPointer)pSession) );
    }

    pthread_mutex_unlock(&gEventCallbackMutex);

    return retStat;
}

cgmi_Status cgmi_canPlayType( const char *type, int *pbCanPlay )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;

    // Preconditions
    if( type == NULL || pbCanPlay == NULL)
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_dbus_preconditions();

    org_cisco_cgmi_call_can_play_type_sync( gProxy,
                                            (const gchar *)type,
                                            (gint *)pbCanPlay,
                                            (gint *)&retStat,
                                            NULL,
                                            &error );

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_Load( void *pSession, const char *uri, cpBlobStruct * cpblob, const char *sessionSettings)
{
   cgmi_Status     retStat = CGMI_ERROR_SUCCESS;
   GVariantBuilder *dataBuilder = NULL;
   GVariant        *cpBlobStruct_Variant = NULL;
   uint32_t        ii = 0;
   gchar           byte;
   guint64         cpBlobStruct_Variant_Size=0;
   GError          *error = NULL;
   GVariant        *sessVar = NULL, *dbusVar = NULL;

   // Preconditions
   if( pSession == NULL || uri == NULL)
   {
      return CGMI_ERROR_BAD_PARAM;
   }

   enforce_session_preconditions(pSession);

   enforce_dbus_preconditions();

   do{
      sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
      if( sessVar == NULL )
      {
         g_print("Failed to create new variant\n");
         retStat = CGMI_ERROR_OUT_OF_MEMORY;
         break;
      }
      sessVar = g_variant_ref_sink(sessVar);

      dbusVar = g_variant_new ( "v", sessVar );
      if( dbusVar == NULL )
      {
         g_print("Failed to create new variant\n");
         retStat = CGMI_ERROR_OUT_OF_MEMORY;
         break;
      }
      dbusVar = g_variant_ref_sink(dbusVar);

      dataBuilder = g_variant_builder_new(G_VARIANT_TYPE("ay"));
      if(NULL == dataBuilder)
      {
         g_print("Failed to create Variant builder\n");
         retStat = CGMI_ERROR_OUT_OF_MEMORY;
         break;
      }

      if (cpblob)
      {
         for(ii = 0; ii < sizeof(cpBlobStruct); ii++)
         {
            byte = ((gchar *)cpblob)[ii];
            g_variant_builder_add(dataBuilder, "y", byte);
         }
         cpBlobStruct_Variant_Size = sizeof(cpBlobStruct);
      }


      cpBlobStruct_Variant = g_variant_builder_end(dataBuilder);

      org_cisco_cgmi_call_load_sync( gProxy,
         dbusVar,
         (const gchar *)uri,
         cpBlobStruct_Variant,
         cpBlobStruct_Variant_Size,
         (sessionSettings != NULL)?sessionSettings:"",
         (gint *)&retStat,
         NULL,
         &error );

   }while(0);

   //Clean up
   if(NULL != dataBuilder)
   {
      g_variant_builder_unref(dataBuilder);
   }

   if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
   if( sessVar != NULL ) { g_variant_unref(sessVar); }

   dbus_check_error(error);

   return retStat;
}

cgmi_Status cgmi_Unload( void *pSession )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    GVariant *sessVar = NULL, *dbusVar = NULL;

    // Preconditions
    if( pSession == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        dbusVar = g_variant_new ( "v", sessVar );
        if( dbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

        org_cisco_cgmi_call_unload_sync( gProxy,
                                         dbusVar,
                                         (gint *)&retStat,
                                         NULL,
                                         &error );

    }while(0);

    //Clean up
    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_Play( void *pSession, int autoPlay )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    GVariant *sessVar = NULL, *dbusVar = NULL;

    // Preconditions
    if( pSession == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        dbusVar = g_variant_new ( "v", sessVar );
        if( dbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

        org_cisco_cgmi_call_play_sync( gProxy,
                                       dbusVar,
                                       (gint)autoPlay,
                                       (gint *)&retStat,
                                       NULL,
                                       &error );

    }while(0);

    //Clean up
    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_SetRate( void *pSession, float rate )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    GVariant *sessVar = NULL, *dbusVar = NULL;

    // Preconditions
    if( pSession == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        dbusVar = g_variant_new ( "v", sessVar );
        if( dbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

        org_cisco_cgmi_call_set_rate_sync( gProxy,
                                           dbusVar,
                                           (gdouble)rate,
                                           (gint *)&retStat,
                                           NULL,
                                           &error );

    }while(0);

    //Clean up
    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_SetPosition( void *pSession, float position )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    GVariant *sessVar = NULL, *dbusVar = NULL;

    // Preconditions
    if( pSession == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        dbusVar = g_variant_new ( "v", sessVar );
        if( dbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

        org_cisco_cgmi_call_set_position_sync( gProxy,
                                               dbusVar,
                                               (gdouble)position,
                                               (gint *)&retStat,
                                               NULL,
                                               &error );

    }while(0);

    //Clean up
    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_GetPosition( void *pSession, float *pPosition )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    gdouble localPosition = 0;
    GVariant *sessVar = NULL, *dbusVar = NULL;

    // Preconditions
    if( pSession == NULL || pPosition == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        dbusVar = g_variant_new ( "v", sessVar );
        if( dbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

        org_cisco_cgmi_call_get_position_sync( gProxy,
                                               dbusVar,
                                               &localPosition,
                                               (gint *)&retStat,
                                               NULL,
                                               &error );

    }while(0);

    //Clean up
    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    dbus_check_error(error);

    *pPosition = (float)localPosition;

    return retStat;
}

cgmi_Status cgmi_GetDuration( void *pSession,  float *pDuration,
                              cgmi_SessionType *type )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    gdouble localDuration = 0;
    GVariant *sessVar = NULL, *dbusVar = NULL;

    // Preconditions
    if( pSession == NULL || pDuration == NULL || type == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        dbusVar = g_variant_new ( "v", sessVar );
        if( dbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

        org_cisco_cgmi_call_get_duration_sync( gProxy,
                                               dbusVar,
                                               &localDuration,
                                               (gint *)type,
                                               (gint *)&retStat,
                                               NULL,
                                               &error );


    }while(0);

    //Clean up
    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    dbus_check_error(error);

    //Dbus doesn't have a float type (just double), hence this funny business
    *pDuration = (float)localDuration;

    return retStat;
}

cgmi_Status cgmi_GetRates (void *pSession,  float pRates[],  unsigned int *pNumRates)
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    GVariant *sessVar = NULL, *dbusVar = NULL;
    GVariant *pOutRates = NULL;
    gdouble rate = 0.0;
    GVariantIter *iter = NULL;
    unsigned int inNumRates = 0;

    // Preconditions
    if( pSession == NULL || pRates == NULL || pNumRates == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }
    if(*pNumRates == 0)
    {
       g_print("CGMI_CLIENT: *pNumRates is 0 which is invalid\n");
       return CGMI_ERROR_BAD_PARAM;
    }
    inNumRates = *pNumRates;
    //g_print("CGMI_CLIENT: inNumRates = %u\n", inNumRates);

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("CGMI_CLIENT: Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        dbusVar = g_variant_new ( "v", sessVar );
        if( dbusVar == NULL )
        {
            g_print("CGMI_CLIENT: Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

        org_cisco_cgmi_call_get_rates_sync( gProxy,
                dbusVar,
                inNumRates,
                &pOutRates,
                (gint *)&retStat,
                NULL,
                &error );

    }while(0);

    //Clean up
    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    dbus_check_error(error);

    g_variant_get(pOutRates, "ad", &iter);
    if( NULL != iter )
    {
       *pNumRates = 0;
       while((*pNumRates < inNumRates) && (g_variant_iter_loop(iter, "d", &rate)))
       {
          pRates[(*pNumRates)++] = rate;
          //g_print("CGMI_CLIENT: pRates[%u] = %f\n", *pNumRates - 1, pRates[*pNumRates - 1]);
       }
       g_variant_iter_free(iter);
       //g_print("CGMI_CLIENT: Number of rates = %u\n", *pNumRates);
    }
    else
    {
       g_print("CGMI_CLIENT: Getting iter by calling g_variant_get(pOutRates) failed\n");
    }

    return retStat;
}

cgmi_Status cgmi_SetVideoRectangle( void *pSession, int srcx, int srcy, int srcw, int srch, int dstx, int dsty, int dstw, int dsth )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    GVariant *sessVar = NULL, *dbusVar = NULL;

    // Preconditions
    if( pSession == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        dbusVar = g_variant_new ( "v", sessVar );
        if( dbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

        org_cisco_cgmi_call_set_video_rectangle_sync( gProxy,
                dbusVar,
                srcx,
                srcy,
                srcw,
                srch,
                dstx,
                dsty,
                dstw,
                dsth,
                (gint *)&retStat,
                NULL,
                &error );

    }while(0);

    //Clean up
    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_GetVideoResolution( void *pSession, int *srcw, int *srch )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    GVariant *sessVar = NULL, *dbusVar = NULL;

    // Preconditions
    if( pSession == NULL || srcw == NULL || srch == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        dbusVar = g_variant_new ( "v", sessVar );
        if( dbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

        org_cisco_cgmi_call_get_video_resolution_sync( gProxy,
                dbusVar,
                srcw,
                srch,
                (gint *)&retStat,
                NULL,
                &error );

    }while(0);

    //Clean up
    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_GetVideoDecoderIndex( void *pSession, int *idx )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    GVariant *sessVar = NULL, *dbusVar = NULL;

    // Preconditions
    if( pSession == NULL || idx == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        dbusVar = g_variant_new ( "v", sessVar );
        if( dbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

        org_cisco_cgmi_call_get_video_decoder_index_sync( gProxy,
                dbusVar,
                idx,
                (gint *)&retStat,
                NULL,
                &error );

    }while(0);

    //Clean up
    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_GetNumAudioLanguages( void *pSession, int *count )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    GVariant *sessVar = NULL, *dbusVar = NULL;

    // Preconditions
    if( pSession == NULL || count == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        dbusVar = g_variant_new ( "v", sessVar );
        if( dbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

        org_cisco_cgmi_call_get_num_audio_languages_sync( gProxy,
                dbusVar,
                count,
                (gint *)&retStat,
                NULL,
                &error );

    }while(0);

    //Clean up
    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_GetAudioLangInfo( void *pSession, int index,
                                   char *buf, int bufSize, char *isEnabled )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    gchar *buffer = NULL;
    GVariant *sessVar = NULL, *dbusVar = NULL;

    // Preconditions
    if( NULL == pSession || NULL == buf )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        dbusVar = g_variant_new ( "v", sessVar );
        if( dbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

        org_cisco_cgmi_call_get_audio_lang_info_sync( gProxy,
                dbusVar,
                index,
                bufSize,
                (gchar **)&buffer,
                (gboolean *)isEnabled,
                (gint *)&retStat,
                NULL,
                &error );

    }while(0);

    //Clean up
    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    if( NULL == buffer )
    {
        return CGMI_ERROR_FAILED;
    }

    strncpy( buf, buffer, bufSize );
    buf[bufSize - 1] = 0;
    g_free( buffer );

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_SetAudioStream( void *pSession, int index )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    GVariant *sessVar = NULL, *dbusVar = NULL;

    // Preconditions
    if( pSession == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        dbusVar = g_variant_new ( "v", sessVar );
        if( dbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

        org_cisco_cgmi_call_set_audio_stream_sync( gProxy,
                dbusVar,
                index,
                (gint *)&retStat,
                NULL,
                &error );

    }while(0);

    //Clean up
    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_SetDefaultAudioLang( void *pSession, const char *language )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    GVariant *sessVar = NULL, *dbusVar = NULL;

    // Preconditions
    if( pSession == NULL || language == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        dbusVar = g_variant_new ( "v", sessVar );
        if( dbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

        org_cisco_cgmi_call_set_default_audio_lang_sync( gProxy,
                dbusVar,
                language,
                (gint *)&retStat,
                NULL,
                &error );

    }while(0);

    //Clean up
    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_GetNumClosedCaptionServices( void *pSession, int *count )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    GVariant *sessVar = NULL, *dbusVar = NULL;

    // Preconditions
    if( pSession == NULL || count == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        dbusVar = g_variant_new ( "v", sessVar );
        if( dbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

        org_cisco_cgmi_call_get_num_closed_caption_services_sync( gProxy,
                dbusVar,
                count,
                (gint *)&retStat,
                NULL,
                &error );

    }while(0);

    //Clean up
    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_GetClosedCaptionServiceInfo( void *pSession, int index,
                                   char *isoCode, int isoCodeSize, int *serviceNum, char *isDigital )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    gchar *buffer = NULL;
    GVariant *sessVar = NULL, *dbusVar = NULL;

    // Preconditions
    if( NULL == pSession || NULL == isoCode )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        dbusVar = g_variant_new ( "v", sessVar );
        if( dbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

        org_cisco_cgmi_call_get_closed_caption_service_info_sync( gProxy,
                dbusVar,
                index,
                isoCodeSize,
                (gchar **)&buffer,
                (gint *)serviceNum,
                (gboolean *)isDigital,
                (gint *)&retStat,
                NULL,
                &error );

    }while(0);

    //Clean up
    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    if( NULL == buffer )
    {
        return CGMI_ERROR_FAILED;
    }

    strncpy( isoCode, buffer, isoCodeSize );
    isoCode[isoCodeSize - 1] = 0;
    g_free( buffer );

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_CreateSectionFilter( void *pSession, int pid, void *pFilterPriv, void **pFilterId )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    tcgmi_SectionFilterCbData *sectionFilterData;
    tcgmi_PlayerEventCallbackData *cbData;
    GVariant *sessVar = NULL, *sessDbusVar = NULL;
    GVariant *filterIdVar = NULL, *filterDbusVar = NULL;
    tCgmiDbusPointer filterIdPtr;

    // Preconditions
    if( pSession == NULL || pFilterId == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        sessDbusVar = g_variant_new ( "v", sessVar );
        if( sessDbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessDbusVar = g_variant_ref_sink(sessDbusVar);

        org_cisco_cgmi_call_create_section_filter_sync( gProxy,
                sessDbusVar,
                pid,
                &filterDbusVar,
                (gint *)&retStat,
                NULL,
                &error );

        if (error)
        {   g_print("%s:%d: IPC failure: %s\n", __FUNCTION__, __LINE__, error->message);
            g_error_free (error);
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        g_variant_get( filterDbusVar, "v", &filterIdVar );
        if( filterIdVar == NULL )
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }
        g_variant_get( filterIdVar, DBUS_POINTER_TYPE, &filterIdPtr );
        g_variant_unref( filterIdVar );
        g_variant_unref( filterDbusVar );

        *pFilterId = (void *)filterIdPtr;

    }while(0);

    //Clean up
    if( sessDbusVar != NULL ) { g_variant_unref(sessDbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }


    dbus_check_error(error);

    //TODO:  Remove debug
    g_print("Created filter ID 0x%08lx\n", filterIdPtr);

    do
    {
        if ( gSectionFilterCbs == NULL )
        {
            g_print("Internal error:  Callback hash table not initialized.\n");
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        sectionFilterData = g_malloc0(sizeof(tcgmi_SectionFilterCbData));
        if (sectionFilterData == NULL)
        {
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }

        cbData = g_hash_table_lookup(gPlayerEventCallbacks, (gpointer)pSession);
        if (cbData == NULL)
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        sectionFilterData->pFilterPriv = pFilterPriv;
        sectionFilterData->pUserData = cbData->userParam;
        sectionFilterData->running = FALSE;

        g_hash_table_insert( gSectionFilterCbs, (gpointer)*pFilterId,
                             (gpointer)sectionFilterData );

        if( NULL == g_hash_table_lookup( gSectionFilterCbs,
            (gpointer)*pFilterId ) )
        {
            g_print("Can't find the new filter in the hash!!!\n");
        }

    }while(0);

    return retStat;
}

cgmi_Status cgmi_DestroySectionFilter(void *pSession, void *pFilterId )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    GVariant *sessVar = NULL, *dbusVar = NULL;
    GVariant *filterIdVar = NULL, *filterDbusVar = NULL;

    // Preconditions
    if( pSession == NULL || pFilterId == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        dbusVar = g_variant_new ( "v", sessVar );
        if( dbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

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

        org_cisco_cgmi_call_destroy_section_filter_sync( gProxy,
                dbusVar,
                filterDbusVar,
                (gint *)&retStat,
                NULL,
                &error );

    }while(0);

    //Clean up
    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }
    if( filterDbusVar != NULL ) { g_variant_unref(filterDbusVar); }
    if( filterIdVar != NULL ) { g_variant_unref(filterIdVar); }

    if ( gSectionFilterCbs != NULL )
    {
        // let the hash table free the tcgmi_SectionFilterCbData instance
        // TODO:  Is this casting required?
        g_hash_table_remove( gSectionFilterCbs, GINT_TO_POINTER((tCgmiDbusPointer)pFilterId) );
    }

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_SetSectionFilter(void *pSession, void *pFilterId, tcgmi_FilterData *pFilter )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    GVariant *sessVar = NULL, *sessDbusVar = NULL;
    GVariant *filterIdVar = NULL, *filterDbusVar = NULL;
    GVariantBuilder *valueBuilder = NULL;
    GVariantBuilder *maskBuilder = NULL;
    GVariant *value;
    GVariant *mask;
    int idx;

    // Preconditions
    if( pSession == NULL || pFilterId == NULL || pFilter == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        // Marshal the value and mask GVariants
        valueBuilder = g_variant_builder_new( G_VARIANT_TYPE("ay") );
        maskBuilder = g_variant_builder_new( G_VARIANT_TYPE("ay") );
        if( valueBuilder == NULL || maskBuilder == NULL )
        {
            g_print("Failed to create new variant builder\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        for( idx = 0; idx < pFilter->length; idx++ )
        {
            g_variant_builder_add( valueBuilder, "y", pFilter->value[idx] );
            g_variant_builder_add( maskBuilder, "y", pFilter->mask[idx] );
        }
        value = g_variant_builder_end( valueBuilder );
        mask = g_variant_builder_end( maskBuilder );
        if( value == NULL || mask == NULL )
        {
            g_print("Failed to create new variant builder\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }

        // Marshal id pointers
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        sessDbusVar = g_variant_new ( "v", sessVar );
        if( sessDbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessDbusVar = g_variant_ref_sink(sessDbusVar);

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

        // Make dbus call
        org_cisco_cgmi_call_set_section_filter_sync( gProxy,
                sessDbusVar,
                filterDbusVar,
                value,
                mask,
                (gint)pFilter->length,
                (gint)pFilter->comparitor,
                (gint *)&retStat,
                NULL,
                &error );

    }while(0);

    //Clean up
    if( sessDbusVar != NULL ) { g_variant_unref(sessDbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }
    if( filterDbusVar != NULL ) { g_variant_unref(filterDbusVar); }
    if( filterIdVar != NULL ) { g_variant_unref(filterIdVar); }
    if( valueBuilder != NULL) { g_variant_builder_unref( valueBuilder ); }
    if( maskBuilder != NULL) {  g_variant_builder_unref( maskBuilder ); }

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_StartSectionFilter(void *pSession,
                                    void *pFilterId,
                                    int timeout,
                                    int bOneShot,
                                    int bEnableCRC,
                                    queryBufferCB bufferCB,
                                    sectionBufferCB sectionCB )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    tcgmi_SectionFilterCbData *filterCb;
    GVariant *sessVar = NULL, *sessDbusVar = NULL;
    GVariant *filterIdVar = NULL, *filterDbusVar = NULL;

    // Preconditions
    if( pSession == NULL || pFilterId == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    if( NULL == gSectionFilterCbs )
    {
        g_print("NULL gSectionFilterCbs.  Invalid call sequence?\n");
        return CGMI_ERROR_NOT_INITIALIZED;
    }

    // Find section filter callback instance
    filterCb = g_hash_table_lookup( gSectionFilterCbs,
        (gpointer)pFilterId );
    if( NULL == filterCb )
    {
        g_print("Unable to find filterCb instance.  Invalid pFilterId.\n");
        return CGMI_ERROR_INVALID_HANDLE;
    }

    // Save/track client callbacks
    filterCb->bufferCB = bufferCB;
    filterCb->sectionCB = sectionCB;
    filterCb->running = TRUE;

    do{
        // Marshal id pointers
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        sessDbusVar = g_variant_new ( "v", sessVar );
        if( sessDbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessDbusVar = g_variant_ref_sink(sessDbusVar);

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

        // Call DBUS
        org_cisco_cgmi_call_start_section_filter_sync( gProxy,
                sessDbusVar,
                filterDbusVar,
                timeout,
                bOneShot,
                bEnableCRC,
                (gint *)&retStat,
                NULL,
                &error );

    }while(0);

    //Clean up
    if( sessDbusVar != NULL ) { g_variant_unref(sessDbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }
    if( filterDbusVar != NULL ) { g_variant_unref(filterDbusVar); }
    if( filterIdVar != NULL ) { g_variant_unref(filterIdVar); }

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_StopSectionFilter(void *pSession, void *pFilterId )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    tcgmi_SectionFilterCbData *filterCb;
    GVariant *sessVar = NULL, *sessDbusVar = NULL;
    GVariant *filterIdVar = NULL, *filterDbusVar = NULL;

    // Preconditions
    if( pSession == NULL || pFilterId == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    // Find section filter callback instance
    filterCb = g_hash_table_lookup( gSectionFilterCbs,
        (gpointer)pFilterId );
    if( NULL == filterCb )
    {
        g_print("Unable to find filterCb instance.  Invalid pFilterId.\n");
        return CGMI_ERROR_INVALID_HANDLE;
    }

    // Flag that we have stopped this filter to ignore tardy callbacks
    filterCb->running = FALSE;

    do{
        // Marshal id pointers
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        sessDbusVar = g_variant_new ( "v", sessVar );
        if( sessDbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessDbusVar = g_variant_ref_sink(sessDbusVar);

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

        // Call DBUS
        org_cisco_cgmi_call_stop_section_filter_sync( gProxy,
                sessDbusVar,
                filterDbusVar,
                (gint *)&retStat,
                NULL,
                &error );

    }while(0);

    //Clean up
    if( sessDbusVar != NULL ) { g_variant_unref(sessDbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }
    if( filterDbusVar != NULL ) { g_variant_unref(filterDbusVar); }
    if( filterIdVar != NULL ) { g_variant_unref(filterIdVar); }

    dbus_check_error(error);

    return retStat;
}

cgmi_Status _cgmi_startUserDataFilterHelper(void *pSession, userDataBufferCB bufferCB, void *pUserData, userDataRawBufferCB rawBufferCB)
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    GVariant *sessVar = NULL, *dbusVar = NULL;
    gchar *fifoName = NULL;
    tcgmi_PlayerEventCallbackData *cbData;

    // Preconditions; only expect user to have 1 callback
    if( pSession == NULL || ((bufferCB == NULL) && (rawBufferCB == NULL)) || ((bufferCB != NULL) && (rawBufferCB != NULL)))
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        dbusVar = g_variant_new ( "v", sessVar );
        if( dbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

        org_cisco_cgmi_call_start_user_data_filter_sync( gProxy,
                                       dbusVar,
                                       &fifoName,
                                       (gint *)&retStat,
                                       NULL,
                                       &error );

        if( NULL == fifoName )
        {
            g_print("Failed to get a fifo name from DBUS.\n");
            retStat = CGMI_ERROR_BAD_PARAM;
            break;
        }

        if( CGMI_ERROR_SUCCESS != retStat )
        {
            g_print("Failed to start user data filter on daemon (%d).\n", retStat);
            break;
        }

        cbData = g_hash_table_lookup(gPlayerEventCallbacks, (gpointer)pSession);
        if (cbData == NULL)
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        cbData->userDataCallback = bufferCB;
        cbData->userDataRawCallback = rawBufferCB;
        cbData->userDataPrivate = pUserData;
        cbData->fifoName = fifoName;

        // spawn thread to read from fifo
        if ( 0 != pthread_create(&cbData->userDataCbThread, NULL, cgmi_UserDataCbThread, cbData) )
        {
            g_print("Error launching thread for UserDataCbThread\n");
            retStat = CGMI_ERROR_FAILED;
            break;
        }

    }while(0);

    //Clean up
    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_startUserDataFilter(void *pSession, userDataBufferCB bufferCB, void *pUserData)
{
    return _cgmi_startUserDataFilterHelper(pSession, bufferCB, pUserData, NULL);
}

cgmi_Status cgmi_startRawUserDataFilter (void *pSession, userDataRawBufferCB bufferCB, void *pUserData)
{
    return _cgmi_startUserDataFilterHelper(pSession, NULL, pUserData, bufferCB);
}

cgmi_Status _cgmi_stopUserDataFilterHelper(void *pSession, userDataBufferCB bufferCB, userDataRawBufferCB rawBufferCB)
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    GVariant *sessVar = NULL, *dbusVar = NULL;
    tcgmi_PlayerEventCallbackData *cbData;

    // Preconditions
    if( pSession == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        dbusVar = g_variant_new ( "v", sessVar );
        if( dbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

        cbData = g_hash_table_lookup(gPlayerEventCallbacks, (gpointer)pSession);
        if (cbData == NULL)
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        // stop thread that reads from fifo
        cbData->userDataCbRunning = FALSE;

        org_cisco_cgmi_call_stop_user_data_filter_sync( gProxy,
                                       dbusVar,
                                       (gint *)&retStat,
                                       NULL,
                                       &error );


    }while(0);

    //Clean up
    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_stopUserDataFilter(void *pSession, userDataBufferCB bufferCB)
{
    return _cgmi_stopUserDataFilterHelper(pSession, bufferCB, NULL);
}

cgmi_Status cgmi_stopRawUserDataFilter (void *pSession, userDataRawBufferCB bufferCB)
{
    return _cgmi_stopUserDataFilterHelper(pSession, NULL, bufferCB);
}

cgmi_Status cgmi_GetNumPids( void *pSession, int *pCount )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    GVariant *sessVar = NULL, *dbusVar = NULL;

    // Preconditions
    if( pSession == NULL || pCount == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        dbusVar = g_variant_new ( "v", sessVar );
        if( dbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

        org_cisco_cgmi_call_get_num_pids_sync( gProxy,
                dbusVar,
                pCount,
                (gint *)&retStat,
                NULL,
                &error );

    }while(0);

    //Clean up
    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_GetPidInfo( void *pSession, int index, tcgmi_PidData *pPidData )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    GVariant *sessVar = NULL, *dbusVar = NULL;

    // Preconditions
    if( pSession == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        dbusVar = g_variant_new ( "v", sessVar );
        if( dbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

        org_cisco_cgmi_call_get_pid_info_sync( gProxy,
                dbusVar,
                index,
                &pPidData->pid,
                &pPidData->streamType,
                (gint *)&retStat,
                NULL,
                &error );

    }while(0);

    //Clean up
    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_SetPidInfo( void *pSession, int index, tcgmi_StreamType type, int enable )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    GVariant *sessVar = NULL, *dbusVar = NULL;

    // Preconditions
    if( pSession == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        dbusVar = g_variant_new ( "v", sessVar );
        if( dbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

        org_cisco_cgmi_call_set_pid_info_sync( gProxy,
                dbusVar,
                index,
                (gint)type,
                enable,
                (gint *)&retStat,
                NULL,
                &error );

    }while(0);

    //Clean up
    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_SetLogging ( const char *gstDebugStr)
{
    GError *error = NULL;

    if ( gProxy == NULL )
    {
        g_print("Error CGMI not initialized.\n");
        return CGMI_ERROR_NOT_INITIALIZED;
    }

    org_cisco_cgmi_call_set_logging_sync( gProxy, gstDebugStr, NULL, &error );

    if(error)
    {
        g_print("%s,%d: Failed in the client call: %s\n", __FUNCTION__, __LINE__, error->message);
        g_error_free (error);
        return  CGMI_ERROR_FAILED;
    }

    return CGMI_ERROR_SUCCESS;
}

cgmi_Status cgmiDiags_GetTimingMetricsMaxCount ( int *pCount )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;

    // Preconditions
    if(pCount == NULL)
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_dbus_preconditions();

    org_cisco_cgmi_call_get_timing_metrics_max_count_sync( gProxy,
                                                           (gint *)pCount,
                                                           (gint *)&retStat,
                                                           NULL,
                                                           &error );

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmiDiags_GetTimingMetrics ( tCgmiDiags_timingMetric metrics[], int *pCount )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    GVariant *pOutBuf = NULL;
    GVariantIter *iter = NULL;
    unsigned char *pTemMetricsBuf = (unsigned char *)metrics;
    unsigned int max_metrics_buf_byte;

    // Preconditions
    if((pCount == NULL) || (NULL == metrics))
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    if(0 == *pCount)
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    max_metrics_buf_byte = sizeof(tCgmiDiags_timingMetric)*(*pCount);

    enforce_dbus_preconditions();

    org_cisco_cgmi_call_get_timing_metrics_sync ( gProxy,
                                                  (gint)(*pCount),
                                                  (gint *)pCount,
                                                  &pOutBuf,
                                                  (gint *)&retStat,
                                                  NULL,
                                                  &error);

    dbus_check_error(error);

    // Unmarshal time metric buffer
    g_variant_get( pOutBuf, "ay", &iter );
    if( NULL == iter )
    {
        g_print("%s: Error - Failed to get iterator from gvariant\n", __FUNCTION__);
    }
    else
    {
        unsigned int bufIdx = 0;
        while( bufIdx < max_metrics_buf_byte && g_variant_iter_loop(iter, "y", &pTemMetricsBuf[bufIdx]) )
        {
            bufIdx++;
        }
        g_variant_iter_free( iter );
    }

    return retStat;
}

cgmi_Status cgmiDiags_ResetTimingMetrics (void)
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;

    enforce_dbus_preconditions();

    org_cisco_cgmi_call_reset_timing_metrics_sync( gProxy,
                                                   (gint *)&retStat,
                                                   NULL,
                                                   &error );

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_GetTsbSlide( void *pSession, unsigned long *pTsbSlide )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    guint32 tsbSlide = 0;
    GVariant *sessVar = NULL, *dbusVar = NULL;

    // Preconditions
    if( pSession == NULL || pTsbSlide == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        dbusVar = g_variant_new ( "v", sessVar );
        if( dbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

        org_cisco_cgmi_call_get_tsb_slide_sync( gProxy,
                                                dbusVar,
                                                &tsbSlide,
                                                (gint *)&retStat,
                                                NULL,
                                                &error );

    }while(0);

    //Clean up
    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    dbus_check_error(error);

    *pTsbSlide = tsbSlide;

    return retStat;
}

cgmi_Status cgmi_GetNumSubtitleLanguages( void *pSession, int *count )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    GVariant *sessVar = NULL, *dbusVar = NULL;

    // Preconditions
    if( pSession == NULL || count == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        dbusVar = g_variant_new ( "v", sessVar );
        if( dbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

        org_cisco_cgmi_call_get_num_subtitle_languages_sync( gProxy,
                dbusVar,
                count,
                (gint *)&retStat,
                NULL,
                &error );

    }while(0);

    //Clean up
    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_GetSubtitleInfo( void *pSession, int index, char *buf, int bufSize, unsigned short *pid,
                                  unsigned char *type, unsigned short *compPageId, unsigned short *ancPageId )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    gchar *buffer = NULL;
    GVariant *sessVar = NULL, *dbusVar = NULL;

    // Preconditions
    if( NULL == pSession || NULL == buf )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        dbusVar = g_variant_new ( "v", sessVar );
        if( dbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

        org_cisco_cgmi_call_get_subtitle_info_sync( gProxy,
                                                    dbusVar,
                                                    index,
                                                    bufSize,
                                                    (gchar **)&buffer,
                                                    (gushort *)pid,
                                                    (guchar *)type,
                                                    (gushort *)compPageId,
                                                    (gushort *)ancPageId,
                                                    (gint *)&retStat,
                                                    NULL,
                                                    &error );

    }while(0);

    //Clean up
    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    if( NULL == buffer )
    {
       return CGMI_ERROR_FAILED;
    }

    strncpy( buf, buffer, bufSize );
    buf[bufSize - 1] = 0;
    g_free( buffer );

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_SetDefaultSubtitleLang( void *pSession, const char *language )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    GVariant *sessVar = NULL, *dbusVar = NULL;

    // Preconditions
    if( pSession == NULL || language == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        dbusVar = g_variant_new ( "v", sessVar );
        if( dbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

        org_cisco_cgmi_call_set_default_subtitle_lang_sync( gProxy,
                dbusVar,
                language,
                (gint *)&retStat,
                NULL,
                &error );

    }while(0);

    //Clean up
    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_GetStc( void *pSession, uint64_t *pStc )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    guint64 stc = 0;
    GVariant *sessVar = NULL, *dbusVar = NULL;

    // Preconditions
    if( pSession == NULL || pStc == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        dbusVar = g_variant_new ( "v", sessVar );
        if( dbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

        org_cisco_cgmi_call_get_stc_sync( gProxy,
                                          dbusVar,
                                          &stc,
                                          (gint *)&retStat,
                                          NULL,
                                          &error );

    }while(0);

    //Clean up
    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    dbus_check_error(error);

    *pStc = stc;

    return retStat;
}


cgmi_Status cgmi_CreateFilter( void *pSession, int pid, void *pFilterPriv, tcgmi_FilterFormat format, void **pFilterId )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    tcgmi_SectionFilterCbData *sectionFilterData;
    tcgmi_PlayerEventCallbackData *cbData;
    GVariant *sessVar = NULL, *sessDbusVar = NULL;
    GVariant *filterIdVar = NULL, *filterDbusVar = NULL;
    tCgmiDbusPointer filterIdPtr;

    // Preconditions
    if( pSession == NULL || pFilterId == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        sessDbusVar = g_variant_new ( "v", sessVar );
        if( sessDbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessDbusVar = g_variant_ref_sink(sessDbusVar);

        org_cisco_cgmi_call_create_filter_sync( gProxy,
                                                sessDbusVar,
                                                pid,
                                                format,
                                                &filterDbusVar,
                                                (gint *)&retStat,
                                                NULL,
                                                &error );

        if (error)
        {   g_print("%s:%d: IPC failure: %s\n", __FUNCTION__, __LINE__, error->message);
            g_error_free (error);
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        g_variant_get( filterDbusVar, "v", &filterIdVar );
        if( filterIdVar == NULL ) 
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }
        g_variant_get( filterIdVar, DBUS_POINTER_TYPE, &filterIdPtr );
        g_variant_unref( filterIdVar );
        g_variant_unref( filterDbusVar );

        *pFilterId = (void *)filterIdPtr;

    }while(0);

    //Clean up
    if( sessDbusVar != NULL ) { g_variant_unref(sessDbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    dbus_check_error(error);

    //TODO:  Remove debug
    g_print("Created filter ID 0x%08lx\n", filterIdPtr);

    do
    {
        if ( gSectionFilterCbs == NULL )
        {
            g_print("Internal error:  Callback hash table not initialized.\n");
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        sectionFilterData = g_malloc0(sizeof(tcgmi_SectionFilterCbData));
        if (sectionFilterData == NULL)
        {
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }

        cbData = g_hash_table_lookup(gPlayerEventCallbacks, (gpointer)pSession);
        if (cbData == NULL)
        {
            retStat = CGMI_ERROR_FAILED;
            break;
        }

        sectionFilterData->pFilterPriv = pFilterPriv;
        sectionFilterData->pUserData = cbData->userParam;
        sectionFilterData->running = FALSE;

        g_hash_table_insert( gSectionFilterCbs, (gpointer)*pFilterId,
                             (gpointer)sectionFilterData );

        if( NULL == g_hash_table_lookup( gSectionFilterCbs, 
            (gpointer)*pFilterId ) )
        {
            g_print("Can't find the new filter in the hash!!!\n");
        }

    }while(0);

    return retStat;
}

cgmi_Status cgmi_DestroyFilter( void *pSession, void *pFilterId )
{
   return cgmi_DestroySectionFilter(pSession, pFilterId);
}

cgmi_Status cgmi_SetFilter( void *pSession, void *pFilterId, tcgmi_FilterData *pFilter )
{
   return cgmi_SetSectionFilter(pSession, pFilterId, pFilter);
}

cgmi_Status cgmi_StartFilter( void *pSession,
                              void *pFilterId,
                              int timeout,
                              int bOneShot,
                              int bEnableCRC,
                              queryBufferCB bufferCB,
                              sectionBufferCB sectionCB )
{
   return cgmi_StartSectionFilter(pSession, pFilterId, timeout, bOneShot, bEnableCRC, bufferCB, sectionCB);
}

cgmi_Status cgmi_StopFilter( void *pSession, void *pFilterId )
{
   return cgmi_StopSectionFilter(pSession, pFilterId);
}

cgmi_Status cgmi_SetPictureSetting( void *pSession, tcgmi_PictureCtrl pctl, int value )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    GVariant *sessVar = NULL, *dbusVar = NULL;

    // Preconditions
    if( pSession == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        dbusVar = g_variant_new ( "v", sessVar );
        if( dbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

        org_cisco_cgmi_call_set_picture_setting_sync( gProxy,
                dbusVar,
                pctl,
                value,
                (gint *)&retStat,
                NULL,
                &error );

    }while(0);

    //Clean up
    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_GetPictureSetting( void *pSession, tcgmi_PictureCtrl pctl, int *pvalue )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    gint localValue = 0;
    GVariant *sessVar = NULL, *dbusVar = NULL;

    // Preconditions
    if( pSession == NULL || pvalue == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    do{
        sessVar = g_variant_new ( DBUS_POINTER_TYPE, (tCgmiDbusPointer)pSession );
        if( sessVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        sessVar = g_variant_ref_sink(sessVar);

        dbusVar = g_variant_new ( "v", sessVar );
        if( dbusVar == NULL )
        {
            g_print("Failed to create new variant\n");
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }
        dbusVar = g_variant_ref_sink(dbusVar);

        org_cisco_cgmi_call_get_picture_setting_sync( gProxy,
                                               dbusVar,
                                               pctl,
                                               &localValue,
                                               (gint *)&retStat,
                                               NULL,
                                               &error );

    }while(0);

    //Clean up
    if( dbusVar != NULL ) { g_variant_unref(dbusVar); }
    if( sessVar != NULL ) { g_variant_unref(sessVar); }

    dbus_check_error(error);

    *pvalue = localValue;

    return retStat;
}

cgmi_Status cgmi_GetActiveSessionsInfo(sessionInfo *sessInfoArr[], int *numSessOut)
{
   cgmi_Status  retStat = CGMI_ERROR_SUCCESS;
   GError       *error = NULL;
   GVariant     *sessInfoArr_variant = NULL;
   GVariantIter *iter = NULL;
   gint         ii = 0;
   gchar        *uri = NULL;
   guint64      videoHandle = 0;
   guint64      audioHandle = 0;

   if((sessInfoArr == NULL) || (numSessOut == NULL))
   {
      return CGMI_ERROR_BAD_PARAM;
   }

   *sessInfoArr = NULL;
   *numSessOut = 0;

   enforce_dbus_preconditions();

   do
   {
      org_cisco_cgmi_call_get_active_sessions_info_sync(gProxy,
            &sessInfoArr_variant,
            numSessOut,
            (gint *)&retStat,
            NULL,
            &error );

      if(0 == *numSessOut)
      {
         break;
      }

      if(NULL == sessInfoArr_variant)
      {
         g_print("sessInfoArr is NULL for non zero session count\n");
         retStat = CGMI_ERROR_FAILED;
         break;
      }

      *sessInfoArr = (sessionInfo *)g_malloc0(sizeof(sessionInfo) * *numSessOut);
      if(NULL == sessInfoArr)
      {
         g_print("Failed to alloc sessInfoArr");
         retStat = CGMI_ERROR_FAILED;
         break;
      }

      g_variant_get(sessInfoArr_variant, "a(stt)", &iter);
      while(g_variant_iter_loop(iter, "(stt)", &uri, &videoHandle, &audioHandle))
      {
         g_strlcpy((*sessInfoArr)[ii].uri, uri, sizeof((*sessInfoArr)[ii].uri));
         (*sessInfoArr)[ii].hwVideoDecHandle = videoHandle;
         (*sessInfoArr)[ii].hwAudioDecHandle = audioHandle;

         ii++;
      }

   }while(0);

   if(NULL != sessInfoArr_variant)
   {
      g_variant_unref(sessInfoArr_variant);
   }

   return retStat;
}
