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
        {   g_print("%s,%d: Failed in the client call: %s\n", __FUNCTION__, __LINE__, error->message);\
            g_error_free (error);\
            return  CGMI_ERROR_FAILED;\
        }\
    }while(0)

#define enforce_dbus_preconditions() \
    if ( gProxy == NULL ) \
    { \
        g_print("%s:%d - %s Error CGMI not initialized.\n", \
                __FILE__, __LINE__, __FUNCTION__); \
        return CGMI_ERROR_WRONG_STATE; \
    }


////////////////////////////////////////////////////////////////////////////////
// Typedefs
////////////////////////////////////////////////////////////////////////////////
typedef struct
{
    cgmi_EventCallback callback;
    void *userParam;

} tcgmi_PlayerEventCallbackData;


////////////////////////////////////////////////////////////////////////////////
// Globals
////////////////////////////////////////////////////////////////////////////////
static GMainLoop        *gLoop                  = NULL;
static OrgCiscoCgmi     *gProxy                 = NULL;
static GHashTable       *gPlayerEventCallbacks  = NULL;
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

        tcgmi_PlayerEventCallbackData *callbackData =
            g_hash_table_lookup(gPlayerEventCallbacks, (gpointer)sessionHandle);

        if ( callbackData == NULL || callbackData->callback == NULL )
        {
            g_print("Failed to find callback for sessionId (%lu) in hash table.\n",
                    sessionHandle );

            break;
        }

        callbackData->callback( callbackData->userParam,
                                (void *)sessionHandle,
                                (tcgmi_Event)event );

    }
    while (0);

    pthread_mutex_unlock(&gEventCallbackMutex);

    return TRUE;
}


////////////////////////////////////////////////////////////////////////////////
// DBUS client specific setup and tear down APIs
////////////////////////////////////////////////////////////////////////////////

/* This callback posts the semaphore to indicate the main loop is running */
static gboolean cgmi_DbusMainLoopStarted( gpointer user_data )
{
    sem_post(&gMainThreadStartSema);
    return FALSE;
}

/* This is spawned as a thread and starts the glib main loop for dbus. */
static void *cgmi_DbusMainLoop(void *data)
{
    GError *error = NULL;

    g_type_init();

    gLoop = g_main_loop_new (NULL, FALSE);
    if ( gLoop == NULL )
    {
        g_print("Error creating a new main_loop\n");
        return;
    }

    gProxy = org_cisco_cgmi_proxy_new_for_bus_sync( G_BUS_TYPE_SESSION,
             G_DBUS_PROXY_FLAGS_NONE, "org.cisco.cgmi", "/org/cisco/cgmi", NULL, &error );

    if (error)
    {
        g_print("Failed in dbus proxy call: %s\n", error->message);
        g_error_free(error);
        return NULL;
    }

    g_signal_connect( gProxy, "player-notify",
                      G_CALLBACK (on_handle_notification), NULL );

    gPlayerEventCallbacks = g_hash_table_new(g_direct_hash, g_direct_equal);


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

    if (gProxy != NULL)
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

    return CGMI_ERROR_SUCCESS;
}

/**
 *  Called to clean up prior to shutdown (called by GCMI Term)
 */
void cgmi_DbusInterfaceTerm()
{
    if ( gProxy == NULL ) return;

    pthread_mutex_lock(&gEventCallbackMutex);
    if ( gPlayerEventCallbacks != NULL )
    {
        g_hash_table_destroy(gPlayerEventCallbacks);
        gPlayerEventCallbacks = NULL;
    }
    pthread_mutex_unlock(&gEventCallbackMutex);

    // Kill the dbus thread
    g_main_loop_quit( gLoop );
    pthread_join (gMainLoopThread, NULL);
    if ( gLoop ) g_main_loop_unref( gLoop );

    g_object_unref(gProxy);
    gProxy = NULL;
}


////////////////////////////////////////////////////////////////////////////////
// CGMI DBUS client APIs
////////////////////////////////////////////////////////////////////////////////
cgmi_Status cgmi_Init (void)
{
    cgmi_Status stat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;

    cgmi_DbusInterfaceInit();

    org_cisco_cgmi_call_init_sync( gProxy, (gint *)&stat, NULL, &error );

    dbus_check_error(error);

    return stat;
}

cgmi_Status cgmi_Term (void)
{
    cgmi_Status stat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;

    enforce_dbus_preconditions();

    org_cisco_cgmi_call_term_sync( gProxy, (gint *)&stat, NULL, &error );

    dbus_check_error(error);

    cgmi_DbusInterfaceTerm();

    return stat;
}

