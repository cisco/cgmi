#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "cgmiPlayerApi.h"
#include "dbusPtrCommon.h"


////////////////////////////////////////////////////////////////////////////////
// Logging
////////////////////////////////////////////////////////////////////////////////

#define CHECK_ERROR(err) \
    if( err != CGMI_ERROR_SUCCESS ) \
    { \
        g_print("CGMI_CLIENT_TEST %s:%d - %s :: Failed with %s \n", __FILE__, __LINE__, __FUNCTION__, cgmi_ErrorString(err) ); \
    }

#define CHECK_ERROR_RETURN(err) \
    CHECK_ERROR(err); \
    if( err != CGMI_ERROR_SUCCESS ) return;

#define CHECK_ERROR_RETURN_STAT(err) \
    CHECK_ERROR(err); \
    if( err != CGMI_ERROR_SUCCESS ) return err;

#define CHECK_ERROR_RETURN_NULL(err) \
    CHECK_ERROR(err); \
    if( err != CGMI_ERROR_SUCCESS ) return NULL;

////////////////////////////////////////////////////////////////////////////////
// MPEG Transport Stream Parsing 
////////////////////////////////////////////////////////////////////////////////

#define MAX_PMT_COUNT 20
#define MAX_CA_COUNT 20
#define MPEGTS_PMT_HEADER_LEN 12
#define MPEGTS_PMT_PRE_SEC_LEN_BYTES 2
#define MPEGTS_CRC_LEN 4

typedef struct ca_desc{

    short desc_tag; // 8 bits
    short desc_len; // 8 bits
    short CA_system_ID; // 16 bits
    short reserved; // 3 bits
    short CA_PID; // 13 bits

    //unsigned char private_data_bytes[100];

}tMpegTsCaDesc;

typedef struct program{

    short stream_type; // 8 bits
    short reserved_1; // 3 bits
    short elementary_PID; // 13 bits

    short reserved_2; // 4 bits
    short ES_info_length; // 12 bits

    short numDesc;
    tMpegTsCaDesc theDesc[MAX_CA_COUNT];

}tMpegTsProgram;

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
    short num_progs_parsed;
    tMpegTsProgram programs[50];

    int CRC;

}tMpegTsPmt;

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

    tMpegTsPmt pmts[MAX_PMT_COUNT]; // Dynamic size, but limited to 16
    short numPMTs;

    int CRC;

    bool parsed;

}tMpegTsPat;

/* Statics */
static tMpegTsPat gPat;
static bool gPmtParsed = false;

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

static int parsePAT( char *buffer, int bufferSize, tMpegTsPat *pat )
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

static void printPAT( tMpegTsPat *pat )
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

