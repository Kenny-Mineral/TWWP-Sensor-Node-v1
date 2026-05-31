#pragma once
#include <Arduino.h>

struct FlowKPoint {
    float flowLpm;
    float kPulsesPerL;
};

float interpolateK(float flowLpm, const FlowKPoint* table, int len);

bool  sensorFlow_begin();
void  sensorFlow_loop();
float sensorFlow_getRateLpm(uint8_t ch);   // L/min, ch = 1, 2, or 3
float sensorFlow_getTotalL(uint8_t ch);    // lifetime total, L (persisted)
uint64_t sensorFlow_getTotalPulses(uint8_t ch); // lifetime raw pulse total (persisted)
float sensorFlow_getTodayL(uint8_t ch);    // L since midnight
float sensorFlow_getWeekL(uint8_t ch);     // L since Monday midnight
float sensorFlow_getMonthL(uint8_t ch);    // L since 1st of month
float sensorFlow_getYearL(uint8_t ch);     // L since 1 Jan
float sensorFlow_getKFactor(uint8_t ch);        // configured nominal K value (pulses/L)
bool  sensorFlow_setKFactor(uint8_t ch, float k);      // update K in RAM + save to node.json
float sensorFlow_getAppliedKFactor(uint8_t ch);        // current interpolated K value
float sensorFlow_getFlowAvgWindowRate(uint8_t ch);     // smoothed (moving-avg) flow rate for the channel
bool  sensorFlow_setKTable(uint8_t ch, const char* json); // set K-table from JSON string, save to node.json
bool  sensorFlow_setDebounceUs(uint8_t ch, uint32_t us);  // set debounce (100-10000 us), saved to node.json
uint32_t sensorFlow_getDebounceUs(uint8_t ch);             // get current debounce (us)
bool  sensorFlow_setFlowAvgWindow(uint8_t windowSize);     // set moving average window (1-20)
uint8_t sensorFlow_getFlowAvgWindow();                     // get current window size
const char* sensorFlow_getKTableJson(uint8_t ch);          // get K-table as JSON string (static buffer)
bool        sensorFlow_setModel(uint8_t ch, const char* model); // select sensor model by name; resets to model defaults
const char* sensorFlow_getModel(uint8_t ch);               // current model name for channel
void        sensorFlow_getModelList(char* buf, size_t len);// comma-separated list of known model names
void  sensorFlow_resetToday(uint8_t ch);   // today only — ch=1, ch=2, ch=3, or ch=0 for all
void  sensorFlow_resetWeek(uint8_t ch);    // this week only
void  sensorFlow_resetMonth(uint8_t ch);   // this month only
void  sensorFlow_resetYear(uint8_t ch);    // this year only
void  sensorFlow_resetTotals(uint8_t ch);  // all subtotals + lifetime total + NVS for channel(s)
void  sensorFlow_factoryReset();           // wipe everything, all channels, clear NVS

// Per-channel calibration state machine (IDLE → COLLECTING → DONE → IDLE)
// Safety: auto-aborts after FLOW_CAL_IDLE_TIMEOUT_MS with no pulses; calCommit rejects < FLOW_CAL_MIN_PULSES
void        sensorFlow_calBegin(uint8_t ch);
bool        sensorFlow_calCommit(uint8_t ch);   // returns false + brief "too_few_pulses" state if < MIN_PULSES
bool        sensorFlow_calAccept(uint8_t ch);
void        sensorFlow_calAbort(uint8_t ch);
void        sensorFlow_setCalRefVol(uint8_t ch, float volL);
const char* sensorFlow_getCalState(uint8_t ch); // "idle"|"collecting"|"done"|"timed_out"|"too_few_pulses"
int         sensorFlow_getCalSecsUntilTimeout(uint8_t ch); // -1=not at risk, N=secs until auto-abort
float       sensorFlow_getCalSuggestedK(uint8_t ch);
uint64_t    sensorFlow_getCalPulsesSinceStart(uint8_t ch);
float       sensorFlow_getCalRefVol(uint8_t ch);
