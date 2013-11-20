#include <glib.h>
#include <stdio.h>
#include <string.h>

#include "cgmiPlayerApi.h"


////////////////////////////////////////////////////////////////////////////////
// Logging stuff
////////////////////////////////////////////////////////////////////////////////

#define CHECK_ERROR(err) \
    if( err != CGMI_ERROR_SUCCESS ) \
    { \
        g_print("CGMI_CLIENT_TEST %s:%d - %s :: Failed with %s \n", __FILE__, __LINE__, __FUNCTION__, cgmi_ErrorString(err) ); \
    }

#define CHECK_ERROR_RETURN(err) \
    CHECK_ERROR(err); \
    if( err != CGMI_ERROR_SUCCESS ) return;


/**
 * CGMI DBUS callback.
 */
static void cgmiCallback( void *pUserData, void *pSession, tcgmi_Event event )
{
    g_print( "CGMI Player Event Recevied : %d \n", event );
}

/**
 * A quick sanity test of the DBUS apis
 */
static gpointer sanity(gpointer user_data)
{
    cgmi_Status stat = CGMI_ERROR_SUCCESS;
    void *pSessionId;

    char *url = (char *)user_data;

    /****** Start playback *******/
    g_print("Calling cgmi_Init...\n");
    stat = cgmi_Init();
    CHECK_ERROR(stat);

    g_print("Calling cgmi_CreateSession...\n");
    stat = cgmi_CreateSession( cgmiCallback, NULL, &pSessionId );
    CHECK_ERROR(stat);
    g_print("create session returned sessionId = (%lx)\n", (guint64)pSessionId);

    g_print("Calling cgmi_Load...\n");
    stat = cgmi_Load( pSessionId, url );
    CHECK_ERROR(stat);

    g_print("Calling cgmi_Play...\n");
    stat = cgmi_Play( pSessionId );
    CHECK_ERROR(stat);


    /****** Call all other API's after starting playback *******/
    int bCanPlay = 0  ;
    g_print("Calling cgmi_canPlayType...\n");
    stat = cgmi_canPlayType( "fakeType", &bCanPlay );
    CHECK_ERROR(stat);
    g_print("cgmi_canPlayType : bCanPlay = (%d)\n", bCanPlay);


    g_print("Calling cgmi_SetRate (pause then resume)...\n");
    stat = cgmi_SetRate( pSessionId, 0.0 );
    CHECK_ERROR(stat);
    g_usleep(4 * 1000 * 1000);
    stat = cgmi_SetRate( pSessionId, 1.0 );
    CHECK_ERROR(stat);


    g_print("Calling cgmi_SetPosition...\n");
    stat = cgmi_SetPosition( pSessionId, 0.0 );
    CHECK_ERROR(stat);


    float curPosition;
    g_print("Calling cgmi_GetPosition...\n");
    stat = cgmi_GetPosition( pSessionId, &curPosition );
    CHECK_ERROR(stat);
    g_print("cgmi_GetPosition : curPosition = (%f)\n", curPosition);


    float duration;
    cgmi_SessionType type;

    g_print("Calling cgmi_GetDuration...\n");
    stat = cgmi_GetDuration( pSessionId, &duration, &type );
    CHECK_ERROR(stat);
    g_print("cgmi_GetDuration : duration = (%f), type = (%d)\n", duration, type);


    float rewindSpeed, fastForwardSpeed;
    g_print("Calling cgmi_GetRateRange...\n");
    stat = cgmi_GetRateRange( pSessionId, &rewindSpeed, &fastForwardSpeed );
    CHECK_ERROR(stat);
    g_print("cgmi_GetRateRange : rewindSpeed = (%f), fastForwardSpeed = (%f)\n",
            rewindSpeed, fastForwardSpeed);


    int count;
    g_print("Calling cgmi_GetNumAudioStreams...\n");
    stat = cgmi_GetNumAudioStreams( pSessionId, &count );
    CHECK_ERROR(stat);
    g_print("cgmi_GetNumAudioStreams : count = (%d)\n", count );


    char streamInfoBuf[1024];
    g_print("Calling cgmi_GetAudioStreamInfo...\n");
    stat = cgmi_GetAudioStreamInfo( pSessionId, 0, streamInfoBuf, 1024 );
    CHECK_ERROR(stat);


    g_print("Calling cgmi_SetAudioStream...\n");
    stat = cgmi_SetAudioStream( pSessionId, 0 );
    CHECK_ERROR(stat);


    g_print("Calling cgmi_SetDefaultAudioLang...\n");
    stat = cgmi_SetDefaultAudioLang( pSessionId, "eng" );
    CHECK_ERROR(stat);


    void *filterId;
    g_print("Calling cgmi_CreateSectionFilter...\n");
    stat = cgmi_CreateSectionFilter( pSessionId, NULL, &filterId );
    CHECK_ERROR(stat);


    tcgmi_FilterData filterData;
    g_print("Calling cgmi_SetSectionFilter...\n");
    stat = cgmi_SetSectionFilter( pSessionId, filterId, &filterData );
    CHECK_ERROR(stat);


    g_print("Calling cgmi_StartSectionFilter...\n");
    stat = cgmi_StartSectionFilter( pSessionId, filterId, 10, 1, 0, NULL, NULL );
    CHECK_ERROR(stat);

    // Let it section filter for a few seconds :)
    g_usleep(2 * 1000 * 1000);

    g_print("Calling cgmi_StopSectionFilter...\n");
    stat = cgmi_StopSectionFilter( pSessionId, filterId );
    CHECK_ERROR(stat);


    g_print("Calling cgmi_DestroySectionFilter...\n");
    stat = cgmi_DestroySectionFilter( pSessionId, &filterId );
    CHECK_ERROR(stat);


    // Let it play for a few more seconds
    g_usleep(10 * 1000 * 1000);


    /****** Shut down session and clean up *******/
    g_print("Calling cgmi_Unload...\n");
    stat = cgmi_Unload( pSessionId );
    CHECK_ERROR(stat);

    g_print("Calling cgmi_DestroySession...\n");
    stat = cgmi_DestroySession( pSessionId );
    CHECK_ERROR(stat);

    g_print("Calling cgmi_Term...\n");
    stat = cgmi_Term();
    CHECK_ERROR(stat);

    return NULL;
}

