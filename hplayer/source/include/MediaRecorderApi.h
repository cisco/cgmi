#ifndef __MEDIA_RECORDER_API_H__
#define __MEDIA_RECORDER_API_H__
/* ****************************************************************************
*
*                   Copyright 2011 Cisco Systems, Inc.
*
*                           Subscriber Engineering
*                           5030 Sugarloaf Parkway
*                               P.O.Box 465447
*                          Lawrenceville, GA 30042
*
*                        Proprietary and Confidential
*              Unauthorized distribution or copying is prohibited
*                            All rights reserved
*
* No part of this computer software may be reprinted, reproduced or utilized
* in any form or by any electronic, mechanical, or other means, now known or
* hereafter invented, including photocopying and recording, or using any
* information storage and retrieval system, without permission in writing
* from Cisco Systems, Inc.
*
******************************************************************************/

#include <SailInf.h>

#define MAX_ASSET_NAME_LEN    128

typedef struct
{
   uint32_t  seconds;
   uint64_t  size;
   char  path[MAX_ASSET_NAME_LEN];
}tRecordInfo;

#ifdef __cplusplus
extern "C"
{
#endif

SailError IsMediaRecorderAvailable( bool* available );

/*
 * Function:             MediaRecorderStartAssetRecord
 *
 * Description:          This function starts a recording session
 *
 * Params:
 *          asset:         filename
 *          source:        param that specifies which stream should be recorded (see also source param MediaPlayer api
 *          time_len_ms:   recording time in ms (optional), if time_len_ms is passed (non-zero) the recording will be stopped and the
 *                         middleware will be notified that the recording is finished.
 *
 */

SailError MediaRecorderStartAssetRecord(const char* asset, char* source, int time_len_ms );
/*
 * Function:             MediaRecorderStopAssetRecord
 *
 * Description:          This function stops the recording "asset"
 *
 * Params:
 *          asset:       filename
 *
 *
 */
SailError MediaRecorderStopAssetRecord(const char* asset);

/*
 * Function:             MediaRecorderDeleteAssetRecord
 *
 * Description:          This function deletes the recording "asset". If the recording is ongoing or
 *                       if a playback session is in progress, this function will return an error
 *
 * Params:
 *          asset:       filename
 */
SailError MediaRecorderDeleteAssetRecord(const char* asset);

/*
 * Function:             MediaRecorderGetAssetRecordInfo
 *
 * Description:          This function can be used to get asset size in seconds and bytes.
 *
 * Params:
 *          asset:          filename
 *          pSizeInMS:      asset size in millisecs
 *          pSizeInBytes:   asset size in bytes
 */

SailError MediaRecorderGetAssetRecordInfo( const char* asset, uint64_t* pSizeInMS, uint64_t* pSizeInByte );

/*
 * Function:             MediaRecorderStartTimeshift
 *
 * Description:          This function starts timeshift recording for specified mediaPlayer.
 *                       If bufferSizeInBytes number of bytes are recorded, the buffer will wrap.
 *                       The buffer is emptied on channel change.
 *
 * Params:
 *          mediaPlayerId:    specifies live stream that should be recorded
 *          bufferSizeInMs:   buffer size in millisecs.
 *
 *    TODO:
 *       * send notification if buffer is wrapped
 *
 */

SailError MediaRecorderStartTimeshift( int mediaPlayerId, uint64_t bufferSizeInMs );

/*
 * Function:             MediaRecorderStopTimeshift
 *
 * Description:          This function stops timeshift recording.
 *
 * Params:
 *          mediaPlayerId:    specifies mediaPlayer for which timeshift should be stopped
 *
 *   TODO:
 *      * empty timeshift buffer?
 *
 */
SailError MediaRecorderStopTimeshift(int mediaPlayerId);

SailError MediaRecorderGetNumberOfAssets( int* pCount );

SailError MediaRecorderGetAssetInfo( int recordIndex, tRecordInfo **info );

#ifdef __cplusplus
}
#endif

#endif 

