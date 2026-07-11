/**
 * @file Network.ino
 * @brief WiFi + tiny web control for the Tinymaker MSLA printer (ESP32).
 *
 * Adds:
 *   - WiFi station mode with automatic hotspot (AP) fallback + mDNS (tinymaker.local)
 *   - A small web UI to upload print folders (PNG slices) to the SD card
 *   - Remote Start / Stop / Pause / Resume of prints
 *   - A live status endpoint and an LCD network-info screen
 *
 * Why a hand-rolled HTTP server?
 *   The ESP32 core's <WebServer.h> includes FS.h, whose fs::File collides with the
 *   global File class of SdFat 1.1.2. Since Arduino concatenates all .ino files into a
 *   single translation unit, we cannot have both. So we run a minimal HTTP/1.1 server
 *   directly on WiFiServer, which keeps full SdFat access here.
 *
 * IMPORTANT SAFETY NOTE
 * ---------------------
 * Starting a print over the network begins UV exposure and Z-axis motion with possibly
 * nobody at the machine. Only start a print you have physically prepared (resin in the
 * vat, build plate clean and installed). The web UI asks you to confirm before starting.
 * Network Stop takes effect at the next layer boundary (it never yanks the plate
 * mid-exposure), so there can be up to one layer of latency before a print halts.
 *
 * How network commands stay safe:
 *   We only service the web server (web_loop()) at points where the SD card and exposure
 *   timing are not in a critical section: while idle in the menus, at each layer boundary,
 *   during homing, and while paused. SD-mutating actions (upload/delete/start) are
 *   refused while a print is running.
 */

// ===================================================================================
// Web UI (single self-contained page, served from flash)
// Defined first so the request handler can reference it.
// ===================================================================================
#include "index_html.h"

// ===================================================================================
// Small string helpers
// ===================================================================================

/** Escape a C string for safe inclusion inside a JSON double-quoted value. */
String jsonEscape(const char *s) {
  String o;
  for (const char *p = s; *p; p++) {
    char c = *p;
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c >= 0x20)        { o += c; }
  }
  return o;
}

/** Convert one hex digit to its value, or 0 if invalid. */
static uint8_t hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 0;
}

/** URL-decode a query value ("%20"/"+" etc.). */
String urlDecode(const String &s) {
  String o;
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '+') {
      o += ' ';
    } else if (c == '%' && i + 2 < s.length()) {
      o += (char)((hexVal(s[i + 1]) << 4) | hexVal(s[i + 2]));
      i += 2;
    } else {
      o += c;
    }
  }
  return o;
}

/** Extract a raw (still URL-encoded) parameter value from a query string. */
String getParam(const String &query, const String &key) {
  int i = 0;
  int n = query.length();
  while (i < n) {
    int amp = query.indexOf('&', i);
    if (amp < 0) amp = n;
    String pair = query.substring(i, amp);
    int eq = pair.indexOf('=');
    if (eq > 0 && pair.substring(0, eq) == key)
      return pair.substring(eq + 1);
    i = amp + 1;
  }
  return "";
}

/** Strip path separators and parent refs so a name is safe as an SD path component. */
void sanitizeName(String &s) {
  s.replace("/", "");
  s.replace("\\", "");
  s.replace("..", "");
}

/** Draw a tiny 3-bar WiFi glyph at (x,y): green=STA, blue=AP, red=disconnected. */
void draw_wifi_glyph(int x, int y) {
  uint16_t col;
  if (wifi_is_ap)                         col = 0x879F; // light blue: hotspot mode
  else if (WiFi.status() == WL_CONNECTED) col = GREEN;  // connected to your WiFi
  else                                    col = RED;    // not connected
  gfx2->fillRect(x,      y + 8, 3, 4,  col);
  gfx2->fillRect(x + 5,  y + 5, 3, 7,  col);
  gfx2->fillRect(x + 10, y + 1, 3, 11, col);
}

// ===================================================================================
// WiFi bring-up
// ===================================================================================
/**
 * @brief Connect to WiFi (STA) or fall back to a self-hosted hotspot (AP),
 *        start mDNS and the HTTP server.
 */
