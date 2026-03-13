## Summary


## OTA soak test (required before merging firmware changes)

If this PR touches firmware source, platformio.ini, or partition tables:

- [ ] Built both firmware binaries
- [ ] `bash scripts/ota_soak_test.sh 5 all` — all 10 rounds pass (5 controller, 5 display)

Without a passing soak test, merging may produce a firmware that cannot OTA-update itself, requiring serial flash on all devices.
