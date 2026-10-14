// Bench spike for the ESP-NOW fallback design (2026-10-13 spec, section 1).
// Question: does a unicast esp_now_send() report SUCCESS only on the channel the peer
// is listening on, and how long does the send callback take?
//
// The prober never joins WiFi. It cycles channels 1, 6, 11, sends one unicast heartbeat
// to SPIKE_PEER_MAC on each, and prints the send-callback status and latency.
// Build with the espnow-probe-spike env:
//   SPIKE_PEER_MAC=AA:BB:CC:DD:EE:FF pio run -e espnow-probe-spike
// Optional encryption: also set SPIKE_LMK to 32 hex chars.
#if defined(ESPNOW_PROBE_SPIKE)

#include <Arduino.h>
#include <WiFi.h>
#include <atomic>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

#include "transport/espnow_packets.h"

#ifndef SPIKE_PEER_MAC
#error "Set SPIKE_PEER_MAC, e.g. SPIKE_PEER_MAC=AA:BB:CC:DD:EE:FF"
#endif
#ifndef SPIKE_LMK
#define SPIKE_LMK ""
#endif

namespace {
std::atomic<int> g_status{-1};        // -1 pending, 0 fail, 1 success
std::atomic<uint32_t> g_cb_us{0};
uint8_t g_peer[6] = {0};
bool g_ready = false;  // setup() completed; loop() does nothing otherwise

bool parse_mac(const char *s, uint8_t out[6]) {
  unsigned v[6];
  if (sscanf(s, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) {
    return false;
  }
  for (int i = 0; i < 6; ++i) out[i] = static_cast<uint8_t>(v[i]);
  return true;
}

bool parse_lmk(const char *s, uint8_t out[16]) {
  if (strlen(s) != 32) return false;
  for (int i = 0; i < 16; ++i) {
    unsigned v;
    if (sscanf(s + i * 2, "%2x", &v) != 1) return false;
    out[i] = static_cast<uint8_t>(v);
  }
  return true;
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(2000);
  if (!parse_mac(SPIKE_PEER_MAC, g_peer)) {
    Serial.println("[spike] bad SPIKE_PEER_MAC");
    return;
  }
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("[spike] esp_now_init failed");
    return;
  }
  esp_now_register_send_cb([](const esp_now_send_info_t *, esp_now_send_status_t status) {
    g_cb_us.store(micros());
    g_status.store(status == ESP_NOW_SEND_SUCCESS ? 1 : 0);
  });
  esp_now_peer_info_t peer;
  memset(&peer, 0, sizeof(peer));
  memcpy(peer.peer_addr, g_peer, 6);
  peer.channel = 0;
  uint8_t lmk[16];
  peer.encrypt = parse_lmk(SPIKE_LMK, lmk);
  if (peer.encrypt) memcpy(peer.lmk, lmk, 16);
  const esp_err_t add_err = esp_now_add_peer(&peer);
  Serial.printf("[spike] my_mac=%s peer=%s encrypt=%d add_peer=%d\n",
                WiFi.macAddress().c_str(), SPIKE_PEER_MAC, peer.encrypt ? 1 : 0,
                static_cast<int>(add_err));
  if (add_err != ESP_OK) {
    Serial.println("[spike] esp_now_add_peer failed");
    return;
  }
  g_ready = true;
}

void loop() {
  if (!g_ready) {
    delay(1000);  // setup() failed (see its log); never send
    return;
  }
  static const uint8_t kChannels[] = {1, 6, 11};
  for (uint8_t ch : kChannels) {
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    thermostat::HeartbeatPacket pkt;
    pkt.header.type = static_cast<uint8_t>(thermostat::PacketType::Heartbeat);
    pkt.header.version = thermostat::kEspNowProtocolVersion;
    pkt.toggle = 0;
    g_status.store(-1);
    const uint32_t t0 = micros();
    const esp_err_t err =
        esp_now_send(g_peer, reinterpret_cast<const uint8_t *>(&pkt), sizeof(pkt));
    while (g_status.load() == -1 && (micros() - t0) < 500000UL) {
      delay(1);
    }
    const int status = g_status.load();
    Serial.printf("[spike] ch=%u send_err=%d status=%d latency_us=%lu\n",
                  static_cast<unsigned>(ch), static_cast<int>(err), status,
                  status == -1 ? 0UL : static_cast<unsigned long>(g_cb_us.load() - t0));
    delay(100);
  }
  Serial.println("[spike] ---");
  delay(3000);
}

#endif  // ESPNOW_PROBE_SPIKE
