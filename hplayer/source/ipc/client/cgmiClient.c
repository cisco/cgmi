#include <glib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>

#include "cgmiPlayerApi.h"
#include "cgmi_dbus_client_generated.h"

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
                __FILE__, __LINE__, __FUNCTION__, (guint64)pSess); \
        return CGMI_ERROR_BAD_PARAM; \
    }


////////////////////////////////////////////////////////////////////////////////
// Typedefs
////////////////////////////////////////////////////////////////////////////////
typedef struct
{
    cgmi_EventCallback callback;
    void *userParam;

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
static OrgCiscoCgmi     *gProxy                   = NULL;
static GHashTable       *gPlayerEventCallbacks    = NULL;
static GHashTable       *gSectionFilterCbs        = NULL;
static cgmi_Status      gInitStatus               = CGMI_ERROR_NOT_INITIALIZED;
static pthread_t        gMainLoopThread;
static sem_t            gMainThreadStartSema;
static pthread_mutex_t  gEventCallbackMutex;


////////////////////////////////////////////////////////////////////////////////
// DBUS Callbacks
////////////////////////////////////////////////////////////////////////////////
static gboolean on_handle_notification (  OrgCiscoCgmi *proxy,
        guint64 sessionHandle,
        gint event,
        gint data)
{
    tcgmi_PlayerEventCallbackData *playerCb = NULL;

    g_print("Enter on_handle_notification sessionHandle = %lu, event = %d...\n",
            sessionHandle, event);

    // Preconditions
    if ( proxy != gProxy )
    {
        g_print("DBUS failure proxy doesn't match.\n");
        return TRUE;
    }

    pthread_mutex_lock(&gEventCallbackMutex);

    do
    {
        if ( gPlayerEventCallbacks == NULL )
        {
            g_print("Internal error:  Callback hash table not initialized.\n");
            break;
        }

        playerCb = g_hash_table_lookup(gPlayerEventCallbacks, (gpointer)sessionHandle);

        if ( playerCb == NULL || playerCb->callback == NULL )
        {
            g_print("Failed to find callback for sessionId (%lu) in hash table.\n",
                    sessionHandle );

            break;
        }

        playerCb->callback( playerCb->userParam,
                            (void *)sessionHandle,
                            (tcgmi_Event)event );

    }
    while (0);

    pthread_mutex_unlock(&gEventCallbackMutex);

    return TRUE;
}

static gboolean on_handle_section_buffer_notify (  OrgCiscoCgmi *proxy,
        guint64 filterId,
        gint sectionStatus,
        GVariant *arg_section,
        gint sectionSize)
{
    tcgmi_SectionFilterCbData *filterCbs = NULL;
    char *retBuffer = NULL;
    int retBufferSize = sectionSize;
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    GVariantIter *iter = NULL;
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
        // Find callbacks
        filterCbs = g_hash_table_lookup( gSectionFilterCbs, (gpointer)filterId );
        if( NULL == filterCbs || NULL == filterCbs->bufferCB || NULL == filterCbs->sectionCB )
        {
            //g_print("Failed to find callback(s) for filterId (0x%08lx) in hash table.\n",
            //        (void *)filterId );
            break;
        }

        // Ignore tardy signals
        if( FALSE == filterCbs->running ) { break; }

        // Ask for a buffer from the app
        retStat = filterCbs->bufferCB( filterCbs->pUserData, 
            filterCbs->pFilterPriv, 
            (void *)filterId,
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
            (void *)filterId,
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
}

/* Called whem the server losses name on dbus */
static void on_name_vanished (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
    g_print ("Name %s does not exist on the session bus\n", name);

    cgmi_ResetClientState();
}


/* This callback posts the semaphore to indicate the main loop is running */
static gboolean cgmi_DbusMainLoopStarted( gpointer user_data )
{
    // If dbus server hasn't been found yet wait a while
    if( gProxy == NULL && user_data == NULL )
    {
        g_timeout_add( 200, cgmi_DbusMainLoopStarted, 
            (gpointer *)&gMainThreadStartSema );
    }else
    {
        sem_post(&gMainThreadStartSema);
    }
    
    return FALSE;
}

/* This is spawned as a thread and starts the glib main loop for dbus. */
static void *cgmi_DbusMainLoop(void *data)
{
    guint watcher_id;

    g_type_init();

    gLoop = g_main_loop_new (NULL, FALSE);
    if ( gLoop == NULL )
    {
        g_print("Error creating a new main_loop\n");
        return;
    }

    // Add callbacks for when the dbus server goes up/down
    watcher_id = g_bus_watch_name ( G_BUS_TYPE_SESSION,
                                 "org.cisco.cgmi",
                                 G_BUS_NAME_WATCHER_FLAGS_NONE,
                                 on_name_appeared,
                                 on_name_vanished,
                                 NULL,
                                 NULL);


    /* This timeout will be called by the main loop after it starts */
    g_timeout_add( 5, cgmi_DbusMainLoopStarted, NULL );
    g_main_loop_run( gLoop );

    return NULL;
}

/**
 *  Needs to be called prior to any DBUS APIs (called by CGMI Init)
 */
cgmi_Status cgmi_DbusInterfaceInit()
{
    GError *error = NULL;

    if ( gClientInited == TRUE )
    {
        g_print("Dbus proxy is already open.\n");
        return CGMI_ERROR_WRONG_STATE;
    }

    sem_init(&gMainThreadStartSema, 0, 0);

    if ( 0 != pthread_create(&gMainLoopThread, NULL, cgmi_DbusMainLoop, NULL) )
    {
        g_print("Error launching thread for gmainloop\n");
        return CGMI_ERROR_FAILED;
    }

    sem_wait(&gMainThreadStartSema);
    sem_destroy(&gMainThreadStartSema);

    gClientInited = TRUE;

    return CGMI_ERROR_SUCCESS;
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
        pthread_join (gMainLoopThread, NULL);
        g_main_loop_unref( gLoop );
    }

    gClientInited = FALSE;
}


////////////////////////////////////////////////////////////////////////////////
// CGMI DBUS client APIs
////////////////////////////////////////////////////////////////////////////////
cgmi_Status cgmi_Init (void)
{
    GError *error = NULL;

    cgmi_DbusInterfaceInit();

    return gInitStatus;
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
    guint64 sessionId;
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
                &sessionId,
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

        *pSession = (void *)sessionId;

        eventCbData = g_malloc0(sizeof(tcgmi_PlayerEventCallbackData));
        if (eventCbData == NULL)
        {
            retStat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }

        eventCbData->callback = eventCB;
        eventCbData->userParam = pUserData;

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

    // Preconditions
    if( pSession == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();


    org_cisco_cgmi_call_destroy_session_sync( gProxy,
            (guint64)pSession,
            (gint *)&retStat,
            NULL,
            &error );

    // Sync operations on the event callback hash
    pthread_mutex_lock(&gEventCallbackMutex);

    if ( gPlayerEventCallbacks != NULL )
    {
        g_hash_table_remove( gPlayerEventCallbacks, GINT_TO_POINTER((guint64)pSession) );
    }

    pthread_mutex_unlock(&gEventCallbackMutex);

    dbus_check_error(error);

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

cgmi_Status cgmi_Load( void *pSession, const char *uri )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;

    // Preconditions
    if( pSession == NULL || uri == NULL)
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();


    org_cisco_cgmi_call_load_sync( gProxy,
                                   (guint64)pSession,
                                   (const gchar *)uri,
                                   (gint *)&retStat,
                                   NULL,
                                   &error );

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_Unload( void *pSession )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;

    // Preconditions
    if( pSession == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();


    org_cisco_cgmi_call_unload_sync( gProxy,
                                     (guint64)pSession,
                                     (gint *)&retStat,
                                     NULL,
                                     &error );

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_Play( void *pSession )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;

    // Preconditions
    if( pSession == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();


    org_cisco_cgmi_call_play_sync( gProxy,
                                   (guint64)pSession,
                                   (gint *)&retStat,
                                   NULL,
                                   &error );

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_SetRate( void *pSession,  float rate )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;

    // Preconditions
    if( pSession == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();


    org_cisco_cgmi_call_set_rate_sync( gProxy,
                                       (guint64)pSession,
                                       (gdouble)rate,
                                       (gint *)&retStat,
                                       NULL,
                                       &error );

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_SetPosition( void *pSession, float position )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;

    // Preconditions
    if( pSession == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();


    org_cisco_cgmi_call_set_position_sync( gProxy,
                                           (guint64)pSession,
                                           (gdouble)position,
                                           (gint *)&retStat,
                                           NULL,
                                           &error );

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_GetPosition( void *pSession, float *pPosition )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    gdouble localPosition = 0;

    // Preconditions
    if( pSession == NULL || pPosition == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();


    org_cisco_cgmi_call_get_position_sync( gProxy,
                                           (guint64)pSession,
                                           &localPosition,
                                           (gint *)&retStat,
                                           NULL,
                                           &error );

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

    // Preconditions
    if( pSession == NULL || pDuration == NULL || type == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();


    org_cisco_cgmi_call_get_duration_sync( gProxy,
                                           (guint64)pSession,
                                           &localDuration,
                                           (gint *)type,
                                           (gint *)&retStat,
                                           NULL,
                                           &error );

    dbus_check_error(error);

    //Dbus doesn't have a float type (just double), hence this funny business
    *pDuration = (float)localDuration;

    return retStat;
}

cgmi_Status cgmi_GetRateRange( void *pSession, float *pRewind, float *pFForward )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    gdouble localRewind = 0;
    gdouble localFForward = 0;

    // Preconditions
    if( pSession == NULL || pRewind == NULL || pFForward == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();


    org_cisco_cgmi_call_get_rate_range_sync( gProxy,
            (guint64)pSession,
            &localRewind,
            &localFForward,
            (gint *)&retStat,
            NULL,
            &error );

    dbus_check_error(error);

    *pRewind = (float)localRewind;
    *pFForward = (float)localFForward;

    return retStat;
}

cgmi_Status cgmi_SetVideoRectangle( void *pSession, int x, int y, int w, int h  )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;

    // Preconditions
    if( pSession == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    org_cisco_cgmi_call_set_video_rectangle_sync( gProxy,
            (guint64)pSession,
            x,
            y,
            w,
            h,
            (gint *)&retStat,
            NULL,
            &error );

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_GetNumAudioLanguages( void *pSession,  int *count )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;

    // Preconditions
    if( pSession == NULL || count == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();


    org_cisco_cgmi_call_get_num_audio_languages_sync( gProxy,
            (guint64)pSession,
            count,
            (gint *)&retStat,
            NULL,
            &error );

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_GetAudioLangInfo( void *pSession, int index,
                                   char *buf, int bufSize )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    int idx;
    gchar *buffer = NULL;

    // Preconditions
    if( NULL == pSession || NULL == buf )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    org_cisco_cgmi_call_get_audio_lang_info_sync( gProxy,
            (guint64)pSession,
            index,
            bufSize,
            (gchar **)&buffer,
            (gint *)&retStat,
            NULL,
            &error );

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

    // Preconditions
    if( pSession == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();

    org_cisco_cgmi_call_set_audio_stream_sync( gProxy,
            (guint64)pSession,
            index,
            (gint *)&retStat,
            NULL,
            &error );

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_SetDefaultAudioLang( void *pSession, const char *language )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;

    // Preconditions
    if( pSession == NULL || language == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();


    org_cisco_cgmi_call_set_default_audio_lang_sync( gProxy,
            (guint64)pSession,
            language,
            (gint *)&retStat,
            NULL,
            &error );

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_CreateSectionFilter( void *pSession, void *pFilterPriv, void **pFilterId )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    tcgmi_SectionFilterCbData *sectionFilterData;
    tcgmi_PlayerEventCallbackData *cbData;
    void *pFilter;

    // Preconditions
    if( pSession == NULL || pFilterId == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();


    org_cisco_cgmi_call_create_section_filter_sync( gProxy,
            (guint64)pSession,
            (guint64 *)pFilterId,
            (gint *)&retStat,
            NULL,
            &error );

    dbus_check_error(error);

    //TODO:  Remove debug
    g_print("Created filter ID 0x%08lx\n", *pFilterId);

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
        else
        {
            g_print("Found the new filter in the hash!!!\n");
        }

    }while(0);

    return retStat;
}

cgmi_Status cgmi_DestroySectionFilter(void *pSession, void *pFilterId )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;

    // Preconditions
    if( pSession == NULL || pFilterId == NULL )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    enforce_session_preconditions(pSession);

    enforce_dbus_preconditions();


    org_cisco_cgmi_call_destroy_section_filter_sync( gProxy,
            (guint64)pSession,
            (guint64)pFilterId,
            (gint *)&retStat,
            NULL,
            &error );

    if ( gSectionFilterCbs != NULL )
    {
        // let the hash table free the tcgmi_SectionFilterCbData instance
        g_hash_table_remove( gSectionFilterCbs, GINT_TO_POINTER((guint64)pFilterId) );
    }

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_SetSectionFilter(void *pSession, void *pFilterId, tcgmi_FilterData *pFilter )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
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

    // Marshal the value and mask GVariants
    valueBuilder = g_variant_builder_new( G_VARIANT_TYPE("ay") );
    maskBuilder = g_variant_builder_new( G_VARIANT_TYPE("ay") );
    for( idx = 0; idx < pFilter->length; idx++ )
    {
        g_variant_builder_add( valueBuilder, "y", pFilter->value[idx] );
        g_variant_builder_add( maskBuilder, "y", pFilter->mask[idx] );
    }
    value = g_variant_builder_end( valueBuilder );
    mask = g_variant_builder_end( maskBuilder );


    org_cisco_cgmi_call_set_section_filter_sync( gProxy,
            (guint64)pSession,
            (guint64)pFilterId,
            (gint)pFilter->pid,
            value,
            mask,
            (gint)pFilter->length,
            (gint)pFilter->comparitor,
            (gint *)&retStat,
            NULL,
            &error );

    dbus_check_error(error);

    g_variant_builder_unref( valueBuilder );
    g_variant_builder_unref( maskBuilder );

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

    // Call DBUS
    org_cisco_cgmi_call_start_section_filter_sync( gProxy,
            (guint64)pSession,
            (guint64)pFilterId,
            timeout,
            bOneShot,
            bEnableCRC,
            (gint *)&retStat,
            NULL,
            &error );

    dbus_check_error(error);

    return retStat;
}

cgmi_Status cgmi_StopSectionFilter(void *pSession, void *pFilterId )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    tcgmi_SectionFilterCbData *filterCb;

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

    org_cisco_cgmi_call_stop_section_filter_sync( gProxy,
            (guint64)pSession,
            (guint64)pFilterId,
            (gint *)&retStat,
            NULL,
            &error );

    dbus_check_error(error);

    return retStat;
}
