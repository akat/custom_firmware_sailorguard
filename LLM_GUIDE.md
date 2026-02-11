# LLM Assistant Guide

> **Quick reference for AI assistants working on this ESP32 firmware project**

## Core Principles

1. **Component-based architecture** - Each feature is a separate component
2. **Configuration via NVS** - User settings stored persistently
3. **REST API pattern** - Logic component + API component  
4. **SPIFFS for static** - Web UI and config schema in flash

## File Locations

- `components/*/` - Firmware components (C code)
- `config/config.json` - Configuration schema (edit here)
- `src/main.c` - Application entry point
- `frontend/src/` - Preact UI source
- `data/` - Auto-generated, don't edit directly

## Common Patterns

### Add Configuration Field

1. Edit `config/config.json`:
```json
{
  "key": "my_setting",
  "label": "My Setting",
  "type": "number|bool|text|select|gpio",
  "default": value
}
```

2. Rebuild frontend: `cd frontend && npm run build:esp`
3. Upload: `pio run -t uploadfs`
4. Use in C:
```c
#include "config_api.h"
int val = config_get_int_or_default("my_setting", default);
```

### Add New Component

**Structure:**
```
components/my_feature/
├── CMakeLists.txt
├── my_feature.c
└── include/my_feature.h
```

**CMakeLists.txt:**
```cmake
idf_component_register(
    SRCS "my_feature.c"
    INCLUDE_DIRS "include"
    REQUIRES config_api driver  # Dependencies
)
```

**Header (include/my_feature.h):**
```c
#pragma once
#include "esp_err.h"

esp_err_t my_feature_init(void);
esp_err_t my_feature_do_something(int param);
```

**Implementation (my_feature.c):**
```c
#include "my_feature.h"
#include "config_api.h"
#include "esp_log.h"

static const char *TAG = "my_feature";

esp_err_t my_feature_init(void) {
    int gpio = config_get_int_or_default("my_gpio", 5);
    ESP_LOGI(TAG, "Initialized on GPIO %d", gpio);
    return ESP_OK;
}
```

**Register in src/main.c:**
```c
#include "my_feature.h"

void app_main(void) {
    // ... existing code ...
    my_feature_init();
}
```

**Update src/CMakeLists.txt:**
```cmake
idf_component_register(
    SRCS "main.c"
    REQUIRES my_feature  # Add dependency
)
```

### Add REST API Endpoint

**Create API component:**
```
components/my_api/
├── CMakeLists.txt
├── my_api.c
└── include/my_api.h
```

**my_api.c:**
```c
#include "esp_http_server.h"
#include "cJSON.h"

static esp_err_t my_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    
    char *response = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    httpd_resp_set_type(req, "application/json");
    esp_err_t result = httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    free(response);
    return result;
}

void my_api_register(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri = "/api/my/endpoint",
        .method = HTTP_GET,
        .handler = my_handler,
    };
    httpd_register_uri_handler(server, &uri);
}
```

**Register in src/main.c:**
```c
#include "my_api.h"

void app_main(void) {
    // ... after HTTP server starts ...
    httpd_handle_t server = start_http_server(...);
    my_api_register(server);
}
```

## Config API Functions

```c
#include "config_api.h"

// With default fallback (recommended)
int config_get_int_or_default(const char *key, int default_value);
bool config_get_bool_or_default(const char *key, bool default_value);

// With error checking
esp_err_t config_get_int(const char *key, int *out_value);
esp_err_t config_get_bool(const char *key, bool *out_value);
esp_err_t config_get_string(const char *key, char *out_value, size_t max_len);
```

## Build Commands

```bash
# Full build
cd frontend && npm run build:esp && cd ..
pio run -t upload          # Firmware
pio run -t uploadfs        # SPIFFS

# Firmware only
pio run -t upload

# SPIFFS only (after frontend changes)
cd frontend && npm run build:esp && cd ..
pio run -t uploadfs
```

## Decision Tree for Adding Features

**User wants to:**

→ **Add configurable setting**
  1. Add to `config/config.json`
  2. Rebuild frontend + upload SPIFFS
  3. Use `config_get_*()` in firmware

→ **Add hardware control**
  1. Create component in `components/`
  2. Add init function
  3. Call from `main.c`

→ **Add REST API**
  1. Create API component
  2. Add handlers
  3. Register with server in `main.c`

→ **Store persistent data**
  - User-configurable? → Use config system
  - System state? → Use NVS directly
  - Temporary? → Use RAM

→ **Add UI feature**
  1. Edit `frontend/src/`
  2. Rebuild: `npm run build:esp`
  3. Upload: `pio run -t uploadfs`

## Common Mistakes to Avoid

❌ Editing `data/` directly (it's auto-generated)
❌ Forgetting to add component to CMakeLists REQUIRES
❌ Not freeing allocated memory (malloc/cJSON)
❌ Ignoring return values from esp_err_t functions
❌ Not providing default values in config
❌ Creating circular dependencies between components

## Quick Checks

**Before suggesting code:**
- [ ] Does component need to be added to REQUIRES?
- [ ] Are error returns checked?
- [ ] Is allocated memory freed?
- [ ] Are config defaults provided?
- [ ] Is the pattern consistent with existing code?

## Useful ESP-IDF APIs

```c
// Logging
ESP_LOGI(TAG, "Info: %d", value);
ESP_LOGE(TAG, "Error: %s", esp_err_to_name(err));

// GPIO
#include "driver/gpio.h"
gpio_set_direction(pin, GPIO_MODE_OUTPUT);
gpio_set_level(pin, 1);

// NVS (raw storage)
#include "nvs_flash.h"
nvs_handle_t handle;
nvs_open("namespace", NVS_READWRITE, &handle);
nvs_set_i32(handle, "key", 123);
nvs_commit(handle);
nvs_close(handle);

// Delays
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
vTaskDelay(pdMS_TO_TICKS(1000));  // 1 second

// JSON
#include "cJSON.h"
cJSON *obj = cJSON_CreateObject();
cJSON_AddNumberToObject(obj, "value", 42);
char *str = cJSON_PrintUnformatted(obj);
cJSON_Delete(obj);
free(str);
```

## Example: Complete Feature Addition

**Task:** Add LED control with GPIO config and API

**1. Config (config/config.json):**
```json
{
  "key": "led_gpio",
  "type": "gpio",
  "default": 2
}
```

**2. Component (components/led_control/):**
```c
// include/led_control.h
#pragma once
esp_err_t led_init(void);
esp_err_t led_set(bool on);

// led_control.c
#include "led_control.h"
#include "config_api.h"
#include "driver/gpio.h"

static int led_pin = -1;

esp_err_t led_init(void) {
    led_pin = config_get_int_or_default("led_gpio", 2);
    gpio_set_direction(led_pin, GPIO_MODE_OUTPUT);
    return ESP_OK;
}

esp_err_t led_set(bool on) {
    return gpio_set_level(led_pin, on ? 1 : 0);
}
```

**3. API (components/led_api/):**
```c
// led_api.c
static esp_err_t led_handler(httpd_req_t *req) {
    // Parse POST body for {"state": true/false}
    // Call led_set()
    // Return JSON response
}

void led_api_register(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri = "/api/led/control",
        .method = HTTP_POST,
        .handler = led_handler,
    };
    httpd_register_uri_handler(server, &uri);
}
```

**4. Register (src/main.c):**
```c
#include "led_control.h"
#include "led_api.h"

void app_main(void) {
    // ... init ...
    led_init();
    httpd_handle_t server = start_http_server(...);
    led_api_register(server);
}
```

Done! LED is now configurable and controllable via API.
