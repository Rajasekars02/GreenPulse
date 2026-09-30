#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(
    SCREEN_WIDTH,
    SCREEN_HEIGHT,
    &Wire,
    -1
);

void setup() {
    Serial.begin(115200);
    delay(1000);

    Wire.begin(OLED_SDA, OLED_SCL);

    Serial.println("Starting OLED...");

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println("OLED INIT FAILED!");
        while (1) {
            delay(1000);
        }
    }

    Serial.println("OLED INIT SUCCESS!");

    display.clearDisplay();

    display.setTextColor(SSD1306_WHITE);

    display.setTextSize(2);
    display.setCursor(10, 10);
    display.println("IMPOSTER");

    display.setTextSize(1);
    display.setCursor(20, 40);
    display.println("ESP32 READY");

    display.display();

    Serial.println("DISPLAY SENT");
}

void loop() {
}