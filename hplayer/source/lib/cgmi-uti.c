/*
    CGMI
    Copyright (C) {2015}  {Cisco System}

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
    USA

    Contributing Authors: Matt Snoby, Kris Kersey, Zack Wine, Chris Foster,
                          Tankut Akgul, Saravanakumar Periyaswamy

*/

/*
 * This source file is where utility functions for the CGMI interface should be placed.
*/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif


#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef ENABLE_DLNA_AUTODETECT
   #include <curl/curl.h>
#endif
#include <gst/gst.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <gst/app/gstappsink.h>
#include "cgmiPlayerApi.h"
#include "cgmi-priv-player.h"
#include "cgmi-section-filter-priv.h"



typedef enum
{
   CONTENT_TYPE_UNSUPPORTED = 0,
   CONTENT_TYPE_AUDIO,
   CONTENT_TYPE_VIDEO
}contentType;

typedef enum
{
   CONTENT_PROTOCOL_UNICAST_HTTP = 0,
   CONTENT_PROTOCOL_DLNA,
   CONTENT_PROTOCOL_HLS
}contentProtocol;

typedef struct
{
   int              httpStatusCode;
   contentType      httpContentType;
   contentProtocol  protocol;
}httpRespHdr;


#ifdef ENABLE_DLNA_AUTODETECT
static size_t hdrResponseCb(void *ptr, size_t size, size_t nmemb, void *pData)
{
   httpRespHdr *pRespHdr = (httpRespHdr *)pData;

   do
   {
      if(NULL == pData)
      {
         printf("pData param is NULL\n");
         break;
      }
      printf("%s", (char *)ptr);

      if(strstr(ptr, "HTTP/1.1 200 OK") != NULL)
      {
         pRespHdr->httpStatusCode = 200;
      }

      if(strstr(ptr, "Content-Type: video") != NULL)
      {
         pRespHdr->httpContentType = CONTENT_TYPE_VIDEO;
      }

      if(strstr(ptr, "Content-Type: audio") != NULL)
      {
         pRespHdr->httpContentType = CONTENT_TYPE_AUDIO;
      }

      if(strstr(ptr, "contentFeatures.dlna.org") != NULL)
      {
         pRespHdr->protocol = CONTENT_PROTOCOL_DLNA;
      }

      if((strstr(ptr, "Content-Type: application/vnd.apple.mpegurl") != NULL) ||
         (strstr(ptr, "Content-Type: audio/x-mpegurl") != NULL) ||
         (strstr(ptr, "Content-Type: audio/mpegurl") != NULL))
      {
         pRespHdr->protocol = CONTENT_PROTOCOL_HLS;
      }
   }while(0);

   return size * nmemb;
}

#endif


cgmi_Status cgmi_utils_init(void)
{
   cgmi_Status  status = CGMI_ERROR_SUCCESS;

   do
   {
      //
      // Initialize the CURL library for this process.
      //
#ifdef ENABLE_DLNA_AUTODETECT
      curl_global_init(CURL_GLOBAL_ALL);
#endif


   }while (0);
   return status;
}

cgmi_Status cgmi_utils_finalize(void)
{
   cgmi_Status  status = CGMI_ERROR_SUCCESS;

   do
   {
      //
      // finalize the CURL library for this process.
      //
#ifdef ENABLE_DLNA_AUTODETECT
      curl_global_cleanup();
#endif

   }while (0);
   return status;
}


/**
 *  \brief \b cgmi_utils_is_content_dlna
 *
 *  Checks to see if the url is pointing to DLNA based content.
 *
 *  \param[in] url  pointer to an http url shall be tested to see if it is pointing to a DLNA server
 *
 *  \param[out] bisdlnacontent  This pointer shall be populated with either TRUE or FALSE
 *
 *
 * \post    On success the result will return a TRUE in the bisDLNAContent variable
 *
 * \return  CGMI_ERROR_SUCCESS when everything has started correctly
 * \return  CGMI_ERROR_NOT_INITIALIZED when gstreamer returns an error message and can't initialize
 *
 *
 *  \ingroup CGMI
 *
 */
cgmi_Status cgmi_utils_is_content_dlna(const gchar* url, gboolean *bisDLNAContent)
{

   cgmi_Status  status = CGMI_ERROR_SUCCESS;
   *bisDLNAContent = FALSE;

#ifdef ENABLE_DLNA_AUTODETECT
   httpRespHdr respHdr = {-1, CONTENT_TYPE_UNSUPPORTED, CONTENT_PROTOCOL_UNICAST_HTTP};
   CURL* ctx = NULL;
   struct curl_slist *headers = NULL;
#endif

   do
   {
#ifdef ENABLE_DLNA_AUTODETECT
      ctx = curl_easy_init();
      curl_easy_setopt(ctx, CURLOPT_HEADERFUNCTION, hdrResponseCb);
      curl_easy_setopt(ctx, CURLOPT_HEADERDATA, &respHdr);

      headers = curl_slist_append(headers,"Accept: */*");
      headers = curl_slist_append(headers,"getcontentFeatures.dlna.org: 1");
      curl_easy_setopt(ctx,CURLOPT_HTTPHEADER , headers );
      curl_easy_setopt(ctx,CURLOPT_NOBODY ,1 );
      curl_easy_setopt(ctx,CURLOPT_URL, url);
      curl_easy_setopt(ctx,CURLOPT_NOPROGRESS ,1 );
      curl_easy_setopt(ctx,CURLOPT_CONNECTTIMEOUT, 2);
      curl_easy_setopt(ctx,CURLOPT_TIMEOUT, 3);

      curl_easy_perform(ctx);
      if(CONTENT_PROTOCOL_DLNA == respHdr.protocol)
      {
         *bisDLNAContent = TRUE;
      }
#endif
   }while (0);

#ifdef ENABLE_DLNA_AUTODETECT
   curl_slist_free_all(headers);
   curl_easy_cleanup(ctx);
#endif
   return status;
}

/**
 *  \brief \b cgmi_utils_get_json_value
 *
 *  Parses json string and extract value for a key
 *
 *  \param[out] pointer to hold the output string value for the json object
 *
 *  \param[in] json   json input string
 *
 *  \param[in] name   json object name
 *
 * \return  CGMI_ERROR_SUCCESS when everything function succeeds
 * \return  CGMI_ERROR_FAILED when parsing failed
 *
 *  \ingroup CGMI
 *
 */
cgmi_Status cgmi_utils_get_json_value(gchar *output, gint outsize, const gchar *json, const gchar *name)
{
   char *start;
   char *end;

   if (json == NULL || strlen(json) == 0)
      return CGMI_ERROR_FAILED;

   if (name == NULL || strlen(name) == 0)
      return CGMI_ERROR_FAILED;

   if (output == NULL)
      return CGMI_ERROR_FAILED;

   start = strstr(json, name);
   if (NULL != start)
   {
      start = strchr(start, ':');
      if (NULL != start)
      {
         start = strchr(start, '"');
         if (NULL != start)
         {
            start++;
            end = strchr(start, '"');
            if (NULL != end)
            {
               gint maxlen = ((outsize - 1) < (end - start))?(outsize - 1):(end - start);
               strncpy(output, start, maxlen);
               output[maxlen] = 0;
               return CGMI_ERROR_SUCCESS;
            }
         }
      }
   }
   return CGMI_ERROR_FAILED;
}

