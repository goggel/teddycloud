#if PODCAST_SUPPORT
#include "podcast_trim.h"
#include "podcast_feed.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

bool podcast_trim_frames(double seconds, uint64_t *frames)
{
    if (!isfinite(seconds) || seconds < 0 || seconds > PODCAST_MAX_TRIM)
        return false;
    *frames = (uint64_t)llround(seconds * PODCAST_SAMPLE_RATE);
    return true;
}
bool podcast_trim_window(uint64_t total, double start, double end, uint64_t *first, uint64_t *last)
{
    uint64_t a, b;
    if (!podcast_trim_frames(start, &a) || !podcast_trim_frames(end, &b))
        return false;
    if (b >= total || a >= total - b)
        return false;
    *first = a;
    *last = total - b;
    return true;
}
uint64_t podcast_trim_overlap(uint64_t offset, uint64_t count, uint64_t first, uint64_t last, uint64_t *skip)
{
    *skip = 0;
    if (count > UINT64_MAX - offset || last <= first || offset >= last || offset + count <= first)
        return 0;
    uint64_t a = offset > first ? offset : first, b = offset + count < last ? offset + count : last;
    *skip = a - offset;
    return b - a;
}
static bool seconds(const cJSON *o, const char *key, bool optional)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return !v ? optional
              : cJSON_IsNumber(v) && isfinite(v->valuedouble) && v->valuedouble >= 0 &&
                    v->valuedouble <= PODCAST_MAX_TRIM;
}
bool podcast_trim_valid(const cJSON *config, char *error, size_t size)
{
    cJSON *overrides = cJSON_GetObjectItemCaseSensitive(config, "episodeTrims"),
          *key = cJSON_GetObjectItemCaseSensitive(config, "episodeTrimSourceKey");
    if (!seconds(config, "trimStartSeconds", true) || !seconds(config, "trimEndSeconds", true) ||
        (key && (!cJSON_IsString(key) || strlen(key->valuestring) > 2052)) ||
        (overrides && (!cJSON_IsArray(overrides) || cJSON_GetArraySize(overrides) > 50)))
        goto fail;
    cJSON *item;
    cJSON_ArrayForEach(item, overrides)
    {
        const char *id = podcast_string(item, "identity");
        if (!cJSON_IsObject(item) || !*id || strlen(id) > 512 || !seconds(item, "trimStartSeconds", false) ||
            !seconds(item, "trimEndSeconds", false))
            goto fail;
        for (const unsigned char *p = (const unsigned char *)id; *p; p++)
            if (*p < 32 || *p == 127)
                goto fail;
        for (cJSON *prior = overrides->child; prior != item; prior = prior->next)
            if (!strcmp(id, podcast_string(prior, "identity")))
                goto fail;
    }
    return true;
fail:
    snprintf(error, size, "Cuts must be finite seconds from 0 to 21600; use at most 50 unique episode overrides");
    return false;
}
void podcast_trim_defaults(cJSON *config)
{
    if (!cJSON_GetObjectItemCaseSensitive(config, "trimStartSeconds"))
        cJSON_AddNumberToObject(config, "trimStartSeconds", 0);
    if (!cJSON_GetObjectItemCaseSensitive(config, "trimEndSeconds"))
        cJSON_AddNumberToObject(config, "trimEndSeconds", 0);
    if (!cJSON_GetObjectItemCaseSensitive(config, "episodeTrims"))
        cJSON_AddArrayToObject(config, "episodeTrims");
    if (!cJSON_GetObjectItemCaseSensitive(config, "episodeTrimSourceKey"))
        cJSON_AddStringToObject(config, "episodeTrimSourceKey", "");
}
void podcast_trim_effective(const cJSON *config, const char *identity, double *start, double *end)
{
    const cJSON *selected = config;
    cJSON *item;
    cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(
                                 config, "episodeTrims")) if (!strcmp(identity, podcast_string(item, "identity")))
    {
        selected = item;
        break;
    }
    *start = podcast_number(selected, "trimStartSeconds");
    *end = podcast_number(selected, "trimEndSeconds");
}
void podcast_trim_preview(cJSON *preview, const cJSON *config)
{
    cJSON *ep, *warnings = cJSON_GetObjectItemCaseSensitive(preview, "warnings");
    cJSON_ArrayForEach(ep, cJSON_GetObjectItemCaseSensitive(preview, "episodes"))
    {
        double start, end;
        podcast_trim_effective(config, podcast_string(ep, "identity"), &start, &end);
        cJSON_DeleteItemFromObjectCaseSensitive(ep, "trimStartSeconds");
        cJSON_DeleteItemFromObjectCaseSensitive(ep, "trimEndSeconds");
        cJSON_AddNumberToObject(ep, "trimStartSeconds", start);
        cJSON_AddNumberToObject(ep, "trimEndSeconds", end);
        double duration = podcast_number(ep, "durationSeconds");
        if (duration > 0)
        {
            cJSON_AddNumberToObject(ep, "estimatedTrimmedDurationSeconds", fmax(0, duration - start - end));
            if (duration <= start + end)
                cJSON_AddItemToArray(
                    warnings,
                    cJSON_CreateString(
                        "Estimated episode duration is shorter than its cuts; preparation will check actual audio"));
        }
    }
}
#endif
