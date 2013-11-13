#ifndef __CGMI_PLAYER_API_H__
#define __CGMI_PLAYER_API_H__

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
   CGMI_ERROR_SUCCESS,
   CGMI_ERROR_FAILED,
   CGMI_ERROR_NOT_IMPLEMENTED,
   CGMI_ERROR_NOT_SUPPORTED,
   CGMI_ERROR_BAD_PARAM,
   CGMI_ERROR_OUT_OF_MEMORY,
   CGMI_ERROR_TIMEOUT,
   CGMI_ERROR_INVALID_HANDLE,
   CGMI_ERROR_NOT_INITIALIZED,
   CGMI_ERROR_NOT_OPEN,
   CGMI_ERROR_NOT_ACTIVE,
   CGMI_ERROR_NOT_READY,
   CGMI_ERROR_NOT_CONNECTED,
   CGMI_ERROR_URI_NOTFOUND,
   CGMI_ERROR_WRONG_STATE
 

}cgmi_Status;
typedef enum
{

   NOTIFY_STREAMING_OK,
   NOTIFY_FIRST_PTS_DECODED,
   NOTIFY_STREAMING_NOT_OK,
   NOTIFY_SEEK_DONE,
   NOTIFY_START_OF_STREAM,
   NOTIFY_END_OF_STREAM,
   NOTIFY_DECRYPTION_FAILED,
   NOTIFY_NO_DECRYPTION_KEY,
   NOTIFY_VIDEO_ASPECT_RATIO_CHANGED,
   NOTIFY_VIDEO_RESOLUTION_CHANGED,
   NOTIFY_CHANGED_LANGUAGE_AUDIO,
   NOTIFY_CHANGED_LANGUAGE_SUBTITLE,
   NOTIFY_CHANGED_LANGUAGE_TELETEXT,
   NOTIFY_MEDIAPLAYER_SESSION_OPEN,
   NOTIFY_MEDIAPLAYER_URL_OPEN_FAILURE,
   NOTIFY_MEDIAPLAYER_CLOSE,
   NOTIFY_MEDIAPLAYER_UNKNOWN

}tcgmi_Event; 
typedef enum
{
   LIVE,
   TSB,
   FIXED,
   cgmi_Session_Type_UNKNOWN

}cgmi_SessionType; 
typedef struct
{


}tcgmi_FilterData; 

typedef void (*cgmi_EventCallback)(void *pUserData, void* pSession, tcgmi_Event event );
typedef cgmi_Status (*queryBufferCB)(void *pUserData, void *pFilterPriv, void* pFilterId, char **ppBuffer, int* pBufferSize );
typedef cgmi_Status (*sectionBufferCB)(void *pUserData, void *pFilterPriv, void* pFilterId, cgmi_Status SectionStatus, const char *pSection, int sectionSize);

cgmi_Status cgmi_Init (void);
cgmi_Status cgmi_Term (void);
cgmi_Status cgmi_CreateSession (cgmi_EventCallback eventCB, void* pUserData, void **pSession );
cgmi_Status cgmi_DestroySession(void *pSession );
cgmi_Status cgmi_canPlayType(const char *type, int *pbCanPlay );

cgmi_Status cgmi_Load    (void *pSession, const char *uri );
cgmi_Status cgmi_Unload  (void *pSession );
cgmi_Status cgmi_Play    (void *pSession);

cgmi_Status cgmi_SetRate      (void *pSession,  float rate);
cgmi_Status cgmi_SetPosition  (void *pSession,  float position);
cgmi_Status cgmi_GetPosition  (void *pSession,  float *pPosition);
cgmi_Status cgmi_GetDuration  (void *pSession,  float *pDuration, cgmi_SessionType *type);
cgmi_Status cgmi_GetRateRange (void *pSession,  float *pRewind, float *pFFoward );

cgmi_Status cgmi_GetNumAudioStreams (void *pSession,  int *count);
cgmi_Status cgmi_GetAudioStreamInfo (void *pSession,  int index, char* buf, int bufSize);
cgmi_Status cgmi_SetAudioStream (void *pSession,  int index );
cgmi_Status cgmi_SetDefaultAudioLang (void *pSession,  const char *language );

cgmi_Status cgmi_CreateSectionFilter(void *pSession, void* pFilterPriv, void** pFilterId  );
cgmi_Status cgmi_DestroySectionFilter(void *pSession, void* pFilterId  );
cgmi_Status cgmi_SetSectionFilter(void *pSession, void* pFilterId, tcgmi_FilterData *pFilter  );
cgmi_Status cgmi_StartSectionFilter(void *pSession, void* pFilterId, int timeout, int bOneShot , int bEnableCRC, queryBufferCB bufferCB,  sectionBufferCB sectionCB);
cgmi_Status cgmi_StopSectionFilter(void *pSession, void* pFilterId );

#ifdef __cplusplus
}
#endif

#endif 
