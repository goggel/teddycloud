#pragma once

#include "cJSON.h"
#include "settings.h"

/* The caller owns the returned JSON tree. The request uses the shared content
 * mutex and never activates a subscription or changes the playing assignment. */
cJSON *source_profiles_request(const char *action, const char *overlay, const char *ruid,
                               const cJSON *body, int *status);
