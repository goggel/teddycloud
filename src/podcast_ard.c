#if PODCAST_SUPPORT
#define _GNU_SOURCE
#include "podcast_ard.h"
#include "podcast_feed.h"
#include "podcast_http.h"
#include <curl/curl.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *search_query =
    "query Search($q:String!){search(query:$q,type:ProgramSets,limit:20){programSets{nodes{id coreId title sharingUrl "
    "publicationService{title}}}}}";
static const char *show_query =
    "query Show($id:ID!,$offset:Int!,$count:Int!){programSet(id:$id){id coreId title sharingUrl "
    "items(first:$count,offset:$offset,orderBy:PUBLISH_DATE_DESC,filter:{isPublished:{equalTo:true},itemType:{"
    "notEqualTo:EVENT_LIVESTREAM}}){pageInfo{hasNextPage endCursor} nodes{id assetId title isPublished itemType "
    "publishDate duration audios{url downloadUrl allowDownload}}}}}";
static const char *resolve_query =
    "query Resolve($coreId:String!){programSetByCoreId(coreId:$coreId){id coreId title sharingUrl}}";
static bool core_id(const char *id)
{
    if (strlen(id) != 29 || strncmp(id, "urn:ard:show:", 13))
        return false;
    for (unsigned i = 13; i < 29; i++)
        if (!isxdigit((unsigned char)id[i]) || isupper((unsigned char)id[i]))
            return false;
    return true;
}
bool podcast_ard_identifier(const char *url, char identifier[64])
{
    if (!podcast_url_valid(url))
        return false;
    CURLU *u = curl_url();
    char *scheme = NULL, *host = NULL, *port = NULL, *path = NULL;
    bool ok = u && !curl_url_set(u, CURLUPART_URL, url, CURLU_PATH_AS_IS) &&
              !curl_url_get(u, CURLUPART_SCHEME, &scheme, 0) && !strcmp(scheme, "https") &&
              !curl_url_get(u, CURLUPART_HOST, &host, 0) &&
              (!strcasecmp(host, "www.ardaudiothek.de") || !strcasecmp(host, "ardaudiothek.de") ||
               !strcasecmp(host, "www.ardsounds.de") || !strcasecmp(host, "ardsounds.de")) &&
              (curl_url_get(u, CURLUPART_PORT, &port, 0) != CURLUE_OK || !strcmp(port, "443")) &&
              !curl_url_get(u, CURLUPART_PATH, &path, 0);
    if (ok)
    {
        size_t n = strlen(path);
        if (n && path[n - 1] == '/')
            path[--n] = 0;
        char *id = strrchr(path, '/');
        ok = !strncmp(path, "/sendung/", 9) && id && id > path + 9 && !memchr(path + 9, '/', (size_t)(id - path - 9));
        if (ok)
        {
            /* Slugs are opaque but must be one ordinary path segment. */
            for (char *p = path + 9; p < id; p++)
                if ((unsigned char)*p < 33 || *p == '%' || *p == '\\')
                    ok = false;
            int decoded_length = 0;
            char *decoded = curl_easy_unescape(NULL, id + 1, 0, &decoded_length);
            ok =
                ok && decoded && decoded_length > 0 && decoded_length < 64 && (size_t)decoded_length == strlen(decoded);
            if (ok)
            {
                bool numeric = strlen(decoded) <= 20;
                for (char *p = decoded; *p; p++)
                    if (!isdigit((unsigned char)*p))
                        numeric = false;
                ok = numeric || core_id(decoded);
                if (ok)
                    strcpy(identifier, decoded);
            }
            curl_free(decoded);
        }
    }
    curl_free(scheme);
    curl_free(host);
    curl_free(port);
    curl_free(path);
    curl_url_cleanup(u);
    return ok;
}
bool podcast_ard_date(const char *value, time_t *result)
{
    if (strlen(value) < 20 || strlen(value) > 64)
        return false;
    for (unsigned i = 0; i < 19; i++)
    {
        char separator = i == 4 || i == 7 ? '-' : i == 10 ? 'T' : i == 13 || i == 16 ? ':' : 0;
        if (separator ? value[i] != separator : !isdigit((unsigned char)value[i]))
            return false;
    }
    int y, m, d, h, min, s, oh = 0, om = 0, used = 0;
    char zone = 0;
    if (sscanf(value, "%4d-%2d-%2dT%2d:%2d:%2d%n", &y, &m, &d, &h, &min, &s, &used) != 6 || used != 19 || y < 1970 ||
        m < 1 || m > 12 || d < 1 || d > 31 || h > 23 || min > 59 || s > 59)
        return false;
    const char *p = value + used;
    if (*p == '.')
    {
        p++;
        if (!isdigit((unsigned char)*p))
            return false;
        while (isdigit((unsigned char)*p))
            p++;
    }
    if (!strcmp(p, "Z"))
        zone = 'Z';
    else
    {
        if (strlen(p) != 6 || (p[0] != '+' && p[0] != '-') || p[3] != ':' || !isdigit((unsigned char)p[1]) ||
            !isdigit((unsigned char)p[2]) || !isdigit((unsigned char)p[4]) || !isdigit((unsigned char)p[5]))
            return false;
        int n = 0;
        if (sscanf(p, "%c%2d:%2d%n", &zone, &oh, &om, &n) != 3 || n != 6 || p[n] || (zone != '+' && zone != '-') ||
            oh > 23 || om > 59)
            return false;
    }
    struct tm tm = {.tm_year = y - 1900, .tm_mon = m - 1, .tm_mday = d, .tm_hour = h, .tm_min = min, .tm_sec = s};
    time_t utc = timegm(&tm);
    if (tm.tm_year != y - 1900 || tm.tm_mon != m - 1 || tm.tm_mday != d)
        return false;
    *result = utc + (zone == '-' ? 1 : -1) * (oh * 3600 + om * 60);
    return true;
}
char *podcast_ard_query_url(const char *operation, const cJSON *variables)
{
    const char *query = !strcmp(operation, "Search")    ? search_query
                        : !strcmp(operation, "Show")    ? show_query
                        : !strcmp(operation, "Resolve") ? resolve_query
                                                        : NULL;
    if (!query)
        return NULL;
    char *json = cJSON_PrintUnformatted(variables), *q = curl_easy_escape(NULL, query, 0),
         *v = json ? curl_easy_escape(NULL, json, 0) : NULL, *url = NULL;
    if (q && v)
    {
        size_t length = strlen(q) + strlen(v) + 64;
        if (length <= 2049)
        {
            url = malloc(length);
            if (url)
                snprintf(url, length, "https://api.ardaudiothek.de/graphql?query=%s&variables=%s", q, v);
        }
    }
    free(json);
    curl_free(q);
    curl_free(v);
    return url;
}
static long milliseconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000;
}
typedef struct
{
    long deadline;
    size_t bytes;
    const atomic_bool *cancel;
    char *error;
    size_t size;
} lookup_t;
static cJSON *fetch(const char *operation, cJSON *variables, lookup_t *ctx)
{
    char *url = podcast_ard_query_url(operation, variables);
    cJSON_Delete(variables);
    long remaining = ctx->deadline - milliseconds();
    podcast_response_t r;
    cJSON *json = NULL;
    if (url && remaining > 0 && ctx->bytes < PODCAST_FEED_LIMIT &&
        podcast_http_get_timed(url, PODCAST_FEED_LIMIT - ctx->bytes, NULL, ctx->cancel, &r, ctx->error, ctx->size,
                               remaining))
    {
        ctx->bytes += r.length;
        json = r.data ? cJSON_ParseWithOpts(r.data, NULL, true) : NULL;
        cJSON *errors = cJSON_GetObjectItemCaseSensitive(json, "errors");
        if (!cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(json, "data")) ||
            (errors && (!cJSON_IsArray(errors) || cJSON_GetArraySize(errors))))
        {
            cJSON_Delete(json);
            json = NULL;
        }
        podcast_response_free(&r);
    }
    free(url);
    if (!json)
        snprintf(ctx->error, ctx->size, "ARD lookup failed or exceeded its response/time limit");
    return json;
}
static cJSON *variables(const char *key, const char *value)
{
    cJSON *v = cJSON_CreateObject();
    cJSON_AddStringToObject(v, key, value);
    return v;
}
static cJSON *show_variables(const char *id, unsigned offset, unsigned count)
{
    cJSON *v = variables("id", id);
    cJSON_AddNumberToObject(v, "offset", offset);
    cJSON_AddNumberToObject(v, "count", count);
    return v;
}
static bool bounded_text(const char *value, size_t limit)
{
    if (strlen(value) > limit)
        return false;
    for (const unsigned char *p = (const unsigned char *)value; *p; p++)
        if (*p < 32 || *p == 127)
            return false;
    return true;
}
static cJSON *show_metadata(const cJSON *show)
{
    const char *id = podcast_string(show, "coreId"), *url = podcast_string(show, "sharingUrl"),
               *numeric = podcast_string(show, "id");
    char parsed[64];
    if (!bounded_text(podcast_string(show, "title"), 512) || !core_id(id) || !numeric[0] || strlen(numeric) > 20 ||
        !podcast_ard_identifier(url, parsed) || (strcmp(parsed, id) && strcmp(parsed, numeric)))
        return NULL;
    for (const char *p = numeric; *p; p++)
        if (!isdigit((unsigned char)*p))
            return NULL;
    cJSON *o = cJSON_CreateObject();
    char key[64];
    snprintf(key, sizeof(key), "ard:%s", id);
    cJSON_AddStringToObject(o, "sourceType", "ard_audiothek");
    cJSON_AddStringToObject(o, "sourceKey", key);
    cJSON_AddStringToObject(o, "ardShowId", id);
    cJSON_AddStringToObject(o, "ardShowUrl", url);
    cJSON_AddStringToObject(o, "title", podcast_string(show, "title"));
    return o;
}
static cJSON *resolve(const char *url, lookup_t *ctx)
{
    char id[64];
    if (!podcast_ard_identifier(url, id))
        return NULL;
    bool urn = core_id(id);
    cJSON *response = fetch(urn ? "Resolve" : "Show", urn ? variables("coreId", id) : show_variables(id, 0, 0), ctx);
    cJSON *data = cJSON_GetObjectItemCaseSensitive(response, "data"),
          *show = cJSON_GetObjectItemCaseSensitive(data, urn ? "programSetByCoreId" : "programSet");
    cJSON *result = show_metadata(show);
    if (result)
        cJSON_AddStringToObject(result, "numericId", podcast_string(show, "id"));
    cJSON_Delete(response);
    return result;
}
cJSON *podcast_ard_resolve(const char *url, const atomic_bool *cancel, char *error, size_t size)
{
    lookup_t ctx = {milliseconds() + 30000, 0, cancel, error, size};
    cJSON *result = resolve(url, &ctx);
    if (!result)
        snprintf(error, size, "ARD show could not be resolved");
    return result;
}
cJSON *podcast_ard_search(const char *query, const atomic_bool *cancel, char *error, size_t size)
{
    lookup_t ctx = {milliseconds() + 30000, 0, cancel, error, size};
    cJSON *response = fetch("Search", variables("q", query), &ctx),
          *data = cJSON_GetObjectItemCaseSensitive(response, "data"),
          *search = cJSON_GetObjectItemCaseSensitive(data, "search"),
          *sets = cJSON_GetObjectItemCaseSensitive(search, "programSets"),
          *nodes = cJSON_GetObjectItemCaseSensitive(sets, "nodes"), *result = NULL;
    if (cJSON_IsArray(nodes))
    {
        result = cJSON_CreateObject();
        cJSON *entries = cJSON_AddArrayToObject(result, "results"), *item;
        cJSON_ArrayForEach(item, nodes)
        {
            if (cJSON_GetArraySize(entries) >= 20)
                break;
            cJSON *entry = show_metadata(item);
            if (!entry)
                continue;
            const char *author = podcast_string(cJSON_GetObjectItemCaseSensitive(item, "publicationService"), "title");
            if (!bounded_text(author, 512))
            {
                cJSON_Delete(entry);
                continue;
            }
            cJSON_AddStringToObject(entry, "providerId", podcast_string(item, "id"));
            cJSON_AddStringToObject(
                entry, "author", podcast_string(cJSON_GetObjectItemCaseSensitive(item, "publicationService"), "title"));
            cJSON_AddItemToArray(entries, entry);
        }
    }
    cJSON_Delete(response);
    if (!result)
        snprintf(error, size, "ARD show search is unavailable");
    return result;
}
static int compare(const void *a, const void *b)
{
    const cJSON *x = *(cJSON *const *)a, *y = *(cJSON *const *)b;
    double dx = podcast_number(x, "publishedAt"), dy = podcast_number(y, "publishedAt");
    return dx != dy ? (dx > dy ? -1 : 1) : strcmp(podcast_string(x, "identity"), podcast_string(y, "identity"));
}
cJSON *podcast_ard_preview(const char *url, unsigned count, bool oldest, time_t now, const atomic_bool *cancel,
                           char *error, size_t size)
{
    lookup_t ctx = {milliseconds() + 30000, 0, cancel, error, size};
    cJSON *result = resolve(url, &ctx);
    if (!result)
        goto fail;
    cJSON *episodes[1000];
    size_t found = 0, seen_count = 0;
    char *seen[1000] = {0};
    bool ended = false, ok = false;
    for (unsigned page = 0; page < 20; page++)
    {
        cJSON *response = fetch("Show", show_variables(podcast_string(result, "numericId"), page * 50, 50), &ctx),
              *show =
                  cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(response, "data"), "programSet"),
              *items = cJSON_GetObjectItemCaseSensitive(show, "items"),
              *nodes = cJSON_GetObjectItemCaseSensitive(items, "nodes"),
              *info = cJSON_GetObjectItemCaseSensitive(items, "pageInfo"),
              *has_next = cJSON_GetObjectItemCaseSensitive(info, "hasNextPage");
        if (!cJSON_IsArray(nodes) || !cJSON_IsBool(has_next) ||
            strcmp(podcast_string(show, "coreId"), podcast_string(result, "ardShowId")) ||
            cJSON_GetArraySize(nodes) > 50)
        {
            cJSON_Delete(response);
            break;
        }
        unsigned progress = 0;
        cJSON *item;
        cJSON_ArrayForEach(item, nodes)
        {
            const char *id = podcast_string(item, "id");
            if (!id[0] || !bounded_text(id, 512) || seen_count == 1000)
                continue;
            bool repeated = false;
            for (size_t i = 0; i < seen_count; i++)
                if (!strcmp(seen[i], id))
                    repeated = true;
            if (repeated)
                continue;
            seen[seen_count++] = strdup(id);
            progress++;
            time_t published;
            if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "isPublished")) ||
                strcmp(podcast_string(item, "itemType"), "EPISODE") ||
                !podcast_ard_date(podcast_string(item, "publishDate"), &published) || published > now)
                continue;
            const char *audio_url = NULL;
            cJSON *audio;
            cJSON_ArrayForEach(
                audio,
                cJSON_GetObjectItemCaseSensitive(
                    item, "audios")) if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(audio, "allowDownload")) &&
                                         podcast_url_valid(podcast_string(audio, "downloadUrl")))
            {
                audio_url = podcast_string(audio, "downloadUrl");
                break;
            }
            if (!audio_url)
                continue;
            const char *asset = podcast_string(item, "assetId");
            char fallback[528];
            snprintf(fallback, sizeof(fallback), "ard-item:%s", id);
            const char *identity = asset[0] ? asset : fallback;
            if (!bounded_text(identity, 512) || !bounded_text(podcast_string(item, "title"), 512))
                continue;
            bool duplicate = false;
            for (size_t i = 0; i < found; i++)
                if (!strcmp(podcast_string(episodes[i], "identity"), identity) ||
                    !strcmp(podcast_string(episodes[i], "url"), audio_url))
                    duplicate = true;
            if (duplicate)
                continue;
            cJSON *ep = cJSON_CreateObject();
            cJSON_AddStringToObject(ep, "identity", identity);
            cJSON_AddStringToObject(ep, "title", podcast_string(item, "title"));
            cJSON_AddStringToObject(ep, "url", audio_url);
            cJSON_AddNumberToObject(ep, "publishedAt", published);
            double duration = podcast_number(item, "duration");
            if (isfinite(duration) && duration > 0 && duration <= 21600)
                cJSON_AddNumberToObject(ep, "durationSeconds", duration);
            episodes[found++] = ep;
        }
        ended = cJSON_IsFalse(has_next);
        cJSON_Delete(response);
        if (found >= count || ended)
        {
            ok = found > 0;
            break;
        }
        if (!progress)
            break;
    }
    for (size_t i = 0; i < seen_count; i++)
        free(seen[i]);
    if (!ok)
    {
        for (size_t i = 0; i < found; i++)
            cJSON_Delete(episodes[i]);
        cJSON_Delete(result);
        result = NULL;
        goto fail;
    }
    qsort(episodes, found, sizeof(*episodes), compare);
    size_t n = found < count ? found : count;
    cJSON *list = cJSON_AddArrayToObject(result, "episodes");
    for (size_t i = 0; i < n; i++)
        cJSON_AddItemToArray(list, episodes[oldest ? n - 1 - i : i]);
    for (size_t i = n; i < found; i++)
        cJSON_Delete(episodes[i]);
    cJSON *warnings = cJSON_AddArrayToObject(result, "warnings");
    if (n < count)
        cJSON_AddItemToArray(warnings,
                             cJSON_CreateString("Fewer downloadable ARD episodes are available than requested"));
    cJSON_DeleteItemFromObjectCaseSensitive(result, "numericId");
    return result;
fail:
    snprintf(error, size, "ARD show has no playable episodes or lookup exceeded its limits");
    return NULL;
}
#endif
