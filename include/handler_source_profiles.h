#pragma once

#include "handler.h"
#include "http/http_server.h"

error_t handleApiSourceProfiles(HttpConnection *connection, const char_t *uri,
                                const char_t *query, client_ctx_t *client_ctx);
