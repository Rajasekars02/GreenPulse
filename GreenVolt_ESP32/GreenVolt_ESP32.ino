/*
 * ============================================================
 *  GreenVolt AI — ESP32 Edge Firmware
 *  Version   : 1.0.0
 *  Board     : ESP32 (30-pin or 38-pin dev board)
 *  IDE       : Arduino IDE 2.x  (ESP32 Arduino Core ≥ 2.0)
 *
 *  ARCHITECTURE:
 *    Physical Sensors → ESP32 → Wi-Fi → GreenVolt Backend
 *    → AI/Analytics → Dashboard → Energy-saving recommendations
 *
 *  HARDWARE (no LDR — removed from project):
 *    Current sensor analog OUT  → GPIO 34  (ADC1_CH6, input-only)
 *    PIR sensor OUT             → GPIO 27  (digital input)
 *    Temperature sensor         → GPIO 32  (see TEMP_SENSOR_TYPE below)
 *
 *  REQUIRED LIBRARIES (install via Arduino Library Manager):
 *    • "DHT sensor library" by Adafruit            (if using DHT11/DHT22)
 *    • "Adafruit Unified Sensor" by Adafruit        (DHT dependency)
 *    • "ArduinoJson" by Benoit Blanchon  ≥ v6.x
 *    • WiFi.h      — bundled with ESP32 Arduino core
 *    • HTTPClient.h — bundled with ESP32 Arduino core
 *
 *  ACCURACY DISCLAIMER:
 *    Power and energy figures are ESTIMATES derived from a single-axis
 *    current sensor and an assumed mains voltage constant. Without a
 *    true voltage sensor or calibrated energy meter, these values
 *    should NOT be used for billing or regulatory purposes.
 * ============================================================
 */

// ─────────────────────────────────────────────────────────────
//  SECTION 1 — LIBRARY INCLUDES
// ─────────────────────────────────────────────────────────────
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Temperature sensor: uncomment the ONE line that matches your hardware.
// Then set TEMP_SENSOR_TYPE in Section 3 to the matching value.
#include <DHT.h>           // DHT11 / DHT22 — comment out if using LM35


// ─────────────────────────────────────────────────────────────
//  SECTION 2 — GPIO PIN CONSTANTS
//  Change these values if you rewire the board.
// ─────────────────────────────────────────────────────────────
static const uint8_t PIN_CURRENT_SENSOR = 34;   // ADC1 channel, analog input-only
static const uint8_t PIN_PIR            = 27;   // digital input
static const uint8_t PIN_TEMPERATURE    = 32;   // DHT data pin  -or-  LM35 analog pin


// ─────────────────────────────────────────────────────────────
//  SECTION 3 — TEMPERATURE SENSOR CONFIGURATION
//
//  Set TEMP_SENSOR_TYPE to ONE of:
//    TEMP_DHT22   — DHT22 (AM2302) digital sensor  [most common]
//    TEMP_DHT11   — DHT11 digital sensor
//    TEMP_LM35    — LM35 analog sensor on PIN_TEMPERATURE
// ─────────────────────────────────────────────────────────────
#define TEMP_DHT22  22
#define TEMP_DHT11  11
#define TEMP_LM35   35

#define TEMP_SENSOR_TYPE  TEMP_DHT22    // ← CHANGE THIS to match your sensor

// Internal: derive DHT type constant once
#if   (TEMP_SENSOR_TYPE == TEMP_DHT22)
  #define DHT_TYPE  DHT22
#elif (TEMP_SENSOR_TYPE == TEMP_DHT11)
  #define DHT_TYPE  DHT11
#endif

// DHT object — only instantiated when a DHT sensor is selected
#if (TEMP_SENSOR_TYPE == TEMP_DHT22 || TEMP_SENSOR_TYPE == TEMP_DHT11)
  DHT dht(PIN_TEMPERATURE, DHT_TYPE);
  static const bool USING_DHT = true;
#else
  static const bool USING_DHT = false;
#endif


// ─────────────────────────────────────────────────────────────
//  SECTION 4 — WI-FI CREDENTIALS
// ─────────────────────────────────────────────────────────────
static const char* WIFI_SSID     = "T4X";       // ← replace
static const char* WIFI_PASSWORD = "SRAR3266";   // ← replace

