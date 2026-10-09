#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include "cJSON.h"

/* Returned object owns title and an ordered episodes array. No remote HTML. */
cJSON *podcast_feed_parse(const char *xml, size_t length, const char *base_url, unsigned count, bool oldest_first,
                          time_t now, char *error, size_t error_size);
bool podcast_config_valid(const cJSON *config, char *error, size_t error_size);
const char *podcast_string(const cJSON *object, const char *key);
double podcast_number(const cJSON *object, const char *key);
