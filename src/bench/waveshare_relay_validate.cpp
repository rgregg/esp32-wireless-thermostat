// On-board validation of the full relay control path on the Waveshare board:
//   ControllerRelayIo (interlock/force-off logic) -> Pca9554RelayBackend -> PCA9554 -> relays
//
// Drives a heat -> cool -> fan -> off sequence and reads the PCA9554 input port back
// each step, so we can confirm on real hardware that:
//   - begin() leaves relays off,
//   - each HVAC demand energizes the right relay bit (heat=0, cool=1, fan=2),
//   - the interlock forces all-off + waits when switching between relays.
// Nothing is wired to HVAC (bench) — relays just click.
#if defined(WAVESHARE_RELAY_VALIDATE)

#include <Arduino.h>
#include <Wire.h>

#include "controller/controller_relay_io.h"
#include "controller/pca9554_relay_backend.h"

using thermostat::ControllerRelayIo;
using thermostat::ControllerRelayIoConfig;
using thermostat::Pca9554RelayBackend;

namespace {
Pca9554RelayBackend g_backend;            // defaults: 0x20, sda42/scl41, heat0/cool1/fan2/spare3
ControllerRelayIo g_io(g_backend, ControllerRelayIoConfig{});  // default interlock waits

uint8_t pca_input() {
  Wire.beginTransmission(0x20);
  Wire.write(0x00);  // input port
  if (Wire.endTransmission(false) != 0) return 0xEE;
  if (Wire.requestFrom(0x20, 1) != 1) return 0xEE;
  return Wire.available() ? Wire.read() : 0xEE;
}
}  // namespace

void setup() {
  // Silence the on-board buzzer (GPIO46) FIRST — it's uncontrolled/floating at boot
  // and can sound until driven. Drive it to the off level (LOW).
  pinMode(46, OUTPUT);
  digitalWrite(46, LOW);

  Serial.begin(115200);
  delay(2500);
  Serial.println();
  Serial.println("==== Waveshare relay control-path validation ====");
  Serial.println("[buzzer] GPIO46 driven LOW to silence");
  Serial.println("ControllerRelayIo (interlock) -> Pca9554RelayBackend -> PCA9554");
  g_io.begin();
  Serial.printf("after begin(): PCA input = 0x%02X (expect 0x00 = all relays off)\n", pca_input());
  Serial.println("sequence: 3s off, 5s HEAT, 5s COOL, 5s FAN, then OFF (interlock forces off+wait on each switch)");
}

void loop() {
  const uint32_t t = millis();

  RelayDemand d;  // default all-false = off
  const char *phase = "OFF";
  if (t >= 3000 && t < 8000) { d.heat = true; phase = "HEAT"; }
  else if (t >= 8000 && t < 13000) { d.cool = true; phase = "COOL"; }
  else if (t >= 13000 && t < 18000) { d.fan = true; phase = "FAN"; }
  else if (t >= 18000) { phase = "OFF(final)"; }

  g_io.apply(t, d, false);

  static uint32_t last = 0;
  if (t - last >= 500) {
    last = t;
    const RelayDemand &o = g_io.latched_output();
    Serial.printf("[%6lu] demand=%-10s -> latched h%d c%d f%d | PCA input=0x%02X | pending=%s\n",
                  t, phase, o.heat, o.cool, o.fan, pca_input(), g_io.pending_name());
  }

  if (t >= 22000) {
    Serial.println("[done] validation sequence complete. Final relay state below, then idling.");
    Serial.printf("final PCA input = 0x%02X (expect 0x00 all off)\n", pca_input());
    while (true) delay(1000);
  }
  delay(20);
}

#endif  // WAVESHARE_RELAY_VALIDATE