// How long to wait for a Wi-Fi connection before giving up this attempt (ms)
static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;

// How long to wait between reconnection retries when Wi-Fi drops (ms)
static const uint32_t WIFI_RETRY_INTERVAL_MS  = 5000;


// ─────────────────────────────────────────────────────────────
//  SECTION 5 — BACKEND / SERVER CONFIGURATION
// ─────────────────────────────────────────────────────────────
// Full URL of the endpoint that receives the JSON POST.
// Example: "http://192.168.1.100:3000/api/sensor-data"
//          "http://your-server.local/api/ingest"
static const char* SERVER_URL = "http://YOUR_SERVER_IP:PORT/api/sensor-data";  // ← replace

// HTTP connection + response timeout (ms)
static const uint32_t HTTP_TIMEOUT_MS = 5000;

// Device identifier sent in every JSON payload
static const char* DEVICE_ID = "GreenVolt-ESP32-01";       // ← rename if deploying multiples


// ─────────────────────────────────────────────────────────────
//  SECTION 6 — TIMING INTERVALS (non-blocking millis() based)
// ─────────────────────────────────────────────────────────────
// How often to read sensors and transmit data (ms)
static const uint32_t SEND_INTERVAL_MS  = 2000;    // 2 s — change to 5000 for 5 s

// How often to print to Serial Monitor (ms) — can differ from send interval
static const uint32_t PRINT_INTERVAL_MS = 2000;


// ─────────────────────────────────────────────────────────────
//  SECTION 7 — CURRENT SENSOR CALIBRATION
//
//  These constants MUST be adjusted for your specific sensor module.
//  The defaults below are representative starting points only.
//
//  ┌─────────────────────────────────────────────────────────┐
//  │  COMMON SENSOR TYPES AND TYPICAL SENSITIVITY VALUES     │
//  │                                                         │
//  │  ACS712-5A    → sensitivity ≈ 185  mV/A                 │
//  │  ACS712-20A   → sensitivity ≈ 66   mV/A                 │
//  │  ACS712-30A   → sensitivity ≈ 66   mV/A                 │
//  │  SCT-013-030  → depends on burden resistor & ratio      │
//  │  INA219       → I2C sensor, not ADC-based               │
//  │                                                         │
//  │  HOW TO CALIBRATE:                                      │
//  │  1. With zero current flowing, read the ADC output.     │
//  │     Set CURRENT_ZERO_OFFSET_ADC to that value.          │
//  │  2. Apply a known current (e.g. 1 A from a lab supply). │
//  │     Observe ADC reading. Compute:                       │
//  │       sensitivity = ADC_counts_per_amp                  │
//  │       (varies with ESP32 ADC reference voltage ~3.3 V)  │
//  │  3. Update CURRENT_SENSITIVITY_ADC_PER_AMP accordingly. │
//  └─────────────────────────────────────────────────────────┘
//
//  ESP32 ADC characteristics:
//    - 12-bit resolution → 0–4095 counts
//    - Reference voltage ≈ 3.3 V  (GPIO 34 is ADC1, no WiFi interference)
//    - Voltage per count ≈ 3.3 / 4096 ≈ 0.000806 V
//
//  For ACS712-5A (sensitivity 185 mV/A, midpoint ~VCC/2 = 1.65 V):
//    CURRENT_ZERO_OFFSET_ADC     = 2048   (1.65 V / 0.000806 V/count)
//    CURRENT_SENSITIVITY_ADC_PER_AMP = 230  (0.185 V/A / 0.000806 V/count)
// ─────────────────────────────────────────────────────────────
static const float   CURRENT_SENSITIVITY_ADC_PER_AMP = 230.0f;  // ← calibrate
static const int32_t CURRENT_ZERO_OFFSET_ADC          = 2048;   // ← calibrate (zero-current ADC reading)

// Optional: number of ADC samples to average per reading (reduces noise)
static const uint8_t CURRENT_ADC_SAMPLES = 20;

// If averaged current is below this threshold, treat as 0 A (eliminate ADC noise floor)
static const float CURRENT_NOISE_FLOOR_A = 0.02f;


