#include "signalk_api.h"
#include "signalk_client.h"
#include "signalk_mdns.h"
#include "signalk_auth.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "signalk_api";

// ============================================================================
// GET /api/signalk/status - Get connection status
// ============================================================================
static esp_err_t signalk_status_handler(httpd_req_t *req) {
    signalk_status_t status;
    esp_err_t err = signalk_get_status(&status);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get status");
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "state", status.state);
    cJSON_AddStringToObject(root, "state_name",
        status.state == SIGNALK_STATE_DISABLED ? "DISABLED" :
        status.state == SIGNALK_STATE_DISCONNECTED ? "DISCONNECTED" :
        status.state == SIGNALK_STATE_DISCOVERING ? "DISCOVERING" :
        status.state == SIGNALK_STATE_AUTH_PENDING ? "AUTH_PENDING" :
        status.state == SIGNALK_STATE_CONNECTING ? "CONNECTING" :
        status.state == SIGNALK_STATE_CONNECTED ? "CONNECTED" :
        status.state == SIGNALK_STATE_STREAMING ? "STREAMING" :
        "ERROR"
    );
    cJSON_AddBoolToObject(root, "authenticated", status.authenticated);
    cJSON_AddNumberToObject(root, "uptime_seconds", status.uptime_seconds);
    cJSON_AddNumberToObject(root, "messages_sent", status.messages_sent);
    cJSON_AddNumberToObject(root, "messages_received", status.messages_received);

    if (status.server.hostname[0]) {
        cJSON *server = cJSON_CreateObject();
        cJSON_AddStringToObject(server, "hostname", status.server.hostname);
        cJSON_AddStringToObject(server, "ip", status.server.ip);
        cJSON_AddNumberToObject(server, "port", status.server.port);
        cJSON_AddBoolToObject(server, "ssl", status.server.ssl_enabled);
        cJSON_AddItemToObject(root, "server", server);
    }

    if (status.error_message[0]) {
        cJSON_AddStringToObject(root, "error", status.error_message);
    }

    char *response = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    esp_err_t result = httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    free(response);
    return result;
}

// ============================================================================
// GET /api/signalk/config - Get current configuration
// ============================================================================
static esp_err_t signalk_config_get_handler(httpd_req_t *req) {
    signalk_config_t config;
    esp_err_t err = signalk_get_config(&config);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get config");
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "enabled", config.enabled);
    cJSON_AddBoolToObject(root, "auto_discovery", config.auto_discovery);
    cJSON_AddStringToObject(root, "hostname", config.hostname);
    cJSON_AddNumberToObject(root, "port", config.port);
    cJSON_AddBoolToObject(root, "use_ssl", config.use_ssl);
    cJSON_AddStringToObject(root, "vessel_name", config.vessel_name);

    char *response = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    esp_err_t result = httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    free(response);
    return result;
}

// ============================================================================
// POST /api/signalk/config - Set configuration
// ============================================================================
static esp_err_t signalk_config_set_handler(httpd_req_t *req) {
    size_t total_len = req->content_len;
    if (total_len == 0 || total_len > 2048) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body size");
    }

    char *buffer = malloc(total_len + 1);
    if (!buffer) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }

    size_t received = 0;
    while (received < total_len) {
        int len = httpd_req_recv(req, buffer + received, total_len - received);
        if (len <= 0) {
            free(buffer);
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to read body");
        }
        received += len;
    }
    buffer[received] = '\0';

    cJSON *json = cJSON_Parse(buffer);
    free(buffer);
    if (!json) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    }

    // Get current config
    signalk_config_t config;
    signalk_get_config(&config);

    // Update from JSON
    cJSON *item;

    if ((item = cJSON_GetObjectItem(json, "enabled")) && cJSON_IsBool(item)) {
        config.enabled = cJSON_IsTrue(item);
    }

    if ((item = cJSON_GetObjectItem(json, "auto_discovery")) && cJSON_IsBool(item)) {
        config.auto_discovery = cJSON_IsTrue(item);
    }


    if ((item = cJSON_GetObjectItem(json, "hostname")) && cJSON_IsString(item)) {
        strncpy(config.hostname, item->valuestring, sizeof(config.hostname) - 1);
    }

    if ((item = cJSON_GetObjectItem(json, "port")) && cJSON_IsNumber(item)) {
        config.port = (uint16_t)item->valueint;
    }

    if ((item = cJSON_GetObjectItem(json, "use_ssl")) && cJSON_IsBool(item)) {
        config.use_ssl = cJSON_IsTrue(item);
    }


    if ((item = cJSON_GetObjectItem(json, "vessel_name")) && cJSON_IsString(item)) {
        strncpy(config.vessel_name, item->valuestring, sizeof(config.vessel_name) - 1);
    }

    cJSON_Delete(json);

    // Save new configuration
    esp_err_t err = signalk_set_config(&config);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save config");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

