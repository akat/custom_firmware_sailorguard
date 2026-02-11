# ESP32-S3 Custom Firmware Dashboard (ESP-IDF + Preact)

This project serves a web dashboard from an ESP32-S3 using SPIFFS and an HTTP server. The UI is built with Preact and served as static files from the device's flash memory.

## Features

- **WiFi Management**
  - STA (Station) mode to connect to existing networks
  - SoftAP (Access Point) fallback when connection fails
  - WiFi network scanning
  - Configurable SoftAP SSID and password
  - Persistent credential storage (only saved after successful connection)
  - ~5 second WiFi scan duration for thorough network discovery

- **Web Dashboard**
  - Static UI served from SPIFFS (SPI Flash File System)
  - Built with Preact + Vite
  - Real-time status updates
  - WiFi settings configuration interface

- **REST APIs**
  - `/api/status` - Device status
  - `/api/wifi/status` - WiFi connection status
  - `/api/wifi/scan` - Scan available networks
  - `/api/wifi/config` - Get/set WiFi credentials
  - `/api/wifi/ap_config` - Get/set SoftAP SSID and password

## Project Structure

```
.
├── src/                    # Application entry point
├── components/
│   ├── wifi_manager/       # WiFi connection & SoftAP management
│   ├── wifi_api/           # WiFi REST API endpoints
│   ├── status_api/         # Status API endpoint
│   ├── http_server/        # HTTP server & static file serving
│   └── spiffs_store/       # SPIFFS mount utilities
├── frontend/               # Preact dashboard source code
│   ├── src/
│   ├── package.json
│   ├── vite.config.js
│   └── scripts/
│       └── copy-to-data.mjs # Build artifact copy script
├── data/                   # SPIFFS filesystem contents (auto-generated)
├── partitions.csv          # Custom partition layout
└── platformio.ini          # PlatformIO configuration
```

## Prerequisites

- **PlatformIO CLI** - For building and uploading firmware
- **Node.js & npm** - For building the frontend UI
- **ESP32-S3** development board
- **USB Cable** - For device connection

## Quick Start: Complete Build & Upload Process

### 1. Build the Frontend UI

```bash
cd frontend
npm install
npm run build:esp
```

This process:
- Installs Preact and build dependencies
- Compiles the Preact UI with Vite
- Outputs to `frontend/dist/`
- Copies build artifacts to `data/` directory (which will be embedded in SPIFFS)

### 2. Build the Firmware

```bash
pio run
```

This compiles the C firmware code and creates the binary that will be flashed to your device.

### 3. Flash SPIFFS (Filesystem) - First Time or After UI Changes

```bash
pio run -t uploadfs
```

This uploads the contents of the `data/` directory (your built UI) to the SPIFFS partition on the device.

### 4. Flash Firmware

```bash
pio run -t upload
```

Flashes the compiled firmware binary to the device.

### 5. Monitor Serial Output (Optional)

```bash
pio device monitor
```

View device logs and debug output.

## Typical Development Workflows

### Only Firmware Code Changed (No UI Changes)

```bash
pio run -t upload
```

### Only Frontend UI Changed

```bash
cd frontend
npm run build:esp
cd ..
pio run -t uploadfs
```

### Complete Build & Upload (Everything Changed)

```bash
cd frontend
npm install
npm run build:esp
cd ..
pio run
pio run -t uploadfs
pio run -t upload
```

### Development Server (Frontend Only)

For rapid frontend development without building for the device:

```bash
cd frontend
npm install
npm run dev
```

Opens a local development server at `http://localhost:5173/`

## After Flashing

1. **Initial Connection**
   - Device starts with SoftAP enabled (default SSID: `ESP32-DASH`, password: `esp32pass`)
   - Connect to this network with your computer or phone

2. **Access the Dashboard**
   - Open browser to `http://192.168.4.1/`

3. **Configure WiFi**
   - Use the WiFi Settings tab to scan and connect to your network
   - Credentials are only saved **after successful connection**
   - On next boot, device will attempt to connect to the saved network
   - If connection fails, SoftAP will activate as fallback

## WiFi Configuration Details

### Default SoftAP Credentials
- **SSID**: `ESP32-DASH`
- **Password**: `esp32pass`

### Changing SoftAP Credentials

Use the WiFi Settings API:

**Get current AP settings:**
```
GET /api/wifi/ap_config
```

Response:
```json
{
  "ssid": "ESP32-DASH",
  "password": "esp32pass"
}
```

**Update AP settings:**
```
POST /api/wifi/ap_config
Content-Type: application/json

{
  "ssid": "MyDevice",
  "password": "newpassword"
}
```

Changes take effect immediately and are persisted to device storage.

### Network Connection Behavior

1. **Successful connection** → Credentials saved to NVS (Flash)
2. **Failed connection** → Credentials NOT saved (temporary only)
3. **On device boot** → Loads saved credentials and attempts connection
4. **Connection timeout** → After 15 seconds, activates SoftAP fallback

This ensures invalid network credentials never get permanently stored.

## API References

### Device Status
```
GET /api/status
```

Response:
```json
{
  "uptime": 12345,
  "version": "1.0"
}
```

### WiFi Status
```
GET /api/wifi/status
```

Response:
```json
{
  "staConnected": true,
  "staSsid": "MyNetwork",
  "staIp": "192.168.1.100",
  "apStarted": false,
  "apSsid": "ESP32-DASH"
}
```

### WiFi Scan
```
GET /api/wifi/scan
```

Response:
```json
{
  "networks": [
    { "ssid": "Network1", "rssi": -45, "auth": "wpa2" },
    { "ssid": "Network2", "rssi": -67, "auth": "open" }
  ]
}
```

### WiFi Configuration
```
GET /api/wifi/config
```

Response:
```json
{ "ssid": "MyNetwork" }
```

**Connect to network:**
```
POST /api/wifi/config
Content-Type: application/json

{
  "ssid": "NetworkName",
  "password": "networkpassword"
}
```

## Configuration

### Customizing Default SoftAP Credentials

Edit [src/main.c](src/main.c):

```c
wifi_manager_init("YourSSID", "YourPassword", 15000);
```

Parameters:
- `"YourSSID"` - SoftAP network name (max 32 chars)
- `"YourPassword"` - SoftAP password (max 64 chars, minimum 8 for WPA2)
- `15000` - Milliseconds to wait before activating SoftAP fallback

### USB Port for Upload

If you have multiple USB devices, specify the port:

```bash
pio run -t upload --upload-port COM3
```

Or on Linux/Mac:
```bash
pio run -t upload --upload-port /dev/ttyUSB0
```

## Troubleshooting

### Device won't connect to WiFi

1. Check SSID and password are correct
2. Verify device is in range of router
3. Check serial output with `pio device monitor`

### SoftAP always active

- Connection may be failing (check signal strength)
- Check saved credentials haven't been corrupted
- Try clearing NVS: Uncomment `nvs_flash_erase()` in `src/main.c` and reflash

### SPIFFS upload failed

- Ensure `data/` directory exists and has UI files
- Run `npm run build:esp` in the frontend directory first

### Can't access dashboard

- Verify you're connected to the WiFi network (device or SoftAP)
- Try `http://192.168.4.1/` for SoftAP
- For WiFi network, check the IP from `/api/wifi/status`

## Notes

- Frontend rebuild automatically updates the SPIFFS data; firmware-only changes don't require rebuilding frontend
- Device reboots are required after firmware changes; SPIFFS changes take effect immediately on the next request
- WiFi scan typically completes within 5 seconds with active scanning enabled
- Maximum 8 networks returned from scan due to memory constraints
