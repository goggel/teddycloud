#include "contentJson.h"

#include "settings.h"
#include "debug.h"
#include "cJSON.h"
#include "os_port.h"
#include "net_config.h"
#include "server_helpers.h"
#include "toniesJson.h"
#include "handler.h"
#include "json_helper.h"
#include "podcast.h"

error_t load_content_json(const char *content_path, contentJson_t *content_json, bool create_if_missing, settings_t *settings)
{
    char *jsonPath = custom_asprintf("%s.json", content_path);
    error_t error = NO_ERROR;
    osMemset(content_json, 0, sizeof(contentJson_t));
    content_json->live = false;
    content_json->nocloud = false;
    content_json->source = NULL;
    content_json->skip_seconds = 0;
    content_json->cache = false;
    content_json->_updated = false;
    content_json->_source_type = CT_SOURCE_NONE;
    content_json->_streamFile = custom_asprintf("%s.stream", content_path);
    content_json->cloud_ruid = NULL;
    content_json->cloud_auth = NULL;
    content_json->cloud_auth_len = 0;
    content_json->cloud_override = false;
    content_json->_has_cloud_auth = false;
    content_json->tonie_model = NULL;
    content_json->_source_model = NULL;
    content_json->hide = false;
    content_json->claimed = false;
    content_json->_valid = false;
    content_json->_create_if_missing = create_if_missing;

    osMemset(&content_json->_tap, 0, sizeof(tonie_audio_playlist_t));

    if (fsFileExists(jsonPath))
    {
        size_t fileSize = 0;
        error = fsGetFileSize(jsonPath, (uint32_t *)(&fileSize));

        FsFile *fsFile = error == NO_ERROR ? fsOpenFile(jsonPath, FS_FILE_MODE_READ) : NULL;
        if (fsFile != NULL)
        {
            size_t sizeRead;
            char *data = osAllocMem(fileSize);
            size_t pos = 0;

            while (pos < fileSize)
            {
                if (fsReadFile(fsFile, &data[pos], fileSize - pos, &sizeRead) != NO_ERROR || sizeRead == 0)
                {
                    error = ERROR_INVALID_FILE;
                    break;
                }
                pos += sizeRead;
            }
            fsCloseFile(fsFile);

            cJSON *contentJson = error == NO_ERROR && pos == fileSize ?
                                 cJSON_ParseWithLengthOpts(data, fileSize, 0, 0) : NULL;
            osFreeMem(data);
            if (contentJson == NULL)
            {
                const char *error_ptr = cJSON_GetErrorPtr();
                TRACE_ERROR("Json parse error\r\n");
                if (error_ptr != NULL)
                {
                    // TRACE_ERROR(" before: %s\r\n", error_ptr); //==194402==ERROR: AddressSanitizer: heap-use-after-free on address 0x6020000bd2d0 at pc 0x555555ac8ba9 bp 0x7ffff2ffb490 sp 0x7ffff2ffac08
                }
                error = ERROR_INVALID_FILE;
            }
            else
            {
                content_json->_persisted_baseline = cJSON_Duplicate(contentJson, true);
                if (!content_json->_persisted_baseline) error = ERROR_OUT_OF_MEMORY;
                content_json->live = jsonGetBool(contentJson, "live");
                content_json->nocloud = jsonGetBool(contentJson, "nocloud");
                content_json->source = jsonGetString(contentJson, "source");
                content_json->_source_resolved = jsonGetString(contentJson, "source");
                content_json->skip_seconds = jsonGetUInt32(contentJson, "skip_seconds");
                content_json->cache = jsonGetBool(contentJson, "cache");
                content_json->cloud_ruid = jsonGetString(contentJson, "cloud_ruid");
                content_json->cloud_auth = jsonGetBytes(contentJson, "cloud_auth", &content_json->cloud_auth_len);
                content_json->cloud_override = jsonGetBool(contentJson, "cloud_override");
                content_json->tonie_model = jsonGetString(contentJson, "tonie_model");
                content_json->hide = jsonGetBool(contentJson, "hide");
                content_json->claimed = jsonGetBool(contentJson, "claimed");
                cJSON *profiles_version = cJSON_GetObjectItemCaseSensitive(contentJson, "sourceProfilesVersion");
                cJSON *profiles_revision = cJSON_GetObjectItemCaseSensitive(contentJson, "sourceProfilesRevision");
                cJSON *profiles = cJSON_GetObjectItemCaseSensitive(contentJson, "sourceProfiles");
                if (profiles_version) content_json->source_profiles_version = cJSON_Duplicate(profiles_version, true);
                if (profiles_revision) content_json->source_profiles_revision = cJSON_Duplicate(profiles_revision, true);
                if (profiles) content_json->source_profiles = cJSON_Duplicate(profiles, true);
                cJSON *assignment = cJSON_GetObjectItemCaseSensitive(contentJson, "sourceAssignment");
                if (assignment) content_json->source_assignment = cJSON_Duplicate(assignment, true);

                // TODO: use checkCustomTonie to validate
                // TODO validate rUID
                if (osStrlen(content_json->cloud_ruid) == 16 && content_json->cloud_auth_len == TONIE_AUTH_TOKEN_LENGTH)
                {
                    content_json->_has_cloud_auth = true;
                }
                else
                {
                    content_json->cloud_override = false;
                }

                if (osStrlen(content_json->source) > 0)
                {
                    resolveSpecialPathPrefix(&content_json->_source_resolved, settings);
                    if (isValidTaf(content_json->_source_resolved, settings->core.full_taf_validation))
                    {
                        content_json->_source_type = CT_SOURCE_TAF;
                    }
                    else
                    {
                        /* Only try tap_load for .tap files (JSON). TAF files are binary – parsing them as JSON
                         * causes "Json parse error". Skip tap_load for .taf and other non-TAP paths. */
                        size_t src_len = osStrlen(content_json->_source_resolved);
                        bool_t looks_like_tap = (src_len >= 4 && osStrncasecmp(content_json->_source_resolved + src_len - 4, ".tap", 4) == 0);
                        error_t tap_error = looks_like_tap ? tap_load(content_json->_source_resolved, &content_json->_tap) : ERROR_INVALID_FILE;
                        if (tap_error == NO_ERROR && content_json->_tap._valid)
                        {
                            if (content_json->_tap._cached)
                            {
                                content_json->_source_type = CT_SOURCE_TAP_CACHED;
                            }
                            else
                            {
                                content_json->_source_type = CT_SOURCE_TAP_STREAM;
                            }
                            osFreeMem(content_json->_source_resolved);
                            content_json->_source_resolved = strdup(content_json->_tap._filepath_resolved);
                        }
                        else if (fsFileExists(content_json->_source_resolved) || osStrstr(content_json->_source_resolved, "://"))
                        {
                            content_json->_source_type = CT_SOURCE_STREAM;
                            if (!content_json->live || !content_json->nocloud)
                            {
                                content_json->live = true;
                                content_json->nocloud = true;
                                content_json->_updated = true;
                            }
                        }
                    }
                }

                if (jsonGetUInt32(contentJson, "_version") != CONTENT_JSON_VERSION)
                {
                    error = ERROR_INVALID_FILE;
                }

                cJSON_Delete(contentJson);
            }
        }
        else if (error == NO_ERROR) error = ERROR_FILE_OPENING_FAILED;
    }
    else
    {
        error = ERROR_FILE_NOT_FOUND;
    }

    if (error == NO_ERROR)
    {
        content_json->_valid = true;
    }

    /* Read-only callers must not replace an unreadable existing file. */
    if (error != NO_ERROR && create_if_missing)
    {
        error = save_content_json(jsonPath, content_json);
        if (error == NO_ERROR)
        {
            /* The reload memsets content_json to 0; free the fields parsed in
               this pass first so they are not leaked (e.g. on a _version
               mismatch, where everything was already allocated). */
            free_content_json(content_json);
            load_content_json(content_path, content_json, true, settings);
        }
    }

    osFreeMem(jsonPath);

    return error;
}

