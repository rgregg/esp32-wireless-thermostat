#pragma once
#if defined(ARDUINO)
#include <Arduino.h>
#include <WebServer.h>
#include <mbedtls/sha256.h>
#include <esp_system.h>
#include <string.h>
#include <stdio.h>

/// Session-based web authentication for ESP32 HTTP server.
///
/// Usage:
///   On boot: web_auth::set_password_hash(nvs_hash_string);
///   On config update: web_auth::set_password(plaintext); persist get_password_hash()
///   At start of each handler: if (!web_auth::require_auth(server)) return;
///   Register GET /login, POST /login, POST /logout routes.
///
/// Auth is disabled entirely when no password has been configured (hash is empty).
namespace web_auth {

/// Session lifetime after creation: 24 hours.
static constexpr uint32_t kSessionExpiryMs = 86400000UL;

/// Module-level state — one instance per firmware binary.
/// Only a single active session is tracked. A new login replaces the old one.
static char s_pwd_hash[65] = {};       // SHA-256 hex (empty = auth disabled)
static char s_session_token[65] = {};  // current valid session hex (empty = none)
static uint32_t s_session_expiry_ms = 0;

/// Compute SHA-256 of a null-terminated string; write 64-char lowercase hex + NUL.
/// @param input   Null-terminated input string.
/// @param out     Output buffer, at least 65 bytes.
inline void sha256_hex(const char *input, char out[65]) {
  uint8_t hash[32];
  mbedtls_sha256(reinterpret_cast<const uint8_t *>(input), strlen(input), hash, 0);
  for (int i = 0; i < 32; ++i) {
    snprintf(out + i * 2, 3, "%02x", hash[i]);
  }
  out[64] = '\0';
}

/// Generate a random 256-bit session token and write as 64-char hex + NUL.
/// @param out  Output buffer, at least 65 bytes.
inline void generate_token(char out[65]) {
  uint8_t buf[32];
  esp_fill_random(buf, sizeof(buf));
  for (int i = 0; i < 32; ++i) {
    snprintf(out + i * 2, 3, "%02x", buf[i]);
  }
  out[64] = '\0';
}

/// Load a pre-hashed password from persistent storage.
/// Pass nullptr or an empty / non-64-char string to disable authentication.
/// Only accepts strings containing exactly 64 lowercase hex digits.
/// Invalidates any existing session.
inline void set_password_hash(const char *hash_hex) {
  s_session_token[0] = '\0';
  s_session_expiry_ms = 0;
  if (hash_hex == nullptr || strlen(hash_hex) != 64) {
    s_pwd_hash[0] = '\0';
    return;
  }
  // Validate that every character is a hex digit to reject corrupted NVS data.
  for (int i = 0; i < 64; ++i) {
    const char c = hash_hex[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
      s_pwd_hash[0] = '\0';
      return;
    }
  }
  memcpy(s_pwd_hash, hash_hex, 64);
  s_pwd_hash[64] = '\0';
}

/// Hash and store a plaintext password.
/// An empty string disables authentication.
/// Invalidates any existing session.
inline void set_password(const char *password) {
  s_session_token[0] = '\0';
  s_session_expiry_ms = 0;
  if (password == nullptr || password[0] == '\0') {
    s_pwd_hash[0] = '\0';
  } else {
    sha256_hex(password, s_pwd_hash);
  }
}

/// Return the current password hash (64 hex chars + NUL) for NVS persistence.
/// Returns a pointer to a 65-byte internal buffer; empty string if auth is disabled.
inline const char *get_password_hash() {
  return s_pwd_hash;
}

/// Returns true when authentication is required (a password has been configured).
inline bool is_enabled() {
  return s_pwd_hash[0] != '\0';
}

/// Extract the value of a named cookie from a Cookie: header string.
/// Returns true on success, writing at most outLen-1 chars to out.
inline bool extract_cookie(const char *header, const char *name,
                            char *out, size_t outLen) {
  if (!header || !name || !out || outLen == 0) return false;
  const size_t nlen = strlen(name);
  const char *p = header;
  while (*p) {
    while (*p == ' ' || *p == '\t') ++p;
    if (strncmp(p, name, nlen) == 0 && p[nlen] == '=') {
      const char *start = p + nlen + 1;
      const char *end = start;
      while (*end && *end != ';') ++end;
      size_t len = static_cast<size_t>(end - start);
      if (len >= outLen) len = outLen - 1;
      memcpy(out, start, len);
      out[len] = '\0';
      return true;
    }
    while (*p && *p != ';') ++p;
    if (*p == ';') ++p;
  }
  return false;
}

/// Returns true if the request carries a valid session cookie,
/// or if authentication is disabled.
inline bool is_authenticated(WebServer &server) {
  if (!is_enabled()) return true;
  if (s_session_token[0] == '\0') return false;
  const uint32_t now = millis();
  // Use signed subtraction to correctly handle millis() rollover every ~49 days.
  if (static_cast<int32_t>(now - s_session_expiry_ms) >= 0) {
    s_session_token[0] = '\0';
    return false;
  }
  const String cookie = server.header("Cookie");
  if (cookie.isEmpty()) return false;
  char tok[65] = {};
  if (!extract_cookie(cookie.c_str(), "session", tok, sizeof(tok))) return false;
  // Constant-time comparison to reduce timing side-channel risk.
  volatile uint8_t diff = 0;
  for (int i = 0; i < 64; ++i) {
    diff |= static_cast<uint8_t>(tok[i]) ^ static_cast<uint8_t>(s_session_token[i]);
  }
  return diff == 0;
}

/// Attempt to log in with a plaintext password.
/// On success: generates a new session token and returns true.
/// On failure: returns false without changing session state.
inline bool login(const char *password) {
  if (!is_enabled()) return true;
  if (!password || !password[0]) return false;
  char hash[65];
  sha256_hex(password, hash);
  volatile uint8_t diff = 0;
  for (int i = 0; i < 64; ++i) {
    diff |= static_cast<uint8_t>(hash[i]) ^ static_cast<uint8_t>(s_pwd_hash[i]);
  }
  if (diff != 0) return false;
  generate_token(s_session_token);
  s_session_expiry_ms = millis() + kSessionExpiryMs;
  return true;
}

/// Invalidate the current session.
inline void logout() {
  s_session_token[0] = '\0';
  s_session_expiry_ms = 0;
}

/// Write a Set-Cookie header to establish the session.
inline void set_session_cookie(WebServer &server) {
  String v = "session=";
  v += s_session_token;
  v += "; Path=/; HttpOnly; SameSite=Strict";
  server.sendHeader("Set-Cookie", v);
}

/// Write a Set-Cookie header that expires the session cookie in the browser.
inline void clear_session_cookie(WebServer &server) {
  server.sendHeader(
      "Set-Cookie",
      "session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
}

/// Check authentication for a handler.
/// If not authenticated, sends a 302 redirect to /login and returns false.
/// Call at the top of each protected handler:  if (!web_auth::require_auth(server)) return;
inline bool require_auth(WebServer &server) {
  if (is_authenticated(server)) return true;
  server.sendHeader("Location", "/login");
  server.send(302, "text/plain", "");
  return false;
}

/// Build and return the HTML login page.
/// @param hostname   Device hostname shown as subtitle (may be null).
/// @param error_msg  Optional error message to display (null or empty = none).
inline String login_page(const char *hostname, const char *error_msg = nullptr) {
  String html;
  html.reserve(1900);
  html += F("<!DOCTYPE html><html><head>"
            "<meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<title>Sign In</title><style>"
            ":root{--bg:#111827;--bg-c:#1F2937;--bg-i:#374151;--tx:#F9FAFB;"
            "--tx2:#9CA3AF;--ac:#4F46E5;--ach:#4338CA;--bd:#374151;--er:#EF4444}"
            "*{box-sizing:border-box;margin:0;padding:0}"
            "body{font-family:-apple-system,system-ui,sans-serif;background:var(--bg);"
            "color:var(--tx);display:flex;flex-direction:column;"
            "align-items:center;justify-content:center;min-height:100vh}"
            ".card{background:var(--bg-c);border-radius:0.5rem;padding:2rem;"
            "width:100%;max-width:340px;margin:1rem}"
            "h1{font-size:1.1rem;font-weight:600;margin-bottom:0.25rem;text-align:center}"
            ".sub{font-size:0.75rem;color:var(--tx2);text-align:center;margin-bottom:1.5rem}"
            ".fg{margin-bottom:1rem}"
            ".fg label{display:block;font-size:0.75rem;color:var(--tx2);margin-bottom:0.2rem}"
            ".fg input{width:100%;padding:0.5rem;border:1px solid var(--bd);"
            "border-radius:0.375rem;background:var(--bg-i);color:var(--tx);font-size:0.9rem}"
            ".fg input:focus{outline:none;border-color:var(--ac)}"
            ".btn{width:100%;padding:0.6rem;border:none;border-radius:0.375rem;"
            "background:var(--ac);color:#fff;font-size:0.9rem;font-weight:500;cursor:pointer}"
            ".btn:hover{background:var(--ach)}"
            ".err{background:rgba(239,68,68,0.15);border:1px solid var(--er);"
            "border-radius:0.375rem;padding:0.5rem 0.75rem;margin-bottom:1rem;"
            "font-size:0.85rem;color:var(--er)}"
            "</style></head><body><div class=\"card\">"
            "<h1>&#128274; Sign In</h1>");
  if (hostname && hostname[0]) {
    html += F("<p class=\"sub\">");
    html += hostname;
    html += F("</p>");
  }
  if (error_msg && error_msg[0]) {
    html += F("<div class=\"err\">");
    html += error_msg;
    html += F("</div>");
  }
  html += F("<form method=\"post\" action=\"/login\">"
            "<div class=\"fg\"><label>Password</label>"
            "<input type=\"password\" name=\"password\" autofocus "
            "autocomplete=\"current-password\"></div>"
            "<button type=\"submit\" class=\"btn\">Sign In</button>"
            "</form></div></body></html>");
  return html;
}

}  // namespace web_auth
#endif  // ARDUINO
