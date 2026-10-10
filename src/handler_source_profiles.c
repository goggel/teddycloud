#include "handler_source_profiles.h"
#include "source_profiles.h"
#include "server_helpers.h"
#include <stdlib.h>
#include <string.h>

static cJSON *body_error(int code)
{
    cJSON *response = cJSON_CreateObject();
    cJSON *error = cJSON_AddObjectToObject(response, "error");
    cJSON_AddStringToObject(error, "code", code == 413 ? "body_too_large" : "invalid_json");
    cJSON_AddStringToObject(error, "message", "Expected a JSON object of at most 64 KiB");
    return response;
}

static bool encoded_nul(const char *data)
{
    for (const char *p = data; *p; p++)
        if (*p == '\\' && p[1])
        {
            p++;
            if (*p == 'u' && !strncmp(p + 1, "0000", 4)) return true;
        }
    return false;
}

error_t handleApiSourceProfiles(HttpConnection *connection, const char_t *uri,
                                const char_t *query, client_ctx_t *client_ctx)
{
    (void)client_ctx;
    int status = 200;
    cJSON *body = NULL, *response = NULL;
    char overlay[128] = "", ruid[32] = "";
    queryGet(query, "overlay", overlay, sizeof(overlay));
    if (!strcmp(uri, "/api/source-profiles/capabilities"))
    {
        response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "supported", true);
        cJSON_AddNumberToObject(response, "version", 1);
        cJSON_AddNumberToObject(response, "maxRequestBytes", 65536);
    }
    else if (strlen(overlay) >= 64)
    {
        status = 400;
        response = body_error(status);
    }
    else if (!strcmp(uri, "/api/source-profiles/save"))
    {
        if (connection->request.contentLength > 65536 || connection->request.chunkedEncoding)
            status = 413;
        else
        {
            char *data = malloc(65537);
            if (!data) return ERROR_OUT_OF_MEMORY;
            size_t used = 0;
            while (used < 65536)
            {
                size_t n = 0;
                error_t error = httpReadStream(connection, data + used, 65536 - used, &n, 0);
                used += n;
                if (error == ERROR_END_OF_STREAM) break;
                if (error != NO_ERROR || !n)
                {
                    status = 400;
                    break;
                }
            }
            data[used] = 0;
            if (memchr(data, 0, used) || encoded_nul(data)) status = 400;
            if (status == 200) body = cJSON_ParseWithOpts(data, NULL, true);
            free(data);
            if (!cJSON_IsObject(body)) status = 400;
        }
        if (status == 200)
        {
            const cJSON *value = cJSON_GetObjectItemCaseSensitive(body, "ruid");
            response = source_profiles_request("save", overlay,
                                                cJSON_IsString(value) ? value->valuestring : "",
                                                body, &status);
        }
        else response = body_error(status);
    }
    else if (!strcmp(uri, "/api/source-profiles"))
    {
        queryGet(query, "ruid", ruid, sizeof(ruid));
        response = source_profiles_request("get", overlay, ruid, NULL, &status);
    }
    else
    {
        status = 404;
        response = cJSON_CreateObject();
    }
    cJSON_Delete(body);
    char *raw = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    if (!raw) return ERROR_OUT_OF_MEMORY;
    httpPrepareHeader(connection, "application/json; charset=utf-8", strlen(raw));
    connection->response.statusCode = status;
    if (status == 400 || status == 413) connection->response.keepAlive = false;
    return httpWriteResponseString(connection, raw, true);
}
