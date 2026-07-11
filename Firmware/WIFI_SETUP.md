# TinyMaker WiFi Remote Control

Adds WiFi to the TinyMaker MSLA printer so you can upload print folders, and
start/stop/pause prints from a browser, plus WiFi status on the UI LCD.

## What was added

- **`TinyMaker_Firmware_v1_0_2/Network.ino`** — WiFi bring-up, a tiny HTTP server, and
  `run_print_job()` (the print routine, extracted so the network can start it).
- **`TinyMaker_Firmware_v1_0_2/index_html.h`** — the web UI page (served from flash).
- Edits to the main sketch (`#include`s, config, `setup()`, `loop()`) and `Interface.ino`
  (a small WiFi glyph on the home and file-select screens).

## How it works

- **No WiFi credentials are compiled into the firmware** — it's safe to publish/fork.
  Credentials are configured at runtime (see below).
- On boot it reads **`/wifi.txt`** from the SD card and joins that network (station mode).
  If there's no config, or it can't join, it hosts a hotspot **`TinyMaker`** (password
  `tinymaker123`) so the setup page is always reachable. Also answers to
  **`http://tinymaker.local`** (mDNS).
- The UI LCD shows a **network screen** (mode / SSID / IP) for 3 s at boot — note the IP —
  and a small WiFi bar glyph on the menus (green = joined WiFi, blue = hotspot, red = none).
- Web UI at `http://<printer-ip>/`: live status, folder list, folder upload, a WiFi setup
  card, and Start / Stop / Pause / Resume.

## Set your WiFi (two ways — no reflashing)

**A. Edit a file on the SD card (easiest, works headless).**
Copy `Firmware/wifi.txt.example` to the **root of the SD card** as **`wifi.txt`**, with your
network on line 1 and password on line 2:

```
YourNetworkName
YourWiFiPassword
```

Put the card back in the printer and power-cycle. (2.4 GHz networks only.)

**B. From the printer's hotspot (no computer file editing).**
If there's no `wifi.txt`, the printer starts a hotspot. Join WiFi **`TinyMaker`**
(password `tinymaker123`) from your phone/laptop, open **`http://tinymaker.local`** (or
`http://192.168.4.1`), go to **WiFi Setup**, tap **Scan**, pick your network, enter the
password, and **Save & Restart**. The printer writes `wifi.txt` for you and reboots onto
your network.

> The password is stored in plain text in `wifi.txt` on the SD card (same trust level as
> the print files already on it).

### Safety notes (resin printer)

- Starting a print over WiFi begins **UV exposure and Z-axis motion**. Only start a job
  you've physically prepared (resin in the vat, plate installed). The web UI asks you to
  confirm.
- **Stop is applied at the next layer boundary** (it never yanks the plate mid-exposure),
  so expect up to one layer of latency. During a long base-layer exposure that can be tens
  of seconds.
- **Don't upload while a print is running** — SD-writing actions are refused during a print.

### HTTP endpoints

| Method | Path | Purpose |
|---|---|---|
| GET  | `/` | Web UI |
| GET  | `/status` | JSON: printing, layer, progress, WiFi/SSID/IP, free RAM, uptime |
| GET  | `/list` | JSON array of printable folders (those containing `1.png`); 503 while printing |
| GET  | `/preview?folder=NAME` | Count layers + estimate (the "Processing" step) without starting |
| POST | `/upload?folder=NAME&name=FILE.png` | Raw file body → `/NAME/FILE.png` on SD (refused while printing) |
| GET  | `/start?folder=NAME` | Begin a print |
| GET  | `/stop` / `/pause` / `/resume` | Print control |
| GET  | `/delete?folder=NAME` | Delete a folder |
| GET  | `/scan` | JSON array of nearby WiFi networks (for the setup page) |
| GET  | `/savewifi?ssid=..&pass=..` | Write `/wifi.txt` and reboot to apply |

---

## Build & upload

**Toolchain matters.** This firmware uses `Arduino_GFX 1.2.0`, which only builds on the
**ESP32 Arduino core 2.0.x** (verified with **2.0.17**). The newer **core 3.x will NOT
compile** it (`'GPIO' was not declared`). Compile-verified size: ~887 KB / 67% of the
default app partition, so the **default partition scheme is fine**.

1. **Arduino IDE.** The repo recommends 1.8.19. IDE 2.x also works — just make sure the
   board core is 2.0.x, not 3.x.