static int parsePMT( tMpegTsPmt *curPmt, unsigned char *buffer, int bufferSize )
{
    int index = 0;
    int programMarker;
    int crcMarker;
    int x;
    
    // Preconditions
    if( curPmt == NULL )
    {
        g_print("ERROR:  NULL PMT buffer\n");
        return -1;
    }

    // Do we have enough buffer to parse the 12 byte header
    if( bufferSize < MPEGTS_PMT_HEADER_LEN )
    {
        g_print("PMT buffer too small (%d)\n", bufferSize);
        return -1;
    }

    curPmt->num_progs_parsed = 0;

    // Start parsing PMT
    curPmt->table_id = buffer[index];

    curPmt->section_syntax_indicator = (bool)( (buffer[++index]&0x80)>>7 );
    curPmt->reserved_1 = (buffer[index]&0x70)>>4;
    curPmt->section_length = (buffer[index]&0x0F)<<8 | buffer[++index];

    curPmt->program_number = ((buffer[++index])<<8 | buffer[++index] );

    curPmt->reserved_2 = (buffer[++index]&0xC0)>>6;
    curPmt->version_number = (buffer[index]&0x3E)>>1;
    curPmt->current_next_indicator = (bool)(buffer[index]&0x01);
    curPmt->section_number = buffer[++index];
    curPmt->last_section_number = buffer[++index];

    curPmt->reserved_3 = (buffer[++index]&0xE0)>>5;
    curPmt->PCR_PID = (buffer[index]&0x1F)<<8 | buffer[++index];

    curPmt->reserved_4 = (buffer[++index]&0xF0)>>4;
    curPmt->program_info_length = (buffer[index]&0x0F)<<8 | buffer[++index];

    //g_print("Parsing streams... section_length: %d, index: %d...\n", curPmt->section_length, index);

    //adjust index to skip the desciptors...
    index += curPmt->program_info_length;


    // Do we have enough buffer to parse the programs?
    if( bufferSize < (MPEGTS_PMT_PRE_SEC_LEN_BYTES + curPmt->section_length) )
    {
        g_print("PMT buffer too small (no programs) bufferSize: %d, index: %d, sec_len: %d\n", 
            bufferSize, index, curPmt->section_length );
        return -1;
    }

    //g_print("Parsing streams... section_length: %d, index: %d...\n", curPmt->section_length, index);

    // This is where the CRC should start... Used to end program parsing loop
    crcMarker = curPmt->section_length + MPEGTS_PMT_PRE_SEC_LEN_BYTES - MPEGTS_CRC_LEN;

    //start parsing the programs
    for( x = 0; index < crcMarker; x++ )
    {

        if( bufferSize < (index + 6) )
        {
            g_print("PMT buffer too small (partial programs) bufferSize: %d, index: %d, sec_len: %d\n", 
                bufferSize, index, curPmt->section_length );
            return -1;
        }

        //g_print("\nParsing stream %d, index %d...\n", x, index);
        curPmt->programs[x].stream_type = (buffer[++index]);

        curPmt->programs[x].reserved_1 = (buffer[++index]&0xE0)>>5;
        curPmt->programs[x].elementary_PID = (buffer[index]&0x1F)<<8 | buffer[++index];

        curPmt->programs[x].reserved_2 = (buffer[++index]&0xF0)>>4;
        curPmt->programs[x].ES_info_length = (buffer[index]&0x0F)<<8 | buffer[++index];

        curPmt->programs[x].numDesc = 0;
        programMarker = index;

        // parse descriptors
        while( index + 3 < bufferSize &&
            index < (programMarker + curPmt->programs[x].ES_info_length) )
        {
            curPmt->programs[x].theDesc[curPmt->programs[x].numDesc].desc_tag = buffer[++index];
            curPmt->programs[x].theDesc[curPmt->programs[x].numDesc].desc_len = buffer[++index];

            index += curPmt->programs[x].theDesc[curPmt->programs[x].numDesc].desc_len;

            curPmt->programs[x].numDesc++;
        }

        //adjust index to skip descriptors...
        //index += curPmt->programs[x].ES_info_length;
        //g_print("\nParsing stream %d, ES_len %d, index %d...\n", x, curPmt->programs[x].ES_info_length, index);

        curPmt->num_progs_parsed++;
    }

    if( bufferSize < (index + MPEGTS_CRC_LEN) )
    {
        g_print("PMT buffer too small (no CRC) bufferSize: %d, index: %d, sec_len: %d\n", 
            bufferSize, index, curPmt->section_length );
        return -1;
    }

    curPmt->CRC = (buffer[++index])<<24 | (buffer[++index])<<16 | (buffer[++index])<<8 | (buffer[++index]);

    return 0;
}

