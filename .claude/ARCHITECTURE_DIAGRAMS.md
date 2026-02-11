# Configuration System - Architecture Diagram

## High-Level Data Flow

```
┌─────────────────────────────────────────────────────────────────┐
│                     USER INTERFACE (Web UI)                     │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  Configuration View (ConfigView.jsx)                    │  │
│  │  ┌────────────┬────────────┬────────────┐               │  │
│  │  │ Hardware   │  Behavior  │  Identity  │ ← Sections    │  │
│  │  │            │            │            │               │  │
│  │  │ anchor_gpio report_int  device_label│ ← Fields      │  │
│  │  │ buzzer_en  device_mode              │               │  │
│  │  └────────────┴────────────┴────────────┘               │  │
│  │                                                          │  │
│  │  [Save Configuration Button]                            │  │
│  └──────────────────────────────────────────────────────────┘  │
└──────────────────────┬──────────────────────────────────────────┘
                       │
                       ├─────────────────────────────┐
                       ↓                             ↓
         GET /api/config/schema      GET /api/config/values
         GET /api/config/values       POST /api/config/values
                       │                             │
┌──────────────────────┴──────────────────────────────┴──────────┐
│          API Layer (config_api.c Handlers)                     │
│  ┌────────────────────────────────────────────────────────┐   │
│  │ config_schema_handler()                                 │   │
│  │  └─→ Read /spiffs/config.json                          │   │
│  │  └─→ Return full structure with sections               │   │
│  │                                                         │   │
│  │ config_values_get_handler()                            │   │
│  │  └─→ For each section.field:                           │   │
│  │      └─→ add_field_value(nVS_read)                     │   │
│  │  └─→ Return flattened: {key: value}                    │   │
│  │                                                         │   │
│  │ config_values_post_handler()                           │   │
│  │  └─→ For each section.field:                           │   │
│  │      └─→ store_field_value(validate+store)             │   │
│  │  └─→ Commit to NVS                                     │   │
│  └────────────────────────────────────────────────────────┘   │
└────────────────────────┬─────────────────────────────────────────┘
                         │
                         ↓
         ┌────────────────────────────┐
         │   Schema (config.json)      │
         │  SPIFFS /spiffs/config.json│
         │                            │
         │ {                          │
         │   "sections": [            │
         │     {                      │
         │       "title": "...",      │
         │       "fields": [...]      │
         │     }                      │
         │   ]                        │
         │ }                          │
         └────────────┬───────────────┘
                      │
                      ↓
         ┌────────────────────────────┐
         │  NVS Storage (Flash)        │
         │  namespace: "config"        │
         │                            │
         │  key₁ = value₁             │
         │  key₂ = value₂             │
         │  key₃ = value₃             │
         │  ...                       │
         │  keyₙ = valueₙ             │
         │                            │
         │  (Flat key→value pairs)    │
         └────────────────────────────┘
```

## Backend Architecture

```
┌─────────────────────────────────────────────────────┐
│       HTTP Request to config_api handlers           │
│  GET /api/config/schema                            │
│  GET /api/config/values                            │
│  POST /api/config/values                           │
└────────────────────┬────────────────────────────────┘
                     │
         ┌───────────┼───────────┐
         ↓           ↓           ↓
    ┌─────────┐ ┌────────┐ ┌──────────┐
    │ Schema  │ │  GET   │ │  POST    │
    │Handler  │ │Handler │ │Handler   │
    └────┬────┘ └───┬────┘ └────┬─────┘
         │          │           │
         │          ├───────────┼─────┐
         │          ↓           ↓     ↓
         │     ┌─────────────────────────────────┐
         │     │  Helper Functions              │
         │     │  ┌─────────────────────────┐   │
         │     │  │ schema_load_root()      │   │
         │     │  └─────────────────────────┘   │
         │     │  ┌─────────────────────────┐   │
         │     │  │ add_field_value()       │   │
         │     │  │  • Get from NVS         │   │
         │     │  │  • Return default       │   │
         │     │  └─────────────────────────┘   │
         │     │  ┌─────────────────────────┐   │
         │     │  │ store_field_value()     │   │
         │     │  │  • Validate type        │   │
         │     │  │  • Check min/max        │   │
         │     │  │  • Verify options       │   │
         │     │  │  • Store to NVS         │   │
         │     │  └─────────────────────────┘   │
         │     └─────────────────────────────────┘
         │              │
         │              ↓
         │     ┌─────────────────────────┐
         │     │  cJSON Library          │
         │     │  • Parse schema         │
         │     │  • Create responses     │
         │     │  • Validate JSON        │
         │     └─────────────────────────┘
         │              │
         ├──────────────┼──────────────────┐
         ↓              ↓                  ↓
    ┌────────┐   ┌────────────┐   ┌───────────────┐
    │ Schema │   │    NVS     │   │  HTTP Server  │
    │  File  │   │   Flash    │   │  Response     │
    └────────┘   └────────────┘   └───────────────┘
```

