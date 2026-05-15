# WQ Summary Display + HA-Configurable Thresholds Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the Pre-RO and Post-RO OLED frames with a single WQ Summary frame showing all three filter positions with configurable status indicators, update the header to show live time + session volume, and expose all thresholds/names/labels as HA entities.

**Architecture:** New `wq_config.{h,cpp}` module owns NVS persistence, eval logic, MQTT state publishing, and cmd parsing. HA discovery for the 16 new entities follows the existing pattern in `main.cpp`. `display_oled.cpp` calls `wq_config` getters directly — no coupling in the other direction.

**Tech Stack:** PlatformIO + Arduino framework, ESP-IDF Preferences (NVS), ArduinoJson v7, ThingPulse OLEDDisplayUi, PubSubClient MQTT over TLS.

---

## File Map

| File | Action | What changes |
|---|---|---|
| `include/config.h` | Modify | Add `TOPIC_WQ_CONFIG` macro |
| `src/wq_config.h` | Create | Public API for wq_config module |
| `src/wq_config.cpp` | Create | NVS load/save, getters, eval, state publish, cmd parse |
| `src/session_flow.h` | Modify | Add `sessionFlow_getCurrentVolumeOut()` declaration |
| `src/session_flow.cpp` | Modify | Implement `sessionFlow_getCurrentVolumeOut()` |
| `src/display_oled.cpp` | Modify | Update header; remove framePreRO/framePostRO; add frameWqSummary; frame array 7→6 |
| `src/main.cpp` | Modify | Wire wqConfig_begin(), publishHaDiscoveryWqConfig(), wqConfig_publishState(), wqConfig_handleCmd() |
| `docs/MQTT_TOPIC_MAP.md` | Modify | Add wq_config topic row |
| `docs/USER_OPERATIONS.md` | Modify | Update OLED frame table and header description |

---

## Task 1: Add TOPIC_WQ_CONFIG to config.h

**Files:**
- Modify: `include/config.h`

- [ ] **Step 1: Open `include/config.h` and add the new topic macro after the existing topic block**

  Find the line `#define TOPIC_OTA_STATE` and add after it:
  ```c
  #define TOPIC_WQ_CONFIG     "twwp/" NODE_ID "/wq_config"
  ```

- [ ] **Step 2: Compile to verify no errors**
  ```bash
  cd "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1"
  pio run 2>&1 | tail -5
  ```
  Expected: `SUCCESS`

- [ ] **Step 3: Commit**
  ```bash
  git add include/config.h
  git commit -m "feat(wq_config): add TOPIC_WQ_CONFIG to config.h"
  ```

---

## Task 2: Add sessionFlow_getCurrentVolumeOut()

**Files:**
- Modify: `src/session_flow.h` (add declaration)
- Modify: `src/session_flow.cpp` (add implementation)

- [ ] **Step 1: Add declaration to `src/session_flow.h`**

  Add after `sessionFlow_getLastPeakIn()`:
  ```cpp
  float    sessionFlow_getCurrentVolumeOut(); // live litres for active session; 0 when idle
  ```

- [ ] **Step 2: Add implementation to `src/session_flow.cpp`**

  Add at the end of the file, before any closing braces:
  ```cpp
  float sessionFlow_getCurrentVolumeOut() {
      if (sessionState == SessionState::IDLE) return 0.0f;
      float raw = sensorFlow_getTotalL(1) - sessionStartTotal1;
      return raw > 0.0f ? raw : 0.0f;
  }
  ```

- [ ] **Step 3: Compile**
  ```bash
  pio run 2>&1 | tail -5
  ```
  Expected: `SUCCESS`

- [ ] **Step 4: Commit**
  ```bash
  git add src/session_flow.h src/session_flow.cpp
  git commit -m "feat(session_flow): add getCurrentVolumeOut() getter for live session volume"
  ```

---

## Task 3: Create wq_config.h

**Files:**
- Create: `src/wq_config.h`

