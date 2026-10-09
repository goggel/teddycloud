#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
typedef struct
{
    const char *path;
    uint32_t trim_start_seconds, trim_end_seconds;
} podcast_encode_input_t;
bool podcast_encode(const podcast_encode_input_t *inputs, size_t count, const char *output, uint32_t audio_id,
                    const atomic_bool *cancel, double *durations, char *error, size_t error_size);
