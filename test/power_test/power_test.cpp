#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>

// Pin definitions from the main project
#define PIN_I2C_SDA      9   // DS3231 RTC + microSD combo module
#define PIN_I2C_SCL      3   // DS3231 RTC + microSD combo module
#define PIN_SD_MOSI     11   // SPI2
#define PIN_SD_SCK      12
#define PIN_SD_MISO     13
#define PIN_SD_CS       14

// Test pins - add any other pins you want to test
const int testPins[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
const int numTestPins = sizeof(testPins) / sizeof(testPins[0]);

// Power management includes
#include "esp_pm.h"
#include "esp_sleep.h"

void setup() {
  // Initialize serial and wait for connection
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n\n=== ESP32-S3 Power Delivery Test ===");
  
  // Disable automatic sleep modes
  Serial.println("Disabling automatic sleep...");
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  
  // Configure power management for maximum performance
  esp_pm_config_esp32s3_t pm_config = {
    .max_freq_mhz = 240,
    .min_freq_mhz = 240,
    .light_sleep_enable = false
  };
  esp_pm_configure(&pm_config);
  
  // Test 1: Set all test pins to OUTPUT and HIGH
  Serial.println("\nTest 1: Setting all test pins to OUTPUT and HIGH");
  for (int i = 0; i < numTestPins; i++) {
    pinMode(testPins[i], OUTPUT);
    digitalWrite(testPins[i], HIGH);
    Serial.printf("Pin %d set to OUTPUT, HIGH\n", testPins[i]);
    delay(100);
  }
  Serial.println("Measure voltage on each pin with a multimeter now.");
  Serial.println("Press any key to continue to next test...");
  while (!Serial.available()) delay(100);
  Serial.read(); // Clear the input buffer
  
  // Test 2: I2C bus scan
  Serial.println("\nTest 2: I2C Bus Scan");
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setTimeOut(10);
  
  Serial.println("Scanning I2C bus...");
  bool deviceFound = false;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t error = Wire.endTransmission();
    if (error == 0) {
      Serial.printf("I2C device found at address 0x%02X\n", addr);
      deviceFound = true;
    } else if (error != 2) { // error=2 is normal for non-existent device
      Serial.printf("Error %d at address 0x%02X\n", error, addr);
    }
  }
  
  if (!deviceFound) {
    Serial.println("No I2C devices found. Check connections and power.");
  }
  
  // Test 3: SPI pins test
  Serial.println("\nTest 3: SPI Pins Test");
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  Serial.println("SPI pins initialized. Measure voltage on MOSI, SCK, and CS pins.");
  
  // Test 4: Power cycling test
  Serial.println("\nTest 4: Power Cycling Test");
  Serial.println("Setting all pins LOW then HIGH to test power stability");
  
  // Set all pins LOW
  for (int i = 0; i < numTestPins; i++) {
    digitalWrite(testPins[i], LOW);
  }
  delay(500);
  
  // Set all pins HIGH again
  for (int i = 0; i < numTestPins; i++) {
    digitalWrite(testPins[i], HIGH);
    Serial.printf("Pin %d set back to HIGH\n", testPins[i]);
    delay(100);
  }
  
  Serial.println("\nAll tests complete. Continuing to loop test...");
}

void loop() {
  // Continuous monitoring
  Serial.println("\n--- Continuous Monitoring ---");
  
  // Toggle pins to check for stability
  for (int i = 0; i < numTestPins; i++) {
    digitalWrite(testPins[i], LOW);
    delay(50);
    digitalWrite(testPins[i], HIGH);
    Serial.printf("Toggled pin %d\n", testPins[i]);
  }
  
  // Scan I2C again
  Serial.println("Rescanning I2C bus...");
  bool deviceFound = false;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("I2C device found at address 0x%02X\n", addr);
      deviceFound = true;
    }
  }
  
  if (!deviceFound) {
    Serial.println("No I2C devices found during rescan.");
  }
  
  delay(5000);