- [ ] **Step 1: Create `src/wq_config.h`**

  ```cpp
  #pragma once
  #include <Arduino.h>

  bool        wqConfig_begin();
  void        wqConfig_publishState();
  bool        wqConfig_handleCmd(const char* payload);

  const char* wqConfig_getPreRoName();
  const char* wqConfig_getPostRoName();
  const char* wqConfig_getReminName();

  float       wqConfig_getPreRoMax();
  float       wqConfig_getPostRoGoodMax();
  float       wqConfig_getPostRoCheckMax();
  float       wqConfig_getReminMin();
  float       wqConfig_getReminMax();

  const char* wqConfig_evalPreRo(float tds_ppm);
  const char* wqConfig_evalPostRo(float tds_ppm);
  const char* wqConfig_evalRemin(float tds_ppm);
  ```

- [ ] **Step 2: Compile**
  ```bash
  pio run 2>&1 | tail -5
  ```
  Expected: `SUCCESS`

---

## Task 4: Create wq_config.cpp

**Files:**
- Create: `src/wq_config.cpp`

- [ ] **Step 1: Create `src/wq_config.cpp`**

  ```cpp
  #include "wq_config.h"
  #include <Preferences.h>
  #include <ArduinoJson.h>
  #include "net_mqtt.h"
  #include "config.h"

  #define WQ_STR_MAX 16

  static Preferences s_prefs;

  static float s_preRoMax       = 110.0f;
  static float s_postRoGoodMax  =   5.0f;
  static float s_postRoCheckMax =   8.0f;
  static float s_reminMin       =  15.0f;
  static float s_reminMax       =  30.0f;

  static char s_preRoName[WQ_STR_MAX]   = "PRE-RO";
  static char s_postRoName[WQ_STR_MAX]  = "POST-RO";
  static char s_reminName[WQ_STR_MAX]   = "REMIN";
  static char s_preOkLbl[WQ_STR_MAX]    = "OK";
  static char s_preWrnLbl[WQ_STR_MAX]   = "WARN";
  static char s_postGdLbl[WQ_STR_MAX]   = "GOOD";
  static char s_postChkLbl[WQ_STR_MAX]  = "CHECK";
  static char s_postChgLbl[WQ_STR_MAX]  = "CHANGE";
  static char s_remLoLbl[WQ_STR_MAX]    = "LOW";
  static char s_remOkLbl[WQ_STR_MAX]    = "OK";
  static char s_remHiLbl[WQ_STR_MAX]    = "HIGH";

  static void loadStr(const char* key, char* buf, const char* def) {
      s_prefs.getString(key, buf, WQ_STR_MAX);
      if (buf[0] == '\0') strlcpy(buf, def, WQ_STR_MAX);
  }

  bool wqConfig_begin() {
      s_prefs.begin("wq_cfg", false);
      s_preRoMax       = s_prefs.getFloat("pre_ro_max",   110.0f);
      s_postRoGoodMax  = s_prefs.getFloat("post_ro_good",   5.0f);
      s_postRoCheckMax = s_prefs.getFloat("post_ro_chk",    8.0f);
      s_reminMin       = s_prefs.getFloat("remin_min",     15.0f);
      s_reminMax       = s_prefs.getFloat("remin_max",     30.0f);
      loadStr("pre_ro_name",   s_preRoName,   "PRE-RO");
      loadStr("post_ro_name",  s_postRoName,  "POST-RO");
      loadStr("remin_name",    s_reminName,   "REMIN");
      loadStr("pre_ok_lbl",    s_preOkLbl,    "OK");
      loadStr("pre_wrn_lbl",   s_preWrnLbl,   "WARN");
      loadStr("post_gd_lbl",   s_postGdLbl,   "GOOD");
      loadStr("post_chk_lbl",  s_postChkLbl,  "CHECK");
      loadStr("post_chg_lbl",  s_postChgLbl,  "CHANGE");
      loadStr("rem_lo_lbl",    s_remLoLbl,     "LOW");
      loadStr("rem_ok_lbl",    s_remOkLbl,     "OK");
      loadStr("rem_hi_lbl",    s_remHiLbl,     "HIGH");
      s_prefs.end();
      Serial.println("[WQ_CFG] loaded");
      return true;
  }

  void wqConfig_publishState() {
      JsonDocument doc;
      doc["pre_ro_max"]           = s_preRoMax;
      doc["post_ro_good_max"]     = s_postRoGoodMax;
      doc["post_ro_check_max"]    = s_postRoCheckMax;
      doc["remin_min"]            = s_reminMin;
      doc["remin_max"]            = s_reminMax;
      doc["pre_ro_name"]          = s_preRoName;
      doc["post_ro_name"]         = s_postRoName;
      doc["remin_name"]           = s_reminName;
      doc["pre_ro_ok_label"]      = s_preOkLbl;
      doc["pre_ro_warn_label"]    = s_preWrnLbl;
      doc["post_ro_good_label"]   = s_postGdLbl;
      doc["post_ro_check_label"]  = s_postChkLbl;
      doc["post_ro_change_label"] = s_postChgLbl;
      doc["remin_low_label"]      = s_remLoLbl;
      doc["remin_ok_label"]       = s_remOkLbl;
      doc["remin_high_label"]     = s_remHiLbl;
      char buf[640];
      size_t n = serializeJson(doc, buf, sizeof(buf));
      if (n == 0 || n >= sizeof(buf)) {
          Serial.println("[WQ_CFG] state JSON overflow");
          return;
      }
      netMqtt_publish(TOPIC_WQ_CONFIG, buf, true);
  }

  bool wqConfig_handleCmd(const char* payload) {
      JsonDocument doc;
      if (deserializeJson(doc, payload)) return false;
      bool changed = false;
      s_prefs.begin("wq_cfg", false);

      auto setF = [&](const char* jk, const char* nk, float& v, float lo, float hi) {
          if (!doc[jk].isNull()) {
              v = constrain(doc[jk].as<float>(), lo, hi);
              s_prefs.putFloat(nk, v);
              changed = true;
          }
      };
      auto setS = [&](const char* jk, const char* nk, char* buf) {
          if (!doc[jk].isNull()) {
              strlcpy(buf, doc[jk].as<const char*>(), WQ_STR_MAX);
              s_prefs.putString(nk, buf);
              changed = true;
          }
      };

      setF("wq_pre_ro_max",          "pre_ro_max",   s_preRoMax,       0, 500);
      setF("wq_post_ro_good_max",    "post_ro_good",  s_postRoGoodMax,  0,  50);
      setF("wq_post_ro_check_max",   "post_ro_chk",   s_postRoCheckMax, 0,  50);
      setF("wq_remin_min",           "remin_min",     s_reminMin,       0, 100);
      setF("wq_remin_max",           "remin_max",     s_reminMax,       0, 100);
      setS("wq_pre_ro_name",         "pre_ro_name",   s_preRoName);
      setS("wq_post_ro_name",        "post_ro_name",  s_postRoName);
      setS("wq_remin_name",          "remin_name",    s_reminName);
      setS("wq_pre_ro_ok_label",     "pre_ok_lbl",    s_preOkLbl);
      setS("wq_pre_ro_warn_label",   "pre_wrn_lbl",   s_preWrnLbl);
      setS("wq_post_ro_good_label",  "post_gd_lbl",   s_postGdLbl);
      setS("wq_post_ro_check_label", "post_chk_lbl",  s_postChkLbl);
      setS("wq_post_ro_change_label","post_chg_lbl",  s_postChgLbl);
      setS("wq_remin_low_label",     "rem_lo_lbl",    s_remLoLbl);
      setS("wq_remin_ok_label",      "rem_ok_lbl",    s_remOkLbl);
      setS("wq_remin_high_label",    "rem_hi_lbl",    s_remHiLbl);

      s_prefs.end();
      if (changed) wqConfig_publishState();
      return changed;
  }

  const char* wqConfig_getPreRoName()     { return s_preRoName; }
  const char* wqConfig_getPostRoName()    { return s_postRoName; }
  const char* wqConfig_getReminName()     { return s_reminName; }
  float       wqConfig_getPreRoMax()      { return s_preRoMax; }
  float       wqConfig_getPostRoGoodMax() { return s_postRoGoodMax; }
  float       wqConfig_getPostRoCheckMax(){ return s_postRoCheckMax; }
  float       wqConfig_getReminMin()      { return s_reminMin; }
  float       wqConfig_getReminMax()      { return s_reminMax; }

  const char* wqConfig_evalPreRo(float tds) {
      return tds <= s_preRoMax ? s_preOkLbl : s_preWrnLbl;
  }
  const char* wqConfig_evalPostRo(float tds) {
      if (tds <= s_postRoGoodMax)  return s_postGdLbl;
      if (tds <= s_postRoCheckMax) return s_postChkLbl;
      return s_postChgLbl;
  }
  const char* wqConfig_evalRemin(float tds) {
      if (tds < s_reminMin)  return s_remLoLbl;
      if (tds <= s_reminMax) return s_remOkLbl;
      return s_remHiLbl;
  }
  ```

