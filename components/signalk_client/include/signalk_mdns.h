#pragma once

#include "signalk_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start mDNS discovery for SignalK servers
 * @return ESP_OK on success
 */
esp_err_t signalk_mdns_start_discovery(void);

/**
 * @brief Stop mDNS discovery
 * @return ESP_OK on success
 */
esp_err_t signalk_mdns_stop_discovery(void);

/**
 * @brief Get list of discovered servers
 * @param servers Array to store discovered servers
 * @param max_servers Maximum number of servers to return
 * @param count Pointer to store actual number of servers found
 * @return ESP_OK on success
 */
esp_err_t signalk_mdns_get_servers(signalk_discovered_server_t *servers, 
                                    size_t max_servers, 
                                    size_t *count);

/**
 * @brief Clear discovered servers list
 * @return ESP_OK on success
 */
esp_err_t signalk_mdns_clear_servers(void);

#ifdef __cplusplus
}
#endif
