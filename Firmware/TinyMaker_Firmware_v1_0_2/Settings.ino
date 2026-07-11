/**
 * @file Settings.ino
 * @brief Print-settings persistence.
 *
 * The printer keeps its print parameters (layer height, exposures, layer counts, lift
 * distances/feedrates) in EEPROM. EEPROM lives in a flash partition, which a normal
 * `write-flash` does not touch - but a full chip erase (or the Arduino IDE "Erase All
 * Flash" option) wipes it, forcing a manual "Back to Default" after every such flash.
 *
 * To make settings truly durable, we ALSO keep them in a plain text file on the SD card
 * (/settings.txt). The SD card survives any flash. On boot we load the SD file if present
 * (and mirror it into EEPROM so the on-printer menu matches); otherwise we fall back to a
 * valid EEPROM, else to defaults, and seed the SD file. Any settings change - from the web
 * page OR the physical menu - is written to both EEPROM and the SD file.
 */

// Built-in defaults (same values as the menu's "Back to Default").
void apply_default_settings() {
  Layer_Height       = 0.10;
  Base_Exposure      = 35;
  Regular_Exposure   = 14;
  Base_Layer         = 2;
  Transition_Layer   = 5;
  Slow_Lift_Distance = 1;
  Fast_Lift_Distance = 2;
  Slow_Lift_Feedrate = 40;
  Fast_Lift_Feedrate = 50;
  Drop_Back_Feedrate = 50;
}

/** Keep values within safe ranges (feedrates/exposures must be > 0 or the printer stalls). */
void clamp_settings() {
  if (Layer_Height < 0.01) Layer_Height = 0.05;
  if (Layer_Height > 2.55) Layer_Height = 2.55;
  if (Base_Exposure < 1)    Base_Exposure = 1;
  if (Base_Exposure > 255)  Base_Exposure = 255;
  if (Regular_Exposure < 1) Regular_Exposure = 1;
  if (Regular_Exposure > 255) Regular_Exposure = 255;
  if (Slow_Lift_Feedrate < 1) Slow_Lift_Feedrate = 1;
  if (Slow_Lift_Feedrate > 255) Slow_Lift_Feedrate = 255;
  if (Fast_Lift_Feedrate < 1) Fast_Lift_Feedrate = 1;
  if (Fast_Lift_Feedrate > 255) Fast_Lift_Feedrate = 255;
  if (Drop_Back_Feedrate < 1) Drop_Back_Feedrate = 1;
  if (Drop_Back_Feedrate > 255) Drop_Back_Feedrate = 255;
}

/** True if the current settings look like real values (not blank/erased EEPROM). */
bool settings_look_valid() {
  return Base_Exposure    >= 1 && Base_Exposure    <= 250
      && Regular_Exposure >= 1 && Regular_Exposure <= 250
      && Layer_Height     >= 0.01 && Layer_Height  <= 2.55
      && Drop_Back_Feedrate >= 1;
}

void load_settings_from_eeprom() {
  Layer_Height       = EEPROM.read(1) / 100.00;
  Base_Exposure      = EEPROM.read(2);
  Regular_Exposure   = EEPROM.read(3);
  Base_Layer         = EEPROM.read(4);
  Transition_Layer   = EEPROM.read(5);
  Slow_Lift_Distance = EEPROM.read(6);
  Fast_Lift_Distance = EEPROM.read(7);
  Slow_Lift_Feedrate = EEPROM.read(8);
  Fast_Lift_Feedrate = EEPROM.read(9);
  Drop_Back_Feedrate = EEPROM.read(10);
}

void write_settings_to_eeprom() {
  EEPROM.write(1, (int)(Layer_Height * 100));
  EEPROM.write(2, Base_Exposure);
  EEPROM.write(3, Regular_Exposure);
  EEPROM.write(4, Base_Layer);
  EEPROM.write(5, Transition_Layer);
  EEPROM.write(6, Slow_Lift_Distance);
  EEPROM.write(7, Fast_Lift_Distance);
  EEPROM.write(8, Slow_Lift_Feedrate);
  EEPROM.write(9, Fast_Lift_Feedrate);
  EEPROM.write(10, Drop_Back_Feedrate);
  EEPROM.commit();
}

/** Write the current settings to /settings.txt on the SD card (key=value per line). */
void save_settings_to_sd() {
  File f = SD.open(SETTINGS_FILE, O_WRITE | O_CREAT | O_TRUNC);
  if (!f) return;
  f.print("layer_height=");       f.println(Layer_Height, 2);
  f.print("base_exposure=");      f.println(Base_Exposure);
  f.print("regular_exposure=");   f.println(Regular_Exposure);
  f.print("base_layers=");        f.println(Base_Layer);
  f.print("transition_layers=");  f.println(Transition_Layer);
  f.print("slow_lift_distance="); f.println(Slow_Lift_Distance);
  f.print("fast_lift_distance="); f.println(Fast_Lift_Distance);
  f.print("slow_lift_feedrate="); f.println(Slow_Lift_Feedrate);
  f.print("fast_lift_feedrate="); f.println(Fast_Lift_Feedrate);
  f.print("drop_back_feedrate="); f.println(Drop_Back_Feedrate);
  f.close();
}

