#include "podcast.h"
#include "server_helpers.h"
#include "mutex_manager.h"
#if PODCAST_SUPPORT
#include "podcast_feed.h"
#include "podcast_http.h"
#include "podcast_encode.h"
#include "podcast_ard.h"
#include "podcast_trim.h"
#include "toniefile.h"
#include "hash/sha256.h"
#include "fs_ext.h"
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <dirent.h>
#include <curl/curl.h>
#include <libxml/parser.h>

static time_t podcast_now(void)
{
#if PODCAST_TESTING
    const char *path = getenv("TC_PODCAST_CLOCK");
    if (path)
    {
        FILE *f = fopen(path, "r");
        long long value = 0;
        if (f)
        {
            int n = fscanf(f, "%lld", &value);
            fclose(f);
            if (n == 1 && value > 0)
                return (time_t)value;
        }
    }
#endif
    return time(NULL);
}
static void publication_boundary(const char *boundary)
{
#if PODCAST_TESTING
    const char *path = getenv("TC_PODCAST_FAILURE");
    if (path)
    {
        FILE *f = fopen(path, "r");
        char selected[64] = "";
        if (f)
        {
            size_t n = fread(selected, 1, sizeof(selected) - 1, f);
            selected[n] = 0;
            fclose(f);
        }
        if (!strcmp(selected, boundary))
            _exit(90);
    }
#else
    (void)boundary;
#endif
}

/* Content -> service is the only nested lock order. Network/encoding never
 * holds either. JSON trees never escape the service lock without deep copying. */
static pthread_mutex_t service_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t discovery_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static pthread_t worker;
static atomic_bool stopping = false;
static atomic_bool job_cancel = false;
static bool started, damaged;
static atomic_bool available = false;
static unsigned active_requests;
static cJSON *registry;
static char *registry_path;
static char running_id[33];
static time_t search_time[2];
static char *search_key[2];
static cJSON *search_cache[2];
static time_t search_window[2];
static unsigned search_requests[2];

void podcast_content_lock(const char *path)
{
    (void)path;
    mutex_lock(MUTEX_CONTENT_JSON);
}
void podcast_content_unlock(const char *path)
{
    (void)path;
    mutex_unlock(MUTEX_CONTENT_JSON);
}
static void str(cJSON *o, const char *k, const char *v)
{
    cJSON_DeleteItemFromObjectCaseSensitive(o, k);
    cJSON_AddStringToObject(o, k, v ? v : "");
}
static void num(cJSON *o, const char *k, double v)
{
    cJSON_DeleteItemFromObjectCaseSensitive(o, k);
    cJSON_AddNumberToObject(o, k, v);
}
static void boolean(cJSON *o, const char *k, bool v)
{
    cJSON_DeleteItemFromObjectCaseSensitive(o, k);
    cJSON_AddBoolToObject(o, k, v);
}
static bool yes(cJSON *o, const char *k)
{
    return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, k));
}
static cJSON *subscriptions(void)
{
    return cJSON_GetObjectItemCaseSensitive(registry, "subscriptions");
}
static cJSON *find(const char *id)
{
    cJSON *s;
    cJSON_ArrayForEach(s, subscriptions()) if (!strcmp(podcast_string(s, "id"), id)) return s;
    return NULL;
}
static cJSON *failure(int *status, int code, const char *message)
{
    *status = code;
    cJSON *o = cJSON_CreateObject(), *e = cJSON_AddObjectToObject(o, "error");
    cJSON_AddStringToObject(e, "code",
                            code == 409   ? "conflict"
                            : code == 404 ? "not_found"
                            : code == 503 ? "unavailable"
                                          : "podcast_error");
    cJSON_AddStringToObject(e, "message", message);
    return o;
}
static bool hex(const char *s, size_t len)
{
    if (!s || strlen(s) != len)
        return false;
    for (size_t i = 0; i < len; i++)
        if (!isxdigit((unsigned char)s[i]))
            return false;
    return true;
}
static bool scope(const char *overlay, char **content, char **library)
{
    *content = NULL;
    *library = NULL;
    /* Match only explicit existing identities: get_settings_ovl falls back on
     * unknown names, which would silently bind an unrelated assignment. */
    mutex_lock(MUTEX_SETTINGS);
    settings_t *settings = NULL;
    if (!overlay || !*overlay)
        settings = get_settings();
    else
        for (unsigned i = 0; i < MAX_OVERLAYS; i++)
        {
            settings_t *candidate = get_settings_id(i);
            if (candidate->internal.config_used && candidate->internal.overlayUniqueId &&
                !strcmp(candidate->internal.overlayUniqueId, overlay))
            {
                settings = candidate;
                break;
            }
        }
    if (settings)
    {
        *content = realpath(settings->internal.contentdirfull, NULL);
        *library = realpath(settings->internal.librarydirfull, NULL);
    }
    mutex_unlock(MUTEX_SETTINGS);
    if (!*content || !*library)
    {
        free(*content);
        free(*library);
        *content = *library = NULL;
        return false;
    }
    return true;
}
static char *content_path(const char *root, const char *ruid)
{
    char upper[17];
    snprintf(upper, sizeof(upper), "%s", ruid);
    for (char *p = upper; *p; p++)
        *p = toupper((unsigned char)*p);
    return custom_asprintf("%s/%.8s/%.8s", root, upper, upper + 8);
}
static bool sync_directory(const char *path)
{
    char *dir = strdup(path), *slash = strrchr(dir, '/');
    if (!slash)
    {
        free(dir);
        return false;
    }
    *slash = 0;
    int fd = open(dir, O_RDONLY | O_DIRECTORY);
    free(dir);
    bool ok = fd >= 0 && fsync(fd) == 0;
    if (fd >= 0)
        close(fd);
    return ok;
}
static bool write_json(const char *path, const cJSON *o)
{
    char *raw = cJSON_PrintUnformatted(o), *tmp = custom_asprintf("%s.tmp", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    size_t length = raw ? strlen(raw) : 0, off = 0;
    bool ok = raw && fd >= 0 && length <= 64U * 1024U * 1024U;
    while (ok && off < length)
    {
        ssize_t n = write(fd, raw + off, length - off);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            ok = false;
        else
            off += (size_t)n;
    }
    if (fd >= 0)
    {
        if (fsync(fd))
            ok = false;
        if (close(fd))
            ok = false;
    }
    if (ok && rename(tmp, path))
        ok = false;
    if (ok)
        ok = sync_directory(path);
    if (!ok)
        unlink(tmp);
    free(raw);
    free(tmp);
    return ok;
}
static cJSON *read_json(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    struct stat st;
    if (fstat(fileno(f), &st) || st.st_size > 64 * 1024 * 1024 || st.st_size < 1)
    {
        fclose(f);
        return NULL;
    }
    char *raw = calloc(1, (size_t)st.st_size + 1);
    cJSON *o = NULL;
    if (raw && fread(raw, 1, (size_t)st.st_size, f) == (size_t)st.st_size)
        o = cJSON_ParseWithOpts(raw, NULL, true);
    free(raw);
    fclose(f);
    return o;
}
static bool legacy_config_valid(const cJSON *s)
{
    double n = podcast_number(s, "episodeCount"), h = podcast_number(s, "intervalHours");
    const char *order = podcast_string(s, "order");
    return podcast_url_valid(podcast_string(s, "feedUrl")) && n >= 1 && n <= 50 && floor(n) == n &&
           cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(s, "intervalHours")) && h >= 0 && h <= 168 &&
           floor(h) == h && (!strcmp(order, "newest_first") || !strcmp(order, "oldest_first")) &&
           cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(s, "enabled")) &&
           (!cJSON_GetObjectItemCaseSensitive(s, "refreshOnPlacement") ||
            cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(s, "refreshOnPlacement")));
}
static bool valid_registry(cJSON *o)
{
    double version = podcast_number(o, "version");
    if (!cJSON_IsObject(o) || (version != 1 && version != 2) ||
        !cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(o, "subscriptions")) ||
        !cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(o, "pins")))
        return false;
    cJSON *s;
    char error[256];
    cJSON_ArrayForEach(s, cJSON_GetObjectItemCaseSensitive(o, "subscriptions"))
    {
        if (!hex(podcast_string(s, "id"), 32) || !hex(podcast_string(s, "ruid"), 16) ||
            podcast_number(s, "revision") < 1)
            return false;
        if (version == 1)
        {
            if (!legacy_config_valid(s))
                return false;
        }
        else if (!podcast_config_valid(s, error, sizeof(error)) ||
                 !cJSON_IsString(cJSON_GetObjectItemCaseSensitive(s, "sourceType")) ||
                 !cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(s, "trimStartSeconds")) ||
                 !cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(s, "trimEndSeconds")) ||
                 !cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(s, "episodeTrims")) ||
                 !cJSON_IsString(cJSON_GetObjectItemCaseSensitive(s, "episodeTrimSourceKey")) ||
                 !*podcast_string(s, "sourceKey") || strlen(podcast_string(s, "sourceKey")) > 2052 ||
                 (cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(s, "episodeTrims")) &&
                  strcmp(podcast_string(s, "sourceKey"), podcast_string(s, "episodeTrimSourceKey"))))
            return false;
    }
    return true;
}
static bool persist(void)
{
    if (damaged || !registry || !registry_path)
        return false;
    char *backup = custom_asprintf("%s.bak", registry_path);
    cJSON *prior = read_json(registry_path);
    /* Backup the previous committed state first. Failure cannot leave an API
     * reporting failure after its primary state was actually committed. */
    bool ok = write_json(backup, valid_registry(prior) ? prior : registry);
    if (ok)
        ok = write_json(registry_path, registry);
    cJSON_Delete(prior);
    free(backup);
    return ok;
}

