# ESP32 Dashboard (ESP-IDF + Preact)

This project serves a small web dashboard from an ESP32-S3 using SPIFFS and an HTTP server. The UI is built with Preact and served as static files from the device.

## Features

- STA Wi-Fi connect with SoftAP fallback (SSID: `ESP32-DASH`, password: `esp32pass`)
- Static UI served from SPIFFS
- Status API at `/api/status`
- Wi-Fi API endpoints (`/api/wifi/status`, `/api/wifi/scan`, `/api/wifi/config`)
- Preact dashboard UI (Vite build)

## Project Layout

- `src/` - App entry point (wiring)
- `components/`
  - `wifi_manager/` - Wi-Fi STA + SoftAP fallback + NVS storage
  - `wifi_api/` - Wi-Fi settings API endpoints
  - `spiffs_store/` - SPIFFS mount helpers
  - `http_server/` - Static file server + API wiring
  - `status_api/` - `/api/status` endpoint
- `frontend/` - Preact dashboard source
- `data/` - Filesystem image contents (copied from `frontend/dist`)
- `partitions.csv` - Custom partition table (includes SPIFFS)

## Build Firmware

```bash
pio run
```

## Build UI and Prepare SPIFFS Data

```bash
cd frontend
npm install
npm run build:esp
```

## Upload SPIFFS

```bash
pio run -t uploadfs
```

## Upload Firmware

```bash
pio run -t upload
```

## Usage

1) Connect to Wi-Fi `ESP32-DASH` (password: `esp32pass`).
2) Open `http://192.168.4.1/`.
3) Use the Wi-Fi Settings tab to scan and connect to your network.

## Notes

- Edit SoftAP credentials in `wifi_manager_init()` (see `components/wifi_manager`).
- The UI polls `/api/status` every 2 seconds.
