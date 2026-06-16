// Bench bring-up for the Waveshare ESP32-S3-ETH-8DI-8RO (real controller board).
//
// Verifies, over serial alone (no relays wired to HVAC):
//   1. I2C bus on SDA=42/SCL=41 and the PCA9554 relay expander at 0x20.
//   2. The POWER-ON relay state (reads the PCA9554 input port BEFORE writing
//      anything — reflects the pull-resistor-determined level while the expander's
//      POR config leaves pins as inputs/high-Z). 0x00 => relays de-energized at
//      power-on (safe). Non-zero => some relays energized before firmware (unsafe;
//      check board pulls / relay polarity).
//   3. The safe-init sequence (write output register 0x00 BEFORE switching pins to
//      outputs) leaves all relays de-energized.
//   4. Per-relay control (cycles each relay once) to confirm the bit->relay mapping
//      for the future Pca9554RelayBackend.
//
// Build with the waveshare-bringup env; flash + read on piserial5.
#if defined(WAVESHARE_BRINGUP)

#include <Arduino.h>
#include <Wire.h>

namespace {
constexpr int kSdaPin = 42;
constexpr int kSclPin = 41;
constexpr uint8_t kPca9554 = 0x20;
// PCA9554 registers
constexpr uint8_t kRegInput = 0x00;
constexpr uint8_t kRegOutput = 0x01;
constexpr uint8_t kRegConfig = 0x03;  // 1 = input, 0 = output

uint8_t pca_read(uint8_t reg) {
  Wire.beginTransmission(kPca9554);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0xEE;  // NAK marker
  if (Wire.requestFrom(static_cast<int>(kPca9554), 1) != 1) return 0xEE;
  return Wire.available() ? Wire.read() : 0xEE;
}

bool pca_write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(kPca9554);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

int g_relay = 0;
uint32_t g_last = 0;
bool g_cycle_done = false;
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(2500);  // let the USB-CDC/JTAG console attach
  Serial.println();
  Serial.println("==== Waveshare ESP32-S3-ETH-8DI-8RO bring-up ====");

  Wire.begin(kSdaPin, kSclPin);
  Wire.setClock(100000);

  // 1. I2C scan
  Serial.print("[i2c] devices found:");
  int n = 0;
  for (uint8_t a = 1; a < 127; ++a) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { Serial.printf(" 0x%02X", a); ++n; }
  }
  Serial.printf("  (%d total)\n", n);

  // 2. Power-on relay state (read BEFORE we write anything to the expander).
  const uint8_t por_in = pca_read(kRegInput);
  const uint8_t por_out = pca_read(kRegOutput);
  const uint8_t por_cfg = pca_read(kRegConfig);
  Serial.printf("[pca9554@0x20] POR registers: input=0x%02X output=0x%02X config=0x%02X"
                "  (config 0xFF = all inputs/high-Z, the expander's reset default)\n",
                por_in, por_out, por_cfg);
  Serial.printf("[SAFETY] relay state at POWER-ON (input port) = 0x%02X -> %s\n",
                por_in, (por_in == 0x00) ? "all OFF (board pulls relays off at power-on: SAFE)"
                                         : "SOME ENERGIZED before firmware ran (check pulls/polarity!)");

  // 3. Safe-init: write output register 0x00 FIRST, then set all pins to outputs.
  const bool w1 = pca_write(kRegOutput, 0x00);
  const bool w2 = pca_write(kRegConfig, 0x00);
  delay(5);
  const uint8_t after = pca_read(kRegInput);
  Serial.printf("[SAFETY] after safe-init (output=0x00 then config=outputs, writes ok=%d/%d): "
                "input port = 0x%02X -> %s\n",
                (int)w1, (int)w2, after,
                (after == 0x00) ? "all relays OFF (verified)" : "NOT all off (PROBLEM)");
  Serial.println("[bring-up] cycling each relay once (1s) to confirm bit->relay mapping...");
  g_last = millis();
}

void loop() {
  if (g_cycle_done) { delay(100); return; }
  if (millis() - g_last < 1200) { delay(20); return; }
  g_last = millis();

  const uint8_t mask = static_cast<uint8_t>(1u << g_relay);
  pca_write(kRegOutput, mask);
  const uint8_t rb = pca_read(kRegInput);
  Serial.printf("[relay-test] relay %d (bit 0x%02X) ON -> input readback 0x%02X %s\n",
                g_relay, mask, rb, (rb == mask) ? "OK" : "(readback mismatch)");

  if (++g_relay >= 8) {
    pca_write(kRegOutput, 0x00);
    Serial.println("[relay-test] cycle complete; all relays OFF. Idling.");
    g_cycle_done = true;
  }
}

#endif  // WAVESHARE_BRINGUP