static cJSON *public_subscription(cJSON *s, const char *assigned_source)
{
    cJSON *copy = cJSON_Duplicate(s, true);
    if (assigned_source)
    {
        const char *published = podcast_string(s, "publishedSource");
        cJSON *original = cJSON_GetObjectItemCaseSensitive(s, "original");
        bool owns = *published ? !strcmp(assigned_source, published)
                               : yes(s, "enabled") &&
                                     (yes(s, "job") || !strcmp(running_id, podcast_string(s, "id"))) &&
                                     !strcmp(assigned_source, podcast_string(original, "source"));
        boolean(copy, "ownsAssignment", owns);
    }
    if (strcmp(podcast_string(copy, "displayNameSourceKey"), podcast_string(copy, "sourceKey")))
    {
        str(copy, "displayName", "");
        str(copy, "displayNameSourceKey", "");
    }
    const char *private_keys[] = {"scope",         "library",  "original", "pending",         "fingerprint",
                                  "lastPlacement", "failures", "job",      "publishedSource", NULL};
    for (unsigned i = 0; private_keys[i]; i++)
        cJSON_DeleteItemFromObjectCaseSensitive(copy, private_keys[i]);
    return copy;
}
static void display_name_from_feed(cJSON *subscription, const cJSON *feed)
{
    const char *title = podcast_string(feed, "title");
    const char *key = podcast_string(feed, "sourceKey");
    if (*title && *key && !strcmp(key, podcast_string(subscription, "sourceKey")))
    {
        char bounded[513];
        size_t size = strlen(title);
        if (size > 512)
        {
            size = 512;
            while (size && ((unsigned char)title[size] & 0xc0) == 0x80) size--;
        }
        memcpy(bounded, title, size);
        bounded[size] = 0;
        for (size_t i = 0; i < size; i++)
            if ((unsigned char)bounded[i] < 32 || (unsigned char)bounded[i] == 127) bounded[i] = ' ';
        str(subscription, "displayName", bounded);
        str(subscription, "displayNameSourceKey", key);
    }
}
static unsigned queued_count(void)
{
    unsigned n = 0;
    cJSON *s;
    cJSON_ArrayForEach(s, subscriptions()) if (yes(s, "job")) n++;
    return n;
}
static bool enqueue(cJSON *s)
{
    if (!yes(s, "enabled"))
        return false;
    if (!yes(s, "job") && queued_count() >= 32)
        return false;
    boolean(s, "job", true);
    if (strcmp(running_id, podcast_string(s, "id")))
        str(s, "state", "queued");
    pthread_cond_signal(&wake);
    return true;
}
static cJSON *preview(const cJSON *body, const atomic_bool *cancel, char *error, size_t size)
{
    unsigned n = (unsigned)podcast_number(body, "episodeCount");
    bool oldest = !strcmp(podcast_string(body, "order"), "oldest_first");
    cJSON *p = NULL;
    if (!strcmp(podcast_string(body, "sourceType"), "ard_audiothek"))
        p = podcast_ard_preview(podcast_string(body, "ardShowUrl"), n, oldest, podcast_now(), cancel, error, size);
    else
    {
        podcast_response_t r;
        if (!podcast_http_get(podcast_string(body, "feedUrl"), PODCAST_FEED_LIMIT, NULL, cancel, &r, error, size))
            return NULL;
        p = podcast_feed_parse(r.data, r.length, r.final_url, n, oldest, podcast_now(), error, size);
        podcast_response_free(&r);
        if (p)
        {
            str(p, "sourceType", "rss");
            str(p, "sourceKey", podcast_string(body, "sourceKey"));
        }
    }
    if (p)
        podcast_trim_preview(p, body);
    return p;
}
/* Normalize outside service/content locks. The request wrapper retains a private
 * revision snapshot and request_impl checks that revision again before saving. */
static cJSON *normalize_config(const cJSON *input, const cJSON *prior, bool is_preview, int *status)
{
    cJSON *body = cJSON_Duplicate(input, true);
    char *requested_ard = NULL, *resolved_title = NULL;
    bool supplied_name = cJSON_GetObjectItemCaseSensitive(input, "displayName") != NULL;
    bool trim_mismatch = false;
    const char *fields[] = {"sourceType",           "ardShowUrl", "trimStartSeconds", "trimEndSeconds", "episodeTrims",
                            "episodeTrimSourceKey", NULL};
    for (unsigned i = 0; fields[i]; i++)
        if (!cJSON_GetObjectItemCaseSensitive(body, fields[i]))
        {
            cJSON *value = cJSON_GetObjectItemCaseSensitive(prior, fields[i]);
            if (value)
                cJSON_AddItemToObject(body, fields[i], cJSON_Duplicate(value, true));
        }
    if (!cJSON_GetObjectItemCaseSensitive(body, "sourceType"))
        str(body, "sourceType", "rss");
    podcast_trim_defaults(body);
    if (is_preview)
    {
        if (!cJSON_GetObjectItemCaseSensitive(body, "intervalHours"))
            num(body, "intervalHours", 0);
        if (!cJSON_GetObjectItemCaseSensitive(body, "enabled"))
            boolean(body, "enabled", true);
    }
    char error[256];
    if (!podcast_config_valid(body, error, sizeof(error)))
    {
        *status = 400;
        goto fail;
    }
    boolean(body, "refreshOnPlacement", true);
    requested_ard = strdup(podcast_string(body, "ardShowUrl"));
    if (!strcmp(podcast_string(body, "sourceType"), "ard_audiothek"))
    {
        if (is_preview)
        {
            free(requested_ard);
            return body;
        }
        cJSON *source = podcast_ard_resolve(podcast_string(body, "ardShowUrl"), &stopping, error, sizeof(error));
        if (!source)
        {
            *status = 502;
            goto fail;
        }
        str(body, "sourceKey", podcast_string(source, "sourceKey"));
        str(body, "ardShowId", podcast_string(source, "ardShowId"));
        str(body, "ardShowUrl", podcast_string(source, "ardShowUrl"));
        resolved_title = strdup(podcast_string(source, "title"));
        cJSON_Delete(source);
        str(body, "feedUrl", "");
    }
    else
    {
        CURLU *u = curl_url();
        char *url = NULL;
        curl_url_set(u, CURLUPART_URL, podcast_string(body, "feedUrl"), 0);
        curl_url_get(u, CURLUPART_URL, &url, CURLU_NO_DEFAULT_PORT);
        if (!url)
        {
            curl_url_cleanup(u);
            *status = 400;
            snprintf(error, sizeof(error), "Invalid RSS URL");
            goto fail;
        }
        str(body, "feedUrl", url);
        char *key = custom_asprintf("rss:%s", url);
        str(body, "sourceKey", key);
        free(key);
        curl_free(url);
        curl_url_cleanup(u);
        str(body, "ardShowUrl", "");
        str(body, "ardShowId", "");
    }
    if (!is_preview)
    {
        const char *key = podcast_string(body, "sourceKey");
        const char *requested_key = podcast_string(body, "displayNameSourceKey");
        const char *requested_name = podcast_string(body, "displayName");
        if (supplied_name && *requested_name && strcmp(requested_key, key) &&
            (strcmp(podcast_string(body, "sourceType"), "ard_audiothek") ||
             strcmp(requested_key, requested_ard)))
        {
            *status = 409;
            snprintf(error, sizeof(error), "Display name belongs to a different source");
            goto fail;
        }
        char *name = strdup(supplied_name ? requested_name :
                            prior && !strcmp(key, podcast_string(prior, "sourceKey")) &&
                            !strcmp(key, podcast_string(prior, "displayNameSourceKey")) ?
                            podcast_string(prior, "displayName") : "");
        if (resolved_title && *resolved_title) { free(name); name = strdup(resolved_title); }
        str(body, "displayName", name);
        str(body, "displayNameSourceKey", *name ? key : "");
        free(name);
    }
    if (cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(body, "episodeTrims")))
    {
        if (strcmp(podcast_string(body, "episodeTrimSourceKey"), podcast_string(body, "sourceKey")))
        {
            trim_mismatch = true;
            *status = 409;
            snprintf(error, sizeof(error),
                     "Episode overrides belong to a different source; explicitly clear them before changing source");
            goto fail;
        }
    }
    else
        str(body, "episodeTrimSourceKey", "");
    free(requested_ard);
    free(resolved_title);
    return body;
