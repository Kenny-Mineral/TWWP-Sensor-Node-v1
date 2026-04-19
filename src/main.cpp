#include <Arduino.h>
#include "config.h"
#include "net_wifi.h"
#include "net_mqtt.h"
#include "time_rtc.h"
#include "store_sd.h"
#include "watchdog.h"
#include "status_led.h"
#include "sensor_leak.h"
#include "sensor_flow_stub.h"
#include "sensor_pressure_stub.h"
#include "sensor_temp_stub.h"
#include "actuator_solenoid_stub.h"

void setup() {
    Serial.begin(115200);

    statusLed_begin();
    statusLed_setState(LedState::BOOTING);

    watchdog_begin();

    storeSd_begin();
    timeRtc_begin();

    sensorLeak_begin();
    sensorFlow_begin();
    sensorPressure_begin();
    sensorTemp_begin();
    actuatorSolenoid_begin();

    netWifi_begin();
    netMqtt_begin();
}

void loop() {
    watchdog_feed();

    storeSd_loop();
    timeRtc_loop();
    statusLed_loop();

    sensorLeak_loop();
    sensorFlow_loop();
    sensorPressure_loop();
    sensorTemp_loop();
    actuatorSolenoid_loop();

    netWifi_loop();
    netMqtt_loop();
}
