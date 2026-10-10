#include "source_profiles.h"
#include "contentJson.h"
#include "handler_api.h"
#include "podcast.h"
#include "server_helpers.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static cJSON *problem(int *status, int code, const char *message)
{
    *status = code;
    cJSON *result = cJSON_CreateObject();
    cJSON *error = cJSON_AddObjectToObject(result, "error");
    cJSON_AddStringToObject(error, "code", code == 409 ? "conflict" : code == 404 ? "not_found" :
                                          code == 503 ? "unavailable" : "invalid_request");
    cJSON_AddStringToObject(error, "message", message);
    return result;
}

static bool bounded_text(const cJSON *value, size_t max)
{
    if (!cJSON_IsString(value)) return false;
    const unsigned char *p = (const unsigned char *)value->valuestring;
    size_t size = strlen((const char *)p);
    if (size > max) return false;
    for (size_t i = 0; i < size; i++)
        if (p[i] < 32 || p[i] == 127) return false;
    return true;
}

static bool whole_number(const cJSON *value, double min, double max)
{
    return cJSON_IsNumber(value) && isfinite(value->valuedouble) &&
           value->valuedouble >= min && value->valuedouble <= max &&
           floor(value->valuedouble) == value->valuedouble;
}

static bool one_of(const char *name, const char *const *names)
{
    for (size_t i = 0; names[i]; i++)
        if (!strcmp(name, names[i])) return true;
    return false;
}

static bool valid_episode_trims(const cJSON *value)
{
    if (!cJSON_IsArray(value) || cJSON_GetArraySize(value) > 50) return false;
    const char *const fields[] = {"identity", "trimStartSeconds", "trimEndSeconds", NULL};
    cJSON *item;
    cJSON_ArrayForEach(item, value)
    {
        const cJSON *identity = cJSON_GetObjectItemCaseSensitive(item, "identity");
        if (!cJSON_IsObject(item) || !bounded_text(identity, 512) || !*identity->valuestring ||
            !whole_number(cJSON_GetObjectItemCaseSensitive(item, "trimStartSeconds"), 0, 21600) ||
            !whole_number(cJSON_GetObjectItemCaseSensitive(item, "trimEndSeconds"), 0, 21600)) return false;
        for (cJSON *prior = value->child; prior != item; prior = prior->next)
            if (!strcmp(identity->valuestring,
                        cJSON_IsString(cJSON_GetObjectItemCaseSensitive(prior, "identity")) ?
                        cJSON_GetObjectItemCaseSensitive(prior, "identity")->valuestring : "")) return false;
        for (cJSON *field = item->child; field; field = field->next)
            if (!one_of(field->string, fields)) return false;
    }
    return true;
}

static bool valid_profile(const char *kind, const cJSON *profile)
{
    if (!cJSON_IsObject(profile)) return false;
    const char *const audio[] = {"source", NULL};
    const char *const radio[] = {"source", "displayName", NULL};
    const char *const podcast[] = {"sourceType", "feedUrl", "ardShowUrl", "sourceKey", "displayName",
                                   "displayNameSourceKey", "episodeCount", "order", "intervalHours", "enabled",
                                   "refreshOnPlacement", "trimStartSeconds", "trimEndSeconds", "episodeTrimSourceKey",
                                   "episodeTrims", NULL};
    const char *const *allowed = !strcmp(kind, "audio") ? audio : !strcmp(kind, "radio") ? radio : podcast;
    for (const cJSON *field = profile->child; field; field = field->next)
    {
        const char *key = field->string;
        if (!one_of(key, allowed)) return false;
        if (!strcmp(key, "source") || !strcmp(key, "feedUrl") || !strcmp(key, "ardShowUrl"))
        {
            if (!bounded_text(field, !strcmp(key, "source") ? 4096 : 2048)) return false;
        }
        else if (!strcmp(key, "displayName"))
        {
            if (!bounded_text(field, 512)) return false;
        }
        else if (!strcmp(key, "displayNameSourceKey") || !strcmp(key, "sourceKey") ||
                 !strcmp(key, "episodeTrimSourceKey"))
        {
            if (!bounded_text(field, 2052)) return false;
        }
        else if (!strcmp(key, "sourceType"))
        {
            if (!cJSON_IsString(field) || strcmp(field->valuestring,
                !strcmp(kind, "podcast") ? "rss" : "ard_audiothek")) return false;
        }
        else if (!strcmp(key, "episodeCount"))
        {
            if (!whole_number(field, 1, 50)) return false;
        }
        else if (!strcmp(key, "intervalHours"))
        {
            if (!whole_number(field, 0, 168)) return false;
        }
        else if (!strcmp(key, "trimStartSeconds") || !strcmp(key, "trimEndSeconds"))
        {
            if (!whole_number(field, 0, 21600)) return false;
        }
        else if (!strcmp(key, "order"))
        {
            if (!cJSON_IsString(field) || (strcmp(field->valuestring, "newest_first") &&
                                           strcmp(field->valuestring, "oldest_first"))) return false;
        }
        else if (!strcmp(key, "enabled") || !strcmp(key, "refreshOnPlacement"))
        {
            if (!cJSON_IsBool(field)) return false;
        }
        else if (!strcmp(key, "episodeTrims") && !valid_episode_trims(field)) return false;
    }
    return true;
}

