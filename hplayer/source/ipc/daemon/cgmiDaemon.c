#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <glib.h>

#include "cgmiPlayerApi.h"
#include "cgmi_dbus_server_generated.h"


////////////////////////////////////////////////////////////////////////////////
// Logging for daemon.  TODO:  Send to syslog when in background
////////////////////////////////////////////////////////////////////////////////
#define LOGGING_ENABLED

#ifdef LOGGING_ENABLED
#define LOG_LEVEL(level, ...) g_print("CGMI_DAEMON %s:%d - %s %s :: ", \
    __FILE__, __LINE__, __FUNCTION__, level); g_print(__VA_ARGS__)
#define LOG_TRACE_ENTER()    g_print("%s:%d - %s >>>> Enter\n", \
    __FILE__, __LINE__, __FUNCTION__);

#else
#define LOG_LEVEL(level, ...)
#define LOG_TRACE_ENTER()
#endif

#define LOG_INFO(...)  LOG_LEVEL("INFO", __VA_ARGS_)
#define LOG_ERROR(...) LOG_LEVEL("ERROR", __VA_ARGS_)



////////////////////////////////////////////////////////////////////////////////
// Callbacks called by CGMI core to message client via DBUS
////////////////////////////////////////////////////////////////////////////////
void cgmiEventCallback( void *pUserData, void *pSession, tcgmi_Event event )
{

    org_cisco_cgmi_emit_player_notify( (OrgCiscoCgmi *) pUserData,
                                       (guint64)pSession, 
                                       event,
                                       0 );

    g_print("cgmiEventCallback -- pSession: %lu, event%d \n", 
        (guint64)pSession, event);
}

static cgmi_Status cgmiQueryBufferCallback(void *pUserData, void *pFilterPriv, 
    void* pFilterId, char **ppBuffer, int* pBufferSize )
{

    org_cisco_cgmi_emit_query_buffer_notify( (OrgCiscoCgmi *) pUserData,
                                       0,
                                       (guint64)pFilterPriv,
                                       (guint64)pFilterId);

    g_print("cgmiQueryBufferCallback -- pFilterId: %lu, pFilterPriv%lu \n", 
        (guint64)pFilterId, (guint64)pFilterPriv);
}

static cgmi_Status cgmiSectionBufferCallback(void *pUserData, 
    void *pFilterPriv, 
    void* pFilterId, 
    cgmi_Status sectionStatus, 
    const char *pSection, 
    int sectionSize)
{

    org_cisco_cgmi_emit_section_buffer_notify( (OrgCiscoCgmi *) pUserData,
                                        0,
                                       (guint64)pFilterPriv, 
                                       (guint64)pFilterId,
                                       (gint)sectionStatus,
                                       pSection,
                                       sectionSize );

    g_print("cgmiSectionBufferCallback -- pFilterId: %lu, pFilterPriv%lu \n", 
        (guint64)pFilterId, (guint64)pFilterPriv);

}

////////////////////////////////////////////////////////////////////////////////
// CGMI Player APIs
////////////////////////////////////////////////////////////////////////////////
static gboolean
on_handle_cgmi_init (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation )
{
    cgmi_Status stat = CGMI_ERROR_FAILED;

    LOG_TRACE_ENTER();

    stat = cgmi_Init( );

    org_cisco_cgmi_complete_init (object,
                                    invocation,
                                    stat);

   return TRUE;
}

static gboolean
on_handle_cgmi_term (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation )
{
    cgmi_Status stat = CGMI_ERROR_FAILED;

    LOG_TRACE_ENTER();

    stat = cgmi_Term( );

    org_cisco_cgmi_complete_term (object,
                                    invocation,
                                    stat);

   return TRUE;
}

static gboolean
on_handle_cgmi_create_session (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation )
{
    cgmi_Status stat = CGMI_ERROR_FAILED;
    void *pSessionId = NULL;

    LOG_TRACE_ENTER();

    stat = cgmi_CreateSession( cgmiEventCallback, (void *)object, &pSessionId );

    org_cisco_cgmi_complete_create_session (object,
                                    invocation,
                                    (guint64)pSessionId,
                                    stat);

   return TRUE;
}

static gboolean
on_handle_cgmi_destroy_session (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    const guint64 arg_sessionId )
{
    cgmi_Status stat = CGMI_ERROR_FAILED;

    LOG_TRACE_ENTER();

    stat = cgmi_DestroySession( (void *)arg_sessionId );

    org_cisco_cgmi_complete_destroy_session (object,
                                    invocation,
                                    stat);

   return TRUE;
}

static gboolean
on_handle_cgmi_can_play_type (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    const gchar *arg_type )
{
    cgmi_Status stat = CGMI_ERROR_FAILED;
    gint bCanPlay = 0;

    LOG_TRACE_ENTER();

    stat = cgmi_canPlayType( arg_type, &bCanPlay );

    org_cisco_cgmi_complete_can_play_type (object,
                                    invocation,
                                    bCanPlay,
                                    stat);

   return TRUE;
}