// ============================================================================
// POST /api/signalk/discover - Start mDNS discovery
// ============================================================================
static esp_err_t signalk_discover_handler(httpd_req_t *req) {
    signalk_mdns_clear_servers();
    esp_err_t err = signalk_mdns_start_discovery();
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Discovery failed");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":true,\"message\":\"Discovery started\"}", HTTPD_RESP_USE_STRLEN);
}

// ============================================================================
// GET /api/signalk/servers - Get discovered servers
// ============================================================================
static esp_err_t signalk_servers_handler(httpd_req_t *req) {
    signalk_discovered_server_t servers[10];
    size_t count = 0;

    signalk_mdns_get_servers(servers, 10, &count);

    cJSON *root = cJSON_CreateArray();
    for (size_t i = 0; i < count; i++) {
        cJSON *server = cJSON_CreateObject();
        cJSON_AddStringToObject(server, "name", servers[i].name);
        cJSON_AddStringToObject(server, "hostname", servers[i].hostname);
        cJSON_AddStringToObject(server, "ip", servers[i].ip);
        cJSON_AddNumberToObject(server, "port", servers[i].port);
        cJSON_AddBoolToObject(server, "ssl", servers[i].has_ssl);
        cJSON_AddStringToObject(server, "version", servers[i].version);
        cJSON_AddItemToArray(root, server);
    }

    char *response = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    esp_err_t result = httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    free(response);
    return result;
}

// ============================================================================
// POST /api/signalk/connect - Connect to server
// ============================================================================
static esp_err_t signalk_connect_handler(httpd_req_t *req) {
    size_t total_len = req->content_len;
    if (total_len == 0 || total_len > 512) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body size");
    }

    char *buffer = malloc(total_len + 1);
    if (!buffer) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }

    int len = httpd_req_recv(req, buffer, total_len);
    if (len <= 0) {
        free(buffer);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to read body");
    }
    buffer[len] = '\0';

    cJSON *json = cJSON_Parse(buffer);
    free(buffer);
    if (!json) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    }

    cJSON *hostname_item = cJSON_GetObjectItem(json, "hostname");
    cJSON *port_item = cJSON_GetObjectItem(json, "port");

    if (!hostname_item || !cJSON_IsString(hostname_item)) {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing hostname");
    }

    uint16_t port = 3000;
    if (port_item && cJSON_IsNumber(port_item)) {
        port = (uint16_t)port_item->valueint;
    }

    esp_err_t err = signalk_connect(hostname_item->valuestring, port);
    cJSON_Delete(json);

    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Connection failed");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