static void printPMT( tMpegTsPmt *curPmt )
{
    int x;
    if( NULL == curPmt )
    {
        g_print("Failed to print NULL PMT\n");
        return;
    }

    g_print("Dumping PMT pid (%d)...", curPmt->program_map_PID);
    g_print("\tTable_id: 0x%x\n", curPmt->table_id);
    if( curPmt->section_syntax_indicator )
        g_print("\tsection_syntax_indicator: True(1)\n");
    else
        g_print("\tsection_syntax_indicator: False(0)\n");
    g_print("\treserved_1: 0x%x\n", curPmt->reserved_1);
    g_print("\tsection_length: 0x%x (%d)\n",  curPmt->section_length, curPmt->section_length);

    g_print("\tprogram_number: 0x%x\n", curPmt->program_number);

    g_print("\treserved_2: 0x%x\n", curPmt->reserved_2);
    g_print("\tversion_number: 0x%x\n", curPmt->version_number);
    if( curPmt->current_next_indicator )
        g_print("\tcurrent_next_indicator: True(1)\n");
    else
        g_print("\tcurrent_next_indicator: False(0)\n");
    g_print("\tsection_number: 0x%x\n", curPmt->section_number);
    g_print("\tlast_section_number: 0x%x\n", curPmt->last_section_number);

    g_print("\treserved_3: 0x%x\n", curPmt->reserved_3);
    g_print("\tPCR_PID: 0x%x\n", curPmt->PCR_PID);

    g_print("\treserved_4: 0x%x\n", curPmt->reserved_4);
    g_print("\tprogram_info_length: 0x%x\n", curPmt->program_info_length);


    // print the programs found
    for( x = 0 ; x < curPmt->num_progs_parsed ; x++ )
    {
        g_print("\tProgram #%d:\n", x);
        g_print("\t\tstream_type: 0x%x\n", curPmt->programs[x].stream_type);
        g_print("\t\treserved_1: 0x%x\n", curPmt->programs[x].reserved_1);
        g_print("\t\telementary_PID: 0x%x\n", curPmt->programs[x].elementary_PID);
        g_print("\t\treserved_2: 0x%x\n", curPmt->programs[x].reserved_2);
        g_print("\t\tES_info_length: 0x%x\n", curPmt->programs[x].ES_info_length);
    }

    g_print("\tCRC: 0x%x\n", curPmt->CRC);
}


////////////////////////////////////////////////////////////////////////////////
// CGMI callbacks
////////////////////////////////////////////////////////////////////////////////

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

static cgmi_Status cgmi_SectionBufferCallback_PMT(
    void *pUserData,
    void *pFilterPriv,
    void *pFilterId,
    cgmi_Status sectionStatus,
    char *pSection,
    int sectionSize)
{
    cgmi_Status retStat;
    tMpegTsPmt curPmt;

    //g_print( "cgmi_QueryBufferCallback -- pFilterId: 0x%08lx \n", pFilterId );

    if( NULL == pSection )
    {
        g_print("NULL buffer passed to cgmiSectionBufferCallback.\n");
        return CGMI_ERROR_BAD_PARAM;
    }

    g_print("Received section pFilterId: 0x%lx, sectionSize %d\n\n", pFilterId, sectionSize);
    printHex( pSection, sectionSize );
    g_print("\n\n");

    parsePMT( &curPmt, pSection, sectionSize );
    printPMT( &curPmt );

    gPmtParsed = true;

    g_print("Calling cgmi_StopSectionFilter...\n");
    retStat = cgmi_StopSectionFilter( pFilterPriv, pFilterId );
    CHECK_ERROR(retStat);

    /*
    g_print("Calling cgmi_DestroySectionFilter...\n");
    retStat = cgmi_DestroySectionFilter( pFilterPriv, pFilterId );
    CHECK_ERROR(retStat);
    */
    // Free buffer allocated in cgmi_QueryBufferCallback
    g_free( pSection );


    return CGMI_ERROR_SUCCESS;
}


static tMpegTsPat gPat;

