/**
 * * \addtogroup CGMI-diags
 * @{
*/
// -----------------------------------------------------
/**
*   \file: cgmi-diags-priv.h
*
*   \brief CGMI DIAGS internal API
*
*   System: RDK 2.0
*   Component Name : CGMI-Diags
*   Language       : C
*   License        : Proprietary 
*
*   (c) Copyright Cisco Systems 2014
*
*
*
*   Description: This header file contains the API utilized internal to CGMI
*                to track diagnostic info.
*
*   Thread Safe: Yes
*
*   \authors : Zack Wine, QuocThanh Thuy
*   \ingroup CGMI-diags-priv
*/

// -----------------------------------------------------

#ifndef __CGMI_DIAGS_PRIV_H__
#define __CGMI_DIAGS_PRIV_H__

#include "cgmiPlayerApi.h"

#ifdef __cplusplus
extern "C"
{
#endif


#define CGMI_DIAGS_TIMING_METRIC_MAX_ENTRY 128



/**
 *  \brief \b cgmiDiags_Init
 *
 *  Initialize the diags tracking data structures.
 *
 *
 *  \post    On success the diags subsystem will be ready to accept data.
 *
 *  \return  CGMI_ERROR_SUCCESS when everything has started correctly
 *  \return  CGMI_ERROR_NOT_INITIALIZED for any failure.
 *
 *
 *   \ingroup CGMI-diags-priv
 *
 */
cgmi_Status cgmiDiags_Init (void);


/**
 *  \brief \b cgmiDiags_Term
 *
 *  Terminate the diags tracking data structures.  Clean up all resources.
 *
 *  \post    On success the diags subsystem is completely shutdown and all memory is freed. 
 *
 *  \return  CGMI_ERROR_SUCCESS when everything has shutdown properly and all memory freed
 *
 *
 *  \ingroup CGMI-diags-priv
 *
 */
cgmi_Status cgmiDiags_Term (void);

/**
 *  \brief \b cgmiDiag_addTimingEntry 
 *
 *  Add a timing metric entry.
 *  \param[in] timingEvent  This is the timing event you want to
 *        add.
 *
 *  \param[in] pSession  This is a handle to the active session.
 *
 *  \param[in] markTime  This is the time you want to mark the
 *        event with.
 *  
 *  Note: markTime should be MS since the Epoch. If user input
 *        0, this indicate that user want cgmiDiag to internally
 *        mark the time.
 *
 *  \post    On success the metric entry will be added to
 *           history list.
 *
 *  \return  CGMI_ERROR_SUCCESS when the metric is stored.
 *  \return  CGMI_ERROR_OUT_OF_MEMORY  when an allocation of memory has failed.
 *
 *  \ingroup CGMI-diags-priv
 *
 */
cgmi_Status cgmiDiag_addTimingEntry(tCgmiDiag_timingEvent timingEvent, unsigned int index, char *uri, double markTime);

/**
 *  \brief \b cgmiDiags_GetNextSessionIndex
 *
 *  This is a request to get the next diag session index, used
 *  to track the event with a session. Note: not using session
 *  id since the address can be re-use. 
 *
 *  \pre    The system has to be initialized via cgmi_Init()
 *
 *  \return  CGMI_ERROR_SUCCESS when the API succeeds
 *
 *  \ingroup CGMI-diags-priv
 *
 */
cgmi_Status cgmiDiags_GetNextSessionIndex(unsigned int *pIndex);

#ifdef __cplusplus
}
#endif

#endif
