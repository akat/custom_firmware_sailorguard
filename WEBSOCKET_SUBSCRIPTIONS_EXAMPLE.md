# WebSocket Subscriptions - Real-Time Signal K Deltas

## ✅ Διορθώθηκε! WebSocket τώρα **ΛΕΙΤΟΥΡΓΕΙ**

### Τι άλλαξε;

1. **Προσθέσαμε `esp_websocket_client` στο CMakeLists.txt** - Αυτό ήταν το πρόβλημα!
2. **Υλοποιήσαμε πλήρες subscription system** με real-time delta parsing
3. **Callback-based architecture** - Δεν χρειάζεται polling, παίρνεις updates όταν αλλάζουν

---

## 📝 Παράδειγμα Χρήσης

```c
#include "esp_log.h"
#include "signalk_subscriber.h"

static const char *TAG = "my_app";

// Callback function που καλείται όταν λάβεις delta update
void temperature_updated(const char *path, 
                        const signalk_data_t *data, 
                        void *user_data) {
    if (data->type == SIGNALK_VALUE_FLOAT) {
        float temp_kelvin = data->value.f;
        float temp_celsius = temp_kelvin - 273.15f;
        
        ESP_LOGI(TAG, "🌡️  Temperature at %s: %.1f°C", 
                 path, temp_celsius);
        
        // Do something with the temperature
        // Update display, trigger alarm, etc.
    }
}

void depth_updated(const char *path, 
                   const signalk_data_t *data, 
                   void *user_data) {
    if (data->type == SIGNALK_VALUE_FLOAT) {
        float depth = data->value.f;
        ESP_LOGI(TAG, "📊 Depth: %.1f meters", depth);
        
        if (depth < 2.0f) {
            ESP_LOGW(TAG, "⚠️  SHALLOW WATER!");
        }
    }
}

void position_updated(const char *path, 
                     const signalk_data_t *data, 
                     void *user_data) {
    if (data->type == SIGNALK_VALUE_POSITION) {
        ESP_LOGI(TAG, "🗺️  Position: %.6f, %.6f",
                 data->value.pos.latitude,
                 data->value.pos.longitude);
    }
}

void app_main(void) {
    // ... Initialize WiFi and Signal K client ...
    
    // Subscribe to temperature updates
    // Update every 5 seconds (don't spam the server)
    signalk_subscribe("environment.outside.temperature",
                     temperature_updated,
                     5000,  // period_ms
                     NULL); // user_data
    
    // Subscribe to depth
    signalk_subscribe("environment.depth.belowTransducer",
                     depth_updated,
                     3000,
                     NULL);
    
    // Subscribe to position
    signalk_subscribe("navigation.position",
                     position_updated,
                     10000,
                     NULL);
    
    // Now your callbacks will be called automatically when
    // the Signal K server sends delta messages!
    
    while (1) {
        // Your main app code here
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    // When done, unsubscribe
    signalk_unsubscribe("environment.outside.temperature");
    signalk_unsubscribe("environment.depth.belowTransducer");
    signalk_unsubscribe("navigation.position");
}
```

---

## 🔄 Πώς λειτουργεί;

### 1️⃣ Σύνδεση WebSocket

```
[ESP32] --websocket--> [Signal K Server]
   ✓ Connected
   ✓ Token sent in Authorization header
   ✓ Ready for subscriptions
```

### 2️⃣ Αποστολή Subscription Request

```json
{
  "context": "vessels.self",
  "subscribe": [
    {
      "path": "environment.outside.temperature",
      "period": 5000,
      "format": "delta",
      "policy": "instant"
    }
  ]
}
```

### 3️⃣ Λήψη Real-Time Deltas

```json
{
  "context": "vessels.self",
  "updates": [
    {
      "source": {
        "device": "signalk-server"
      },
      "timestamp": "2026-02-12T10:30:45.123Z",
      "values": [
        {
          "path": "environment.outside.temperature",
          "value": 295.37
        }
      ]
    }
  ]
}
```

### 4️⃣ Κλήση Callback

```
temperature_updated("environment.outside.temperature", 
                   &data{type: FLOAT, value: 295.37},
                   NULL)
```

---

## 📊 Σύγκριση: HTTP Polling vs WebSocket

| Χαρακτηριστικό | HTTP Polling | WebSocket |
|---|---|---|
| Real-time | ❌ Καθυστέρηση | ✅ Instantly |
| Bandwidth | ❌ Υψηλό | ✅ Χαμηλό |
| Server Load | ❌ Υψηλό | ✅ Χαμηλό |
| Λήψη ενημερώσεων | Manual poll | Automatic callback |
| Σύνδεση | Per request | Persistent |
| CPU | ❌ Task polling | ✅ Event-driven |

---

## 🎯 Subscription Types

### Εγγραφή σε μία τιμή

```c
signalk_subscribe("environment.outside.temperature",
                 temp_callback,
                 5000,  // Update every 5 seconds
                 NULL);
```

### Εγγραφή σε πολλές τιμές

