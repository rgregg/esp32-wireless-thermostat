#include "thermostat/esp32s3_thermostat_firmware.h"

#if defined(ARDUINO)
#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }
  thermostat::thermostat_firmware_setup();
}

void loop() {
  thermostat::thermostat_firmware_loop();
  delay(5);
}
#endif
