# GreenVolt AI — Telegram Firmware Setup Guide

## File location
`GreenVolt_ESP32/GreenVolt_Telegram/GreenVolt_Telegram.ino`

---

## 1. Required libraries

Install all of these via **Arduino IDE → Tools → Manage Libraries**:

| Library | Author | Version | Purpose |
|---|---|---|---|
| DHT sensor library | Adafruit | any recent | DHT22 / DHT11 temperature |
| Adafruit Unified Sensor | Adafruit | any recent | DHT dependency |

The following are **bundled** with the ESP32 Arduino core (no install needed):
- `WiFi.h`
- `WiFiClientSecure.h`
- `HTTPClient.h`

> **Note:** `ArduinoJson` is NOT needed for this firmware.

---

## 2. Board setup in Arduino IDE

1. Go to **File → Preferences → Additional Boards Manager URLs** and add:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
2. Go to **Tools → Board → Boards Manager**, search `esp32`, install **esp32 by Espressif Systems ≥ 2.0.0**.
3. Select **Tools → Board → ESP32 Arduino → ESP32 Dev Module**.
4. Set **Upload Speed: 115200**, **Flash Size: 4MB**.

---

## 3. Five things you must edit before flashing

Open `GreenVolt_Telegram.ino` and find these lines:

```cpp
// Section 4
static const char* WIFI_SSID     = "YOUR_WIFI_SSID";
static const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Section 5
static const char* BOT_TOKEN = "YOUR_BOT_TOKEN";
static const char* CHAT_ID   = "YOUR_CHAT_ID";

// Section 3
#define TEMP_SENSOR_TYPE  TEMP_TYPE_DHT22   // change if using DHT11 or LM35
```

---

## 4. Telegram Bot setup

### Step 1 — Create a bot
1. Open Telegram and search for **@BotFather**.
2. Send `/newbot`.
3. Follow the prompts (choose a name and username).
4. Copy the **token** — it looks like `123456789:ABCdefGHIjklMNOpqrSTUvwxyz`.
5. Paste it as `BOT_TOKEN` in the firmware.

### Step 2 — Get your Chat ID

**Easiest method:**
1. Search for **@userinfobot** in Telegram.
2. Send `/start`.
3. It replies with your numeric user ID (e.g. `987654321`).
4. Paste that number (as a string) as `CHAT_ID` in the firmware.

**Alternative method:**
1. Send any message to your new bot.
2. Open this URL in a browser (replace `TOKEN` with your actual token):
   ```
   https://api.telegram.org/botTOKEN/getUpdates
   ```
3. Look for `"chat":{"id": 987654321}` in the JSON response.
4. That number is your `CHAT_ID`.

---

## 5. Pin connections

| Sensor | Signal | ESP32 GPIO | Notes |
|---|---|---|---|
| Current sensor | Analog OUT | **34** | ADC1_CH6 — input-only pin |
| PIR sensor | Digital OUT | **27** | 3.3 V compatible |
| DHT22 / DHT11 | Data | **32** | 4.7 kΩ pull-up on data line |
| LM35 | Analog OUT | **32** | if selected in Section 3 |
| All sensors | GND | GND | Common ground required |

> GPIO 34 is on **ADC1**, which continues to work correctly while Wi-Fi is active.  
> ADC2 pins (e.g. GPIO 25, 26, 27 analogue mode) are unreliable with Wi-Fi — avoid for current sensing.

---

## 6. Current sensor calibration

### Step 1 — Zero-current offset
1. Disconnect all loads from the monitored circuit (no current flowing).
2. Flash the firmware and open **Serial Monitor at 115200 baud**.
3. Note the `Current ADC` value printed in the live data table.
4. Set `CURRENT_OFFSET` to that value.
   - Typical value for ACS712 on 3.3 V supply: **≈ 2048**

### Step 2 — Sensitivity (counts per Amp)
1. Connect a known resistive load (e.g. a 60 W lamp = ~0.26 A on 230 V).
2. Measure the true current with a clamp meter.
3. Adjust `CURRENT_SENSITIVITY` until the Serial Monitor current matches your meter.

### Reference values
| Sensor model | CURRENT_OFFSET | CURRENT_SENSITIVITY |
|---|---|---|
| ACS712-5A  | 2048 | 230 |
| ACS712-20A | 2048 | 82  |
| ACS712-30A | 2048 | 55  |

> These are starting points. Actual values vary by individual module and supply voltage.

---

## 7. Temperature sensor configuration

Edit **Section 3** of the firmware:

```cpp
// DHT22 (most common):
#define TEMP_SENSOR_TYPE  TEMP_TYPE_DHT22

// DHT11:
#define TEMP_SENSOR_TYPE  TEMP_TYPE_DHT11

// LM35 analog:
#define TEMP_SENSOR_TYPE  TEMP_TYPE_LM35
```

If you use LM35, also comment out `#include <DHT.h>` — it is not needed.

---

## 8. Occupancy timeout explained

```cpp
static const uint32_t INACTIVITY_TIMEOUT = 60000;  // 60 seconds
```

The PIR sensor triggers on motion. After the last detected motion, the firmware waits `INACTIVITY_TIMEOUT` milliseconds before declaring the room empty.

- If someone is sitting still, increase this value (e.g. `120000` = 2 minutes).
- For a quick-response demo, reduce to `30000` (30 seconds).
- The room is **never** declared empty the instant PIR goes LOW.

