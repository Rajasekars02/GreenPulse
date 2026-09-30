# GreenVolt AI — ESP32 Firmware

## Quick-start checklist

1. **Install libraries** (Arduino IDE → Library Manager):
   - `DHT sensor library` by Adafruit
   - `Adafruit Unified Sensor` by Adafruit
   - `ArduinoJson` by Benoit Blanchon ≥ v6.x

2. **Select board**: `Tools → Board → ESP32 Arduino → ESP32 Dev Module`

3. **Edit `GreenVolt_ESP32.ino`** — the five things you must change before flashing:

   | Line / Constant | What to set |
   |---|---|
   | `WIFI_SSID` | Your Wi-Fi network name |
   | `WIFI_PASSWORD` | Your Wi-Fi password |
   | `SERVER_URL` | Dashboard/backend HTTP endpoint |
   | `TEMP_SENSOR_TYPE` | `TEMP_DHT22`, `TEMP_DHT11`, or `TEMP_LM35` |
   | `CURRENT_ZERO_OFFSET_ADC` | Zero-current ADC reading (see calibration) |

4. **Flash** at 115200 baud.

## Wiring

| Sensor | Signal | ESP32 GPIO |
|---|---|---|
| Current sensor | Analog OUT | 34 (ADC1) |
| PIR sensor | Digital OUT | 27 |
| DHT22 / DHT11 | Data | 32 |
| LM35 | Analog OUT | 32 |
| All sensors | GND | GND |

> GPIO 34 is ADC1 on the ESP32 — it is input-only and gives clean ADC readings even when Wi-Fi is active.

## Current-sensor calibration

1. With **no load connected**, read the ADC value printed in the Serial Monitor (`Current ADC`).
   Set `CURRENT_ZERO_OFFSET_ADC` to that value (typically near 2048 for a 3.3 V-biased sensor).

2. Connect a **known load** (e.g. a 60 W lamp on a 230 V circuit → ~0.26 A).
   Observe the current reading. Adjust `CURRENT_SENSITIVITY_ADC_PER_AMP` until the displayed current matches your reference measurement.

3. For **ACS712-5A** at 3.3 V supply (sensitivity 185 mV/A):
   `CURRENT_SENSITIVITY_ADC_PER_AMP ≈ 230`

## Accuracy disclaimer

This firmware calculates **estimated apparent power** (`P = V × I`) using a fixed assumed voltage. This is suitable for a hackathon prototype and relative trend monitoring — it is NOT a calibrated energy meter. True power requires simultaneous voltage sensing and power-factor correction.