## Frontend (React/Preact) Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                    App.jsx (State Management)                │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ State:                                               │   │
│  │  • configSchema: full schema object                  │   │
│  │  • configValues: flattened {key: value} object       │   │
│  │                                                      │   │
│  │ Functions:                                           │   │
│  │  • updateConfigValue(key, value)                     │   │
│  │  • submitConfig() → POST /api/config/values          │   │
│  └──────────────────────────────────────────────────────┘   │
└────────────────────┬─────────────────────────────────────────┘
                     │
                     ↓
┌──────────────────────────────────────────────────────────────┐
│             ConfigView.jsx (Presentation)                    │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ Props:                                               │   │
│  │  • schema: { sections: [...] }                       │   │
│  │  • values: { key: value }                            │   │
│  │  • onChange, onSubmit callbacks                      │   │
│  │                                                      │   │
│  │ Logic:                                               │   │
│  │  • renderField(field) helper function                │   │
│  │  • Map schema.sections to <fieldset>                 │   │
│  │  • Map section.fields to input elements              │   │
│  │  • Handle form submission                            │   │
│  └──────────────────────────────────────────────────────┘   │
└────────────────────┬─────────────────────────────────────────┘
                     │
         ┌───────────┼────────────┐
         ↓           ↓            ↓
    ┌────────┐  ┌─────────┐  ┌──────────┐
    │Section │  │Fieldset │  │  Input   │
    │(CSS)   │  │ Legend  │  │Elements  │
    └────────┘  └─────────┘  └──────────┘
         │           │            │
         ├────────────┴────────────┤
         ↓
    ┌──────────────────────┐
    │  styles.css          │
    │  • .config-section   │
    │  • .config-form      │
    │  • .field            │
    │  • .toggle-row       │
    │  • .field-help       │
    └──────────────────────┘
```

## Data Structure Transformations

### Schema Structure (JSON)

```
INPUT: /spiffs/config.json
┌─────────────────────────────────────┐
│ {                                   │
│   "title": "...",                   │
│   "sections": [                     │
│     {                               │
│       "title": "Hardware Config",   │
│       "description": "...",         │
│       "fields": [                   │
│         {                           │
│           "key": "anchor_gpio",     │
│           "label": "Anchor GPIO",   │
│           "type": "gpio",           │
│           "default": 4,             │
│           "min": 0,                 │
│           "max": 39,                │
│           "help": "..."             │
│         },                          │
│         ...                         │
│       ]                             │
│     },                              │
│     ...                             │
│   ]                                 │
│ }                                   │
└─────────────────────────────────────┘
        ↓
    GET /api/config/schema
        ↓
FRONTEND receives → ConfigView → User sees organized sections
```

### Values Structure

```
NVS Storage: ("config" namespace)
┌───────────────────────────────────┐
│ anchor_gpio = "4"                 │
│ buzzer_enabled = "true"           │
│ report_interval = "30"            │
│ device_mode = "auto"              │
│ device_label = "SailorGuard"       │
└───────────────────────────────────┘
        ↓
    GET /api/config/values
        ↓
┌───────────────────────────────────┐
│ {                                 │
│   "anchor_gpio": 4,               │
│   "buzzer_enabled": true,         │
│   "report_interval": 30,          │
│   "device_mode": "auto",          │
│   "device_label": "SailorGuard"    │
│ }                                 │
└───────────────────────────────────┘
        ↓
FRONTEND fills form fields
        ↓
USER modifies values
        ↓
    POST /api/config/values
        ↓
┌───────────────────────────────────┐
│ {                                 │
│   "anchor_gpio": 5,               │
│   "report_interval": 60           │
│ }                                 │
└───────────────────────────────────┘
        ↓
BACKEND validates using schema constraints
        ↓
Store back to NVS: anchor_gpio = "5", report_interval = "60"
```

## Request/Response Cycle

### GET /api/config/schema
```
Browser:                Device:
GET /api/config/schema  
    ├─────────────────→  config_schema_handler()
                         ├─→ read_file_to_buffer("/spiffs/config.json")
                         ├─→ cJSON_Parse(content)
                         └─→ httpd_resp_send(json_string)
                         
                    ↓
    ←─────────────────  HTTP 200 + JSON body
    
{
  "title": "Device Configuration",
  "sections": [
    {
      "title": "Hardware Configuration",
      "fields": [...]
    }
  ]
}

Frontend:
├─→ setConfigSchema(response)
└─→ Trigger ConfigView re-render
```

### GET /api/config/values
```
Browser:                Device:
GET /api/config/values  
    ├─────────────────→  config_values_get_handler()
                         ├─→ schema_load_root()
                         ├─→ response = cJSON_CreateObject()
                         │
                         ├─→ For each section in schema:
                         │   └─→ For each field:
                         │       └─→ add_field_value(response, field)
                         │
                         └─→ httpd_resp_send(response)
                         
                    ↓
    ←─────────────────  HTTP 200 + JSON body
    
