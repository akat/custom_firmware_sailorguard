#pragma once

#include "esp_http_server.h"
#include "esp_netif.h"

httpd_handle_t start_http_server(const char *base_path, esp_netif_t *ap_netif);
void http_server_register_static(httpd_handle_t server, const char *base_path);
