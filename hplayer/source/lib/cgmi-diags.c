/**
 * * \addtogroup CGMI-diags
 * @{
*/
// -----------------------------------------------------
/**
*   \file: cgmi-diags.c
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
*   Description: This c file contains the API
*                implementation for CGMI diagnostic.
*
*   Thread Safe: Yes
*
*   \authors : QuocThanh Thuy
*   \ingroup CGMI-diags
*/

#include <glib.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>

#include "cgmiDiagsApi.h"
#include "cgmi-diags-priv.h"

static tCgmiDiags_timingMetric *gTimingBuf = NULL;
static int timingBufIndex = 0;
static bool timingBufWrapped = false;
static bool cgmiDiagInitialized = false;
static pthread_mutex_t cgmiDiagMutex = PTHREAD_MUTEX_INITIALIZER;;

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
cgmi_Status cgmiDiags_Init (void)
{
    cgmi_Status retStatus = CGMI_ERROR_SUCCESS;

    g_print("%s: Enter\n", __FUNCTION__);

    pthread_mutex_lock(&cgmiDiagMutex);

    if(false == cgmiDiagInitialized) 
    {
        gTimingBuf = (tCgmiDiags_timingMetric *) calloc(sizeof(tCgmiDiags_timingMetric) * CGMI_DIAGS_TIMING_METRIC_MAX_ENTRY, sizeof(char));

        if(NULL == gTimingBuf) 
        {
            retStatus = CGMI_ERROR_NOT_INITIALIZED;
        }
        else
        {
            cgmiDiagInitialized = true;
        }
    }

    pthread_mutex_unlock(&cgmiDiagMutex);

    return retStatus;
}


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
cgmi_Status cgmiDiags_Term (void)
{
    g_print("%s: Enter\n", __FUNCTION__);

    pthread_mutex_lock(&cgmiDiagMutex);

    cgmiDiagInitialized = false;

    if(gTimingBuf) 
    {
        free(gTimingBuf);
        gTimingBuf=NULL;
    }

    pthread_mutex_unlock(&cgmiDiagMutex);

    return CGMI_ERROR_SUCCESS;
}

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
cgmi_Status cgmiDiags_GetTimingMetricsMaxCount ( int *pCount )
{
    *pCount = CGMI_DIAGS_TIMING_METRIC_MAX_ENTRY;
    return CGMI_ERROR_SUCCESS;
}

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
cgmi_Status cgmiDiags_GetTimingMetrics ( tCgmiDiags_timingMetric metrics[], int *pCount )
{
    cgmi_Status retStatus = CGMI_ERROR_SUCCESS;
    int i, entryCount = 0;
    int curCount = 0;
    int inputCountAvailable = *pCount;

    pthread_mutex_lock(&cgmiDiagMutex);

    if(true == cgmiDiagInitialized)
    {
        if(true == timingBufWrapped) 
        {        
            //calculate how much we need to fill up the first round
            entryCount = CGMI_DIAGS_TIMING_METRIC_MAX_ENTRY - timingBufIndex;
    
            //fill the output buffer and update remaining buffer count
            if(entryCount > inputCountAvailable) 
            {
                memcpy(metrics, &gTimingBuf[timingBufIndex], sizeof(tCgmiDiags_timingMetric)*inputCountAvailable);
                inputCountAvailable = 0;
            }
            else
            {
                memcpy(metrics, &gTimingBuf[timingBufIndex], sizeof(tCgmiDiags_timingMetric)*entryCount);
                inputCountAvailable = inputCountAvailable - entryCount;
            }
        }
    
        if(inputCountAvailable > 0) 
        {
            //fill the output buffer and update remaining buffer count
            if(timingBufIndex > inputCountAvailable) 
            {
                memcpy(&metrics[entryCount], gTimingBuf, sizeof(tCgmiDiags_timingMetric)*inputCountAvailable);
                inputCountAvailable = 0;
            }
            else
            {
                memcpy(&metrics[entryCount], gTimingBuf, sizeof(tCgmiDiags_timingMetric)*timingBufIndex);
                inputCountAvailable = inputCountAvailable - timingBufIndex;
            }
        }
    
        *pCount = *pCount - inputCountAvailable;
    }
    else
    {
        retStatus = CGMI_ERROR_NOT_INITIALIZED;
    }

    pthread_mutex_unlock(&cgmiDiagMutex);

    return retStatus;
}

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
cgmi_Status cgmiDiag_addTimingEntry(tCgmiDiag_timingEvent timingEvent, unsigned int index, char *uri, double markTime)
{
    cgmi_Status retStatus = CGMI_ERROR_SUCCESS;

    pthread_mutex_lock(&cgmiDiagMutex);
    
    if(true == cgmiDiagInitialized) 
    {
        if(0 == markTime) 
        {
            struct timeval  current_tv;

            gettimeofday(&current_tv, NULL);
            markTime = (current_tv.tv_sec) * 1000 + (current_tv.tv_usec) / 1000;
        }

        gTimingBuf[timingBufIndex].timingEvent = timingEvent;
        gTimingBuf[timingBufIndex].sessionIndex = index;
        gTimingBuf[timingBufIndex].markTime = markTime;

        if(NULL == uri) 
        {
            snprintf(gTimingBuf[timingBufIndex].sessionUri, sizeof(gTimingBuf[timingBufIndex].sessionUri), "NULL URI");
        }
        else
        {
            strncpy(gTimingBuf[timingBufIndex].sessionUri, uri, sizeof(gTimingBuf[timingBufIndex].sessionUri));
            gTimingBuf[timingBufIndex].sessionUri[sizeof(gTimingBuf[timingBufIndex].sessionUri) - 1] = '\0';
        }
    
        timingBufIndex++;
    
        if(timingBufIndex >= CGMI_DIAGS_TIMING_METRIC_MAX_ENTRY) 
        {
            timingBufIndex = 0;
            timingBufWrapped = true;
        }
    }
    else
    {
        retStatus = CGMI_ERROR_NOT_INITIALIZED;
    }

    pthread_mutex_unlock(&cgmiDiagMutex);

    g_print("%s: Add event = %d; index = %d; time = %F; ret = %d\n", __FUNCTION__, timingEvent, index, markTime, retStatus);

    return retStatus;
}

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
cgmi_Status cgmiDiags_ResetTimingMetrics (void)
{
    cgmi_Status retStatus = CGMI_ERROR_SUCCESS;

    pthread_mutex_lock(&cgmiDiagMutex);
    
    if(true == cgmiDiagInitialized) 
    {
        timingBufIndex = 0;
        timingBufWrapped = false;
    }
    else
    {
        retStatus = CGMI_ERROR_NOT_INITIALIZED;
    }

    pthread_mutex_unlock(&cgmiDiagMutex);

    return retStatus;
}

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
cgmi_Status cgmiDiags_GetNextSessionIndex(unsigned int *pIndex)
{
    static curIndex = 0;

    if(NULL == pIndex) 
    {
        return CGMI_ERROR_BAD_PARAM;
    }

    pthread_mutex_lock(&cgmiDiagMutex);

    *pIndex = curIndex;
    curIndex++;

    pthread_mutex_unlock(&cgmiDiagMutex);

    return CGMI_ERROR_SUCCESS;
}
