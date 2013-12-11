#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <glib.h>
#include <syslog.h>
#include <glib/gprintf.h>
// put in a #define #include "diaglib.h"
#include "cgmiPlayerApi.h"
#include "cgmi_dbus_server_generated.h"


////////////////////////////////////////////////////////////////////////////////
// Defines
////////////////////////////////////////////////////////////////////////////////
#define DEFAULT_SECTION_FILTER_BUFFER_SIZE 256;

////////////////////////////////////////////////////////////////////////////////
// Logging for daemon.  TODO:  Send to syslog when in background
////////////////////////////////////////////////////////////////////////////////
#define DAEMON_NAME "CGMI_DAEMON"

#define LOGGING_ENABLED

#ifdef LOGGING_ENABLED
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define CGMID_AT DAEMON_NAME ":" TOSTRING(__LINE__)

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
// Globals
////////////////////////////////////////////////////////////////////////////////
static gboolean gCgmiInited = FALSE;
static gboolean gInForeground = FALSE;


////////////////////////////////////////////////////////////////////////////////
// Logging
////////////////////////////////////////////////////////////////////////////////
static void cgmiDaemonLog( const char *open, int priority, const char *format, ... )
{
    gchar *message;

    va_list args;
    va_start (args, format);
    message = g_strdup_vprintf (format, args);
    va_end (args);

    if( gInForeground == TRUE )
    {
        g_print("%-20s - %s", open, message);
    }else{
        syslog(priority, "%-20s - %s", open, message);
    }

    g_free (message);
}

static void gPrintToSylog(const gchar *message)
{
    syslog(LOG_INFO, "%-20s - %s", "CGMI", message);
}
static void gPrintErrToSylog(const gchar *message)
{
    syslog(LOG_ERR, "%-20s - %s", "CGMI ERROR", message);
}

