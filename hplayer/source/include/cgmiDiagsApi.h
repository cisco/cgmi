/**
 * * \addtogroup CGMI-diags
 * @{
*/
// -----------------------------------------------------
/**
*   \file: cgmiDiags.h
*
*   \brief CGMI DIAGS public API
*
*   System: RDK 2.0
*   Component Name : CGMI-Diags
*   Status         : Version 0.1 Release 1
*   Language       : C
*
*   License        : Proprietary 
*
*   (c) Copyright Cisco Systems 2014
*
*
*
*   Description: This header file contains the API used to access CGMI diagnostic.
*
*   Thread Safe: Yes
*
*   \authors : Zack Wine, QuocThanh Thuy
*   \ingroup CGMI-diags
*/

// -----------------------------------------------------


#ifndef __CGMI_DIAGS_API_H__
#define __CGMI_DIAGS_API_H__

#include "cgmiPlayerApi.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** Timing events 
 */
typedef enum
{
    DIAG_TIMING_METRIC_UNLOAD,
    DIAG_TIMING_METRIC_LOAD,
    DIAG_TIMING_METRIC_PLAY,
    DIAG_TIMING_METRIC_PTS_DECODED
}tCgmiDiag_timingEvent;


typedef struct
{
    tCgmiDiag_timingEvent timingEvent;
    char sessionUri[1024];
    unsigned int sessionIndex;
    unsigned long long markTime;
}tCgmiDiags_timingMetric;


/**
 *  \brief \b cgmiDiags_GetTimingMetricsMaxCount
 *
 *  This is a request to get the max number of timing metrics that will be buffered.
 *
 *  \param[out] pCount    This will be populated with the maximum number of buffered timing metrics.
 *
 *  \pre    The system has to be initialized via cgmi_Init()
 *
 *  \return  CGMI_ERROR_SUCCESS when the API succeeds
 *
 *  \ingroup CGMI-diags 
 *
 */
cgmi_Status cgmiDiags_GetTimingMetricsMaxCount(int *pCount);

/**
 *  \brief \b cgmiDiags_GetTimingMetrics
 *
 *  This is a request to get the current timing metrics buffer.
 *
 *  \param[out] metrics  An array of to be populated with metrics.  This array must be allocated by the caller.
 *
 *  \param[in,out] pCount    In: Indicates the size of metrics array.  Out:  Indicates the actual number of metrics populated.
 *
 *  \pre    The system has to be initialized via cgmi_Init()
 *
 *  \return  CGMI_ERROR_SUCCESS when the API succeeds
 *
 *  \ingroup CGMI-diags
 *
 */
cgmi_Status cgmiDiags_GetTimingMetrics(tCgmiDiags_timingMetric metrics[], int *pCount);

/**
 *  \brief \b cgmiDiags_ResetTimingMetrics
 *
 *  This is a request to reset/clear timing metrics buffer.
 *
 *  \pre    The system has to be initialized via cgmi_Init()
 *
 *  \return  CGMI_ERROR_SUCCESS when the API succeeds
 *
 *  \ingroup CGMI-diags
 *
 */
cgmi_Status cgmiDiags_ResetTimingMetrics(void);

#ifdef __cplusplus
}
#endif

#endif
