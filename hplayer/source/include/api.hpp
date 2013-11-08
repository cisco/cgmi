#ifndef __API_HPP__
#define __API_HPP__

#include <SailInfCommon.h>
#include "stdio.h"
#define API_NOISE(fmt, ...) \
   do \
   { \
      printf("API_NOISE: %s (%d)", __FUNCTION__, __LINE__); \
      printf(fmt, ##__VA_ARGS__); \
      fflush(stdout);\
   } \
   while( 0 )
#define API_INFO( fmt, ... )                        \
   do \
   { \
      printf("API_INFO: %s (%d)", __FUNCTION__, __LINE__); \
      printf(fmt, ##__VA_ARGS__); \
      fflush(stdout);\
   } \
   while( 0 )
#define API_WARN( fmt, ... )                        \
   do \
   { \
      printf("API_WARN: %s (%d)", __FUNCTION__, __LINE__); \
      printf(fmt, ##__VA_ARGS__); \
      fflush(stdout);\
   } \
   while( 0 )
#define API_FATAL( fmt, ... )                        \
   do \
   { \
      printf("API_FATAL: %s (%d)", __FUNCTION__, __LINE__); \
      printf(fmt, ##__VA_ARGS__); \
      fflush(stdout);\
   } \
   while( 0 )



#define MAX_CLIENT_WIDTH   1920
#define MAX_CLIENT_HEIGHT  1080

//rms extern logHandle gApiLogHandle;

//
// Internal APIs used by sailng for Zappware porting layer. 
// These APIs are not given as part of the Zappware includes.
//
#if 0
void SystemInit( SessionHandle gfxHandle );
void SystemNotify( ui32 code, WORD x, WORD y, WORD z );
void SystemStartMaintenance( void );
void SystemStopMaintenance( void );
#endif

void MediaRecorderInit( void );
void MediaRecorderFinalize( void );

void MediaPlayerInit( void );
void MediaPlayerFinalize( void );
void MediaPlayerOff( void );
SailError MediaPlayerEnableTimeshift( int mediaPlayerId, uint64_t bufferSizeInMs );
SailError MediaPlayerDisableTimeshift( int mediaPlayerId );
void PlayerNotify( ui32 code, WORD x, WORD y, WORD z );
void *recordAllocHandle( void );
void recordFreeHandle( void *handle );

/**
 * This will be moved to a public API once the definition is
 * finished.
 * 
 * @param mediaPlayerId
 * @param code
 * 
 * @return SailError
 */
SailError MediaPlayerSetCopyProtection( int mediaPlayerId, int code );

void DiskInit( void );
void DiskFinalize( void );

#endif 