// ============================================================================
// POST /api/signalk/disconnect - Disconnect from server
// ============================================================================
static esp_err_t signalk_disconnect_handler(httpd_req_t *req) {
    signalk_disconnect();

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

// ============================================================================
// POST /api/signalk/clear-token - Clear stored authentication token
// ============================================================================
static esp_err_t signalk_clear_token_handler(httpd_req_t *req) {
    esp_err_t err = signalk_auth_clear_token();
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", err == ESP_OK);
    if (err != ESP_OK) {
        cJSON_AddStringToObject(root, "error", esp_err_to_name(err));
    }
    
    char *response = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    httpd_resp_set_type(req, "application/json");
    esp_err_t result = httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    free(response);
    return result;
}

// ============================================================================
// POST /api/signalk/send - Send SignalK data via WebSocket
// ============================================================================
static esp_err_t signalk_send_data_handler(httpd_req_t *req) {
    size_t total_len = req->content_len;
    if (total_len == 0 || total_len > 1024) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body size");
    }

    char *buffer = malloc(total_len + 1);
    if (!buffer) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }

    int len = httpd_req_recv(req, buffer, total_len);
    if (len <= 0) {
        free(buffer);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to read body");
    }
    buffer[len] = '\0';

    cJSON *json = cJSON_Parse(buffer);
    free(buffer);
    if (!json) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    }

    // Extract path
    cJSON *path_item = cJSON_GetObjectItem(json, "path");
    if (!path_item || !cJSON_IsString(path_item) || !path_item->valuestring[0]) {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or empty path");
    }

    // Extract value (any type)
    cJSON *value_item = cJSON_GetObjectItem(json, "value");
    if (!value_item) {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing value");
    }

    // Prepare signalk_data_t
    signalk_data_t data = {0};
    strncpy(data.path, path_item->valuestring, sizeof(data.path) - 1);

    // Determine type and convert value
    if (cJSON_IsNull(value_item)) {
        data.type = SIGNALK_VALUE_NULL;
    } else if (cJSON_IsBool(value_item)) {
        data.type = SIGNALK_VALUE_BOOL;
        data.value.b = cJSON_IsTrue(value_item);
    } else if (cJSON_IsNumber(value_item)) {
        // Check if it's an integer or float
        if (value_item->valuedouble == (double)(long)value_item->valuedouble) {
            data.type = SIGNALK_VALUE_INT;
            data.value.i = (int64_t)value_item->valuedouble;
        } else {
            data.type = SIGNALK_VALUE_FLOAT;
            data.value.f = (float)value_item->valuedouble;
        }
    } else if (cJSON_IsString(value_item)) {
        data.type = SIGNALK_VALUE_STRING;
        strncpy(data.value.s, value_item->valuestring, sizeof(data.value.s) - 1);
    } else if (cJSON_IsObject(value_item)) {
        // Check if it's a position object
        cJSON *lat = cJSON_GetObjectItem(value_item, "latitude");
        cJSON *lon = cJSON_GetObjectItem(value_item, "longitude");
        if (lat && lon && cJSON_IsNumber(lat) && cJSON_IsNumber(lon)) {
            data.type = SIGNALK_VALUE_POSITION;
            data.value.pos.latitude = lat->valuedouble;
            data.value.pos.longitude = lon->valuedouble;
            cJSON *alt = cJSON_GetObjectItem(value_item, "altitude");
            if (alt && cJSON_IsNumber(alt)) {
                data.value.pos.altitude = alt->valuedouble;
            }
        } else {
            cJSON_Delete(json);
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid object format");
        }
    } else {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unsupported value type");
    }

    // Optional: source label
    cJSON *source_item = cJSON_GetObjectItem(json, "source");
    if (source_item && cJSON_IsString(source_item) && source_item->valuestring[0]) {
        strncpy(data.source_label, source_item->valuestring, sizeof(data.source_label) - 1);
    } else {
        strncpy(data.source_label, "sailorguard", sizeof(data.source_label) - 1);
    }

    cJSON_Delete(json);

    // Send the data
    esp_err_t err = signalk_send_data(&data);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Send failed path=%s err=0x%x", data.path, (unsigned int)err);
        cJSON *root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "ok", false);
        char *response;
        if (err == ESP_ERR_INVALID_STATE) {
            cJSON_AddStringToObject(root, "error", "Not connected to SignalK server");
        } else if (err == ESP_ERR_INVALID_ARG) {
            cJSON_AddStringToObject(root, "error", "Invalid argument");
        } else {
            cJSON_AddStringToObject(root, "error", "Failed to send data");
        }
        response = cJSON_PrintUnformatted(root);
        httpd_resp_set_type(req, "application/json");
        esp_err_t result = httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
        free(response);
        cJSON_Delete(root);
        return result;
    }

    ESP_LOGI(TAG, "Send ok path=%s", data.path);
    // Success response
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "message", "Data sent");
    cJSON_AddStringToObject(root, "path", data.path);
    char *response = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    esp_err_t result = httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    free(response);
    cJSON_Delete(root);
    return result;
}