/* This is descriptive metadata, never an alternative playback assignment. */
const char *content_json_source_kind(const contentJson_t *content_json)
{
    const cJSON *assignment = content_json->source_assignment;
    const cJSON *kind = cJSON_GetObjectItemCaseSensitive(assignment, "kind");
    const cJSON *source = cJSON_GetObjectItemCaseSensitive(assignment, "source");
    if (!cJSON_IsObject(assignment) || !cJSON_IsString(kind) || !cJSON_IsString(source) ||
        !content_json->source || strcmp(source->valuestring, content_json->source) ||
        (strcmp(kind->valuestring, "audio") && strcmp(kind->valuestring, "radio"))) return NULL;
    return kind->valuestring;
}

error_t save_content_json(const char *json_path, contentJson_t *content_json)
{
    /* retrieve content directory */
    char *content_dir = strdup(json_path);

    if (fsRemoveFilename(content_dir) != NO_ERROR)
    {
        TRACE_ERROR("Error retrieving content directory from json path.\r\n");
        TRACE_ERROR("  json_path: '%s'\r\n", json_path);
        return ERROR_INVALID_PARAMETER;
    }
    /* create if not existing */
    if (!fsDirExists(content_dir))
    {
        TRACE_INFO("Content dir for JSON '%s' not existing, creating it.\r\n", json_path);
        fsCreateDir(content_dir);
    }
    osFreeMem(content_dir);

    char *jsonPathTmp = custom_asprintf("%s.tmp", json_path);
    error_t error = NO_ERROR;
    cJSON *contentJson = cJSON_CreateObject();

    cJSON_AddBoolToObject(contentJson, "live", content_json->live);
    cJSON_AddBoolToObject(contentJson, "nocloud", content_json->nocloud);
    jsonAddStringToObject(contentJson, "source", content_json->source);
    cJSON_AddNumberToObject(contentJson, "skip_seconds", content_json->skip_seconds);
    cJSON_AddBoolToObject(contentJson, "cache", content_json->cache);
    jsonAddStringToObject(contentJson, "cloud_ruid", content_json->cloud_ruid);
    jsonAddByteArrayToObject(contentJson, "cloud_auth", content_json->cloud_auth, content_json->cloud_auth_len);
    cJSON_AddBoolToObject(contentJson, "cloud_override", content_json->cloud_override);
    jsonAddStringToObject(contentJson, "tonie_model", content_json->tonie_model);
    cJSON_AddBoolToObject(contentJson, "hide", content_json->hide);
    cJSON_AddBoolToObject(contentJson, "claimed", content_json->claimed);
    if (content_json->source_profiles_version)
        cJSON_AddItemToObject(contentJson, "sourceProfilesVersion", cJSON_Duplicate(content_json->source_profiles_version, true));
    if (content_json->source_profiles_revision)
        cJSON_AddItemToObject(contentJson, "sourceProfilesRevision", cJSON_Duplicate(content_json->source_profiles_revision, true));
    if (content_json->source_profiles)
        cJSON_AddItemToObject(contentJson, "sourceProfiles", cJSON_Duplicate(content_json->source_profiles, true));
    if (content_json_source_kind(content_json))
        cJSON_AddItemToObject(contentJson, "sourceAssignment", cJSON_Duplicate(content_json->source_assignment, true));
    cJSON_AddNumberToObject(contentJson, "_version", CONTENT_JSON_VERSION);

    char *jsonRaw = cJSON_Print(contentJson);

    FsFile *file = fsOpenFile(jsonPathTmp, FS_FILE_MODE_WRITE);
    if (file != NULL)
    {
        error = fsWriteFile(file, jsonRaw, osStrlen(jsonRaw));
        fsCloseFile(file);
    }
    else
    {
        error = ERROR_FILE_OPENING_FAILED;
    }

    if (error == NO_ERROR)
    {
        error = fsMoveFile(jsonPathTmp, json_path, true);
    }

    if (error == NO_ERROR)
    {
        content_json->_updated = false;
        content_json->_version = CONTENT_JSON_VERSION;
    }

    cJSON_Delete(contentJson);
    osFreeMem(jsonRaw);
    osFreeMem(jsonPathTmp);
    return error;
}

