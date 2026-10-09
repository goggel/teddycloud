#if PODCAST_SUPPORT
#include "podcast_http.h"
#include "cJSON.h"
#include <curl/curl.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <time.h>

bool podcast_http_init(void)
{
    return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
}
void podcast_http_cleanup(void)
{
    curl_global_cleanup();
}
void podcast_response_free(podcast_response_t *r)
{
    free(r->data);
    free(r->final_url);
    memset(r, 0, sizeof(*r));
}

bool podcast_address_public(int family, const void *address)
{
    const unsigned char *b = address;
    if (family == AF_INET)
    {
        return !(b[0] == 0 || b[0] == 10 || b[0] == 127 || b[0] >= 224 || (b[0] == 100 && (b[1] & 192) == 64) ||
                 (b[0] == 169 && b[1] == 254) || (b[0] == 172 && (b[1] & 240) == 16) ||
                 (b[0] == 192 && (b[1] == 168 || b[1] == 0 || b[1] == 2)) ||
                 (b[0] == 198 && (b[1] == 18 || b[1] == 19 || b[1] == 51)) ||
                 (b[0] == 203 && b[1] == 0 && b[2] == 113));
    }
    /* Only global unicast. Exclude documentation, Teredo and 6to4 gateways. */
    if (family == AF_INET6)
        return (b[0] & 224) == 32 && !(b[0] == 32 && b[1] == 1 && (b[2] < 2 || (b[2] == 13 && b[3] == 184))) &&
               !(b[0] == 32 && b[1] == 2);
    return false;
}

static CURLU *parse_url(const char *url)
{
    if (!url || strlen(url) > 2048 || strpbrk(url, "\r\n\t"))
        return NULL;
    CURLU *u = curl_url();
    char *scheme = NULL, *host = NULL, *user = NULL, *password = NULL, *fragment = NULL;
    bool valid = u && curl_url_set(u, CURLUPART_URL, url, 0) == CURLUE_OK &&
                 curl_url_get(u, CURLUPART_SCHEME, &scheme, 0) == CURLUE_OK &&
                 (!strcmp(scheme, "http") || !strcmp(scheme, "https")) &&
                 curl_url_get(u, CURLUPART_HOST, &host, 0) == CURLUE_OK && host[0] &&
                 curl_url_get(u, CURLUPART_USER, &user, 0) != CURLUE_OK &&
                 curl_url_get(u, CURLUPART_PASSWORD, &password, 0) != CURLUE_OK &&
                 curl_url_get(u, CURLUPART_FRAGMENT, &fragment, 0) != CURLUE_OK;
    curl_free(scheme);
    curl_free(host);
    curl_free(user);
    curl_free(password);
    curl_free(fragment);
    if (!valid)
    {
        curl_url_cleanup(u);
        return NULL;
    }
    return u;
}
bool podcast_url_valid(const char *url)
{
    CURLU *u = parse_url(url);
    if (!u)
        return false;
    curl_url_cleanup(u);
    return true;
}

typedef struct
{
    podcast_response_t *r;
    size_t limit, received;
    FILE *file;
    const atomic_bool *cancel;
    char location[2049];
} transfer_t;
static size_t receive(void *data, size_t size, size_t count, void *ctx)
{
    transfer_t *t = ctx;
    if (size && count > SIZE_MAX / size)
        return 0;
    size_t n = size * count;
    if ((t->cancel && atomic_load(t->cancel)) || n > t->limit - t->received)
        return 0;
    if (t->file)
    {
        if (fwrite(data, 1, n, t->file) != n)
            return 0;
    }
    else
    {
        char *next = realloc(t->r->data, t->received + n + 1);
        if (!next)
            return 0;
        t->r->data = next;
        memcpy(next + t->received, data, n);
        next[t->received + n] = 0;
    }
    t->received += n;
    t->r->length = t->received;
    return n;
}
static size_t header(void *data, size_t size, size_t count, void *ctx)
{
    transfer_t *t = ctx;
    size_t n = size * count;
    if (n > 9 && !strncasecmp(data, "Location:", 9))
    {
        const char *p = (char *)data + 9;
        size_t len = n - 9;
        while (len && (*p == ' ' || *p == '\t'))
        {
            p++;
            len--;
        }
        while (len && (p[len - 1] == '\r' || p[len - 1] == '\n' || p[len - 1] == ' '))
            len--;
        if (len >= sizeof(t->location))
            return 0;
        memcpy(t->location, p, len);
        t->location[len] = 0;
    }
    return n;
}
static int progress(void *ctx, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    transfer_t *t = ctx;
    return t->cancel && atomic_load(t->cancel);
}

/* Validate the actual resolved peer immediately before opening its socket.
 * curl connects to this same sockaddr, so DNS changes cannot substitute an
 * unchecked address. Its asynchronous resolver retains timeout/cancellation. */