/** Load the WiFi network name + password from /wifi.txt on the SD card (if present). */
void read_wifi_config() {
  sta_ssid = "";
  sta_pass = "";
  File f = SD.open(WIFI_CONFIG_FILE);
  if (f) {
    sta_ssid = f.readStringUntil('\n'); sta_ssid.trim();
    sta_pass = f.readStringUntil('\n'); sta_pass.trim();
    f.close();
  }
}

void wifi_setup() {
  read_wifi_config();

  bool connected = false;
  if (sta_ssid.length() > 0) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(sta_ssid.c_str(), sta_pass.c_str());
    // Give the router up to ~12 seconds to associate + hand out an IP.
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 12000) {
      delay(250);
    }
    connected = (WiFi.status() == WL_CONNECTED);
  }

  if (connected) {
    wifi_is_ap = false;
    wifi_ip = WiFi.localIP().toString();
  } else {
    // Unconfigured, or the join failed -> host a hotspot so the setup page is reachable.
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    wifi_is_ap = true;
    wifi_ip = WiFi.softAPIP().toString();
  }

  if (MDNS.begin(MDNS_HOST)) {
    MDNS.addService("http", "tcp", 80);
  }

  httpd.begin();
}

/**
 * @brief Show the network connection details on the UI LCD (gfx2) for a few seconds
 *        so you can note the IP address.
 */
void show_network_screen() {
  gfx2->fillScreen(BLACK);
  gfx2->fillRoundRect(0, 0, 160, 80, 5, ORANGE);
  gfx2->fillRoundRect(2, 2, 156, 76, 3, BLACK);
  gfx2->fillRoundRect(0, 0, 160, 20, 3, ORANGE);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(38, 14);
  gfx2->print(wifi_is_ap ? "Hotspot" : "WiFi");

  gfx2->setCursor(7, 38);
  gfx2->print(wifi_is_ap ? AP_SSID : sta_ssid.c_str());

  gfx2->setCursor(7, 56);
  gfx2->print(wifi_ip);

  gfx2->setTextColor(0x879F);
  gfx2->setCursor(7, 74);
  gfx2->print(MDNS_HOST);
  gfx2->print(".local");

  draw_wifi_glyph(139, 4);
  delay(3000);
}

// ===================================================================================
// Print-control glue (consumed at safe points inside run_print_job)
// ===================================================================================

/**
 * @brief Count the printable layers in the currently selected folder (foldersel_long)
 *        into the global layer_counter. Mirrors screen11/screen111, including the
 *        halving applied for layer heights above 0.06mm.
 * @return true if the folder has at least one printable layer.
 */
bool count_layers_selected() {
  File entry;
  layer_counter = 0;

  do { // fast seek in steps of 100
    layer_counter += 100;
    FileName = foldersel_long; FileName += "/"; FileName += layer_counter; FileName += ".png";
    entry = SD.open(FileName);
  } while (entry);
  layer_counter -= 100;

  do { // exact seek
    layer_counter++;
    FileName = foldersel_long; FileName += "/"; FileName += layer_counter; FileName += ".png";
    entry = SD.open(FileName);
  } while (entry);
  layer_counter--;

  if (Layer_Height > 0.06) layer_counter /= 2;
  return layer_counter > 0;
}

/**
 * @brief If the web UI requested a print, set it up and run it. Called from loop() while
 *        idle. Refuses if a print is running or the folder is invalid.
 */
void handle_network_start() {
  if (!net_start_request) return;
  net_start_request = false;
  if (printing) return;
  if (net_start_folder[0] == '\0') return;

  SD.begin(SDCS, SD_SCK_MHZ(16)); // re-init like the physical-button path does

  strncpy(foldersel_long, net_start_folder, 100);
  foldersel_long[100] = '\0';
  foldersel = String(foldersel_long).substring(0, 10);

  if (!count_layers_selected()) return; // folder missing or empty -> ignore

  get_motor_updown_time(); // populate the on-screen time estimate
  run_print_job();
}

