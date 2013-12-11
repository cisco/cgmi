/* Includes */
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <stdbool.h>

// put this in a define. #include "diaglib.h"
#include <sys/time.h>

#include "cgmiPlayerApi.h"

/* Defines for section filtering. */
#define MAX_PMT_COUNT 20

typedef struct program{

    short stream_type; // 8 bits
    short reserved_1; // 3 bits
    short elementary_PID; // 13 bits

    short reserved_2; // 4 bits
    short ES_info_length; // 12 bits

    //short numDesc;
    //ca_desc theDesc[];

}tPmtProgram;

typedef struct{

    short programNumber; // 16 bits
    short reserved; // 3 bits
    short program_map_PID; // 13 bits

    short pointer; // 8 bits
    short table_id; // 8 bits

    bool section_syntax_indicator; // 1 bit
    short reserved_1; // 3 bits
    short section_length; // 12 bits

    short program_number; // 16 bits

    short reserved_2; // 2 bits
    short version_number; // 5 bits
    bool current_next_indicator; // 1 bit
    short section_number; // 8 bits

    short last_section_number; // 8 bits

    short reserved_3; // 3 bits
    short PCR_PID; // 13 bits

    short reserved_4; // 4 bits
    short program_info_length; // 12 bits

    // descriptors missing

    // programs
    tPmtProgram programs[50];

    int CRC;

}tPmt;

typedef struct{

    short table_id; // 8 bits

    bool section_syntax_indicator; // 1 bit
    short reserved_1; // 3 bits
    short section_length; // 12 bits

    short transport_stream_id; // 16 bits

    short reserved_2; // 2 bits
    short version_number; // 5 bits
    bool current_next_indicator; // 1 bit
    short section_number; // 8 bits

    short last_section_number; // 8 bits

    tPmt pmts[MAX_PMT_COUNT]; // Dynamic size, but limited to 16
    short numPMTs;

    int CRC;

}tPat;

static void *filterid = NULL;
/* End defines for section filtering. */

/* Prototypes */
static void cgmiCallback( void *pUserData, void *pSession, tcgmi_Event event );

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

    if ( filterid != NULL )
    {
        retCode = cgmi_StopSectionFilter( pSessionId, filterid );
        if (retCode != CGMI_ERROR_SUCCESS )
        {
            printf("CGMI StopSectionFilterFailed\n");
        }

        retCode = cgmi_DestroySectionFilter( pSessionId, filterid );
        if (retCode != CGMI_ERROR_SUCCESS )
        {
            printf("CGMI StopSectionFilterFailed\n");
        }

        filterid = NULL;
    }

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

/****************************  Example PAT parsing  *******************************/
static int parsePAT( char *buffer, int bufferSize, tPat *pat )
{
    int index = 0;
    int x;

    printf( "Parsing PAT..." );
    fflush(stdout);
    pat->table_id = buffer[++index];
    pat->section_syntax_indicator = (bool)( (buffer[++index]&0x80)>>7 );
    pat->reserved_1 = (buffer[index]&0x70)>>4;
    pat->section_length = (buffer[index]&0x0F)<<8 | buffer[++index];
    pat->transport_stream_id = (buffer[++index]<<8 | buffer[++index] );
    pat->reserved_2 = (buffer[++index]&0xC0)>>6;
    pat->version_number = (buffer[index]&0x3E)>>1;
    pat->current_next_indicator = (bool)(buffer[index]&0x01);
    pat->section_number = buffer[++index];
    pat->last_section_number = buffer[++index];

    // number of PMTs is section_length - 5 bytes remaining in PAT headers, - 4 bytes for CRC_32, divided by 4 bytes for each entry.
    pat->numPMTs = (pat->section_length - 5 - 4) / 4;
    printf( "." );
    fflush(stdout);

    for( x = 0; x < pat->numPMTs; x++ )
    {
        pat->pmts[x].programNumber = (buffer[++index]<<8) | buffer[++index];
        pat->pmts[x].reserved = (buffer[++index]&0xC0)>>5;
        pat->pmts[x].program_map_PID = (buffer[index]&0x1F)<<8 | buffer[++index];
        printf( "." );
        fflush(stdout);
    }

    pat->CRC = (buffer[++index]<<24) | (buffer[++index]<<16) | (buffer[++index]<<8) | (buffer[++index]);

    printf( "Done.\n" );
    fflush(stdout);

    return 0;
}