static cgmi_Status cgmi_SectionBufferCallback_PAT(
    void *pUserData,
    void *pFilterPriv,
    void *pFilterId,
    cgmi_Status sectionStatus,
    char *pSection,
    int sectionSize)
{
    cgmi_Status retStat;
    tMpegTsPat pat;
    tcgmi_FilterData filterData;
    void *pNewFilterId = NULL;

    //g_print( "cgmi_QueryBufferCallback -- pFilterId: 0x%08lx \n", pFilterId );

    if( NULL == pSection )
    {
        g_print("NULL buffer passed to cgmiSectionBufferCallback.\n");
        return CGMI_ERROR_BAD_PARAM;
    }

    g_print("Received section pFilterId: 0x%lx, sectionSize %d\n\n", pFilterId, sectionSize);
    printHex( pSection, sectionSize );
    g_print("\n\n");

    parsePAT( pSection, sectionSize, &gPat );
    printPAT( &gPat );
    gPat.parsed = true;

    g_print("Calling cgmi_StopSectionFilter...\n");
    retStat = cgmi_StopSectionFilter( pFilterPriv, pFilterId );
    CHECK_ERROR(retStat);

    /*
    g_print("Calling cgmi_DestroySectionFilter...\n");
    retStat = cgmi_DestroySectionFilter( pFilterPriv, pFilterId );
    CHECK_ERROR(retStat);
    */

#if 0
// Disabled until it works properly. 
    // Ok now that we've found the PAT find the first PMT (if we have one)
    if( pat.numPMTs > 0 )
    {
        void *pNewFilterId = NULL;
        g_print("Calling cgmi_CreateSectionFilter...\n");
        retStat = cgmi_CreateSectionFilter( pFilterPriv, pFilterPriv, &pNewFilterId );
        CHECK_ERROR(retStat);

        // Use the PID of the first PMT
        filterData.pid = pat.pmts[0].program_map_PID;
        filterData.value = NULL;
        filterData.mask = NULL;
        filterData.length = 0;
        filterData.comparitor = FILTER_COMP_EQUAL;

        g_print("Calling cgmi_SetSectionFilter... for filterId 0x%08lx\n", pNewFilterId);
        retStat = cgmi_SetSectionFilter( pFilterPriv, pNewFilterId, &filterData );
        CHECK_ERROR(retStat);


        g_print("Calling cgmi_StartSectionFilter...\n");
        retStat = cgmi_StartSectionFilter( pFilterPriv, pNewFilterId, 10, 1, 0, cgmi_QueryBufferCallback, cgmi_SectionBufferCallback_PMT );
        CHECK_ERROR(retStat);
    }
#endif

    // Free buffer allocated in cgmi_QueryBufferCallback
    g_free( pSection );


    return CGMI_ERROR_SUCCESS;
}

static cgmi_Status cgmiTestUserDataBufferCB(void *pUserData, void *pBuffer)
{
    GstBuffer *pGstBuff = (GstBuffer *)pBuffer;
    guint8 *bufferData;
    guint bufferSize;

    if( NULL == pGstBuff )
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    bufferData = GST_BUFFER_DATA( pGstBuff );
    bufferSize = GST_BUFFER_SIZE( pGstBuff );

    // Dump data recieved.
    g_print("cgmiTestUserDataBufferCB called with buffer of size (%d)...\n",
        bufferSize);
    printHex( bufferData, bufferSize );
    g_print("\n");

    // Free buffer
    gst_buffer_unref( pGstBuff );

    return CGMI_ERROR_SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// Convenience functions
////////////////////////////////////////////////////////////////////////////////
static cgmi_Status startPlayback(const char *url, void **ppSessionId)
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    void *pSessionId;

    /****** Start playback *******/
    g_print("Calling cgmi_Init...\n");
    retStat = cgmi_Init();
    CHECK_ERROR_RETURN_STAT(retStat);

    g_print("Calling cgmi_CreateSession...\n");
    retStat = cgmi_CreateSession( cgmiCallback, NULL, &pSessionId );
    CHECK_ERROR_RETURN_STAT(retStat);
    g_print("create session returned sessionId = (%lx)\n", (tCgmiDbusPointer)pSessionId);
    *ppSessionId = pSessionId;

    g_print("Calling cgmi_Load...\n");
    retStat = cgmi_Load( pSessionId, url );
    CHECK_ERROR_RETURN_STAT(retStat);

    g_print("Calling cgmi_Play...\n");
    retStat = cgmi_Play( pSessionId );
    CHECK_ERROR_RETURN_STAT(retStat);

    return retStat;
}