static gboolean
on_handle_cgmi_load (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    const guint64 arg_sessionId,
    const gchar *uri )
{
    cgmi_Status stat = CGMI_ERROR_FAILED;

    LOG_TRACE_ENTER();

    stat = cgmi_Load( (void *)arg_sessionId, uri );

    org_cisco_cgmi_complete_load (object,
                                    invocation,
                                    stat);

   return TRUE;
}

static gboolean
on_handle_cgmi_unload (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    const guint64 arg_sessionId )
{
    cgmi_Status stat = CGMI_ERROR_FAILED;

    LOG_TRACE_ENTER();

    stat = cgmi_Unload( (void *)arg_sessionId );

    org_cisco_cgmi_complete_load (object,
                                    invocation,
                                    stat);

   return TRUE;
}

static gboolean
on_handle_cgmi_play (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    const guint64 arg_sessionId )
{
    cgmi_Status stat = CGMI_ERROR_FAILED;

    LOG_TRACE_ENTER();

    stat = cgmi_Play( (void *)arg_sessionId );

    org_cisco_cgmi_complete_play (object,
                                    invocation,
                                    stat);

   return TRUE;
}

static gboolean
on_handle_cgmi_set_rate (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    const guint64 arg_sessionId,
    const gdouble arg_rate )
{
    cgmi_Status stat = CGMI_ERROR_FAILED;

    LOG_TRACE_ENTER();

    stat = cgmi_SetRate( (void *)arg_sessionId, arg_rate );

    org_cisco_cgmi_complete_set_rate (object,
                                    invocation,
                                    stat);

   return TRUE;
}

static gboolean
on_handle_cgmi_set_position (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    const guint64 arg_sessionId,
    const gdouble arg_position )
{
    cgmi_Status stat = CGMI_ERROR_FAILED;

    LOG_TRACE_ENTER();

    stat = cgmi_SetPosition( (void *)arg_sessionId, arg_position );

    org_cisco_cgmi_complete_set_position (object,
                                    invocation,
                                    stat);

   return TRUE;
}

static gboolean
on_handle_cgmi_get_position (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    const guint64 arg_sessionId )
{
    cgmi_Status stat = CGMI_ERROR_FAILED;
    float position = 0;

    LOG_TRACE_ENTER();

    stat = cgmi_GetPosition( (void *)arg_sessionId, &position );

    org_cisco_cgmi_complete_get_position (object,
                                    invocation,
                                    position,
                                    stat);

   return TRUE;
}

static gboolean
on_handle_cgmi_get_duration (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    const guint64 arg_sessionId,
    const gint arg_type )
{
    cgmi_Status stat = CGMI_ERROR_FAILED;
    float duration = 0;

    LOG_TRACE_ENTER();

    // MZW - disabled until the API is updated
    //stat = cgmi_GetDuration( (void *)arg_sessionId, &duration, arg_type );

    org_cisco_cgmi_complete_get_duration (object,
                                    invocation,
                                    duration,
                                    stat);

   return TRUE;
}

static gboolean
on_handle_cgmi_get_rate_range (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    const guint64 arg_sessionId )
{
    cgmi_Status stat = CGMI_ERROR_FAILED;
    float rewindRate = 0;
    float fForwardRate = 0;

    LOG_TRACE_ENTER();

    stat = cgmi_GetRateRange( (void *)arg_sessionId, &rewindRate, &fForwardRate );

    org_cisco_cgmi_complete_get_rate_range (object,
                                    invocation,
                                    rewindRate,
                                    fForwardRate,
                                    stat);

   return TRUE;
}

static gboolean
on_handle_cgmi_get_num_audio_streams (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    const guint64 arg_sessionId )
{
    cgmi_Status stat = CGMI_ERROR_FAILED;
    int count = 0;

    LOG_TRACE_ENTER();

    stat = cgmi_GetNumAudioStreams( (void *)arg_sessionId, &count );

    org_cisco_cgmi_complete_get_num_audio_streams (object,
                                    invocation,
                                    count,
                                    stat);

   return TRUE;
}

static gboolean
on_handle_cgmi_get_audio_stream_info (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    const guint64 arg_sessionId,
    const gint index,
    const gint bufSize )
{
    cgmi_Status stat = CGMI_ERROR_FAILED;
    char buffer[bufSize];

    LOG_TRACE_ENTER();

    stat = cgmi_GetAudioStreamInfo( (void *)arg_sessionId, index, buffer, bufSize );

    org_cisco_cgmi_complete_get_audio_stream_info (object,
                                    invocation,
                                    buffer,
                                    stat);

   return TRUE;
}

static gboolean
on_handle_cgmi_set_audio_stream (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    const guint64 arg_sessionId,
    const gint index )
{
    cgmi_Status stat = CGMI_ERROR_FAILED;

    LOG_TRACE_ENTER();

    stat = cgmi_SetAudioStream( (void *)arg_sessionId, index );

    org_cisco_cgmi_complete_set_audio_stream (object,
                                    invocation,
                                    stat);

   return TRUE;
}

