# Notes on upgrade

since we're no longer using esphome, there were a few assumptions we had to deal with... we can now clean those up.

1) our thermostat - controller interface should really be command driven both directions, so that updates are idemopotent and can be ignored if we have already received them. We did this for the thermostat -> controller direction, but we should do it the other way too.
2) we need to fully support Home Assistant over MQTT for all the settings and sensor values from both the controller and the display. Ideally let's make it look like _one device_ to Home Assistant, not two.
3) we can support redundance on the interface between the two devices. Since both are connected to WiFi the majority of the time, we can prefer using the MQTT broker as the primary way the devices communicate with each other. However, if they are unable to communicate that way or are unable to connect to WiFi they should fall back to ESP-NOW for a direct connection.