fail:
    free(requested_ard);
    free(resolved_title);
    cJSON_Delete(body);
    cJSON *result = failure(status, *status, error);
    if (trim_mismatch)
        str(cJSON_GetObjectItemCaseSensitive(result, "error"), "code", "trim_source_mismatch");
    return result;
}
static void stage(const char *id, unsigned revision, const char *state)
{
    pthread_mutex_lock(&service_lock);
    cJSON *s = find(id);
    if (s && podcast_number(s, "revision") == revision)
        str(s, "state", state);
    pthread_mutex_unlock(&service_lock);
}
static bool still_current(const char *id, unsigned revision)
{
    pthread_mutex_lock(&service_lock);
    cJSON *s = find(id);
    bool ok = s && yes(s, "enabled") && podcast_number(s, "revision") == revision && !atomic_load(&stopping);
    pthread_mutex_unlock(&service_lock);
    return ok;
}
static void prune(cJSON *snapshot, bool startup);
static char *cached_input(const char *directory, const cJSON *episode)
{
    Sha256Context context;
    uint8_t digest[32];
    char key[65];
    const char *identity = podcast_string(episode, "identity"), *url = podcast_string(episode, "url");
    sha256Init(&context);
    sha256Update(&context, identity, strlen(identity) + 1);
    sha256Update(&context, url, strlen(url));
    sha256Final(&context, digest);
    for (unsigned i = 0; i < 32; i++)
        snprintf(key + i * 2, 3, "%02x", digest[i]);
    return custom_asprintf("%s/cache-%s.audio", directory, key);
}
static void prune_inputs(const char *directory, char *const *keep, size_t count)
{
    DIR *dir = opendir(directory);
    if (!dir)
        return;
    struct dirent *entry;
    while ((entry = readdir(dir)))
    {
        const char *name = entry->d_name;
        if (strlen(name) != 76 || strncmp(name, "cache-", 6) || strcmp(name + 70, ".audio"))
            continue;
        bool generated = true;
        for (unsigned i = 6; i < 70; i++)
            if (!isxdigit((unsigned char)name[i]))
                generated = false;
        if (!generated)
            continue;
        char *path = custom_asprintf("%s/%s", directory, name);
        bool retain = false;
        for (size_t i = 0; i < count; i++)
            if (keep[i] && !strcmp(keep[i], path))
                retain = true;
        if (!retain)
            unlink(path);
        free(path);
    }
    closedir(dir);
}

