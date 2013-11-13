#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include "gst_player.h"




int main( int argc, char *argv[] )
{
   gboolean bOK;
   tSession *pSession = NULL;
   int ch;

   bOK = cisco_gst_init( argc, argv );
   pSession =  cisco_create_session(NULL );

   //cisco_gst_set_pipeline(pSession, "file:///var/www/vegas.ts", "filesrc location=///var/www/vegas.ts ! tsdemux name=d ! queue max-size-buffers=0 max-size-time=0 ! aacparse ! faad ! audioconvert ! audioresample ! autoaudiosink d. ! queue max-size-buffers=0 max-size-time=0 ! ffdec_h264 ! ffmpegcolorspace ! videoscale ! autovideosink");
   cisco_gst_set_pipeline(pSession, "file:///var/www/vegas.ts",NULL );


   cisco_gst_play(pSession);


   while(1)
   {
      printf("hit any key to quit\n");
      printf("hit p to play\n");
      printf("hit r to pause\n");
      ch = getchar();

      if (ch == 'q'){break;}
      if (ch == 'p'){cisco_gst_play(pSession);}
      if (ch == 'r'){cisco_gst_pause(pSession);}

  
      debug_cisco_gst_streamDurPos( pSession );
   }

   

   cisco_delete_session(pSession);
   cisco_gst_deinit( );

}
