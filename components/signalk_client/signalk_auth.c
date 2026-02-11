#include "signalk_auth.h"
#include "signalk_types.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

// Forward declare storage functions
extern esp_err_t signalk_storage_load_config(signalk_config_t *config);
extern esp_err_t signalk_storage_save_config(const signalk_config_t *config);
extern esp_err_t signalk_set_config(const signalk_config_t *config);
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "signalk_auth";

typedef struct {
    char *data;
    size_t size;
} http_buf_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    http_buf_t *buf = (http_buf_t *)evt->user_data;
    if (!buf) {
        return ESP_OK;
    }

    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        char *next = realloc(buf->data, buf->size + evt->data_len + 1);
        if (!next) {
            return ESP_ERR_NO_MEM;
        }
        buf->data = next;
        memcpy(buf->data + buf->size, evt->data, evt->data_len);
        buf->size += evt->data_len;
        buf->data[buf->size] = '\0';
    }

    return ESP_OK;
}

static esp_err_t http_request_json(const char *method,
                                   const char *url,
                                   const char *body,
                                   char **response) {
    if (!method || !url || !response) {
        return ESP_ERR_INVALID_ARG;
    }

    http_buf_t buf = {0};
    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &buf,
        .timeout_ms = 5000,
        .disable_auto_redirect = false
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_method(client, HTTP_METHOD_GET);
    if (strcmp(method, "POST") == 0) {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
    }

    if (body) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body, (int)strlen(body));
    }

    ESP_LOGI(TAG, "HTTP %s %s", method, url);
    if (body) {
        ESP_LOGI(TAG, "HTTP body: %.256s", body);
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP status: %d", status);
        if (buf.data) {
            ESP_LOGI(TAG, "HTTP response: %.256s", buf.data);
        }
        if (status >= 200 && status < 300) {
            if (buf.data) {
                *response = buf.data;
            } else {
                *response = malloc(3);
                if (*response) {
                    strcpy(*response, "{}");
                } else {
                    err = ESP_ERR_NO_MEM;
                }
            }
        } else {
            err = ESP_FAIL;
        }
    }

    if (err != ESP_OK && buf.data) {
        ESP_LOGW(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        free(buf.data);
    }

    esp_http_client_cleanup(client);
    return err;
}

static void extract_request_id(const char *href, char *out_id, size_t out_size) {
    if (!href || !out_id || out_size == 0) {
        return;
    }
    const char *last = strrchr(href, '/');
    const char *start = last ? last + 1 : href;
    strncpy(out_id, start, out_size - 1);
    out_id[out_size - 1] = '\0';
}

static void parse_access_request(cJSON *json, signalk_auth_request_t *request) {
    if (!json || !request) {
        return;
    }

    cJSON *state = cJSON_GetObjectItem(json, "state");
    cJSON *access_request = cJSON_GetObjectItem(json, "accessRequest");
    cJSON *permission = access_request ? cJSON_GetObjectItem(access_request, "permission") : NULL;
    cJSON *token = access_request ? cJSON_GetObjectItem(access_request, "token") : NULL;

    if (state && cJSON_IsString(state) && strcmp(state->valuestring, "COMPLETED") == 0) {
        if (permission && cJSON_IsString(permission)) {
            if (strcmp(permission->valuestring, "APPROVED") == 0) {
                request->state = AUTH_REQUEST_APPROVED;
            } else if (strcmp(permission->valuestring, "DENIED") == 0) {
                request->state = AUTH_REQUEST_DENIED;
            } else {
                request->state = AUTH_REQUEST_PENDING;
            }
        } else {
            request->state = AUTH_REQUEST_PENDING;
        }
    } else if (state && cJSON_IsString(state)) {
        if (strcmp(state->valuestring, "APPROVED") == 0) {
            request->state = AUTH_REQUEST_APPROVED;
        } else if (strcmp(state->valuestring, "DENIED") == 0) {
            request->state = AUTH_REQUEST_DENIED;
        } else {
            request->state = AUTH_REQUEST_PENDING;
        }
    } else {
        request->state = AUTH_REQUEST_PENDING;
    }

    if (token && cJSON_IsString(token)) {
        strncpy(request->token, token->valuestring, sizeof(request->token) - 1);
    }
}

