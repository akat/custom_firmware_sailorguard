# Signal K Integration - Implementation Plan

## Στόχος
Να προσθέσουμε Signal K client functionality στο ESP32 firmware, παρόμοια με το SensESP library.

## Signal K Connection Flow

### 1. Server Discovery (mDNS)
```
ESP32 → mDNS Query "_signalk-http._tcp.local"
     ← Server responds with:
       - Hostname (e.g., "vessel.local")
       - IP Address
       - Port (usually 3000)
       - Service info
```

**Εναλλακτικά:** Manual configuration (IP/hostname + port)

### 2. Authentication Flow

#### Option A: Token Request (Αυτόματο)
```
1. GET /signalk/v1/auth/login
   ← Returns login page URL or token request endpoint

2. POST /signalk/v1/access/requests
   Body: {
     "clientId": "device-unique-id",
     "description": "SailorGuard Device"
   }
   ← Returns: {
     "state": "PENDING",
     "requestId": "xxxxx",
     "href": "/signalk/v1/access/requests/xxxxx"
   }

3. Polling: GET /signalk/v1/access/requests/{requestId}
   (Repeat every few seconds)
   ← Eventually returns: {
     "state": "COMPLETED",
     "accessRequest": {
       "token": "your-jwt-token",
       "expiry": "..."
     }
   }

User must approve request via Signal K admin UI
```

#### Option B: Manual Token (Χειροκίνητο)
```
User creates token in Signal K admin UI
User pastes token into device configuration
```

### 3. WebSocket Connection
```
WebSocket URL: ws://server:port/signalk/v1/stream?subscribe=none
Headers:
  Authorization: Bearer {token}

← Server sends initial messages:
  - hello message
  - self identification
```

### 4. Data Exchange

**Send data (PUT):**
```json
{
  "context": "vessels.self",
  "updates": [{
    "source": { "label": "SailorGuard" },
    "timestamp": "2024-01-01T12:00:00Z",
    "values": [{
      "path": "electrical.batteries.house.voltage",
      "value": 12.6
    }]
  }]
}
```

**Subscribe to data:**
```json
{
  "context": "vessels.self",
  "subscribe": [{
    "path": "navigation.position",
    "period": 1000
  }]
}
```

---

## Αρχιτεκτονική Implementation

### Components Structure

```
components/
├── signalk_client/          # Core SignalK client logic
│   ├── signalk_client.c
│   └── include/
│       └── signalk_client.h
├── signalk_mdns/            # mDNS discovery
│   ├── signalk_mdns.c
│   └── include/
│       └── signalk_mdns.h
├── signalk_auth/            # Authentication & token management
│   ├── signalk_auth.c
│   └── include/
│       └── signalk_auth.h
└── signalk_api/             # REST API endpoints
    ├── signalk_api.c
    └── include/
        └── signalk_api.h
```

### Configuration Fields

Προσθήκη στο `config/config.json`:

```json
{
  "title": "Signal K Connection",
  "description": "Connect to Signal K server",
  "fields": [
    {
      "key": "signalk_enabled",
      "label": "Enable Signal K",
      "type": "bool",
      "default": false
    },
    {
      "key": "signalk_discovery",
      "label": "Auto-Discovery (mDNS)",
      "type": "bool",
      "default": true,
      "help": "Automatically discover Signal K server via mDNS"
    },
    {
      "key": "signalk_hostname",
      "label": "Server Hostname/IP",
      "type": "text",
      "default": "",
      "help": "Manual server address (if auto-discovery disabled)"
    },
    {
      "key": "signalk_port",
      "label": "Server Port",
      "type": "number",
      "default": 3000,
      "min": 1,
      "max": 65535
    },
    {
      "key": "signalk_use_ssl",
      "label": "Use SSL/TLS",
      "type": "bool",
      "default": false
    },
    {
      "key": "signalk_auth_mode",
      "label": "Authentication Mode",
      "type": "select",
      "default": "manual",
      "options": [
        {"label": "Manual Token", "value": "manual"},
        {"label": "Auto Request", "value": "auto"}
      ]
    },
    {
      "key": "signalk_token",
      "label": "Access Token",
      "type": "text",
      "default": "",
      "help": "JWT token from Signal K server"
    },
    {
      "key": "signalk_vessel_name",
      "label": "Vessel Name",
      "type": "text",
      "default": "SailorGuard",
      "help": "Name shown in Signal K"
    }
  ]
}
```

### REST API Endpoints

```
GET  /api/signalk/status      - Connection status
POST /api/signalk/discover    - Trigger mDNS discovery
POST /api/signalk/connect     - Connect to server
POST /api/signalk/disconnect  - Disconnect
GET  /api/signalk/servers     - List discovered servers
POST /api/signalk/auth/request - Request new token
GET  /api/signalk/auth/status  - Check auth request status
POST /api/signalk/test        - Test connection
```

### State Machine

```
DISCONNECTED
    ↓ (user enables)
DISCOVERING (if auto-discovery)
    ↓
AUTH_PENDING (if auto-auth)
    ↓
CONNECTING
    ↓
CONNECTED
    ↓ (send data)
STREAMING
```

---

## Implementation Steps

