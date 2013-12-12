/**
 * * \addtogroup CGMI
 * @{
*/
// -----------------------------------------------------
/**
*   \file: cgmiPlayerApi.h
*
*    \brief Cisco Gstreamer Media Player interface API.
*
*   System: RDK 1.3
*   Component Name : CGMI (Cisco Gstreamer Media Interface)
*   Status         : Version 1.0 Release 1
*   Language       : C
*
*   License        : Proprietary 
*
*   (c) Copyright Cisco Systems 2013
*
*

   Description: This header file contains the Media Player prototypes 
*                for the RDK 1.3 implementation.  It is meant to replace
*                the usage of RMF for Media Playback.
*
*    Thread Safe: Yes
*
*    \authors : Matt Snoby, Kris Kersey, Zack Wine, Chris Foster, Tankut Akgul
*    Platform Dependencies: Gstreamer 0.10.
*  \ingroup CGMI
*/

// -----------------------------------------------------




#ifndef __CGMI_PLAYER_API_H__
#define __CGMI_PLAYER_API_H__

#ifdef __cplusplus
extern "C"
{
#endif

#define SECTION_FILTER_EMPTY_PID 0x1FFF // Indicates filter shouldn't match PID

/** Function return status values
 */
typedef enum
{
   CGMI_ERROR_SUCCESS,           ///<Success
   CGMI_ERROR_FAILED,            ///<General Error
   CGMI_ERROR_NOT_IMPLEMENTED,   ///<This feature or function has not yet been implmented
   CGMI_ERROR_NOT_SUPPORTED,     ///<This feature or funtion is not supported
   CGMI_ERROR_BAD_PARAM,         ///<One of the parameters passed in is invalid
   CGMI_ERROR_OUT_OF_MEMORY,     ///<An allocation of memory has failed.
   CGMI_ERROR_TIMEOUT,           ///<A time out on a filter has occured.
   CGMI_ERROR_INVALID_HANDLE,    ///<A session handle or filter handle passed in is not correct.
   CGMI_ERROR_NOT_INITIALIZED,   ///<A function is being called when the system is not ready.
   CGMI_ERROR_NOT_OPEN,          ///<The Interface has yet to be opened.
   CGMI_ERROR_NOT_ACTIVE,        ///<This feature is not currently active
   CGMI_ERROR_NOT_READY,         ///<The feature requested can not be provided at this time.
   CGMI_ERROR_NOT_CONNECTED,     ///<The pipeline is currently not connected for the request.
   CGMI_ERROR_URI_NOTFOUND,      ///<The URL passed in could not be resolved.
   CGMI_ERROR_WRONG_STATE,       ///<The Requested state could not be set
   CGMI_ERROR_NUM_ERRORS         ///<Place Holder to know how many errors there are in the struct enum.
}cgmi_Status;

/** Event Callback types
 */
typedef enum
{

   NOTIFY_STREAMING_OK,                   ///<
   NOTIFY_FIRST_PTS_DECODED,              ///<The decoding has started for the stream and or after a seek.
   NOTIFY_STREAMING_NOT_OK,               ///< A streaming error has occurred.
   NOTIFY_SEEK_DONE,                      ///< The seek has completed
   NOTIFY_START_OF_STREAM,                ///< The Current position is now at the Start of the stream
   NOTIFY_END_OF_STREAM,                  ///< You are at the end of stream or EOF
   NOTIFY_DECRYPTION_FAILED,              ///< Not able to decrypt the stream, we don't know how to decrypt
   NOTIFY_NO_DECRYPTION_KEY,              ///<No key has been provided to decrypt this content.
   NOTIFY_VIDEO_ASPECT_RATIO_CHANGED,     ///<The stream has changed it's aspect ratio
   NOTIFY_VIDEO_RESOLUTION_CHANGED,       ///<The resolution of the stream has changed.
   NOTIFY_CHANGED_LANGUAGE_AUDIO,         ///<The streams Audio language has changed
   NOTIFY_CHANGED_LANGUAGE_SUBTITLE,      ///<The subtitle language has changed.
   NOTIFY_CHANGED_LANGUAGE_TELETEXT,      ///<The teletext language has changed.
   NOTIFY_MEDIAPLAYER_URL_OPEN_FAILURE,   ///<The requested URL could not be opened. 
   NOTIFY_MEDIAPLAYER_UNKNOWN             ///<An unexpected error has occured. 

}tcgmi_Event; 

typedef enum
{
   LIVE,                                  ///<This is a Live stream 
   TSB,                                   ///<This is a time shift buffer 
   FIXED,                                 ///<This is a normal stram or file  
   cgmi_Session_Type_UNKNOWN

}cgmi_SessionType; 

typedef enum
{
    FILTER_COMP_EQUAL,
    FILTER_COMP_NOT_EQUAL
}cgmi_FilterComparitor;

typedef struct
{
   int pid;
   unsigned char *value;
   unsigned char *mask;
   int length;
   unsigned int offset;
   cgmi_FilterComparitor comparitor;

}tcgmi_FilterData; 


typedef void (*cgmi_EventCallback)(void *pUserData, void* pSession, tcgmi_Event event );
typedef cgmi_Status (*queryBufferCB)(void *pUserData, void *pFilterPriv, void* pFilterId, char **ppBuffer, int* pBufferSize );
typedef cgmi_Status (*sectionBufferCB)(void *pUserData, void *pFilterPriv, void* pFilterId, cgmi_Status SectionStatus, char *pSection, int sectionSize);

/**
 *  \brief \b cgmi_ErrorString
 *
 *  For Debugging returns a string for the error status 
 *
 * \param   stat
 *
 * \return  String of the error status passed in 
 *
 *
 *  \ingroup CGMI
 *
 */
char* cgmi_ErrorString(cgmi_Status stat);
/**
 *  \brief \b cgmi_Init
 *
 *  Initialize the gstreamer subsystem This should not get called by anyone but the main process.
 *
 *
 * \post    On success the gstreamer subsystem is initialized and ready to play streams.
 *
 * \return  CGMI_ERROR_SUCCESS when everything has started correctly
 * \return  CGMI_ERROR_NOT_INITIALIZED when gstreamer returns an error message and can't initialize
 * \return  CGMI_ERROR_NOT_SUPPORTED when GLIB threads are not available on the platform 
 *
 *  \image html Initialization_and_Shutdown.png "How to Initialize and shutdown the subsystem"
 *
 *  \ingroup CGMI
 *
 */
cgmi_Status cgmi_Init (void);

/**
 *  \brief \b cgmi_Term
 *
 *  Terminate  the gstreamer subsystem This should not get called by anyone but the main process.
 *
 * \post    On success the gstreamer subsystem is completely shutdown and all memory is freed. 
 *
 * \return  CGMI_ERROR_SUCCESS when everything has shutdown properly and all memory freed
 *
 *  \image html Initialization_and_Shutdown.png "How to Initialize and shutdown the subsystem"
 *
 *  \ingroup CGMI
 *
 */
cgmi_Status cgmi_Term (void);

/**
 *  \brief \b cgmi_CreateSession 
 *
 *  Create a session to interact with the gstreamer frame work
 *  \param[in] eventCB  This is a function pointer for CGMI to report async events too.
 *
 *  \param[in] pUserData  This is a private userdata to be returned in callbacks
 *
 *  \param[out] pSession  This is a Pointer to a Pointer that will be filled with a unique identifier for the session for all other CGMI_ calls.
 *
 * \post    On success the user can call all the remaining CGMI api's with the provided Session handle
 *
 * \return  CGMI_ERROR_SUCCESS when everything has started correctly
 *
 *  \image html playback_trickmode.png "Hot to do playback and trick modes "
 *
 *  \ingroup CGMI
 *
 */
cgmi_Status cgmi_CreateSession (cgmi_EventCallback eventCB, void* pUserData, void **pSession );

/**
 *  \brief \b cgmi_DestroyDession 
 *
 *  Terminate and destroy the passed in session. 
 *  \param[in] pSession  This is a handle to the active session.
 *
 *  \post    On success the user can no longer call any CGMI api's and all the memory has been freed.
 *
 * \return  CGMI_ERROR_SUCCESS when everything has been terminated  correctly
 * \return  CGMI_ERROR_NOT_READY if the video is still playing and has not been Unloaded then we will return this error. 
 *
 *  \ingroup CGMI
 *
 */
cgmi_Status cgmi_DestroySession(void *pSession );

/**
 *  \brief \b cgmi_canPlay 
 *
 *  Return whether we can play this asset or not.
 *  \param[in] type  This is a string to the url that is requested to be checked to see if we can play it.
 *
 *  \param[out] pbCanPlay  The CGMI will indicate TRUE in this variable if we can play the asset or FALSE if we can not.
 *
 * \return  CGMI_ERROR_SUCCESS when the subsystem has determined if we canplay the content.
 *
 *  \image html playback_trickmode.png "How to do playback and trick modes "
 *
 *  \ingroup CGMI
 *
 */
cgmi_Status cgmi_canPlayType(const char *type, int *pbCanPlay );

/**
 *  \brief \b cgmi_Load 
 *
 *  Load the URL and get ready to play the asset 
 *  \param[in] pSession  This is a handle to the active session.
 *
 *  \param[in] uri  String that hold the location of the asset to play
 *
 *  \post    On success the user can now play the uri pointed to 
 *
 * \return  CGMI_ERROR_SUCCESS when everything is loaded and ready to play the uri 
 * \return  CGMI_ERROR_OUT_OF_MEMORY  when an allocation of memory has failed.
 * \return  CGMI_ERROR_NOT_IMPLEMENTED  when the pipeline can not be created because of a missing plugin.
 *
 *  \image html playback_trickmode.png "How to do playback and trick modes "
 *
 *  \ingroup CGMI
 *
 */
cgmi_Status cgmi_Load    (void *pSession, const char *uri );

/**
 *  \brief \b cgmi_Unload 
 *
 *  Tear down the pipeline for the current asset that is playing and or has been loaded. 
 *  \param[in] pSession  This is a handle to the active session.
 *
 *
 *  \post    On success the user can now load a new uri or shutdown the system. 
 *
 * \return  CGMI_ERROR_SUCCESS when everything is loaded and ready to play the uri 
 *
 *
 *  \image html channel_change.png "How to do playback and trick modes "
 *  \ingroup CGMI
 *
 */
cgmi_Status cgmi_Unload  (void *pSession );

/**
 *  \brief \b cgmi_Play
 *
 *  Play the asset that is currently loaded. If the uri can not be found
 *  an error will be returned in the callback.
 *  \param[in] pSession  This is a handle to the active session.
 *
 *  \pre    The Session must be open the the url must be loaded
 *
 * \return  CGMI_ERROR_SUCCESS when everything is playing
 *
 *  \image html playback_trickmode.png "How to do playback and trick modes "
 *  \image html channel_change.png "How to do playback and trick modes "
 *
 *  \ingroup CGMI
 *
 */
cgmi_Status cgmi_Play    (void *pSession);

/**
 *  \brief \b cgmi_SetRate
 *
 *  Change the playback rate of the asset, this is essentially trick modes for FF and RWD. 
 *  \param[in] pSession  This is a handle to the active session.
 *
 *  \param[in] rate  This is a handle to the active session.
 *
 *  \pre    The Session must be open the the url must be loaded
 *
 * \return  CGMI_ERROR_SUCCESS when everything is playing
 * \return  CGMI_ERROR_NOT_SUPPORTED when a rate requested is not supported on this asset.
 *
 *  \image html playback_trickmode.png "How to do playback and trick modes "
 *
 *  \ingroup CGMI
 *
 */
cgmi_Status cgmi_SetRate      (void *pSession,  float rate);

/**
 *  \brief \b cgmi_SetPosition
 *
 *  This is a request to set the currently playing stream to a different position.  This is essentially a seek cmd. 
 *  \param[in] pSession  This is a handle to the active session.
 *
 *  \param[in] position  The position you want to seek to based in seconds from the start of the stream. 
 *
 *  \pre    The Session must be open the the url must be loaded
 *  \post   The Media event NOTIFY_SEEK_DONE NOTIFY_FIRST_PTS_DECODED will be sent when the seek is complete and 
 *          when the decoding has taken place at the new postion.
 *
 * \return  CGMI_ERROR_SUCCESS when the Seek is successful is playing
 *
 *  \ingroup CGMI
 *
 */
cgmi_Status cgmi_SetPosition  (void *pSession,  float position);

/**
 *  \brief \b cgmi_GetPosition
 *
 *  This is a request to set the currently playing stream to a different position.  This is essentially a seek cmd. 
 *  \param[in] pSession  This is a handle to the active session.
 *
 *  \param[in] pPosition  The current position in seconds will be populated in this float. 
 *
 *  \pre    The Session must be open the the url must be loaded
 *
 * \return  CGMI_ERROR_SUCCESS when the location is obtained and sent back.
 *
 *  \ingroup CGMI
 *
 */
cgmi_Status cgmi_GetPosition  (void *pSession,  float *pPosition);

/**
 *  \brief \b cgmi_GetDuration
 *
 *  This is a request to set the currently playing stream to a different position.  This is essentially a seek cmd. 
 *  \param[in] pSession  This is a handle to the active session.
 *
 *  \param[out] pDuration  The Duration of the file in seconds shall be populated in this variable.
 *  \param[out]  type       Type of asset we are getting the duration for.
 *
 *  \pre    The Session must be open the the url must be loaded
 *
 * \return  CGMI_ERROR_SUCCESS when the location is obtained and sent back.
 *
 *  \ingroup CGMI
 *
 */
cgmi_Status cgmi_GetDuration  (void *pSession,  float *pDuration, cgmi_SessionType *type);

/**
 *  \brief \b cgmi_GetRateRange
 *
 *  This is a request to find out what speed trick mode that this playing asset supports. 
 *  \param[in] pSession  This is a handle to the active session.
 *
 *  \param[out] pRewind  This is the speed at which we can rewind the asset. 
 *  \param[out] pFFoward This is the speed at which we can ffwrd the asset. 
 *
 *  \pre    The Session must be open the the url must be loaded
 *
 * \return  CGMI_ERROR_SUCCESS when the location is obtained and sent back.
 * \todo Need to figure out how to handle mutiple ff or rwd speeds.
 *  \ingroup CGMI
 *
 */
cgmi_Status cgmi_GetRateRange (void *pSession,  float *pRewind, float *pFFoward );

/**
 *  \brief \b cgmi_SetVideoRectangle
 *
 *  This is a request to scale the video wrt fullscreen coordinates (0,0,1280,720)
 *  \param[in] pSession  This is a handle to the active session.
 *
 *  \param[in] x  Video destination rectangle x-position
 *
 *  \param[in] y  Video destination rectangle y-position
 *
 *  \param[in] w  Video destination rectangle width
 *
 *  \param[in] h  Video destination rectangle height
 *
 *  \pre    The Session must be open the the url must be loaded.
 *
 * \return  CGMI_ERROR_SUCCESS when the API succeeds
 *  \ingroup CGMI
 *
 */
cgmi_Status cgmi_SetVideoRectangle (void *pSession, int x, int y, int w, int h );

/**
 *  \brief \b cgmi_GetNumAudioLanguages
 *
 *  This is a request to find out how many audio languages the currently loaded asset has.
 *  \param[in] pSession  This is a handle to the active session.
 *
 *  \param[out] count  this int will be populated with the nubmer of audio languages the current asset has. 
 *
 *  \pre    The Session must be open the the url must be loaded
 *
 * \return  CGMI_ERROR_SUCCESS when the API succeeds
 *  \ingroup CGMI
 *
 *  \image html audio_language_selection.png "How to do Audio Language Selection"
 */
cgmi_Status cgmi_GetNumAudioLanguages (void *pSession,  int *count);

/**
 *  \brief \b cgmi_GetAudioLangInfo
 *
 *  This is to find out ISO-639 language code of the requested stream.
 *  \param[in] pSession  This is a handle to the active session.
 *
 *  \param[in] index  Index of the audio stream in the returned number of available languages
 *
 *  \param[in] buf    Buffer to write the ISO-639 code in
 *
 *  \param[in] bufSize Size of the buffer passed in
 *
 *  \pre    The Session must be open the the url must be loaded.
 *
 * \return  CGMI_ERROR_SUCCESS when the API succeeds
 *  \ingroup CGMI
 *
 *  \image html audio_language_selection.png "How to do Audio Language Selection"
 */
cgmi_Status cgmi_GetAudioLangInfo (void *pSession, int index, char* buf, int bufSize);

/**
 *  \brief \b cgmi_SetAudioStream
 *
 *  This is to select the audio language from the available list of languages
 *  \param[in] pSession  This is a handle to the active session.
 *
 *  \param[in] index     Index of the audio stream in the returned number of available languages.
 *
 * \return  CGMI_ERROR_SUCCESS when the API succeeds
 *  \ingroup CGMI
 *
 *  \pre    The Session must be open the the url must be loaded.
 *
 *  \image html audio_language_selection.png "How to do Audio Language Selection"
 */
cgmi_Status cgmi_SetAudioStream (void *pSession, int index);

/**
 *  \brief \b cgmi_SetDefaultAudioLang
 *
 *  This is a request to specify the default audio language.
 *  \param[in] pSession  This is a handle to the active session.
 *
 *  \param[in] language  The ISO-639 code for the default audio language to be set
 *
 * \return  CGMI_ERROR_SUCCESS when the API succeeds
 *  \ingroup CGMI
 *
 *  \image html audio_language_selection.png "How to do Audio Language Selection"
 */
cgmi_Status cgmi_SetDefaultAudioLang (void *pSession,  const char *language );

cgmi_Status cgmi_CreateSectionFilter(void *pSession, void* pFilterPriv, void** pFilterId  );
cgmi_Status cgmi_DestroySectionFilter(void *pSession, void* pFilterId  );
cgmi_Status cgmi_SetSectionFilter(void *pSession, void* pFilterId, tcgmi_FilterData *pFilter  );
cgmi_Status cgmi_StartSectionFilter(void *pSession, void* pFilterId, int timeout, int bOneShot , int bEnableCRC, queryBufferCB bufferCB,  sectionBufferCB sectionCB);
cgmi_Status cgmi_StopSectionFilter(void *pSession, void* pFilterId );


/**
 * \section How To section
 *  \attention "How to Initialize and shutdown the subsystem"
 * 
 *  \image html Initialization_and_Shutdown.png "How to Initialize and shutdown the subsystem"
 *    
 */


/** @} */ // end of CGMI group

#ifdef __cplusplus
}
#endif

#endif 