static const char *persisted_text(const cJSON *object, const char *key)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(value) ? value->valuestring : "";
}

static bool_t same_text(const char *left, const char *right)
{
    return osStrcmp(left ? left : "", right ? right : "") == 0;
}

static char *content_path_for_json(const char *json_path)
{
    size_t length = osStrlen(json_path);
    if (length < 5 || osStrcmp(json_path + length - 5, ".json")) return NULL;
    return custom_asprintf("%.*s", (int)(length - 5), json_path);
}

/* Apply only fields changed by a legacy reader. Its profile-free snapshot must
 * never replace a newer Content JSON written by profile CAS or the worker. */
error_t save_content_json_legacy(const char *json_path, contentJson_t *modified, settings_t *settings)
{
    char *content_path = content_path_for_json(json_path);
    if (!content_path || !modified->_persisted_baseline)
    {
        osFreeMem(content_path);
        return ERROR_INVALID_PARAMETER;
    }
    podcast_content_lock(content_path);
    contentJson_t current = {0};
    error_t error = load_content_json(content_path, &current, false, settings);
    if (error != NO_ERROR || !current._valid)
    {
        free_content_json(&current);
        podcast_content_unlock(content_path);
        osFreeMem(content_path);
        return error != NO_ERROR ? error : ERROR_INVALID_FILE;
    }

    const cJSON *base = modified->_persisted_baseline;
    const cJSON *disk = current._persisted_baseline;
    bool_t changed = false;
    bool_t conflict = false;
    /* Loading a stream derives these runtime flags. Start from actual disk
     * values, then apply only this reader's intentional changes. */
    current.live = jsonGetBool((cJSON *)disk, "live");
    current.nocloud = jsonGetBool((cJSON *)disk, "nocloud");
    current.cloud_override = jsonGetBool((cJSON *)disk, "cloud_override");

#define MERGE_BOOL(member, key) do { \
    bool_t old_value = jsonGetBool((cJSON *)base, key); \
    if (modified->member != old_value) { \
        if (jsonGetBool((cJSON *)disk, key) != old_value) conflict = true; \
        else { current.member = modified->member; changed = true; } \
    } \
} while (0)
#define MERGE_TEXT(member, key) do { \
    const char *old_value = persisted_text(base, key); \
    if (!same_text(modified->member, old_value)) { \
        if (!same_text(persisted_text(disk, key), old_value)) conflict = true; \
        else { char *replacement = strdup(modified->member ? modified->member : ""); \
            if (!replacement) { error = ERROR_OUT_OF_MEMORY; goto done; } \
            osFreeMem(current.member); current.member = replacement; changed = true; } \
    } \
} while (0)

    const char *old_source = persisted_text(base, "source");
    bool_t source_changed_elsewhere = !same_text(persisted_text(disk, "source"), old_source);
    bool_t source_update = !same_text(modified->source, old_source);
    bool_t live_update = modified->live != jsonGetBool((cJSON *)base, "live");
    bool_t nocloud_update = modified->nocloud != jsonGetBool((cJSON *)base, "nocloud");
    bool_t override_update = modified->cloud_override != jsonGetBool((cJSON *)base, "cloud_override");
    if (source_changed_elsewhere && (source_update || live_update || nocloud_update || override_update))
        conflict = true;
    else
    {
        MERGE_TEXT(source, "source");
        MERGE_BOOL(live, "live");
        MERGE_BOOL(nocloud, "nocloud");
        MERGE_BOOL(cloud_override, "cloud_override");
    }
    MERGE_BOOL(cache, "cache");
    MERGE_BOOL(hide, "hide");
    MERGE_BOOL(claimed, "claimed");
    MERGE_TEXT(cloud_ruid, "cloud_ruid");
    MERGE_TEXT(tonie_model, "tonie_model");

    size_t old_skip = jsonGetUInt32((cJSON *)base, "skip_seconds");
    if (modified->skip_seconds != old_skip)
    {
        if (jsonGetUInt32((cJSON *)disk, "skip_seconds") != old_skip) conflict = true;
        else { current.skip_seconds = modified->skip_seconds; changed = true; }
    }
    size_t old_auth_len = 0, disk_auth_len = 0;
    uint8_t *old_auth = jsonGetBytes((cJSON *)base, "cloud_auth", &old_auth_len);
    uint8_t *disk_auth = jsonGetBytes((cJSON *)disk, "cloud_auth", &disk_auth_len);
    bool_t auth_update = old_auth_len != modified->cloud_auth_len ||
                         (old_auth_len && osMemcmp(old_auth, modified->cloud_auth, old_auth_len));
    if (auth_update)
    {
        if (disk_auth_len != old_auth_len || (old_auth_len && osMemcmp(disk_auth, old_auth, old_auth_len)))
            conflict = true;
        else
        {
            uint8_t *replacement = NULL;
            if (modified->cloud_auth_len)
            {
                replacement = osAllocMem(modified->cloud_auth_len);
                if (!replacement) { error = ERROR_OUT_OF_MEMORY; goto auth_done; }
                osMemcpy(replacement, modified->cloud_auth, modified->cloud_auth_len);
            }
            osFreeMem(current.cloud_auth);
            current.cloud_auth = replacement;
            current.cloud_auth_len = modified->cloud_auth_len;
            changed = true;
        }
    }