- [ ] **Step 2: Compile**
  ```bash
  pio run 2>&1 | tail -5
  ```
  Expected: `SUCCESS`

- [ ] **Step 3: Commit**
  ```bash
  git add src/wq_config.h src/wq_config.cpp
  git commit -m "feat(wq_config): NVS persistence, eval logic, state publish, cmd parse"
  ```

---

## Task 5: Update display_oled.cpp — header and WQ Summary frame

**Files:**
- Modify: `src/display_oled.cpp`

- [ ] **Step 1: Add includes for new dependencies at top of `src/display_oled.cpp`**

  After the existing includes, add:
  ```cpp
  #include "time_rtc.h"
  #include "session_flow.h"
  #include "wq_config.h"
  ```

- [ ] **Step 2: Update the header draw function — replace the left-side flow text**

  Find and replace in `drawHeader()`:
  ```cpp
  // OLD:
  d->setTextAlignment(TEXT_ALIGN_LEFT);
  snprintf(buf, sizeof(buf), "%.1fL", sensorFlow_getTodayL(1));
  d->drawString(0, 0, buf);
  ```
  Replace with:
  ```cpp
  d->setTextAlignment(TEXT_ALIGN_LEFT);
  String ts = timeRtc_getISOTimestamp();
  float sesVol = sessionFlow_getCurrentVolumeOut();
  snprintf(buf, sizeof(buf), "%c%c:%c%c %.1fL",
           ts[11], ts[12], ts[14], ts[15], sesVol);
  d->drawString(0, 0, buf);
  ```

