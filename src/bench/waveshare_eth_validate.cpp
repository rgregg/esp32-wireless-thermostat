// W5500 Ethernet bring-up on the Waveshare board. Brings up the SPI Ethernet and
// reports link state + DHCP IP. Cable is connected to the user's LAN (has DHCP).
// W5500 pins (Waveshare): SCK=15, MISO=14, MOSI=13, CS=16, INT=12, RST=-1 (try; verify).
#if defined(WAVESHARE_ETH_VALIDATE)
#include <Arduino.h>
#include <ETH.h>

namespace {
constexpr int kEthCs = 16, kEthIrq = 12, kEthRst = -1;
constexpr int kEthSck = 15, kEthMiso = 14, kEthMosi = 13;
bool g_began = false;
}  // namespace

void setup() {
  pinMode(46, OUTPUT); digitalWrite(46, LOW);  // silence buzzer
  Serial.begin(115200); delay(2500);
  Serial.println("\n==== W5500 Ethernet bring-up ====");
  Serial.printf("[eth] CS=%d IRQ=%d RST=%d  SCK=%d MISO=%d MOSI=%d  host=SPI2\n",
                kEthCs, kEthIrq, kEthRst, kEthSck, kEthMiso, kEthMosi);
  g_began = ETH.begin(ETH_PHY_W5500, 1 /*phy_addr*/, kEthCs, kEthIrq, kEthRst,
                      SPI2_HOST, kEthSck, kEthMiso, kEthMosi);
  Serial.printf("[eth] ETH.begin() -> %s\n", g_began ? "ok" : "FAILED (check wiring/RST pin)");
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last >= 2000) {
    last = millis();
    Serial.printf("[eth] linkUp=%d  IP=%s  speed=%dMbps %s  mac=%s\n",
                  ETH.linkUp() ? 1 : 0, ETH.localIP().toString().c_str(),
                  ETH.linkSpeed(), ETH.fullDuplex() ? "FD" : "HD", ETH.macAddress().c_str());
  }
  delay(50);
}
#endif