static void prepare(cJSON *job)
{
    const char *id = podcast_string(job, "id");
    unsigned revision = podcast_number(job, "revision");
    char error[256] = "", *root = NULL, *library = NULL, *dir = NULL, *output = NULL, *final = NULL, *source = NULL,
         *path = NULL;
    char *inputs[50] = {0};
    size_t count = 0;
    cJSON *feed = NULL;
    bool ok = false, unchanged = false;
    if (!scope(podcast_string(job, "overlay"), &root, &library) || strcmp(root, podcast_string(job, "scope")) ||
        strcmp(library, podcast_string(job, "library")))
    {
        snprintf(error, sizeof(error), "The content/library assignment changed; save the subscription again");
        goto done;
    }
    stage(id, revision, "checking");
    feed = preview(job, &job_cancel, error, sizeof(error));
    if (!feed)
        goto done;
    if (strcmp(podcast_string(feed, "sourceKey"), podcast_string(job, "sourceKey")))
    {
        snprintf(error, sizeof(error), "Source identity changed; save the subscription again");
        goto done;
    }
    cJSON *episodes = cJSON_GetObjectItemCaseSensitive(feed, "episodes");
    /* Encoding profile participates in fingerprint; titles/dates alone do not
     * change audio, but enclosure URL and ordered GUID identity do. */
    cJSON *f = cJSON_CreateObject();
    cJSON_AddStringToObject(f, "profile", "taf-opus-48k-stereo-trim-v2");
    cJSON_AddStringToObject(f, "sourceKey", podcast_string(feed, "sourceKey"));
    cJSON *keys = cJSON_AddArrayToObject(f, "episodes"), *ep;
    cJSON_ArrayForEach(ep, episodes)
    {
        cJSON *k = cJSON_CreateObject();
        cJSON_AddStringToObject(k, "identity", podcast_string(ep, "identity"));
        cJSON_AddStringToObject(k, "url", podcast_string(ep, "url"));
        num(k, "trimStartSeconds", podcast_number(ep, "trimStartSeconds"));
        num(k, "trimEndSeconds", podcast_number(ep, "trimEndSeconds"));
        cJSON_AddItemToArray(keys, k);
    }
    char *serialized = cJSON_PrintUnformatted(f);
    cJSON_Delete(f);
    const char *published = podcast_string(job, "publishedSource");
    if (!strcmp(serialized, podcast_string(job, "fingerprint")) && !strncmp(published, "lib://podcasts/", 15))
    {
        char *existing = custom_asprintf("%s/%s", library, published + 6);
        bool valid = isValidTaf(existing, true);
        free(existing);
        if (valid)
        {
            unchanged = true;
            ok = true;
            free(serialized);
            goto done;
        }
    }
    str(job, "fingerprint", serialized);
    free(serialized);
    dir = custom_asprintf("%s/podcasts/%s", library, id);
    if (!fsDirExists(dir) && fsCreateDirEx(dir, true) != NO_ERROR)
    {
        snprintf(error, sizeof(error), "Cannot create podcast storage");
        goto done;
    }
    uint32_t audio_id;
    pthread_mutex_lock(&service_lock);
    uint64_t last = (uint64_t)podcast_number(registry, "lastAudioId"), timestamp = (uint64_t)podcast_now();
    timestamp = timestamp > TEDDY_BENCH_AUDIO_ID_DEDUCT ? timestamp - TEDDY_BENCH_AUDIO_ID_DEDUCT : 1;
    uint64_t next = last + 1 > timestamp ? last + 1 : timestamp;
    if (next > UINT32_MAX)
    {
        pthread_mutex_unlock(&service_lock);
        snprintf(error, sizeof(error), "Audio version space exhausted");
        goto done;
    }
    num(registry, "lastAudioId", next);
    bool allocated = persist();
    pthread_mutex_unlock(&service_lock);
    if (!allocated)
    {
        snprintf(error, sizeof(error), "Cannot persist audio version");
        goto done;
    }
    audio_id = (uint32_t)next;
    output = custom_asprintf("%s/%08x.tmp", dir, audio_id);
    final = custom_asprintf("%s/%08x.taf", dir, audio_id);
    source = custom_asprintf("lib://podcasts/%s/%08x.taf", id, audio_id);
    stage(id, revision, "downloading");
    size_t selected_count = 0;
    cJSON_ArrayForEach(ep, episodes) inputs[selected_count++] = cached_input(dir, ep);
    prune_inputs(dir, inputs, selected_count);
    uint64_t workspace = 0;
    for (size_t i = 0; i < selected_count; i++)
    {
        struct stat existing;
        if (!lstat(inputs[i], &existing) && S_ISREG(existing.st_mode) && existing.st_size > 0 &&
            existing.st_size <= PODCAST_AUDIO_LIMIT)
            workspace += (uint64_t)existing.st_size;
    }
    cJSON_ArrayForEach(ep, episodes)
    {
        if (!still_current(id, revision))
        {
            snprintf(error, sizeof(error), "Preparation superseded or cancelled");
            goto done;
        }
        struct stat st;
        bool cached =
            !lstat(inputs[count], &st) && S_ISREG(st.st_mode) && st.st_size > 0 && st.st_size <= PODCAST_AUDIO_LIMIT;
        if (!cached)
        {
            struct statvfs space;
            if (statvfs(dir, &space) ||
                (uint64_t)space.f_bavail * space.f_frsize < PODCAST_AUDIO_LIMIT + 240ULL * 1024 * 1024)
            {
                snprintf(error, sizeof(error), "Not enough free podcast storage");
                goto done;
            }
            if (workspace >= 1024ULL * 1024 * 1024)
            {
                snprintf(error, sizeof(error), "Preparation exceeds 1 GiB workspace");
                goto done;
            }
            size_t remaining = (size_t)(1024ULL * 1024 * 1024 - workspace);
            size_t limit = remaining < PODCAST_AUDIO_LIMIT ? remaining : PODCAST_AUDIO_LIMIT;
            char *download = custom_asprintf("%s.tmp", inputs[count]);
            podcast_response_t r;
            bool downloaded =
                podcast_http_get(podcast_string(ep, "url"), limit, download, &job_cancel, &r, error, sizeof(error));
            if (downloaded)
            {
                int fd = open(download, O_RDONLY | O_CLOEXEC);
                downloaded = fd >= 0 && fsync(fd) == 0;
                if (fd >= 0)
                    close(fd);
                if (downloaded)
                    downloaded = rename(download, inputs[count]) == 0;
                workspace += r.length;
                podcast_response_free(&r);
            }
            unlink(download);
            free(download);
            if (!downloaded)
                goto done;
        }
        count++;
        if (workspace > 1024ULL * 1024 * 1024)
        {
            snprintf(error, sizeof(error), "Preparation exceeds 1 GiB workspace");
            goto done;
        }
    }
    stage(id, revision, "encoding");
    podcast_encode_input_t encoding[50];
    double durations[50];
    for (size_t i = 0; i < count; i++)
    {
        cJSON *episode = cJSON_GetArrayItem(episodes, (int)i);
        encoding[i] = (podcast_encode_input_t){inputs[i], (uint32_t)podcast_number(episode, "trimStartSeconds"),
                                               (uint32_t)podcast_number(episode, "trimEndSeconds")};
    }
    if (!podcast_encode(encoding, count, output, audio_id, &job_cancel, durations, error, sizeof(error)))
        goto done;
    for (size_t i = 0; i < count; i++)
        num(cJSON_GetArrayItem(episodes, (int)i), "trimmedDurationSeconds", durations[i]);
    int fd = open(output, O_RDONLY | O_CLOEXEC);
    bool synced = fd >= 0 && fsync(fd) == 0;
    if (fd >= 0)
        close(fd);
    if (!synced || rename(output, final) || !sync_directory(final))
    {
        snprintf(error, sizeof(error), "Cannot commit prepared audio");
        goto done;
    }
    cJSON *manifest = cJSON_Duplicate(feed, true);
    cJSON_AddStringToObject(manifest, "encoderProfile", "taf-opus-48k-stereo-trim-v2");
    cJSON_AddNumberToObject(manifest, "audioId", audio_id);
    cJSON_AddStringToObject(manifest, "source", source);
    cJSON_AddNumberToObject(manifest, "createdAt", podcast_now());
    char *manifest_path = custom_asprintf("%s.json", final);
    bool manifest_ok = write_json(manifest_path, manifest);
    free(manifest_path);
    cJSON_Delete(manifest);
    if (!manifest_ok)
    {
        snprintf(error, sizeof(error), "Cannot commit episode manifest");
        goto done;
    }
    /* No borrowed settings pointer crosses a job. Resolve again before publishing. */
    char *current_root = NULL, *current_library = NULL;
    bool same = scope(podcast_string(job, "overlay"), &current_root, &current_library) && !strcmp(current_root, root) &&
                !strcmp(current_library, library);
    free(current_root);
    free(current_library);
    if (!same)
    {
        snprintf(error, sizeof(error), "The assignment changed during preparation");
        goto done;
    }
    path = content_path(root, podcast_string(job, "ruid"));
    podcast_content_lock(path);
    pthread_mutex_lock(&service_lock);
    cJSON *s = find(id);
    if (s && yes(s, "enabled") && podcast_number(s, "revision") == revision && !atomic_load(&stopping))
    {
        settings_t *settings = get_settings_ovl(podcast_string(job, "overlay"));
        contentJson_t content;
        load_content_json(path, &content, true, settings);
        const char *previous = podcast_string(s, "publishedSource");
        cJSON *original = cJSON_GetObjectItemCaseSensitive(s, "original");
        const char *expected = previous[0] ? previous : podcast_string(original, "source");
        if (!strcmp(content.source ? content.source : "", expected))
        {
            cJSON *pending = cJSON_CreateObject();
            str(pending, "source", source);
            str(pending, "fingerprint", podcast_string(job, "fingerprint"));
            cJSON_AddItemToObject(pending, "feed", cJSON_Duplicate(feed, true));
            num(pending, "audioId", audio_id);
            cJSON_DeleteItemFromObjectCaseSensitive(s, "pending");
            cJSON_AddItemToObject(s, "pending", pending);
            if (persist())
            {
                publication_boundary("after_intent");
                free(content.source);
                content.source = strdup(source);
                content.live = false;
                content.nocloud = true;
                content.cloud_override = false;
                char *json_path = custom_asprintf("%s.json", path);
                if (save_content_json(json_path, &content) == NO_ERROR)
                {
                    publication_boundary("after_source");
                    str(s, "publishedSource", source);
                    str(s, "fingerprint", podcast_string(job, "fingerprint"));
                    str(s, "title", podcast_string(feed, "title"));
                    display_name_from_feed(s, feed);
                    num(s, "audioId", audio_id);
                    cJSON_DeleteItemFromObjectCaseSensitive(s, "episodes");
                    cJSON_AddItemToObject(s, "episodes", cJSON_Duplicate(episodes, true));
                    cJSON_DeleteItemFromObjectCaseSensitive(s, "pending");
                    ok = true;
                }
                free(json_path);
            }
            if (!ok)
                snprintf(error, sizeof(error), "Cannot save prepared assignment; recovery is pending");
        }
        else
        {
            boolean(s, "enabled", false);
            str(s, "state", "paused");
            snprintf(error, sizeof(error), "Audio assignment changed manually; subscription paused");
        }
        free_content_json(&content);
    }
    else
        snprintf(error, sizeof(error), "Preparation superseded or cancelled");
    pthread_mutex_unlock(&service_lock);
    podcast_content_unlock(path);
done:
    for (unsigned i = 0; i < 50; i++)
        if (inputs[i])
        {
            if (!ok)
                unlink(inputs[i]);
            free(inputs[i]);
        }
    if (output)
        unlink(output);
    pthread_mutex_lock(&service_lock);
    cJSON *saved = find(id);
    if (saved && podcast_number(saved, "revision") == revision)
    {
        time_t now = podcast_now();
        num(saved, "lastCheckedAt", now);
        /* A successful feed lookup names the requested source even if a later
         * download or encode fails. The prepared title remains unchanged. */
        if (feed)
            display_name_from_feed(saved, feed);
        if (ok)
        {
            str(saved, "state", "ready");
            str(saved, "lastError", "");
            num(saved, "lastSuccessAt", now);
            num(saved, "failures", 0);
            unsigned hours = podcast_number(saved, "intervalHours");
            num(saved, "nextCheckAt", hours ? now + hours * 3600 : 0);
            if (unchanged && feed)
            {
                if (*podcast_string(feed, "title")) str(saved, "title", podcast_string(feed, "title"));
                display_name_from_feed(saved, feed);
            }
        }
        else
        {
            str(saved, "state", yes(saved, "enabled") ? "error" : "paused");
            str(saved, "lastError", error[0] ? error : "Preparation failed");
            unsigned failures = podcast_number(saved, "failures") + 1;
            num(saved, "failures", failures);
            unsigned hours = podcast_number(saved, "intervalHours"), delay = failures == 1 ? 900
                                                                             : failures == 2
                                                                                 ? 3600
                                                                                 : (hours ? hours : 6) * 3600;
            num(saved, "nextCheckAt", now + delay);
        }
        if (!persist())
        {
            str(saved, "state", "error");
            str(saved, "lastError", "Could not persist podcast state; check storage permissions/free space");
        }
    }
    pthread_mutex_unlock(&service_lock);
    cJSON_Delete(feed);
    free(root);
    free(library);
    free(dir);
    free(output);
    free(final);
    free(source);
    free(path);
}
static void *work(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&service_lock);
    while (!atomic_load(&stopping))
    {
        cJSON *selected = NULL, *s;
        cJSON_ArrayForEach(s, subscriptions()) if (yes(s, "job") && yes(s, "enabled"))
        {
            selected = s;
            break;
        }
        if (!selected)
        {
            pthread_cond_wait(&wake, &service_lock);
            continue;
        }
        boolean(selected, "job", false);
        str(selected, "state", "checking");
        snprintf(running_id, sizeof(running_id), "%s", podcast_string(selected, "id"));
        cJSON *job = cJSON_Duplicate(selected, true);
        atomic_store(&job_cancel, false);
        pthread_mutex_unlock(&service_lock);
        prepare(job);
        prune(job, false);
        cJSON_Delete(job);
        pthread_mutex_lock(&service_lock);
        running_id[0] = 0;
    }
    pthread_mutex_unlock(&service_lock);
    return NULL;
}
/* Remove only generated files in this subscription's owned directory. Detached
 * directories are kept. Expired pins remain tombstones so offsets are rejected. */
