#ifndef __SAIL_INF_H__
#define __SAIL_INF_H__
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

#include <stdint.h>
#include <stdbool.h>
#include <time.h>


typedef enum
{
   SAIL_ERROR_SUCCESS,
   SAIL_ERROR_FAILED,
   SAIL_ERROR_NOT_IMPLEMENTED,
   SAIL_ERROR_NOT_SUPPORTED,
   SAIL_ERROR_BAD_PARAM,
   SAIL_ERROR_OUT_OF_MEMORY,
   SAIL_ERROR_OUT_OF_HANDLES,
   SAIL_ERROR_TIMEOUT,
   SAIL_ERROR_INVALID_HANDLE,
   SAIL_ERROR_NOT_INITIALIZED,
   SAIL_ERROR_NOT_OPEN,
   SAIL_ERROR_NOT_ACTIVE,
   SAIL_ERROR_NOT_READY,
   SAIL_ERROR_NOT_CONNECTED,
   SAIL_ERROR_WRONG_STATE
}SailError;

#ifndef tVideoResolution
typedef enum
{
   RES_NTSC_M,
   RES_PAL_BG,
   RES_480p60,
   RES_576p50,
   RES_720p50,
   RES_720p60,
   RES_1080i50,
   RES_1080i60,
   RES_1080p24,
   RES_1080p25,
   RES_1080p30,
   RES_1080p50,
   RES_1080p60,
   RES_LAST_USER_PREFERRED,
   INVALID_RES
}tVideoResolution;
#endif 
typedef tVideoResolution VideoResolution;

#ifndef tAspectRatio
typedef enum
{   
   AR_4x3,
   AR_16x9,
   AR_4x3LB,
   AR_16x9PB,
   AR_4x3CCO,
   INVALID_AR,
}tAspectRatio;
#endif 
typedef tAspectRatio AspectRatio;

#ifndef  tColorSpace
typedef enum
{
      COLSPACE_RGB,
      COLSPACE_422,
      COLSPACE_444,
      COLSPACE_MAX
}tColorSpace;
#endif 

#ifndef tColorDepth 
typedef enum
{
      COLDEPTH_8BIT,
      COLDEPTH_10BIT,
      COLDEPTH_12BIT,
      COLDEPTH_16BIT,
}tColorDepth;
#endif 

typedef enum
{
   VIDEO_PORT_RF        =  0x01,
   VIDEO_PORT_SCART     =  0x02,
   VIDEO_PORT_COMPOSITE =  0x04,
   VIDEO_PORT_SVIDEO    =  0x08,
   VIDEO_PORT_COMPONENT =  0x10,
   VIDEO_PORT_HDMI      =  0x20
}tVideoPort;
typedef tVideoPort VideoPort;

typedef enum
{
   AUDIO_PORT_RF        = 0x01,
   AUDIO_PORT_RCA       = 0x02,
   AUDIO_PORT_SCART     = 0x04,
   AUDIO_PORT_SPDIF     = 0x08,
   AUDIO_PORT_HDMI      = 0x10
}tAudioPort;
typedef tAudioPort AudioPort;

typedef enum
{
   VIDEO_OUT_SD   = (VIDEO_PORT_RF | VIDEO_PORT_SCART | VIDEO_PORT_COMPOSITE | VIDEO_PORT_SVIDEO),
   VIDEO_OUT_HD   = (VIDEO_PORT_COMPONENT | VIDEO_PORT_HDMI)
}tVideoOutput;
typedef tVideoOutput VideoOutput;

#ifndef tVideoCodec
typedef enum
{
   VIDEO_CODEC_MPEG1,
   VIDEO_CODEC_MPEG2,
   VIDEO_CODEC_MPEG4,
   VIDEO_CODEC_H263,
   VIDEO_CODEC_H264,
   VIDEO_CODEC_H264SVC,
   VIDEO_CODEC_H264MVC,
   VIDEO_CODEC_VC1,
   VIDEO_CODEC_VC1ADV,
   VIDEO_CODEC_DIVX311,
   VIDEO_CODEC_AVS,
   VIDEO_CODEC_RV40,
   VIDEO_CODEC_VP6,
   VIDEO_CODEC_UNKNOWN
}tVideoCodec;
#endif
typedef tVideoCodec VideoCodec;

typedef struct
{
   short x;
 	short y;
 	short width;
 	short height;
}tRectangle;
typedef tRectangle Rectangle;

typedef enum
{ 
   FORMAT_UNKNOWN,
   FORMAT_NTSC,
   FORMAT_PAL
}tVideoFormat;

#ifdef __cplusplus
extern "C"
{
#endif

SailError SailSystemInit( void );
SailError SailSystemFinalize( void );
#define boolean int
#define ui32 unsigned int
#define ui8 unsigned char
#define WORD unsigned int
#define  SessionHandle void *
#define logHandle  int
#define MAX_FILE_LEN 1024

#ifdef __cplusplus
}
#endif

#endif 

