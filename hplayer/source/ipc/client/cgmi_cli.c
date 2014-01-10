/* Includes */
#include <glib.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <stdbool.h>

// put this in a define. #include "diaglib.h"
#include <sys/time.h>

#include "cgmiPlayerApi.h"

/* Defines for section filtering. */
#define MAX_PMT_COUNT 20

#define MAX_COMMAND_LENGTH 512
#define MAX_HISTORY 50

static void *filterid = NULL;
static bool filterRunning = false;
static struct termios oldt, newt;

/* Prototypes */
static void cgmiCallback( void *pUserData, void *pSession, tcgmi_Event event );
static cgmi_Status destroyfilter( void *pSessionId );

/* Signal Handler */
void sig_handler(int signum)
{
    tcsetattr( STDIN_FILENO, TCSANOW, &oldt);
    printf("\n\nNext time you may want to use Ctrl+D to exit correctly. :-)\n");
    exit(1);
}

/* Play Command */
static cgmi_Status play(void *pSessionId, char *src)
{   
    cgmi_Status retCode = CGMI_ERROR_SUCCESS;

    /* First load the URL. */
    retCode = cgmi_Load( pSessionId, src );
    if (retCode != CGMI_ERROR_SUCCESS)
    {
        printf("CGMI Load failed\n");
    } else {
        /* Play the URL if load succeeds. */
        retCode = cgmi_Play( pSessionId );
        if (retCode != CGMI_ERROR_SUCCESS)
        {
            printf("CGMI Play failed\n");
        }
    }

    return retCode;
}

/* Stop Command */
static cgmi_Status stop(void *pSessionId)
{
    cgmi_Status retCode = CGMI_ERROR_SUCCESS;

    /* Destroy the section filter */
    retCode = destroyfilter(pSessionId);

    /* Stop = Unload */
    retCode = cgmi_Unload( pSessionId );
    if (retCode != CGMI_ERROR_SUCCESS)
    {
        printf("CGMI Unload failed\n");
    }

    return retCode;
}

/* GetPosition Command */
static cgmi_Status getposition(void *pSessionId, float *pPosition)
{
    cgmi_Status retCode = CGMI_ERROR_SUCCESS;

    retCode = cgmi_GetPosition( pSessionId, pPosition );
    if (retCode != CGMI_ERROR_SUCCESS)
    {
        printf("CGMI GetPosition Failed\n");
    }

    return retCode;
}

/* SetPosition Command */
static cgmi_Status setposition(void *pSessionId, float Position)
{
    cgmi_Status retCode = CGMI_ERROR_SUCCESS;

    retCode = cgmi_SetPosition( pSessionId, Position );
    if (retCode != CGMI_ERROR_SUCCESS)
    {
        printf("CGMI SetRate Failed\n");
    }

    return retCode;
}

/* GetRateRange Command */
static cgmi_Status getraterange(void *pSessionId, float *pRewind, float *pFFoward)
{
    cgmi_Status retCode = CGMI_ERROR_SUCCESS;

    retCode = cgmi_GetRateRange( pSessionId, pRewind, pFFoward );
    if (retCode != CGMI_ERROR_SUCCESS)
    {
        printf("CGMI GetRateRange Failed\n");
    }

    return retCode;
}

/* SetRate Command */
static cgmi_Status setrate(void *pSessionId, float Rate)
{
    cgmi_Status retCode = CGMI_ERROR_SUCCESS;

    retCode = cgmi_SetRate( pSessionId, Rate );
    if (retCode != CGMI_ERROR_SUCCESS)
    {
        printf("CGMI SetRate Failed\n");
    }

    return retCode;
}

/* GetDuration Command */
static cgmi_Status getduration(void *pSessionId, float *pDuration, cgmi_SessionType *type)
{
    cgmi_Status retCode = CGMI_ERROR_SUCCESS;

    retCode = cgmi_GetDuration( pSessionId, pDuration, type );
    if (retCode != CGMI_ERROR_SUCCESS)
    {
        printf("CGMI GetDuration Failed\n");
    }

    return retCode;
}