/** Load settings from /settings.txt into the globals. Returns false if the file is absent. */
bool load_settings_from_sd() {
  File f = SD.open(SETTINGS_FILE);
  if (!f) return false;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    int eq = line.indexOf('=');
    if (eq <= 0) continue;
    String key = line.substring(0, eq);
    String val = line.substring(eq + 1);
    val.trim();
    if      (key == "layer_height")       Layer_Height       = val.toFloat();
    else if (key == "base_exposure")      Base_Exposure      = val.toInt();
    else if (key == "regular_exposure")   Regular_Exposure   = val.toInt();
    else if (key == "base_layers")        Base_Layer         = val.toInt();
    else if (key == "transition_layers")  Transition_Layer   = val.toInt();
    else if (key == "slow_lift_distance") Slow_Lift_Distance = val.toInt();
    else if (key == "fast_lift_distance") Fast_Lift_Distance = val.toInt();
    else if (key == "slow_lift_feedrate") Slow_Lift_Feedrate = val.toInt();
    else if (key == "fast_lift_feedrate") Fast_Lift_Feedrate = val.toInt();
    else if (key == "drop_back_feedrate") Drop_Back_Feedrate = val.toInt();
  }
  f.close();
  return true;
}

/** Persist the current settings to BOTH stores (call after any change). */
void persist_settings() {
  clamp_settings();
  write_settings_to_eeprom();
  save_settings_to_sd();
}

/** Boot-time load. Called from setup() after SD.begin(). */
void init_settings() {
  EEPROM.begin(24);
  apply_default_settings();          // sane starting point

  if (load_settings_from_sd()) {
    // SD file is the durable source of truth -> mirror into EEPROM for the menu.
    clamp_settings();
    write_settings_to_eeprom();
  } else {
    // No SD file yet: use EEPROM if it's valid, otherwise defaults; then seed the file.
    load_settings_from_eeprom();
    if (!settings_look_valid()) apply_default_settings();
    clamp_settings();
    write_settings_to_eeprom();
    save_settings_to_sd();
  }
}

// ===================================================================================
// Web helpers
// ===================================================================================

/** Current settings as JSON for the web UI. */
String build_settings_json() {
  String s = "{";
  s += "\"layer_height\":";       s += String(Layer_Height, 2);
  s += ",\"base_exposure\":";     s += Base_Exposure;
  s += ",\"regular_exposure\":";  s += Regular_Exposure;
  s += ",\"base_layers\":";       s += Base_Layer;
  s += ",\"transition_layers\":"; s += Transition_Layer;
  s += ",\"slow_lift_distance\":"; s += Slow_Lift_Distance;
  s += ",\"fast_lift_distance\":"; s += Fast_Lift_Distance;
  s += ",\"slow_lift_feedrate\":"; s += Slow_Lift_Feedrate;
  s += ",\"fast_lift_feedrate\":"; s += Fast_Lift_Feedrate;
  s += ",\"drop_back_feedrate\":"; s += Drop_Back_Feedrate;
  s += "}";
  return s;
}

/** Apply settings from a web query string (only keys that are present), then persist. */
void apply_settings_from_query(const String &query) {
  String v;
  v = getParam(query, "layer_height");       if (v.length()) Layer_Height       = v.toFloat();
  v = getParam(query, "base_exposure");      if (v.length()) Base_Exposure      = v.toInt();
  v = getParam(query, "regular_exposure");   if (v.length()) Regular_Exposure   = v.toInt();
  v = getParam(query, "base_layers");        if (v.length()) Base_Layer         = v.toInt();
  v = getParam(query, "transition_layers");  if (v.length()) Transition_Layer   = v.toInt();
  v = getParam(query, "slow_lift_distance"); if (v.length()) Slow_Lift_Distance = v.toInt();
  v = getParam(query, "fast_lift_distance"); if (v.length()) Fast_Lift_Distance = v.toInt();
  v = getParam(query, "slow_lift_feedrate"); if (v.length()) Slow_Lift_Feedrate = v.toInt();
  v = getParam(query, "fast_lift_feedrate"); if (v.length()) Fast_Lift_Feedrate = v.toInt();
  v = getParam(query, "drop_back_feedrate"); if (v.length()) Drop_Back_Feedrate = v.toInt();
  persist_settings();
}
