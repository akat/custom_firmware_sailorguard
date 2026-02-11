#pragma once

#include "esp_http_server.h"
#include "esp_netif.h"

void status_api_register(httpd_handle_t server, esp_netif_t *ap_netif);
