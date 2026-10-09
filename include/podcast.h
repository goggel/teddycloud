#pragma once
#include "handler.h"
#include "cJSON.h"

#ifndef PODCAST_SUPPORT
#define PODCAST_SUPPORT 0
#endif
#ifndef PODCAST_TESTING
#define PODCAST_TESTING 0
#endif

void podcast_init(void);
void podcast_shutdown(void);
void podcast_tick(void);
void podcast_placed(settings_t *settings, uint64_t uid);
/* Real lock independent of the currently disabled mutex_lock_id implementation.
 * Lock order: content -> service; never hold service while acquiring content. */
void podcast_content_lock(const char *path);
void podcast_content_unlock(const char *path);
void podcast_source_changed(const char *path, const char *source);
cJSON *podcast_request(const char *action, const char *overlay, const char *ruid, const cJSON *body, int *status);
/* NULL: ordinary source. Allocated immutable path: managed source.
 * Orphan nonzero ranges return an empty path and must not serve new bytes. */
char *podcast_download_path(settings_t *settings, const char *box, const char *ruid, const char *source, size_t offset);