/**
 * Make all the calls to start playing video
 */
static gpointer play( char *uri )
{
    cgmi_Status stat = CGMI_ERROR_SUCCESS;
    void *pSessionId;

    g_print("Calling cgmi_Init...\n");
    stat = cgmi_Init();
    CHECK_ERROR(stat);

    g_print("Calling cgmi_CreateSession...\n");
    stat = cgmi_CreateSession( cgmiCallback, NULL, &pSessionId );
    CHECK_ERROR(stat);

    g_print("Calling cgmi_Load...\n");
    stat = cgmi_Load( pSessionId, uri );
    CHECK_ERROR(stat);

    g_print("Calling cgmi_Play...\n");
    stat = cgmi_Play( pSessionId );
    CHECK_ERROR(stat);

    // Sleep for a moment to catch any signals
    g_usleep(500 * 1000);

    return NULL;
}

int main(int argc, char **argv)
{

    if (argc < 2)
    {
        printf("usage: %s play <url> | sanity <url>\n", argv[0]);
        return -1;
    }

    if (strcmp(argv[1], "sanity") == 0)
    {
        if (argc < 3)
        {
            printf("usage: %s sanity <url>\n", argv[0]);
            return -1;
        }
        sanity(argv[2]);

    }
    else if (strcmp(argv[1], "play") == 0)
    {
        if (argc < 3)
        {
            printf("usage: %s play <url>\n", argv[0]);
            return -1;
        }
        play(argv[2]);
    }

    return 0;
}
