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
    loadStr("pre_ro_name",  s_preRoName,  "PRE-RO");
    loadStr("post_ro_name", s_postRoName, "POST-RO");
    loadStr("remin_name",   s_reminName,  "REMIN");
    loadStr("pre_ok_lbl",   s_preOkLbl,   "OK");
    loadStr("pre_wrn_lbl",  s_preWrnLbl,  "WARN");
    loadStr("post_gd_lbl",  s_postGdLbl,  "GOOD");
    loadStr("post_chk_lbl", s_postChkLbl, "CHECK");
    loadStr("post_chg_lbl", s_postChgLbl, "CHANGE");
    loadStr("rem_lo_lbl",   s_remLoLbl,   "LOW");
    loadStr("rem_ok_lbl",   s_remOkLbl,   "OK");
    loadStr("rem_hi_lbl",   s_remHiLbl,   "HIGH");
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

    setF("wq_pre_ro_max",           "pre_ro_max",   s_preRoMax,       0, 500);
    setF("wq_post_ro_good_max",     "post_ro_good",  s_postRoGoodMax,  0,  50);
    setF("wq_post_ro_check_max",    "post_ro_chk",   s_postRoCheckMax, 0,  50);
    setF("wq_remin_min",            "remin_min",     s_reminMin,       0, 100);
    setF("wq_remin_max",            "remin_max",     s_reminMax,       0, 100);
    setS("wq_pre_ro_name",          "pre_ro_name",   s_preRoName);
    setS("wq_post_ro_name",         "post_ro_name",  s_postRoName);
    setS("wq_remin_name",           "remin_name",    s_reminName);
    setS("wq_pre_ro_ok_label",      "pre_ok_lbl",    s_preOkLbl);
    setS("wq_pre_ro_warn_label",    "pre_wrn_lbl",   s_preWrnLbl);
    setS("wq_post_ro_good_label",   "post_gd_lbl",   s_postGdLbl);
    setS("wq_post_ro_check_label",  "post_chk_lbl",  s_postChkLbl);
    setS("wq_post_ro_change_label", "post_chg_lbl",  s_postChgLbl);
    setS("wq_remin_low_label",      "rem_lo_lbl",    s_remLoLbl);
    setS("wq_remin_ok_label",       "rem_ok_lbl",    s_remOkLbl);
    setS("wq_remin_high_label",     "rem_hi_lbl",    s_remHiLbl);

    s_prefs.end();
    if (changed) wqConfig_publishState();
    return changed;
}

const char* wqConfig_getPreRoName()      { return s_preRoName; }
const char* wqConfig_getPostRoName()     { return s_postRoName; }
const char* wqConfig_getReminName()      { return s_reminName; }
float       wqConfig_getPreRoMax()       { return s_preRoMax; }
float       wqConfig_getPostRoGoodMax()  { return s_postRoGoodMax; }
float       wqConfig_getPostRoCheckMax() { return s_postRoCheckMax; }
float       wqConfig_getReminMin()       { return s_reminMin; }
float       wqConfig_getReminMax()       { return s_reminMax; }

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