---

## 9. Telegram anti-spam logic

Three boolean flags prevent repeated messages:

```cpp
bool g_emptyAlertSent      = false;
bool g_highEnergyAlertSent = false;
bool g_escalatedAlertSent  = false;
```

**Flow:**
```
Motion detected
      ↓
  g_occupied = true
  ALL alert flags reset → false
      ↓
Room monitored normally
      ↓
No motion for INACTIVITY_TIMEOUT
      ↓
  g_occupied = false
      ↓
  emptyAlertSent == false → send empty-room alert → set true
      ↓
  (if power > threshold) → send high-energy alert → set true
      ↓
  (if still high power after HIGH_ENERGY_PERSIST_MS) → escalated alert → set true
      ↓
No more alerts until motion is detected again
```

---

## 10. Adjustable alert thresholds

```cpp
// Power threshold for "high energy" alert (Watts)
static const float HIGH_ENERGY_THRESHOLD_W = 100.0f;  // ← adjust

// How long high-power must persist while empty before escalated alert
static const uint32_t HIGH_ENERGY_PERSIST_MS = 120000;  // 2 minutes ← adjust
```

---

## 11. Example Serial Monitor output

### Normal operation
```
==========================================
       GREENVOLT AI - LIVE DATA
==========================================
  Temperature  : 29.4 degC
  Current      : 1.820 A
  Power (est.) : 418.6 W
  Energy (est.): 0.2316 Wh

  Occupancy    : OCCUPIED
  PIR          : MOTION DETECTED

  Current ADC  : 2466
  WiFi RSSI    : -58 dBm
  IP Address   : 192.168.1.42
  Uptime       : 124 s
  [P/E estimated: V*I, assumed supply voltage]
==========================================
```

### Alert event
```
==========================================
  GREENVOLT AI ALERT
==========================================
  No motion detected.
  Room status: UNOCCUPIED
  Current Power: 418.6 W
  Telegram alert sent.
==========================================
```

---

## 12. Example Telegram messages

### Startup message
```
✅ GreenVolt AI is Online

ESP32 connected successfully.
Monitoring has started.

IP: 192.168.1.42
Supply voltage assumption: 230 V
Inactivity timeout: 60 s

GreenVolt AI — Edge Intelligence Active
```

### Empty-room alert
```
⚠️ GREENVOLT AI ALERT

Room appears unoccupied.

Current: 1.82 A
Estimated Power: 418.6 W
Temperature: 29.4 °C
Energy (session): 12.45 Wh

💡 Recommendation:
Switch OFF unnecessary electrical loads to reduce energy consumption.
```

### High energy alert
```
🔴 GREENVOLT AI — HIGH ENERGY ALERT

No person detected.

Current: 1.82 A
Estimated Power: 418.6 W
Temperature: 29.4 °C

⚡ Potential energy wastage detected.

💡 Recommendation:
Check and switch OFF unnecessary appliances immediately.
```

### Escalated alert
```
🚨 GREENVOLT AI — SUSTAINED ENERGY WASTAGE

Room has been unoccupied for an extended period.
High power consumption is ongoing.

Current: 1.82 A
Estimated Power: 418.6 W
Duration above threshold: ≥ 2 min

🌱 Energy-saving action required.
Please inspect the area and switch OFF all non-essential loads.
```

---

## 13. Troubleshooting

| Problem | Likely cause | Fix |
|---|---|---|
| "Connection timeout" at startup | Wrong SSID/password or weak signal | Double-check credentials; move ESP32 closer to router |
| "[TG] Connection failed" | Firewall blocking port 443 or no internet | Ensure router has internet access; try a mobile hotspot |
| Current always reads 0 | CURRENT_OFFSET is wrong | Calibrate with no load (see Section 6) |
| Current reads negative | Sensor wired in reverse OR offset too high | Swap sensor wires or decrease CURRENT_OFFSET |
| "DHT read failed" | DHT wiring or missing pull-up resistor | Add 4.7 kΩ pull-up between data pin and 3.3 V |
| PIR triggers constantly | Sensor in direct sunlight or near heat | Shield the PIR; increase INACTIVITY_TIMEOUT |
| No Telegram messages | Wrong BOT_TOKEN or CHAT_ID | Verify with @BotFather; check getUpdates endpoint |
| Telegram sends duplicate messages | Alert flags not resetting | Confirm `g_emptyAlertSent` resets on motion (check `updateOccupancy`) |
| Temperature reads 999.0 | Sensor disconnected or wrong type | Check wiring; verify TEMP_SENSOR_TYPE in Section 3 |

---

## 14. Accuracy disclaimer

GreenVolt AI uses:
- A **single-axis current sensor** (ACS712 class)
- An **assumed constant supply voltage**

Power is calculated as: `P = V_assumed × I_measured`

This produces **estimated apparent power** only. It does not account for:
- Actual mains voltage fluctuations
- Power factor (for inductive loads like motors)
- Harmonics

This is appropriate for:
- ✅ Trend monitoring and anomaly detection
- ✅ Hackathon demonstrations
- ✅ Relative energy comparisons

This is NOT appropriate for:
- ❌ Utility billing
- ❌ Regulatory energy audits
- ❌ High-accuracy metering

For accurate measurement, add a ZMPT101B voltage sensor and calculate true power (P = V × I × cos φ).
