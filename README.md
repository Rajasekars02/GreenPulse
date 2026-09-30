# GreenVolt AI — Intelligent Energy & Environmental Monitoring System

> **A hackathon prototype** that combines an ESP32 microcontroller, real IoT sensors, a live web dashboard, and Telegram alerts to monitor energy consumption, detect room occupancy, and estimate CO₂ emissions in real time.

---

## Table of Contents

- [What It Does](#what-it-does)
- [System Architecture](#system-architecture)
- [Project Structure](#project-structure)
- [Hardware](#hardware)
- [Wiring](#wiring)
- [Firmware — GreenVolt_ESP32.ino](#firmware--greenvolt_esp32ino)
  - [Required Libraries](#required-libraries)
  - [Configuration](#configuration)
  - [Current Sensor Calibration](#current-sensor-calibration)
  - [HTTP API](#http-api)
  - [JSON Response Schema](#json-response-schema)
- [Dashboard — index.html](#dashboard--indexhtml)
  - [How to Open](#how-to-open)
  - [Dashboard Sections](#dashboard-sections)
  - [Dashboard Configuration](#dashboard-configuration)
  - [How Calculations Work](#how-calculations-work)
- [Telegram Alerts](#telegram-alerts)
- [Accuracy Disclaimer](#accuracy-disclaimer)
- [Troubleshooting](#troubleshooting)
- [Repository Files](#repository-files)

---

## What It Does

GreenVolt AI collects sensor data from an ESP32 every second and streams it to a local web dashboard over Wi-Fi. It also sends automated Telegram alerts when a room becomes unoccupied while electrical loads are still running.

**What gets monitored:**

| Measurement | Source |
|---|---|
| Temperature (°C) | DHT22 sensor |
| Humidity (%) | DHT22 sensor |
| Room occupancy | PIR motion sensor |
| Electrical current (A) | Analog current sensor on GPIO 34 |
| Estimated power (W) | Calculated: `Voltage × Current` |
| Session energy (kWh) | Accumulated over time |
| Electricity cost (₹) | `Energy × Tariff` |
| Estimated CO₂ (kg) | `Energy × Emission factor` |

---

## System Architecture

```
Physical Sensors
      │
      ▼
  ESP32 (GPIO 34, 27, 32)
      │
      ├─► Wi-Fi (HTTP GET /api/sensor-data)
      │         │
      │         ▼
      │   Web Dashboard (index.html)
      │   ├─ KPI cards (Power, Energy, Current, CO₂)
      │   ├─ Real-time line chart
      │   ├─ Environmental monitoring
      │   ├─ Session metrics (Cost, CO₂, Energy)
      │   └─ Live alert panel
      │
      └─► Telegram Bot API (HTTPS)
              │
              ▼
          Your Phone
          (occupancy + energy alerts)
```

---

## Project Structure

```
GreenPulse/
├── index.html                          ← Web dashboard (single file, open in any browser)
└── GreenVolt_ESP32/
    ├── GreenVolt_ESP32.ino             ← Main ESP32 firmware (HTTP server + sensors)
    ├── README.md                       ← Firmware-specific quick-start
    └── GreenVolt_Telegram/
        ├── GreenVolt_Telegram.ino      ← Telegram-only alert firmware (see note below)
        └── SETUP_GUIDE.md              ← Telegram bot setup instructions
```

> **Note on `GreenVolt_Telegram.ino`:** This file currently contains an OLED display test sketch used during hardware development. The production firmware with both HTTP server and Telegram alerts is `GreenVolt_ESP32.ino`.

---

## Hardware

| Component | Purpose |
|---|---|
| ESP32 32-pin development board | Microcontroller + Wi-Fi |
| DHT22 (or DHT11) | Temperature + Humidity |
| PIR motion sensor | Room occupancy detection |
| Analog current sensor | Electrical current measurement |
| USB cable | Programming + Serial Monitor |

> **LDR (light sensor):** Was part of earlier prototypes but has been removed from the current hardware build. The dashboard shows "N/A" for the light field — this is intentional.

---

## Wiring

| Sensor | Signal Pin | ESP32 GPIO | Notes |
|---|---|---|---|
| Current sensor | Analog OUT | **GPIO 34** | ADC1_CH6 — input-only, works with Wi-Fi active |
| PIR sensor | Digital OUT | **GPIO 27** | Digital input |
| DHT22 / DHT11 | Data | **GPIO 32** | Add 4.7 kΩ pull-up resistor to 3.3 V |
| All sensors | GND | GND | Common ground required |

> GPIO 34 is on **ADC1**, which remains accurate while Wi-Fi is transmitting. Do not use ADC2 pins for the current sensor.

---

## Firmware — GreenVolt_ESP32.ino

The firmware runs a non-blocking loop that:

1. Reads all sensors every 2 seconds
2. Accumulates energy using real elapsed time (not a fixed interval)
3. Serves a local HTTP JSON API on port 80
4. Reconnects to Wi-Fi automatically if the connection drops
5. Sends Telegram alerts when the room becomes unoccupied with load still running

### Required Libraries

Install via **Arduino IDE → Tools → Manage Libraries**:

| Library | Author | Used for |
|---|---|---|
| `DHT sensor library` | Adafruit | DHT22 / DHT11 temperature + humidity |
| `Adafruit Unified Sensor` | Adafruit | DHT dependency |
| `ArduinoJson` | Benoit Blanchon (v6.x+) | JSON payload serialization |

The following are bundled with the ESP32 Arduino core and require no installation:

- `WiFi.h`
- `WebServer.h`
- `HTTPClient.h`
- `WiFiClientSecure.h`

### Configuration

Open `GreenVolt_ESP32.ino` and edit these sections before flashing:

**Section 4 — Wi-Fi credentials**
```cpp
static const char* WIFI_SSID     = "YOUR_WIFI_NAME";
static const char* WIFI_PASSWORD  = "YOUR_WIFI_PASSWORD";
```

**Section 3 — Temperature sensor type**
```cpp
#define TEMP_SENSOR_TYPE  TEMP_DHT22   // change to TEMP_DHT11 or TEMP_LM35
```

**Section 7 — Current sensor calibration**
```cpp
static const float ZERO_CURRENT_VOLTAGE = 1.65f;  // sensor output (V) at zero current
static const float SENSOR_SENSITIVITY   = 0.185f; // V per Amp (ACS712-5A default)
```

**Section 7 — Power and energy**
```cpp
static const float SUPPLY_VOLTAGE     = 230.0f;  // assumed grid voltage
static const float ELECTRICITY_TARIFF = 8.0f;    // ₹ per kWh
static const float CO2_FACTOR         = 0.70f;   // kg CO₂ per kWh
```

### Current Sensor Calibration

The firmware uses a voltage-based conversion formula:

```
averageADC     = mean of 100 analogRead() samples
sensorVoltage  = (averageADC / 4095.0) × 3.3
current (A)    = (sensorVoltage − ZERO_CURRENT_VOLTAGE) / SENSOR_SENSITIVITY
```

**Step 1 — Find `ZERO_CURRENT_VOLTAGE`:**
Disconnect all loads. Open Serial Monitor. Note the `Sensor V` value printed. Set `ZERO_CURRENT_VOLTAGE` to that value (typically ~1.65 V for a VCC/2-biased sensor).

**Step 2 — Find `SENSOR_SENSITIVITY`:**
Connect a known resistive load. Measure actual current with a clamp meter. Adjust `SENSOR_SENSITIVITY` until the Serial Monitor reading matches.

**Common starting values:**

| Sensor model | `ZERO_CURRENT_VOLTAGE` | `SENSOR_SENSITIVITY` |
|---|---|---|
| ACS712-5A | 1.65 V | 0.185 V/A |
| ACS712-20A | 1.65 V | 0.100 V/A |
| ACS712-30A | 1.65 V | 0.066 V/A |

### HTTP API

After flashing, the ESP32 starts a web server on port 80. Two endpoints are available:

**`GET /api/health`** — connectivity test, no sensor dependency
```json
{
  "status": "ok",
  "device": "GreenVolt-ESP32",
  "wifi": true,
  "uptime_ms": 12345,
  "ip": "10.206.64.128"
}
```

**`GET /api/sensor-data`** — full live sensor data
```json
{
  "timestamp": 300,
  "pir": {
    "motion": true,
    "occupancy": 1
  },
  "dht22": {
    "temperature": 27.60,
    "humidity": 70.20
  },
  "current": {
    "amps": 2.310,
    "power": 531.30,
    "energy": 0.000295,
    "cost": 0.0024,
    "co2": 0.000206
  },
  "system": {
    "online": true
  }
}
```

Both endpoints include CORS headers (`Access-Control-Allow-Origin: *`) so any browser can fetch from them directly.

### JSON Response Schema

| Field | Type | Description |
|---|---|---|
| `timestamp` | integer | ESP32 uptime in seconds since boot |
| `pir.motion` | boolean | `true` = motion currently detected |
| `pir.occupancy` | 0 or 1 | 1 = occupied, 0 = unoccupied |
| `dht22.temperature` | float or null | Degrees Celsius; null if sensor fails |
| `dht22.humidity` | float or null | Relative humidity %; null if sensor fails |
| `current.amps` | float | Measured current in Amperes |
| `current.power` | float | Estimated power in Watts (`V × I`) |
| `current.energy` | float | Accumulated session energy in kWh |
| `current.cost` | float | Electricity cost in ₹ |
| `current.co2` | float | Estimated CO₂ emissions in kg |
| `system.online` | boolean | Always `true` when server is responding |

---

## Dashboard — index.html

A single self-contained HTML file. No build step, no Node.js, no framework. Open it in Chrome or Edge directly from the filesystem or serve it from any static web server.

### How to Open

1. Double-click `index.html` — or —
2. Drag it into a browser tab — or —
3. Serve locally: `python -m http.server 5500` then open `http://localhost:5500`

> The browser and ESP32 must be on the **same Wi-Fi network** for the dashboard to reach the ESP32.

**Before opening**, set the ESP32's IP address in the CONFIG block at the top of the `<script>` section:

```js
ESP32_BASE_URL: 'http://10.206.64.128',  // ← replace with your ESP32's actual IP
```

The ESP32 prints its IP to Serial Monitor after connecting:
```
[WiFi] Connected!  IP: 10.206.64.128   RSSI: -52 dBm
[HTTP] Endpoints:
         http://10.206.64.128/api/health
         http://10.206.64.128/api/sensor-data
```

### Dashboard Sections

| Section | What it shows |
|---|---|
| **Top navigation** | GreenVolt logo, live clock, ESP32 connection status badge |
| **Connection banner** | Orange/red banner shown when ESP32 is unreachable |
| **KPI cards (×4)** | Current Power (kW), Session Energy (kWh), Current (A), Estimated CO₂ (kg) — each with a sparkline |
| **Real-time chart** | Scrolling 60-second power consumption line chart |
| **System Status card** | ESP32 online/offline, per-sensor status pills, last update time |
| **Environmental Monitoring** | Temperature, Humidity, PIR motion, Ambient Light (N/A — sensor removed) |
| **Session Metrics** | Energy (kWh), Electricity cost (₹), Estimated CO₂ (kg), current tariff |
| **Live Alerts** | Automatically generated rule-based alerts (high power, high temp, no motion with load, ESP32 offline) |
| **Raw Sensor Values** | Direct display of the latest JSON values for debugging |
| **Footer** | Version, poll rate, overall system status |

### Dashboard Configuration

All configurable values live in the `CONFIG` object at the top of the `<script>` block:

```js
const CONFIG = {
  ESP32_BASE_URL:      'http://10.206.64.128', // ← your ESP32 IP
  GRID_VOLTAGE:        230,     // V — used as fallback power calculation
  ELECTRICITY_TARIFF:  8.00,    // ₹ per kWh
  GRID_EMISSION_FACTOR: 0.7,    // kg CO₂ per kWh
  POLL_INTERVAL_MS:    1000,    // fetch every 1 second
  REQUEST_TIMEOUT_MS:  3000,    // abort fetch after 3 seconds
  GRAPH_MAX_POINTS:    60,      // rolling 60-second window
  ALERT_HIGH_POWER_KW: 2.0,     // alert threshold in kW
  ALERT_HIGH_TEMP_C:   32.0,    // alert threshold in °C
  ALERT_HIGH_HUM_PCT:  80,      // alert threshold in %
};
```

### How Calculations Work

**Power** — read directly from `data.current.power` (calculated on the ESP32).
Falls back to `amps × GRID_VOLTAGE` if the firmware doesn't supply the field.

**Energy** — read directly from `data.current.energy` (accumulated on the ESP32 using real elapsed time).
```
elapsedHours    = (currentMillis − previousMillis) / 3,600,000
energyIncrement = (power_W / 1000) × elapsedHours
totalEnergy_kWh += energyIncrement
```

**Electricity cost**
```
cost (₹) = energy_kWh × ELECTRICITY_TARIFF
```

**Estimated CO₂**
```
co2 (kg) = energy_kWh × GRID_EMISSION_FACTOR
```

**Alert rules**

| Condition | Alert severity |
|---|---|
| Power > 2.0 kW | High |
| Temperature > 32 °C | Medium |
| Humidity > 80 % | Medium |
| No occupancy + power > 0.1 kW | Low |
| ESP32 unreachable | High |

---

## Telegram Alerts

The firmware sends automated Telegram messages when:

- The room becomes **unoccupied** (PIR inactivity timeout reached) — one message per occupancy cycle
- The room is unoccupied **and** power exceeds `HIGH_ENERGY_THRESHOLD_W` — one high-energy alert
- The room stays unoccupied with high power for more than `HIGH_ENERGY_PERSIST_MS` — one escalated alert

All alert flags reset automatically when motion is detected again, preventing spam.

**Setup:**

1. Open Telegram → search `@BotFather` → `/newbot` → copy the token
2. Search `@userinfobot` → `/start` → copy your Chat ID
3. Edit `GreenVolt_ESP32.ino` or `GreenVolt_Telegram.ino`:
   ```cpp
   static const char* BOT_TOKEN = "123456789:ABCdef...";
   static const char* CHAT_ID   = "987654321";
   ```

See `GreenVolt_ESP32/GreenVolt_Telegram/SETUP_GUIDE.md` for full Telegram setup instructions.

---

## Accuracy Disclaimer

Power and energy values are **estimates**, not metered measurements.

- Power is calculated as `P = assumed_voltage × measured_current`
- No power-factor correction is applied
- Mains voltage is assumed constant (default: 230 V)
- Current sensor accuracy depends on calibration

These values are suitable for:
- ✅ Energy trend monitoring and anomaly detection
- ✅ Hackathon demonstrations and relative comparisons
- ✅ Rule-based energy-saving alerts

They are **not** suitable for:
- ❌ Utility billing
- ❌ Regulatory energy audits
- ❌ High-accuracy metering

---

## Troubleshooting

| Problem | Check |
|---|---|
| Dashboard shows "ESP32 Offline" | 1. Confirm ESP32 is powered. 2. Open `http://ESP32_IP/api/health` in browser — if it fails, it's a network issue. 3. Verify `ESP32_BASE_URL` in `index.html` matches the IP in Serial Monitor. |
| IP changed after reboot | DHCP assigns a new IP. Update `ESP32_BASE_URL` in `index.html` and hard-reload (Ctrl+Shift+R). |
| Temperature shows "Unavailable" | Check DHT22 wiring. Add 4.7 kΩ pull-up between data pin and 3.3 V. |
| Current always reads 0 A | Calibrate `ZERO_CURRENT_VOLTAGE` — read `Sensor V` in Serial Monitor with no load. |
| Current reads wrong value | Adjust `SENSOR_SENSITIVITY`. See calibration section above. |
| Telegram messages not sending | Verify `BOT_TOKEN` and `CHAT_ID` are correct. Test with `@BotFather`. |
| CORS error in browser console | Ensure the ESP32 firmware includes `Access-Control-Allow-Origin: *`. |
| All values show `—` after connecting | Hard-reload the dashboard (Ctrl+Shift+R) to clear any cached old script. |

---

## Repository Files

| File | Description |
|---|---|
| `index.html` | Complete web dashboard — HTML + CSS + JavaScript in one file |
| `GreenVolt_ESP32/GreenVolt_ESP32.ino` | Main ESP32 firmware: sensors, HTTP server, Telegram, energy calculations |
| `GreenVolt_ESP32/README.md` | Firmware quick-start guide |
| `GreenVolt_ESP32/GreenVolt_Telegram/GreenVolt_Telegram.ino` | Currently contains an OLED test sketch from hardware development |
| `GreenVolt_ESP32/GreenVolt_Telegram/SETUP_GUIDE.md` | Telegram bot setup and calibration instructions |

---

*GreenVolt AI — Intelligent Energy & Environmental Optimization*
*Built for hackathon demonstration. Not intended for billing or regulatory use.*