static curl_socket_t open_public_socket(void *ctx, curlsocktype purpose, struct curl_sockaddr *address)
{
    (void)ctx;
    if (purpose != CURLSOCKTYPE_IPCXN)
        return CURL_SOCKET_BAD;
    const void *ip = address->family == AF_INET    ? (void *)&((struct sockaddr_in *)&address->addr)->sin_addr
                     : address->family == AF_INET6 ? (void *)&((struct sockaddr_in6 *)&address->addr)->sin6_addr
                                                   : NULL;
    if (!ip || !podcast_address_public(address->family, ip))
        return CURL_SOCKET_BAD;
    return socket(address->family, address->socktype, address->protocol);
}

static long monotonic_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000L + now.tv_nsec / 1000000;
}
bool podcast_http_get_timed(const char *url, size_t limit, const char *file, const atomic_bool *cancel,
                            podcast_response_t *r, char *error, size_t error_size, long timeout_ms)
{
    memset(r, 0, sizeof(*r));
    bool success = false;
    char *current = strdup(url);
    long deadline = monotonic_ms() + timeout_ms;
#if PODCAST_TESTING
    /* Exact URL mapping in a trusted test-process manifest; no production switch. */
    const char *manifest = getenv("TC_PODCAST_FIXTURES");
    if (manifest)
    {
        FILE *m = fopen(manifest, "rb");
        char *json = calloc(1, 65537);
        if (m && json)
        {
            size_t n = fread(json, 1, 65536, m);
            json[n] = 0;
        }
        if (m)
            fclose(m);
        cJSON *map = json ? cJSON_Parse(json) : NULL;
        cJSON *entry = cJSON_GetObjectItemCaseSensitive(map, url);
        FILE *in = cJSON_IsString(entry) ? fopen(entry->valuestring, "rb") : NULL;
        transfer_t t = {.r = r, .limit = limit, .cancel = cancel};
        if (file)
            t.file = fopen(file, "wb");
        if (in && (!file || t.file))
        {
            char buf[8192];
            size_t n;
            success = true;
            while ((n = fread(buf, 1, sizeof(buf), in)))
                if (receive(buf, 1, n, &t) != n)
                {
                    success = false;
                    break;
                }
            if (ferror(in))
                success = false;
        }
        if (in)
            fclose(in);
        if (t.file && fclose(t.file))
            success = false;
        cJSON_Delete(map);
        free(json);
        free(current);
        if (success)
        {
            r->final_url = strdup(url);
            return true;
        }
        snprintf(error, error_size, "Fixture transfer failed");
        podcast_response_free(r);
        if (file)
            remove(file);
        return false;
    }
#endif
    for (unsigned redirect = 0; current && redirect <= 5; redirect++)
    {
        long remaining = deadline - monotonic_ms();
        if (remaining <= 0)
            break;
        CURLU *u = parse_url(current);
        CURL *curl = u ? curl_easy_init() : NULL;
        if (!curl)
        {
            if (u)
                curl_url_cleanup(u);
            break;
        }
        transfer_t t = {.r = r, .limit = limit, .cancel = cancel};
        if (file)
            t.file = fopen(file, "wb");
        if (file && !t.file)
        {
            curl_easy_cleanup(curl);
            curl_url_cleanup(u);
            break;
        }
        curl_easy_setopt(curl, CURLOPT_URL, current);
        curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION, open_public_socket);
        curl_easy_setopt(curl, CURLOPT_PROXY, "");
        curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, remaining < 10000 ? remaining : 10000L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, remaining);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "TeddyCloud-Podcasts/1");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &t);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &t);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &t);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        CURLcode code = curl_easy_perform(curl);
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        if (t.file && fclose(t.file))
            code = CURLE_WRITE_ERROR;
        curl_easy_cleanup(curl);
        if (code == CURLE_OK && status >= 200 && status < 300)
        {
            r->final_url = strdup(current);
            success = true;
            curl_url_cleanup(u);
            break;
        }
        bool follow = code == CURLE_OK &&
                      (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) &&
                      t.location[0];
        char *next = NULL;
        if (follow && !curl_url_set(u, CURLUPART_URL, t.location, 0))
            curl_url_get(u, CURLUPART_URL, &next, 0);
        free(current);
        current = next ? strdup(next) : NULL;
        curl_free(next);
        curl_url_cleanup(u);
        podcast_response_free(r);
    }
    free(current);
    if (!success)
    {
        snprintf(error, error_size,
                 "Download failed: invalid/public-network URL, transfer limit, timeout or upstream error");
        podcast_response_free(r);
        if (file)
            remove(file);
    }
    return success;
}
bool podcast_http_get(const char *url, size_t limit, const char *file, const atomic_bool *cancel, podcast_response_t *r,
                      char *error, size_t error_size)
{
    return podcast_http_get_timed(url, limit, file, cancel, r, error, error_size, file ? 900000L : 30000L);
}
#endif