void printPAT( tPat *pat )
{
    int x;

    g_print("Dumping PAT...\n");
    g_print("\tTable_id: 0x%x\n", pat->table_id);
    if( pat->section_syntax_indicator )
        g_print("\tsection_syntax_indicator: True(1)\n");
    else
        g_print("\tsection_syntax_indicator: False(0)\n");
    g_print("\treserved_1: 0x%x\n", pat->reserved_1);
    g_print("\tsection_length: 0x%x (%d)\n", pat->section_length, pat->section_length);
    g_print("\ttransport_stream_id: 0x%x\n", pat->transport_stream_id);
    g_print("\treserved_2: 0x%x\n", pat->reserved_2);
    g_print("\tversion_number: 0x%x\n", pat->version_number);
    if( pat->current_next_indicator )
        g_print("\tcurrent_next_indicator: True(1)\n");
    else
        g_print("\tcurrent_next_indicator: False(0)\n");
    g_print("\tsection_number: 0x%x\n", pat->section_number);
    g_print("\tlast_section_number: 0x%x\n", pat->last_section_number);

    g_print("\tFound %d program(s) in the PAT...\n", pat->numPMTs);

    for(x = 0; x < pat->numPMTs ; x++)
    {
        g_print("\tProgram #%d:\n", x + 1);
        g_print("\t\tprogramNumber: 0x%x\n", pat->pmts[x].programNumber);
        g_print("\t\treserved: 0x%x\n", pat->pmts[x].reserved);
        g_print("\t\tprogram_map_PID: 0x%x\n\n", pat->pmts[x].program_map_PID);
    }

    g_print("\n");
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
    tPat pat;

    //g_print( "cgmi_QueryBufferCallback -- pFilterId: 0x%08lx \n", pFilterId );

    if( NULL == pSection )
    {
        g_print("NULL buffer passed to cgmiSectionBufferCallback.\n");
        return CGMI_ERROR_BAD_PARAM;
    }

    g_print("Received section pFilterId: 0x%p, sectionSize %d\n\n", pFilterId, sectionSize);
#if 0 /* Problems with these output functions. */
    //printHex( pSection, sectionSize );
    g_print("\n\n");

    parsePAT( pSection, sectionSize, &pat );
    printPAT( &pat );
#endif

    // After printing the PAT stop the filter
    g_print("Calling cgmi_StopSectionFilter...\n");
    retStat = cgmi_StopSectionFilter( pFilterPriv, pFilterId );
    if (retStat != CGMI_ERROR_SUCCESS )
    {
        printf("CGMI StopSectionFilterFailed\n");
    }

    // TODO:  Create a new filter to get a PMT found in the PAT above.

    g_print("Calling cgmi_DestroySectionFilter...\n");
    retStat = cgmi_DestroySectionFilter( pFilterPriv, pFilterId );
    if (retStat != CGMI_ERROR_SUCCESS )
    {
        printf("CGMI StopSectionFilterFailed\n");
    }

    filterid = NULL;

    // Free buffer allocated in cgmi_QueryBufferCallback
    g_free( pSection );

    return CGMI_ERROR_SUCCESS;
}

/* Section Filter */
static cgmi_Status sectionfilter( void *pSessionId, gint pid, guchar *value,
                                  guchar *mask, gint length, guint offset )
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
    filterdata.offset = offset;
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
    }

    return retCode;
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
    printf("Supported commands:\n"
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
           "\tcreatefilter pid=0xVAL <value=0xVAL> <mask=0xVAL> <offset=0xVAL>\n"
           "\n"
           "\tgetaudiolanginfo\n"
           "\n"
           "\tsetaudiolang <index>\n"
           "\n"
           "\tsetdefaudiolang <lang>\n"
           "\n"
           "Tests:\n"
           "\tcct <url #1> <url #2> <interval (seconds)> <duration(seconds)>\n"
           "\t\tChannel Change Test - Change channels between <url #1> and\n"
           "\t\t<url#2> at interval <interval> for duration <duration>.\n"
           "\n"
           "\thelp\n" 
           "\n"
           "\tquit\n\n");
}