// ============================================================================
// POST /api/signalk/test - Test connection
// ============================================================================
static esp_err_t signalk_test_handler(httpd_req_t *req) {
    size_t total_len = req->content_len;
    if (total_len == 0 || total_len > 512) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body size");
    }

    char *buffer = malloc(total_len + 1);
    if (!buffer) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }

    int len = httpd_req_recv(req, buffer, total_len);
    if (len <= 0) {
        free(buffer);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to read body");
    }
    buffer[len] = '\0';

    cJSON *json = cJSON_Parse(buffer);
    free(buffer);
    if (!json) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    }

    cJSON *hostname_item = cJSON_GetObjectItem(json, "hostname");
    cJSON *port_item = cJSON_GetObjectItem(json, "port");

    if (!hostname_item || !cJSON_IsString(hostname_item)) {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing hostname");
    }

    uint16_t port = 3000;
    if (port_item && cJSON_IsNumber(port_item)) {
        port = (uint16_t)port_item->valueint;
    }

    // Test in background (stub for now)
    ESP_LOGI(TAG, "Testing connection to %s:%d", hostname_item->valuestring, port);
    cJSON_Delete(json);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":true,\"message\":\"Test in progress\"}", HTTPD_RESP_USE_STRLEN);
}

// ============================================================================
// GET /api/signalk/subscriptions - Get subscriptions with cached values
// ============================================================================
static esp_err_t signalk_subscriptions_handler(httpd_req_t *req) {
    signalk_subscription_t subs[8];
    size_t count = 0;
    signalk_get_subscriptions(subs, 8, &count);

    cJSON *root = cJSON_CreateArray();
    for (size_t i = 0; i < count; i++) {
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddStringToObject(entry, "path", subs[i].path);
        cJSON_AddNumberToObject(entry, "period_ms", subs[i].period_ms);

        signalk_data_t data;
        if (signalk_get_cached_value(subs[i].path, &data) == ESP_OK) {
            switch (data.type) {
                case SIGNALK_VALUE_FLOAT:
                    cJSON_AddNumberToObject(entry, "value", (double)data.value.f);
                    cJSON_AddStringToObject(entry, "type", "float");
                    break;
                case SIGNALK_VALUE_INT:
                    cJSON_AddNumberToObject(entry, "value", data.value.i);
                    cJSON_AddStringToObject(entry, "type", "int");
                    break;
                case SIGNALK_VALUE_BOOL:
                    cJSON_AddBoolToObject(entry, "value", data.value.b);
                    cJSON_AddStringToObject(entry, "type", "bool");
                    break;
                case SIGNALK_VALUE_STRING:
                    cJSON_AddStringToObject(entry, "value", data.value.s);
                    cJSON_AddStringToObject(entry, "type", "string");
                    break;
                case SIGNALK_VALUE_POSITION: {
                    cJSON *pos = cJSON_CreateObject();
                    cJSON_AddNumberToObject(pos, "latitude", data.value.pos.latitude);
                    cJSON_AddNumberToObject(pos, "longitude", data.value.pos.longitude);
                    if (data.value.pos.altitude != 0.0) {
                        cJSON_AddNumberToObject(pos, "altitude", data.value.pos.altitude);
                    }
                    cJSON_AddItemToObject(entry, "value", pos);
                    cJSON_AddStringToObject(entry, "type", "position");
                    break;
                }
                default:
                    cJSON_AddNullToObject(entry, "value");
                    cJSON_AddStringToObject(entry, "type", "null");
                    break;
            }
            cJSON_AddStringToObject(entry, "source", data.source_label);
        } else {
            cJSON_AddNullToObject(entry, "value");
            cJSON_AddStringToObject(entry, "type", "null");
        }

        cJSON_AddItemToArray(root, entry);
    }

    char *response = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    esp_err_t result = httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    free(response);
    return result;
}

