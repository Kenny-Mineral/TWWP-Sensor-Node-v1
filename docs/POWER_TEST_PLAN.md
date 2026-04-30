# Power Delivery Test Plan

## Overview
This document outlines a systematic approach to diagnose and resolve the power delivery issues with the header pins and RTC/SD card module on the ESP32-S3-RS485-CAN board.

## Test Equipment Required
- Digital multimeter
- Oscilloscope (optional but helpful)
- Logic analyzer (optional)
- Breadboard and jumper wires
- 4.7kΩ resistors (for I2C pull-up testing)
- 3.3V voltage regulator (for comparison testing)

## Test Procedure

### 1. Visual Inspection
- Check for cold solder joints on header pins
- Inspect PCB traces for damage
- Verify RTC/SD module connections
- Check for bent pins or poor connections

### 2. Power Rail Measurements
1. **Baseline Voltage Measurements**
   - Measure 3.3V rail at power supply output
   - Measure 3.3V at SH 1.0 pins (known working)
   - Measure 3.3V at header pins (problematic)
   - Record all measurements in the results table

2. **Load Testing**
   - Measure voltage with no load
   - Add 10mA load (220Ω resistor to GND) and measure again
   - Add 50mA load (47Ω resistor to GND) and measure again
   - Record voltage drop under each load condition

### 3. Run Power Test Program
1. Upload the `power_test.cpp` program to the ESP32-S3
2. Follow the serial monitor instructions for each test
3. Record all measurements and observations

### 4. I2C Bus Testing
1. Run the I2C scanner portion of the test program
2. If no devices are found:
   - Add external 4.7kΩ pull-up resistors to SDA and SCL
   - Test with a known working I2C device
   - Try different I2C clock speeds (100kHz, 400kHz)

### 5. SD Card Interface Testing
1. Test SPI pins with multimeter during the test program
2. Verify CS pin toggles correctly
3. Try connecting SD card directly to the ESP32-S3 (bypassing any module)

### 6. Sleep Mode Testing
1. Run the power test with and without the sleep mode disabled
2. Compare voltage measurements in both scenarios
3. Check if GPIO states are maintained during sleep transitions

## Potential Solutions to Test

### Solution 1: Power Management Configuration
```cpp
// Add to setup() in main.cpp
#include "esp_pm.h"
esp_pm_config_esp32s3_t pm_config = {
    .max_freq_mhz = 240,
    .min_freq_mhz = 240,
    .light_sleep_enable = false
};
esp_pm_configure(&pm_config);
```

### Solution 2: GPIO Drive Strength Modification
```cpp
// Add after pinMode() calls
gpio_set_drive_capability(GPIO_NUM_9, GPIO_DRIVE_CAP_3);  // Stronger drive for SDA
gpio_set_drive_capability(GPIO_NUM_3, GPIO_DRIVE_CAP_3);  // Stronger drive for SCL
```

### Solution 3: External Pull-up Resistors
- Add 4.7kΩ resistors between SDA/SCL and 3.3V
- Test I2C communication with external pull-ups

### Solution 4: Separate Power Supply
- Use an external 3.3V regulator to power the RTC/SD module
- Connect only signal lines (SDA/SCL/MOSI/etc.) to the ESP32-S3

## Results Documentation

### Voltage Measurements Table
| Test Point | No Load | 10mA Load | 50mA Load | Notes |
|------------|---------|-----------|-----------|-------|
| Power Supply 3.3V | | | | |
| SH 1.0 3.3V Pin | | | | |
| Header 3.3V Pin | | | | |
| GPIO9 (SDA) | | | | |
| GPIO3 (SCL) | | | | |

### I2C Test Results
| Configuration | Devices Found | Addresses | Notes |
|---------------|--------------|-----------|-------|
| Default | | | |
| With External Pull-ups | | | |
| Different Clock Speed | | | |

### Solution Test Results
| Solution | Effect on Voltage | Effect on I2C | Effect on SD Card | Notes |
|----------|------------------|--------------|------------------|-------|
| Power Management Config | | | | |
| GPIO Drive Strength | | | | |
| External Pull-ups | | | | |
| Separate Power Supply | | | | |

## Conclusion and Recommendations

Based on the test results, implement the following changes:

1. [To be filled after testing]
2. [To be filled after testing]
3. [To be filled after testing]

## Next Steps

After resolving the power delivery issues:

1. Implement the buffer overflow cap in store_sd.cpp
2. Test the complete system with all sensors
3. Document the solution in the project wiki
4. Update the hardware design notes for future revisions