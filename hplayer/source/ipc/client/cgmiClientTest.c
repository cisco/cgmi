#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

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


#define PRINT_HEX_WIDTH 16

static void printHex (void *buffer, int size) {
    guchar asciiBuf[17];
    guchar *pBuffer = buffer;
    gint i;

    for( i = 0; i < size; i++ )
    {
        // Print the previous chucks ascii
        if( (i % PRINT_HEX_WIDTH) == 0 )
        {
            if( i != 0 )
                g_print("  %s\n", asciiBuf);
            g_print("  %04x ", i);
        }

        // Print current byte
        g_print(" %02x", pBuffer[i]);

        if( (pBuffer[i] < 0x20) || (pBuffer[i] > 0x7e) )
        {
            asciiBuf[i % PRINT_HEX_WIDTH] = '.';
        }else
        {
            asciiBuf[i % PRINT_HEX_WIDTH] = pBuffer[i];
        }
        asciiBuf[ (i % PRINT_HEX_WIDTH) + 1 ] = '\0';
    }

    // Handle one off last ascii print
    while( (i % PRINT_HEX_WIDTH) != 0 )
    {
        g_print("   ");
        i++;
    }
    g_print("  %s\n", asciiBuf);
}

static int parsePAT( char *buffer, int bufferSize, tPat *pat )
{
    int index = 0;
    int x;

    if( bufferSize < 8 )
    {
        g_print("PAT buffer too small (%d)\n", bufferSize);
        return -1;
    }

    pat->table_id = buffer[index];
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

    if( pat->numPMTs > MAX_PMT_COUNT )
    {
        g_print("Invalid number of PMTs parsed (%d)\n", pat->numPMTs);
        pat->numPMTs = 0;
        return -1;
    }

    if( bufferSize < 8 + (pat->numPMTs * 4) )
    {
        g_print("PAT buffer too small (no PMTs) (%d)\n", bufferSize);
        return -1;
    }

    for( x = 0; x < pat->numPMTs; x++ )
    {
        pat->pmts[x].programNumber = (buffer[++index]<<8) | buffer[++index];
        pat->pmts[x].reserved = (buffer[++index]&0xC0)>>5;
        pat->pmts[x].program_map_PID = (buffer[index]&0x1F)<<8 | buffer[++index];
    }

    if( bufferSize < 8 + (pat->numPMTs * 4) + 4 )
    {
        g_print("PAT buffer too small (no CRC) (%d)\n", bufferSize);
        return -1;
    }

    pat->CRC = (buffer[++index]<<24) | (buffer[++index]<<16) | (buffer[++index]<<8) | (buffer[++index]);

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

/**
 * CGMI DBUS callback.
 */
static void cgmiCallback( void *pUserData, void *pSession, tcgmi_Event event )
{
    g_print( "CGMI Player Event Recevied : %d \n", event );
}

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
    tPat pat;

    //g_print( "cgmi_QueryBufferCallback -- pFilterId: 0x%08lx \n", pFilterId );

    if( NULL == pSection )
    {
        g_print("NULL buffer passed to cgmiSectionBufferCallback.\n");
        return CGMI_ERROR_BAD_PARAM;
    }

    g_print("Received section pFilterId: 0x%lx, sectionSize %d\n\n", pFilterId, sectionSize);
    printHex( pSection, sectionSize );
    g_print("\n\n");

    parsePAT( pSection, sectionSize, &pat );
    printPAT( &pat );

    g_print("Calling cgmi_StopSectionFilter...\n");
    retStat = cgmi_StopSectionFilter( pFilterPriv, pFilterId );
    CHECK_ERROR(retStat);

    g_print("Calling cgmi_DestroySectionFilter...\n");
    retStat = cgmi_DestroySectionFilter( pFilterPriv, pFilterId );
    CHECK_ERROR(retStat);

    // Free buffer allocated in cgmi_QueryBufferCallback
    g_free( pSection );


    return CGMI_ERROR_SUCCESS;
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
    g_print("Calling cgmi_GetNumAudioLanguages...\n");
    retStat = cgmi_GetNumAudioLanguages( pSessionId, &count );
    CHECK_ERROR(retStat);
    g_print("cgmi_GetNumAudioLanguages : count = (%d)\n", count );


    char langInfoBuf[1024];
    g_print("Calling cgmi_GetAudioLangInfo...\n");
    retStat = cgmi_GetAudioLangInfo( pSessionId, 0, langInfoBuf, 1024 );
    CHECK_ERROR(retStat);


    g_print("Calling cgmi_SetAudioStream...\n");
    retStat = cgmi_SetAudioStream( pSessionId, 0 );
    CHECK_ERROR(retStat);


    g_print("Calling cgmi_SetDefaultAudioLang...\n");
    retStat = cgmi_SetDefaultAudioLang( pSessionId, "eng" );
    CHECK_ERROR(retStat);

    g_print("Calling cgmi_SetVideoRectangle...\n");
    retStat = cgmi_SetVideoRectangle( pSessionId, 0, 0, 400, 400 );
    CHECK_ERROR(retStat);

    // Let it play for a few more seconds
    g_usleep(1 * 1000 * 1000);

    /* Create section filter */
    void *filterId = NULL;
    g_print("Calling cgmi_CreateSectionFilter...\n");
    retStat = cgmi_CreateSectionFilter( pSessionId, pSessionId, &filterId );
    CHECK_ERROR(retStat);

    /* */

    tcgmi_FilterData filterData;
    filterData.pid = 0;
    filterData.value = NULL;
    filterData.mask = NULL;
    filterData.length = 0;
    filterData.offset = 0;
    filterData.comparitor = FILTER_COMP_EQUAL;

    g_print("Calling cgmi_SetSectionFilter... for filterId 0x%08lx\n", filterId);
    retStat = cgmi_SetSectionFilter( pSessionId, filterId, &filterData );
    CHECK_ERROR(retStat);


    g_print("Calling cgmi_StartSectionFilter...\n");
    retStat = cgmi_StartSectionFilter( pSessionId, filterId, 10, 1, 0, cgmi_QueryBufferCallback, cgmi_SectionBufferCallback );
    CHECK_ERROR(retStat);
    /*/

    // Let it section filter for a bit
    g_usleep( 200 * 1000);

    g_print("Calling cgmi_StopSectionFilter...\n");
    retStat = cgmi_StopSectionFilter( pSessionId, filterId );
    CHECK_ERROR(retStat);


    g_print("Calling cgmi_DestroySectionFilter...\n");
    retStat = cgmi_DestroySectionFilter( pSessionId, filterId );
    CHECK_ERROR(retStat);
    
    // */

    // Let it play for a few more seconds
    g_usleep(6 * 1000 * 1000);


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