static gboolean
on_handle_cgmi_set_default_audio_lang (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    const guint64 arg_sessionId,
    const char *language )
{
    cgmi_Status stat = CGMI_ERROR_FAILED;

    LOG_TRACE_ENTER();

    stat = cgmi_SetDefaultAudioLang( (void *)arg_sessionId, language );

    org_cisco_cgmi_complete_set_audio_stream (object,
                                    invocation,
                                    stat);

   return TRUE;
}

static gboolean
on_handle_cgmi_create_section_filter (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    const guint64 arg_sessionId,
    const guint64 arg_filterPriv )
{
    cgmi_Status stat = CGMI_ERROR_FAILED;
    void* pFilterId;

    LOG_TRACE_ENTER();

    stat = cgmi_CreateSectionFilter( (void *)arg_sessionId, 
        (void *)arg_filterPriv, 
        &pFilterId );

    org_cisco_cgmi_complete_create_section_filter (object,
                                    invocation,
                                    (guint64)pFilterId,
                                    stat);

   return TRUE;
}

static gboolean
on_handle_cgmi_destroy_section_filter (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    const guint64 arg_sessionId,
    const guint64 arg_filterId )
{
    cgmi_Status stat = CGMI_ERROR_FAILED;

    LOG_TRACE_ENTER();

    stat = cgmi_DestroySectionFilter( (void *)arg_sessionId, 
        (void *)arg_filterId );

    org_cisco_cgmi_complete_destroy_section_filter (object,
                                    invocation,
                                    stat);

   return TRUE;
}

static gboolean
on_handle_cgmi_set_section_filter (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    const guint64 arg_sessionId,
    const guint64 arg_filterId,
    GVariant *filter )
{
    cgmi_Status stat = CGMI_ERROR_FAILED;

    LOG_TRACE_ENTER();

    // TODO:  Update to match tcgmi_FilterData data structure

    stat = cgmi_SetSectionFilter( (void *)arg_sessionId, 
        (void *)arg_filterId, NULL );

    org_cisco_cgmi_complete_set_section_filter (object,
                                    invocation,
                                    stat);

   return TRUE;
}

static gboolean
on_handle_cgmi_start_section_filter (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    const guint64 arg_sessionId,
    const guint64 arg_filterId,
    const gint timeout,
    const gint oneShot,
    const gint enableCRC )
{
    cgmi_Status stat = CGMI_ERROR_FAILED;

    LOG_TRACE_ENTER();

    //TODO handle callbacks

    stat = cgmi_StartSectionFilter( (void *)arg_sessionId, 
        (void *)arg_filterId,
        timeout,
        oneShot,
        enableCRC,
        cgmiQueryBufferCallback,
        cgmiSectionBufferCallback );

    org_cisco_cgmi_complete_start_section_filter (object,
                                    invocation,
                                    stat);

   return TRUE;
}

static gboolean
on_handle_cgmi_stop_section_filter (
    OrgCiscoCgmi *object,
    GDBusMethodInvocation *invocation,
    const guint64 arg_sessionId,
    const guint64 arg_filterId )
{
    cgmi_Status stat = CGMI_ERROR_FAILED;

    LOG_TRACE_ENTER();

    //TODO handle callbacks

    stat = cgmi_StopSectionFilter( (void *)arg_sessionId, 
        (void *)arg_filterId );

    org_cisco_cgmi_complete_start_section_filter (object,
                                    invocation,
                                    stat);

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
    g_print("Acquired the name %s\n", name);
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
    g_print("Lost the name %s\n", name);
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
    GError *error = NULL;
    OrgCiscoCgmi *interface = org_cisco_cgmi_skeleton_new();

    if( NULL == interface )
    {
        g_print("org_cisco_dbustest_skeleton_new() FAILED.\n");
        return;
    }

    org_cisco_cgmi_set_verbose (interface, TRUE);

    g_print("bus acquired\n");

    g_signal_connect (interface,
                      "handle-init",
                      G_CALLBACK (on_handle_cgmi_init),
                      NULL);

    g_signal_connect (interface,
                      "handle-term",
                      G_CALLBACK (on_handle_cgmi_term),
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
                      "handle-get-num-audio-streams",
                      G_CALLBACK (on_handle_cgmi_get_num_audio_streams),
                      NULL);

    g_signal_connect (interface,
                      "handle-get-audio-stream-info",
                      G_CALLBACK (on_handle_cgmi_get_audio_stream_info),
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
        g_print( "Failed in g_dbus_interface_skeleton_export.\n" );
    }
}

////////////////////////////////////////////////////////////////////////////////
// Entry point for DBUS daemon
////////////////////////////////////////////////////////////////////////////////
int main( int argc, char *argv[] )
{
    gboolean foreground = FALSE;
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
            foreground = TRUE;
            break;
        default:
            g_print( "Invalid option (%c).\n", c );
        }
    }

    /* Fork into background depending on args provided */
    if ( foreground == FALSE )
    {
        if( 0 != daemon( 0, 0 ) )
        {
            g_print( "Failed to fork background daemon. Abort." );
            return errno;
        }
    }

    //TODO: put the gstreamer system init here.


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

    g_bus_unown_name( id );
    g_main_loop_unref( loop );


    //TODO: shutdown gstreamer here.

    return 0;
}
