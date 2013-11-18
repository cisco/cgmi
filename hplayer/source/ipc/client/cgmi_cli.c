/* Includes */
#include <glib.h>
#include <stdio.h>
#include <string.h>

#include "cgmiPlayerApi.h"

/* Error Strings */
const char *strError[] =
{
    "CGMI_ERROR_SUCCESS",
    "CGMI_ERROR_FAILED",
    "CGMI_ERROR_NOT_IMPLEMENTED",
    "CGMI_ERROR_NOT_SUPPORTED",
    "CGMI_ERROR_BAD_PARAM",
    "CGMI_ERROR_OUT_OF_MEMORY",
    "CGMI_ERROR_TIMEOUT",
    "CGMI_ERROR_INVALID_HANDLE",
    "CGMI_ERROR_NOT_INITIALIZED",
    "CGMI_ERROR_NOT_OPEN",
    "CGMI_ERROR_NOT_ACTIVE",
    "CGMI_ERROR_NOT_READY",
    "CGMI_ERROR_NOT_CONNECTED",
    "CGMI_ERROR_URI_NOTFOUND",
    "CGMI_ERROR_WRONG_STATE"
};
#define DUMP_ERROR(err)    \
   do\
      {  \
         printf("%s\n", strError[err]);  \
      }  \
      while(0) 

/* Prototypes */
static void cgmiCallback( void *pUserData, void *pSession, tcgmi_Event event );

/* Play Command */
static cgmi_Status play(void *pSessionId, char *src)
{   
    cgmi_Status retCode = CGMI_ERROR_SUCCESS;

    /* First load the URL. */
    retCode = cgmi_Load( pSessionId, src );
    if (retCode != CGMI_ERROR_SUCCESS)
    {
        printf("CGMI Load failed\n");
    } else {
        /* Play the URL if load succeeds. */
        retCode = cgmi_Play( pSessionId );
        if (retCode != CGMI_ERROR_SUCCESS)
        {
            printf("CGMI Play failed\n");
        }
    }

    return retCode;
}

/* Stop Command */
static cgmi_Status stop(void *pSessionId)
{
    cgmi_Status retCode = CGMI_ERROR_SUCCESS;

    /* Stop = Unload */
    retCode = cgmi_Unload( pSessionId );
    if (retCode != CGMI_ERROR_SUCCESS)
    {
        printf("CGMI Unload failed\n");
    }

    return retCode;
}

/* GetPosition Command */
static cgmi_Status getposition(void *pSessionId, float *pPosition)
{
    cgmi_Status retCode = CGMI_ERROR_SUCCESS;

    retCode = cgmi_GetPosition( pSessionId, pPosition );
    if (retCode != CGMI_ERROR_SUCCESS)
    {
        printf("CGMI GetPosition failed\n");
    }

    return retCode;
}

/* Callback Function */
static void cgmiCallback( void *pUserData, void *pSession, tcgmi_Event event )
{
  printf("CGMI Event Recevied : %d \n",event);
}

/* MAIN */
int main(int argc, char **argv)
{
    cgmi_Status retCode = CGMI_ERROR_SUCCESS;
    void *pSessionId;

    gchar command[512];
    gchar arg[512];
    gint quit = 0;
    gint playing = 0;
    gfloat Position = 0.0;

    /* Init CGMI. */
    retCode = cgmi_Init();
    if(retCode != CGMI_ERROR_SUCCESS)
    {
        printf("CGMI Init Failed\n");
        DUMP_ERROR(retCode);
        return 1;
    } else {
        printf("CGMI Init Success!\n");
    }

    /* Create a playback session. */
    retCode = cgmi_CreateSession( cgmiCallback, NULL, &pSessionId );
    if (retCode != CGMI_ERROR_SUCCESS)
    {
        printf("CGMI CreateSession failed\n");
        DUMP_ERROR(retCode);
        return 1;
    } else {
        printf("CGMI CreateSession Success!\n");
    }

    /* Helpful Information */
    printf("CGMI CLI Ready...\n");

    printf("Supported commands:\n"
           "\tplay <url>\n"
           "\tstop (or unload)\n"
           "\tgetposition\n"
           "\tquit\n\n");

    /* Main Command Loop */
    while (!quit)
    {
        retCode = CGMI_ERROR_SUCCESS;

        /* Get the command. */
        printf( "cgmi> " );
        if ( fgets( command, 512, stdin ) == NULL )
        {
            fprintf( stderr, "Error getting input. Exiting.\n" );
            quit = 1;
        }

        /* play */
        if (strncmp(command, "play", 4) == 0)
        {
            strncpy( arg, command + 5, strlen(command) - 6 );
            arg[strlen(command) - 6] = '\0';
            printf("Playing \"%s\"...\n", arg);
            play(pSessionId, arg);
            if ( retCode == CGMI_ERROR_SUCCESS )
            {
                playing = 1;
            }
        }
        /* stop or unload */
        else if ((strncmp(command, "stop", 4) == 0) || 
                 (strncmp(command, "unload", 6) == 0))
        {
            retCode = stop(pSessionId);
            if ( retCode == CGMI_ERROR_SUCCESS )
            {
                playing = 0;
            }
        }
        /* getposition */
        else if (strncmp(command, "getposition", 11) == 0)
        {
            retCode = getposition(pSessionId, &Position);
            if (!retCode)
            {
                printf( "Position: %f\n", Position );
            }
        }
        /* quit */
        else if (strncmp(command, "quit", 4) == 0)
        {
            quit = 1;
        }

        /* If we receive and error, print error. */
        if (retCode)
        {
            DUMP_ERROR(retCode);
        }
    }

    /* If we were playing, stop. */
    if ( playing )
    {
        retCode = stop(pSessionId);
    }

    /* Destroy the created session. */
    retCode = cgmi_DestroySession( pSessionId );
    if (retCode != CGMI_ERROR_SUCCESS)
    {
        printf("CGMI DestroySession failed\n");
        DUMP_ERROR(retCode);
    } else {
        printf("CGMI DestroySession Success!\n");
    }

    /* Terminate CGMI interface. */
    retCode = cgmi_Term();
    if (retCode != CGMI_ERROR_SUCCESS)
    {
        printf("CGMI Term failed\n");
        DUMP_ERROR(retCode);
    } else {
        printf("CGMI Term Success!\n");
    }

    return 0;
}
