#ifndef __SAIL_LOG_H__
#define __SAIL_LOG_H__

/**
 * @file log.h
 * 
 * @brief Public APIs for logging.
 * 
 * For this module to be active, SAIL_LOGGING_SUPPORT has to be defined
 * at compile time.
 * 
 */

#ifdef __cplusplus
extern "C" {
#endif

// ====== includes ======
//#include <SailSystem.h>

// ====== defines ======
#define DEBUG_LOG_NAME_LEN_MAX      ( (ui32)10 )
#define DEBUG_LOG_HANDLE_MAX        ( (ui32)64 )
#define DEBUG_LOG_LEVEL_MAX         ( (ui32)4 )

#ifdef SAIL_LOGGING_SUPPORT
#define debugLog( bCondition, Id, Level, Message ) \
   do \
   { \
      if( bCondition && debugLogVisible( Id, Level ) ) \
      { \
         debugLogPrefix( Id ); \
         debugLogPrint Message; \
      }\
   } \
   while( 0 )
#define debugHexDump( bCondition, Id, Level, ArgList ) \
   do \
   { \
      if( bCondition && debugLogVisible( Id, Level ) ) \
      { \
         debugDumpHex ArgList; \
      }\
   } \
   while( 0 )
#else
   #define debugLog( bCondition, Id, Level, Message )
   #define debugHexDump( bCondition, Id, Level, ArgList )
#endif


#define debugLogNoise( Id, Message ) \
   debugLog( true, Id, DEBUG_LOG_LEVEL_NOISE, Message )
#define debugLogInfo( Id, Message ) \
   debugLog( true, Id, DEBUG_LOG_LEVEL_INFO, Message )
#define debugLogWarn( Id, Message ) \
   debugLog( true, Id, DEBUG_LOG_LEVEL_WARN, Message )
#define debugLogFatal( Id, Message ) \
   debugLog( true, Id, DEBUG_LOG_LEVEL_FATAL, Message )

#ifdef SAIL_LOGGING_SUPPORT

#define WARNONERROR(x)  if (x) debugLogPrint("%s:%d: %s() returning error: %08X (%s)\n", __FILE__, __LINE__, __FUNCTION__, x, printError(x))
#define ENTER() debugLogPrint("%s() entering...\n", __FUNCTION__);
#define EXIT(x) { debugLogPrint("%s() exiting...\n", __FUNCTION__); return x;}

#else

#define WARNONERROR(x)
#define ENTER()
#define EXIT(x)

#endif 

#define PRINT_RETURN_ERROR
#ifdef PRINT_RETURN_ERROR
#define RETURN(x) \
   { \
      if (x) { \
         WARNONERROR(x); \
         return x; \
      } \
      else { \
         return x; \
      } \
   }
#else
#define RETURN(x) return x
#endif 

#ifdef SAIL_LOGGING_SUPPORT
#define NOISE( Message )                        \
   do \
   { \
      if( debugLogVisible( gDebugLogHandle, DEBUG_LOG_LEVEL_NOISE ) ) \
      { \
         debugLogPrefix( gDebugLogHandle );  \
         debugLogPrint( "%s(%d) ", __FUNCTION__, __LINE__ );  \
         debugLogPrint Message; \
      }\
   } \
   while( 0 )
#define INFO( Message )                        \
   do \
   { \
      if( debugLogVisible( gDebugLogHandle, DEBUG_LOG_LEVEL_INFO ) ) \
      { \
         debugLogPrefix( gDebugLogHandle );  \
         debugLogPrint( "%s(%d) ", __FUNCTION__, __LINE__ );  \
         debugLogPrint Message; \
      }\
   } \
   while( 0 )
#define WARN( Message )                        \
   do \
   { \
      if( debugLogVisible( gDebugLogHandle, DEBUG_LOG_LEVEL_WARN ) ) \
      { \
         debugLogPrefix( gDebugLogHandle );  \
         debugLogPrint( "%s(%d) ", __FUNCTION__, __LINE__ );  \
         debugLogPrint Message; \
      }\
   } \
   while( 0 )
#define FATAL( Message )                        \
   do \
   { \
      if( debugLogVisible( gDebugLogHandle, DEBUG_LOG_LEVEL_FATAL ) ) \
      { \
         debugLogPrefix( gDebugLogHandle );  \
         debugLogPrint( "%s(%d) ", __FUNCTION__, __LINE__ );  \
         debugLogPrint Message; \
      }\
   } \
   while( 0 )

#else
#define NOISE(Message)
#define INFO(Message)
#define WARN(Message)
#define FATAL(Message)
#endif 


// ====== enums ======

// ====== typedefs ======
//typedef SailHandle logHandle;
#define logHandle int
/**
 * Log levels.
 */
typedef enum
{
   /**
    * Noise is the lowest level and is typically used when you
    * rarely want to see this output.
    */
   DEBUG_LOG_LEVEL_NOISE,
   /**
    * Info is the normal level for most debugging.
    */
   DEBUG_LOG_LEVEL_INFO,
   /**
    * Warn is the default level. Messages sent at the WARN level
    * should be things that need to be noticed.
    */
   DEBUG_LOG_LEVEL_WARN,
   /**
    * Fatal messages are non-maskable. Message should be sent at
    * this level when something catostrophic has happened.
    */
   DEBUG_LOG_LEVEL_FATAL,
   DEBUG_LOG_LEVEL_UNKNOWN
}tDebugLogLevel;

typedef struct
{
   char           name[DEBUG_LOG_NAME_LEN_MAX];
   tDebugLogLevel level;
}tDebugLogEntry;

// ====== prototypes ======

/**
 * Public API to initialize logging facility.
 * 
 * @return SailErr
 */
SailErr debugLogInit( void );

/**
 * Public API to finalize logging facility.
 * 
 * @return SailErr
 */
SailErr debugLogFinalize( void );

/**
 * Public API to add a unique module for logging.
 * 
 * @param handle  [out] logHandle pointer returned for handle to
 *                APIs.
 * 
 * @param modName The string name for this log. The maximum
 *                length is 8 characters.
 * 
 * @return SailErr
 */
SailErr debugLogAddModule( logHandle *handle, const char* modName );

/**
 * Public API to set the output level of the log messages.
 * 
 * @param handle  logHandle returned in call to
 *                debugLogAddModule()
 * 
 * @param Level   The level to set as defined by tDebugLogLevel.
 * 
 * @return SailErr
 */
SailErr debugLogLevelSet( logHandle handle, tDebugLogLevel Level );

/**
 * Public API to remove a module from the logging facility.
 * 
 * @param handle  logHandle returned in call to
 *                debugLogAddModule()
 * 
 * @return SailErr
 */
SailErr debugLogRemoveModule( logHandle handle );

/**
 * Public API to retrieve the currently registered modules.
 * 
 * @param list[out]     List to be filled by this function.
 * 
 * @param count[in/out] Input:   The maximum size of list
 *                               parameter.
 *                      Output:  The actual number of entries
 *                               added to list parameter.
 * 
 * @return SailErr
 */
SailErr debugLogListGet( tDebugLogEntry *list, ui32 *count );

/**
 * Public API to determine if a message will be printed based on
 * \b Level.
 * 
 * @param handle  logHandle returned in call to
 *                debugLogAddModule()
 * 
 * @param Level   The level to compare.
 * 
 * @return boolean
 */
boolean debugLogVisible( logHandle handle, tDebugLogLevel Level );

/**
 * Public API to send output through the logging facility.
 * 
 * @param sMessage   String message with variable arguments.
 */
void debugLogPrint( const char* sMessage, ... );

/**
 * Private API to prefix a message with the log module name.
 * 
 * @param handle  logHandle returned in call to
 *                debugLogAddModule()
 */
void debugLogPrefix( logHandle handle );

/**
 * Public API to set the log level of a module.
 * This interface is used for console input.
 * 
 * @param facility   String name of the facility.
 * 
 * @param level      String level to be set.
 *                   Possible values are:
 *                   - \b noise
 *                   - \b info
 *                   - \b warn
 *                   - \b fatal
 * 
 * @note Although the entire string level can be sent, only the
 *       first character is compared.
 */
void setDebugLevel( const char * facility, const char * level );

/**
 * Public API to get the log level of a module.
 * 
 * @param facility   String name of the facility.
 * 
 * @return tDebugLogLevel
 */
tDebugLogLevel getDebugLevel( const char * facility );

/**
 * 
 * @param handle
 * @param sMessage
 */
void debugLogWrite( logHandle handle, const char* sMessage, ... );


#ifdef __cplusplus
}
#endif

#endif