- [ ] **Step 3: Add the WQ Summary frame function — insert before `framePreRO`**

  Replace the `framePreRO` and `framePostRO` functions entirely with `frameWqSummary`:
  ```cpp
  // ── Frame 0: WQ Summary ────────────────────────────────────────────────────
  static void frameWqSummary(OLEDDisplay* d, OLEDDisplayUiState*, int16_t x, int16_t y) {
      char buf[24];
      d->setFont(ArialMT_Plain_10);

      // Row 1: Pre-RO
      bool preOn  = sensorTdsMeter_isOnline(TDS_ZONE_PRE_RO);
      float preTds = preOn ? sensorTdsMeter_getTds(TDS_ZONE_PRE_RO) : 0.0f;
      d->setTextAlignment(TEXT_ALIGN_LEFT);
      d->drawString(x, y + 14, wqConfig_getPreRoName());
      d->setTextAlignment(TEXT_ALIGN_CENTER);
      if (preOn) snprintf(buf, sizeof(buf), "%dppm", (int)preTds);
      else       strlcpy(buf, "---", sizeof(buf));
      d->drawString(x + 64, y + 14, buf);
      d->setTextAlignment(TEXT_ALIGN_RIGHT);
      d->drawString(x + 128, y + 14, preOn ? wqConfig_evalPreRo(preTds) : "---");

      // Row 2: Post-RO
      bool postOn  = sensorTdsMeter_isOnline(TDS_ZONE_POST_RO);
      float postTds = postOn ? sensorTdsMeter_getTds(TDS_ZONE_POST_RO) : 0.0f;
      d->setTextAlignment(TEXT_ALIGN_LEFT);
      d->drawString(x, y + 31, wqConfig_getPostRoName());
      d->setTextAlignment(TEXT_ALIGN_CENTER);
      if (postOn) snprintf(buf, sizeof(buf), "%dppm", (int)postTds);
      else        strlcpy(buf, "---", sizeof(buf));
      d->drawString(x + 64, y + 31, buf);
      d->setTextAlignment(TEXT_ALIGN_RIGHT);
      d->drawString(x + 128, y + 31, postOn ? wqConfig_evalPostRo(postTds) : "---");

      // Row 3: Remin
      bool remOn  = sensorYieryi_isOnline(YIERYI_ZONE_REMIN);
      float remTds = remOn ? sensorYieryi_getTdsPpm(YIERYI_ZONE_REMIN) : 0.0f;
      d->setTextAlignment(TEXT_ALIGN_LEFT);
      d->drawString(x, y + 48, wqConfig_getReminName());
      d->setTextAlignment(TEXT_ALIGN_CENTER);
      if (remOn) snprintf(buf, sizeof(buf), "%dppm", (int)remTds);
      else       strlcpy(buf, "---", sizeof(buf));
      d->drawString(x + 64, y + 48, buf);
      d->setTextAlignment(TEXT_ALIGN_RIGHT);
      d->drawString(x + 128, y + 48, remOn ? wqConfig_evalRemin(remTds) : "---");
  }
  ```