static void prune(cJSON *snapshot, bool startup)
{
    const char *id = podcast_string(snapshot, "id"), *library = podcast_string(snapshot, "library");
    if (!hex(id, 32))
        return;
    char *directory = custom_asprintf("%s/podcasts/%s", library, id);
    DIR *dir = opendir(directory);
    if (!dir)
    {
        free(directory);
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)))
    {
        const char *name = entry->d_name;
        size_t n = strlen(name);
        bool temporary = startup && n > 4 && !strcmp(name + n - 4, ".tmp");
        bool generation = n == 12 && !strcmp(name + 8, ".taf");
        if (generation)
            for (unsigned i = 0; i < 8; i++)
                if (!isxdigit((unsigned char)name[i]))
                    generation = false;
        if (!temporary && !generation)
            continue;
        char *file = custom_asprintf("%s/%s", directory, name);
        struct stat st;
        if (lstat(file, &st) || !S_ISREG(st.st_mode))
        {
            free(file);
            continue;
        }
        bool remove = temporary;
        if (generation && podcast_now() - st.st_mtime > 86400)
        {
            char *source = custom_asprintf("lib://podcasts/%s/%s", id, name);
            pthread_mutex_lock(&service_lock);
            cJSON *current = find(id), *pin;
            bool referenced = !current || !strcmp(source, podcast_string(current, "publishedSource"));
            cJSON *pending = cJSON_GetObjectItemCaseSensitive(current, "pending");
            if (!strcmp(source, podcast_string(pending, "source")))
                referenced = true;
            cJSON_ArrayForEach(pin, cJSON_GetObjectItemCaseSensitive(registry, "pins"))
            {
                if (!strcmp(source, podcast_string(pin, "source")) &&
                    podcast_now() - podcast_number(pin, "createdAt") <= 86400)
                    referenced = true;
            }
            pthread_mutex_unlock(&service_lock);
            remove = !referenced;
            free(source);
        }
        if (remove)
        {
            unlink(file);
            if (generation)
            {
                char *manifest = custom_asprintf("%s.json", file);
                unlink(manifest);
                free(manifest);
            }
        }
        free(file);
    }
    closedir(dir);
    free(directory);
}