/****************************  Section Filter Callbacks  *******************************/
static cgmi_Status cgmi_QueryBufferCallback(
    void *pUserData,
    void *pFilterPriv,
    void *pFilterId,
    char **ppBuffer,
    int *pBufferSize )
{
    //g_print( "cgmi_QueryBufferCallback -- pFilterId: 0x%08lx \n", pFilterId );


    // Preconditions
    if( NULL == ppBuffer )
    {
        g_print("NULL buffer pointer passed to cgmiQueryBufferCallback.\n");
        return CGMI_ERROR_BAD_PARAM;
    }

    // Check if a size of greater than zero was provided, use default if not
    if( *pBufferSize <= 0 )
    {
        *pBufferSize = 256;
    }

    //g_print("Allocating buffer of size (%d)\n", *pBufferSize);
    *ppBuffer = g_malloc0(*pBufferSize);

    if( NULL == *ppBuffer )
    {
        *pBufferSize = 0;
        return CGMI_ERROR_OUT_OF_MEMORY;
    }

    return CGMI_ERROR_SUCCESS;
}

static cgmi_Status cgmi_SectionBufferCallback(
    void *pUserData,
    void *pFilterPriv,
    void *pFilterId,
    cgmi_Status sectionStatus,
    char *pSection,
    int sectionSize)
{
    cgmi_Status retStat;
    //tPat pat;
    int i = 0;

    //g_print( "cgmi_QueryBufferCallback -- pFilterId: 0x%08lx \n", pFilterId );

    if( NULL == pSection )
    {
        g_print("NULL buffer passed to cgmiSectionBufferCallback.\n");
        return CGMI_ERROR_BAD_PARAM;
    }

    g_print("Received section pFilterId: %p, sectionSize %d\n\n",
            pFilterId, sectionSize);
    for ( i=0; i < sectionSize; i++ )
    {
        printf( "0x%x ", (unsigned char) pSection[i] );
    }
    printf("\n");

    // After printing the PAT stop the filter
    g_print("Calling cgmi_StopSectionFilter...\n");
    retStat = cgmi_StopSectionFilter( pFilterPriv, pFilterId );
    if (retStat != CGMI_ERROR_SUCCESS )
    {
        printf("CGMI StopSectionFilterFailed\n");
    }

    // TODO:  Create a new filter to get a PMT found in the PAT above.
    filterRunning = false;
    // Free buffer allocated in cgmi_QueryBufferCallback
    g_free( pSection );

    return CGMI_ERROR_SUCCESS;
}

/* Section Filter */
static cgmi_Status sectionfilter( void *pSessionId, gint pid, guchar *value,
                                  guchar *mask, gint length )
{
    cgmi_Status retCode = CGMI_ERROR_SUCCESS;

    tcgmi_FilterData filterdata;

    retCode = cgmi_CreateSectionFilter( pSessionId, pSessionId, &filterid );
    if (retCode != CGMI_ERROR_SUCCESS)
    {
        printf("CGMI CreateSectionFilter Failed\n");

        return retCode;
    }

    filterdata.pid = pid;
    filterdata.value = value;
    filterdata.mask = mask;
    filterdata.length = length;
    filterdata.comparitor = FILTER_COMP_EQUAL;

    retCode = cgmi_SetSectionFilter( pSessionId, filterid, &filterdata );
    if (retCode != CGMI_ERROR_SUCCESS)
    {
        printf("CGMI SetSectionFilter Failed\n");

        return retCode;
    }

    retCode = cgmi_StartSectionFilter( pSessionId, filterid, 10, 1, 0, 
                                       cgmi_QueryBufferCallback, cgmi_SectionBufferCallback );
    if (retCode != CGMI_ERROR_SUCCESS)
    {
        printf("CGMI StartSectionFilter Failed\n");
    }else
    {
        filterRunning = true;
    }

    return retCode;
}

static cgmi_Status destroyfilter( void *pSessionId )
{
    cgmi_Status retCode = CGMI_ERROR_SUCCESS;

    if ( filterid != NULL )
    {
        if ( filterRunning == true )
        {
            retCode = cgmi_StopSectionFilter( pSessionId, filterid );
            if (retCode != CGMI_ERROR_SUCCESS )
            {
                printf("CGMI StopSectionFilterFailed\n");
            }
            filterRunning == false;
        }

        retCode = cgmi_DestroySectionFilter( pSessionId, filterid );
        if (retCode != CGMI_ERROR_SUCCESS )
        {
            printf("CGMI StopSectionFilterFailed\n");
        }

        filterid = NULL;
    }    
}

