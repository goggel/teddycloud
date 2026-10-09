#include "handler_podcast.h"
#include "podcast.h"
#include "server_helpers.h"

error_t handleApiPodcast(HttpConnection *connection, const char_t *uri, const char_t *query, client_ctx_t *client_ctx)
{
    (void)client_ctx;
    char overlay[64] = "", ruid[32] = "", q[256] = "", country[8] = "DE";
    queryGet(query, "overlay", overlay, sizeof(overlay));
    queryGet(query, "ruid", ruid, sizeof(ruid));
    const char *action = uri + strlen("/api/podcasts/");
    cJSON *body = NULL, *response = NULL;
    int status = 200;
    if (!strcmp(connection->request.method, "POST"))
    {
        if (connection->request.contentLength > 16384 || connection->request.chunkedEncoding)
            status = 413;
        else
        {
            char data[16385];
            size_t used = 0;
            while (used < 16384)
            {
                size_t n = 0;
                error_t e = httpReadStream(connection, data + used, 16384 - used, &n, 0);
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
            if (memchr(data, 0, used))
                status = 400;
            if (status == 200)
                body = cJSON_ParseWithOpts(data, NULL, true);
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
        cJSON_AddStringToObject(error, "message", "Expected a JSON object of at most 16 KiB");
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