static void recover(void)
{
    cJSON *s;
    cJSON_ArrayForEach(s, subscriptions())
    {
        /* Reconcile source/registry split commits from durable intent. */
        cJSON *pending = cJSON_GetObjectItemCaseSensitive(s, "pending");
        if (pending)
        {
            char *root = NULL, *lib = NULL;
            if (scope(podcast_string(s, "overlay"), &root, &lib) && !strcmp(root, podcast_string(s, "scope")))
            {
                char *path = content_path(root, podcast_string(s, "ruid"));
                contentJson_t content;
                load_content_json(path, &content, false, get_settings_ovl(podcast_string(s, "overlay")));
                if (content.source && !strcmp(content.source, podcast_string(pending, "source")))
                {
                    str(s, "publishedSource", podcast_string(pending, "source"));
                    str(s, "fingerprint", podcast_string(pending, "fingerprint"));
                    cJSON *feed = cJSON_GetObjectItemCaseSensitive(pending, "feed");
                    str(s, "title", podcast_string(feed, "title"));
                    display_name_from_feed(s, feed);
                    num(s, "audioId", podcast_number(pending, "audioId"));
                    cJSON_DeleteItemFromObjectCaseSensitive(s, "episodes");
                    cJSON_AddItemToObject(s, "episodes",
                                          cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(feed, "episodes"), true));
                    num(s, "lastSuccessAt", podcast_now());
                }
                free_content_json(&content);
                free(path);
            }
            free(root);
            free(lib);
            cJSON_DeleteItemFromObjectCaseSensitive(s, "pending");
        }
        prune(s, true);
        const char *state = podcast_string(s, "state");
        if (yes(s, "enabled") && (!strcmp(state, "checking") || !strcmp(state, "downloading") ||
                                  !strcmp(state, "encoding") || !strcmp(state, "queued")))
            enqueue(s);
    }
}
static bool hydrate_prepared_names(void)
{
    bool changed = false;
    cJSON *s;
    cJSON_ArrayForEach(s, subscriptions())
    {
        if (*podcast_string(s, "displayName")) continue;
        const char *id = podcast_string(s, "id");
        const char *published = podcast_string(s, "publishedSource");
        char *prefix = custom_asprintf("lib://podcasts/%s/", id);
        size_t prefix_len = strlen(prefix);
        const char *filename = published + (strlen(published) < prefix_len ? strlen(published) : prefix_len);
        bool valid = !strncmp(published, prefix, prefix_len) && strlen(filename) == 12 &&
                     !strcmp(filename + 8, ".taf");
        for (size_t i = 0; valid && i < 8; i++)
            if (!isxdigit((unsigned char)filename[i])) valid = false;
        if (valid)
        {
            char *path = custom_asprintf("%s/podcasts/%s/%s.json", podcast_string(s, "library"), id, filename);
            cJSON *manifest = read_json(path);
            if (manifest && *podcast_string(manifest, "title") &&
                !strcmp(podcast_string(manifest, "sourceKey"), podcast_string(s, "sourceKey")))
            {
                display_name_from_feed(s, manifest);
                changed = true;
            }
            cJSON_Delete(manifest);
            free(path);
        }
        free(prefix);
    }
    return changed;
}
void podcast_init(void)
{
    if (!podcast_http_init())
        return;
    xmlInitParser();
    registry_path = custom_asprintf("%s/podcasts.json", get_settings()->internal.configdirfull);
    registry = read_json(registry_path);
    bool unsupported = registry && cJSON_IsObject(registry) && podcast_number(registry, "version") != 1 &&
                       podcast_number(registry, "version") != 2;
    if (!valid_registry(registry))
    {
        bool existed = access(registry_path, F_OK) == 0;
        cJSON_Delete(registry);
        registry = NULL;
        char *backup = custom_asprintf("%s.bak", registry_path);
        registry = unsupported ? NULL : read_json(backup);
        bool backup_existed = access(backup, F_OK) == 0;
        free(backup);
        if (!valid_registry(registry))
        {
            cJSON_Delete(registry);
            registry = cJSON_CreateObject();
            num(registry, "version", 2);
            num(registry, "lastAudioId", 0);
            cJSON_AddArrayToObject(registry, "subscriptions");
            cJSON_AddArrayToObject(registry, "pins");
            damaged = unsupported || existed || backup_existed;
        }
        else if (existed)
        {
            char *corrupt = custom_asprintf("%s.corrupt-%ld", registry_path, (long)podcast_now());
            if (rename(registry_path, corrupt))
                damaged = true;
            free(corrupt);
        }
    }
    available = !damaged;
    atomic_store(&stopping, false);
    cJSON *legacy = podcast_number(registry, "version") == 1 ? cJSON_Duplicate(registry, true) : NULL;
    recover();
    if (legacy && available)
    {
        char *backup = custom_asprintf("%s.v1.bak", registry_path);
        bool backed = access(backup, F_OK) == 0;
        if (backed)
        {
            cJSON *saved = read_json(backup);
            backed = valid_registry(saved) && podcast_number(saved, "version") == 1;
            cJSON_Delete(saved);
        }
        else
            backed = write_json(backup, legacy);
        free(backup);
        if (backed)
        {
            cJSON *s;
            cJSON_ArrayForEach(s, subscriptions())
            {
                str(s, "sourceType", "rss");
                podcast_trim_defaults(s);
                CURLU *u = curl_url();
                char *url = NULL;
                curl_url_set(u, CURLUPART_URL, podcast_string(s, "feedUrl"), 0);
                curl_url_get(u, CURLUPART_URL, &url, CURLU_NO_DEFAULT_PORT);
                char *key = custom_asprintf("rss:%s", url ? url : podcast_string(s, "feedUrl"));
                str(s, "sourceKey", key);
                free(key);
                curl_free(url);
                curl_url_cleanup(u);
                cJSON *ep;
                cJSON_ArrayForEach(ep, cJSON_GetObjectItemCaseSensitive(s, "episodes"))
                {
                    num(ep, "trimStartSeconds", 0);
                    num(ep, "trimEndSeconds", 0);
                }
            }
            num(registry, "version", 2);
            backed = valid_registry(registry) && persist();
        }
        if (!backed)
        {
            damaged = true;
            available = false;
        }
    }
    cJSON_Delete(legacy);
    if (available)
    {
        bool policy_changed = false;
        cJSON *s;
        cJSON_ArrayForEach(s, subscriptions())
            if (!yes(s, "refreshOnPlacement"))
            {
                boolean(s, "refreshOnPlacement", true);
                policy_changed = true;
            }
        if (policy_changed && !persist())
        {
            damaged = true;
            available = false;
        }
    }
    if (available && hydrate_prepared_names() && !persist())
    {
        damaged = true;
        available = false;
    }
    if (available)
    {
        if (pthread_create(&worker, NULL, work, NULL) == 0)
            started = true;
        else
            available = false;
    }
}
void podcast_shutdown(void)
{
    atomic_store(&stopping, true);
    atomic_store(&job_cancel, true);
    pthread_mutex_lock(&service_lock);
    available = false;
    pthread_cond_broadcast(&wake);
    pthread_mutex_unlock(&service_lock);
    if (started)
        pthread_join(worker, NULL);
    pthread_mutex_lock(&service_lock);
    while (active_requests)
        pthread_cond_wait(&wake, &service_lock);
    pthread_mutex_unlock(&service_lock);
    pthread_mutex_lock(&service_lock);
    persist();
    available = false;
    cJSON_Delete(registry);
    registry = NULL;
    pthread_mutex_unlock(&service_lock);
    free(registry_path);
    registry_path = NULL;
    started = false;
    for (unsigned i = 0; i < 2; i++)
    {
        cJSON_Delete(search_cache[i]);
        search_cache[i] = NULL;
        free(search_key[i]);
        search_key[i] = NULL;
    }
    podcast_http_cleanup();
    xmlCleanupParser();
}
void podcast_tick(void)
{
    pthread_mutex_lock(&service_lock);
    if (available)
    {
        cJSON *s;
        cJSON_ArrayForEach(s, subscriptions())
        {
            time_t due = podcast_number(s, "nextCheckAt");
            if (yes(s, "enabled") && due && due <= podcast_now() && strcmp(running_id, podcast_string(s, "id")))
                enqueue(s);
        }
    }
    pthread_mutex_unlock(&service_lock);
}
void podcast_placed(settings_t *settings, uint64_t uid)
{
    char ruid[17];
    for (unsigned i = 0; i < 8; i++)
        snprintf(ruid + 2 * i, 3, "%02x", (unsigned)((uid >> (8 * i)) & 255));
    char *root = realpath(settings->internal.contentdirfull, NULL);
    if (!root)
        return;
    pthread_mutex_lock(&service_lock);
    if (available)
    {
        cJSON *s;
        cJSON_ArrayForEach(s, subscriptions()) if (yes(s, "enabled") &&
                                                   !strcmp(root, podcast_string(s, "scope")) &&
                                                   !strcmp(ruid, podcast_string(s, "ruid")) &&
                                                   podcast_now() - podcast_number(s, "lastPlacement") >= 900)
        {
            if (enqueue(s))
                num(s, "lastPlacement", podcast_now());
            break;
        }
    }
    pthread_mutex_unlock(&service_lock);
    free(root);
}
void podcast_source_changed(const char *path, const char *source)
{
    pthread_mutex_lock(&service_lock);
    if (available)
    {
        cJSON *s;
        cJSON_ArrayForEach(s, subscriptions())
        {
            char *managed = content_path(podcast_string(s, "scope"), podcast_string(s, "ruid"));
            if (!strcmp(managed, path) && strcmp(podcast_string(s, "publishedSource"), source))
            {
                if (!strcmp(running_id, podcast_string(s, "id")))
                    atomic_store(&job_cancel, true);
                boolean(s, "enabled", false);
                boolean(s, "job", false);
                num(s, "revision", podcast_number(s, "revision") + 1);
                str(s, "state", "paused");
                persist();
            }
            free(managed);
        }
    }
    pthread_mutex_unlock(&service_lock);
}
static cJSON *discovery(const cJSON *body, int *status)
{
    const char *provider = podcast_string(body, "provider");
    if (*provider && strcmp(provider, "apple") && strcmp(provider, "ard_audiothek"))
        return failure(status, 400, "Unknown podcast search provider");
    unsigned index = !strcmp(provider, "ard_audiothek");
    const char *query = podcast_string(body, "q"), *country = podcast_string(body, "country");
    if (strlen(query) < 2 || strlen(query) > 100 || strlen(country) != 2 || !isalpha((unsigned char)country[0]) ||
        !isalpha((unsigned char)country[1]))
        return failure(status, 400, "Enter 2–100 search characters and a two-letter country code");
    char *key = custom_asprintf("%s:%s:%s", index ? "ard_audiothek" : "apple", country, query);
    pthread_mutex_lock(&discovery_lock);
    if (search_cache[index] && search_key[index] && !strcmp(key, search_key[index]) &&
        podcast_now() - search_time[index] < 600)
    {
        cJSON *r = cJSON_Duplicate(search_cache[index], true);
        pthread_mutex_unlock(&discovery_lock);
        free(key);
        return r;
    }
    if (podcast_now() - search_window[index] >= 60)
    {
        search_window[index] = podcast_now();
        search_requests[index] = 0;
    }
    if (search_requests[index] >= 20)
    {
        pthread_mutex_unlock(&discovery_lock);
        free(key);
        return failure(status, 429, "Search is busy; wait a minute or enter an RSS URL");
    }
    search_requests[index]++;
    char *escaped = curl_easy_escape(NULL, query, 0),
         *url =
             custom_asprintf("https://itunes.apple.com/search?media=podcast&entity=podcast&limit=20&country=%s&term=%s",
                             country, escaped);
    curl_free(escaped);
    char error[256];
    podcast_response_t response;
    cJSON *result = NULL;
    if (index)
        result = podcast_ard_search(query, &stopping, error, sizeof(error));
    else if (podcast_http_get(url, PODCAST_FEED_LIMIT, NULL, &stopping, &response, error, sizeof(error)))
    {
        cJSON *provider = response.data ? cJSON_Parse(response.data) : NULL;
        if (cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(provider, "results")))
        {
            result = cJSON_CreateObject();
            cJSON *results = cJSON_AddArrayToObject(result, "results"), *item;
            cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(
                                         provider, "results")) if (podcast_url_valid(podcast_string(item, "feedUrl")))
            {
                cJSON *entry = cJSON_CreateObject();
                str(entry, "sourceType", "rss");
                num(entry, "providerId", podcast_number(item, "collectionId"));
                str(entry, "title", podcast_string(item, "collectionName"));
                str(entry, "author", podcast_string(item, "artistName"));
                str(entry, "feedUrl", podcast_string(item, "feedUrl"));
                cJSON_AddItemToArray(results, entry);
            }
        }
        cJSON_Delete(provider);
        podcast_response_free(&response);
    }
    free(url);
    if (result)
    {
        cJSON_Delete(search_cache[index]);
        search_cache[index] = cJSON_Duplicate(result, true);
        free(search_key[index]);
        search_key[index] = key;
        key = NULL;
        search_time[index] = podcast_now();
    }
    pthread_mutex_unlock(&discovery_lock);
    free(key);
    return result ? result : failure(status, 502, "Podcast search is unavailable; enter an RSS URL instead");
}

