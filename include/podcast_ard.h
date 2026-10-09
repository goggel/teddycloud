#pragma once
#include "cJSON.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include <time.h>
bool podcast_ard_identifier(const char *url, char identifier[64]);
bool podcast_ard_date(const char *value, time_t *result);
char *podcast_ard_query_url(const char *operation, const cJSON *variables);
cJSON *podcast_ard_search(const char *query, const atomic_bool *cancel, char *error, size_t size);
cJSON *podcast_ard_resolve(const char *url, const atomic_bool *cancel, char *error, size_t size);
cJSON *podcast_ard_preview(const char *url, unsigned count, bool oldest, time_t now, const atomic_bool *cancel,
                           char *error, size_t size);