/**
 * @brief Translate pending network Stop/Pause requests into the firmware's existing
 *        print flags. Called at each layer boundary (a safe point).
 */
void apply_network_print_controls() {
  if (net_stop_request) {
    net_stop_request = false;
    print_canceled = true;
    print_paused = false;
    current_state = 4; // Canceling
    screen1111_state();
  } else if (net_pause_request) {
    net_pause_request = false;
    if (!print_paused) {
      print_paused = true;
      current_state = 5; // Pausing (the pause block completes the transition)
      screen1111_state();
      screen1112();
    }
  }
}

// ===================================================================================
// run_print_job()
// ===================================================================================
/**
 * @brief Executes a full print. This is the print body that used to live inline in
 *        loop()'s "case 111", unchanged except for network-servicing hooks added at
 *        safe points (homing loop, layer boundary, pause loop). Called both from the
 *        physical Start button (case 111) and from handle_network_start().
 */
void run_print_job() {
  printing = true;
  // Clear any stale network commands so a print does not immediately self-cancel.
  net_stop_request = false;
  net_pause_request = false;
  net_resume_request = false;

  homing_canceled = false;
  print_paused = false;
  print_canceled = false;
  current_state = 0;
  current_layer = 0;
  Position_before_pause = 0;
  Transition_Exposure = Base_Exposure;

  // Pre-compute the estimate so /status shows it during homing (refined each layer below).
  estimated_seconds = (long)Base_Layer * (long)Base_Exposure
                    + (long)layer_counter * (long)Regular_Exposure
                    + (long)(motor_updown_time * (float)(layer_counter - 1));
  estimated_hours = estimated_seconds / 3600;
  estimated_minutes = (estimated_seconds % 3600) / 60;

  screen1111();
  gfx2->fillRect(136, 52, 6, 16, 0x8410);
  gfx2->fillRect(146, 52, 6, 16, 0x8410);
  screen1111_state();
  screen1111UP();
  delay(500);

  // -------------------------------------------------------------------------------
  // Homing Sequence
  // -------------------------------------------------------------------------------
  stepper.setCurrentPosition(0);
  stepper.setMaxSpeed(Drop_Back_Feedrate * steps_mm / 60);
  stepper.enableOutputs();
  long initial_homing = 0;
  long current_position;
  while (!digitalRead(end_stop)) {
    stepper.moveTo(initial_homing); // Set the position to move to
    initial_homing--;               // Decrease by 1 for next move if needed
    stepper.run();                  // Start moving the stepper
    current_position = stepper.currentPosition();

    // Network hook: allow a remote Stop to abort homing (safe: motor only holds).
    web_loop();
    if (net_stop_request) {
      net_stop_request = false;
      stepper.disableOutputs();
      homing_canceled = true;
      break;
    }

    if (current_position < -106799) {
      stepper.disableOutputs();
      homing_canceled = true;
      gfx2->fillRoundRect(5, 5, 150, 70, 7, BLACK);
      gfx2->fillRoundRect(7, 7, 146, 66, 5, RED);
      gfx2->fillRoundRect(9, 9, 142, 62, 3, BLACK);
      gfx2->fillRoundRect(16, 11, 5, 10, 1, RED);
      gfx2->fillCircle(18, 25, 2, RED);
      gfx2->setTextColor(WHITE);
      gfx2->setTextSize(1);
      gfx2->setCursor(27, 23);
      gfx2->println("Homing error,");
      gfx2->setCursor(13, 41);
      gfx2->println("print canceled.");
      gfx2->fillRoundRect(82, 51, 67, 18, 2, 0x879F);
      gfx2->setCursor(100, 64);
      gfx2->println("OK :(");
      while (digitalRead(buttonOK) == HIGH);
      break;
    }
    if (Duration >= 500 && screen == 1111 && digitalRead(buttonOK) == LOW) {
      screen11111();
      startTime = millis();
    }
    Duration = millis() - startTime;
    if (Duration >= 500 && screen == 11111 && digitalRead(buttonOK) == LOW) {
      stepper.disableOutputs();
      homing_canceled = true;
      break;
    }
    if (Duration >= 500 && screen == 11111 && digitalRead(buttonBack) == LOW) {
      screen1111();
      gfx2->fillRect(136, 52, 6, 16, 0x8410);
      gfx2->fillRect(146, 52, 6, 16, 0x8410);
      screen1111_state();
      screen1111UP();
    }
  }
  delay(50);

  if (homing_canceled != true) {
    stepper.disableOutputs();
    stepper.setCurrentPosition(0);
    digitalWrite(FAN, HIGH);
    if (screen != 11111) {
      gfx2->fillRect(136, 52, 6, 16, YELLOW);
      gfx2->fillRect(146, 52, 6, 16, YELLOW);
    }
  }

  // -------------------------------------------------------------------------------
  // Printing Loop
  // -------------------------------------------------------------------------------
  while (!homing_canceled && !print_canceled) {
    // Network hook: safe layer boundary. Service web clients and apply any pending
    // remote Stop/Pause. A remote Stop breaks here before the next exposure.
    web_loop();
    apply_network_print_controls();
    if (print_canceled) break;

    estimated_seconds = 0;
    estimated_hours = 0;
    estimated_minutes = 0;
    motor_updown_time_total = 0;
    if (current_layer < Base_Layer)
      estimated_seconds += (Base_Layer - current_layer) * Base_Exposure;
    estimated_seconds += (layer_counter - current_layer) * Regular_Exposure;
    motor_updown_time_total += (layer_counter - current_layer - 1) * motor_updown_time;
    estimated_seconds += motor_updown_time_total;
    estimated_hours = estimated_seconds / 3600;
    estimated_minutes = (estimated_seconds % 3600) / 60;

    print_next_png();

    if (screen != 11111 && screen != 11112) {
      gfx2->fillRoundRect(2, 38, 116, 40, 3, BLACK);
      gfx2->setFont(&FreeSans8pt7b);
      gfx2->setTextColor(WHITE);
      gfx2->setTextSize(1);
      gfx2->setCursor(6, 54);
      gfx2->print(current_layer);
      gfx2->print(" / ");
      gfx2->print(layer_counter);
      gfx2->setCursor(6, 74);
      gfx2->print(estimated_hours);
      gfx2->print("h ");
      gfx2->print(estimated_minutes);
      gfx2->print("min");
    }

    if (current_state != 4 && current_state != 5) {
      current_state = 1;
      screen1111_state();
    }

    turn_on_LED();
    gfx1->fillScreen(BLACK);

    if (current_state != 4 && current_state != 5) {
      current_state = 2;
      screen1111_state();
    }
    lift_print();
    delay(50);

    if (current_layer == layer_counter)
      break;

    // -----------------------------------------------------------------------------
    // Pause Handling
    // -----------------------------------------------------------------------------
    if (print_paused == true) {
      Position_before_pause = stepper.currentPosition();
      stepper.setMaxSpeed(Fast_Lift_Feedrate * steps_mm / 60);
      stepper.enableOutputs();
      if (Position_before_pause + (20 * steps_mm) <= max_height * steps_mm)
        stepper.move(20 * steps_mm);
      else
        stepper.moveTo(max_height * steps_mm);
      while (stepper.distanceToGo() != 0)
        stepper.run();
      stepper.disableOutputs();
      delay(10);

      current_state = 6;
      screen1111_state();
      gfx2->fillRect(136, 12, 16, 16, RED);
      gfx2->fillTriangle(136, 52, 136, 68, 152, 60, GREEN);
      screen1111DOWN();

      while (print_paused == true) {
        Duration2 = millis() - startTime2;

        // Network hook: allow remote Resume / Stop while paused (safe: idle here).
        web_loop();
        if (net_stop_request) {
          net_stop_request = false;
          screen1111();
          current_state = 4;
          screen1111_state();
          screen1111UP();
          print_canceled = true;
          print_paused = false;
        }
        if (net_resume_request) {
          net_resume_request = false;
          screen1111();
          current_state = 7;
          screen1111_state();
          gfx2->fillRect(136, 12, 16, 16, 0x8410);
          gfx2->fillRect(136, 52, 6, 16, 0x8410);
          gfx2->fillRect(146, 52, 6, 16, 0x8410);
          gfx2->drawRoundRect(128, 44, 32, 32, 3, 0x8410);
          stepper.setMaxSpeed(Fast_Lift_Feedrate * steps_mm / 60);
          stepper.enableOutputs();
          stepper.moveTo(Position_before_pause);
          while (stepper.distanceToGo() != 0)
            stepper.run();
          stepper.disableOutputs();
          delay(10);
          gfx2->fillRect(136, 12, 16, 16, RED);
          gfx2->fillRect(136, 52, 6, 16, YELLOW);
          gfx2->fillRect(146, 52, 6, 16, YELLOW);
          gfx2->drawRoundRect(128, 44, 32, 32, 3, WHITE);
          print_paused = false;
        }

        if (Duration2 >= 500 && digitalRead(buttonUp) == LOW && screen == 1112) {
          screen1111UP();
          Duration2 = 0;
          startTime2 = millis();
        }
        if (Duration2 >= 500 && digitalRead(buttonDown) == LOW && screen == 1112) {
          screen1111DOWN();
          Duration2 = 0;
          startTime2 = millis();
        }
        if (Duration2 >= 500 && digitalRead(buttonOK) == LOW && printing_item_updown == 1 && screen != 11111) {
          screen11111();
          Duration2 = 0;
          startTime2 = millis();
        }
        if (Duration2 >= 500 && digitalRead(buttonOK) == LOW && printing_item_updown == 0 && screen != 11113) {
          screen11113();
          Duration2 = 0;
          startTime2 = millis();
        }
        if (Duration2 >= 500 && digitalRead(buttonBack) == LOW && screen == 11111) {
          screen1111();
          screen1111_state();
          screen1112();
          screen1111UP();
          Duration2 = 0;
          startTime2 = millis();
        }
        if (Duration2 >= 500 && digitalRead(buttonBack) == LOW && screen == 11113) {
          screen1111();
          screen1111_state();
          screen1112();
          screen1111DOWN();
          Duration2 = 0;
          startTime2 = millis();
        }
        if (Duration2 >= 500 && digitalRead(buttonOK) == LOW && screen == 11111) {
          screen1111();
          current_state = 4;
          screen1111_state();
          screen1111UP();
          print_canceled = true;
          print_paused = false;
        }
        if (Duration2 >= 500 && digitalRead(buttonOK) == LOW && screen == 11113) {
          screen1111();
          current_state = 7;
          screen1111_state();
          gfx2->fillRect(136, 12, 16, 16, 0x8410);
          gfx2->fillRect(136, 52, 6, 16, 0x8410);
          gfx2->fillRect(146, 52, 6, 16, 0x8410);
          gfx2->drawRoundRect(128, 44, 32, 32, 3, 0x8410);
          stepper.setMaxSpeed(Fast_Lift_Feedrate * steps_mm / 60);
          stepper.enableOutputs();
          stepper.moveTo(Position_before_pause);
          while (stepper.distanceToGo() != 0)
            stepper.run();
          stepper.disableOutputs();
          delay(10);
          gfx2->fillRect(136, 12, 16, 16, RED);
          gfx2->fillRect(136, 52, 6, 16, YELLOW);
          gfx2->fillRect(146, 52, 6, 16, YELLOW);
          gfx2->drawRoundRect(128, 44, 32, 32, 3, WHITE);
          print_paused = false;
        }
      }
    }

    if (!print_canceled) {
      current_state = 3;
      screen1111_state();
      lower_print();
    }
  }
  if (!homing_canceled) {
    if (!print_canceled) {
      current_state = 8;
      screen1111_state();
      gfx2->fillRect(136, 12, 16, 16, 0x8410);
      gfx2->fillRect(136, 52, 6, 16, 0x8410);
      gfx2->fillRect(146, 52, 6, 16, 0x8410);
      if (printing_item_updown == 1)
        gfx2->drawRoundRect(128, 4, 32, 32, 3, 0x8410);
      if (printing_item_updown == 0)
        gfx2->drawRoundRect(128, 44, 32, 32, 3, 0x8410);
    }
    lift_finished_print();
  }
  digitalWrite(FAN, LOW);
  screen1();

  printing = false;
}

