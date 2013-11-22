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
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    void *pSessionId;

    char *url = (char *)user_data;

    /****** Start playback *******/
    g_print("Calling cgmi_Init...\n");
    retStat = cgmi_Init();
    CHECK_ERROR(retStat);

    g_print("Calling cgmi_CreateSession...\n");
    retStat = cgmi_CreateSession( cgmiCallback, NULL, &pSessionId );
    CHECK_ERROR(retStat);
    g_print("create session returned sessionId = (%lx)\n", (guint64)pSessionId);

    g_print("Calling cgmi_Load...\n");
    retStat = cgmi_Load( pSessionId, url );
    CHECK_ERROR(retStat);

    g_print("Calling cgmi_Play...\n");
    retStat = cgmi_Play( pSessionId );
    CHECK_ERROR(retStat);


    /****** Call all other API's after starting playback *******/
    int bCanPlay = 0  ;
    g_print("Calling cgmi_canPlayType...\n");
    retStat = cgmi_canPlayType( "fakeType", &bCanPlay );
    CHECK_ERROR(retStat);
    g_print("cgmi_canPlayType : bCanPlay = (%d)\n", bCanPlay);

    /*

    g_print("Calling cgmi_SetRate (pause then resume)...\n");
    retStat = cgmi_SetRate( pSessionId, 0.0 );
    CHECK_ERROR(retStat);
    g_usleep(4 * 1000 * 1000);
    retStat = cgmi_SetRate( pSessionId, 1.0 );
    CHECK_ERROR(retStat);


    g_print("Calling cgmi_SetPosition...\n");
    retStat = cgmi_SetPosition( pSessionId, 0.0 );
    CHECK_ERROR(retStat);

    */

    float curPosition;
    g_print("Calling cgmi_GetPosition...\n");
    retStat = cgmi_GetPosition( pSessionId, &curPosition );
    CHECK_ERROR(retStat);
    g_print("cgmi_GetPosition : curPosition = (%f)\n", curPosition);


    float duration;
    cgmi_SessionType type;

    g_print("Calling cgmi_GetDuration...\n");
    retStat = cgmi_GetDuration( pSessionId, &duration, &type );
    CHECK_ERROR(retStat);
    g_print("cgmi_GetDuration : duration = (%f), type = (%d)\n", duration, type);


    float rewindSpeed, fastForwardSpeed;
    g_print("Calling cgmi_GetRateRange...\n");
    retStat = cgmi_GetRateRange( pSessionId, &rewindSpeed, &fastForwardSpeed );
    CHECK_ERROR(retStat);
    g_print("cgmi_GetRateRange : rewindSpeed = (%f), fastForwardSpeed = (%f)\n",
            rewindSpeed, fastForwardSpeed);


    int count;
    g_print("Calling cgmi_GetNumAudioStreams...\n");
    retStat = cgmi_GetNumAudioStreams( pSessionId, &count );
    CHECK_ERROR(retStat);
    g_print("cgmi_GetNumAudioStreams : count = (%d)\n", count );


    char streamInfoBuf[1024];
    g_print("Calling cgmi_GetAudioStreamInfo...\n");
    retStat = cgmi_GetAudioStreamInfo( pSessionId, 0, streamInfoBuf, 1024 );
    CHECK_ERROR(retStat);


    g_print("Calling cgmi_SetAudioStream...\n");
    retStat = cgmi_SetAudioStream( pSessionId, 0 );
    CHECK_ERROR(retStat);


    g_print("Calling cgmi_SetDefaultAudioLang...\n");
    retStat = cgmi_SetDefaultAudioLang( pSessionId, "eng" );
    CHECK_ERROR(retStat);

    // Let it play for a few more seconds
    g_usleep(1 * 1000 * 1000);

    /* Create section filter */
    void *filterId;
    g_print("Calling cgmi_CreateSectionFilter...\n");
    retStat = cgmi_CreateSectionFilter( pSessionId, NULL, &filterId );
    CHECK_ERROR(retStat);


    tcgmi_FilterData filterData;
    filterData.pid = 0;
    filterData.value = NULL;
    filterData.mask = NULL;
    filterData.length = 0;
    filterData.offset = 0;
    filterData.comparitor = FILTER_COMP_EQUAL;

    g_print("Calling cgmi_SetSectionFilter...\n");
    retStat = cgmi_SetSectionFilter( pSessionId, filterId, &filterData );
    CHECK_ERROR(retStat);


    g_print("Calling cgmi_StartSectionFilter...\n");
    retStat = cgmi_StartSectionFilter( pSessionId, filterId, 10, 1, 0, NULL, NULL );
    CHECK_ERROR(retStat);

    // Let it section filter for a few seconds :)
    g_usleep(10 * 1000 * 1000);

    g_print("Calling cgmi_StopSectionFilter...\n");
    retStat = cgmi_StopSectionFilter( pSessionId, filterId );
    CHECK_ERROR(retStat);


    g_print("Calling cgmi_DestroySectionFilter...\n");
    retStat = cgmi_DestroySectionFilter( pSessionId, &filterId );
    CHECK_ERROR(retStat);
    
    // */

    // Let it play for a few more seconds
    g_usleep(10 * 1000 * 1000);


    /****** Shut down session and clean up *******/
    g_print("Calling cgmi_Unload...\n");
    retStat = cgmi_Unload( pSessionId );
    CHECK_ERROR(retStat);

    g_print("Calling cgmi_DestroySession...\n");
    retStat = cgmi_DestroySession( pSessionId );
    CHECK_ERROR(retStat);

    g_print("Calling cgmi_Term...\n");
    retStat = cgmi_Term();
    CHECK_ERROR(retStat);

    return NULL;
}

/**
 * Make all the calls to start playing video
 */
static gpointer play( char *uri )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    void *pSessionId;

    g_print("Calling cgmi_Init...\n");
    retStat = cgmi_Init();
    CHECK_ERROR(retStat);

    g_print("Calling cgmi_CreateSession...\n");
    retStat = cgmi_CreateSession( cgmiCallback, NULL, &pSessionId );
    CHECK_ERROR(retStat);

    g_print("Calling cgmi_Load...\n");
    retStat = cgmi_Load( pSessionId, uri );
    CHECK_ERROR(retStat);

    g_print("Calling cgmi_Play...\n");
    retStat = cgmi_Play( pSessionId );
    CHECK_ERROR(retStat);

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