static cgmi_Status stopPlayback(void *pSessionId)
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;

    /****** Start playback *******/
    g_print("Calling cgmi_Unload...\n");
    retStat = cgmi_Unload( pSessionId );
    CHECK_ERROR_RETURN_STAT(retStat);

    g_print("Calling cgmi_DestroySession...\n");
    retStat = cgmi_DestroySession( pSessionId );
    CHECK_ERROR_RETURN_STAT(retStat);

    g_print("Calling cgmi_Term...\n");
    retStat = cgmi_Term();
    CHECK_ERROR_RETURN_STAT(retStat);

    return retStat;
}

static cgmi_Status buildSectionFilterPid(void *pSessionId, int pid, sectionBufferCB sectionCB, void **ppFilterId)
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    tcgmi_FilterData filterData;
    void *filterId = NULL;

    /* Create section filter */
    g_print("Calling cgmi_CreateSectionFilter...\n");
    retStat = cgmi_CreateSectionFilter( pSessionId, pSessionId, &filterId );
    *ppFilterId = filterId;
    CHECK_ERROR_RETURN_STAT(retStat);

    /*  Init filter data */
    filterData.pid = pid;
    filterData.value = NULL;
    filterData.mask = NULL;
    filterData.length = 0;
    filterData.comparitor = FILTER_COMP_EQUAL;

    g_print("Calling cgmi_SetSectionFilter... for filterId 0x%08lx\n", filterId);
    retStat = cgmi_SetSectionFilter( pSessionId, filterId, &filterData );
    CHECK_ERROR_RETURN_STAT(retStat);

    g_print("Calling cgmi_StartSectionFilter...\n");
    retStat = cgmi_StartSectionFilter( pSessionId, filterId, 10, 1, 0, cgmi_QueryBufferCallback, sectionCB );
    CHECK_ERROR_RETURN_STAT(retStat);

    return retStat;
}