/* Callback Function */
static void cgmiCallback( void *pUserData, void *pSession, tcgmi_Event event )
{
    printf("CGMI Event Recevied : ");

    switch (event)
    {
        case NOTIFY_STREAMING_OK:
            printf("NOTIFY_STREAMING_OK");
            break;
        case NOTIFY_FIRST_PTS_DECODED:
            printf("NOTIFY_FIRST_PTS_DECODED");
            break;
        case NOTIFY_STREAMING_NOT_OK:
            printf("NOTIFY_STREAMING_NOT_OK");
            break;
        case NOTIFY_SEEK_DONE:
            printf("NOTIFY_SEEK_DONE");
            break;
        case NOTIFY_START_OF_STREAM:
            printf("NOTIFY_START_OF_STREAM");
            break;
        case NOTIFY_END_OF_STREAM:
            printf("NOTIFY_END_OF_STREAM");
            break;
        case NOTIFY_DECRYPTION_FAILED:
            printf("NOTIFY_DECRYPTION_FAILED");
            break;
        case NOTIFY_NO_DECRYPTION_KEY:
            printf("NOTIFY_NO_DECRYPTION_KEY");
            break;
        case NOTIFY_VIDEO_ASPECT_RATIO_CHANGED:
            printf("NOTIFY_VIDEO_ASPECT_RATIO_CHANGED");
            break;
        case NOTIFY_VIDEO_RESOLUTION_CHANGED:
            printf("NOTIFY_VIDEO_RESOLUTION_CHANGED");
            break;
        case NOTIFY_CHANGED_LANGUAGE_AUDIO:
            printf("NOTIFY_CHANGED_LANGUAGE_AUDIO");
            break;
        case NOTIFY_CHANGED_LANGUAGE_SUBTITLE:
            printf("NOTIFY_CHANGED_LANGUAGE_SUBTITLE");
            break;
        case NOTIFY_CHANGED_LANGUAGE_TELETEXT:
            printf("NOTIFY_CHANGED_LANGUAGE_TELETEXT");
            break;
        case NOTIFY_MEDIAPLAYER_URL_OPEN_FAILURE:
            printf("NOTIFY_MEDIAPLAYER_URL_OPEN_FAILURE");
            break;
        case NOTIFY_MEDIAPLAYER_UNKNOWN:
            printf("NOTIFY_MEDIAPLAYER_UNKNOWN");
            break;
        default:
            printf("UNKNOWN");
            break;
    }

    printf( "\n" );
}

void help(void)
{
    printf( "Supported commands:\n"
            "Single APIs:\n"
           "\tplay <url>\n"
           "\tstop (or unload)\n"
           "\n"
           "\tgetraterange\n"
           "\tsetrate <rate (float)>\n"
           "\n"
           "\tgetposition\n"
           "\tsetposition <position (seconds) (float)>\n"
           "\n"
           "\tgetduration\n"
           "\n"
           "\tnewsession\n"
           "\n"
           "\tcreatefilter pid=0xVAL <value=0xVAL> <mask=0xVAL>\n"
           "\tstopfilter\n"
           "\n"
           "\tgetaudiolanginfo\n"
           "\n"
           "\tsetaudiolang <index>\n"
           "\n"
           "\tsetdefaudiolang <lang>\n"
           "\n"
           "\tsetvideorect <x,y,w,h>\n"
           "\n"
           "Tests:\n"
           "\tcct <url #1> <url #2> <interval (seconds)> <duration(seconds)>\n"
           "\t\tChannel Change Test - Change channels between <url #1> and\n"
           "\t\t<url#2> at interval <interval> for duration <duration>.\n"
           "\n"
           "\thelp\n" 
           "\thistory\n"
           "\n"
           "\tquit\n\n");
}