2. **Board core:** Boards Manager → *esp32 by Espressif Systems* → install **2.0.17**
   (add `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   in Preferences → Additional Boards Manager URLs if needed).
3. **Libraries:** Sketch → Include Library → Add .ZIP Library, and add all four zips from
   `Firmware/Libraries/` (use these exact versions).
4. **USB driver:** if no COM port appears, install `Firmware/Driver/CH341SER.EXE`.
5. **Open** the whole `TinyMaker_Firmware_v1_0_2` folder.
6. **Tools:** Board = *ESP32 Dev Module* (or your board), Port = your COMx, Upload Speed
   921600. **Partition scheme:** leave the menu as-is — the sketch ships a
   `partitions.csv` that is picked up automatically and **overrides** the menu. Do not
   delete it: it defines the custom `eeprom` partition at `0x290000` where all print
   settings live. Build with the default scheme (no `partitions.csv`) and `EEPROM.begin()`
   fails, so layer height / exposures / layer counts all read back as **0** (blank
   estimate, no real layer processing). Because a normal flash never writes the `0x290000`
   region, your saved settings survive across reflashes.
7. **Upload.** After boot, read the IP off the LCD and browse to it (or `tinymaker.local`).

---

## Back up your CURRENT firmware first

This reads the entire flash chip (including your tuned print settings) so you can restore
the exact stock image if anything goes wrong.

Install esptool with `pip install esptool`, then run it as `python -m esptool`
(esptool v5+; note the commands use **hyphens**: `read-flash`, `write-flash`).

**Find the COM port:** Arduino IDE → Tools → Port, or Windows Device Manager. This board's
USB-serial chip is a **CH340** (the bundled `CH341SER.EXE` driver covers CH340 too). On the
verified unit it enumerates as **COM3** — substitute your own port below.

> Use `--baud 460800`. 921600 drops bytes over the CH340 on a full read
> (`Corrupt data, expected 0x1000 bytes...`). If 460800 still fails, use 115200 (slower but
> rock-solid), and plug the USB straight into the PC (no hub) with a real data cable.

**Read the full 4 MB flash to a file** (ESP32-D0WD modules are 4 MB = `0x400000`):

```bash
python -m esptool --chip esp32 --port COM3 --baud 460800 read-flash 0x0 0x400000 tinymaker_stock_backup.bin
```

**To restore the stock firmware later:**

```bash
python -m esptool --chip esp32 --port COM3 --baud 460800 write-flash 0x0 tinymaker_stock_backup.bin
```

---

## Flash the new firmware from the command line (esptool)

You do **not** need the Arduino IDE to flash — a prebuilt merged image is the simplest path
and avoids the core-version issue entirely.

**One file, one command** — flash the merged image to offset `0x0` (close the Serial
Monitor first, or you'll get "Access is denied"):

```bash
python -m esptool --chip esp32 --port COM3 --baud 460800 write-flash 0x0 tinymaker_wifi_merged.bin
```

The merged image bundles the bootloader (`0x1000`), partition table (`0x8000`),
`boot_app0` (`0xe000`), and the app (`0x10000`) — the exact same layout the Arduino IDE
uploads. Your saved print settings (EEPROM/NVS region) are untouched by this flash.

If you ever have only the four separate parts instead of the merged file:

```bash
python -m esptool --chip esp32 --port COM3 --baud 460800 write-flash \
  0x1000 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 app.bin
```

After it finishes it hard-resets automatically; watch the LCD for the network screen (IP).

### Or build + flash from source with arduino-cli

Requires ESP32 core **2.0.x** (3.x won't compile `Arduino_GFX 1.2.0`) and the four libs:

```bash
arduino-cli config set library.enable_unsafe_install true
arduino-cli lib install --zip-path Firmware/Libraries/AccelStepper-1.64.0.zip \
  --zip-path Firmware/Libraries/Arduino_GFX-1.2.0.zip \
  --zip-path Firmware/Libraries/PNGdec-1.0.1.zip \
  --zip-path Firmware/Libraries/SdFat-1.1.2.zip
arduino-cli core install esp32:esp32@2.0.17 \
  --additional-urls https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
arduino-cli compile --fqbn esp32:esp32:esp32 Firmware/TinyMaker_Firmware_v1_0_2
arduino-cli upload  --fqbn esp32:esp32:esp32 -p COM3 Firmware/TinyMaker_Firmware_v1_0_2
```

---

## Recommended first run (no resin)

Before trusting a real print, dry-run it:

1. Flash, confirm the LCD network screen shows an IP and the glyph is green.
2. Browse to the IP; confirm `/status` shows **Idle** and `/list` shows your SD folders.
3. Upload a small folder over WiFi; confirm it appears in `/list`.
4. With **no resin and the vat safe to move**, press **Start** from the web UI and confirm
   homing begins, the LCD shows progress, and **Stop** halts it at the next layer.

Only then run a real print.
