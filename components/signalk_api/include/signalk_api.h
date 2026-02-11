#pragma once

#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register SignalK API endpoints with HTTP server
 * @param server HTTP server handle
 * @return ESP_OK on success
 */
void signalk_api_register(httpd_handle_t server);

#ifdef __cplusplus
}
#endif