- [ ] **Step 4: Update the frame registry — replace `framePreRO, framePostRO` with `frameWqSummary`, update count**

  Find:
  ```cpp
  static FrameCallback   s_frames[]   = { framePreRO, framePostRO, frameRemin,
                                           frameFlow, frameTank, frameSystemHealth,
                                           frameBranding };
  ```
  Replace with:
  ```cpp
  static FrameCallback   s_frames[]   = { frameWqSummary, frameRemin,
                                           frameFlow, frameTank, frameSystemHealth,
                                           frameBranding };
  ```

- [ ] **Step 5: Update `setFrames` call in `displayOled_begin()` — change 7 to 6**

  Find:
  ```cpp
  s_ui.setFrames(s_frames, 7);
  ```
  Replace with:
  ```cpp
  s_ui.setFrames(s_frames, 6);
  ```

- [ ] **Step 6: Compile**
  ```bash
  pio run 2>&1 | tail -5
  ```
  Expected: `SUCCESS`. If there are unused variable warnings for the now-removed `drawTdsMeterFrame` or `drawZoneFrame` helpers, remove those static functions too.

- [ ] **Step 7: Commit**
  ```bash
  git add src/display_oled.cpp
  git commit -m "feat(oled): WQ summary frame replaces Pre-RO/Post-RO; header shows time + session volume"
  ```

---

## Task 6: Wire wq_config into main.cpp

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Add `#include "wq_config.h"` near the top of `src/main.cpp`**

  Add after the other driver includes (near the other `#include` lines at the top):
  ```cpp
  #include "wq_config.h"
  ```

- [ ] **Step 2: Add `wqConfig_begin()` to `setup()`**

  In `setup()`, after `sensorTdsMeter_begin();` and before `sensorPressure_begin();`, add:
  ```cpp
  wqConfig_begin();
  ```

