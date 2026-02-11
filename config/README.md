# Device Configuration

This directory contains the configuration schema that defines the device's runtime settings and UI.

## Files

- **config.json** - Configuration schema (source of truth)
- **example_usage.c** - Example code showing how to use config values in firmware

## Configuration Schema Structure

The `config.json` file defines the configuration UI and validation rules. It uses this structure:

```json
{
  "title": "Main title",
  "description": "Main description",
  "sections": [
    {
      "title": "Section title",
      "description": "Section description",
      "fields": [
        {
          "key": "field_key",
          "label": "Field Label",
          "type": "field_type",
          "default": "default_value",
          "help": "Help text"
        }
      ]
    }
  ]
}
```

## Supported Field Types

### 1. Number (`"type": "number"`)

For numeric values with optional min/max constraints.

```json
{
  "key": "report_interval",
  "label": "Report Interval (seconds)",
  "type": "number",
  "default": 30,
  "min": 5,
  "max": 3600,
  "help": "How often the device reports status."
}
```

### 2. GPIO (`"type": "gpio"`)

Special number type for GPIO pins (displayed with GPIO validation).

```json
{
  "key": "anchor_gpio",
  "label": "Anchor GPIO Pin",
  "type": "gpio",
  "default": 4,
  "min": 0,
  "max": 39,
  "help": "GPIO pin for anchor control."
}
```

### 3. Boolean (`"type": "bool"`)

For on/off or true/false settings.

```json
{
  "key": "buzzer_enabled",
  "label": "Buzzer Enabled",
  "type": "bool",
  "default": true,
  "help": "Enable or disable the buzzer."
}
```

### 4. Select (`"type": "select"`)

For dropdown/option selection with predefined choices.

```json
{
  "key": "device_mode",
  "label": "Device Mode",
  "type": "select",
  "default": "auto",
  "options": [
    { "label": "Automatic", "value": "auto" },
    { "label": "Manual", "value": "manual" },
    { "label": "Debug", "value": "debug" }
  ],
  "help": "Operating mode for the device."
}
```

### 5. Text (`"type": "text"`)

For free-form text input.

```json
{
  "key": "device_label",
  "label": "Device Name",
  "type": "text",
  "default": "SailorGuard",
  "help": "Friendly name for this device."
}
```

## Adding New Configuration Fields

1. **Edit config.json** and add your field to the appropriate section
2. **Choose the correct type** (number, gpio, bool, select, text)
3. **Set appropriate defaults** - Users may not configure all fields
4. **Add validation** - Use min/max for numbers, options for selects
5. **Write help text** - Explain what the setting does

Example: Adding a new timeout setting:

```json
{
  "key": "connection_timeout",
  "label": "Connection Timeout (ms)",
  "type": "number",
  "default": 5000,
  "min": 1000,
  "max": 30000,
  "help": "Maximum time to wait for connection before timeout."
}
```

## Using Config Values in Firmware

See [example_usage.c](example_usage.c) for complete examples.

**Quick reference:**

```c
#include "config_api.h"

// Read with default fallback
int timeout = config_get_int_or_default("connection_timeout", 5000);
bool enabled = config_get_bool_or_default("buzzer_enabled", true);

// Read with error checking
char label[64];
if (config_get_string("device_label", label, sizeof(label)) == ESP_OK) {
    // Use label
}
```

## Build Process

When you build the frontend (`npm run build:esp`):

1. `config.json` is copied from this directory to `data/config.json`
2. Frontend build artifacts are copied to `data/`
3. The entire `data/` directory is uploaded to SPIFFS on the device

The device reads the schema from `/spiffs/config.json` and stores user-configured values in NVS (Non-Volatile Storage).

## Configuration Storage

- **Schema** (config.json) → Stored in SPIFFS at `/spiffs/config.json`
- **User values** → Stored in NVS under namespace `config`
- **API endpoints**:
  - `GET /api/config/schema` - Returns the config.json schema
  - `GET /api/config/values` - Returns current configured values (or defaults)
  - `POST /api/config/values` - Updates configuration values

## Best Practices

1. **Always provide defaults** - Not all fields may be configured by users
2. **Use descriptive keys** - Use snake_case, be specific (e.g., `motor_speed_rpm`)
3. **Add validation** - Use min/max, option lists to prevent invalid values
4. **Group related settings** - Use sections to organize the UI
5. **Write clear help text** - Explain units, ranges, and impact of settings
6. **Test default values** - Ensure device works correctly with defaults
7. **Document in code** - Add comments when reading config values explaining their purpose