static cJSON *snapshot(const contentJson_t *content)
{
    cJSON *response = cJSON_CreateObject();
    if (content->source_profiles_version)
        cJSON_AddItemToObject(response, "version", cJSON_Duplicate(content->source_profiles_version, true));
    cJSON *revision = content->source_profiles_revision;
    cJSON_AddNumberToObject(response, "revision", whole_number(revision, 0, 2147483647) ? revision->valuedouble : 0);
    cJSON_AddItemToObject(response, "profiles", cJSON_IsObject(content->source_profiles) ?
                          cJSON_Duplicate(content->source_profiles, true) : cJSON_CreateObject());
    return response;
}

cJSON *source_profiles_request(const char *action, const char *overlay, const char *ruid,
                               const cJSON *body, int *status)
{
    if (!ruid || strlen(ruid) != 16) return problem(status, 400, "Use a 16-digit hexadecimal rUID");
    for (size_t i = 0; i < 16; i++)
        if (!isxdigit((unsigned char)ruid[i])) return problem(status, 400, "Use a 16-digit hexadecimal rUID");
    char *path = NULL;
    settings_t *settings = NULL;
    if (!resolveSourceProfileContent(overlay, ruid, &path, &settings))
        return problem(status, 404, "Unknown overlay or content scope");
    bool save = !strcmp(action, "save");
    if (save)
    {
        const char *const request_fields[] = {"ruid", "kind", "expectedRevision", "profile", NULL};
        const cJSON *revision = cJSON_GetObjectItemCaseSensitive(body, "expectedRevision");
        const cJSON *kind = cJSON_GetObjectItemCaseSensitive(body, "kind");
        const cJSON *profile = cJSON_GetObjectItemCaseSensitive(body, "profile");
        bool fields_valid = true;
        for (const cJSON *field = body->child; field; field = field->next)
            if (!one_of(field->string, request_fields)) fields_valid = false;
        if (!fields_valid || !whole_number(revision, 0, 2147483646) || !cJSON_IsString(kind) ||
            (strcmp(kind->valuestring, "audio") && strcmp(kind->valuestring, "radio") &&
             strcmp(kind->valuestring, "podcast") && strcmp(kind->valuestring, "ard")) ||
            !valid_profile(kind->valuestring, profile))
        {
            free(path);
            return problem(status, 400, "Invalid source profile or revision");
        }
    }
    podcast_content_lock(path);
    contentJson_t content;
    error_t error = load_content_json(path, &content, save, settings);
    cJSON *result = NULL;
    if (error != NO_ERROR && (save || error != ERROR_FILE_NOT_FOUND))
        result = problem(status, 503, "Cannot load content metadata");
    else if (save && content.source_profiles_version &&
             !whole_number(content.source_profiles_version, 1, 1))
        result = problem(status, 409, "Unknown source profile version");
    else if (save)
    {
        double current = whole_number(content.source_profiles_revision, 0, 2147483646) ?
                         content.source_profiles_revision->valuedouble : 0;
        double expected = cJSON_GetObjectItemCaseSensitive(body, "expectedRevision")->valuedouble;
        if (current != expected)
            result = problem(status, 409, "Source profiles changed; reload before saving");
        else
        {
            const cJSON *kind = cJSON_GetObjectItemCaseSensitive(body, "kind");
            const cJSON *profile = cJSON_GetObjectItemCaseSensitive(body, "profile");
            if (!content.source_profiles || !cJSON_IsObject(content.source_profiles))
            {
                cJSON_Delete(content.source_profiles);
                content.source_profiles = cJSON_CreateObject();
            }
            cJSON_DeleteItemFromObjectCaseSensitive(content.source_profiles, kind->valuestring);
            cJSON_AddItemToObject(content.source_profiles, kind->valuestring, cJSON_Duplicate(profile, true));
            cJSON_Delete(content.source_profiles_version);
            cJSON_Delete(content.source_profiles_revision);
            content.source_profiles_version = cJSON_CreateNumber(1);
            content.source_profiles_revision = cJSON_CreateNumber(current + 1);
            char *json_path = custom_asprintf("%s.json", path);
            error = save_content_json(json_path, &content);
            free(json_path);
            if (error == NO_ERROR) result = snapshot(&content);
            else result = problem(status, 503, "Cannot save source profiles");
        }
    }
    else result = snapshot(&content);
    free_content_json(&content);
    podcast_content_unlock(path);
    free(path);
    return result;
}