/* MAIN */
int main(int argc, char **argv)
{
    cgmi_Status retCode = CGMI_ERROR_SUCCESS;
    void *pSessionId;

    gchar command[512];
    gchar arg[512];
    gint quit = 0;
    gint playing = 0;

    /* Status Variables */
    gfloat Position = 0.0;
    gfloat Rate = 0.0;
    gfloat Rewind = 0.0;
    gfloat FFoward = 0.0;
    gfloat Duration = 0.0;
    cgmi_SessionType type = cgmi_Session_Type_UNKNOWN;
    gint pbCanPlay = 0;

    gchar url1[128], url2[128];
    gchar *str = NULL;
    gint interval = 0;
    gint duration = 0;
    struct timeval start, current;
    int i = 0;

    gchar arg1[256], arg2[256], arg3[256], arg4[256], tmp[256];
    gint pid = 0;
    guchar value[208], mask[208];
    gint vlength = 0, mlength = 0;
    guint offset = 0;
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

    /* Main Command Loop */
    while (!quit)
    {
        retCode = CGMI_ERROR_SUCCESS;

        /* Get the command. */
        printf( "cgmi> " );
        if ( fgets( command, 512, stdin ) == NULL )
        {
            fprintf( stderr, "Error getting input. Exiting.\n" );
            quit = 1;
        }

        /* play */
        if (strncmp(command, "play", 4) == 0)
        {
            if (playing)
            {
                printf( "Stop playback before starting a new one.\n" );
                continue;
            }
            strncpy( arg, command + 5, strlen(command) - 6 );
            arg[strlen(command) - 6] = '\0';
            /* Check First */
            printf("Checking if we can play this...");
            retCode = cgmi_canPlayType( arg, &pbCanPlay );
            if ( retCode == CGMI_ERROR_NOT_IMPLEMENTED )
            {
                printf( "cgmi_canPlayType Not Implemented\n" );
                printf("Playing \"%s\"...\n", arg);
                retCode = play(pSessionId, arg);
                if ( retCode == CGMI_ERROR_SUCCESS )
                {
                    playing = 1;
                }
            } else if ( !retCode && pbCanPlay )
            {
                printf( "Yes\n" );
                printf("Playing \"%s\"...\n", arg);
                retCode = play(pSessionId, arg);
                if ( retCode == CGMI_ERROR_SUCCESS )
                {
                    playing = 1;
                }
            } else {
                printf( "No\n" );
            }
        }
        /* stop or unload */
        else if ((strncmp(command, "stop", 4) == 0) || 
                 (strncmp(command, "unload", 6) == 0))
        {
            retCode = stop(pSessionId);
            if ( retCode == CGMI_ERROR_SUCCESS )
            {
                playing = 0;
            }
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
            strncpy( arg, command + 8, strlen(command) - 9 );
            arg[strlen(command) - 9] = '\0';
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
            strncpy( arg, command + 12, strlen(command) - 13 );
            arg[strlen(command) - 13] = '\0';
            Position = (float) atof(arg);
            printf( "Setting position to %f (%s)\n", Position, arg );
            retCode = setposition(pSessionId, Position);
        }
        /* setposition */
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
            offset = 0;

            /* command */
            str = strtok( command, " " );
            if ( str == NULL )
            {
                printf( "Invalid command:\n"
                        "\tcreatefilter pid=0xVAL <value=0xVAL> <mask=0xVAL> <offset=0xVAL>\n"
                      );
                continue;
            }

            /* arg1 */
            str = strtok( NULL, " " );
            if ( str == NULL )
            {
                printf( "Invalid command:\n"
                        "\tcreatefilter pid=0xVAL <value=0xVAL> <mask=0xVAL> <offset=0xVAL>\n"
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

                /* offset */
                if ( strncmp( tmp, "offset=", 7 ) == 0 )
                {
                    offset = (int) strtoul( tmp + 7, NULL, 0 );
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

            retCode = sectionfilter( pSessionId, pid, value, mask, mlength, offset );
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
            strncpy( arg, command + 13, strlen(command) - 14 );
            arg[strlen(command) - 14] = '\0';
            
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
            strncpy( arg, command + 16, strlen(command) - 17 );
            arg[strlen(command) - 17] = '\0';
            
            retCode = cgmi_SetDefaultAudioLang( pSessionId,  arg );
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
        /* quit */
        else if (strncmp(command, "quit", 4) == 0)
        {
            quit = 1;
        }

        /* If we receive and error, print error. */
        if (retCode)
        {
             printf( "Error: %s\n", cgmi_ErrorString( retCode ) );
        }
    }

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