/* MAIN */
int main(int argc, char **argv)
{
    cgmi_Status retCode = CGMI_ERROR_SUCCESS;   /* Return codes from cgmi */
    void *pSessionId;                           /* CGMI Session Handle */

    gchar command[MAX_COMMAND_LENGTH];          /* Command buffer */
    gchar arg[MAX_COMMAND_LENGTH];              /* Command args buffer for
                                                   parsing. */
    gchar history[MAX_HISTORY][MAX_COMMAND_LENGTH]; /* History buffers */
    gint cur_history = 0;                       /* Current position in up/down
                                                   history browsing. */
    gint history_ptr = 0;                       /* Current location to store
                                                   new history. */
    gint history_depth = 0;                     /* How full is the history
                                                   buffer. */
    gint quit = 0;                              /* 1 = quit command parsing */
    gint playing = 0;                           /* Play state:
                                                    0: stopped
                                                    1: playing */

    /* Command Parsing */
    int c = 0;      /* Character from console input. */
    int a = 0;      /* Character location in the command buffer. */
    int retcom = 0; /* Return command for processing. */

    /* Status Variables */
    gfloat Position = 0.0;
    gfloat Rate = 0.0;
    gfloat Rewind = 0.0;
    gfloat FFoward = 0.0;
    gfloat Duration = 0.0;
    cgmi_SessionType type = cgmi_Session_Type_UNKNOWN;
    gint pbCanPlay = 0;

    /* Change Channel Test parameters */
    gchar url1[128], url2[128];
    gchar *str = NULL;
    gint interval = 0;
    gint duration = 0;
    struct timeval start, current;
    int i = 0, j = 0;

    /* createfilter parameters */
    gchar arg1[256], arg2[256], arg3[256], arg4[256], tmp[256];
    gint pid = 0;
    guchar value[208], mask[208];
    gint vlength = 0, mlength = 0;
    gchar *ptmpchar;
    gchar tmpstr[3];
    int len = 0;
    int err = 0;

    // need to put this in a define diagInit (DIAGTYPE_DEFAULT, NULL, 0);

    /* Init CGMI. */
    retCode = cgmi_Init();
    if(retCode != CGMI_ERROR_SUCCESS)
    {
        printf("CGMI Init Failed: %s\n", cgmi_ErrorString( retCode ));
        return 1;
    } else {
        printf("CGMI Init Success!\n");
    }

    /* Create a playback session. */
    retCode = cgmi_CreateSession( cgmiCallback, NULL, &pSessionId );
    if (retCode != CGMI_ERROR_SUCCESS)
    {
        printf("CGMI CreateSession Failed: %s\n", cgmi_ErrorString( retCode ));
        return 1;
    } else {
        printf("CGMI CreateSession Success!\n");
    }

    /* Helpful Information */
    printf("CGMI CLI Ready...\n");
    help();

    /* Needed to handle key press events correctly. */
    tcgetattr( STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON); // Disable line buffering
    newt.c_lflag &= ~(ECHO); // Disable local echo
    tcsetattr( STDIN_FILENO, TCSANOW, &newt);

    /* Signal handler to clean up console. */
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    /* Main Command Loop */
    while (!quit)
    {
        retCode = CGMI_ERROR_SUCCESS;

        /* commandline */
        printf( "cgmi> " );
        a = 0;
        retcom = 0;
        cur_history = history_ptr;
        while (!retcom)
        {
            c = getchar();
            switch (c)
            {
                case 0x1b:      /* Arrow keys */
                    c = getchar();
                    c = getchar();
                    if ( c == 0x41 )    /* Up */
                    {
                        cur_history--;
                        if ( cur_history < 0 )
                        {
                            if ( history_depth == MAX_HISTORY )
                            {
                                cur_history = MAX_HISTORY - 1;
                            } else {
                                cur_history = 0;
                            }
                        }

                        strncpy( command, history[cur_history],
                                 MAX_COMMAND_LENGTH );
                        a = strlen( command );
                        printf( "\ncgmi> %s", command );
                    }
                    if ( c == 0x42 )    /* Down */
                    {
                        cur_history++;
#if 0
                        if ( cur_history >= history_depth )
                        {
                            cur_history = history_depth - 1;
                        }
#endif
                        if ( cur_history >= MAX_HISTORY )
                        {
                            cur_history = MAX_HISTORY - 1;
                        }

                        if ( cur_history >= history_ptr )
                        {
                            cur_history = history_ptr;
                            command[0] = '\0';
                            a = 0;
                        } else {
                            strncpy( command, history[cur_history],
                                     MAX_COMMAND_LENGTH );
                            a = strlen( command );
                        }
    
                        printf( "\ncgmi> %s", command );
                    }
                    break;
                case 0x4:       /* Ctrl+D */
                    printf("\n");
                    quit = 1;
                    retcom = 1;
                    break;
                case 0x8:       /* Backspace */
                case 0x7f:
                    a--;
                    if (a<0)
                        a=0;
                    else
                        printf("\b \b");
                    break;
                case 0xa:       /* Enter */
                    printf( "\n" );
                    retcom = 1;
                    break;
                case 0x10:      /* Ctrl+P */
                    /* This is for fun.  Enjoy! */
                    printf( "\nYour pizza order has been placed and should"
                            " arrive in 20-30 minutes or it's FREE!!!\n" );
                    command[0] = '\0';
                    a = 0;
                    retcom = 1;
                    break;
                default:
                    /* Printable Characters */
                    if ( (c >= 0x20) && (c <= 0x7e) )
                    {
                        command[a++] = (char) c;
                        printf( "%c", c );
                    } else {
                        printf( "\nUnknown key: 0x%x\n", c );
                        printf( "\ncgmi> %s", command );
                    }
                    break;
            }
        }

        command[a] = '\0';
        if ( a > 0 )
        {
            //printf("command: %s\n", command);
            strncpy( history[history_ptr++], command, MAX_COMMAND_LENGTH );
            if ( history_ptr > (MAX_HISTORY - 1) )
            {
                history_ptr = 0;
            }
            if ( history_depth < MAX_HISTORY )
            {
                history_depth++;
            }
        }

        /* play */
        if (strncmp(command, "play", 4) == 0)
        {
            if (playing)
            {
                printf( "Stop playback before starting a new one.\n" );
                continue;
            }
            if ( strlen( command ) <= 5 )
            {
                printf( "\tplay <url>\n" );
                continue;
            }
            strncpy( arg, command + 5, strlen(command) - 5 );
            arg[strlen(command) - 5] = '\0';
            /* Check First */
            printf("Checking if we can play this...");
            retCode = cgmi_canPlayType( arg, &pbCanPlay );
            if ( retCode == CGMI_ERROR_NOT_IMPLEMENTED )
            {
                printf( "cgmi_canPlayType Not Implemented\n" );
                printf( "Playing \"%s\"...\n", arg );
                retCode = play(pSessionId, arg);
                if ( retCode == CGMI_ERROR_SUCCESS )
                {
                    playing = 1;
                }
            } else if ( !retCode && pbCanPlay )
            {
                printf( "Yes\n" );
                printf( "Playing \"%s\"...\n", arg );
                retCode = play(pSessionId, arg);
                if ( retCode == CGMI_ERROR_SUCCESS )
                {
                    playing = 1;
                }
            } else {
                printf( "No\n" );
            }
        }
        /* Stop Section Filter */
        else if (strncmp(command, "stopfilter", 10) == 0)
        {
            if ( filterid != NULL )
            {
                /* Stop/Destroy the section filter */
                retCode = destroyfilter(pSessionId);
                printf("Filter stopped.\n");
            } else {
                printf("Filter has not been started.\n");
            }
        }
        /* stop or unload */
        else if (
                ((strncmp(command, "stop", 4) == 0) && (strlen(command) == 4))
                || (strncmp(command, "unload", 6) == 0))
        {
            retCode = stop(pSessionId);
            playing = 0;
        }
        /* getraterange */
        else if (strncmp(command, "getraterange", 12) == 0)
        {
            retCode = getraterange(pSessionId, &Rewind, &FFoward);
            if (!retCode)
            {
                printf( "Rate Range: %f : %f\n", Rewind, FFoward );
            }
        }
        /* setrate */
        else if (strncmp(command, "setrate", 7) == 0)
        {
            if ( strlen( command ) <= 8 )
            {
                printf( "\tsetrate <rate (float)>\n" );
                continue;
            }
            strncpy( arg, command + 8, strlen(command) - 8 );
            arg[strlen(command) - 8] = '\0';
            Rate = (float) atof(arg);
            printf( "Setting rate to %f (%s)\n", Rate, arg );
            retCode = setrate(pSessionId, Rate);
        }
        /* getposition */
        else if (strncmp(command, "getposition", 11) == 0)
        {
            retCode = getposition(pSessionId, &Position);
            if (!retCode)
            {
                printf( "Position: %f\n", Position );
            }
        }
        /* setposition */
        else if (strncmp(command, "setposition", 11) == 0)
        {
            if ( strlen( command ) <= 12 )
            {
                printf( "\tsetposition <position (seconds) (float)>\n" );
                continue;
            }
            strncpy( arg, command + 12, strlen(command) - 12 );
            arg[strlen(command) - 12] = '\0';
            Position = (float) atof(arg);
            printf( "Setting position to %f (%s)\n", Position, arg );
            retCode = setposition(pSessionId, Position);
        }
        /* getduration */
        else if (strncmp(command, "getduration", 11) == 0)
        {
            retCode = getduration(pSessionId, &Duration, &type);
            if (!retCode)
            {
                printf( "Duration: %f, SessionType: ", Duration );
                switch( type )
                {
                    case LIVE:
                        printf( "LIVE\n" );
                        break;
                    case TSB:
                        printf( "TSB\n" );
                        break;
                    case FIXED:
                        printf( "FIXED\n" );
                        break;
                    case cgmi_Session_Type_UNKNOWN:
                        printf( "cgmi_Session_Type_UNKNOWN\n" );
                        break;
                    default:
                        printf( "ERROR\n" );
                        break;
                }
            }
        }
        /* New Session */
        else if (strncmp(command, "newsession", 10) == 0)
        {
            /* If we were playing, stop. */
            if ( playing )
            {
                printf("Stopping playback.\n");
                retCode = stop(pSessionId);
                playing = 0;
            }

            /* Destroy the created session. */
            retCode = cgmi_DestroySession( pSessionId );
            if (retCode != CGMI_ERROR_SUCCESS)
            {
                printf("CGMI DestroySession Failed: %s\n",
                        cgmi_ErrorString( retCode ));
                break;
            } else {
                printf("CGMI DestroySession Success!\n");
            }

            /* Create a playback session. */
            retCode = cgmi_CreateSession( cgmiCallback, NULL, &pSessionId );
            if (retCode != CGMI_ERROR_SUCCESS)
            {
                printf("CGMI CreateSession Failed: %s\n", cgmi_ErrorString( retCode ));
                break;;
            } else {
                printf("CGMI CreateSession Success!\n");
            }
        }
        /* Create Filter */
        else if (strncmp(command, "createfilter", 12) == 0)
        {
            if ( filterid != NULL )
            {
                printf( "Only one filter at a time allowed currently in cgmi_cli.\n" );
                continue;
            }
            /* default values for section filter code */
            err = 0;
            pid = SECTION_FILTER_EMPTY_PID;
            bzero( value, 208 * sizeof( guchar ) );
            bzero( mask, 208 * sizeof( guchar ) );
            vlength = 0;
            mlength = 0;

            /* command */
            str = strtok( command, " " );
            if ( str == NULL )
            {
                printf( "Invalid command:\n"
                        "\tcreatefilter pid=0xVAL <value=0xVAL> <mask=0xVAL>\n"
                      );
                continue;
            }

            /* arg1 */
            str = strtok( NULL, " " );
            if ( str == NULL )
            {
                printf( "Invalid command:\n"
                        "\tcreatefilter pid=0xVAL <value=0xVAL> <mask=0xVAL>\n"
                      );
                continue;
            } else {
                strncpy( arg1, str, 256 );
            }

            /* arg2 */
            str = strtok( NULL, " " );
            if ( str != NULL )
            {
                strncpy( arg2, str, 256 );
            } else {
                strcpy( arg2, "" );
            }

            /* arg3 */
            str = strtok( NULL, " " );
            if ( str != NULL )
            {
                strncpy( arg3, str, 256 );
            } else {
                strcpy( arg3, "" );
            }

            /* arg4 */
            str = strtok( NULL, " " );
            if ( str != NULL )
            {
                strncpy( arg4, str, 256 );
            } else {
                strcpy( arg4, "" );
            }

            for ( i=0; i < 4; i++ )
            {
                /* cycle through each argument so they're supported out of order */
                switch (i)
                {
                    case 0:
                        strcpy( tmp, arg1 );
                        break;
                    case 1:
                        strcpy( tmp, arg2 );
                        break;
                    case 2:
                        strcpy( tmp, arg3 );
                        break;
                    case 3:
                        strcpy( tmp, arg4 );
                        break;
                }

                /* pid */
                if ( strncmp( tmp, "pid=", 4 ) == 0 )
                {
                    pid = (int) strtol( tmp + 4, NULL, 0 );
                }

                /* value */
                if ( strncmp( tmp, "value=", 6 ) == 0 )
                {
                    if ( strncmp( tmp + 6, "0x", 2 ) != 0 )
                    {
                        printf( "Hex values required for value.\n" );
                        err = 1;
                        break;
                    }
                    ptmpchar = tmp + 8;
                    vlength = 0;
                    len = strlen( ptmpchar );
                    while ( (ptmpchar[0] != '\0') && (ptmpchar[0] != '\n') && (len > (vlength * 2)) )
                    {
                        strncpy( tmpstr, ptmpchar, 2 );
                        tmpstr[2] = '\0';
                        value[vlength] = (guchar) strtoul( tmpstr, NULL, 16 );
                        ptmpchar = ptmpchar + 2;
                        vlength++;
                    }
                }

                /* mask */
                if ( strncmp( tmp, "mask=", 5 ) == 0 )
                {
                    if ( strncmp( tmp + 5, "0x", 2 ) != 0 )
                    {
                        printf( "Hex values required for mask.\n" );
                        err = 1;
                        break;
                    }
                    ptmpchar = tmp + 7;
                    mlength = 0;
                    len = strlen( ptmpchar );
                    while ( (ptmpchar[0] != '\0') && (ptmpchar[0] != '\n') && (len > (mlength * 2)) )
                    {
                        strncpy( tmpstr, ptmpchar, 2 );
                        tmpstr[2] = '\0';
                        mask[mlength] = (guchar) strtoul( tmpstr, NULL, 16 );
                        ptmpchar = ptmpchar + 2;
                        mlength++;
                    }
                }
            }

            if ( err )
            {
                continue;
            }

            if ( (mlength > 0) && (vlength > 0) && (mlength != vlength) )
            {
                printf( "Mask length and value length must be equal.\n" );

                continue;
            }

            retCode = sectionfilter( pSessionId, pid, value, mask, mlength );
            if ( retCode == CGMI_ERROR_SUCCESS )
            {
                printf( "Filter created.\n" );
            } else {
                printf( "Filter creation failed.\n" );
            }
        }
        /* get audio languages available */
        else if (strncmp(command, "getaudiolanginfo", 16) == 0)
        {
            gint count;
            gint i;
            gchar lang[4] = { 0 };
            retCode = cgmi_GetNumAudioLanguages( pSessionId, &count );
            if ( retCode != CGMI_ERROR_SUCCESS )
            {
                printf("Error returned %d\n", retCode);
                continue;
            }
            printf("\nAvailable Audio Languages:\n");
            printf("--------------------------\n");
            for ( i = 0; i < count; i++ )
            {
                retCode = cgmi_GetAudioLangInfo( pSessionId, i, lang, sizeof(lang) );
                if ( retCode != CGMI_ERROR_SUCCESS )
                    break;
                printf("%d: %s\n", i, lang);
            }
        }
        /* set audio stream */
        else if (strncmp(command, "setaudiolang", 12) == 0)
        {
            gint index;
            if ( strlen( command ) <= 13 )
            {
                printf( "\tsetaudiolang <index>\n" );
                continue;
            }
            strncpy( arg, command + 13, strlen(command) - 13 );
            arg[strlen(command) - 13] = '\0';
            
            index = atoi( arg );

            retCode = cgmi_SetAudioStream( pSessionId, index );
            if ( retCode == CGMI_ERROR_BAD_PARAM )
            {
                printf("Invalid index, use getaudiolanginfo to see available languages and their indexes %d\n", retCode);                
            }
            else if ( retCode != CGMI_ERROR_SUCCESS )
            {
                printf("Error returned %d\n", retCode);                
            }
        }
        /* set default audio language */
        else if (strncmp(command, "setdefaudiolang", 15) == 0)
        {
            if ( strlen( command ) <= 16 )
            {
                printf( "\tsetdefaudiolang <lang>\n" );
                continue;
            }
            strncpy( arg, command + 16, strlen(command) - 16 );
            arg[strlen(command) - 16] = '\0';
            
            retCode = cgmi_SetDefaultAudioLang( pSessionId, arg );
            if ( retCode != CGMI_ERROR_SUCCESS )
            {
                printf("Error returned %d\n", retCode);                
            }
        }
        /* set video rectangle */
        else if (strncmp(command, "setvideorect", 12) == 0)
        {
            char *ptr;
            char *dim;
            int i, size[4];
            if ( strlen( command ) <= 13 )
            {
                printf( "\tsetvideorect <x,y,w,h>\n" );
                continue;
            }
            strncpy( arg, command + 13, strlen(command) - 13 );
            arg[strlen(command) - 13] = '\0';
            
            dim = arg;
            for ( i = 0; i < 4; i++ )
            {                
                if ( i != 3 )
                    ptr = strchr(dim, ',');

                if ( NULL == ptr )
                {
                  printf("Error parsing arguments, please specify rectangle dimensions in the format x,y,w,h\n");
                  break;
                }
                if ( i != 3 )
                    *ptr = 0;

                size[i] = atoi( dim );
                dim = ptr + 1;
            }

            if ( NULL == ptr )
                continue;

            retCode = cgmi_SetVideoRectangle( pSessionId, size[0], size[1], size[2], size[3] );
            if ( retCode != CGMI_ERROR_SUCCESS )
            {
                printf("Error returned %d\n", retCode);                
            }
        }
        /* Channel Change Test */
        else if (strncmp(command, "cct", 3) == 0)
        {
            /* command */
            str = strtok( command, " " );
            if ( str == NULL ) continue;

            /* url1 */
            str = strtok( NULL, " " );
            if ( str == NULL ) continue;
            strncpy( url1, str, 128 );

            /* url2 */
            str = strtok( NULL, " " );
            if ( str == NULL ) continue;
            strncpy( url2, str, 128 );

            /* interval */
            str = strtok( NULL, " " );
            if ( str == NULL ) continue;
            interval = atoi( str );

            /* duration */
            str = strtok( NULL, " " );
            if ( str == NULL ) continue;
            duration = atoi( str );

            retCode = cgmi_canPlayType( url1, &pbCanPlay );
            if ( retCode == CGMI_ERROR_NOT_IMPLEMENTED )
            {
                printf( "cgmi_canPlayType Not Implemented\n" );
            } else if ( retCode || !pbCanPlay )
            {
                printf( "Cannot play %s\n", url1 );
                continue;
            }

            retCode = cgmi_canPlayType( url2, &pbCanPlay );
            if ( retCode == CGMI_ERROR_NOT_IMPLEMENTED )
            {
                printf( "cgmi_canPlayType Not Implemented\n" );
            } else if ( retCode || !pbCanPlay )
            {
                printf( "Cannot play %s\n", url2 );
                continue;
            }

            /* Run test. */
            gettimeofday( &start, NULL );
            gettimeofday( &current, NULL );
            str = url1;
            i = 0;
            while ( (current.tv_sec - start.tv_sec) < duration )
            {
                i++;
                printf( "(%d) Playing %s...\n", i, str );
                retCode = play(pSessionId, str);
                sleep( interval );
                retCode = stop(pSessionId);

                if ( str == url1 )
                    str = url2;
                else
                    str = url1;

                gettimeofday( &current, NULL );
            }

            printf( "Played for %d seconds. %d channels.\n",
                    (int) (current.tv_sec - start.tv_sec), i );
        }
        /* help */
        else if (strncmp(command, "help", 4) == 0)
        {
            help();
        }
        /* history */
        else if (strncmp(command, "history", 7) == 0)
        {
            for ( j=0; j < history_depth; j++ )
            {
                printf( "\t%s\n", history[j] );
            }
        }
        /* quit */
        else if (strncmp(command, "quit", 4) == 0)
        {
            quit = 1;
        }
        /* unknown */
        else
        {
            if ( command[0] != '\0' )
            {
                printf( "Unknown command: \"%s\"\n", command );
            }
        }

        /* If we receive and error, print error. */
        if (retCode)
        {
             printf( "Error: %s\n", cgmi_ErrorString( retCode ) );
        }
    }

    tcsetattr( STDIN_FILENO, TCSANOW, &oldt);

    /* If we were playing, stop. */
    if ( playing )
    {
        printf("Stopping playback.\n");
        retCode = stop(pSessionId);
    }

    /* Destroy the created session. */
    retCode = cgmi_DestroySession( pSessionId );
    if (retCode != CGMI_ERROR_SUCCESS)
    {
        printf("CGMI DestroySession Failed: %s\n", cgmi_ErrorString( retCode ));
    } else {
        printf("CGMI DestroySession Success!\n");
    }

    /* Terminate CGMI interface. */
    retCode = cgmi_Term();
    if (retCode != CGMI_ERROR_SUCCESS)
    {
        printf("CGMI Term Failed: %s\n", cgmi_ErrorString( retCode ));
    } else {
        printf("CGMI Term Success!\n");
    }

    return 0;
}
