# Architecture Guide for ESP32 Custom Firmware

> **For LLMs and Developers**: This document explains the project architecture, patterns, and how to extend the firmware with new features.

## Table of Contents
1. [Project Overview](#project-overview)
2. [Architecture Patterns](#architecture-patterns)
3. [Component System](#component-system)
4. [Configuration System](#configuration-system)
5. [Adding New Features](#adding-new-features)
6. [Common Tasks](#common-tasks)
7. [Best Practices](#best-practices)

---

## Project Overview

This is an ESP32-S3 firmware project using ESP-IDF with a Preact web dashboard. The firmware follows a **component-based architecture** where each major feature is isolated in its own component.

### Key Technologies
- **Firmware**: ESP-IDF (C language)
- **Web UI**: Preact + Vite
- **Storage**: SPIFFS (static files) + NVS (persistent data)
- **HTTP Server**: ESP-IDF HTTP server component
- **Build System**: PlatformIO + CMake

### Directory Structure
```
.
├── src/                    # Main application (app_main entry point)
├── components/             # Reusable firmware components
│   ├── wifi_manager/       # WiFi connection logic
│   ├── wifi_api/           # WiFi REST endpoints
│   ├── config_api/         # Configuration system
│   ├── status_api/         # Device status endpoints
│   ├── http_server/        # HTTP server initialization
│   └── spiffs_store/       # SPIFFS mount utilities
├── config/                 # Device configuration schema (source)
├── frontend/               # Preact UI source code
├── data/                   # SPIFFS filesystem (auto-generated)
└── platformio.ini          # Build configuration
```

---

## Architecture Patterns

### 1. Component-Based Design

Each feature is encapsulated in a **component** with:
- **Public API** (`include/*.h`) - Functions exposed to other components
- **Implementation** (`*.c`) - Internal logic
- **Dependencies** (`CMakeLists.txt`) - Required components

**Example: WiFi Manager Component**
```
components/wifi_manager/
├── CMakeLists.txt          # Build config + dependencies
├── wifi_manager.c          # Implementation
└── include/
    └── wifi_manager.h      # Public API
```

### 2. API-Component Pattern

HTTP API endpoints are separated from business logic:
- **Logic Component** (e.g., `wifi_manager`) - Core functionality
- **API Component** (e.g., `wifi_api`) - HTTP handlers that use the logic

This separation allows:
- Testing logic without HTTP server
- Reusing logic from multiple places
- Clear separation of concerns

### 3. Storage Layers

**SPIFFS** (read-only at runtime):
- Static web UI files (HTML, CSS, JS)
- Configuration schema (`config.json`)
- Embedded assets

**NVS** (read-write persistent storage):
- WiFi credentials
- Configuration values (set by user via UI)
- Device settings

**RAM** (volatile):
- Runtime state
- Connection status
- Active sessions

---

## Component System

### Creating a New Component

**Step 1: Create Directory Structure**
```bash
mkdir -p components/my_feature/{include}
touch components/my_feature/CMakeLists.txt
touch components/my_feature/my_feature.c
touch components/my_feature/include/my_feature.h
```

**Step 2: Write CMakeLists.txt**
```cmake
idf_component_register(
    SRCS "my_feature.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server config_api  # Add dependencies here
)
```

**Step 3: Define Public API (include/my_feature.h)**
```c
#pragma once

#include "esp_err.h"

/**
 * @brief Initialize my feature
 * @return ESP_OK on success
 */
esp_err_t my_feature_init(void);

/**
 * @brief Do something useful
 * @param param Input parameter
 * @return ESP_OK on success
 */
esp_err_t my_feature_do_something(int param);
```

**Step 4: Implement (my_feature.c)**
```c
#include "my_feature.h"
#include "config_api.h"
#include "esp_log.h"

static const char *TAG = "my_feature";

esp_err_t my_feature_init(void) {
    ESP_LOGI(TAG, "Initializing my feature");
    
    // Read configuration
    int gpio = config_get_int_or_default("my_gpio", 5);
    
    // Initialize hardware/logic here
    
    ESP_LOGI(TAG, "My feature initialized on GPIO %d", gpio);
    return ESP_OK;
}

esp_err_t my_feature_do_something(int param) {
    ESP_LOGI(TAG, "Doing something with param: %d", param);
    // Implementation here
    return ESP_OK;
}
```

**Step 5: Use in main.c**
```c
#include "my_feature.h"

void app_main(void) {
    // ... existing init code ...
    
    // Initialize your feature
    my_feature_init();
    
    // ... rest of app_main ...
}
```

**Step 6: Update src/CMakeLists.txt**
```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS ""
    REQUIRES config_api http_server my_feature  # Add here
)
```

---

## Configuration System

### How It Works

1. **Schema** defined in `config/config.json` (field types, defaults, validation)
2. **Schema copied** to `data/config.json` during frontend build
3. **Device reads** schema from `/spiffs/config.json` at runtime
4. **User values** stored in NVS (persistent across reboots)
5. **Components read** values using `config_api` helper functions

### Adding a Configuration Field

**Step 1: Edit config/config.json**
```json
{
  "sections": [
    {
      "title": "My Feature Settings",
      "fields": [
        {
          "key": "my_gpio",
          "label": "Feature GPIO Pin",
          "type": "gpio",
          "default": 5,
          "min": 0,
          "max": 39,
          "help": "GPIO pin for my feature"
        },
        {
          "key": "my_enabled",
          "label": "Enable My Feature",
          "type": "bool",
          "default": true
        },
        {
          "key": "my_mode",
          "label": "Feature Mode",
          "type": "select",
          "default": "fast",
          "options": [
            {"label": "Fast", "value": "fast"},
            {"label": "Slow", "value": "slow"}
          ]
        }
      ]
    }
  ]
}
```

**Step 2: Rebuild Frontend**
```bash
cd frontend
npm run build:esp
cd ..
pio run -t uploadfs
```

**Step 3: Use in Firmware**
```c
#include "config_api.h"

void my_feature_init(void) {
    // Read values with defaults
    int gpio = config_get_int_or_default("my_gpio", 5);
    bool enabled = config_get_bool_or_default("my_enabled", true);
    
    char mode[32];
    if (config_get_string("my_mode", mode, sizeof(mode)) == ESP_OK) {
        if (strcmp(mode, "fast") == 0) {
            // Fast mode
        } else {
            // Slow mode
        }
    }
}
```

### Configuration Field Types

| Type | JSON Type | Use For | C Function |
|------|-----------|---------|------------|
| `number` | Number | Integers, counts, delays | `config_get_int()` |
| `gpio` | Number | GPIO pin numbers | `config_get_int()` |
| `bool` | Boolean | On/off, enable/disable | `config_get_bool()` |
| `text` | String | Names, labels, IDs | `config_get_string()` |
| `select` | String | Dropdown choices | `config_get_string()` |

---

## Adding New Features

### Example: Adding a Motor Control Feature

**Task**: Add a motor that can be controlled via API and configured via UI.

#### 1. Create Motor Component

**components/motor_control/CMakeLists.txt**:
```cmake
idf_component_register(
    SRCS "motor_control.c"
    INCLUDE_DIRS "include"
    REQUIRES driver config_api
)
```

**components/motor_control/include/motor_control.h**:
```c
#pragma once
#include "esp_err.h"

esp_err_t motor_control_init(void);
esp_err_t motor_set_speed(int speed);  // 0-100
int motor_get_speed(void);
```

**components/motor_control/motor_control.c**:
```c
#include "motor_control.h"
#include "config_api.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "motor";
static int current_speed = 0;
static int motor_gpio = -1;

esp_err_t motor_control_init(void) {
    // Read GPIO from config
    motor_gpio = config_get_int_or_default("motor_gpio", 12);
    bool motor_enabled = config_get_bool_or_default("motor_enabled", true);
    
    if (!motor_enabled) {
        ESP_LOGI(TAG, "Motor disabled in config");
        return ESP_OK;
    }
    
    // Setup PWM for motor control
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
    };
    ledc_timer_config(&timer);
    
    ledc_channel_config_t channel = {
        .gpio_num = motor_gpio,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
    };
    ledc_channel_config(&channel);
    
    ESP_LOGI(TAG, "Motor initialized on GPIO %d", motor_gpio);
    return ESP_OK;
}

esp_err_t motor_set_speed(int speed) {
    if (speed < 0 || speed > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    
    current_speed = speed;
    uint32_t duty = (speed * 255) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    
    ESP_LOGI(TAG, "Motor speed set to %d%%", speed);
    return ESP_OK;
}

int motor_get_speed(void) {
    return current_speed;
}
```

#### 2. Create Motor API Component

**components/motor_api/CMakeLists.txt**:
```cmake
idf_component_register(
    SRCS "motor_api.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server motor_control cJSON
)
```

**components/motor_api/include/motor_api.h**:
```c
#pragma once
#include "esp_http_server.h"

void motor_api_register(httpd_handle_t server);
```

**components/motor_api/motor_api.c**:
```c
#include "motor_api.h"
#include "motor_control.h"
#include "cJSON.h"
#include "esp_log.h"

static const char *TAG = "motor_api";

static esp_err_t motor_status_handler(httpd_req_t *req) {
    int speed = motor_get_speed();
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "speed", speed);
    
    char *response = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    httpd_resp_set_type(req, "application/json");
    esp_err_t result = httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    free(response);
    return result;
}

static esp_err_t motor_control_handler(httpd_req_t *req) {
    // Read POST body
    char buffer[100];
    int ret = httpd_req_recv(req, buffer, sizeof(buffer));
    if (ret <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
    }
    buffer[ret] = '\0';
    
    // Parse JSON
    cJSON *root = cJSON_Parse(buffer);
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    }
    
    cJSON *speed_json = cJSON_GetObjectItem(root, "speed");
    if (!cJSON_IsNumber(speed_json)) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing speed");
    }
    
    int speed = speed_json->valueint;
    cJSON_Delete(root);
    
    // Set motor speed
    if (motor_set_speed(speed) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid speed");
    }
    
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

void motor_api_register(httpd_handle_t server) {
    httpd_uri_t status_uri = {
        .uri = "/api/motor/status",
        .method = HTTP_GET,
        .handler = motor_status_handler,
    };
    httpd_register_uri_handler(server, &status_uri);
    
    httpd_uri_t control_uri = {
        .uri = "/api/motor/control",
        .method = HTTP_POST,
        .handler = motor_control_handler,
    };
    httpd_register_uri_handler(server, &control_uri);
    
    ESP_LOGI(TAG, "Motor API registered");
}
```

#### 3. Add Configuration Fields

**config/config.json**:
```json
{
  "sections": [
    {
      "title": "Motor Control",
      "description": "Motor configuration and settings",
      "fields": [
        {
          "key": "motor_gpio",
          "label": "Motor Control GPIO",
          "type": "gpio",
          "default": 12,
          "min": 0,
          "max": 39,
          "help": "GPIO pin for motor PWM control"
        },
        {
          "key": "motor_enabled",
          "label": "Enable Motor",
          "type": "bool",
          "default": true,
          "help": "Enable or disable motor control"
        }
      ]
    }
  ]
}
```

#### 4. Register in Main Application

**src/main.c**:
```c
#include "motor_control.h"
#include "motor_api.h"

void app_main(void) {
    // ... existing init ...
    
    // Initialize motor
    motor_control_init();
    
    // Start HTTP server
    httpd_handle_t server = start_http_server("/spiffs", wifi_manager_get_ap_netif());
    
    // Register motor API
    motor_api_register(server);
    
    // ... rest of code ...
}
```

**src/CMakeLists.txt**:
```cmake
idf_component_register(
    SRCS "main.c"
    REQUIRES motor_control motor_api  # Add dependencies
)
```

#### 5. Build and Upload

```bash
# Rebuild frontend to include new config fields
cd frontend && npm run build:esp && cd ..

# Build and upload firmware
pio run -t upload

# Upload SPIFFS with new config
pio run -t uploadfs
```

---

## Common Tasks

### Task 1: Add a New REST API Endpoint

1. **Identify the component** (create new API component if needed)
2. **Add handler function** to the API component
3. **Register URI** in the `*_api_register()` function
4. **Return JSON** using cJSON library

### Task 2: Read a Configuration Value

```c
#include "config_api.h"

int value = config_get_int_or_default("my_key", 42);
bool enabled = config_get_bool_or_default("feature_enabled", true);
```

### Task 3: Store Persistent Data (Non-Config)

```c
#include "nvs_flash.h"
#include "nvs.h"

void save_to_nvs(void) {
    nvs_handle_t handle;
    nvs_open("my_namespace", NVS_READWRITE, &handle);
    nvs_set_i32(handle, "my_value", 123);
    nvs_commit(handle);
    nvs_close(handle);
}
```

### Task 4: Add a GPIO Output

```c
#include "driver/gpio.h"

gpio_set_direction(GPIO_NUM_5, GPIO_MODE_OUTPUT);
gpio_set_level(GPIO_NUM_5, 1);  // High
gpio_set_level(GPIO_NUM_5, 0);  // Low
```

---

## Best Practices

### 1. Naming Conventions

- **Components**: `snake_case` (e.g., `wifi_manager`, `motor_control`)
- **Functions**: `component_action` (e.g., `motor_set_speed`, `wifi_manager_init`)
- **Constants**: `UPPER_SNAKE_CASE` (e.g., `MAX_RETRIES`)
- **Config keys**: `snake_case` (e.g., `motor_gpio`, `report_interval`)

### 2. Error Handling

Always check return values:
```c
esp_err_t err = my_function();
if (err != ESP_OK) {
    ESP_LOGE(TAG, "Function failed: %s", esp_err_to_name(err));
    return err;
}
```

### 3. Logging

Use appropriate log levels:
```c
ESP_LOGE(TAG, "Critical error");    // Errors
ESP_LOGW(TAG, "Warning");            // Warnings
ESP_LOGI(TAG, "Info message");       // Information
ESP_LOGD(TAG, "Debug details");      // Debug (disabled in production)
ESP_LOGV(TAG, "Verbose trace");      // Verbose (very detailed)
```

### 4. Configuration Philosophy

- **Always provide defaults** - System should work without user configuration
- **Validate ranges** - Use min/max in config.json
- **Document in help text** - Explain units and impact
- **Group related fields** - Use sections in config.json

### 5. Component Dependencies

- **Minimize dependencies** - Only require what you need
- **Avoid circular dependencies** - Component A → B, B → C, C ❌→ A
- **Use interfaces** - Define clear public APIs in headers

### 6. Memory Management

- **Free allocated memory** - Always `free()` what you `malloc()`
- **Check allocations** - Verify `malloc()` didn't return NULL
- **Use stack when possible** - For small buffers, prefer stack allocation
- **Monitor heap usage** - Use `esp_get_free_heap_size()`

---

## Quick Reference for LLMs

**When asked to add a feature:**
1. Create a component in `components/`
2. Add configuration fields to `config/config.json`
3. Create API component if REST endpoints needed
4. Register in `main.c`
5. Update CMakeLists.txt dependencies

**When asked to access configuration:**
```c
#include "config_api.h"
int val = config_get_int_or_default("key", default);
```

**When asked to add API endpoint:**
1. Create handler function
2. Register in `*_api_register()`
3. Use cJSON for JSON responses

**When asked to store data:**
- **User-configurable** → Add to config.json + use config_api
- **System state** → Use NVS directly
- **Temporary** → Use RAM variables

**When reading existing code:**
- `components/` = Reusable modules
- `src/` = Main application entry
- `config/` = Configuration schema source
- `data/` = Auto-generated (don't edit directly)