esp_err_t signalk_auth_request_token(const char *hostname,
                                      uint16_t port,
                                      bool use_ssl,
                                      const char *client_id,
                                      const char *description,
                                      signalk_auth_request_t *request) {
    if (!hostname || !client_id || !request) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Requesting token from %s:%d", hostname, port);

    memset(request, 0, sizeof(signalk_auth_request_t));

    char url[256];
    const char *scheme = use_ssl ? "https" : "http";
    snprintf(url, sizeof(url), "%s://%s:%d/signalk/v1/access/requests", scheme, hostname, port);

    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "clientId", client_id);
    cJSON_AddStringToObject(body, "description", description ? description : "SailorGuard");
    cJSON_AddStringToObject(body, "clientType", "device");
    cJSON *permissions = cJSON_CreateArray();
    cJSON_AddItemToArray(permissions, cJSON_CreateString("readwrite"));
    cJSON_AddItemToObject(body, "permissions", permissions);
    char *payload = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);

    char *response = NULL;
    esp_err_t err = http_request_json("POST", url, payload, &response);
    free(payload);

    if (err != ESP_OK || !response) {
        if (response) {
            free(response);
        }
        return ESP_FAIL;
    }

    cJSON *json = cJSON_Parse(response);
    free(response);
    if (!json) {
        return ESP_FAIL;
    }

    cJSON *request_id = cJSON_GetObjectItem(json, "requestId");
    cJSON *href = cJSON_GetObjectItem(json, "href");

    if (request_id && cJSON_IsString(request_id)) {
        strncpy(request->request_id, request_id->valuestring, sizeof(request->request_id) - 1);
    } else if (href && cJSON_IsString(href)) {
        extract_request_id(href->valuestring, request->request_id, sizeof(request->request_id));
        strncpy(request->approval_url, href->valuestring, sizeof(request->approval_url) - 1);
    }

    parse_access_request(json, request);

    cJSON_Delete(json);

    if (request->request_id[0] == '\0') {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t signalk_auth_check_request(const char *hostname,
                                      uint16_t port,
                                      bool use_ssl,
                                      const char *request_id,
                                      signalk_auth_request_t *request) {
    if (!hostname || !request_id || !request) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Checking auth request %s", request_id);

    memset(request, 0, sizeof(signalk_auth_request_t));
    strncpy(request->request_id, request_id, sizeof(request->request_id) - 1);

    char url[256];
    const char *scheme = use_ssl ? "https" : "http";
    snprintf(url, sizeof(url), "%s://%s:%d/signalk/v1/access/requests/%s", scheme, hostname, port, request_id);

    char *response = NULL;
    esp_err_t err = http_request_json("GET", url, NULL, &response);
    if (err != ESP_OK || !response) {
        if (response) {
            free(response);
        }
        return ESP_FAIL;
    }

    cJSON *json = cJSON_Parse(response);
    free(response);
    if (!json) {
        return ESP_FAIL;
    }

    parse_access_request(json, request);

    cJSON_Delete(json);
    return ESP_OK;
}

esp_err_t signalk_auth_validate_token(const char *hostname,
                                       uint16_t port,
                                       bool use_ssl,
                                       const char *token) {
    if (!hostname || !token) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Validating token with %s:%d (stub - Phase B)", hostname, port);

    // TODO: Send WebSocket connection with token
    // or HTTP request with Authorization header

    return ESP_OK;
}

esp_err_t signalk_auth_clear_token(void) {
    ESP_LOGI(TAG, "Clearing stored authentication token");
    
    // Load current config
    signalk_config_t config;
    esp_err_t err = signalk_storage_load_config(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load config: %s", esp_err_to_name(err));
        return err;
    }
    
    // Clear the token
    config.token[0] = '\0';
    
    // Save config back to NVS
    err = signalk_storage_save_config(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save config: %s", esp_err_to_name(err));
        return err;
    }
    
    // Update in-memory config in the client
    err = signalk_set_config(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update in-memory config: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "Token cleared successfully - auth will restart");
    return ESP_OK;
}
