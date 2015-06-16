#ifndef __CGMI_SECTION_FILTER_PRIV_H__
#define __CGMI_SECTION_FILTER_PRIV_H__

#include <glib.h>
#include "cgmi-priv-player.h"
#include "cgmiPlayerApi.h"

#ifdef __cplusplus
extern "C"
{
#endif


typedef enum
{
    FILTER_TYPE_IP,
    FILTER_TYPE_TUNER
} ciscoGstFilterType;

typedef enum
{
    FILTER_OPEN  = 1,
    FILTER_START = 2, 
    FILTER_STOP  = 4,
    FILTER_CLOSE = 8,
    FILTER_SET   = 16
} ciscoGstFilterAction;

typedef tcgmi_FilterFormat ciscoGstFilterFormat;

typedef struct
{
   int                   pid;
   void                  *parentSession;
   void                  *filterPrivate;
   void                  *handle;
   gulong                padAddedCbId;
   int                   timeout;
   int                   bOneShot;
   int                   bEnableCRC;
   queryBufferCB         bufferCB;
   sectionBufferCB       sectionCB;
   GstElement            *appsink;
   ciscoGstFilterAction  lastAction;
   ciscoGstFilterFormat  format;

}tSectionFilter;


#ifdef __cplusplus
}
#endif

#endif

