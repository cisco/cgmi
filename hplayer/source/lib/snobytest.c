#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include "cgmiPlayerApi.h"


void EventCallback(void *pUserData, void* pSession, tcgmi_Event event )
{

}


int main( int argc, char *argv[] )
{
   void *pSession = NULL;
   cgmi_Status stat = CGMI_ERROR_SUCCESS;
   int ch;
   float curPos;
   float Duration;

   printf("About to try and play %s\n", argv[1]);

   cgmi_Init( );

   stat  =  cgmi_CreateSession(EventCallback, NULL, &pSession );
   if (stat != CGMI_ERROR_SUCCESS)
   {
      printf("Error creating the sesion: %s\n", cgmi_ErrorString(stat));
      return -1;
   }

   //cisco_gst_set_pipeline(pSession, "file:///var/www/vegas.ts", "filesrc location=///var/www/vegas.ts ! tsdemux name=d ! queue max-size-buffers=0 max-size-time=0 ! aacparse ! faad ! audioconvert ! audioresample ! autoaudiosink d. ! queue max-size-buffers=0 max-size-time=0 ! ffdec_h264 ! ffmpegcolorspace ! videoscale ! autovideosink");

   stat  =  cgmi_Load(pSession, argv[1]); 
//   stat  =  cgmi_Load(pSession, "file:///var/www/vegas.ts");
   if (stat != CGMI_ERROR_SUCCESS)
   {
      printf("Error Loading the sesion\n");
      return -1;
   }


   stat = cgmi_Play(pSession);
   if (stat != CGMI_ERROR_SUCCESS)
   {
      printf("Error attemtping to play\n");
      return -1;
   }


   while(1)
   {

      printf("hit any key to quit\n");
      printf("hit p to play\n");
      printf("hit r to pause\n");
      ch = getchar();

      if (ch == 'q'){break;}
      if (ch == 'p'){cgmi_SetRate(pSession, 1);}
      if (ch == 'r'){cgmi_SetRate(pSession, 0);}


      stat = cgmi_GetPosition(pSession, &curPos);
      if (stat != CGMI_ERROR_SUCCESS)
      {
         printf("Error Getting Position\n");
         return -1;
      }
      printf("Current Position: %f (seconds) \n",curPos);

      stat = cgmi_GetDuration(pSession, &Duration, FIXED);
      if (stat != CGMI_ERROR_SUCCESS)
      {
         printf("Error Getting the duration \n");
         return -1;
      }
      printf("Duration: %f (seconds) \n",Duration);
   }

   printf("unloading\n");
   stat = cgmi_Unload(pSession);
   if (stat != CGMI_ERROR_SUCCESS)
   {
      printf("Error unloading: %s \n",cgmi_ErrorString(stat) );
      return -1;
   }

   stat = cgmi_DestroySession(pSession );
   if (stat != CGMI_ERROR_SUCCESS)
   {
      printf("Error Destroying Seession :%s \n",cgmi_ErrorString(stat) );
      return -1;
   }


   cgmi_Term( );

}