{
  "anchor_gpio": 4,
  "buzzer_enabled": true,
  "report_interval": 30,
  "device_mode": "auto",
  "device_label": "SailorGuard"
}

Frontend:
├─→ setConfigValues(response)
└─→ Pre-fill form fields
```

### POST /api/config/values
```
Browser:                Device:
POST /api/config/values {
  "anchor_gpio": 5,
  "report_interval": 60
}
    ├─────────────────→  config_values_post_handler()
                         ├─→ cJSON_Parse(body)
                         ├─→ schema_load_root()
                         ├─→ nvs_open("config", NVS_READWRITE)
                         │
                         ├─→ For each section in schema:
                         │   └─→ For each field:
                         │       └─→ store_field_value(
                         │            validate type/min/max/options
                         │            nvs_set_str(handle, key, value)
                         │          )
                         │
                         ├─→ nvs_commit(handle)
                         ├─→ nvs_close(handle)
                         └─→ httpd_resp_send({"ok":true})
                         
                    ↓
    ←─────────────────  HTTP 200 + {"ok": true}

Frontend:
├─→ Show success message
└─→ Trigger GET /values to sync state
```

## Component Interaction

```
┌─────────────────────────────────────────────────┐
│             App.jsx                             │
│  Main state holder                              │
│  • configSchema                                 │
│  • configValues                                 │
│  • updateConfigValue(key, value)                │
│  • submitConfig()                               │
└────────────────┬────────────────────────────────┘
                 │
        ┌────────┴────────┐
        ↓                 ↓
    ┌──────────────┐  ┌──────────────────┐
    │ Header.jsx   │  │ ConfigView.jsx   │
    │              │  │                  │
    │ Navigation   │  │ renderField()    │
    │ Tabs         │  │ Sections         │
    │              │  │ Fields           │
    └──────────────┘  │ Form handling    │
                      └──────────────────┘
                             │
        ┌────────────────────┴────────────────────┐
        ↓                                          ↓
    ┌──────────────────┐                  ┌──────────────────┐
    │ styles.css       │                  │ Backend APIs     │
    │                  │                  │                  │
    │ .config-form     │                  │ /api/config/*    │
    │ .config-section  │                  │ Routes to        │
    │ .section-desc    │                  │ config_api       │
    │ .field           │                  │                  │
    │ .toggle-row      │                  └──────────────────┘
    │ .field-help      │
    └──────────────────┘
```

## Deployment Architecture

```
┌────────────────────────────────────────────────────┐
│            ESP32-S3 Device (Flash)                 │
│  ┌──────────────────────────────────────────────┐ │
│  │  Firmware (ESP-IDF)                          │ │
│  │  ┌──────────────────────────────────────┐   │ │
│  │  │ SPIFFS Filesystem (/spiffs/)         │   │ │
│  │  │ • config.json         (schema)       │   │ │
│  │  │ • index.html          (UI)           │   │ │
│  │  │ • assets/index-*.js   (JS bundle)    │   │ │
│  │  │ • assets/index-*.css  (CSS bundle)   │   │ │
│  │  └──────────────────────────────────────┘   │ │
│  │                                              │ │
│  │  ┌──────────────────────────────────────┐   │ │
│  │  │ NVS Flash (config namespace)         │   │ │
│  │  │ • anchor_gpio = "4"                  │   │ │
│  │  │ • buzzer_enabled = "true"            │   │ │
│  │  │ • report_interval = "30"             │   │ │
│  │  │ • device_mode = "auto"               │   │ │
│  │  │ • device_label = "SailorGuard"       │   │ │
│  │  └──────────────────────────────────────┘   │ │
│  │                                              │ │
│  │  ┌──────────────────────────────────────┐   │ │
│  │  │ HTTP Server (esp_http_server)        │   │ │
│  │  │ └─ /              → static UI        │   │ │
│  │  │ └─ /api/config/*  → config handlers  │   │ │
│  │  └──────────────────────────────────────┘   │ │
│  └──────────────────────────────────────────────┘ │
└─────────────────────┬──────────────────────────────┘
                      │
              WiFi (AP/STA mode)
                      │
         ┌────────────┴────────────┐
         ↓                         ↓
    ┌─────────┐            ┌──────────────┐
    │ Mobile  │            │ Web Browser  │
    │ Client  │            │ (Desktop)    │
    │ (iOS/   │            │              │
    │ Android)│            │ http://      │
    │         │            │ 192.168.4.1  │
    └─────────┘            └──────────────┘
```

---

## Summary

The architecture implements a **clean three-tier system**:

1. **UI Layer**: React/Preact components that render based on schema
2. **API Layer**: HTTP handlers that manage validation and storage
3. **Storage Layer**: NVS flat key→value pairs for persistence

**Benefits**:
- Schema changes don't require firmware rebuilds
- Field logic is centralized (no duplication)
- API is stable (response format unchanged)
- Both flat and sectioned schemas work
- Mobile-responsive UI
