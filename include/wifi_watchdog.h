#pragma once

// WiFi connectivity watchdog: detects "zombie WiFi" state where WiFi.isConnected()
// returns true but the device is actually unreachable on the network.
// Uses ESP-IDF ping API to periodically ping the gateway. If the gateway is
// unreachable for an extended period, forces WiFi reconnect or full reboot.

#include <WiFi.h>
#include <atomic>
#include <esp_system.h>
#include "ping/ping_sock.h"
#include "lwip/inet.h"

namespace wifi_watchdog {

static constexpr uint32_t PING_INTERVAL_MS        = 30000;  // Ping every 30 seconds
static constexpr uint32_t RECONNECT_THRESHOLD_MS   = 120000; // Reconnect after 2 min of failures
static constexpr uint32_t REBOOT_THRESHOLD_MS      = 300000; // Reboot after 5 min of failures
static constexpr uint32_t PING_TIMEOUT_MS          = 5000;   // Single ping timeout
static constexpr uint32_t PING_STALE_MS            = 15000;  // Force-cleanup if ping hangs
static constexpr uint32_t MAX_RECONNECTS           = 3;      // Reboot after this many failed reconnects

struct State {
    uint32_t last_ping_ms          = 0;
    uint32_t ping_start_ms         = 0;     // When current ping was launched
    uint32_t first_failure_ms      = 0;
    uint32_t last_success_ms       = 0;     // Last time gateway responded
    uint32_t reconnect_count       = 0;     // Reconnects since last successful ping
    bool     failing               = false;
    bool     reconnect_attempted   = false;
    bool     watchdog_disconnected = false;  // True when WE initiated disconnect
    esp_ping_handle_t ping_handle  = nullptr;
    std::atomic<bool> ping_in_flight{false};
    std::atomic<bool> ping_got_reply{false};
};

static State g_state;

static void on_ping_success(esp_ping_handle_t hdl, void *args) {
    static_cast<State *>(args)->ping_got_reply = true;
}

static void on_ping_timeout(esp_ping_handle_t, void *) {
    // ping_got_reply stays false
}

static void on_ping_end(esp_ping_handle_t, void *args) {
    static_cast<State *>(args)->ping_in_flight = false;
}

static void cleanup_session() {
    if (g_state.ping_handle) {
        esp_ping_stop(g_state.ping_handle);
        esp_ping_delete_session(g_state.ping_handle);
        g_state.ping_handle = nullptr;
    }
    g_state.ping_in_flight = false;
}

static void log_state(const char *event, uint32_t now_ms) {
    uint32_t uptime_s = now_ms / 1000;
    uint32_t since_success = g_state.last_success_ms
        ? (now_ms - g_state.last_success_ms) / 1000 : 0;
    Serial.printf("[watchdog] %s | uptime=%lus wifi=%s rssi=%d ip=%s gw=%s "
                  "failing=%d reconnects=%lu last_ok=%lus_ago\n",
                  event,
                  (unsigned long)uptime_s,
                  WiFi.isConnected() ? "yes" : "no",
                  WiFi.isConnected() ? WiFi.RSSI() : 0,
                  WiFi.localIP().toString().c_str(),
                  WiFi.gatewayIP().toString().c_str(),
                  g_state.failing ? 1 : 0,
                  (unsigned long)g_state.reconnect_count,
                  (unsigned long)since_success);
}

static bool start_ping(IPAddress gateway, uint32_t now_ms) {
    cleanup_session();

    ip_addr_t target_addr = {};
    target_addr.type = IPADDR_TYPE_V4;
    target_addr.u_addr.ip4.addr = static_cast<uint32_t>(gateway);

    esp_ping_config_t ping_cfg = ESP_PING_DEFAULT_CONFIG();
    ping_cfg.target_addr = target_addr;
    ping_cfg.count = 1;
    ping_cfg.interval_ms = 1000;
    ping_cfg.timeout_ms = PING_TIMEOUT_MS;
    ping_cfg.data_size = 32;
    ping_cfg.task_stack_size = 4096;
    ping_cfg.task_prio = 1;

    esp_ping_callbacks_t cbs = {};
    cbs.cb_args = &g_state;
    cbs.on_ping_success = on_ping_success;
    cbs.on_ping_timeout = on_ping_timeout;
    cbs.on_ping_end = on_ping_end;

    g_state.ping_got_reply = false;
    g_state.ping_in_flight = true;
    g_state.ping_start_ms = now_ms;

    esp_err_t err = esp_ping_new_session(&ping_cfg, &cbs, &g_state.ping_handle);
    if (err != ESP_OK) {
        log_state("ping_session_create_failed", now_ms);
        Serial.printf("[watchdog]   esp_ping_new_session err=0x%x\n", err);
        g_state.ping_handle = nullptr;
        g_state.ping_in_flight = false;
        return false;
    }

    err = esp_ping_start(g_state.ping_handle);
    if (err != ESP_OK) {
        log_state("ping_start_failed", now_ms);
        Serial.printf("[watchdog]   esp_ping_start err=0x%x\n", err);
        cleanup_session();
        return false;
    }
    return true;
}

static void handle_ping_result(uint32_t now_ms) {
    bool success = g_state.ping_got_reply;
    cleanup_session();

    if (success) {
        if (g_state.failing) {
            log_state("gateway_recovered", now_ms);
        }
        g_state.failing = false;
        g_state.reconnect_attempted = false;
        g_state.watchdog_disconnected = false;
        g_state.reconnect_count = 0;
        g_state.last_success_ms = now_ms;
        return;
    }

    // Gateway unreachable
    if (!g_state.failing) {
        g_state.failing = true;
        g_state.first_failure_ms = now_ms;
        log_state("gateway_unreachable", now_ms);
        return;
    }

    uint32_t failure_duration = now_ms - g_state.first_failure_ms;

    // Reboot if we've exceeded the time threshold OR exhausted reconnect attempts
    if (failure_duration >= REBOOT_THRESHOLD_MS ||
        g_state.reconnect_count >= MAX_RECONNECTS) {
        log_state("REBOOTING", now_ms);
        Serial.printf("[watchdog]   reason: failure_duration=%lus reconnects=%lu\n",
                      (unsigned long)(failure_duration / 1000),
                      (unsigned long)g_state.reconnect_count);
        Serial.flush();
        delay(100);
        ESP.restart();
    }

    if (failure_duration >= RECONNECT_THRESHOLD_MS && !g_state.reconnect_attempted) {
        g_state.reconnect_attempted = true;
        g_state.watchdog_disconnected = true;
        g_state.reconnect_count++;
        log_state("forcing_wifi_reconnect", now_ms);
        WiFi.disconnect();
        WiFi.reconnect();
    }
}

} // namespace wifi_watchdog

