#include "signalk_mdns.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "signalk_mdns";

static signalk_discovered_server_t g_discovered_servers[10];
static size_t g_server_count = 0;

// Note: Full mDNS discovery requires the ESP-IDF mdns component.
// For now, this provides the API structure with manual server entry support.
// To enable full auto-discovery, add mdns library and uncomment the mdns_query_ptr section.

esp_err_t signalk_mdns_start_discovery(void) {
    ESP_LOGI(TAG, "Starting mDNS discovery");

    g_server_count = 0;
    memset(g_discovered_servers, 0, sizeof(g_discovered_servers));

    // TODO: Implement with full mDNS when component is available
    // This would require:
    // 1. Adding mdns component to system
    // 2. Including <mdns.h>
    // 3. Calling mdns_query_ptr("_signalk-http", "_tcp", 3000, 20, &results)
    // 4. Parsing results and populating g_discovered_servers
    
    // For now, discovery is available through manual server entry in API
    ESP_LOGI(TAG, "mDNS discovery ready (manual entry via API)");

    return ESP_OK;
}

esp_err_t signalk_mdns_stop_discovery(void) {
    ESP_LOGI(TAG, "Stopping mDNS discovery");
    return ESP_OK;
}

esp_err_t signalk_mdns_get_servers(signalk_discovered_server_t *servers,
                                    size_t max_servers,
                                    size_t *count) {
    if (!servers || !count) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t copy_count = (g_server_count < max_servers) ? g_server_count : max_servers;
    memcpy(servers, g_discovered_servers, copy_count * sizeof(signalk_discovered_server_t));
    *count = copy_count;

    return ESP_OK;
}

esp_err_t signalk_mdns_clear_servers(void) {
    ESP_LOGI(TAG, "Clearing discovered servers list");
    memset(g_discovered_servers, 0, sizeof(g_discovered_servers));
    g_server_count = 0;

    return ESP_OK;
}
