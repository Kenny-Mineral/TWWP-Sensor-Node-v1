#pragma once
#include <map>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Shared NVS state — accessible from test files that need to seed or inspect it
static std::map<std::string, std::string> g_nvsStore;
static std::string g_nvsNamespace;

class Preferences {
public:
    void begin(const char* ns, bool) { g_nvsNamespace = ns; }
    void end() {}

    bool isKey(const char* k) {
        return g_nvsStore.count(std::string(g_nvsNamespace) + "/" + k) > 0;
    }

    void putString(const char* k, const char* v) {
        g_nvsStore[std::string(g_nvsNamespace) + "/" + k] = v ? v : "";
    }
    // Returns std::string (used by actuator_valve via .c_str())
    std::string getString(const char* k, const char* def) {
        auto it = g_nvsStore.find(std::string(g_nvsNamespace) + "/" + k);
        return it != g_nvsStore.end() ? it->second : std::string(def ? def : "");
    }
    // Fills char buffer (used by sensor_tds_meter)
    size_t getString(const char* k, char* buf, size_t bufLen) {
        auto it = g_nvsStore.find(std::string(g_nvsNamespace) + "/" + k);
        if (it == g_nvsStore.end() || !buf || bufLen == 0) return 0;
        strncpy(buf, it->second.c_str(), bufLen - 1);
        buf[bufLen - 1] = '\0';
        return strlen(buf);
    }

    void putUInt(const char* k, uint32_t v) {
        g_nvsStore[std::string(g_nvsNamespace) + "/" + k] = std::to_string(v);
    }
    uint32_t getUInt(const char* k, uint32_t def) {
        auto it = g_nvsStore.find(std::string(g_nvsNamespace) + "/" + k);
        if (it == g_nvsStore.end()) return def;
        return (uint32_t)std::stoul(it->second);
    }

    void putBool(const char* k, bool v) {
        g_nvsStore[std::string(g_nvsNamespace) + "/" + k] = v ? "1" : "0";
    }
    bool getBool(const char* k, bool def) {
        auto it = g_nvsStore.find(std::string(g_nvsNamespace) + "/" + k);
        if (it == g_nvsStore.end()) return def;
        return it->second == "1";
    }

    void putFloat(const char* k, float v) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%f", (double)v);
        g_nvsStore[std::string(g_nvsNamespace) + "/" + k] = buf;
    }
    float getFloat(const char* k, float def) {
        auto it = g_nvsStore.find(std::string(g_nvsNamespace) + "/" + k);
        if (it == g_nvsStore.end()) return def;
        return (float)std::stof(it->second);
    }
};