- [ ] **Step 3: Add HA discovery function — insert before `publishOnlineState()`**

  Add this new static function immediately before `static void publishOnlineState()`:
  ```cpp
  static bool publishHaDiscoveryWqConfig() {
      bool ok = true;
      const char* stateTopic = TOPIC_WQ_CONFIG;

      struct NumEnt { const char* uid; const char* name; const char* vk; const char* ck;
                      float mn; float mx; float st; const char* unit; } nums[] = {
          { "twwp_" NODE_ID "_wq_pre_ro_max",       "Pre-RO Max TDS",     "pre_ro_max",       "wq_pre_ro_max",       0, 500, 1,   "ppm" },
          { "twwp_" NODE_ID "_wq_post_ro_good_max",  "Post-RO Good Max",   "post_ro_good_max", "wq_post_ro_good_max", 0,  50, 0.5, "ppm" },
          { "twwp_" NODE_ID "_wq_post_ro_check_max", "Post-RO Check Max",  "post_ro_check_max","wq_post_ro_check_max",0,  50, 0.5, "ppm" },
          { "twwp_" NODE_ID "_wq_remin_min",         "Remin Min TDS",      "remin_min",        "wq_remin_min",        0, 100, 1,   "ppm" },
          { "twwp_" NODE_ID "_wq_remin_max",         "Remin Max TDS",      "remin_max",        "wq_remin_max",        0, 100, 1,   "ppm" },
      };
      for (auto& n : nums) {
          JsonDocument doc;
          doc["name"]             = n.name;
          doc["unique_id"]        = n.uid;
          doc["object_id"]        = n.uid;
          doc["entity_category"]  = "config";
          doc["state_topic"]      = stateTopic;
          char tmpl[80];
          snprintf(tmpl, sizeof(tmpl), "{{ value_json.%s }}", n.vk);
          doc["value_template"]   = tmpl;
          doc["command_topic"]    = TOPIC_CMD;
          char ctmpl[80];
          snprintf(ctmpl, sizeof(ctmpl), "{\"%s\": {{ value }}}", n.ck);
          doc["command_template"] = ctmpl;
          doc["unit_of_measurement"] = n.unit;
          doc["min"]  = n.mn;
          doc["max"]  = n.mx;
          doc["step"] = n.st;
          doc["mode"] = "box";
          doc["icon"] = "mdi:water-check";
          doc["availability_topic"]    = TOPIC_LWT;
          doc["payload_available"]     = "online";
          doc["payload_not_available"] = "offline";
          fillHaDevice(doc);
          char payload[768];
          if (!serializeDoc(doc, payload, sizeof(payload))) { ok = false; continue; }
          char topic[128];
          snprintf(topic, sizeof(topic), "homeassistant/number/%s/config", n.uid);
          if (!netMqtt_publish(topic, payload, true)) ok = false;
      }

      struct TxtEnt { const char* uid; const char* name; const char* vk; const char* ck; } txts[] = {
          { "twwp_" NODE_ID "_wq_pre_ro_name",         "Pre-RO Name",           "pre_ro_name",         "wq_pre_ro_name"         },
          { "twwp_" NODE_ID "_wq_post_ro_name",        "Post-RO Name",          "post_ro_name",        "wq_post_ro_name"        },
          { "twwp_" NODE_ID "_wq_remin_name",          "Remin Name",            "remin_name",          "wq_remin_name"          },
          { "twwp_" NODE_ID "_wq_pre_ro_ok_label",     "Pre-RO OK Label",       "pre_ro_ok_label",     "wq_pre_ro_ok_label"     },
          { "twwp_" NODE_ID "_wq_pre_ro_warn_label",   "Pre-RO Warn Label",     "pre_ro_warn_label",   "wq_pre_ro_warn_label"   },
          { "twwp_" NODE_ID "_wq_post_ro_good_label",  "Post-RO Good Label",    "post_ro_good_label",  "wq_post_ro_good_label"  },
          { "twwp_" NODE_ID "_wq_post_ro_check_label", "Post-RO Check Label",   "post_ro_check_label", "wq_post_ro_check_label" },
          { "twwp_" NODE_ID "_wq_post_ro_change_label","Post-RO Change Label",  "post_ro_change_label","wq_post_ro_change_label"},
          { "twwp_" NODE_ID "_wq_remin_low_label",     "Remin Low Label",       "remin_low_label",     "wq_remin_low_label"     },
          { "twwp_" NODE_ID "_wq_remin_ok_label",      "Remin OK Label",        "remin_ok_label",      "wq_remin_ok_label"      },
          { "twwp_" NODE_ID "_wq_remin_high_label",    "Remin High Label",      "remin_high_label",    "wq_remin_high_label"    },
      };
      for (auto& t : txts) {
          JsonDocument doc;
          doc["name"]             = t.name;
          doc["unique_id"]        = t.uid;
          doc["object_id"]        = t.uid;
          doc["entity_category"]  = "config";
          doc["state_topic"]      = stateTopic;
          char tmpl[80];
          snprintf(tmpl, sizeof(tmpl), "{{ value_json.%s }}", t.vk);
          doc["value_template"]   = tmpl;
          doc["command_topic"]    = TOPIC_CMD;
          char ctmpl[80];
          snprintf(ctmpl, sizeof(ctmpl), "{\"%s\": \"{{ value }}\"}", t.ck);
          doc["command_template"] = ctmpl;
          doc["max"]  = 15;
          doc["icon"] = "mdi:label-outline";
          doc["availability_topic"]    = TOPIC_LWT;
          doc["payload_available"]     = "online";
          doc["payload_not_available"] = "offline";
          fillHaDevice(doc);
          char payload[768];
          if (!serializeDoc(doc, payload, sizeof(payload))) { ok = false; continue; }
          char topic[128];
          snprintf(topic, sizeof(topic), "homeassistant/text/%s/config", t.uid);
          if (!netMqtt_publish(topic, payload, true)) ok = false;
      }

      Serial.print("[MQTT] HA WQ config discovery ");
      Serial.println(ok ? "published" : "partial");
      return ok;
  }
  ```

