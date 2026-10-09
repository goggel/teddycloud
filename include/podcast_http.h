#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>

#define PODCAST_FEED_LIMIT (5U * 1024U * 1024U)
#define PODCAST_AUDIO_LIMIT (200U * 1024U * 1024U)
typedef struct
{
    char *data;
    size_t length;
    char *final_url;
} podcast_response_t;
bool podcast_http_init(void);
void podcast_http_cleanup(void);
bool podcast_url_valid(const char *url);
bool podcast_address_public(int family, const void *address);
bool podcast_http_get(const char *url, size_t limit, const char *file, const atomic_bool *cancel,
                      podcast_response_t *response, char *error, size_t error_size);
void podcast_response_free(podcast_response_t *response);
bool podcast_http_get_timed(const char *url, size_t limit, const char *file, const atomic_bool *cancel,
                            podcast_response_t *response, char *error, size_t error_size, long timeout_ms);