char* cgmi_ErrorString ( cgmi_Status stat )
{
    gchar *statusString = NULL;
    GError *error = NULL;

    if ( gProxy == NULL )
    {
        g_print("%s:%d - %s Error CGMI not initialized.\n",
                __FILE__, __LINE__, __FUNCTION__);
        return NULL;
    }

    org_cisco_cgmi_call_error_string_sync( gProxy, stat, &statusString, NULL, &error );

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
    cgmi_Status stat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    guint64 sessionId;
    tcgmi_PlayerEventCallbackData *eventCbData = NULL;

    enforce_dbus_preconditions();

    pthread_mutex_lock(&gEventCallbackMutex);

    do
    {

        org_cisco_cgmi_call_create_session_sync( gProxy,
                &sessionId,
                (gint *)&stat,
                NULL,
                &error );

        if (error)
        {
            g_print("%s,%d: Failed in the client call: %s\n", __FUNCTION__, __LINE__,
                    error->message);
            g_error_free (error);
            stat = CGMI_ERROR_FAILED;
            break;
        }

        *pSession = (void *)sessionId;

        eventCbData = g_malloc0(sizeof(tcgmi_PlayerEventCallbackData));
        if (eventCbData == NULL)
        {
            stat = CGMI_ERROR_OUT_OF_MEMORY;
            break;
        }

        eventCbData->callback = eventCB;
        eventCbData->userParam = pUserData;

        if ( gPlayerEventCallbacks == NULL )
        {
            g_print("Internal error:  Callback hash table not initialized.\n");
            stat = CGMI_ERROR_FAILED;
            break;
        }

        g_hash_table_insert( gPlayerEventCallbacks, (gpointer)sessionId,
                             (gpointer)eventCbData );

    }
    while (0);

    pthread_mutex_unlock(&gEventCallbackMutex);

    return stat;
}

cgmi_Status cgmi_DestroySession( void *pSession )
{
    cgmi_Status stat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;

    enforce_dbus_preconditions();

    org_cisco_cgmi_call_destroy_session_sync( gProxy,
            (guint64)pSession,
            (gint *)&stat,
            NULL,
            &error );

    pthread_mutex_lock(&gEventCallbackMutex);

    if ( gPlayerEventCallbacks != NULL )
    {
        g_hash_table_remove( gPlayerEventCallbacks, GINT_TO_POINTER((guint64)pSession) );
    }

    pthread_mutex_unlock(&gEventCallbackMutex);

    dbus_check_error(error);

    return stat;
}

cgmi_Status cgmi_canPlayType( const char *type, int *pbCanPlay )
{
    cgmi_Status stat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;

    enforce_dbus_preconditions();

    org_cisco_cgmi_call_can_play_type_sync( gProxy,
                                            (const gchar *)type,
                                            (gint *)pbCanPlay,
                                            (gint *)&stat,
                                            NULL,
                                            &error );

    dbus_check_error(error);

    return stat;
}

cgmi_Status cgmi_Load( void *pSession, const char *uri )
{
    cgmi_Status stat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;

    enforce_dbus_preconditions();

    org_cisco_cgmi_call_load_sync( gProxy,
                                   (guint64)pSession,
                                   (const gchar *)uri,
                                   (gint *)&stat,
                                   NULL,
                                   &error );

    dbus_check_error(error);

    return stat;
}

cgmi_Status cgmi_Unload( void *pSession )
{
    cgmi_Status stat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;

    enforce_dbus_preconditions();

    org_cisco_cgmi_call_unload_sync( gProxy,
                                     (guint64)pSession,
                                     (gint *)&stat,
                                     NULL,
                                     &error );

    dbus_check_error(error);

    return stat;
}

cgmi_Status cgmi_Play( void *pSession )
{
    cgmi_Status stat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;

    enforce_dbus_preconditions();

    org_cisco_cgmi_call_play_sync( gProxy,
                                   (guint64)pSession,
                                   (gint *)&stat,
                                   NULL,
                                   &error );

    dbus_check_error(error);

    return stat;
}

cgmi_Status cgmi_SetRate( void *pSession,  float rate )
{
    cgmi_Status stat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;

    enforce_dbus_preconditions();

    org_cisco_cgmi_call_set_rate_sync( gProxy,
                                       (guint64)pSession,
                                       (gdouble)rate,
                                       (gint *)&stat,
                                       NULL,
                                       &error );

    dbus_check_error(error);

    return stat;
}

cgmi_Status cgmi_SetPosition( void *pSession, float position )
{
    cgmi_Status stat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;

    enforce_dbus_preconditions();

    org_cisco_cgmi_call_set_position_sync( gProxy,
                                           (guint64)pSession,
                                           (gdouble)position,
                                           (gint *)&stat,
                                           NULL,
                                           &error );

    dbus_check_error(error);

    return stat;
}

cgmi_Status cgmi_GetPosition( void *pSession, float *pPosition )
{
    cgmi_Status stat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    gdouble localPosition = 0;

    enforce_dbus_preconditions();

    org_cisco_cgmi_call_get_position_sync( gProxy,
                                           (guint64)pSession,
                                           (gdouble *)pPosition,
                                           (gint *)&stat,
                                           NULL,
                                           &error );

    dbus_check_error(error);

    *pPosition = (float)localPosition;

    return stat;
}

