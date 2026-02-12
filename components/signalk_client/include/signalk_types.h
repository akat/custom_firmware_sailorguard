#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// SignalK connection states
typedef enum {
    SIGNALK_STATE_DISABLED = 0,
    SIGNALK_STATE_DISCONNECTED,
    SIGNALK_STATE_DISCOVERING,
    SIGNALK_STATE_AUTH_PENDING,
    SIGNALK_STATE_CONNECTING,
    SIGNALK_STATE_CONNECTED,
    SIGNALK_STATE_STREAMING,
    SIGNALK_STATE_ERROR
} signalk_state_t;

// Server information
typedef struct {
    char hostname[128];
    char ip[16];
    uint16_t port;
    bool ssl_enabled;
} signalk_server_t;

// Connection configuration
typedef struct {
    bool enabled;
    bool auto_discovery;
    char hostname[128];
    uint16_t port;
    bool use_ssl;
    char token[512];
    char vessel_name[64];
    char client_id[64];
} signalk_config_t;

// Connection status
typedef struct {
    signalk_state_t state;
    signalk_server_t server;
    bool authenticated;
    char error_message[256];
    uint32_t uptime_seconds;
    uint32_t messages_sent;
    uint32_t messages_received;
    int64_t last_message_time;
} signalk_status_t;

// Data value types
typedef enum {
    SIGNALK_VALUE_NULL = 0,
    SIGNALK_VALUE_BOOL,
    SIGNALK_VALUE_INT,
    SIGNALK_VALUE_FLOAT,
    SIGNALK_VALUE_STRING,
    SIGNALK_VALUE_POSITION
} signalk_value_type_t;

// Position data
typedef struct {
    double latitude;
    double longitude;
    double altitude;
} signalk_position_t;

// Data value union
typedef union {
    bool b;
    int32_t i;
    float f;
    char s[128];
    signalk_position_t pos;
} signalk_value_data_t;

// Data to send to SignalK
typedef struct {
    char path[128];              // e.g., "navigation.position"
    signalk_value_type_t type;
    signalk_value_data_t value;
    char source_label[32];
} signalk_data_t;

// Discovered server (from mDNS)
typedef struct {
    char name[64];
    char hostname[128];
    char ip[16];
    uint16_t port;
    bool has_ssl;
    char version[32];
} signalk_discovered_server_t;

// Callback for incoming delta data
typedef void (*signalk_data_callback_t)(const signalk_data_t *data, void *user_ctx);

// Subscription entry
typedef struct {
    char path[128];
    uint32_t period_ms;  // 0 = server default
} signalk_subscription_t;

// Auth request status
typedef struct {
    char request_id[64];
    enum {
        AUTH_REQUEST_PENDING,
        AUTH_REQUEST_APPROVED,
        AUTH_REQUEST_DENIED,
        AUTH_REQUEST_TIMEOUT,
        AUTH_REQUEST_ERROR
    } state;
    char token[512];
    char approval_url[256];
} signalk_auth_request_t;

#ifdef __cplusplus
}
#endif
