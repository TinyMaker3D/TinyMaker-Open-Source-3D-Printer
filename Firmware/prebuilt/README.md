# Prebuilt firmware

`TinyMaker_WiFi.bin` — the WiFi remote-control firmware, ready to flash to an ESP32-based
TinyMaker printer. It is a full image (bootloader + partition table + app) for a 4 MB
ESP32, built against ESP32 Arduino core 2.0.17. **No WiFi credentials are included.**

## Flash

Close any Serial Monitor first, then (replace `COM3` with your port):

```
python -m esptool --chip esp32 --port COM3 --baud 460800 write-flash 0x0 TinyMaker_WiFi.bin
```

Then set your WiFi via `wifi.txt` on the SD card or the printer's hotspot setup page — see
[`../WIFI_SETUP.md`](../WIFI_SETUP.md).

## Integrity

```
SHA-256  0589ed2962141fca54844149db66c88a8eb31028ad2152cf678083566476e40b
```

Verify: `sha256sum TinyMaker_WiFi.bin` (Linux/macOS/Git Bash) or
`CertUtil -hashfile TinyMaker_WiFi.bin SHA256` (Windows).
