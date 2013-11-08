#include "api.hpp"
#include "SailErr.h"
static ui32 MAX_CLIENT_HANDLES = 2;
#define RECORD_LIST_SIZE      100

typedef struct
{
   ui32           index;
   SessionHandle  sessH;
   int        lock;
   ui32           srcId;
}tDvrHandle;

typedef struct
{
   ui32 max;
   ui32 count;
   tRecordInfo *list;
}tRecordList;

static tDvrHandle *gHandle = NULL;


SailError IsMediaRecorderAvailable( bool* available )
{
   if ( available == NULL ) return SailErr_BadParam;

   if ( gHandle != NULL )
   {
      *available = true;
   }
   else
   {
      *available = false;
   }

   return SailErr_None;
}
SailError MediaRecorderStartAssetRecord( const char* asset, char* source, int time_len_ms )
{
   SailErr retCode = SailErr_None;

   API_INFO("asset: %s source: %s time_len_ms: %d\n", asset, source, time_len_ms);




   return retCode;
}

SailError MediaRecorderStopAssetRecord( const char* asset )
{
   if ( gHandle == NULL ) return SailErr_NotSupported;

   if ( asset == NULL ) return SailErr_BadParam;

   SailErr retCode = SailErr_None;

   API_INFO("asset: %s\n", asset);


#if 0
   if ( handle == NULL ) 
   {
      API_WARN(("ERROR: asset (%s) not found\n", asset));
      return SailErr_BadParam;
   }

   retCode = stopRecording( handle );

#endif
   return retCode;
}

SailError MediaRecorderDeleteAssetRecord( const char* asset )
{
   if ( gHandle == NULL ) return SailErr_NotSupported;

   if ( asset == NULL ) return SailErr_BadParam;


   API_INFO("asset: %s\n", asset);


   return SailErr_None;
}

SailError MediaRecorderGetAssetRecordInfo( const char* asset, uint64_t* pSizeInMS, uint64_t* pSizeInByte )
{
   if ( gHandle == NULL ) return SailErr_NotSupported;

   if ( (asset == NULL) || (pSizeInMS == NULL) || (pSizeInByte == NULL) ) return SailErr_BadParam;

   SailErr retCode = SailErr_None;
   return retCode;
}

SailError MediaRecorderStartTimeshift( int mediaPlayerId, uint64_t bufferSizeInMs )
{
   return SailErr_NotSupported;
}

SailError MediaRecorderStopTimeshift( int mediaPlayerId )
{
   return SailErr_NotSupported;
}

SailError MediaRecorderGetNumberOfAssets( int* pCount )
{
   SailErr err= SailErr_None;
   if ( gHandle == NULL ) return SailErr_NotSupported;

   if ( pCount == NULL ) 
   {
      API_WARN("ERROR: Bad parameter pCount == NULL\n");
      return SailErr_BadParam;
   }


   return err;
}


SailError MediaRecorderGetAssetInfo( int assetIndex, tRecordInfo **info )
{
   if ( gHandle == NULL ) return SailErr_NotSupported;



   return SailErr_None;
}

void MediaRecorderInit( void )
{
}

void MediaRecorderFinalize( void )
{
   if ( gHandle == NULL ) return;
   
}