### Phase 1: mDNS Discovery
- [ ] Create `signalk_mdns` component
- [ ] Implement mDNS query for "_signalk-http._tcp"
- [ ] Parse service records (hostname, IP, port)
- [ ] Store discovered servers in list
- [ ] Add API endpoint to list discovered servers

### Phase 2: Configuration
- [ ] Add Signal K fields to `config/config.json`
- [ ] Create UI section for Signal K settings
- [ ] Store/retrieve configuration from NVS
- [ ] Add connection settings form in frontend

### Phase 3: Authentication
- [ ] Create `signalk_auth` component
- [ ] Implement token request flow (POST to /signalk/v1/access/requests)
- [ ] Implement polling for approval
- [ ] Store token securely in NVS
- [ ] Add manual token input option
- [ ] Handle token expiry & refresh

### Phase 4: WebSocket Client
- [ ] Create `signalk_client` component
- [ ] Implement WebSocket connection with ESP-IDF
- [ ] Handle connection lifecycle (connect, disconnect, reconnect)
- [ ] Parse incoming Signal K delta messages
- [ ] Send outgoing data in Signal K format

### Phase 5: Data Integration
- [ ] Define data paths (e.g., "environment.inside.temperature")
- [ ] Map device sensors to Signal K paths
- [ ] Implement data publishing
- [ ] Implement data subscription (receive from Signal K)

### Phase 6: REST API
- [ ] Create `signalk_api` component
- [ ] Status endpoint (connection state)
- [ ] Discovery trigger endpoint
- [ ] Manual connect/disconnect endpoints
- [ ] Auth request endpoints

### Phase 7: UI Integration
- [ ] Add Signal K section to frontend
- [ ] Server discovery UI
- [ ] Connection status display
- [ ] Auth request workflow UI
- [ ] Data path configuration

---

## ESP-IDF Libraries Needed

```cmake
# In component CMakeLists.txt
REQUIRES
    mdns              # For mDNS discovery
    esp_http_client   # For HTTP requests
    json              # For JSON parsing
    esp_websocket_client  # For WebSocket
    mbedtls           # For TLS/SSL (optional)
    config_api        # Our config system
```

---

## Example Usage

### C Code Example

```c
#include "signalk_client.h"
#include "config_api.h"

void app_main(void) {
    // ... existing init ...
    
    // Initialize Signal K client
    signalk_client_config_t sk_config = {
        .enabled = config_get_bool_or_default("signalk_enabled", false),
        .auto_discovery = config_get_bool_or_default("signalk_discovery", true),
        .port = config_get_int_or_default("signalk_port", 3000),
    };
    
    if (sk_config.enabled) {
        signalk_client_init(&sk_config);
        signalk_client_start();
    }
}

// Send data to Signal K
void send_temperature(float temp) {
    signalk_data_t data = {
        .path = "environment.inside.temperature",
        .value_type = SIGNALK_VALUE_FLOAT,
        .value.f = temp,
    };
    signalk_client_send(&data);
}
```

---

## Testing Plan

1. **Test with real Signal K server** (e.g., on Raspberry Pi)
2. **Test mDNS discovery** on local network
3. **Test authentication flow** (both manual and auto)
4. **Test WebSocket connection** (with and without SSL)
5. **Test data sending** (various data types)
6. **Test reconnection** (after network drop)
7. **Test token expiry handling**

---

## Considerations

### Complexity Level
- **Simple**: Manual configuration + manual token → Direct connection
- **Medium**: mDNS discovery + manual token
- **Advanced**: Full auto-discovery + auto-authentication

### Security
- Store tokens encrypted in NVS
- Support TLS/SSL for production use
- Validate server certificates (optional TOFU)

### Network
- Handle WiFi disconnection gracefully
- Automatic reconnection with exponential backoff
- Queue data when disconnected? (optional)

### Performance
- WebSocket runs in separate task
- Non-blocking operations
- Configurable send rate limits

---

## Decision Points

**Πρέπει να αποφασίσουμε:**

1. **Πόσο automated να είναι;**
   - Simple: Χειροκίνητο hostname + χειροκίνητο token ✓ Εύκολο
   - Advanced: Full auto-discovery + auto-auth → Πιο πολύπλοκο

2. **SSL/TLS support;**
   - Προαιρετικό αλλά recommended για production
   - Απαιτεί περίπου 40-50KB extra memory

3. **Data paths;**
   - Ποια sensors θέλεις να στείλεις στο Signal K;
   - Anchor status, GPS, temperature, battery?

4. **Bidirectional;**
   - Μόνο send data → Signal K
   - Ή και receive (π.χ., για remote control)?

5. **Phased implementation;**
   - Phase 1: Simple manual config + send data
   - Phase 2: Add mDNS discovery
   - Phase 3: Add auto-authentication

---

## Recommended Starting Point

**Minimal Viable Implementation (MVP):**

1. Manual hostname/IP configuration
2. Manual token input
3. WebSocket connection (no SSL initially)
4. Send one data point (e.g., temperature)
5. Connection status API

**Estimated effort:** 20-30 hours for MVP

Θες να ξεκινήσουμε με το MVP ή θέλεις κάτι πιο advanced από την αρχή;
