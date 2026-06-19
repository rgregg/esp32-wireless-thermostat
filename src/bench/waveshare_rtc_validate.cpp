// On-board PCF85063 RTC validation: read current time, set a known time, then read
// back every 2s to confirm the clock is running (seconds advance) and osc_ok is true.
#if defined(WAVESHARE_RTC_VALIDATE)
#include <Arduino.h>
#include "controller/pcf85063_rtc.h"
using thermostat::Pcf85063Rtc;
using thermostat::RtcTime;
namespace { Pcf85063Rtc g_rtc; }  // 0x51, SDA42/SCL41
static void show(const char* tag) {
  RtcTime t; bool osc = false;
  if (g_rtc.read(t, osc))
    Serial.printf("%s %04u-%02u-%02u %02u:%02u:%02u wd%u osc_ok=%d\n",
                  tag, t.year, t.month, t.day, t.hour, t.minute, t.second, t.weekday, osc);
  else Serial.printf("%s I2C read error\n", tag);
}
void setup() {
  pinMode(46, OUTPUT); digitalWrite(46, LOW);  // silence buzzer
  Serial.begin(115200); delay(2500);
  Serial.println("\n==== PCF85063 RTC validation ====");
  g_rtc.begin();
  show("[read-before]");
  RtcTime set; set.year=2026; set.month=6; set.day=16; set.hour=12; set.minute=0; set.second=0; set.weekday=2;
  Serial.printf("[set] 2026-06-16 12:00:00 -> %s\n", g_rtc.set(set) ? "ok" : "FAIL");
  show("[read-after ]");
  Serial.println("[tick] reading every 2s — seconds should advance, proving the oscillator runs:");
}
void loop() {
  static uint32_t last = 0;
  if (millis() - last >= 2000) { last = millis(); show("[tick]"); }
  delay(50);
}
#endif