static cJSON *request_impl(const char *action, const char *overlay, const char *ruid, const cJSON *body, int *status)
{
    *status = 200;
    if (!strcmp(action, "capabilities"))
    {
        cJSON *o = cJSON_CreateObject();
        boolean(o, "supported", available);
        num(o, "maxEpisodes", 50);
        num(o, "maxIntervalHours", 168);
        num(o, "maxBodyBytes", 65536);
        num(o, "maxRequestBytes", 65536);
        num(o, "maxTrimSeconds", 21600);
        num(o, "maxEpisodeTrims", 50);
        boolean(o, "trimSupported", true);
        boolean(o, "displayNameSupported", true);
        boolean(o, "placementRefreshAlways", true);
        boolean(o, "feedInspectionSupported", true);
        cJSON *types = cJSON_AddArrayToObject(o, "sourceTypes");
        cJSON_AddItemToArray(types, cJSON_CreateString("rss"));
        cJSON_AddItemToArray(types, cJSON_CreateString("ard_audiothek"));
        num(o, "maxDurationHours", 6);
        if (damaged)
            str(o, "message",
                "Podcast registry needs repair or migration could not be saved; check storage and backups");
        return o;
    }
    if (!available)
        return failure(status, 503, "Podcast service is unavailable; check registry and storage");
    if (!strcmp(action, "search"))
        return discovery(body, status);
    if (!strcmp(action, "feed-info"))
    {
        const char *url = podcast_string(body, "feedUrl");
        if (!podcast_url_valid(url))
            return failure(status, 400, "Expected a public RSS feed URL");
        podcast_response_t response;
        char error[256];
        if (!podcast_http_get_timed(url, PODCAST_FEED_LIMIT, NULL, &stopping, &response, error, sizeof(error), 5000))
            return failure(status, 502, error);
        cJSON *o = cJSON_CreateObject();
        boolean(o, "videoOnly", podcast_feed_video_only(response.data, response.length, response.final_url));
        podcast_response_free(&response);
        return o;
    }
    if (!strcmp(action, "preview"))
    {
        char error[256];
        cJSON *r = preview(body, &stopping, error, sizeof(error));
        if (r && cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(body, "episodeTrims")) &&
            strcmp(podcast_string(body, "episodeTrimSourceKey"), podcast_string(r, "sourceKey")))
        {
            cJSON_Delete(r);
            cJSON *conflict =
                failure(status, 409, "Episode overrides belong to a different source; clear them explicitly");
            str(cJSON_GetObjectItemCaseSensitive(conflict, "error"), "code", "trim_source_mismatch");
            return conflict;
        }
        return r ? r : failure(status, 502, error);
    }
    char *root = NULL, *library = NULL;
    if (!scope(overlay, &root, &library))
        return failure(status, 404, "Unknown overlay or missing storage");
    const char *requested = ruid && *ruid ? ruid : podcast_string(body, "ruid");
    char normalized[17] = "";
    if (*requested && hex(requested, 16))
    {
        snprintf(normalized, sizeof(normalized), "%s", requested);
        for (char *p = normalized; *p; p++)
            *p = tolower((unsigned char)*p);
    }
    else if (*requested)
    {
        free(root);
        free(library);
        return failure(status, 400, "Expected a 16-character hexadecimal rUID");
    }
    bool save = !strcmp(action, "subscriptions/save"), detach = !strcmp(action, "subscriptions/detach"),
         refresh = !strcmp(action, "subscriptions/refresh");
    char error[256];
    if (save && (!*normalized || !podcast_config_valid(body, error, sizeof(error))))
    {
        free(root);
        free(library);
        return failure(status, 400, *normalized ? error : "Choose a Tonie rUID");
    }
    const char *id = podcast_string(body, "id");
    if ((detach || refresh || (save && *id)) && !hex(id, 32))
    {
        free(root);
        free(library);
        return failure(status, 400, "Invalid subscription ID");
    }
    char *path = *normalized ? content_path(root, normalized) : NULL;
    /* Detach needs its content path before acquiring the locks in order. */
    if (detach)
    {
        pthread_mutex_lock(&service_lock);
        cJSON *s = find(id);
        if (s && !strcmp(root, podcast_string(s, "scope")))
            path = content_path(root, podcast_string(s, "ruid"));
        pthread_mutex_unlock(&service_lock);
    }
    char *assigned_source = NULL;
    if (path)
    {
        podcast_content_lock(path);
        contentJson_t content;
        if (load_content_json(path, &content, false, get_settings_ovl(overlay ? overlay : "")) == NO_ERROR)
            assigned_source = strdup(content.source ? content.source : "");
        free_content_json(&content);
    }
    pthread_mutex_lock(&service_lock);
    cJSON *result = NULL, *s = *id ? find(id) : NULL, *backup = NULL;
    if (!strcmp(action, "subscriptions"))
    {
        result = cJSON_CreateObject();
        cJSON *list = cJSON_AddArrayToObject(result, "subscriptions"), *entry;
        cJSON_ArrayForEach(entry,
                           subscriptions()) if (!strcmp(root, podcast_string(entry, "scope")) &&
                                                (!*normalized || !strcmp(normalized, podcast_string(entry, "ruid"))))
            cJSON_AddItemToArray(list, public_subscription(entry, assigned_source));
        goto out;
    }
    if (!save && !detach && !refresh)
    {
        result = failure(status, 404, "Unknown podcast operation");
        goto out;
    }
    if ((*id && !s) || (s && strcmp(root, podcast_string(s, "scope"))))
    {
        result = failure(status, 404, "Subscription not found in this overlay");
        goto out;
    }
    if (s && (save || detach) &&
        (!cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(body, "expectedRevision")) ||
         podcast_number(body, "expectedRevision") != podcast_number(s, "revision")))
    {
        result = failure(status, 409, "Subscription changed; reopen the editor before saving");
        goto out;
    }
    if (s && strcmp(library, podcast_string(s, "library")))
    {
        result = failure(status, 409, "Overlays sharing podcast content must use the same library directory");
        goto out;
    }
    if (s && save && strcmp(normalized, podcast_string(s, "ruid")))
    {
        result = failure(status, 409, "Subscription belongs to another Tonie");
        goto out;
    }
    if ((save || refresh) && !yes(s ? s : (cJSON *)body, "enabled"))
    {
        if (refresh)
        {
            result = failure(status, 409, "Enable and save the subscription before refreshing");
            goto out;
        }
    }
    if ((save && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(body, "enabled"))) || refresh)
    {
        if (queued_count() >= 32 && (!s || !yes(s, "job")))
        {
            result = failure(status, 429, "Podcast queue is full; try again later");
            goto out;
        }
    }
    backup = cJSON_Duplicate(registry, true);
    if (s && (save || detach) && !strcmp(running_id, id))
        atomic_store(&job_cancel, true);
    if (save)
    {
        if (!s)
        {
            cJSON *entry;
            cJSON_ArrayForEach(entry, subscriptions()) if (!strcmp(root, podcast_string(entry, "scope")) &&
                                                           !strcmp(normalized, podcast_string(entry, "ruid")))
            {
                result = failure(status, 409, "This Tonie already has a subscription");
                goto out;
            }
            if (cJSON_GetArraySize(subscriptions()) >= 128)
            {
                result = failure(status, 429, "Subscription limit reached");
                goto out;
            }
            unsigned char random[16];
            int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
            ssize_t got = fd >= 0 ? read(fd, random, sizeof(random)) : -1;
            if (fd >= 0)
                close(fd);
            if (got != (ssize_t)sizeof(random))
            {
                result = failure(status, 503, "Cannot allocate subscription identity");
                goto out;
            }
            char new_id[33];
            for (unsigned i = 0; i < 16; i++)
                snprintf(new_id + i * 2, 3, "%02x", random[i]);
            s = cJSON_CreateObject();
            str(s, "id", new_id);
            str(s, "ruid", normalized);
            str(s, "scope", root);
            str(s, "library", library);
            str(s, "overlay", overlay ? overlay : "");
            num(s, "createdAt", podcast_now());
            contentJson_t content;
            load_content_json(path, &content, true, get_settings_ovl(overlay ? overlay : ""));
            cJSON *original = cJSON_AddObjectToObject(s, "original");
            str(original, "source", content.source);
            if (content_json_source_kind(&content))
                cJSON_AddItemToObject(original, "sourceAssignment", cJSON_Duplicate(content.source_assignment, true));
            boolean(original, "live", content.live);
            boolean(original, "nocloud", content.nocloud);
            boolean(original, "cloud_override", content.cloud_override);
            free_content_json(&content);
            cJSON_AddArrayToObject(s, "episodes");
            cJSON_AddItemToArray(subscriptions(), s);
            *status = 201;
        }
        const char *keys[] = {"feedUrl",
                              "sourceType",
                              "sourceKey",
                              "ardShowUrl",
                              "ardShowId",
                              "trimStartSeconds",
                              "trimEndSeconds",
                              "episodeTrims",
                              "episodeTrimSourceKey",
                              "episodeCount",
                              "order",
                              "intervalHours",
                              "refreshOnPlacement",
                              "displayName",
                              "displayNameSourceKey",
                              "enabled",
                              NULL};
        for (unsigned i = 0; keys[i]; i++)
        {
            cJSON_DeleteItemFromObjectCaseSensitive(s, keys[i]);
            cJSON_AddItemToObject(s, keys[i], cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(body, keys[i]), true));
        }
        str(s, "overlay", overlay ? overlay : "");
        num(s, "revision", podcast_number(s, "revision") + 1);
        str(s, "state", yes(s, "enabled") ? "queued" : "paused");
        boolean(s, "job", false);
        str(s, "lastError", "");
        if (yes(s, "enabled"))
            enqueue(s);
        result = cJSON_CreateObject();
        cJSON_AddItemToObject(result, "subscription", public_subscription(s, assigned_source));
    }
    else if (refresh)
    {
        if (strcmp(running_id, id))
            enqueue(s);
        *status = 202;
        result = cJSON_CreateObject();
        str(result, "jobId", id);
    }
    else
    {
        if (!path)
        {
            result = failure(status, 404, "Subscription not found");
            goto out;
        }
        contentJson_t content;
        load_content_json(path, &content, true, get_settings_ovl(overlay ? overlay : ""));
        if (content.source && !strcmp(content.source, podcast_string(s, "publishedSource")))
        {
            cJSON *original = cJSON_GetObjectItemCaseSensitive(s, "original");
            free(content.source);
            content.source = strdup(podcast_string(original, "source"));
            cJSON_Delete(content.source_assignment);
            content.source_assignment = cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(original, "sourceAssignment"), true);
            content.live = yes(original, "live");
            content.nocloud = yes(original, "nocloud");
            content.cloud_override = yes(original, "cloud_override");
            char *json_path = custom_asprintf("%s.json", path);
            error_t e = save_content_json(json_path, &content);
            free(json_path);
            if (e != NO_ERROR)
            {
                free_content_json(&content);
                result = failure(status, 503, "Cannot restore original audio assignment");
                goto out;
            }
        }
        free_content_json(&content);
        cJSON *removed = cJSON_DetachItemViaPointer(subscriptions(), s);
        cJSON_Delete(removed);
        s = NULL;
        result = cJSON_CreateObject();
        boolean(result, "detached", true);
    }
    if (!persist())
    {
        cJSON_Delete(registry);
        registry = backup;
        backup = NULL;
        cJSON_Delete(result);
        result = failure(status, 503, "Cannot persist subscription; check storage permissions/free space");
    }
    else if (s)
    {
        str(result, "jobId", podcast_string(s, "id"));
    }
