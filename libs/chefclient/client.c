/**
 * Copyright 2022, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 * 
 */

#include <curl/curl.h>
#include <wolfssl/ssl.h>
#include <errno.h>
#include <libchefclient.h>
#include <stdlib.h>
#include <string.h>

#define MAX_RESPONSE_SIZE 4096

static const char* g_mollenosTenantId   = "d8acf75d-9820-4522-a25b-ad672acc5fdd";
static const char* g_chefClientId       = "17985824-571b-4bdf-b291-c25b2ff14837";
static char*       g_curlresponseBuffer = NULL;
static char*       g_curlErrorBuffer    = NULL; // CURL_ERROR_SIZE

static int __response_writer(char *data, size_t size, size_t nmemb, size_t* dataIndex)
{
    if (dataIndex == NULL) {
        return 0;
    }

    memcpy(g_curlresponseBuffer + *dataIndex, data, size * nmemb);
    return size * nmemb;
}

static int __get_devicecode_auth_link(char* buffer, size_t maxLength)
{
    int written = snprintf(buffer, maxLength,
        "https://login.microsoftonline.com/%s/oauth2/v2.0/devicecode",
        g_mollenosTenantId
    );
    return written < maxLength ? 0 : -1;
}

static int __get_device_auth_body(char* buffer, size_t maxLength)
{
    int written = snprintf(buffer, maxLength,
        "client_id=%s&scope=%s",
        g_chefClientId,
        "user.read%20openid%20profile"
    );
    return written < maxLength ? 0 : -1;
}

struct data {
  char trace_ascii; /* 1 or 0 */
};
 
static
void dump(const char *text,
          FILE *stream, unsigned char *ptr, size_t size,
          char nohex)
{
  size_t i;
  size_t c;
 
  unsigned int width = 0x10;
 
  if(nohex)
    /* without the hex output, we can fit more on screen */
    width = 0x40;
 
  fprintf(stream, "%s, %10.10lu bytes (0x%8.8lx)\n",
          text, (unsigned long)size, (unsigned long)size);
 
  for(i = 0; i<size; i += width) {
 
    fprintf(stream, "%4.4lx: ", (unsigned long)i);
 
    if(!nohex) {
      /* hex not disabled, show it */
      for(c = 0; c < width; c++)
        if(i + c < size)
          fprintf(stream, "%02x ", ptr[i + c]);
        else
          fputs("   ", stream);
    }
 
    for(c = 0; (c < width) && (i + c < size); c++) {
      /* check for 0D0A; if found, skip past and start a new line of output */
      if(nohex && (i + c + 1 < size) && ptr[i + c] == 0x0D &&
         ptr[i + c + 1] == 0x0A) {
        i += (c + 2 - width);
        break;
      }
      fprintf(stream, "%c",
              (ptr[i + c] >= 0x20) && (ptr[i + c]<0x80)?ptr[i + c]:'.');
      /* check again for 0D0A, to avoid an extra \n if it's at width */
      if(nohex && (i + c + 2 < size) && ptr[i + c + 1] == 0x0D &&
         ptr[i + c + 2] == 0x0A) {
        i += (c + 3 - width);
        break;
      }
    }
    fputc('\n', stream); /* newline */
  }
  fflush(stream);
}
 
static
int my_trace(CURL *handle, curl_infotype type,
             char *data, size_t size,
             void *userp)
{
  struct data *config = (struct data *)userp;
  const char *text;
  (void)handle; /* prevent compiler warning */
 
  switch(type) {
  case CURLINFO_TEXT:
    fprintf(stderr, "== Info: %s", data);
    /* FALLTHROUGH */
  default: /* in case a new one is introduced to shock us */
    return 0;
 
  case CURLINFO_HEADER_OUT:
    text = "=> Send header";
    break;
  case CURLINFO_DATA_OUT:
    text = "=> Send data";
    break;
  case CURLINFO_SSL_DATA_OUT:
    text = "=> Send SSL data";
    break;
  case CURLINFO_HEADER_IN:
    text = "<= Recv header";
    break;
  case CURLINFO_DATA_IN:
    text = "<= Recv data";
    break;
  case CURLINFO_SSL_DATA_IN:
    text = "<= Recv SSL data";
    break;
  }
 
  dump(text, stderr, (unsigned char *)data, size, config->trace_ascii);
  return 0;
}

static int __oauth2_device_flow_start(void)
{
    CURL*    curl;
    CURLcode code;
    size_t   dataIndex = 0;
    char     buffer[256];
    struct data config;
    
    config.trace_ascii = 1; /* enable ascii tracing */


    // initialize a curl session
    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "__oauth2_device_flow_start: curl_easy_init() failed\n");
        return -1;
    }

    code = curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, g_curlErrorBuffer);
    if (code != CURLE_OK) {
        fprintf(stderr, "__oauth2_device_flow_start: failed to set error buffer [%d]\n", code);
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, my_trace);
    curl_easy_setopt(curl, CURLOPT_DEBUGDATA, &config);
 
    /* the DEBUGFUNCTION has no effect until we enable VERBOSE */
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    // To get around CA cert issues
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
 
    // set the url
    __get_devicecode_auth_link(buffer, sizeof(buffer));
    code = curl_easy_setopt(curl, CURLOPT_URL, &buffer[0]);
    if (code != CURLE_OK) {
        fprintf(stderr, "__oauth2_device_flow_start: failed to set url [%s]\n", g_curlErrorBuffer);
        return -1;
    }

    // set the writer function to get the response
    code = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, __response_writer);
    if (code != CURLE_OK) {
        fprintf(stderr, "__oauth2_device_flow_start: failed to set writer [%s]\n", g_curlErrorBuffer);
        return -1;
    }
    
    code = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dataIndex);
    if(code != CURLE_OK) {
        fprintf(stderr, "__oauth2_device_flow_start: failed to set write data [%s]\n", g_curlErrorBuffer);
        return -1;
    }

    __get_device_auth_body(buffer, sizeof(buffer));
    code = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, &buffer[0]);
    if (code != CURLE_OK) {
        fprintf(stderr, "__oauth2_device_flow_start: failed to set body [%s]\n", g_curlErrorBuffer);
        return -1;
    }

    code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        fprintf(stderr, "__oauth2_device_flow_start: curl_easy_perform() failed: %s\n", curl_easy_strerror(code));
    }

    printf("__oauth2_device_flow_start: response: %s\n", g_curlresponseBuffer);

    curl_easy_cleanup(curl);
    return 0;
}


int chefclient_initialize(void)
{
    g_curlresponseBuffer = (char*)malloc(MAX_RESPONSE_SIZE);
    if (g_curlresponseBuffer == NULL) {
        fprintf(stderr, "chefclient_initialize: failed to allocate response buffer\n");
        return -1;
    }

    g_curlErrorBuffer = (char*)malloc(CURL_ERROR_SIZE);
    if (g_curlErrorBuffer == NULL) {
        fprintf(stderr, "chefclient_initialize: failed to allocate error buffer\n");
        return -1;
    }

    // required on windows
    curl_global_init(CURL_GLOBAL_ALL);

    return __oauth2_device_flow_start();
}

void chefclient_cleanup(void)
{
    // required on windows
    curl_global_cleanup();

    if (g_curlresponseBuffer) {
        free(g_curlresponseBuffer);
        g_curlresponseBuffer = NULL;
    }

    if (g_curlErrorBuffer) {
        free(g_curlErrorBuffer);
        g_curlErrorBuffer = NULL;
    }
}

int chefclient_login(void)
{
    errno = ENOTSUP;
    return -1;
}

void chefclient_logout(void)
{

}
