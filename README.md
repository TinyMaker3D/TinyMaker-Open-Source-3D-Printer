# TinyMaker — WiFi Remote Control (community fork)

> This is a community fork of the [TinyMaker Open-Source 3D Printer](#upstream-project)
> firmware that adds **WiFi remote control**: upload sliced print folders over your
> network and **start / stop / pause / resume** prints from any browser, plus a live
> status page and WiFi info on the printer's LCD.

**No credentials are baked into the firmware** — you set your WiFi from a file on the SD
card or from the printer's own setup page, so it's safe to flash and share.

![](Images/Palm_Sized.jpg)

## Features

- 📶 Joins your 2.4 GHz WiFi, or hosts a **`TinyMaker`** setup hotspot if unconfigured
- 🌐 Web UI at **`http://tinymaker.local`**: status, folder upload, print controls
- 🖼️ Upload a whole sliced folder (or multi-select PNGs) straight to the SD card
- ▶️ **Processing → estimate → confirm** modal before a print starts (layers, height, time)
- 🛠️ Built-in **WiFi Setup** page (scan networks, save, auto-reconnect) — no re-flashing
- 📟 LCD shows the network screen + a WiFi status glyph
- 🔒 Safety-first: remote Stop applies at the next layer boundary; SD writes refused mid-print

## Quick start (flash-and-go, ~5 min)

You only need a USB cable and Python — no Arduino IDE.

1. **Install esptool:** `pip install esptool`
2. **Plug in** the printer over USB. If no COM port shows up, install the CH340/CH341
   driver (`Firmware/Driver/CH341SER.EXE` on Windows). Note the port (e.g. `COM3`).
3. **(Recommended) Back up the stock firmware first** so you can always revert:
   ```
   python -m esptool --chip esp32 --port COM3 --baud 460800 read-flash 0x0 0x400000 stock_backup.bin
   ```
4. **Flash this fork** (close any Serial Monitor first):
   ```
   python -m esptool --chip esp32 --port COM3 --baud 460800 write-flash 0x0 Firmware/prebuilt/TinyMaker_WiFi.bin
   ```
5. **Set your WiFi** (pick one):
   - **File:** copy `Firmware/wifi.txt.example` to the SD card root as **`wifi.txt`**
     (line 1 = network name, line 2 = password), reinsert the card, power-cycle; **or**
   - **Hotspot:** join WiFi **`TinyMaker`** (password `tinymaker123`), open
     **`http://tinymaker.local`** → **WiFi Setup** → Scan → your network → Save & Restart.
6. Watch the LCD for the IP, then browse to it (or `http://tinymaker.local`).

> To revert to stock: `python -m esptool --chip esp32 --port COM3 --baud 460800 write-flash 0x0 stock_backup.bin`

**Full details, endpoints, and building from source:** see **[`Firmware/WIFI_SETUP.md`](Firmware/WIFI_SETUP.md)**.

> ⚠️ **Resin-printer safety:** starting a print begins UV exposure and Z-axis motion.
> Only start jobs you've physically prepared, and keep someone nearby. This firmware is
> provided as-is with no warranty.

## Hardware

ESP32 (ESP32-D0WD) MSLA/resin printer. Print jobs are folders of PNG layer slices named
`1.png`, `2.png`, … on a FAT32 SD card.

## Building from source

Requires **Arduino IDE** with **ESP32 core 2.0.x** (3.x will not compile `Arduino_GFX 1.2.0`)
and the four bundled libraries in `Firmware/Libraries/`. A `partitions.csv` in the sketch
folder defines the custom `eeprom` partition where print settings live — don't remove it.
Step-by-step build instructions are in [`Firmware/WIFI_SETUP.md`](Firmware/WIFI_SETUP.md).

---

## Upstream project

This fork is based on the official TinyMaker repository. Original notes:

> :speech_balloon: This repository is not yet complete. At the moment, we have only uploaded
> part of the available materials, including firmware, libraries, schematics, and 3D models.
>
> If you plan to use the firmware provided here, please make sure to use the correct version
> of the libraries (the versions we uploaded are the verified ones), and also use Arduino IDE
> version 1.8.19.

Please support the original creators. See `LICENSE.md` for licensing.