auth_done:
    osFreeMem(old_auth);
    osFreeMem(disk_auth);
    if (error != NO_ERROR) goto done;
    if (changed) error = save_content_json(json_path, &current);
    if (error == NO_ERROR && conflict) error = ERROR_FAILURE;
done:
#undef MERGE_BOOL
#undef MERGE_TEXT
    free_content_json(&current);
    podcast_content_unlock(content_path);
    osFreeMem(content_path);
    return error;
}

error_t move_content_json_source(const char *content_path, const char *expected_source,
                                 const char *new_source, const char *from_file,
                                 const char *to_file, bool_t skip_move, settings_t *settings)
{
    podcast_content_lock(content_path);
    contentJson_t current = {0};
    error_t error = load_content_json(content_path, &current, false, settings);
    if (error == NO_ERROR && current._valid)
    {
        if (!same_text(current.source, expected_source)) error = ERROR_FAILURE;
        else
        {
            char *replacement = strdup(new_source);
            if (!replacement) error = ERROR_OUT_OF_MEMORY;
            else
            {
                error = skip_move ? fsDeleteFile(from_file) : fsMoveFile(from_file, to_file, false);
                if (error == NO_ERROR)
                {
                    osFreeMem(current.source);
                    current.source = replacement;
                    current.live = jsonGetBool(current._persisted_baseline, "live");
                    current.nocloud = jsonGetBool(current._persisted_baseline, "nocloud");
                    current.cloud_override = jsonGetBool(current._persisted_baseline, "cloud_override");
                    char *json_path = custom_asprintf("%s.json", content_path);
                    error = json_path ? save_content_json(json_path, &current) : ERROR_OUT_OF_MEMORY;
                    osFreeMem(json_path);
                }
                else osFreeMem(replacement);
            }
        }
    }
    free_content_json(&current);
    podcast_content_unlock(content_path);
    return error;
}

