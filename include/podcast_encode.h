#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
bool podcast_encode(const char *const *inputs, size_t count, const char *output, uint32_t audio_id,
                    const atomic_bool *cancel, char *error, size_t error_size);