// ===================================================================================
// Minimal HTTP server
// ===================================================================================

/** Build the JSON status object from the current printer state. */
String build_status_json() {
  String s = "{";
  s += "\"printing\":"; s += (printing ? "true" : "false");
  s += ",\"paused\":"; s += (print_paused ? "true" : "false");
  s += ",\"folder\":\""; s += jsonEscape(foldersel_long); s += "\"";
  s += ",\"layer\":"; s += current_layer;
  s += ",\"layers\":"; s += layer_counter;
  s += ",\"state\":"; s += current_state;
  s += ",\"eta_h\":"; s += estimated_hours;
  s += ",\"eta_m\":"; s += estimated_minutes;
  s += ",\"wifi\":\""; s += (wifi_is_ap ? "AP" : "STA"); s += "\"";
  s += ",\"ip\":\""; s += wifi_ip; s += "\"";
  s += ",\"ssid\":\""; s += jsonEscape(wifi_is_ap ? AP_SSID : sta_ssid.c_str()); s += "\"";
  s += ",\"heap\":"; s += (ESP.getFreeHeap() / 1024);   // free RAM, KB
  s += ",\"uptime\":"; s += (millis() / 1000);          // seconds since boot
  s += "}";
  return s;
}

/** Scan for nearby WiFi networks and return them as a JSON array (strongest first). */
String build_scan_json() {
  int n = WiFi.scanNetworks();
  String s = "[";
  for (int i = 0; i < n; i++) {
    if (i) s += ",";
    s += "{\"ssid\":\""; s += jsonEscape(WiFi.SSID(i).c_str()); s += "\"";
    s += ",\"rssi\":"; s += WiFi.RSSI(i);
    s += ",\"lock\":"; s += (WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false");
    s += "}";
  }
  s += "]";
  WiFi.scanDelete();
  return s;
}

/** Build the JSON array of printable folders (directories containing 1.png). */
String build_folders_json() {
  String s = "[";
  bool first = true;
  File dir = SD.open("/");
  if (dir) {
    File e;
    while ((e = dir.openNextFile())) {
      if (e.isDirectory()) {
        char nm[101];
        e.getName(nm, 101);
        String probe = String("/") + nm + "/1.png";
        File p = SD.open(probe.c_str());
        bool ok = (bool)p;
        if (p) p.close();
        if (ok) {
          if (!first) s += ",";
          first = false;
          s += "\""; s += jsonEscape(nm); s += "\"";
        }
      }
      e.close();
    }
    dir.close();
  }
  s += "]";
  return s;
}

/**
 * @brief "Processing" step for the web UI: count the layers in a folder and compute the
 *        print estimate WITHOUT starting a print. Mirrors the physical preview (screen111).
 *        Returns JSON {layers, eta_h, eta_m, height, tall}. layers==0 means no slices found.
 */
String build_preview_json(const String &folderArg) {
  String folder = folderArg;
  sanitizeName(folder);
  if (folder.length() == 0) return "{\"layers\":0}";

  strncpy(foldersel_long, folder.c_str(), 100);
  foldersel_long[100] = '\0';

  if (!count_layers_selected()) return "{\"layers\":0}"; // no 1.png / 2.png ... found
  get_motor_updown_time();

  long est = (long)Base_Layer * (long)Base_Exposure;
  long regLayers = (long)layer_counter - (long)Base_Layer;
  if (regLayers < 0) regLayers = 0;
  est += regLayers * (long)Regular_Exposure;
  est += (long)(motor_updown_time * (float)(layer_counter - 1));

  int eh = est / 3600;
  int em = (est % 3600) / 60;
  float height = Layer_Height * (float)layer_counter;

  String s = "{";
  s += "\"layers\":"; s += layer_counter;
  s += ",\"eta_h\":"; s += eh;
  s += ",\"eta_m\":"; s += em;
  s += ",\"height\":\""; s += String(height, 1); s += "\"";
  s += ",\"tall\":"; s += (height > max_height ? "true" : "false");
  s += "}";
  return s;
}

/** Send a simple response with an in-RAM body. */
void sendResponse(WiFiClient &c, int code, const char *status, const char *type, const String &body) {
  c.print("HTTP/1.1 "); c.print(code); c.print(' '); c.print(status); c.print("\r\n");
  c.print("Content-Type: "); c.print(type); c.print("\r\n");
  c.print("Content-Length: "); c.print(body.length()); c.print("\r\n");
  c.print("Connection: close\r\n\r\n");
  c.print(body);
}

/** Stream the PROGMEM HTML page to the client. */
void sendIndexHtml(WiFiClient &c) {
  size_t len = strlen_P(INDEX_HTML);
  c.print("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\nContent-Length: ");
  c.print(len);
  c.print("\r\n\r\n");
  char buf[128];
  size_t i = 0;
  while (i < len) {
    size_t n = (len - i < sizeof(buf)) ? (len - i) : sizeof(buf);
    memcpy_P(buf, INDEX_HTML + i, n);
    c.write((const uint8_t *)buf, n);
    i += n;
  }
}

/** Receive a raw-body file upload (POST /upload?folder=..&name=..) and write it to SD. */
void handleUpload(WiFiClient &c, const String &query, long contentLength) {
  // Refuse immediately during a print: don't touch the SD card and don't drain the
  // body, so a mid-print upload can never stall an exposure or motor move.
  if (printing) {
    sendResponse(c, 503, "Service Unavailable", "text/plain", "Busy printing - upload refused");
    return;
  }

  String folder = urlDecode(getParam(query, "folder"));
  String name   = urlDecode(getParam(query, "name"));
  sanitizeName(folder);
  sanitizeName(name);
  if (folder.length() == 0) folder = "netprint";
  if (name.length() == 0)   name = "1.png";

  String dir = String("/") + folder;
  SD.mkdir(dir.c_str());
  String path = dir + "/" + name;
  File f = SD.open(path.c_str(), O_WRITE | O_CREAT | O_TRUNC);

  uint8_t buf[512];
  long remaining = contentLength;
  unsigned long lastData = millis();
  while (remaining > 0) {
    int avail = c.available();
    if (avail > 0) {
      long want = remaining < (long)sizeof(buf) ? remaining : (long)sizeof(buf);
      int n = c.read(buf, want);
      if (n > 0) {
        if (f) f.write(buf, n);
        remaining -= n;
        lastData = millis();
      }
    } else {
      if (!c.connected() || millis() - lastData > 4000) break; // stalled client
      delay(1);
    }
  }
  if (f) f.close();

  sendResponse(c, 200, "OK", "text/plain", "OK");
}

/** Parse and dispatch a single HTTP request from a connected client. */
void handleHttpClient(WiFiClient &c) {
  c.setTimeout(2000);

  String reqLine = c.readStringUntil('\n');
  reqLine.trim();
  if (reqLine.length() == 0) return;

  int sp1 = reqLine.indexOf(' ');
  int sp2 = reqLine.indexOf(' ', sp1 + 1);
  if (sp1 < 0 || sp2 < 0) return;
  String method = reqLine.substring(0, sp1);
  String target = reqLine.substring(sp1 + 1, sp2);

  String path = target, query = "";
  int q = target.indexOf('?');
  if (q >= 0) { path = target.substring(0, q); query = target.substring(q + 1); }

  // Read headers; capture Content-Length. Blank line terminates the header block.
  long contentLength = 0;
  while (true) {
    String h = c.readStringUntil('\n');
    h.trim();
    if (h.length() == 0) break;
    String hl = h;
    hl.toLowerCase();
    if (hl.startsWith("content-length:"))
      contentLength = h.substring(h.indexOf(':') + 1).toInt();
  }

  if (path == "/upload") {
    handleUpload(c, query, contentLength);
    return;
  }

  // For non-upload requests, consume any body so the socket closes cleanly.
  while (contentLength > 0 && c.available()) { c.read(); contentLength--; }

  if (path == "/") {
    sendIndexHtml(c);
  } else if (path == "/status") {
    sendResponse(c, 200, "OK", "application/json", build_status_json());
  } else if (path == "/list") {
    // Don't scan the SD card mid-print; the page keeps its existing list.
    if (printing) sendResponse(c, 503, "Service Unavailable", "text/plain", "printing");
    else          sendResponse(c, 200, "OK", "application/json", build_folders_json());
  } else if (path == "/preview") {
    // Processing step: count layers + estimate, shown in a modal before Start.
    if (printing) sendResponse(c, 409, "Conflict", "text/plain", "printing");
    else          sendResponse(c, 200, "OK", "application/json",
                               build_preview_json(urlDecode(getParam(query, "folder"))));
  } else if (path == "/scan") {
    // List nearby networks for the WiFi setup page.
    if (printing) sendResponse(c, 409, "Conflict", "text/plain", "printing");
    else          sendResponse(c, 200, "OK", "application/json", build_scan_json());
  } else if (path == "/savewifi") {
    // Save credentials to /wifi.txt on the SD card, then reboot to apply them.
    String ssid = urlDecode(getParam(query, "ssid"));
    String pass = urlDecode(getParam(query, "pass"));
    if (printing) {
      sendResponse(c, 409, "Conflict", "text/plain", "printing");
    } else if (ssid.length() == 0) {
      sendResponse(c, 400, "Bad Request", "text/plain", "missing ssid");
    } else {
      File f = SD.open(WIFI_CONFIG_FILE, O_WRITE | O_CREAT | O_TRUNC);
      if (f) {
        f.println(ssid);
        f.println(pass);
        f.close();
        sendResponse(c, 200, "OK", "application/json", "{\"ok\":true}");
        delay(400);
        ESP.restart(); // reboot so wifi_setup() re-reads and joins the new network
      } else {
        sendResponse(c, 500, "Internal Server Error", "text/plain", "could not write SD");
      }
    }
  } else if (path == "/start") {
    if (printing) {
      sendResponse(c, 409, "Conflict", "text/plain", "already printing");
    } else {
      String f = urlDecode(getParam(query, "folder"));
      sanitizeName(f);
      if (f.length() == 0) {
        sendResponse(c, 400, "Bad Request", "text/plain", "missing folder");
      } else {
        f.toCharArray(net_start_folder, 101);
        net_start_request = true;
        sendResponse(c, 200, "OK", "application/json", "{\"ok\":true}");
      }
    }
  } else if (path == "/stop") {
    net_stop_request = true;
    sendResponse(c, 200, "OK", "application/json", "{\"ok\":true}");
  } else if (path == "/pause") {
    net_pause_request = true;
    sendResponse(c, 200, "OK", "application/json", "{\"ok\":true}");
  } else if (path == "/resume") {
    net_resume_request = true;
    sendResponse(c, 200, "OK", "application/json", "{\"ok\":true}");
  } else if (path == "/delete") {
    if (printing) {
      sendResponse(c, 409, "Conflict", "text/plain", "printing");
    } else {
      String folder = urlDecode(getParam(query, "folder"));
      sanitizeName(folder);
      if (folder.length() == 0) {
        sendResponse(c, 400, "Bad Request", "text/plain", "bad folder");
      } else {
        String base = String("/") + folder;
        while (true) {
          File d = SD.open(base.c_str());
          if (!d) break;
          File e = d.openNextFile();
          if (!e) { d.close(); break; }
          char nm[101];
          e.getName(nm, 101);
          e.close();
          d.close();
          String p = base + "/" + nm;
          SD.remove(p.c_str());
        }
        SD.rmdir(base.c_str());
        sendResponse(c, 200, "OK", "application/json", "{\"ok\":true}");
      }
    }
  } else {
    sendResponse(c, 404, "Not Found", "text/plain", "Not found");
  }
}

/** Service one pending web client, if any. Call this at safe points. */
void web_loop() {
  WiFiClient client = httpd.available();
  if (!client) return;
  handleHttpClient(client);
  client.stop();
}