// ─────────────────────────────────────────────────────────────
//  SECTION 8 — POWER / ENERGY CONFIGURATION
//
//  NOTE: This is an ASSUMED constant voltage.
//  True power (VA) requires simultaneous voltage sensing.
//  This firmware calculates APPARENT power only:
//    Apparent Power (W) = Assumed Voltage × RMS Current
// ─────────────────────────────────────────────────────────────
static const float ASSUMED_VOLTAGE_V = 230.0f;   // ← change to 120.0 for US/Japan

// PIR debounce — minimum time (ms) the PIR must stay HIGH before confirming presence
static const uint32_t PIR_DEBOUNCE_MS = 500;


// ─────────────────────────────────────────────────────────────
//  SECTION 9 — RUNTIME STATE  (do not edit)
// ─────────────────────────────────────────────────────────────
struct SensorData {
  float    temperature;       // °C
  bool     tempValid;         // false if sensor returned NaN or error
  float    currentAmps;       // A (calculated)
  int32_t  currentAdcRaw;     // raw ADC average
  float    powerW;            // W  (estimated, see disclaimer)
  float    energyWh;          // Wh (estimated, accumulated since boot)
  bool     occupied;          // PIR state
  int32_t  wifiRssi;          // dBm
  String   ipAddress;
};

static SensorData g_data;              // global sensor snapshot
static float      g_energyWh  = 0.0f; // accumulated energy (Wh) since boot
static uint32_t   g_lastEnergyTs = 0;  // millis() of last energy integration step

// Timing trackers
static uint32_t g_lastSendTs  = 0;
static uint32_t g_lastPrintTs = 0;
static uint32_t g_lastWifiCheckTs = 0;

// PIR debounce state
static uint32_t g_pirRisingEdgeTs = 0;
static bool     g_pirRawPrev      = false;


// ─────────────────────────────────────────────────────────────
//  SECTION 10 — FORWARD DECLARATIONS
// ─────────────────────────────────────────────────────────────
void  connectWiFi();
void  handleWiFiReconnect();
float readTemperature(bool &valid);
float readCurrent(int32_t &rawOut);
bool  readOccupancy();
float calculatePower(float currentA);
void  calculateEnergy(float powerW);
bool  createJSONPayload(const SensorData &d, String &out);
bool  sendDataToServer(const String &payload);
void  printSensorData(const SensorData &d);


// ═════════════════════════════════════════════════════════════
//  SETUP
// ═════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(500);   // brief settle for USB serial on some boards

  Serial.println();
  Serial.println(F("============================================================"));
  Serial.println(F("  GreenVolt AI — ESP32 Edge Firmware v1.0.0"));
  Serial.println(F("============================================================"));

  // Configure GPIO
  pinMode(PIN_PIR, INPUT);
  // GPIO 34 is input-only — no pinMode needed, analogRead works directly

  // Start temperature sensor
#if (TEMP_SENSOR_TYPE == TEMP_DHT22 || TEMP_SENSOR_TYPE == TEMP_DHT11)
  dht.begin();
  Serial.println(F("[TEMP] DHT sensor initialised."));
#else
  Serial.println(F("[TEMP] Using LM35 analog sensor."));
#endif

  // Configure ADC for current sensor (GPIO 34 = ADC1_CH6)
  analogReadResolution(12);          // 12-bit: 0–4095
  analogSetAttenuation(ADC_11db);    // 0–3.3 V full-scale range
  Serial.println(F("[ADC]  12-bit, 0–3.3 V range configured."));

  // Initialise energy integrator timestamp
  g_lastEnergyTs = millis();

  // Connect to Wi-Fi
  connectWiFi();

  Serial.println(F("[INIT] Setup complete. Starting sensor loop."));
  Serial.println();
}