out:
    cJSON_Delete(backup);
    pthread_mutex_unlock(&service_lock);
    if (path)
        podcast_content_unlock(path);
    free(assigned_source);
    free(path);
    free(root);
    free(library);
    return result;
}

cJSON *podcast_request(const char *action, const char *overlay, const char *ruid, const cJSON *body, int *status)
{
    pthread_mutex_lock(&service_lock);
    if (!available && strcmp(action, "capabilities"))
    {
        pthread_mutex_unlock(&service_lock);
        return failure(status, 503, "Podcast service is unavailable");
    }
    active_requests++;
    pthread_mutex_unlock(&service_lock);
    cJSON *normalized = NULL, *result = NULL, *prior = NULL;
    bool config = !strcmp(action, "preview") || !strcmp(action, "subscriptions/save");
    if (config)
    {
        *status = 200;
        if (!strcmp(action, "subscriptions/save"))
        {
            char *root = NULL, *library = NULL;
            if (!scope(overlay, &root, &library))
                result = failure(status, 404, "Unknown overlay or missing storage");
            else if (*podcast_string(body, "id") && !hex(podcast_string(body, "id"), 32))
                result = failure(status, 400, "Invalid subscription ID");
            else if (*podcast_string(body, "id"))
            {
                pthread_mutex_lock(&service_lock);
                cJSON *existing = find(podcast_string(body, "id"));
                if (!existing || strcmp(root, podcast_string(existing, "scope")))
                    result = failure(status, 404, "Subscription not found in this overlay");
                else if (podcast_number(body, "expectedRevision") != podcast_number(existing, "revision"))
                    result = failure(status, 409, "Subscription changed; reopen the editor");
                else
                    prior = cJSON_Duplicate(existing, true);
                pthread_mutex_unlock(&service_lock);
            }
            free(root);
            free(library);
        }
        if (!result)
        {
            normalized = normalize_config(body, prior, !strcmp(action, "preview"), status);
            if (*status != 200)
            {
                result = normalized;
                normalized = NULL;
            }
        }
    }
    if (!result)
        result = request_impl(action, overlay, ruid, normalized ? normalized : body, status);
    cJSON_Delete(prior);
    cJSON_Delete(normalized);
    pthread_mutex_lock(&service_lock);
    active_requests--;
    pthread_cond_broadcast(&wake);
    pthread_mutex_unlock(&service_lock);
    return result;
}

char *podcast_download_path(settings_t *settings, const char *box, const char *ruid, const char *source, size_t offset)
{
    if (!source || strncmp(source, "lib://podcasts/", 15))
        return NULL;
    char *root = realpath(settings->internal.contentdirfull, NULL), *path = NULL;
    if (!root)
        return strdup("");
    pthread_mutex_lock(&service_lock);
    if (!registry || !available)
    {
        pthread_mutex_unlock(&service_lock);
        free(root);
        return offset ? strdup("") : NULL;
    }
    cJSON *s = NULL, *entry;
    cJSON_ArrayForEach(entry, subscriptions()) if (!strcmp(root, podcast_string(entry, "scope")) &&
                                                   !strcasecmp(ruid, podcast_string(entry, "ruid")))
    {
        s = entry;
        break;
    }
    /* Detached sources remain ordinary local audio, but retain existing pins. */
    cJSON *pins = cJSON_GetObjectItemCaseSensitive(registry, "pins"), *pin = NULL;
    if (box && *box)
        cJSON_ArrayForEach(entry, pins) if (!strcmp(box, podcast_string(entry, "box")) &&
                                            !strcmp(root, podcast_string(entry, "scope")) &&
                                            !strcasecmp(ruid, podcast_string(entry, "ruid")))
        {
            pin = entry;
            break;
        }
    const char *selected = source;
    if (offset)
    {
        if (!pin || podcast_now() - podcast_number(pin, "createdAt") > 86400)
            selected = NULL;
        else
            selected = podcast_string(pin, "source");
    }
    else if (box && *box)
    {
        if (!pin && cJSON_GetArraySize(pins) < 2048)
        {
            pin = cJSON_CreateObject();
            cJSON_AddItemToArray(pins, pin);
        }
        if (pin)
        {
            str(pin, "box", box);
            str(pin, "scope", root);
            str(pin, "ruid", ruid);
            str(pin, "source", source);
            num(pin, "createdAt", podcast_now());
            if (!persist())
                selected = NULL;
        }
        else
            selected = NULL;
    }
    if (selected && !strncmp(selected, "lib://podcasts/", 15) && !strstr(selected, ".."))
    {
        const char *library = s ? podcast_string(s, "library") : settings->internal.librarydirfull;
        path = custom_asprintf("%s/%s", library, selected + 6);
        if (access(path, R_OK))
        {
            free(path);
            path = NULL;
        }
    }
    pthread_mutex_unlock(&service_lock);
    free(root);
    return path ? path : strdup("");
}
#else
void podcast_init(void)
{
}
void podcast_shutdown(void)
{
}
void podcast_tick(void)
{
}
void podcast_placed(settings_t *settings, uint64_t uid)
{
    (void)settings;
    (void)uid;
}
void podcast_content_lock(const char *path)
{
    (void)path;
    mutex_lock(MUTEX_CONTENT_JSON);
}
void podcast_content_unlock(const char *path)
{
    (void)path;
    mutex_unlock(MUTEX_CONTENT_JSON);
}
void podcast_source_changed(const char *path, const char *source)
{
    (void)path;
    (void)source;
}
char *podcast_download_path(settings_t *settings, const char *box, const char *ruid, const char *source, size_t offset)
{
    (void)settings;
    (void)box;
    (void)ruid;
    (void)source;
    (void)offset;
    return NULL;
}
cJSON *podcast_request(const char *action, const char *overlay, const char *ruid, const cJSON *body, int *status)
{
    (void)overlay;
    (void)ruid;
    (void)body;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "supported", false);
    *status = !strcmp(action, "capabilities") ? 200 : 503;
    return o;
}
#endif