cgmi_Status cgmi_GetDuration( void *pSession,  float *pDuration,
                              cgmi_SessionType type )
{
    cgmi_Status stat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    gdouble localDuration = 0;

    enforce_dbus_preconditions();


    org_cisco_cgmi_call_get_duration_sync( gProxy,
                                           (guint64)pSession,
                                           type,
                                           &localDuration,
                                           (gint *)&stat,
                                           NULL,
                                           &error );

    dbus_check_error(error);

    //Dbus doesn't have a float type (just double), hence this funny business
    *pDuration = (float)localDuration;

    return stat;
}

cgmi_Status cgmi_GetRateRange( void *pSession, float *pRewind, float *pFForward )
{
    cgmi_Status stat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;
    gdouble localRewind = 0;
    gdouble localFForward = 0;

    enforce_dbus_preconditions();

    org_cisco_cgmi_call_get_rate_range_sync( gProxy,
            (guint64)pSession,
            &localRewind,
            &localFForward,
            (gint *)&stat,
            NULL,
            &error );

    dbus_check_error(error);

    *pRewind = (float)localRewind;
    *pFForward = (float)localFForward;

    return stat;
}

cgmi_Status cgmi_GetNumAudioStreams( void *pSession,  int *count )
{
    cgmi_Status stat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;

    enforce_dbus_preconditions();

    org_cisco_cgmi_call_get_num_audio_streams_sync( gProxy,
            (guint64)pSession,
            count,
            (gint *)&stat,
            NULL,
            &error );

    dbus_check_error(error);

    return stat;
}

cgmi_Status cgmi_GetAudioStreamInfo( void *pSession,  int index,
                                     char *buf, int bufSize )
{
    cgmi_Status stat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;

    enforce_dbus_preconditions();

    org_cisco_cgmi_call_get_audio_stream_info_sync( gProxy,
            (guint64)pSession,
            index,
            bufSize,
            &buf,
            (gint *)&stat,
            NULL,
            &error );

    dbus_check_error(error);

    return stat;
}

cgmi_Status cgmi_SetAudioStream( void *pSession,  int index )
{
    cgmi_Status stat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;

    enforce_dbus_preconditions();

    org_cisco_cgmi_call_set_audio_stream_sync( gProxy,
            (guint64)pSession,
            index,
            (gint *)&stat,
            NULL,
            &error );

    dbus_check_error(error);

    return stat;
}

cgmi_Status cgmi_SetDefaultAudioLang( void *pSession,  const char *language )
{
    cgmi_Status stat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;

    enforce_dbus_preconditions();

    org_cisco_cgmi_call_set_default_audio_lang_sync( gProxy,
            (guint64)pSession,
            language,
            (gint *)&stat,
            NULL,
            &error );

    dbus_check_error(error);

    return stat;
}

cgmi_Status cgmi_CreateSectionFilter( void *pSession, void *pFilterPriv, void **pFilterId )
{
    cgmi_Status stat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;

    enforce_dbus_preconditions();

    org_cisco_cgmi_call_create_section_filter_sync( gProxy,
            (guint64)pSession,
            (guint64)pFilterPriv,
            (guint64 *)pFilterId,
            (gint *)&stat,
            NULL,
            &error );

    dbus_check_error(error);

    return stat;
}

cgmi_Status cgmi_DestroySectionFilter(void *pSession, void *pFilterId )
{
    cgmi_Status stat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;

    enforce_dbus_preconditions();

    org_cisco_cgmi_call_destroy_section_filter_sync( gProxy,
            (guint64)pSession,
            (guint64)pFilterId,
            (gint *)&stat,
            NULL,
            &error );

    dbus_check_error(error);

    return stat;
}

cgmi_Status cgmi_SetSectionFilter(void *pSession, void *pFilterId, tcgmi_FilterData *pFilter )
{
    cgmi_Status stat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;

    enforce_dbus_preconditions();

    org_cisco_cgmi_call_set_section_filter_sync( gProxy,
            (guint64)pSession,
            (guint64)pFilterId,
            (gchar *)pFilter,
            (gint *)&stat,
            NULL,
            &error );

    dbus_check_error(error);

    return stat;
}

cgmi_Status cgmi_StartSectionFilter(void *pSession,
                                    void *pFilterId,
                                    int timeout,
                                    int bOneShot,
                                    int bEnableCRC,
                                    queryBufferCB bufferCB,
                                    sectionBufferCB sectionCB )
{
    cgmi_Status stat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;

    enforce_dbus_preconditions();

    // TODO:  Implement callbacks
    org_cisco_cgmi_call_start_section_filter_sync( gProxy,
            (guint64)pSession,
            (guint64)pFilterId,
            timeout,
            bOneShot,
            bEnableCRC,
            (gint *)&stat,
            NULL,
            &error );

    dbus_check_error(error);

    return stat;
}

cgmi_Status cgmi_StopSectionFilter(void *pSession, void *pFilterId )
{
    cgmi_Status stat = CGMI_ERROR_SUCCESS;
    GError *error = NULL;

    enforce_dbus_preconditions();

    org_cisco_cgmi_call_stop_section_filter_sync( gProxy,
            (guint64)pSession,
            (guint64)pFilterId,
            (gint *)&stat,
            NULL,
            &error );

    dbus_check_error(error);

    return stat;
}
