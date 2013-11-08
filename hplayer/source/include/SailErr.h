#ifndef __SAIL_ERR_H__
#define __SAIL_ERR_H__

/**
 * @file SailErr.h
 * @brief Public error code definitions
 */
#ifdef __cplusplus
extern "C" {
#endif

// ====== includes ======

// ====== defines ======

#define SAIL_ERROR_MODULES    (ui32)23
#define SAIL_ERROR_MASK       (ui32)(0xFFFFFFFF >> SAIL_ERROR_MODULES)
#define ERROR_MODULE_MASK     (ui32)~SAIL_ERROR_MASK
#define ERROR_BASE_START      (ui32)(SAIL_ERROR_MASK + 1)
#define ERROR_MODULE_START    (ui32)(ERROR_BASE_START >> 1)

// ====== enums ======

// ====== typedefs ======
typedef ui32 SailErr;

typedef enum
{
   ERROR_OS_IDX,
   ERROR_SESSION_IDX,
   ERROR_DVR_IDX,
   ERROR_AVPLAYER_IDX,
   ERROR_AVOUT_IDX,
   ERROR_NETWORK_IDX,
   ERROR_TRANSPORT_IDX,
   ERROR_GFX_IDX,
   ERROR_HAL_IDX,
   ERROR_TUNER_IDX,
   ERROR_SOURCE_IDX,
   ERROR_DMS_IDX,
   ERROR_MODULES_MAX
}ErrorIndex;

/**
* 
* Public SAIL error codes.
* 
*/
typedef enum
{
   SailErr_None            = 0,

   //
   // Generate the rest of of the common error codes.
   // 
#undef SAIL_ERROR
#define SAIL_ERROR(x,y) SailErr_##x = y,
#include "ErrBase.h"

   SailErr_OS_Base         = ERROR_BASE_START << ERROR_OS_IDX,        ///< Base offset for OS specific error codes defined in osErr.h
   SailErr_Session_Base    = ERROR_BASE_START << ERROR_SESSION_IDX,   ///< Base offset for Session manager error codes defined in SessErr.h
   SailErr_Dvr_Base        = ERROR_BASE_START << ERROR_DVR_IDX,       ///< Base offset for DVR error codes defined in DvrErr.h
   SailErr_AvPlayer_Base   = ERROR_BASE_START << ERROR_AVPLAYER_IDX,  ///< Base offset for AvPlayer error codes defined in AvPlayerErr.h
   SailErr_AvOut_Base      = ERROR_BASE_START << ERROR_AVOUT_IDX,     ///< Base offset for AvOut error codes defined in AvOutErr.h
   SailErr_Network_Base    = ERROR_BASE_START << ERROR_NETWORK_IDX,   ///< Base offset for Network error codes defined in NetErr.h
   SailErr_Transport_Base  = ERROR_BASE_START << ERROR_TRANSPORT_IDX, ///< Base offset for Transport error codes defined in TransportErr.h
   SailErr_Gfx_Base        = ERROR_BASE_START << ERROR_GFX_IDX,       ///< Base offset for Gfx error codes defined in GfxErr.h
   SailErr_HAL_Base        = ERROR_BASE_START << ERROR_HAL_IDX,       ///< Base offset for HAL specific error codes defined in halErr.h
   SailErr_Tuner_Base      = ERROR_BASE_START << ERROR_TUNER_IDX,     ///< Base offset for Tuner error codes defined in TunerErr.h
   SailErr_Source_Base     = ERROR_BASE_START << ERROR_SOURCE_IDX,    ///< Base offset for Source module error codes defined in SourceErr.h
   SailErr_Dms_Base        = ERROR_BASE_START << ERROR_DMS_IDX        ///< Base offset for Source module error codes defined in DmsErr.h
}tSailErr;

#ifdef __cplusplus
}
#endif

#endif