////////////////////////////////////////////////////////////////////////////////
// Fairly extensive section filter test
////////////////////////////////////////////////////////////////////////////////
static cgmi_Status verifySectionFilter( void *pSessionId )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    void *filterId_1 = NULL;
    void *filterId_2 = NULL;
    void *filterId_3 = NULL;
    void *filterId_4 = NULL;
    int idx = 0;

    int testFilterPid;
    sectionBufferCB testFilterCallback;


    /**
     *  Verify we can create and destroy the same filter a few times
     */

    for( idx = 0; idx < 5; idx++ )
    {
        g_print(">>>>  Testing filter creation and destruction #(%d) <<<<\n\n", idx);
        // Init PAT to not parsed
        gPat.parsed = false;

        // Create and start one filter
        retStat = buildSectionFilterPid( pSessionId, 0x0, 
            cgmi_SectionBufferCallback_PAT, &filterId_1 );
        CHECK_ERROR_RETURN_STAT(retStat);

        // Wait half a second for a callback to fire
        g_usleep( 500 * 1000);

        // Did it the CB fire?
        if( gPat.parsed == true )
        {
            g_print("Section filter callback fired.");
        }
        else
        {
            g_print("ERROR:  PAT Section filter failed to fire.");
            return CGMI_ERROR_FAILED;
        }

        // The PAT callback calls Stop.  Just destroy the filter.
        g_print("Calling cgmi_DestroySectionFilter (%d)...\n", idx);
        retStat = cgmi_DestroySectionFilter( pSessionId, filterId_1 );
        CHECK_ERROR_RETURN_STAT(retStat);
    }

    /**
     *  Verify we can create the maximum number of filters.  (3 is max currently)
     */

    //
    // Create a PAT filter
    //
    retStat = buildSectionFilterPid( pSessionId, 0x0, 
        cgmi_SectionBufferCallback_PAT, &filterId_1 );
    CHECK_ERROR_RETURN_STAT(retStat);

    // Wait half a second for a callback to fire
    g_usleep( 500 * 1000);

    // Did it the CB fire?
    if( gPat.parsed == true )
    {
        g_print("Section filter callback fired for PAT.");
    }
    else
    {
        g_print("ERROR:  PAT Section filter failed to fire.");
        return CGMI_ERROR_FAILED;
    }

    // If we have a PMT set the callback and pid to use
    if( gPat.numPMTs > 0 )
    {
        // Use PMT as the section filter callback
        testFilterCallback = cgmi_SectionBufferCallback_PMT;
        testFilterPid = gPat.pmts[0].program_map_PID;
    }
    else
    {
        g_print("ERROR:  Failed to find PMT in PAT.");
        return CGMI_ERROR_FAILED;
    }

    //
    // Create 1st PMT filter
    //
    gPmtParsed = false;
    retStat = buildSectionFilterPid( pSessionId, testFilterPid, 
        testFilterCallback, &filterId_2 );
    CHECK_ERROR_RETURN_STAT(retStat);

    // Wait half a second for a callback to fire
    g_usleep( 500 * 1000);

    // Did it the CB fire?
    if( gPmtParsed == true )
    {
        g_print("Section filter callback fired for PMT 1.");
    }
    else
    {
        g_print("ERROR:  PAT Section filter failed to fire.");
        return CGMI_ERROR_FAILED;
    }


    //
    // Create 2nd PMT filter
    //
    gPmtParsed = false;
    retStat = buildSectionFilterPid( pSessionId, testFilterPid, 
        testFilterCallback, &filterId_3 );
    CHECK_ERROR_RETURN_STAT(retStat);

    // Wait half a second for a callback to fire
    g_usleep( 500 * 1000);

    // Did it the CB fire?
    if( gPmtParsed == true )
    {
        g_print("Section filter callback fired for PMT 2.\n");
    }
    else
    {
        g_print("ERROR:  PAT Section filter failed to fire.\n");
        return CGMI_ERROR_FAILED;
    }

    //
    // Attempt to create 4th filter without closing others, should fail
    // NOTE: This test should be updated to create filters until a graceful
    // failure is detected instead of assuming a max of 3.
    //
    g_print("Creating 4th section filter that should fail...\n");
    retStat = cgmi_CreateSectionFilter( pSessionId, pSessionId, &filterId_4 );

    if( retStat != CGMI_ERROR_SUCCESS )
    {
        g_print("As expected the 4th filter failed to create verifying TSDEMUX error reporting.\n");
    }
    else
    {
        g_print("ERROR:  The 4th filter creation was reported successful, and we only support 3 filters.\n");
        return CGMI_ERROR_FAILED;
    }

    // Destroy the 3 successful filters
    g_print("Calling cgmi_DestroySectionFilter 1...\n");
    retStat = cgmi_DestroySectionFilter( pSessionId, filterId_1 );
    CHECK_ERROR_RETURN_STAT(retStat);

    g_print("Calling cgmi_DestroySectionFilter 2...\n");
    retStat = cgmi_DestroySectionFilter( pSessionId, filterId_2 );
    CHECK_ERROR_RETURN_STAT(retStat);

    g_print("Calling cgmi_DestroySectionFilter 3...\n");
    retStat = cgmi_DestroySectionFilter( pSessionId, filterId_3 );
    CHECK_ERROR_RETURN_STAT(retStat);

    /**
     *  Verify yet another filter can still be created and destroyed.
     */
    gPmtParsed = false;
    retStat = buildSectionFilterPid( pSessionId, testFilterPid, 
        testFilterCallback, &filterId_4 );
    CHECK_ERROR_RETURN_STAT(retStat);

    // Wait half a second for a callback to fire
    g_usleep( 500 * 1000);

    // Did it the CB fire?
    if( gPmtParsed == true )
    {
        g_print("Section filter callback fired for PMT last.");
    }
    else
    {
        g_print("ERROR:  PMT Section filter failed to fire.");
        return CGMI_ERROR_FAILED;
    }

    g_print("Calling cgmi_DestroySectionFilter of last filter...\n");
    retStat = cgmi_DestroySectionFilter( pSessionId, filterId_4 );
    CHECK_ERROR_RETURN_STAT(retStat);

    g_print("\n");
    g_print("*************************************************************\n\n");
    g_print(">>>>>>   Section filtering verification SUCCESSFUL!   <<<<<<<\n\n");
    g_print("*************************************************************\n\n");

    return CGMI_ERROR_SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// Verify section filters work as expected
