# Signal K Integration - Data Publishing

## Overview

The Signal K integration now supports HTTP-based data publishing to Signal K servers. This allows your ESP32 device to send sensor data, telemetry, and other values to a Signal K server even without WebSocket support.

## Features

✅ **HTTP PUT-based publishing** - Uses Signal K's delta format via HTTP
✅ **Authentication** - Automatic token-based authentication
✅ **Multiple data types** - Support for boolean, integer, float, string, and position data
✅ **Helper functions** - Easy-to-use publisher API
✅ **Test endpoint** - Built-in test data transmission

## Usage

### 1. Basic Publishing (C Code)

```c
#include "signalk_publisher.h"

// Publish temperature (automatically converts Celsius to Kelvin)
signalk_publish_temperature("environment.outside.temperature", 22.5f, "ESP32-Sensor");

// Publish voltage
signalk_publish_voltage("electrical.batteries.house.voltage", 12.6f, "ESP32-Monitor");

// Publish boolean
signalk_publish_bool("electrical.switches.cabin.state", true, "ESP32-Control");

// Publish position
signalk_publish_position(37.7749, -122.4194, 0, "ESP32-GPS");

// Publish any float value
signalk_publish_float("navigation.speedThroughWater", 2.5f, "ESP32-Sensor");
```

### 2. REST API Publishing

Send data from any HTTP client:

**POST `/api/signalk/publish`**

```json
{
  "path": "environment.outside.temperature",
  "value": 295.65,
  "source": "ESP32"
}
```

Supported value types:
- Boolean: `{"path": "...", "value": true}`
- Integer: `{"path": "...", "value": 42}`
- Float: `{"path": "...", "value": 3.14}`
- String: `{"path": "...", "value": "sailing"}`

### 3. Test Data

Use the web interface:
1. Navigate to Signal K settings
2. Ensure device is authenticated
3. Click "Send Test Data" button

Or via API:

**POST `/api/signalk/test`**

Sends sample data:
- Temperature: 22.5°C
- Voltage: 12.6V
- Switch state: ON

## Common Signal K Paths

### Navigation
- `navigation.position` - GPS coordinates
- `navigation.speedThroughWater` - Speed in m/s
- `navigation.courseOverGroundTrue` - COG in radians
- `navigation.headingTrue` - Heading in radians

### Environment
- `environment.outside.temperature` - Temperature in Kelvin
- `environment.inside.temperature` - Inside temperature
- `environment.wind.speedTrue` - Wind speed in m/s
- `environment.depth.belowTransducer` - Water depth in meters

### Electrical
- `electrical.batteries.house.voltage` - Battery voltage
- `electrical.batteries.house.current` - Current in Amperes
- `electrical.solar.power` - Solar power in Watts
- `electrical.switches.<name>.state` - Switch states

## Example Integration

```c
#include "signalk_publisher.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void sensor_task(void *pvParameters) {
    while (1) {
        // Read sensors
        float temperature = read_temperature_sensor();
        float voltage = read_battery_voltage();
        
        // Publish to Signal K
        signalk_publish_temperature("environment.outside.temperature", 
                                    temperature, "ESP32-Temp");
        signalk_publish_voltage("electrical.batteries.house.voltage", 
                               voltage, "ESP32-Battery");
        
        // Wait 5 seconds
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
```

## Technical Details

### Authentication
- Requires valid authentication token
- Token obtained automatically via `/signalk/v1/access/requests`
- Token stored in NVS and reused across reboots

### Data Format
Data is sent as Signal K delta updates:

```json
{
  "updates": [{
    "values": [{
      "path": "environment.outside.temperature",
      "value": 295.65
    }],
    "source": {
      "label": "ESP32-Sensor",
      "type": "ESP32"
    }
  }]
}
```

### HTTP Method
- Uses `PUT` to `/signalk/v1/api/vessels/self`
- Includes `Authorization: Bearer <token>` header
- Content-Type: `application/json`

## Limitations

- No WebSocket streaming (HTTP polling only)
- Maximum path length: 128 characters
- Maximum string value length: 128 characters
- No subscription support (publish-only)

## Future Enhancements

When `esp_websocket_client` component becomes available:
- ✅ Real-time WebSocket streaming
- ✅ Server-to-device updates
- ✅ Delta subscriptions
- ✅ Persistent connections

## References

- [Signal K Specification](https://signalk.org/specification/)
- [Signal K HTTP API](https://signalk.org/specification/latest/doc/rest_api.html)
- [Signal K Delta Format](https://signalk.org/specification/latest/doc/data_model.html#delta)
