#pragma once

#include <stdint.h>

namespace thermostat {

struct RtcTime {
  uint16_t year = 2000;   // full year, 2000-2099
  uint8_t month = 1;      // 1-12
  uint8_t day = 1;        // 1-31
  uint8_t hour = 0;       // 0-23
  uint8_t minute = 0;     // 0-59
  uint8_t second = 0;     // 0-59
  uint8_t weekday = 0;    // 0-6
};

inline uint8_t rtc_bcd_to_dec(uint8_t b) { return static_cast<uint8_t>((b >> 4) * 10 + (b & 0x0F)); }
inline uint8_t rtc_dec_to_bcd(uint8_t d) { return static_cast<uint8_t>(((d / 10) << 4) | (d % 10)); }

// Decode the 7-byte PCF85063 time register block (regs 0x04..0x0A) into `out`.
// Returns true if the oscillator-stop (OS) flag is CLEAR (time trustworthy).
// Masks each field per the datasheet. Year maps 00-99 -> 2000-2099.
bool pcf85063_decode(const uint8_t regs[7], RtcTime &out);

// Encode `t` into the 7-byte block (OS flag cleared). Inverse of pcf85063_decode
// for valid in-range times.
void pcf85063_encode(const RtcTime &t, uint8_t regs[7]);

// ---- RtcTime <-> Unix epoch (UTC) -------------------------------------------
// The RTC stores UTC. These are pure, platform-agnostic conversions (no libc time
// zone state) using Howard Hinnant's proleptic-Gregorian day algorithms, so they are
// deterministic and native-testable. Valid for the RtcTime range (years 2000-2099).

// Days since 1970-01-01 for a proleptic-Gregorian y/m/d (m: 1-12, d: 1-31).
inline long rtc_days_from_civil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<long>(era) * 146097L + static_cast<long>(doe) - 719468L;
}

// Inverse of rtc_days_from_civil: days-since-epoch -> y/m/d.
inline void rtc_civil_from_days(long z, int &y, unsigned &m, unsigned &d) {
  z += 719468L;
  const long era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  y = static_cast<int>(yoe) + static_cast<int>(era) * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y += (m <= 2);
}

// RtcTime (UTC) -> Unix epoch seconds.
inline long long rtc_time_to_epoch(const RtcTime &t) {
  const long days = rtc_days_from_civil(t.year, t.month, t.day);
  return static_cast<long long>(days) * 86400LL +
         t.hour * 3600 + t.minute * 60 + t.second;
}

// Unix epoch seconds -> RtcTime (UTC), including weekday (0=Sun..6=Sat).
inline RtcTime rtc_time_from_epoch(long long epoch) {
  long days = static_cast<long>(epoch / 86400);
  long secs = static_cast<long>(epoch % 86400);
  if (secs < 0) { secs += 86400; days -= 1; }
  int y; unsigned m, d;
  rtc_civil_from_days(days, y, m, d);
  RtcTime t;
  t.year = static_cast<uint16_t>(y);
  t.month = static_cast<uint8_t>(m);
  t.day = static_cast<uint8_t>(d);
  t.hour = static_cast<uint8_t>(secs / 3600);
  t.minute = static_cast<uint8_t>((secs % 3600) / 60);
  t.second = static_cast<uint8_t>(secs % 60);
  long wd = (days % 7 + 4) % 7;  // 1970-01-01 was a Thursday (4)
  if (wd < 0) wd += 7;
  t.weekday = static_cast<uint8_t>(wd);
  return t;
}

class Pcf85063Rtc {
 public:
  explicit Pcf85063Rtc(uint8_t i2c_addr = 0x51, int sda_pin = 42, int scl_pin = 41)
      : addr_(i2c_addr), sda_(sda_pin), scl_(scl_pin) {}
  void begin();
  // Reads the time; sets osc_ok=false if the OS flag indicates lost time. Returns
  // false on I2C error.
  bool read(RtcTime &out, bool &osc_ok);
  bool set(const RtcTime &t);
 private:
  uint8_t addr_;
  int sda_;
  int scl_;
};

}  // namespace thermostat