// ═════════════════════════════════════════════════════════════
//  MAIN LOOP — non-blocking, millis()-based
// ═════════════════════════════════════════════════════════════
void loop() {
  uint32_t now = millis();

  // ── 1. Wi-Fi watchdog ──────────────────────────────────────
  if (now - g_lastWifiCheckTs >= WIFI_RETRY_INTERVAL_MS) {
    g_lastWifiCheckTs = now;
    handleWiFiReconnect();
  }

  // ── 2. Read sensors (on every send tick) ───────────────────
  if (now - g_lastSendTs >= SEND_INTERVAL_MS) {
    g_lastSendTs = now;

    // Populate global snapshot
    g_data.temperature  = readTemperature(g_data.tempValid);
    g_data.currentAmps  = readCurrent(g_data.currentAdcRaw);
    g_data.occupied     = readOccupancy();
    g_data.powerW       = calculatePower(g_data.currentAmps);
    calculateEnergy(g_data.powerW);          // updates g_energyWh
    g_data.energyWh     = g_energyWh;
    g_data.wifiRssi     = WiFi.RSSI();
    g_data.ipAddress    = WiFi.localIP().toString();

    // ── 3. Transmit if Wi-Fi is up ──────────────────────────
    if (WiFi.status() == WL_CONNECTED) {
      String payload;
      if (createJSONPayload(g_data, payload)) {
        sendDataToServer(payload);
      }
    }
  }

  // ── 4. Serial print (independent cadence) ──────────────────
  if (now - g_lastPrintTs >= PRINT_INTERVAL_MS) {
    g_lastPrintTs = now;
    printSensorData(g_data);
  }
}


// ═════════════════════════════════════════════════════════════
//  FUNCTION: connectWiFi()
//  Blocking at startup — connects once before the main loop.
// ═════════════════════════════════════════════════════════════
void connectWiFi() {
  Serial.printf("[WiFi] Connecting to \"%s\" ...", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > WIFI_CONNECT_TIMEOUT_MS) {
      Serial.println(F("\n[WiFi] Timeout. Will retry in main loop."));
      return;
    }
    delay(500);
    Serial.print('.');
  }

  Serial.println();
  Serial.printf("[WiFi] Connected!  IP: %s  RSSI: %d dBm\n",
                WiFi.localIP().toString().c_str(),
                (int)WiFi.RSSI());
}


// ═════════════════════════════════════════════════════════════
//  FUNCTION: handleWiFiReconnect()
//  Non-destructive reconnect attempt — called from main loop.
//  Does NOT restart the ESP32 or block the loop.
// ═════════════════════════════════════════════════════════════
void handleWiFiReconnect() {
  if (WiFi.status() == WL_CONNECTED) return;  // already connected

  Serial.println(F("[WiFi] Disconnected — attempting reconnect..."));
  WiFi.disconnect(false);  // do not erase credentials
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // Non-blocking: we check the result on the NEXT loop tick.
  // If still not connected on next WIFI_RETRY_INTERVAL_MS tick,
  // we simply try again. The sensor loop keeps running regardless.
}


// ═════════════════════════════════════════════════════════════
//  FUNCTION: readTemperature()
//  Returns temperature in °C.
//  Sets 'valid = false' and returns -999.0f on sensor failure.
//  Isolating this function means you can replace the sensor
//  by editing only this one function.
// ═════════════════════════════════════════════════════════════
float readTemperature(bool &valid) {
  float temp = -999.0f;
  valid = false;

#if (TEMP_SENSOR_TYPE == TEMP_DHT22 || TEMP_SENSOR_TYPE == TEMP_DHT11)
  // DHT library returns NaN on read failure
  float raw = dht.readTemperature();   // Celsius
  if (!isnan(raw) && raw > -40.0f && raw < 85.0f) {
    temp  = raw;
    valid = true;
  } else {
    Serial.println(F("[TEMP] DHT read failed — NaN or out-of-range."));
  }

#elif (TEMP_SENSOR_TYPE == TEMP_LM35)
  // LM35: output voltage = 10 mV / °C
  // At 3.3 V reference and 12-bit ADC:
  //   voltage = adc * (3.3 / 4096)
  //   temp °C = voltage / 0.01
  int32_t adc = analogRead(PIN_TEMPERATURE);
  if (adc >= 0 && adc <= 4095) {
    float voltage = (float)adc * (3.3f / 4096.0f);
    temp  = voltage / 0.01f;           // LM35 = 10 mV/°C
    // Sanity range check (−10 °C to 110 °C is LM35 rated range)
    if (temp >= -10.0f && temp <= 110.0f) {
      valid = true;
    } else {
      Serial.printf("[TEMP] LM35 out-of-range: %.1f °C\n", temp);
      temp  = -999.0f;
      valid = false;
    }
  } else {
    Serial.println(F("[TEMP] LM35 ADC read error."));
  }
#endif

  return temp;
}


