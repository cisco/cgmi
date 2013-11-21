/* Includes */
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "cgmiPlayerApi.h"

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
        printf("CGMI GetPosition Failed\n");
    }

    return retCode;
}

/* SetPosition Command */
static cgmi_Status setposition(void *pSessionId, float Position)
{
    cgmi_Status retCode = CGMI_ERROR_SUCCESS;

    retCode = cgmi_SetPosition( pSessionId, Position );
    if (retCode != CGMI_ERROR_SUCCESS)
    {
        printf("CGMI SetRate Failed\n");
    }

    return retCode;
}

/* GetRateRange Command */
static cgmi_Status getraterange(void *pSessionId, float *pRewind, float *pFFoward)
{
    cgmi_Status retCode = CGMI_ERROR_SUCCESS;

    retCode = cgmi_GetRateRange( pSessionId, pRewind, pFFoward );
    if (retCode != CGMI_ERROR_SUCCESS)
    {
        printf("CGMI GetRateRange Failed\n");
    }

    return retCode;
}

/* SetRate Command */
static cgmi_Status setrate(void *pSessionId, float Rate)
{
    cgmi_Status retCode = CGMI_ERROR_SUCCESS;

    retCode = cgmi_SetRate( pSessionId, Rate );
    if (retCode != CGMI_ERROR_SUCCESS)
    {
        printf("CGMI SetRate Failed\n");
    }

    return retCode;
}

/* GetDuration Command */
static cgmi_Status getduration(void *pSessionId, float *pDuration, cgmi_SessionType *type)
{
    cgmi_Status retCode = CGMI_ERROR_SUCCESS;

    retCode = cgmi_GetDuration( pSessionId, pDuration, type );
    if (retCode != CGMI_ERROR_SUCCESS)
    {
        printf("CGMI GetDuration Failed\n");
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

    /* Status Variables */
    gfloat Position = 0.0;
    gfloat Rate = 0.0;
    gfloat Rewind = 0.0;
    gfloat FFoward = 0.0;
    gfloat Duration = 0.0;
    cgmi_SessionType type = cgmi_Session_Type_UNKNOWN;
    gint pbCanPlay = 0;

    /* Init CGMI. */
    retCode = cgmi_Init();
    if(retCode != CGMI_ERROR_SUCCESS)
    {
        printf("CGMI Init Failed: %s\n", cgmi_ErrorString( retCode ));
        return 1;
    } else {
        printf("CGMI Init Success!\n");
    }

    /* Create a playback session. */
    retCode = cgmi_CreateSession( cgmiCallback, NULL, &pSessionId );
    if (retCode != CGMI_ERROR_SUCCESS)
    {
        printf("CGMI CreateSession Failed: %s\n", cgmi_ErrorString( retCode ));
        return 1;
    } else {
        printf("CGMI CreateSession Success!\n");
    }

    /* Helpful Information */
    printf("CGMI CLI Ready...\n");

    printf("Supported commands:\n"
           "\tplay <url>\n"
           "\tstop (or unload)\n"
           "\n"
           "\tgetraterange\n"
           "\tsetrate <rate (float)>\n"
           "\n"
           "\tgetposition\n"
           "\tsetposition <position (seconds) (float)>\n"
           "\n"
           "\tgetduration\n"
           "\n"
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
            /* Check First */
            printf("Checking if we can play this...");
            retCode = cgmi_canPlayType( arg, &pbCanPlay );
            if ( retCode == CGMI_ERROR_NOT_IMPLEMENTED )
            {
                printf( "cgmi_canPlayType Not Implemented\n" );
                printf("Playing \"%s\"...\n", arg);
                retCode = play(pSessionId, arg);
                if ( retCode == CGMI_ERROR_SUCCESS )
                {
                    playing = 1;
                }
            } else if ( !retCode && pbCanPlay )
            {
                printf( "Yes\n" );
                printf("Playing \"%s\"...\n", arg);
                retCode = play(pSessionId, arg);
                if ( retCode == CGMI_ERROR_SUCCESS )
                {
                    playing = 1;
                }
            } else {
                printf( "No\n" );
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
        /* getraterange */
        else if (strncmp(command, "getraterange", 12) == 0)
        {
            retCode = getraterange(pSessionId, &Rewind, &FFoward);
            if (!retCode)
            {
                printf( "Rate Range: %f : %f\n", Rewind, FFoward );
            }
        }
        /* setrate */
        else if (strncmp(command, "setrate", 7) == 0)
        {
            strncpy( arg, command + 8, strlen(command) - 9 );
            arg[strlen(command) - 9] = '\0';
            Rate = (float) atof(arg);
            printf( "Setting rate to %f (%s)\n", Rate, arg );
            retCode = setrate(pSessionId, Rate);
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
        /* setposition */
        else if (strncmp(command, "setposition", 11) == 0)
        {
            strncpy( arg, command + 12, strlen(command) - 13 );
            arg[strlen(command) - 13] = '\0';
            Position = (float) atof(arg);
            printf( "Setting position to %f (%s)\n", Position, arg );
            retCode = setposition(pSessionId, Position);
        }
        /* setposition */
        else if (strncmp(command, "getduration", 11) == 0)
        {
            retCode = getduration(pSessionId, &Duration, &type);
            if (!retCode)
            {
                printf( "Duration: %f, SessionType: ", Duration );
                switch( type )
                {
                    case LIVE:
                        printf( "LIVE\n" );
                        break;
                    case TSB:
                        printf( "TSB\n" );
                        break;
                    case FIXED:
                        printf( "FIXED\n" );
                        break;
                    case cgmi_Session_Type_UNKNOWN:
                        printf( "cgmi_Session_Type_UNKNOWN\n" );
                        break;
                    default:
                        printf( "ERROR\n" );
                        break;
                }
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
            printf( "Error: %s\n", cgmi_ErrorString( retCode ) );
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
        printf("CGMI DestroySession Failed: %s\n", cgmi_ErrorString( retCode ));
    } else {
        printf("CGMI DestroySession Success!\n");
    }

    /* Terminate CGMI interface. */
    retCode = cgmi_Term();
    if (retCode != CGMI_ERROR_SUCCESS)
    {
        printf("CGMI Term Failed: %s\n", cgmi_ErrorString( retCode ));
    } else {
        printf("CGMI Term Success!\n");
    }

    return 0;
}