```c
signalk_subscribe("environment.depth.belowTransducer", depth_cb, 3000, NULL);
signalk_subscribe("navigation.position", pos_cb, 10000, NULL);
signalk_subscribe("navigation.speedOverGround", speed_cb, 2000, NULL);
signalk_subscribe("electrical.batteries.house.voltage", volt_cb, 5000, NULL);
```

### Wildcard subscriptions (if server supports)

```c
// Όλες τις τιμές περιβάλλοντος
signalk_subscribe("environment.*", env_cb, 5000, NULL);

// Όλες τις τιμές ναυσιπλοΐας
signalk_subscribe("navigation.*", nav_cb, 5000, NULL);
```

---

## 🔧 Advanced: Custom User Data

```c
// Per-subscription user data
typedef struct {
    int alert_threshold;
    char name[32];
} subscriber_context_t;

void sensor_callback(const char *path,
                    const signalk_data_t *data,
                    void *user_data) {
    subscriber_context_t *ctx = (subscriber_context_t*)user_data;
    
    if (data->type == SIGNALK_VALUE_FLOAT && 
        data->value.f > ctx->alert_threshold) {
        ESP_LOGW(TAG, "⚠️  %s exceeded threshold!", ctx->name);
    }
}

void app_main(void) {
    // Create context for temperature sensor
    subscriber_context_t *temp_ctx = malloc(sizeof(subscriber_context_t));
    temp_ctx->alert_threshold = 323.15f; // 50°C in Kelvin
    strcpy(temp_ctx->name, "Outside Temperature");
    
    signalk_subscribe("environment.outside.temperature",
                     sensor_callback,
                     5000,
                     temp_ctx);  // Pass user data
    
    // Later...
    signalk_unsubscribe("environment.outside.temperature");
    free(temp_ctx);
}
```

---

## 🚀 Performance Benefits

### WebSocket vs HTTP Polling

**Σενάριο**: Monitor 10 values every 1 second

#### HTTP Polling
- 10 HTTP GET requests/second = 10 requests/sec
- ~500 bytes per request × 10 = 5 KB/sec
- Constant CPU wake-ups to poll
- Higher battery drain

#### WebSocket
- 1 WebSocket connection
- ~50 bytes per delta update
- Event-driven callbacks
- Much lower battery usage
- Perfect for IoT

---

## 📋 Available Signal K Paths

Common paths to subscribe to:

```c
// Navigation
"navigation.position"                    // lat/lon/alt
"navigation.speedOverGround"             // knots
"navigation.courseOverGroundTrue"        // degrees
"navigation.courseOverGroundMagnetic"    // degrees
"navigation.underway"                    // bool

// Environment
"environment.outside.temperature"        // Kelvin
"environment.outside.pressure"           // Pa
"environment.outside.humidity"           // %
"environment.water.temperature"          // Kelvin
"environment.water.salinity"             // ppt
"environment.depth.belowTransducer"      // meters
"environment.wind.angleAppparent"        // radians
"environment.wind.speedApparent"         // m/s

// Electrical
"electrical.batteries.house.voltage"     // Volts
"electrical.batteries.house.current"     // Amps
"electrical.alternators.main.output"     // Amps

// Propulsion
"propulsion.main.state"                  // enum
"propulsion.main.rpm"                    // RPM
"propulsion.main.temperature"            // Kelvin
"propulsion.main.oilPressure"            // Pa

// Custom paths
"custom.myapp.alert"                     // Your own data
"custom.sensors.temperature"             // Your own data
```

---

## 🐛 Debugging

### Ενεργοποίηση Log Output

```c
// In sdkconfig.esp32dev, set:
CONFIG_LOG_DEFAULT_LEVEL=3  // INFO level

// View logs:
// platformio device monitor --baud 115200
```

### Check subscription status

```c
void debug_subscriptions(void) {
    signalk_status_t status;
    signalk_get_status(&status);
    
    ESP_LOGI(TAG, "State: %d", status.state);
    ESP_LOGI(TAG, "Authenticated: %s", 
             status.authenticated ? "Yes" : "No");
    ESP_LOGI(TAG, "Messages received: %d", 
             status.messages_received);
    ESP_LOGI(TAG, "Last message: %lld ms ago",
             esp_log_timestamp() - status.last_message_time);
}
```

---

## ✅ Status Codes

- `SIGNALK_STATE_DISCONNECTED` → Not connected yet
- `SIGNALK_STATE_CONNECTING` → WebSocket connection in progress
- `SIGNALK_STATE_CONNECTED` → HTTP only (WebSocket failed)
- `SIGNALK_STATE_STREAMING` → WebSocket connected, receiving deltas ⭐
- `SIGNALK_STATE_AUTH_PENDING` → Waiting for auth approval
- `SIGNALK_STATE_ERROR` → Connection error

---

## 🎉 Δηλώσεις Ενημέρωσης

Το WebSocket component **υπάρχει** στο esp-idf. Απλά δεν το δηλώσαμε σωστά στο build system. Τώρα:

✅ WebSocket είναι ενεργό  
✅ Real-time deltas λαμβάνονται  
✅ Callbacks καλούνται αυτόματα  
✅ No polling needed  
✅ Όπως κάνει το SenseESP  
