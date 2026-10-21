#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Parsing and validation for the ESP-NOW peer MAC and LMK config values. Header-only
// and platform-agnostic so it can be unit-tested. Security-relevant: a value that fails
// to parse used to leave the peer at broadcast, which silently turned encryption off
// (an encrypted sender's frames are then dropped by the receiver with no error), so
// both firmwares parse through here and reject bad values at config time.
namespace thermostat {
namespace espnow_config {

inline int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Accepts "AA:BB:CC:DD:EE:FF", "AA-BB-CC-DD-EE-FF" (one separator style throughout) or
// the compact "AABBCCDDEEFF" the config pages display, in any case. Anything else,
// including trailing characters, returns false and leaves out untouched.
inline bool parse_mac(const char *text, uint8_t out[6]) {
  if (text == nullptr) return false;
  const size_t len = strlen(text);
  char sep = 0;
  if (len == 17) {
    sep = text[2];
    if (sep != ':' && sep != '-') return false;
  } else if (len != 12) {
    return false;
  }
  const size_t stride = sep != 0 ? 3 : 2;
  uint8_t mac[6];
  for (int i = 0; i < 6; ++i) {
    const char *p = text + i * stride;
    const int hi = hex_nibble(p[0]);
    const int lo = hex_nibble(p[1]);
    if (hi < 0 || lo < 0) return false;
    if (sep != 0 && i < 5 && p[2] != sep) return false;
    mac[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  memcpy(out, mac, sizeof(mac));
  return true;
}

// Canonical stored form: uppercase, colon-separated. out must hold 18 bytes.
inline void format_mac(const uint8_t mac[6], char out[18]) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4],
           mac[5]);
}

// Normalizes any form parse_mac() accepts to the canonical form.
inline bool normalize_mac(const char *text, char out[18]) {
  uint8_t mac[6];
  if (!parse_mac(text, mac)) return false;
  format_mac(mac, out);
  return true;
}

// Exactly 32 hex characters (16 bytes). Anything else returns false, out untouched.
inline bool parse_lmk(const char *text, uint8_t out[16]) {
  if (text == nullptr || strlen(text) != 32) return false;
  uint8_t lmk[16];
  for (int i = 0; i < 16; ++i) {
    const int hi = hex_nibble(text[i * 2]);
    const int lo = hex_nibble(text[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    lmk[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  memcpy(out, lmk, sizeof(lmk));
  return true;
}

// Config-time check for an LMK value: empty (no encryption) or a valid 32-hex key.
inline bool lmk_value_valid(const char *text) {
  if (text == nullptr) return false;
  if (text[0] == '\0') return true;
  uint8_t unused[16];
  return parse_lmk(text, unused);
}

}  // namespace espnow_config
}  // namespace thermostat
