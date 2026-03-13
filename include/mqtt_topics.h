#pragma once

#include <cstdio>
#include <cstring>

namespace mqtt_topics {

/// Build "{base}/devices/{mac}/{suffix}" into buf. Returns snprintf result.
inline int device_topic(char *buf, size_t buf_len,
                        const char *base, const char *mac,
                        const char *suffix) {
  return snprintf(buf, buf_len, "%s/devices/%s/%s", base, mac, suffix);
}

/// Build "{base}-{mac}" client ID into buf. Returns snprintf result.
inline int client_id(char *buf, size_t buf_len,
                     const char *base, const char *mac) {
  return snprintf(buf, buf_len, "%s-%s", base, mac);
}

}  // namespace mqtt_topics
