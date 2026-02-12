# Signal K Integration - Current Status

## ✅ What Works

### 1. Authentication
- ✅ Auto-request access tokens
- ✅ Poll for approval (AUTH_PENDING state)
- ✅ Save token to NVS
- ✅ Clear token functionality
- ✅ Manual hostname/port configuration
- ✅ Status shows "CONNECTED" when authenticated

### 2. HTTP Data Publishing (Write-Only)
```c
#include "signalk_publisher.h"

// Send temperature to Signal K
signalk_publish_temperature("environment.outside.temperature", 22.5f, "ESP32");

// Send voltage
signalk_publish_voltage("electrical.batteries.house.voltage", 12.6f, "ESP32");

// Send boolean
signalk_publish_bool("electrical.switches.cabin.state", true, "ESP32");
```

### 3. HTTP Data Reading (Polling)
```c
#include "signalk_subscriber.h"

// Read a value from Signal K
signalk_data_t data;
if (signalk_read_value("environment.outside.temperature", &data) == ESP_OK) {
    if (data.type == SIGNALK_VALUE_FLOAT) {
        float temp_kelvin = data.value.f;
        float temp_celsius = temp_kelvin - 273.15f;
        printf("Temperature: %.1f°C\n", temp_celsius);
    }
}

// Get raw JSON
char json[1024];
if (signalk_get_json("navigation.position", json, sizeof(json)) == ESP_OK) {
    printf("Position JSON: %s\n", json);
}
```

### 4. Frontend UI
- ✅ Configuration panel
- ✅ Status display (shows CONNECTED when authenticated)
- ✅ Send test data button
- ✅ Clear token button

## ❌ What Doesn't Work

### 1. Real-Time Subscriptions
**Problem**: WebSocket component (`esp_websocket_client`) not available in your ESP-IDF build

**Impact**:
- ❌ Cannot receive real-time delta updates from Signal K
- ❌ Cannot subscribe to paths for automatic updates
- ❌ Must manually poll using `signalk_read_value()` to get server data

**Workaround**: Use HTTP polling:
```c
// Poll temperature every 5 seconds
void sensor_task(void *param) {
    while (1) {
        signalk_data_t temp;
        if (signalk_read_value("environment.outside.temperature", &temp) == ESP_OK) {
            // Use the value
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
```

### 2. mDNS Auto-Discovery
**Problem**: mDNS component not included (stubbed out)

**Impact**:
- ❌ Cannot auto-discover Signal K servers on network
- ❌ Must manually enter hostname/port

**Workaround**: Manual configuration via web UI

### 3. Bidirectional Communication
**Problem**: Without WebSocket, only HTTP request-response works

**Impact**:
- ✅ Can WRITE data to Signal K (works!)
- ✅ Can READ data from Signal K on-demand (works!)
- ❌ Cannot receive push notifications when data changes
- ❌ Server cannot initiate communication to device

## 📊 Comparison Table

| Feature | HTTP (Working) | WebSocket (Not Available) |
|---------|---------------|---------------------------|
| Send data to Signal K | ✅ Yes | ✅ Yes |
| Read data from Signal K | ✅ Yes (polling) | ✅ Yes (real-time) |
| Subscribe to changes | ❌ No | ✅ Yes |
| Real-time updates | ❌ No | ✅ Yes |
| Bandwidth efficient | ⚠️ Moderate | ✅ High |
| Connection overhead | ⚠️ Per request | ✅ Persistent |

## 🔧 How to Enable WebSocket (Future)

To enable full real-time subscriptions, you need the `esp_websocket_client` component:

1. **Check ESP-IDF version** - Requires ESP-IDF 4.4+
2. **Enable in sdkconfig**:
   ```
   CONFIG_ESP_WEBSOCKET_CLIENT=y
   CONFIG_WS_TRANSPORT=y
   ```
3. **Add to platformio.ini** if using PlatformIO
4. Clean build and recompile

Once available, the system will automatically:
- ✅ Establish WebSocket connection after authentication
- ✅ Show status as "STREAMING" instead of "CONNECTED"
- ✅ Receive real-time delta updates
- ✅ Enable subscription callbacks

## 💡 Example Usage (Current HTTP-Only Mode)

### Send Sensor Data
```c
#include "signalk_publisher.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void sensor_task(void *param) {
    while (1) {
        // Read sensors
        float temp = read_temperature();
        float voltage = read_battery_voltage();
        bool pump_running = read_pump_state();
        
        // Publish to Signal K
        signalk_publish_temperature("environment.outside.temperature", temp, "ESP32");
        signalk_publish_voltage("electrical.batteries.house.voltage", voltage, "ESP32");
        signalk_publish_bool("electrical.pumps.bilge.state", pump_running, "ESP32");
        
        vTaskDelay(pdMS_TO_TICKS(10000)); // Every 10 seconds
    }
}
```

### Monitor Remote Values
```c
#include "signalk_subscriber.h"

void monitor_task(void *param) {
    while (1) {
        signalk_data_t depth;
        if (signalk_read_value("environment.depth.belowTransducer", &depth) == ESP_OK) {
            if (depth.type == SIGNALK_VALUE_FLOAT) {
                float depth_meters = depth.value.f;
                if (depth_meters < 2.0f) {
                    ESP_LOGW(TAG, "Shallow water! Depth: %.1fm", depth_meters);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(3000)); // Poll every 3 seconds
    }
}
```

## 🎯 Summary

**Current system is production-ready for:**
- ✅ Sending sensor data to Signal K
- ✅ Manual data queries from Signal K
- ✅ Authentication and token management
- ✅ HTTP-based integration

**Limitations:**
- ⚠️ No real-time push notifications from server
- ⚠️ Must poll for remote data changes
- ⚠️ No mDNS auto-discovery
- ⚠️ Higher bandwidth than WebSocket

**Recommendation**: Use HTTP-only mode for periodic sensor updates and occasional reads. If you need real-time bidirectional communication, enable the WebSocket component in your build.