// ═════════════════════════════════════════════════════════════
//  FUNCTION: readCurrent()
//  Reads GPIO 34 (ADC1_CH6) using multi-sample averaging.
//  Converts the ADC count to current (Amps) using the
//  calibration constants in Section 7.
//
//  CALIBRATION NOTES:
//    rawOut → the averaged 12-bit ADC value (0–4095)
//    currentAmps = (rawOut − CURRENT_ZERO_OFFSET_ADC)
//                  / CURRENT_SENSITIVITY_ADC_PER_AMP
//
//  Returns 0.0f if below noise floor, negative values are clamped.
// ═════════════════════════════════════════════════════════════
float readCurrent(int32_t &rawOut) {
  // Average multiple samples to reduce ADC noise
  int64_t sum = 0;
  for (uint8_t i = 0; i < CURRENT_ADC_SAMPLES; i++) {
    sum += analogRead(PIN_CURRENT_SENSOR);
    delayMicroseconds(100);   // short inter-sample pause
  }
  int32_t avgAdc = (int32_t)(sum / CURRENT_ADC_SAMPLES);
  rawOut = avgAdc;

  // Convert to current
  float current = (float)(avgAdc - CURRENT_ZERO_OFFSET_ADC)
                  / CURRENT_SENSITIVITY_ADC_PER_AMP;

  // Clamp negative values (no reverse current through a load)
  if (current < 0.0f) current = 0.0f;

  // Apply noise floor — treat tiny readings as zero
  if (current < CURRENT_NOISE_FLOOR_A) current = 0.0f;

  return current;
}


// ═════════════════════════════════════════════════════════════
//  FUNCTION: readOccupancy()
//  Reads the PIR sensor with a debounce mechanism.
//  A rising edge must sustain for PIR_DEBOUNCE_MS before
//  confirming presence — reduces false triggers from
//  electrical noise or brief motion outside the detection zone.
// ═════════════════════════════════════════════════════════════
bool readOccupancy() {
  bool raw = (digitalRead(PIN_PIR) == HIGH);

  if (raw && !g_pirRawPrev) {
    // Rising edge: start timer
    g_pirRisingEdgeTs = millis();
  }
  g_pirRawPrev = raw;

  if (!raw) {
    // Signal gone → clear
    return false;
  }

  // Signal is HIGH — check if it has been stable long enough
  return (millis() - g_pirRisingEdgeTs >= PIR_DEBOUNCE_MS);
}


// ═════════════════════════════════════════════════════════════
//  FUNCTION: calculatePower()
//  Returns ESTIMATED apparent power in Watts.
//  This is NOT true (real) power — no power-factor correction.
//  Accuracy depends entirely on CURRENT_SENSITIVITY calibration
//  and how representative ASSUMED_VOLTAGE_V is of the actual load.
// ═════════════════════════════════════════════════════════════
float calculatePower(float currentA) {
  if (currentA < 0.0f) currentA = 0.0f;
  return ASSUMED_VOLTAGE_V * currentA;
}


// ═════════════════════════════════════════════════════════════
//  FUNCTION: calculateEnergy()
//  Integrates power over time using the trapezoidal rule.
//  Accumulates into the global g_energyWh counter.
//  Energy is reset on ESP32 reboot (stored in RAM, not NVS).
// ═════════════════════════════════════════════════════════════
void calculateEnergy(float powerW) {
  uint32_t now       = millis();
  uint32_t elapsedMs = now - g_lastEnergyTs;

  // Guard: skip if interval is unreasonable (e.g. first call or overflow)
  if (elapsedMs > 0 && elapsedMs < 30000UL) {
    float hours = (float)elapsedMs / 3600000.0f;   // ms → hours
    g_energyWh += powerW * hours;                  // Wh increment
  }

  g_lastEnergyTs = now;
}


