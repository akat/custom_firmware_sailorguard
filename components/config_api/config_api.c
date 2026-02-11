#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "cJSON.h"

static const char *TAG = "config_api";
static const char *k_config_ns = "config";
static const char *k_schema_path = "/spiffs/config.json";

static esp_err_t read_file_to_buffer(const char *path, char **out, size_t *out_len) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        return ESP_ERR_NOT_FOUND;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    if (size <= 0) {
        fclose(file);
        return ESP_ERR_INVALID_SIZE;
    }
    fseek(file, 0, SEEK_SET);

    char *buffer = (char *)malloc((size_t)size + 1);
    if (!buffer) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    size_t read_bytes = fread(buffer, 1, (size_t)size, file);
    fclose(file);

    if (read_bytes != (size_t)size) {
        free(buffer);
        return ESP_FAIL;
    }

    buffer[size] = '\0';
    *out = buffer;
    *out_len = (size_t)size;
    return ESP_OK;
}

static bool nvs_get_value_str(const char *key, char *out, size_t out_len) {
    nvs_handle_t handle;
    if (nvs_open(k_config_ns, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }

    size_t len = out_len;
    esp_err_t err = nvs_get_str(handle, key, out, &len);
    nvs_close(handle);
    return err == ESP_OK;
}

static esp_err_t nvs_set_value_str(nvs_handle_t handle, const char *key, const char *value) {
    if (!value) {
        return nvs_erase_key(handle, key);
    }
    return nvs_set_str(handle, key, value);
}

static cJSON *schema_load_root(void) {
    char *schema_text = NULL;
    size_t schema_len = 0;
    if (read_file_to_buffer(k_schema_path, &schema_text, &schema_len) != ESP_OK) {
        return NULL;
    }

    cJSON *root = cJSON_Parse(schema_text);
    free(schema_text);
    if (!root) {
        return NULL;
    }

    cJSON *fields = cJSON_GetObjectItem(root, "fields");
    cJSON *sections = cJSON_GetObjectItem(root, "sections");
    if (!cJSON_IsArray(fields) && !cJSON_IsArray(sections)) {
        cJSON_Delete(root);
        return NULL;
    }

    return root;
}

static void add_field_value(cJSON *response, cJSON *field) {
    cJSON *key = cJSON_GetObjectItem(field, "key");
    if (!cJSON_IsString(key)) {
        return;
    }

    const char *type = default_type(cJSON_GetStringValue(cJSON_GetObjectItem(field, "type")));
    char stored[128] = {0};
    if (nvs_get_value_str(key->valuestring, stored, sizeof(stored))) {
        cJSON_AddItemToObject(response, key->valuestring, value_from_string(type, stored));
        return;
    }

    cJSON *def_val = cJSON_GetObjectItem(field, "default");
    if (def_val) {
        cJSON_AddItemToObject(response, key->valuestring, cJSON_Duplicate(def_val, 1));
    }
}

static void store_field_value(nvs_handle_t handle, cJSON *payload, cJSON *field) {
    cJSON *key = cJSON_GetObjectItem(field, "key");
    if (!cJSON_IsString(key)) {
        return;
    }

    cJSON *value = cJSON_GetObjectItem(payload, key->valuestring);
    if (!value) {
        return;
    }

    const char *type = default_type(cJSON_GetStringValue(cJSON_GetObjectItem(field, "type")));
    cJSON *min = cJSON_GetObjectItem(field, "min");
    cJSON *max = cJSON_GetObjectItem(field, "max");
    cJSON *options = cJSON_GetObjectItem(field, "options");

    char value_str[128] = {0};
    if (strcmp(type, "bool") == 0) {
        bool enabled = cJSON_IsTrue(value);
        snprintf(value_str, sizeof(value_str), "%s", enabled ? "true" : "false");
    } else if (strcmp(type, "number") == 0 || strcmp(type, "gpio") == 0) {
        if (!cJSON_IsNumber(value)) {
            return;
        }
        double number = value->valuedouble;
        if (cJSON_IsNumber(min) && number < min->valuedouble) {
            return;
        }
        if (cJSON_IsNumber(max) && number > max->valuedouble) {
            return;
        }
        snprintf(value_str, sizeof(value_str), "%.6g", number);
    } else if (strcmp(type, "select") == 0) {
        if (!cJSON_IsString(value) || !field_matches_option(options, value->valuestring)) {
            return;
        }
        snprintf(value_str, sizeof(value_str), "%s", value->valuestring);
    } else {
        if (!cJSON_IsString(value)) {
            return;
        }
        snprintf(value_str, sizeof(value_str), "%s", value->valuestring);
    }

    if (nvs_set_value_str(handle, key->valuestring, value_str) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to store %s", key->valuestring);
    }
}

static cJSON *value_from_string(const char *type, const char *value) {
    if (!value) {
        return cJSON_CreateNull();
    }

    if (strcmp(type, "bool") == 0) {
        bool enabled = (strcmp(value, "1") == 0 || strcasecmp(value, "true") == 0);
        return cJSON_CreateBool(enabled);
    }

    if (strcmp(type, "number") == 0 || strcmp(type, "gpio") == 0) {
        double number = strtod(value, NULL);
        return cJSON_CreateNumber(number);
    }

    return cJSON_CreateString(value);
}

static const char *default_type(const char *type) {
    return type ? type : "text";
}

static bool field_matches_option(cJSON *options, const char *value) {
    if (!cJSON_IsArray(options) || !value) {
        return true;
    }

    cJSON *option = NULL;
    cJSON_ArrayForEach(option, options) {
        cJSON *opt_value = cJSON_GetObjectItem(option, "value");
        if (cJSON_IsString(opt_value) && strcmp(opt_value->valuestring, value) == 0) {
            return true;
        }
    }

    return false;
}

static esp_err_t config_schema_handler(httpd_req_t *req) {
    char *schema_text = NULL;
    size_t schema_len = 0;
    esp_err_t err = read_file_to_buffer(k_schema_path, &schema_text, &schema_len);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "schema not found");
    }

    httpd_resp_set_type(req, "application/json");
    esp_err_t result = httpd_resp_send(req, schema_text, (ssize_t)schema_len);
    free(schema_text);
    return result;
}

