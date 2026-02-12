#pragma once

#include "esp_err.h"
#include "signalk_types.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char target_ip[16];
    uint16_t broadcast_port;
    uint16_t listen_port;
} signalk_udp_config_t;

typedef void (*signalk_udp_rx_callback_t)(const signalk_data_t *data, void *ctx);

/**
 * @brief Initialize UDP transport (no sockets opened yet).
 */
esp_err_t signalk_udp_init(void);

/**
 * @brief Start UDP transport with the provided configuration.
 */
esp_err_t signalk_udp_start(const signalk_udp_config_t *config);

/**
 * @brief Stop UDP transport and close sockets.
 */
void signalk_udp_stop(void);

/**
 * @brief Check if UDP transport is running.
 */
bool signalk_udp_is_running(void);

/**
 * @brief Send a SignalK delta over UDP.
 */
esp_err_t signalk_udp_send(const signalk_data_t *data);

/**
 * @brief Register a callback for incoming UDP SignalK deltas.
 */
void signalk_udp_set_rx_callback(signalk_udp_rx_callback_t cb, void *ctx);

#ifdef __cplusplus
}
#endif