////////////////////////////////////////////////////////////////////////////////
static int secfil( const char *url )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    void *pSessionId;

    retStat = startPlayback(url, &pSessionId);
    CHECK_ERROR_RETURN_STAT(retStat);

    // Wait for playback to start and demux to be created
    g_usleep(2 * 1000 * 1000);

    retStat = verifySectionFilter(pSessionId);
    CHECK_ERROR_RETURN_STAT(retStat);

    retStat = stopPlayback(pSessionId);
    CHECK_ERROR_RETURN_STAT(retStat);

    return retStat;
}
////////////////////////////////////////////////////////////////////////////////
// A quick sanity test of the CGMI APIs
////////////////////////////////////////////////////////////////////////////////

static int sanity( const char *url )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    void *pSessionId;

    retStat = startPlayback(url, &pSessionId);
    CHECK_ERROR_RETURN_STAT(retStat);

    /****** Call all other API's after starting playback *******/
#if 1
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

    int ii = 0;
    float rates[32];
    unsigned int numRates = 32;
    g_print("Calling cgmi_GetRates...\n");
    retStat = cgmi_GetRates( pSessionId, rates, &numRates );
    CHECK_ERROR(retStat);
    g_print("cgmi_GetRates : numRates = (%u)\n", numRates);
    if(numRates > 0)
    {
       g_print("Following rates are supported\n");
       for(ii = 0; ii <= (int)numRates - 2; ii++) 
       {
          g_print("%f, ", rates[ii]);
       }
       g_print("%f\n", rates[ii]);
    }

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

#endif

    // Let it play for a few more seconds
    g_usleep(2 * 1000 * 1000);

    /* Create section filter */
    void *filterId = NULL;
    void *filterId2 = NULL;
    g_print("Calling cgmi_CreateSectionFilter...\n");
    retStat = cgmi_CreateSectionFilter( pSessionId, pSessionId, &filterId );
    CHECK_ERROR(retStat);

    /* */

    tcgmi_FilterData filterData;
    filterData.pid = 0x0;
    filterData.value = NULL;
    filterData.mask = NULL;
    filterData.length = 0;
    filterData.comparitor = FILTER_COMP_EQUAL;

    g_print("Calling cgmi_SetSectionFilter... for filterId 0x%08lx\n", filterId);
    retStat = cgmi_SetSectionFilter( pSessionId, filterId, &filterData );
    CHECK_ERROR(retStat);


    g_print("Calling cgmi_StartSectionFilter...\n");
    retStat = cgmi_StartSectionFilter( pSessionId, filterId, 10, 1, 0, cgmi_QueryBufferCallback, cgmi_SectionBufferCallback_PAT );
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