static esp_err_t config_values_get_handler(httpd_req_t *req) {
    cJSON *root = schema_load_root();
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid schema");
    }

    cJSON *response = cJSON_CreateObject();
    if (!response) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    }

    // Handle flat fields
    cJSON *fields = cJSON_GetObjectItem(root, "fields");
    if (cJSON_IsArray(fields)) {
        cJSON *field = NULL;
        cJSON_ArrayForEach(field, fields) {
            add_field_value(response, field);
        }
    }

    // Handle grouped sections
    cJSON *sections = cJSON_GetObjectItem(root, "sections");
    if (cJSON_IsArray(sections)) {
        cJSON *section = NULL;
        cJSON_ArrayForEach(section, sections) {
            cJSON *section_fields = cJSON_GetObjectItem(section, "fields");
            if (cJSON_IsArray(section_fields)) {
                cJSON *field = NULL;
                cJSON_ArrayForEach(field, section_fields) {
                    add_field_value(response, field);
                }
            }
        }
    }

    char *payload = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    cJSON_Delete(root);

    if (!payload) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "serialize failed");
    }

    httpd_resp_set_type(req, "application/json");
    esp_err_t result = httpd_resp_send(req, payload, HTTPD_RESP_USE_STRLEN);
    free(payload);
    return result;
}

static esp_err_t config_values_post_handler(httpd_req_t *req) {
    size_t total_len = req->content_len;
    if (total_len == 0 || total_len > 2048) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body");
    }

    char *buffer = (char *)malloc(total_len + 1);
    if (!buffer) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    }

    size_t received = 0;
    while (received < total_len) {
        int len = httpd_req_recv(req, buffer + received, total_len - received);
        if (len <= 0) {
            free(buffer);
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body");
        }
        received += (size_t)len;
    }
    buffer[received] = '\0';

    cJSON *payload = cJSON_Parse(buffer);
    free(buffer);
    if (!payload || !cJSON_IsObject(payload)) {
        cJSON_Delete(payload);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    }

    cJSON *schema_root = schema_load_root();
    if (!schema_root) {
        cJSON_Delete(payload);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid schema");
    }

    nvs_handle_t handle;
    if (nvs_open(k_config_ns, NVS_READWRITE, &handle) != ESP_OK) {
        cJSON_Delete(payload);
        cJSON_Delete(schema_root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "nvs error");
    }

    // Handle flat fields
    cJSON *fields = cJSON_GetObjectItem(schema_root, "fields");
    if (cJSON_IsArray(fields)) {
        cJSON *field = NULL;
        cJSON_ArrayForEach(field, fields) {
            store_field_value(handle, payload, field);
        }
    }

    // Handle grouped sections
    cJSON *sections = cJSON_GetObjectItem(schema_root, "sections");
    if (cJSON_IsArray(sections)) {
        cJSON *section = NULL;
        cJSON_ArrayForEach(section, sections) {
            cJSON *section_fields = cJSON_GetObjectItem(section, "fields");
            if (cJSON_IsArray(section_fields)) {
                cJSON *field = NULL;
                cJSON_ArrayForEach(field, section_fields) {
                    store_field_value(handle, payload, field);
                }
            }
        }
    }

    nvs_commit(handle);
    nvs_close(handle);
    cJSON_Delete(payload);
    cJSON_Delete(schema_root);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

void config_api_register(httpd_handle_t server) {
    httpd_uri_t schema_uri = {
        .uri = "/api/config/schema",
        .method = HTTP_GET,
        .handler = config_schema_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &schema_uri);

    httpd_uri_t values_get_uri = {
        .uri = "/api/config/values",
        .method = HTTP_GET,
        .handler = config_values_get_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &values_get_uri);

    httpd_uri_t values_post_uri = {
        .uri = "/api/config/values",
        .method = HTTP_POST,
        .handler = config_values_post_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &values_post_uri);
}
