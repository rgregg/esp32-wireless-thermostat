#pragma once

#include <string.h>
#include <stddef.h>
#include <stdio.h>

// Simple device registry populated from MQTT device announcements.
// Platform-agnostic: uses only C-compatible types (no Arduino String, no std::string).

struct DeviceRegistryEntry {
  char mac[18];   // "AA:BB:CC:DD:EE:FF\0"
  char name[48];
  char type[16];  // "controller", "thermostat", etc.
  char ip[16];    // "xxx.xxx.xxx.xxx\0"
  bool occupied;
};

static constexpr size_t kMaxRegistryEntries = 10;

struct DeviceRegistry {
  DeviceRegistryEntry entries[kMaxRegistryEntries] = {};

  // Add or update an entry by MAC address.  Returns true if stored.
  bool upsert(const char *mac, const char *name, const char *type, const char *ip) {
    if (mac == nullptr || mac[0] == '\0') return false;

    // Look for existing entry with this MAC
    for (size_t i = 0; i < kMaxRegistryEntries; ++i) {
      if (entries[i].occupied && strcmp(entries[i].mac, mac) == 0) {
        copy_field(entries[i].name, name, sizeof(entries[i].name));
        copy_field(entries[i].type, type, sizeof(entries[i].type));
        copy_field(entries[i].ip, ip, sizeof(entries[i].ip));
        return true;
      }
    }
    // Find a free slot
    for (size_t i = 0; i < kMaxRegistryEntries; ++i) {
      if (!entries[i].occupied) {
        copy_field(entries[i].mac, mac, sizeof(entries[i].mac));
        copy_field(entries[i].name, name, sizeof(entries[i].name));
        copy_field(entries[i].type, type, sizeof(entries[i].type));
        copy_field(entries[i].ip, ip, sizeof(entries[i].ip));
        entries[i].occupied = true;
        return true;
      }
    }
    return false;  // Registry full
  }

  size_t count() const {
    size_t n = 0;
    for (size_t i = 0; i < kMaxRegistryEntries; ++i) {
      if (entries[i].occupied) ++n;
    }
    return n;
  }

private:
  static void copy_field(char *dst, const char *src, size_t dst_size) {
    if (src == nullptr) {
      dst[0] = '\0';
      return;
    }
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
  }
};

// Format a compact MAC (e.g. "AABBCCDDEEFF") into colon-separated form
// ("AA:BB:CC:DD:EE:FF").  If the input already contains colons or is not
// exactly 12 hex chars, it is copied as-is.  Returns true if formatted.
inline bool format_mac_colons(const char *compact, char *out, size_t out_size) {
  if (compact == nullptr || out == nullptr || out_size < 18) {
    if (out && out_size > 0) out[0] = '\0';
    return false;
  }
  size_t len = strlen(compact);
  // Already colon-separated or not a compact MAC — copy as-is
  if (len != 12 || strchr(compact, ':') != nullptr) {
    strncpy(out, compact, out_size - 1);
    out[out_size - 1] = '\0';
    return false;
  }
  snprintf(out, out_size, "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
           compact[0], compact[1], compact[2], compact[3],
           compact[4], compact[5], compact[6], compact[7],
           compact[8], compact[9], compact[10], compact[11]);
  return true;
}

// Simple JSON value extractor using strstr.
// Extracts the string value for a given key from a flat JSON object.
// Writes the value into `out` (max `out_size` bytes including NUL).
// Returns true if the key was found and value extracted.
inline bool json_extract_string(const char *json, const char *key,
                                 char *out, size_t out_size) {
  if (json == nullptr || key == nullptr || out == nullptr || out_size == 0) return false;

  // Ensure out is always initialized to an empty string on valid input.
  out[0] = '\0';

  // Build search pattern: "key":"
  char pattern[64];
  size_t key_len = strlen(key);
  if (key_len + 4 > sizeof(pattern)) return false;
  pattern[0] = '"';
  memcpy(pattern + 1, key, key_len);
  pattern[key_len + 1] = '"';
  pattern[key_len + 2] = ':';
  pattern[key_len + 3] = '\0';

  // Search for the pattern, ensuring it is a complete key (not a suffix of
  // a longer key like "source_ip" matching a search for "ip").  A valid match
  // must be preceded by '{', ',' or whitespace — i.e. a JSON field delimiter.
  const char *found = json;
  while ((found = strstr(found, pattern)) != nullptr) {
    if (found == json || found[-1] == '{' || found[-1] == ',' ||
        found[-1] == ' ' || found[-1] == '\t' || found[-1] == '\n') {
      break;  // valid match — key is not a suffix of a longer key
    }
    found += 1;  // skip this false match and keep searching
  }
  if (found == nullptr) return false;

  // Advance past the pattern
  found += key_len + 3;

  // Skip whitespace
  while (*found == ' ' || *found == '\t') ++found;

  if (*found != '"') return false;
  ++found;  // skip opening quote

  size_t i = 0;
  while (*found != '\0' && *found != '"' && i < out_size - 1) {
    out[i++] = *found++;
  }
  out[i] = '\0';
  return true;
}
