#include "net_ap.h"

#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config.h"
#include "net_wifi.h"
#include "store_sd.h"

#ifndef AP_PASS
#define AP_PASS "wateriswet"
#endif

#ifndef NODE_FIRMWARE_VERSION
#define NODE_FIRMWARE_VERSION "0.0.0"
#endif

static WebServer s_server(AP_PORT);
static bool s_serverStarted = false;
static bool s_routesReady = false;
static bool s_apActive = false;
static bool s_autoTriggeredWaitingRecovery = false;
static unsigned long s_apExpiresAtMs = 0;
static unsigned long s_disconnectSinceMs = 0;
static unsigned long s_weakSinceMs = 0;
static unsigned long s_lastConfigLoadMs = 0;
static uint32_t s_autoTriggerLossMs = AP_AUTO_TRIGGER_LOSS_MS;
static int s_weakRssiThreshold = AP_AUTO_TRIGGER_RSSI_THRESHOLD;
static uint32_t s_autoDurationS = AP_AUTO_DURATION_S;
static String s_apSsid;
static String s_uploadToken;

static const char* FALLBACK_HTML = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>TWWP Sync Portal</title>
  <style>
    :root { color-scheme: light; --bg:#e7f6f7; --ink:#10343b; --muted:#4f6f76; --panel:#ffffff; --line:#b9d7db; --brand:#0b8ea0; --brand-dark:#0b5560; --ok:#1f8b4c; }
    * { box-sizing:border-box; }
    body { margin:0; font-family:Arial,sans-serif; background:linear-gradient(180deg,#d9f1f2 0%,#eef8f8 100%); color:var(--ink); }
    .wrap { max-width:720px; margin:0 auto; padding:24px 16px 48px; }
    .card { background:var(--panel); border:1px solid var(--line); border-radius:18px; padding:18px; box-shadow:0 14px 40px rgba(16,52,59,0.10); }
    h1 { margin:0 0 4px; font-size:28px; }
    h2 { margin:24px 0 8px; font-size:18px; }
    p { line-height:1.5; color:var(--muted); }
    .meta { display:flex; flex-wrap:wrap; gap:12px; margin:14px 0 0; }
    .pill { border:1px solid var(--line); border-radius:999px; padding:8px 12px; font-size:14px; color:var(--brand-dark); background:#f6fbfb; }
    .grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(140px,1fr)); gap:12px; margin:16px 0; }
    label { display:block; font-weight:600; margin:14px 0 8px; }
    input, select, button { width:100%; font:inherit; }
    input, select { padding:12px; border:1px solid var(--line); border-radius:12px; background:#fff; }
    button { border:0; border-radius:14px; padding:13px 16px; margin-top:12px; font-weight:700; cursor:pointer; background:var(--brand); color:#fff; }
    button.secondary { background:#fff; color:var(--brand-dark); border:1px solid var(--line); }
    button:disabled { opacity:0.45; cursor:not-allowed; }
    .actions { display:grid; gap:10px; margin-top:16px; }
    .status { margin-top:16px; padding:12px 14px; border-radius:14px; background:#f4fbfb; border:1px solid var(--line); white-space:pre-line; }
    .small { font-size:13px; color:var(--muted); }
    a.cta { display:inline-block; margin-top:14px; color:#fff; background:var(--brand-dark); padding:12px 16px; border-radius:999px; text-decoration:none; font-weight:700; }
    .hidden { display:none; }
  </style>
</head>
<body>
  <div class="wrap">
    <div class="card">
      <h1 id="title">TWWP Offline Sync</h1>
      <p id="intro">Use your phone to relay buffered node data to The Wholey Water Project.</p>
      <div class="meta">
        <div class="pill" id="nodeMeta">Node: loading...</div>
        <div class="pill" id="countMeta">Buffered messages: ...</div>
        <div class="pill" id="onlineMeta">Internet: checking...</div>
      </div>

      <div class="grid">
        <div>
          <label for="batch">Batch size</label>
          <select id="batch">
            <option value="10">Last 10</option>
            <option value="50">Last 50</option>
            <option value="100">Last 100</option>
            <option value="500">All available</option>
          </select>
        </div>
        <div id="emailBlock">
          <label for="email">Optional email</label>
          <input id="email" type="email" autocomplete="email" placeholder="you@example.com">
        </div>
      </div>

      <div class="actions">
        <button id="downloadBtn">1. Download from node</button>
        <button id="uploadBtn" disabled>2. Upload to TWWP</button>
        <button id="ackBtn" class="secondary" disabled>3. Clear uploaded buffer</button>
      </div>

      <div class="status" id="statusBox">Loading buffer stats...</div>
      <div id="signupBlock" class="hidden">
        <h2>Thanks for helping</h2>
        <p>Want free access to TWWP water? Create an account and start using the wider platform.</p>
        <a class="cta" href="https://app.thewholeywaterproject.com/users/sign_up">Create account</a>
      </div>
      <p class="small">If upload fails while connected to the node WiFi, try again after your phone switches back to cellular data or another internet connection.</p>
    </div>
  </div>
  <script>
    const memberFlow = new URLSearchParams(location.search).get('member') === '1';
    const state = { stats:null, messages:[], uploaded:false };
    const $ = (id) => document.getElementById(id);
    const statusBox = $('statusBox');
    const uploadBtn = $('uploadBtn');
    const ackBtn = $('ackBtn');
    const emailBlock = $('emailBlock');
    const signupBlock = $('signupBlock');
    if (memberFlow) {
      $('title').textContent = 'Syncing node data for TWWP';
      $('intro').textContent = 'Download buffered data from the node, then upload it to TWWP.';
      emailBlock.classList.add('hidden');
    }
    function setStatus(msg) { statusBox.textContent = msg; }
    function setOnline() { $('onlineMeta').textContent = 'Internet: ' + (navigator.onLine ? 'available' : 'offline on node WiFi'); }
    window.addEventListener('online', setOnline);
    window.addEventListener('offline', setOnline);
    setOnline();
    async function loadStats() {
      const res = await fetch('/api/buffer/stats');
      state.stats = await res.json();
      $('nodeMeta').textContent = `Node: ${state.stats.node_id} · FW ${state.stats.firmware}`;
      $('countMeta').textContent = `Buffered messages: ${state.stats.count}`;
      if (state.stats.count === 0) {
        $('downloadBtn').disabled = true;
        setStatus('No buffered messages are waiting on this node.');
        return;
      }
      setStatus(`Ready to sync ${state.stats.count} buffered messages from ${state.stats.node_id}.`);
    }
    $('downloadBtn').addEventListener('click', async () => {
      if (!state.stats) return;
      const wanted = Math.min(parseInt($('batch').value, 10), state.stats.count || 0, 500);
      setStatus(`Downloading ${wanted} messages from node...`);
      const res = await fetch('/api/buffer/fetch?count=' + wanted);
      state.messages = await res.json();
      uploadBtn.disabled = state.messages.length === 0;
      ackBtn.disabled = true;
      state.uploaded = false;
      setStatus(`Downloaded ${state.messages.length} messages. Keep this page open and tap Upload when internet is available.`);
    });
    uploadBtn.addEventListener('click', async () => {
      if (!state.stats || !state.messages.length) return;
      const body = {
        node_id: state.stats.node_id,
        token: state.stats.upload_token,
        messages: state.messages
      };
      if (!memberFlow) {
        const email = $('email').value.trim();
        if (email) body.uploader_email = email;
      }
      setStatus('Uploading messages to TWWP relay...');
      const res = await fetch(state.stats.upload_url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body)
      });
      if (!res.ok) {
        throw new Error('Relay upload failed with HTTP ' + res.status);
      }
      const payload = await res.json();
      state.uploaded = true;
      ackBtn.disabled = false;
      setStatus(`Relay accepted ${payload.published || state.messages.length} messages. Tap Clear uploaded buffer while this page can still reach the node.`);
      if (!memberFlow) signupBlock.classList.remove('hidden');
    });
    ackBtn.addEventListener('click', async () => {
      if (!state.uploaded || !state.messages.length) return;
      setStatus('Clearing uploaded messages from node buffer...');
      const res = await fetch('/api/buffer/ack', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ count: state.messages.length })
      });
      const payload = await res.json();
      setStatus(`Cleared ${payload.acked} messages from node buffer. Remaining: ${payload.remaining}.`);
      state.messages = [];
      state.uploaded = false;
      uploadBtn.disabled = true;
      ackBtn.disabled = true;
      await loadStats();
    });
    loadStats().catch((err) => setStatus('Failed to load node status: ' + err.message));
  </script>
</body>
</html>
)HTML";

static String effectiveSsid() {
#ifdef AP_SSID
    return String(AP_SSID);
#else
    return String("twwp-") + String(NODE_ID);
#endif
}

static String generateToken() {
    static const char alphabet[] = "0123456789abcdef";
    char out[33];
    for (size_t i = 0; i < sizeof(out) - 1; ++i) {
        out[i] = alphabet[esp_random() & 0x0F];
    }
    out[sizeof(out) - 1] = '\0';
    return String(out);
}

static bool loadUploadToken(bool rotate) {
    if (!rotate) {
        JsonDocument doc;
        if (storeSd_readJsonFile(SD_UPLOAD_TOKEN_PATH, doc)) {
            const char* token = doc["token"] | "";
            if (token[0] != '\0') {
                s_uploadToken = token;
                return true;
            }
        }
    }

    s_uploadToken = generateToken();
    JsonDocument doc;
    doc["token"] = s_uploadToken;
    doc["updated_at"] = millis() / 1000UL;
    return storeSd_writeJsonFile(SD_UPLOAD_TOKEN_PATH, doc);
}

static void loadConfig() {
    s_lastConfigLoadMs = millis();
    s_autoTriggerLossMs = AP_AUTO_TRIGGER_LOSS_MS;
    s_weakRssiThreshold = AP_AUTO_TRIGGER_RSSI_THRESHOLD;
    s_autoDurationS = AP_AUTO_DURATION_S;

    JsonDocument doc;
    if (!storeSd_readJsonFile(SD_CONFIG_PATH, doc)) {
        return;
    }

    int lossS = doc["ap"]["auto_trigger_loss_s"] | static_cast<int>(AP_AUTO_TRIGGER_LOSS_MS / 1000UL);
    int weak = doc["ap"]["weak_rssi_threshold"] | AP_AUTO_TRIGGER_RSSI_THRESHOLD;
    int autoDuration = doc["ap"]["auto_duration_s"] | static_cast<int>(AP_AUTO_DURATION_S);

    s_autoTriggerLossMs = static_cast<uint32_t>(max(5, lossS)) * 1000UL;
    s_weakRssiThreshold = constrain(weak, -95, -40);
    s_autoDurationS = static_cast<uint32_t>(constrain(autoDuration, 60, 3600));
}

static void handleRoot() {
    String html;
    if (!storeSd_readTextFile(SD_UPLOAD_HTML_PATH, html)) {
        html = FALLBACK_HTML;
    }
    s_server.send(200, "text/html; charset=utf-8", html);
}

static void handleBufferStats() {
    StoreSdBufferStats stats;
    storeSd_getBufferStats(stats);

    JsonDocument doc;
    doc["node_id"] = NODE_ID;
    doc["firmware"] = NODE_FIRMWARE_VERSION;
    doc["count"] = stats.count;
    if (stats.oldestTs == 0) doc["oldest_ts"] = nullptr;
    else doc["oldest_ts"] = stats.oldestTs;
    if (stats.newestTs == 0) doc["newest_ts"] = nullptr;
    else doc["newest_ts"] = stats.newestTs;
    doc["est_bytes"] = stats.estBytes;
    doc["upload_token"] = s_uploadToken;
    doc["upload_url"] = UPLOAD_RELAY_URL;
    doc["ap_ssid"] = s_apSsid;
    doc["ap_expires_s"] = netAp_getExpiresS();

    String payload;
    serializeJson(doc, payload);
    s_server.send(200, "application/json", payload);
}

static void handleBufferFetch() {
    int count = s_server.hasArg("count") ? s_server.arg("count").toInt() : 10;
    count = constrain(count, 1, static_cast<int>(SD_MAX_BUFFER_LINES));

    String payload;
    if (!storeSd_fetchOldestBufferJson(static_cast<uint16_t>(count), payload)) {
        s_server.send(500, "application/json", "{\"error\":\"buffer read failed\"}");
        return;
    }

    s_server.send(200, "application/json", payload);
}

static void handleBufferAck() {
    JsonDocument doc;
    if (deserializeJson(doc, s_server.arg("plain"))) {
        s_server.send(400, "application/json", "{\"error\":\"invalid json\"}");
        return;
    }

    uint16_t wanted = static_cast<uint16_t>(constrain(doc["count"] | 0, 0, static_cast<int>(SD_MAX_BUFFER_LINES)));
    uint16_t acked = storeSd_ackOldestBuffer(wanted);

    JsonDocument reply;
    reply["acked"] = acked;
    reply["remaining"] = storeSd_bufferCount();
    String payload;
    serializeJson(reply, payload);
    s_server.send(200, "application/json", payload);
}

static void handleNotFound() {
    s_server.send(404, "application/json", "{\"error\":\"not found\"}");
}

static void beginServer() {
    if (!s_routesReady) {
        s_server.on("/", HTTP_GET, handleRoot);
        s_server.on("/api/buffer/stats", HTTP_GET, handleBufferStats);
        s_server.on("/api/buffer/fetch", HTTP_GET, handleBufferFetch);
        s_server.on("/api/buffer/ack", HTTP_POST, handleBufferAck);
        s_server.onNotFound(handleNotFound);
        s_routesReady = true;
    }

    if (s_serverStarted) {
        return;
    }

    s_server.begin();
    s_serverStarted = true;
}

static void stopServer() {
    if (!s_serverStarted) {
        return;
    }
    s_server.stop();
    s_serverStarted = false;
}

static void logAutoTrigger(const char* reason) {
    char msg[80];
    snprintf(msg, sizeof(msg), "[AP] auto-triggered: %s", reason);
    Serial.println(msg);
    storeSd_logEvent(msg);
}

bool netAp_begin() {
    s_apSsid = effectiveSsid();
    loadConfig();
    return loadUploadToken(false);
}

bool netAp_start(uint32_t durationSeconds) {
    if (durationSeconds == 0) {
        durationSeconds = AP_DEFAULT_DURATION_S;
    }

    if (s_apActive) {
        s_apExpiresAtMs = millis() + durationSeconds * 1000UL;
        return true;
    }

    if (!loadUploadToken(false)) {
        Serial.println("[AP] upload token unavailable");
    }

    WiFi.mode(WIFI_AP_STA);
    if (!WiFi.softAP(s_apSsid.c_str(), AP_PASS)) {
        Serial.println("[AP] failed to start soft AP");
        return false;
    }

    s_apActive = true;
    s_apExpiresAtMs = millis() + durationSeconds * 1000UL;
    beginServer();

    char msg[128];
    snprintf(msg, sizeof(msg), "[AP] active ssid=%s expires_in=%lus", s_apSsid.c_str(),
             static_cast<unsigned long>(durationSeconds));
    Serial.println(msg);
    storeSd_logEvent(msg);
    return true;
}

void netAp_stop() {
    if (!s_apActive) {
        return;
    }

    stopServer();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    s_apActive = false;
    s_apExpiresAtMs = 0;
    Serial.println("[AP] stopped");
    storeSd_logEvent("[AP] stopped");
}

void netAp_loop() {
    unsigned long now = millis();
    if (now - s_lastConfigLoadMs > 30000UL) {
        loadConfig();
    }

    if (s_apActive) {
        s_server.handleClient();
        if (s_apExpiresAtMs != 0 && static_cast<long>(now - s_apExpiresAtMs) >= 0) {
            netAp_stop();
        }
    }

    if (netWifi_isConnected()) {
        s_disconnectSinceMs = 0;
        s_autoTriggeredWaitingRecovery = false;
        int rssi = WiFi.RSSI();
        if (rssi < s_weakRssiThreshold) {
            if (s_weakSinceMs == 0) {
                s_weakSinceMs = now;
            }
            if (!s_apActive && !s_autoTriggeredWaitingRecovery && now - s_weakSinceMs >= s_autoTriggerLossMs) {
                logAutoTrigger("weak signal");
                s_autoTriggeredWaitingRecovery = true;
                netAp_start(s_autoDurationS);
            }
        } else {
            s_weakSinceMs = 0;
        }
        return;
    }

    s_weakSinceMs = 0;
    if (s_disconnectSinceMs == 0) {
        s_disconnectSinceMs = now;
        return;
    }

    if (!s_apActive && !s_autoTriggeredWaitingRecovery && now - s_disconnectSinceMs >= s_autoTriggerLossMs) {
        logAutoTrigger("wifi loss");
        s_autoTriggeredWaitingRecovery = true;
        netAp_start(s_autoDurationS);
    }
}

bool netAp_isActive() {
    return s_apActive;
}

const char* netAp_getSsid() {
    return s_apSsid.c_str();
}

uint8_t netAp_getClientCount() {
    return static_cast<uint8_t>(WiFi.softAPgetStationNum());
}

uint32_t netAp_getExpiresS() {
    if (!s_apActive || s_apExpiresAtMs == 0) {
        return 0;
    }

    unsigned long now = millis();
    if (static_cast<long>(s_apExpiresAtMs - now) <= 0) {
        return 0;
    }
    return static_cast<uint32_t>((s_apExpiresAtMs - now) / 1000UL);
}

bool netAp_rotateUploadToken() {
    bool ok = loadUploadToken(true);
    if (ok) {
        Serial.println("[AP] upload token rotated");
        storeSd_logEvent("[AP] upload token rotated");
    }
    return ok;
}
