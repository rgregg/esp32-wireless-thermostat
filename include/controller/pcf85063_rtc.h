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