// ═════════════════════════════════════════════════════════════
//  FUNCTION: createJSONPayload()
//  Builds the JSON string from the current sensor snapshot.
//  Returns true on success, false if serialisation failed.
//  Uses ArduinoJson v6 StaticJsonDocument to avoid heap fragmentation.
// ═════════════════════════════════════════════════════════════
bool createJSONPayload(const SensorData &d, String &out) {
  // Capacity estimate: ≈ 512 bytes is comfortable for this payload
  StaticJsonDocument<512> doc;

  doc["device_id"]  = DEVICE_ID;
  doc["uptime_ms"]  = millis();

  // Temperature — send null if sensor failed (dashboards can handle null)
  if (d.tempValid) {
    doc["temperature"] = serialized(String(d.temperature, 2));
  } else {
    doc["temperature"] = nullptr;
  }

  doc["current"]     = serialized(String(d.currentAmps, 3));
  doc["power"]       = serialized(String(d.powerW, 2));
  doc["energy_wh"]   = serialized(String(d.energyWh, 4));
  doc["occupied"]    = d.occupied;
  doc["current_adc"] = d.currentAdcRaw;
  doc["wifi_rssi"]   = d.wifiRssi;
  doc["ip"]          = d.ipAddress;

  // Measurement note for dashboard consumers
  doc["notes"] = "power=estimated(V*I), energy=accumulated_since_boot";

  out = "";
  size_t len = serializeJson(doc, out);
  return (len > 0);
}


// ═════════════════════════════════════════════════════════════
//  FUNCTION: sendDataToServer()
//  HTTP POST to SERVER_URL with JSON body.
//  Non-blocking timeout via HTTPClient.setTimeout().
//  Returns true if server returned 2xx.
// ═════════════════════════════════════════════════════════════
bool sendDataToServer(const String &payload) {
  HTTPClient http;

  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(HTTP_TIMEOUT_MS);

  int httpCode = http.POST(payload);

  if (httpCode > 0) {
    if (httpCode >= 200 && httpCode < 300) {
      Serial.printf("[HTTP] POST OK — %d\n", httpCode);
      http.end();
      return true;
    } else {
      Serial.printf("[HTTP] POST returned %d\n", httpCode);
    }
  } else {
    // httpCode < 0 means a connection/network error
    Serial.printf("[HTTP] POST failed — error: %s\n",
                  http.errorToString(httpCode).c_str());
  }

  http.end();
  return false;
}


// ═════════════════════════════════════════════════════════════
//  FUNCTION: printSensorData()
//  Prints a formatted live-data table to the Serial Monitor.
// ═════════════════════════════════════════════════════════════
void printSensorData(const SensorData &d) {
  Serial.println();
  Serial.println(F("=========================================="));
  Serial.println(F("  GREENVOLT AI  —  LIVE SENSOR DATA"));
  Serial.println(F("=========================================="));

  // Temperature
  if (d.tempValid) {
    Serial.printf("  Temperature  : %.1f °C\n", d.temperature);
  } else {
    Serial.println(F("  Temperature  : SENSOR UNAVAILABLE"));
  }

  Serial.printf("  Current      : %.3f A\n",    d.currentAmps);
  Serial.printf("  Power (est.) : %.1f W\n",    d.powerW);
  Serial.printf("  Energy (est.): %.4f Wh\n",   d.energyWh);
  Serial.printf("  Occupancy    : %s\n",         d.occupied ? "YES (motion detected)" : "NO");
  Serial.printf("  Current ADC  : %d\n",         (int)d.currentAdcRaw);

  // Wi-Fi status
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("  WiFi RSSI    : %d dBm\n",  (int)d.wifiRssi);
    Serial.printf("  IP Address   : %s\n",       d.ipAddress.c_str());
  } else {
    Serial.println(F("  WiFi         : DISCONNECTED — reconnecting..."));
  }

  Serial.printf("  Uptime       : %lu s\n",     millis() / 1000UL);
  Serial.println(F("  [NOTE] Power/Energy are estimates (V*I, assumed V)"));
  Serial.println(F("=========================================="));
}