#if 1
    g_usleep( 700 * 1000);
    // Ok now that we've found the PAT find the first PMT (if we have one)
    if( gPat.numPMTs > 0 )
    {
        /* Create section filter */
        g_print("Calling cgmi_CreateSectionFilter...\n");
        retStat = cgmi_CreateSectionFilter( pSessionId, pSessionId, &filterId2 );
        CHECK_ERROR(retStat);


        // Use the PID of the first PMT
        filterData.pid = gPat.pmts[0].program_map_PID;
        filterData.value = NULL;
        filterData.mask = NULL;
        filterData.length = 0;
        filterData.comparitor = FILTER_COMP_EQUAL;

        g_print("Calling cgmi_SetSectionFilter... for filterId 0x%08lx\n", filterId2);
        retStat = cgmi_SetSectionFilter( pSessionId, filterId2, &filterData );
        CHECK_ERROR(retStat);


        g_print("Calling cgmi_StartSectionFilter...\n");
        retStat = cgmi_StartSectionFilter( pSessionId, filterId2, 10, 1, 0, cgmi_QueryBufferCallback, cgmi_SectionBufferCallback_PMT );
        CHECK_ERROR(retStat);
    }
#endif

#if 1
    g_usleep( 1 * 1000 * 1000);

    g_print("Calling cgmi_startUserDataFilter...\n");
    retStat = cgmi_startUserDataFilter( pSessionId, cgmiTestUserDataBufferCB, pSessionId );
    CHECK_ERROR(retStat);

    g_usleep( 500 * 1000);

    g_print("Calling cgmi_stopUserDataFilter...\n");
    retStat = cgmi_stopUserDataFilter( pSessionId, cgmiTestUserDataBufferCB );
    CHECK_ERROR(retStat);
#endif

    // Let it play for a few more seconds
    g_usleep(4 * 1000 * 1000);

    g_print("Calling cgmi_DestroySectionFilter...\n");
    retStat = cgmi_DestroySectionFilter( pSessionId, filterId );
    CHECK_ERROR(retStat);

    g_print("Calling cgmi_DestroySectionFilter filterId2...\n");
    retStat = cgmi_DestroySectionFilter( pSessionId, filterId2 );
    CHECK_ERROR(retStat);


    /****** Shut down session and clean up *******/
    retStat = stopPlayback(pSessionId);
    CHECK_ERROR_RETURN_STAT(retStat);

    return retStat;
}


////////////////////////////////////////////////////////////////////////////////
// Make all the calls to start playing video
////////////////////////////////////////////////////////////////////////////////

static int play( const char *url )
{
    cgmi_Status retStat = CGMI_ERROR_SUCCESS;
    void *pSessionId;

    // Start playback
    retStat = startPlayback(url, &pSessionId);
    CHECK_ERROR_RETURN_STAT(retStat);

    // Let it play for a few seconds.
    g_usleep(10 * 1000 * 1000);

    retStat = stopPlayback(pSessionId);
    CHECK_ERROR_RETURN_STAT(retStat);

    return retStat;
}

int main(int argc, char **argv)
{
    int retStat = 0;

    if (argc < 2)
    {
        g_print("usage: %s play <url> | sanity <url> | secfil <url>\n", argv[0]);
        return -1;
    }

    if (strcmp(argv[1], "sanity") == 0)
    {
        if (argc < 3)
        {
            g_print("usage: %s sanity <url>\n", argv[0]);
            return -1;
        }
        retStat = sanity(argv[2]);

    }
    else if (strcmp(argv[1], "play") == 0)
    {
        if (argc < 3)
        {
            g_print("usage: %s play <url>\n", argv[0]);
            return -1;
        }
        retStat = play(argv[2]);
    }    
    else if (strcmp(argv[1], "secfil") == 0)
    {
        if (argc < 3)
        {
            g_print("usage: %s secfil <url>\n", argv[0]);
            return -1;
        }
        retStat = secfil(argv[2]);
    }

    return retStat;
}