- [ ] **Step 4: Add `publishHaDiscoveryWqConfig()` and `wqConfig_publishState()` to `publishOnlineState()`**

  In `publishOnlineState()`, after the `publishHaDiscoveryTdsMeter();` line, add:
  ```cpp
  publishHaDiscoveryWqConfig();
  wqConfig_publishState();
  ```

- [ ] **Step 5: Add `wqConfig_handleCmd(payload)` to `handleCmd()`**

  At the top of `handleCmd()`, after `deserializeJson` succeeds, add:
  ```cpp
  wqConfig_handleCmd(payload);
  ```

  The exact location — after `if (deserializeJson(doc, payload)) { ... return; }`, add:
  ```cpp
  wqConfig_handleCmd(payload);
  ```

- [ ] **Step 6: Compile**
  ```bash
  pio run 2>&1 | tail -5
  ```
  Expected: `SUCCESS`

- [ ] **Step 7: Commit**
  ```bash
  git add src/main.cpp
  git commit -m "feat(wq_config): wire begin, HA discovery, state publish, cmd handling into main"
  ```

---

## Task 7: Update docs

**Files:**
- Modify: `docs/MQTT_TOPIC_MAP.md`
- Modify: `docs/USER_OPERATIONS.md`

- [ ] **Step 1: Add wq_config topic to `docs/MQTT_TOPIC_MAP.md`**

  Add a row in the node→broker section:
  ```
  | `twwp/<id>/wq_config` | node → broker | 1 | yes | Retained JSON of all WQ threshold, label, and name config values. Published on connect and after any MQTT cmd change. |
  ```

- [ ] **Step 2: Update OLED frame table in `docs/USER_OPERATIONS.md`**

  Find the OLED frame table. Replace the Pre-RO (frame 0) and Post-RO (frame 1) rows with a single WQ Summary row, and renumber remaining frames:

  ```
  | 0 | WQ SUMMARY | All three filter positions: name, TDS ppm, status label (OK/WARN/GOOD/CHECK/CHANGE/LOW/HIGH). Thresholds and labels configurable from HA. Sensor offline shows `---`. |
  | 1 | REMIN | Remineralised water — pH, ORP, TDS, temperature from YiErYi meter. |
  | 2 | FLOW & WASTE | Output flow rate, input feed rate, waste ratio. |
  | 3 | STORAGE TANK | Estimated tank level bar, %, litres, ETA to full. |
  | 4 | SYS HEALTH | Battery voltage/%, WiFi signal, uptime, offline buffer count. |
  | 5 | BRANDING | TWWP logo. |
  ```

- [ ] **Step 3: Update header description in `docs/USER_OPERATIONS.md`**

  Find the header description table row for "Left". Replace:
  ```
  | Left — today's volume | `14.2L` — litres used today on output sensor. |
  ```
  With:
  ```
  | Left — time + session volume | `10:34 2.3L` — current RTC time (HH:MM) and live session volume from output sensor. Volume counts up while a tap session is active; resets to 0.0L between sessions. |
  ```

- [ ] **Step 4: Commit**
  ```bash
  git add docs/MQTT_TOPIC_MAP.md docs/USER_OPERATIONS.md
  git commit -m "docs: update MQTT topic map and USER_OPERATIONS for WQ summary frame"
  ```

---

## Task 8: Final compile and verify

- [ ] **Step 1: Full clean build**
  ```bash
  cd "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1"
  pio run 2>&1 | grep -E "ERROR|WARNING|SUCCESS|RAM|Flash"
  ```
  Expected: `SUCCESS`. Note RAM and Flash usage — flag if RAM >85% or Flash >90%.

- [ ] **Step 2: Check for unused static function warnings**

  `drawTdsMeterFrame` in `display_oled.cpp` is now unused (it was only called by the removed `framePreRO` and `framePostRO`). Remove the entire `drawTdsMeterFrame` static function. `drawZoneFrame` is still used by `frameRemin` — leave it. Recompile after removal.

- [ ] **Step 3: Final commit if any cleanup was needed**
  ```bash
  git add src/display_oled.cpp
  git commit -m "chore(oled): remove unused drawTdsMeterFrame helper"
  ```