// connectivity_proven: pass true when the application layer (e.g. MQTT) is
// confirmed working — this proves WiFi is healthy, so skip the gateway ping
// and reset any failure state.  Only resort to ICMP when the app layer is down.
inline void wifi_watchdog_tick(uint32_t now_ms, bool connectivity_proven = false) {
    using namespace wifi_watchdog;

    if (WiFi.status() != WL_CONNECTED) {
        // If a ping session is stuck, clean it up
        cleanup_session();
        if (!g_state.watchdog_disconnected) {
            // External disconnect (not us) — reset failure tracking
            g_state.failing = false;
            g_state.reconnect_attempted = false;
            g_state.reconnect_count = 0;
        }
        // Keep first_failure_ms intact when we initiated the disconnect,
        // so the reboot threshold can still be reached.
        g_state.last_ping_ms = now_ms;
        return;
    }

    // If the application layer confirms connectivity (e.g. MQTT is connected),
    // WiFi is provably healthy — no need to ping the gateway.
    // Clear any accumulated failure state so a brief MQTT drop doesn't
    // immediately count against the reboot threshold when it reconnects.
    if (connectivity_proven) {
        if (g_state.ping_in_flight || g_state.ping_handle) {
            cleanup_session();
        }
        g_state.failing = false;
        g_state.reconnect_attempted = false;
        g_state.reconnect_count = 0;
        g_state.watchdog_disconnected = false;
        g_state.last_success_ms = now_ms;
        g_state.last_ping_ms = now_ms;  // Don't ping immediately after MQTT drops
        return;
    }

    // WiFi reconnected after our forced disconnect — resume pinging
    // but keep failure state so reboot can escalate if needed.
    if (g_state.watchdog_disconnected) {
        g_state.watchdog_disconnected = false;
        g_state.reconnect_attempted = false; // Allow another reconnect attempt
        log_state("wifi_reconnected_after_watchdog", now_ms);
    }

    // Stale ping detection: if ping_in_flight for too long, the ping task hung
    if (g_state.ping_in_flight) {
        if (now_ms - g_state.ping_start_ms >= PING_STALE_MS) {
            log_state("ping_stale_timeout", now_ms);
            cleanup_session();
            handle_ping_result(now_ms);  // treat stale ping as failure
            return;
        }
        return; // Still waiting for callback
    }

    // Process completed ping result (callback cleared ping_in_flight, handle still set)
    if (g_state.ping_handle) {
        handle_ping_result(now_ms);
        return;
    }

    // Time to send a new ping?
    if (now_ms - g_state.last_ping_ms >= PING_INTERVAL_MS) {
        g_state.last_ping_ms = now_ms;
        IPAddress gw = WiFi.gatewayIP();
        if (gw == IPAddress(0, 0, 0, 0)) {
            return;
        }
        start_ping(gw, now_ms);
    }
}
