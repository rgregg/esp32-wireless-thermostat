#include "controller/pcf85063_rtc.h"

namespace thermostat {

bool pcf85063_decode(const uint8_t regs[7], RtcTime &out) {
  const bool osc_ok = (regs[0] & 0x80) == 0;
  out.second  = rtc_bcd_to_dec(regs[0] & 0x7F);
  out.minute  = rtc_bcd_to_dec(regs[1] & 0x7F);
  out.hour    = rtc_bcd_to_dec(regs[2] & 0x3F);
  out.day     = rtc_bcd_to_dec(regs[3] & 0x3F);
  out.weekday = static_cast<uint8_t>(regs[4] & 0x07);
  out.month   = rtc_bcd_to_dec(regs[5] & 0x1F);
  out.year    = static_cast<uint16_t>(2000 + rtc_bcd_to_dec(regs[6]));
  return osc_ok;
}

void pcf85063_encode(const RtcTime &t, uint8_t regs[7]) {
  regs[0] = rtc_dec_to_bcd(t.second) & 0x7F;  // OS flag cleared
  regs[1] = rtc_dec_to_bcd(t.minute) & 0x7F;
  regs[2] = rtc_dec_to_bcd(t.hour) & 0x3F;
  regs[3] = rtc_dec_to_bcd(t.day) & 0x3F;
  regs[4] = static_cast<uint8_t>(t.weekday & 0x07);
  regs[5] = rtc_dec_to_bcd(t.month) & 0x1F;
  regs[6] = rtc_dec_to_bcd(static_cast<uint8_t>(t.year % 100));
}

}  // namespace thermostat

#if defined(ARDUINO)
#include <Arduino.h>
#include <Wire.h>
namespace thermostat {
void Pcf85063Rtc::begin() { Wire.begin(sda_, scl_); Wire.setClock(100000); }

bool Pcf85063Rtc::read(RtcTime &out, bool &osc_ok) {
  Wire.beginTransmission(addr_);
  Wire.write(0x04);  // seconds register (start of time block)
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(static_cast<int>(addr_), 7) != 7) return false;
  uint8_t regs[7];
  for (int i = 0; i < 7; ++i) regs[i] = Wire.available() ? Wire.read() : 0;
  osc_ok = pcf85063_decode(regs, out);
  return true;
}

bool Pcf85063Rtc::set(const RtcTime &t) {
  uint8_t regs[7];
  pcf85063_encode(t, regs);
  Wire.beginTransmission(addr_);
  Wire.write(0x04);
  for (int i = 0; i < 7; ++i) Wire.write(regs[i]);
  return Wire.endTransmission() == 0;
}
}  // namespace thermostat
#else
namespace thermostat {
void Pcf85063Rtc::begin() {}
bool Pcf85063Rtc::read(RtcTime &, bool &osc_ok) { osc_ok = false; return false; }
bool Pcf85063Rtc::set(const RtcTime &) { return false; }
}  // namespace thermostat
#endif