////////////////////////////////////////////////////////////////////////////////
// Callbacks called by CGMI core to message client via DBUS
////////////////////////////////////////////////////////////////////////////////
static void cgmiEventCallback( void *pUserData, void *pSession, tcgmi_Event event )
{

    org_cisco_cgmi_emit_player_notify( (OrgCiscoCgmi *) pUserData,
                                       (guint64)pSession,
                                       event,
                                       0 );

    CGMID_INFO("cgmiEventCallback -- pSession: %lu, event%d \n",
            (guint64)pSession, event);
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
    GVariantBuilder *sectionBuilder = NULL;
    GVariant *sectionArray = NULL;
    int idx;

    //CGMID_INFO("cgmiSectionBufferCallback -- pFilterId: %lu, pFilterPriv: %lu \n",
    //        (guint64)pFilterId, (guint64)pFilterPriv);

    // Preconditions
    if( NULL == pSection )
    {
        CGMID_ERROR("NULL buffer passed to cgmiSectionBufferCallback.\n");
        return CGMI_ERROR_BAD_PARAM;
    }

    // Marshal gvariant
    sectionBuilder = g_variant_builder_new( G_VARIANT_TYPE("ay") );
    for( idx = 0; idx < sectionSize; idx++ )
    {
        g_variant_builder_add( sectionBuilder, "y", pSection[idx] );
    }
    sectionArray = g_variant_builder_end( sectionBuilder );

    //CGMID_INFO("Sending pFilterId: 0x%lx, sectionSize %d\n", pFilterId, sectionSize);
    org_cisco_cgmi_emit_section_buffer_notify( (OrgCiscoCgmi *) pUserData,
            (guint64)pFilterId,
            (gint)sectionStatus,
            sectionArray,
            sectionSize );

    g_variant_builder_unref( sectionBuilder );

    // Free buffer allocated in cgmiQueryBufferCallback
    g_free( pSection );

    return CGMI_ERROR_SUCCESS;
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

    CGMID_ENTER();

    retStat = cgmi_CreateSession( cgmiEventCallback, (void *)object, &pSessionId );

    org_cisco_cgmi_complete_create_session (object,
                                            invocation,
                                            (guint64)pSessionId,
                                            retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_destroy_session (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    guint64 arg_sessionId )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;

    CGMID_ENTER();

    retStat = cgmi_DestroySession( (void *)arg_sessionId );

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

    CGMID_ENTER();

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
    guint64 arg_sessionId,
    const gchar *uri )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;

    CGMID_ENTER();

    retStat = cgmi_Load( (void *)arg_sessionId, uri );

    org_cisco_cgmi_complete_load (object,
                                  invocation,
                                  retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_unload (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    guint64 arg_sessionId )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;

    CGMID_ENTER();

    retStat = cgmi_Unload( (void *)arg_sessionId );

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
    guint64 arg_sessionId )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;

    CGMID_ENTER();

    retStat = cgmi_Play( (void *)arg_sessionId );

    org_cisco_cgmi_complete_play (object,
                                  invocation,
                                  retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_set_rate (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    guint64 arg_sessionId,
    gdouble arg_rate )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;

    CGMID_ENTER();

    retStat = cgmi_SetRate( (void *)arg_sessionId, arg_rate );

    org_cisco_cgmi_complete_set_rate (object,
                                      invocation,
                                      retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_set_position (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    guint64 arg_sessionId,
    gdouble arg_position )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;

    CGMID_ENTER();

    retStat = cgmi_SetPosition( (void *)arg_sessionId, arg_position );

    org_cisco_cgmi_complete_set_position (object,
                                          invocation,
                                          retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_get_position (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    guint64 arg_sessionId )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    float position = 0;

    CGMID_ENTER();

    retStat = cgmi_GetPosition( (void *)arg_sessionId, &position );

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
    guint64 arg_sessionId )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    float duration = 0;
    cgmi_SessionType type = 0;

    CGMID_ENTER();

    retStat = cgmi_GetDuration( (void *)arg_sessionId, &duration, &type );

    org_cisco_cgmi_complete_get_duration (object,
                                          invocation,
                                          duration,
                                          type,
                                          retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_get_rate_range (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    guint64 arg_sessionId )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    float rewindRate = 0;
    float fForwardRate = 0;

    CGMID_ENTER();

    retStat = cgmi_GetRateRange( (void *)arg_sessionId, &rewindRate, &fForwardRate );

    org_cisco_cgmi_complete_get_rate_range (object,
                                            invocation,
                                            rewindRate,
                                            fForwardRate,
                                            retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_get_num_audio_languages (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    guint64 arg_sessionId )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    gint count = 0;

    CGMID_ENTER();

    retStat = cgmi_GetNumAudioLanguages( (void *)arg_sessionId, &count );

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
    guint64 arg_sessionId,
    gint index,
    gint bufSize )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    GVariantBuilder *bufferBuilder = NULL;
    int idx;
    char *buffer = NULL;

    CGMID_ENTER();

    if ( bufSize > 0 )
    {
        buffer = g_malloc0(bufSize);        
    }
    
    if ( NULL == buffer )
        retStat = CGMI_ERROR_OUT_OF_MEMORY;    
    else
        retStat = cgmi_GetAudioLangInfo( (void *)arg_sessionId, index, buffer, bufSize );
       
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
    guint64 arg_sessionId,
    gint index )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;

    CGMID_ENTER();

    retStat = cgmi_SetAudioStream( (void *)arg_sessionId, index );

    org_cisco_cgmi_complete_set_audio_stream (object,
            invocation,
            retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_set_default_audio_lang (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    guint64 arg_sessionId,
    const char *language )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;

    CGMID_ENTER();

    retStat = cgmi_SetDefaultAudioLang( (void *)arg_sessionId, language );

    org_cisco_cgmi_complete_set_default_audio_lang (object,
            invocation,
            retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_create_section_filter (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    guint64 arg_sessionId )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
    void *pFilterId;

    CGMID_ENTER();

    // Provide a pointer to the sessionId as the private data.
    retStat = cgmi_CreateSectionFilter( (void *)arg_sessionId,
                                     (void *)object,
                                     &pFilterId );

    org_cisco_cgmi_complete_create_section_filter (object,
            invocation,
            (guint64)pFilterId,
            retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_destroy_section_filter (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    guint64 arg_sessionId,
    guint64 arg_filterId )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;

    CGMID_ENTER();

    retStat = cgmi_DestroySectionFilter( (void *)arg_sessionId,
                                      (void *)arg_filterId );

    org_cisco_cgmi_complete_destroy_section_filter (object,
            invocation,
            retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_set_section_filter (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    guint64 arg_sessionId,
    guint64 arg_filterId,
    gint arg_filterPid,
    GVariant *arg_filterValue,
    GVariant *arg_filterMask,
    gint arg_filterLength,
    guint arg_filterOffset,
    gint arg_filterComparitor )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;
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
        pFilter.pid = arg_filterPid;
        pFilter.value = arg_filterValue;
        pFilter.mask = arg_filterMask;
        pFilter.length = arg_filterLength;
        pFilter.offset = arg_filterOffset;
        pFilter.comparitor = arg_filterComparitor;

        retStat = cgmi_SetSectionFilter( (void *)arg_sessionId,
                                      (void *)arg_filterId, 
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
    guint64 arg_sessionId,
    guint64 arg_filterId,
    gint timeout,
    gint oneShot,
    gint enableCRC )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;

    CGMID_ENTER();

    //TODO handle callbacks

    retStat = cgmi_StartSectionFilter( (void *)arg_sessionId,
                                    (void *)arg_filterId,
                                    timeout,
                                    oneShot,
                                    enableCRC,
                                    cgmiQueryBufferCallback,
                                    cgmiSectionBufferCallback );

    org_cisco_cgmi_complete_start_section_filter (object,
            invocation,
            retStat);

    return TRUE;
}

static gboolean
on_handle_cgmi_stop_section_filter (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    const guint64 arg_sessionId,
    const guint64 arg_filterId )
{
    cgmi_Status retStat = CGMI_ERROR_FAILED;

    CGMID_ENTER();

    //TODO handle callbacks

    retStat = cgmi_StopSectionFilter( (void *)arg_sessionId,
                                   (void *)arg_filterId );

    org_cisco_cgmi_complete_stop_section_filter (object,
            invocation,
            retStat);

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
                      "handle-get-rate-range",
                      G_CALLBACK (on_handle_cgmi_get_rate_range),
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
// Entry point for DBUS daemon
////////////////////////////////////////////////////////////////////////////////
int main( int argc, char *argv[] )
{
    int c = 0;
    GMainLoop *loop;
    guint id;

    /* we are using printf so disable buffering */
    setbuf(stdout, NULL);

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
            printf( "Invalid option (%c).\n", c );
        }
    }

    /* Fork into background depending on args provided */
    if ( gInForeground == FALSE )
    {
        /* Setup syslog when in background */
        openlog( DAEMON_NAME, LOG_CONS | LOG_PID | LOG_NDELAY, LOG_DAEMON );
        g_set_print_handler(gPrintToSylog);
        g_set_printerr_handler(gPrintErrToSylog);

        if ( 0 != daemon( 0, 0 ) )
        {
            CGMID_ERROR( "Failed to fork background daemon. Abort." );
            return errno;
        }
    }

    //rms put in to a #define diagInit (DIAGTYPE_DEFAULT, NULL, 0);
    /* DBUS Code */
    loop = g_main_loop_new( NULL, FALSE );

    g_type_init();

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

    return 0;
}