error_t rename_content_json_model(const char *content_path, const char *from_model,
                                  const char *to_model, settings_t *settings)
{
    podcast_content_lock(content_path);
    contentJson_t current = {0};
    error_t error = load_content_json(content_path, &current, false, settings);
    if (error == NO_ERROR && current._valid && current.tonie_model && current.tonie_model[0] &&
        osStrcasecmp(current.tonie_model, from_model) == 0)
    {
        current.live = jsonGetBool(current._persisted_baseline, "live");
        current.nocloud = jsonGetBool(current._persisted_baseline, "nocloud");
        current.cloud_override = jsonGetBool(current._persisted_baseline, "cloud_override");
        char *replacement = strdup(to_model);
        if (!replacement) error = ERROR_OUT_OF_MEMORY;
        else
        {
            osFreeMem(current.tonie_model);
            current.tonie_model = replacement;
            char *json_path = custom_asprintf("%s.json", content_path);
            error = json_path ? save_content_json(json_path, &current) : ERROR_OUT_OF_MEMORY;
            osFreeMem(json_path);
        }
    }
    free_content_json(&current);
    podcast_content_unlock(content_path);
    return error;
}

void content_json_update_model(contentJson_t *content_json, uint32_t audio_id, uint8_t *hash)
{
    if (content_json->_valid)
    {
        toniesJson_item_t *toniesJson = tonies_byAudioIdHash(audio_id, hash);
        if (toniesJson != NULL)
        {
            if (osStrcmp(content_json->tonie_model, toniesJson->model) != 0)
            {
                if (audio_id == SPECIAL_AUDIO_ID_ONE && hash == NULL)
                { // don't update special tonies without hash
                }
                else
                {
                    osFreeMem(content_json->tonie_model);
                    content_json->tonie_model = strdup(toniesJson->model);
                    content_json->_updated = true;
                }
            }
        }
        else
        {
            // TODO add to tonies.custom.json + report
            TRACE_DEBUG("Audio-id %08X unknown but previous content known by model %s.\r\n", audio_id, content_json->tonie_model);
        }
    }
}

