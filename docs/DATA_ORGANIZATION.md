# Data Organization for TWWP Project

## Overview
This document outlines the data organization strategies for the TWWP water monitoring project, covering local storage, MQTT transmission, and data pipeline considerations.

## Local Storage Options

### 1. CSV Format (Current Implementation)
```
timestamp,flow1,flow_total,pressure,temperature,supply_voltage,leak,flow_ok,pressure_ok,power_ok,ph,orp,ec,tds,water_temp
1682345678,12.5,1234.5,350.2,22.3,4.98,0,1,1,1,7.2,650,423,211,21.5
```

**Advantages:**
- Simple, human-readable format
- Low processing overhead
- Easy to import into spreadsheets and analysis tools
- Efficient storage (smaller file size than JSON)
- Straightforward to append new records

**Disadvantages:**
- Less flexible for nested data structures
- Schema changes require careful handling
- No built-in data validation

**Recommendation:** Continue using CSV for daily log files. The format is well-suited for time-series sensor data and provides good performance on resource-constrained devices.

### 2. JSON Format (Used for MQTT Buffer)
```json
{
  "timestamp": 1682345678,
  "sensors": {
    "flow1": 12.5,
    "flow_total": 1234.5,
    "pressure": 350.2,
    "temperature": 22.3,
    "supply_voltage": 4.98,
    "leak": false
  },
  "status": {
    "flow_ok": true,
    "pressure_ok": true,
    "power_ok": true
  },
  "water_quality": {
    "ph": 7.2,
    "orp": 650,
    "ec": 423,
    "tds": 211,
    "water_temp": 21.5
  }
}
```

**Advantages:**
- Flexible schema with nested structures
- Self-describing data
- Native format for MQTT payloads and web services
- Easy to extend with new fields

**Disadvantages:**
- Larger storage footprint than CSV
- Higher processing overhead for parsing
- More complex to generate on constrained devices

**Recommendation:** Continue using JSON for MQTT message buffering, as it preserves the structure needed for MQTT transmission and is compatible with Home Assistant's expected format.

### 3. Binary Format (Potential Future Option)
```cpp
struct SensorData {
    uint32_t timestamp;
    float flow1;
    float flow_total;
    float pressure;
    float temperature;
    float supply_voltage;
    uint8_t flags;  // bit 0: leak, bit 1: flow_ok, etc.
    // Additional fields...
};
```

**Advantages:**
- Most compact storage format
- Minimal processing overhead
- Direct memory mapping

**Disadvantages:**
- Not human-readable
- Difficult to debug
- Platform-dependent (endianness, alignment)
- Inflexible to schema changes

**Recommendation:** Not recommended for this project phase. The complexity outweighs the benefits, especially given the modest storage requirements and the need for human-readable data during development.

## MQTT Data Organization

### 1. Topic Structure
Current structure:
```
twwp/<node_id>/status    - Telemetry data (10s interval)
twwp/<node_id>/alert     - Alert state changes
twwp/<node_id>/log       - System logs
twwp/<node_id>/lwt       - Last will and testament
twwp/<node_id>/cmd       - Command channel
```

**Recommendation:** This structure is well-designed and follows MQTT best practices. No changes needed.

### 2. Payload Format
Current format: JSON with flat structure for status messages.

**Recommendation:** Consider adopting a more structured JSON format for status messages to improve organization:

```json
{
  "timestamp": 1682345678,
  "sensors": {
    "flow": { "current": 12.5, "total": 1234.5 },
    "pressure": 350.2,
    "temperature": 22.3,
    "voltage": 4.98,
    "leak": false
  },
  "status": {
    "flow_ok": true,
    "pressure_ok": true,
    "power_ok": true
  },
  "water_quality": {
    "ph": 7.2,
    "orp": 650,
    "ec": 423,
    "tds": 211,
    "temperature": 21.5
  },
  "system": {
    "uptime": 86400,
    "wifi_rssi": -65,
    "free_heap": 123456
  }
}
```

This structure is more organized and easier to extend while remaining compatible with Home Assistant.

## Data Pipeline Considerations

### 1. Local Processing vs. Cloud Processing
**Local Processing:**
- Perform basic calculations on the ESP32-S3 (averages, thresholds, alerts)
- Store raw and processed data locally
- Send both raw and processed data via MQTT

**Cloud Processing:**
- Send only raw data from the ESP32-S3
- Perform advanced analytics in the cloud
- Store historical data in a time-series database

**Recommendation:** Hybrid approach - perform essential processing on the device (thresholds, alerts) but offload complex analytics to the server side.

### 2. Time-Series Database Options
For server-side storage and analysis:

**InfluxDB:**
- Purpose-built for time-series data
- Efficient storage and querying
- Built-in downsampling and retention policies
- Good integration with Grafana for visualization

**TimescaleDB:**
- PostgreSQL extension for time-series data
- SQL interface for querying
- Combines relational and time-series capabilities
- Good for mixed workloads

**Recommendation:** Consider implementing InfluxDB on the server side for long-term data storage and analysis. This would complement the local CSV storage on the device.

### 3. Data Retention Strategy

**Device-side retention:**
- Daily CSV files
- Automatic rotation at midnight
- Keep 30 days of data locally (configurable)
- Oldest files deleted when storage threshold reached

**Server-side retention:**
- Raw data: 3 months at full resolution
- 1-minute averages: 1 year
- 1-hour averages: 5 years
- Daily averages: indefinite

**Recommendation:** Implement the above retention strategy to balance storage requirements with data availability.

## Library Recommendations

### 1. SD Card Libraries
**SdFat (Current):**
- Excellent performance
- Advanced features (wear leveling, etc.)
- Active development
- Good compatibility with ESP32-S3

**Recommendation:** Continue using SdFat.

### 2. JSON Libraries
**ArduinoJson (Current):**
- Efficient memory usage
- Well-documented
- Active development
- Good compatibility with ESP32-S3

**Recommendation:** Continue using ArduinoJson v7.

### 3. MQTT Libraries
**PubSubClient (Current):**
- Lightweight
- Well-established
- Good compatibility with ESP32-S3

**Alternatives:**
- **MQTT Client** (Arduino Library Manager)
- **AsyncMqttClient** (for non-blocking operation)

**Recommendation:** Continue with PubSubClient unless non-blocking operation becomes critical.

## Implementation Plan

### Phase 1: Optimize Current Implementation
1. Implement buffer overflow cap in store_sd.cpp
2. Enhance error handling for SD card failures
3. Improve MQTT message structure as recommended above

### Phase 2: Enhanced Data Management
1. Implement configurable data retention policies
2. Add data compression for long-term storage
3. Develop data recovery mechanisms for power failures

### Phase 3: Advanced Analytics
1. Set up server-side time-series database
2. Implement data pipeline for processing and analysis
3. Develop visualization dashboards

## Conclusion
The current data organization approach using CSV for local storage and JSON for MQTT is sound and well-suited to the project requirements. The recommendations in this document build upon this foundation to enhance reliability, scalability, and analytical capabilities while maintaining compatibility with the existing system.