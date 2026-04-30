# Power Delivery Issue Diagnosis

## Problem Summary
- Header pins are not providing sufficient voltage to sensors (only a fraction of expected 3.3V)
- SH 1.0 pins are working correctly and providing 3.3V
- RTC/SD card module is not being acknowledged by the system
- This may be breaking the entire system since logging is a critical function

## System Analysis

### Hardware Configuration
- **Board**: ESP32-S3-RS485-CAN with ESP32-S3-WROOM-1 module (16MB flash, 8MB PSRAM)
- **RTC Module**: DS3231 on GPIO9(SDA)/GPIO3(SCL)
- **SD Card**: SPI interface on GPIO11(MOSI)/GPIO12(SCK)/GPIO13(MISO)/GPIO14(CS)
- **Power Rails**: 3.3V and 5V available on header pins

### Firmware Configuration
- I2C initialization in `time_rtc.cpp`:
  ```cpp
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);  // GPIO9, GPIO3
  rtcReady = rtc.begin(&Wire);
  ```
- SD card initialization in `store_sd.cpp`:
  ```cpp
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  sdSpi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  sd.begin(SdSpiConfig(PIN_SD_CS, DEDICATED_SPI, SD_SCK_MHZ(4), &sdSpi))
  ```

## Potential Causes

### 1. Power Management Issues
- **Sleep Mode Effects**: The ESP32-S3 may be entering a power-saving mode that's affecting GPIO output capabilities
- **Inconsistent Power Rails**: The SH 1.0 pins might be connected to a different power domain than the header pins
- **Insufficient Current**: The 3.3V regulator may be unable to supply enough current for all connected devices

### 2. GPIO Configuration Issues
- **Pin Mode Configuration**: Some pins may not be properly configured as outputs or with appropriate drive strength
- **Pull-up/Pull-down Resistors**: Incorrect resistor configuration could be affecting voltage levels
- **GPIO Initialization Timing**: Power pins may be configured after devices try to initialize

### 3. Hardware Issues
- **Voltage Regulator Stability**: The 3.3V regulator might be unstable under certain loads
- **PCB Trace Resistance**: High resistance in PCB traces to header pins could cause voltage drop
- **Cold Solder Joints**: Poor connections on header pins could increase resistance

## Diagnostic Steps

### 1. Power Rail Verification
```
1. Measure voltage on 3.3V header pin with no devices connected
2. Measure voltage on SH 1.0 3.3V pin with no devices connected
3. Compare both measurements - they should be identical
4. Measure voltage on 3.3V header pin with RTC/SD module connected
5. Check for voltage drop under load
```

### 2. GPIO Output Testing
```
1. Create a simple test program that sets all GPIO pins to OUTPUT mode
2. Configure pins to HIGH and measure voltage
3. Test both header pins and SH 1.0 pins
4. Verify that all pins can source sufficient current
```

### 3. Sleep Mode Investigation
```cpp
// Add this to setup() to disable automatic light sleep
esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
```

### 4. I2C Bus Verification
```cpp
// Add this to timeRtc_begin() for more detailed I2C diagnostics
Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
Wire.setTimeOut(10);
Serial.println("[RTC] Scanning I2C bus:");
for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t error = Wire.endTransmission();
    if (error == 0) {
        Serial.printf("[RTC]   Device found at 0x%02X\r\n", addr);
    } else if (error != 2) { // error=2 is normal for non-existent device
        Serial.printf("[RTC]   Error %d at 0x%02X\r\n", error, addr);
    }
}
```

### 5. SD Card Initialization Verification
```cpp
// Add more detailed error reporting to SD initialization
if (!sd.begin(SdSpiConfig(PIN_SD_CS, DEDICATED_SPI, SD_SCK_MHZ(4), &sdSpi))) {
    Serial.print("[SD] Mount failed. Error code: ");
    Serial.println(sd.sdErrorCode());
    Serial.print("[SD] Error data: ");
    Serial.println(sd.sdErrorData());
    sdReady = false;
    return false;
}
```

## Recommended Solutions

### 1. Power Management Improvements
```cpp
// Add to setup() to ensure GPIO pins maintain state
#include "esp_pm.h"
esp_pm_config_esp32s3_t pm_config = {
    .max_freq_mhz = 240,
    .min_freq_mhz = 240,
    .light_sleep_enable = false
};
esp_pm_configure(&pm_config);
```

### 2. GPIO Initialization Sequence
```cpp
// Add to beginning of setup() before any other initialization
// Explicitly set power pins to OUTPUT and HIGH
pinMode(3, OUTPUT);  // 3.3V control pin (if applicable)
digitalWrite(3, HIGH);
delay(100);  // Allow power to stabilize
```

### 3. Hardware Modifications
- Add external pull-up resistors (4.7kΩ) to I2C lines if not already present
- Consider adding a separate 3.3V LDO regulator dedicated to sensors if current draw is high
- Check for and fix any cold solder joints on header pins

### 4. Dependency Handling
```cpp
// Modify the code to handle missing RTC/SD gracefully
bool storeSd_begin() {
    // Existing initialization code...
    
    if (!sdReady) {
        Serial.println("[SD] Operating in degraded mode without SD card");
        // Set up in-memory buffer or other fallback
        return false;  // Still return false but system continues
    }
    return true;
}
```

## Data Organization Recommendations

For PlatformIO libraries and data organization:

### 1. SD Card Library Options
- **SdFat** (currently used): Best performance, most features
- **SD**: Simpler but less efficient
- Recommendation: Continue with SdFat

### 2. RTC Library Options
- **RTClib** (currently used): Well-supported, works with DS3231
- **DS3231**: More specialized for this specific chip
- Recommendation: Continue with RTClib

### 3. Data Format Recommendations
- **CSV for local storage**: Simple, efficient, easy to analyze
- **JSON for MQTT**: More flexible for complex data structures
- Recommendation: Continue with current approach

### 4. Buffering Strategy
- Current FIFO approach is sound
- Consider adding a RAM buffer before writing to SD to reduce wear
- Implement the buffer overflow cap as planned in the next task

## Next Steps

1. Implement the diagnostic steps above to identify the root cause
2. Apply the appropriate solution based on findings
3. Proceed with the planned buffer overflow cap implementation:
   ```cpp
   // In storeSd_bufferMessage():
   uint32_t count = countBufferFilesAndMaxSeq();
   if (count >= SD_MAX_BUFFER_LINES) {
       char oldestPath[96];
       uint32_t oldestSeq = 0;
       if (findOldestBufferFile(oldestPath, sizeof(oldestPath), oldestSeq)) {
           sd.remove(oldestPath);
           storeSd_logEvent("Buffer overflow: deleted oldest file");
       }
   }
   ```

4. Test the system with the hardware modifications and code changes
5. Document the solution for future reference