void free_content_json(contentJson_t *content_json)
{
    content_json->_valid = false;
    cJSON_Delete(content_json->_persisted_baseline);
    content_json->_persisted_baseline = NULL;
    cJSON_Delete(content_json->source_profiles_version);
    cJSON_Delete(content_json->source_profiles_revision);
    cJSON_Delete(content_json->source_profiles);
    cJSON_Delete(content_json->source_assignment);
    content_json->source_assignment = NULL;
    content_json->source_profiles_version = NULL;
    content_json->source_profiles_revision = NULL;
    content_json->source_profiles = NULL;
    if (content_json->source)
    {
        osFreeMem(content_json->source);
        content_json->source = NULL;
    }
    if (content_json->cloud_ruid)
    {
        osFreeMem(content_json->cloud_ruid);
        content_json->cloud_ruid = NULL;
    }
    if (content_json->cloud_auth)
    {
        osFreeMem(content_json->cloud_auth);
        content_json->cloud_auth = NULL;
    }
    if (content_json->tonie_model)
    {
        osFreeMem(content_json->tonie_model);
        content_json->tonie_model = NULL;
    }
    if (content_json->_source_model)
    {
        osFreeMem(content_json->_source_model);
        content_json->_source_model = NULL;
    }
    if (content_json->_streamFile)
    {
        osFreeMem(content_json->_streamFile);
        content_json->_streamFile = NULL;
    }
    if (content_json->_source_resolved)
    {
        osFreeMem(content_json->_source_resolved);
        content_json->_source_resolved = NULL;
    }
    tap_free(&content_json->_tap);
    content_json->cloud_auth_len = 0;
}