// ============================================================================
// Register all endpoints
// ============================================================================
static void register_uri_or_log(httpd_handle_t server, httpd_uri_t *uri, const char *name) {
    esp_err_t err = httpd_register_uri_handler(server, uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register %s: %s", name, esp_err_to_name(err));
    }
}

void signalk_api_register(httpd_handle_t server) {
    // Status
    httpd_uri_t status_uri = {
        .uri = "/api/signalk/status",
        .method = HTTP_GET,
        .handler = signalk_status_handler,
    };
    register_uri_or_log(server, &status_uri, "signalk status");

    // Configuration get
    httpd_uri_t config_get_uri = {
        .uri = "/api/signalk/config",
        .method = HTTP_GET,
        .handler = signalk_config_get_handler,
    };
    register_uri_or_log(server, &config_get_uri, "signalk config get");

    // Configuration set
    httpd_uri_t config_set_uri = {
        .uri = "/api/signalk/config",
        .method = HTTP_POST,
        .handler = signalk_config_set_handler,
    };
    register_uri_or_log(server, &config_set_uri, "signalk config set");

    // Discovery
    httpd_uri_t discover_uri = {
        .uri = "/api/signalk/discover",
        .method = HTTP_POST,
        .handler = signalk_discover_handler,
    };
    register_uri_or_log(server, &discover_uri, "signalk discover");

    // Discovered servers list
    httpd_uri_t servers_uri = {
        .uri = "/api/signalk/servers",
        .method = HTTP_GET,
        .handler = signalk_servers_handler,
    };
    register_uri_or_log(server, &servers_uri, "signalk servers");

    // Connect
    httpd_uri_t connect_uri = {
        .uri = "/api/signalk/connect",
        .method = HTTP_POST,
        .handler = signalk_connect_handler,
    };
    register_uri_or_log(server, &connect_uri, "signalk connect");

    // Disconnect
    httpd_uri_t disconnect_uri = {
        .uri = "/api/signalk/disconnect",
        .method = HTTP_POST,
        .handler = signalk_disconnect_handler,
    };
    register_uri_or_log(server, &disconnect_uri, "signalk disconnect");

    // Clear token
    httpd_uri_t clear_token_uri = {
        .uri = "/api/signalk/clear-token",
        .method = HTTP_POST,
        .handler = signalk_clear_token_handler,
    };
    register_uri_or_log(server, &clear_token_uri, "signalk clear token");

    // Send data
    httpd_uri_t send_data_uri = {
        .uri = "/api/signalk/send",
        .method = HTTP_POST,
        .handler = signalk_send_data_handler,
    };
    register_uri_or_log(server, &send_data_uri, "signalk send data");

    // Test
    httpd_uri_t test_uri = {
        .uri = "/api/signalk/test",
        .method = HTTP_POST,
        .handler = signalk_test_handler,
    };
    register_uri_or_log(server, &test_uri, "signalk test");

    // Subscriptions + cached values
    httpd_uri_t subs_uri = {
        .uri = "/api/signalk/subscriptions",
        .method = HTTP_GET,
        .handler = signalk_subscriptions_handler,
    };
    register_uri_or_log(server, &subs_uri, "signalk subscriptions");

    ESP_LOGI(TAG, "SignalK API endpoints registered");
}
