#include "handler_podcast.h"
#include "podcast.h"
#include "server_helpers.h"

/* cJSON strings are NUL-terminated; reject an encoded NUL before it can
 * silently truncate a URL or episode identity during validation. */
static bool json_nul_escape(const char *data)
{
    for (const char *p = data; *p; p++)
        if (*p == '\\' && p[1])
        {
            p++;
            if (*p == 'u' && !strncmp(p + 1, "0000", 4))
                return true;
        }
    return false;
}
error_t handleApiPodcast(HttpConnection *connection, const char_t *uri, const char_t *query, client_ctx_t *client_ctx)
{
    (void)client_ctx;
    char overlay[64] = "", ruid[32] = "", q[256] = "", country[8] = "DE", provider[32] = "apple";
    queryGet(query, "overlay", overlay, sizeof(overlay));
    queryGet(query, "ruid", ruid, sizeof(ruid));
    const char *action = uri + strlen("/api/podcasts/");
    cJSON *body = NULL, *response = NULL;
    int status = 200;
    if (!strcmp(connection->request.method, "POST"))
    {
        if (connection->request.contentLength > 65536 || connection->request.chunkedEncoding)
            status = 413;
        else
        {
            char *data = malloc(65537);
            if (!data)
                return ERROR_OUT_OF_MEMORY;
            size_t used = 0;
            while (used < 65536)
            {
                size_t n = 0;
                error_t e = httpReadStream(connection, data + used, 65536 - used, &n, 0);
                used += n;
                if (e == ERROR_END_OF_STREAM)
                    break;
                if (e != NO_ERROR || !n)
                {
                    status = 400;
                    break;
                }
            }
            data[used] = 0;
            if (memchr(data, 0, used) || json_nul_escape(data))
                status = 400;
            if (status == 200)
                body = cJSON_ParseWithOpts(data, NULL, true);
            free(data);
            if (!cJSON_IsObject(body))
                status = 400;
        }
    }
    else
    {
        body = cJSON_CreateObject();
        if (!strcmp(action, "search"))
        {
            queryGet(query, "q", q, sizeof(q));
            queryGet(query, "country", country, sizeof(country));
            queryGet(query, "provider", provider, sizeof(provider));
            cJSON_AddStringToObject(body, "provider", provider);
            cJSON_AddStringToObject(body, "q", q);
            cJSON_AddStringToObject(body, "country", country);
        }
    }
    if (status == 200)
        response = podcast_request(action, overlay, ruid, body, &status);
    else
    {
        response = cJSON_CreateObject();
        cJSON *error = cJSON_AddObjectToObject(response, "error");
        cJSON_AddStringToObject(error, "code", status == 413 ? "body_too_large" : "invalid_json");
        cJSON_AddStringToObject(error, "message", "Expected a JSON object of at most 64 KiB");
    }
    cJSON_Delete(body);
    char *raw = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    httpPrepareHeader(connection, "application/json; charset=utf-8", strlen(raw));
    connection->response.statusCode = status;
    if (status == 400 || status == 413)
        connection->response.keepAlive = false;
    return httpWriteResponseString(connection, raw, true);
}
