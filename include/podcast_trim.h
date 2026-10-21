#pragma once
#include "cJSON.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define PODCAST_SAMPLE_RATE 48000ULL
#define PODCAST_MAX_TRIM 21600U
bool podcast_trim_frames(double seconds, uint64_t *frames);
bool podcast_trim_window(uint64_t total, double start, double end, uint64_t *first, uint64_t *last);
uint64_t podcast_trim_overlap(uint64_t offset, uint64_t count, uint64_t first, uint64_t last, uint64_t *skip);
bool podcast_trim_valid(const cJSON *config, char *error, size_t size);
void podcast_trim_defaults(cJSON *config);
void podcast_trim_effective(const cJSON *config, const char *identity, double *start, double *end);
void podcast_trim_preview(cJSON *preview, const cJSON *config);
