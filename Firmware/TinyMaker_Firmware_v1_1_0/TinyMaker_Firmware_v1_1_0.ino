/**
 * @file TinyMaker_Firmware_v1_1_0.ino
 * @author Tinymaker Team & Ciro
 * @version 1.1.0
 * @date 2026-06-10
 * @brief Main firmware for Tinymaker MSLA 3D Printer.
 */

#include <SPI.h>
#include <EEPROM.h>              
#include <AccelStepper.h>        
#include <Arduino_GFX_Library.h> 
#include "FreeSans8pt7b.h"       
#include <PNGdec.h>              
#include <SdFat.h>               

#define BLACK   0x0000
#define BLUE    0x001F
#define RED     0xF800
#define GREEN   0x07E0
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0
#define WHITE   0xFFFF
#define ORANGE  0xFDA0
#define PURPLE  0x8010

const int buttonBack = 33; 
const int buttonUp = 32;   
const int buttonDown = 35; 
const int buttonOK = 34;   
const int end_stop = 26; 
const int mot_en = 13;   
const int mot_step = 12; 
const int mot_dir = 14;  
const int LED = 21;      
const int FAN = 16;      
const int SDCS = 25;     

#define IN1 12
#define IN2 13
#define IN3 14
#define IN4 22

AccelStepper stepper(AccelStepper::HALF4WIRE, IN1, IN3, IN2, IN4);

Arduino_DataBus *bus = new Arduino_ESP32SPI(27, 5, 18, 23, 19, VSPI);
Arduino_GFX *gfx1 = new Arduino_ST7789(bus, -1, 1, true);

Arduino_DataBus *bus2 = new Arduino_ESP32SPI(27, 4, 18, 23, 19, VSPI);
Arduino_GFX *gfx2 = new Arduino_ST7735(bus2, -1, 3, true, 80, 160, 26, 1, 26, 1);

int startTime, Duration, startTime2, Duration2;
int screen = 1;
int counter = 0;
long Position_before_pause;
float total_height, motor_updown_time, motor_updown_time_total;
long estimated_seconds;
byte estimated_hours, estimated_minutes;
int setting_item;
bool setting_item_updown = 1, printing_item_updown = 1;
bool homing_canceled = false, print_paused = false, print_canceled = false;
float steps_mm = 1463, max_height = 68;
int homing_Feedrate = 300;
int current_layer = 0, current_state = 0;

float Layer_Height, Base_Exposure, Regular_Exposure, Transition_Exposure;
byte Base_Layer, Transition_Layer, Slow_Lift_Distance, Fast_Lift_Distance;
int Slow_Lift_Feedrate, Fast_Lift_Feedrate, Drop_Back_Feedrate;
int manual_exposure = 35;

byte Light_Off_Delay, Rest_Before_Lift, UV_Power_Percent, Override_GCODE;

SdFat SD;
char foldersel_long[101];
String foldersel, DirAndFile, FileName;
int layer_counter;
File root;
File myfile;
PNG png;

void parse_gcode_parameters(String folderPath) {
  String fullPath = "/";
  fullPath += folderPath;
  fullPath += "/run.gcode";
  File gcodeFile = SD.open(fullPath); 
  if (!gcodeFile) return;
  while (gcodeFile.available()) {
    String line = gcodeFile.readStringUntil('\n');
    if (line.startsWith(";START_GCODE_BEGIN")) break; 
    if (line.startsWith(";normalExposureTime:")) Regular_Exposure = line.substring(20).toFloat();
    else if (line.startsWith(";bottomLayerExposureTime:")) Base_Exposure = line.substring(25).toFloat();
    else if (line.startsWith(";lightOffTime:")) Light_Off_Delay = line.substring(14).toInt();
  }
  gcodeFile.close();
}

void print_delay(unsigned long ms) {
  unsigned long start_ms = millis();
  while (millis() - start_ms < ms) {
    Duration2 = millis() - startTime2;
    if (Duration2 >= 300) {
      if (digitalRead(buttonUp) == LOW && screen == 1111) { screen1111UP(); startTime2 = millis(); }
      else if (digitalRead(buttonDown) == LOW && screen == 1111) { screen1111DOWN(); startTime2 = millis(); }
      else if (digitalRead(buttonOK) == LOW && screen == 1111) { if (printing_item_updown == 1) screen11111(); else screen11112(); startTime2 = millis(); }
      else if (digitalRead(buttonBack) == LOW && (screen == 11111 || screen == 11112)) { screen1111(); screen1111_state(); startTime2 = millis(); }
      else if (digitalRead(buttonOK) == LOW && screen == 11111) { print_canceled = true; break; }
      else if (digitalRead(buttonOK) == LOW && screen == 11112) { print_paused = true; break; }
    }
    delay(10);
  }
}

void setup() {  
  Serial.begin(115200);
  delay(500);

  pinMode(buttonBack, INPUT); pinMode(buttonUp, INPUT); pinMode(buttonDown, INPUT); pinMode(buttonOK, INPUT);
  pinMode(end_stop, INPUT); pinMode(LED, OUTPUT); pinMode(FAN, OUTPUT);
  digitalWrite(LED, LOW); digitalWrite(FAN, LOW);

  ledcAttach(LED, 5000, 8); 
  ledcWrite(LED, 0); 

  stepper.setMaxSpeed(1200.0); stepper.setAcceleration(2500.0); stepper.disableOutputs();
  
  if (!SD.begin(SDCS, SD_SCK_MHZ(16))) {
    Serial.println("[WARN] SD Card Failed.");
  }
  
  gfx1->begin(); 
  gfx1->fillScreen(BLACK);  
  gfx2->begin(); 
  gfx2->fillScreen(BLACK);
  
  screen0();
  
  EEPROM.begin(24);
  Layer_Height = EEPROM.read(1) / 100.00;
  Base_Exposure = EEPROM.read(2); Regular_Exposure = EEPROM.read(3);
  Base_Layer = EEPROM.read(4); Transition_Layer = EEPROM.read(5);
  Slow_Lift_Distance = EEPROM.read(6); Fast_Lift_Distance = EEPROM.read(7);
  Slow_Lift_Feedrate = EEPROM.read(8); Fast_Lift_Feedrate = EEPROM.read(9);
  Drop_Back_Feedrate = EEPROM.read(10);
  Light_Off_Delay = EEPROM.read(11); Rest_Before_Lift = EEPROM.read(12);
  UV_Power_Percent = EEPROM.read(13); Override_GCODE = EEPROM.read(14);
  
  bool eeprom_needs_commit = false;
  if (Light_Off_Delay == 255) { Light_Off_Delay = 0; EEPROM.write(11, 0); eeprom_needs_commit = true; }
  if (Rest_Before_Lift == 255) { Rest_Before_Lift = 0; EEPROM.write(12, 0); eeprom_needs_commit = true; }
  if (UV_Power_Percent == 255 || UV_Power_Percent == 0) { UV_Power_Percent = 100; EEPROM.write(13, 100); eeprom_needs_commit = true; }
  if (Override_GCODE == 255 || Override_GCODE > 1) { Override_GCODE = 1; EEPROM.write(14, 1); eeprom_needs_commit = true; }
  if (eeprom_needs_commit) EEPROM.commit();
  
  delay(500); 
  screen = 1;
  screen1(); 
}

void loop() {
  if (!root) {
    if (SD.begin(SDCS, SD_SCK_MHZ(16))) {
      root = SD.open("/");
    }
  }

  if (digitalRead(buttonBack) == LOW) {
    switch (screen) {
      case 11: screen1(); break;
      case 111: screen11(); counter --; folderDown(root); break;
      case 112: screen11(); counter --; folderDown(root); break;      
      case 12: screen1(); break;
      case 21: screen1(); screen2(); break;
      case 22: screen1(); screen2(); break;
      case 23: screen1(); screen2(); break;
      case 211: screen21(); break;
      case 212: screen21(); break;
      case 213:
      gfx2->fillRoundRect(2, 20, 156, 56, 3, BLACK); 
      gfx2->setTextColor(WHITE); gfx2->setCursor(46, 43); gfx2->print("Canceled"); delay(600);
      gfx2->setTextColor(BLACK); gfx2->setCursor(46, 43); gfx2->print("Canceled"); delay(600);
      gfx2->setTextColor(WHITE); gfx2->setCursor(46, 43); gfx2->print("Canceled"); delay(600);
      gfx2->setTextColor(BLACK); gfx2->setCursor(46, 43); gfx2->print("Canceled"); delay(600);
      gfx2->setTextColor(WHITE); gfx2->setCursor(46, 43); gfx2->print("Canceled"); delay(600);
      screen21(); 
        break;
      case 221: screen21(); screen22(); break;
      case 222: screen21(); screen22(); break;        
      case 223: screen21(); screen22(); break; 
      case 2211: screen221(); break;
      case 2221: screen221(); screen222(); break;        
      case 2231: screen221(); screen223(); break; 
      case 231: screen21(); screen23(); break;
      case 2311: screen21(); screen23(); break;
      case 31: screen1(); screen3(); break; 
      case 311:
      if(setting_item_updown == 1){ setting_item ++; screen31UP(); }
      if(setting_item_updown == 0){ setting_item --; screen31DOWN(); }
      delay(300);
        break; 
      case 3111:
      Layer_Height = EEPROM.read(1) / 100.00;
      Base_Exposure = EEPROM.read(2); Regular_Exposure = EEPROM.read(3);
      Base_Layer = EEPROM.read(4); Transition_Layer = EEPROM.read(5);
      Slow_Lift_Distance = EEPROM.read(6); Fast_Lift_Distance = EEPROM.read(7);
      Slow_Lift_Feedrate = EEPROM.read(8); Fast_Lift_Feedrate = EEPROM.read(9);
      Drop_Back_Feedrate = EEPROM.read(10); Light_Off_Delay = EEPROM.read(11);
      Rest_Before_Lift = EEPROM.read(12); UV_Power_Percent = EEPROM.read(13);
      Override_GCODE = EEPROM.read(14);
      if(setting_item_updown == 1){ setting_item ++; screen31UP(); }
      if(setting_item_updown == 0){ setting_item --; screen31DOWN(); } 
        break;
    }
    delay(200);
  }

  if (digitalRead(buttonUp) == LOW) {
    switch (screen) {
      case 2: screen1(); break;
      case 3: screen2(); break;
      case 11: 
        if (counter > 0) { folderUp(root); }
        break;
      case 22: screen21(); break;
      case 23: screen22(); break;
      case 222: screen221(); break;
      case 223: screen222(); break; 
      case 2211: manual_lift(); break;
      case 2221: manual_lift(); break;
      case 2231: manual_lift(); break;      
      case 2311: screen2311increase(); break;            
      case 31: screen31UP(); break;
      case 311: screen3111increase(); break;
      case 3111: screen3111increase(); break;
    }
    delay(200);
  }  

  if (digitalRead(buttonDown) == LOW) {
    switch (screen) {
      case 1: screen2(); break;
      case 2: screen3(); break;
      case 11: folderDown(root); break;
      case 21: screen22(); break;
      case 22: screen23(); break;
      case 221: screen222(); break;
      case 222: screen223(); break;
      case 2211: manual_down(); break;
      case 2221: manual_down(); break;
      case 2231: manual_down(); break;
      case 2311: screen2311decrease(); break;                    
      case 31: screen31DOWN(); break;
      case 311: screen3111decrease(); break;
      case 3111: screen3111decrease(); break;
    }
    delay(200);
  }  

  if (digitalRead(buttonOK) == LOW) {
    switch (screen) {
      case 1:
      if (SD.begin(SDCS, SD_SCK_MHZ(16))){
        root = SD.open("/"); screen11(); counter = 0; folderDown(root);                        
      } else { screen12(); }
        break;
      case 11: {
      gfx2->fillScreen(BLACK); gfx2->fillRoundRect(0, 0, 160, 80, 5, ORANGE); gfx2->fillRoundRect(2, 2, 156, 76, 3, BLACK); 
      gfx2->setFont(&FreeSans8pt7b); gfx2->setTextColor(WHITE); gfx2->setTextSize(1);
      gfx2->setCursor(22, 34); gfx2->print("Processing files"); gfx2->setCursor(34, 52); gfx2->print("Please wait...");
      delay(500); 
 
      layer_counter = 0; File entry;
      do { layer_counter += 100; FileName = "/"; FileName += foldersel_long; FileName += "/"; FileName += layer_counter; FileName += ".png"; entry = SD.open(FileName); } while(entry);
      layer_counter -= 100;

      do { layer_counter++; FileName = "/"; FileName += foldersel_long; FileName += "/"; FileName += layer_counter; FileName += ".png"; entry = SD.open(FileName); } while(entry);
      layer_counter --; 

      if (layer_counter <= 1080) { screen111(); } else { screen112(); }
      } break;

      case 111: {
        homing_canceled = false; print_paused = false; print_canceled = false;
        current_state = 0; current_layer = 0; Position_before_pause = 0; Transition_Exposure = Base_Exposure;
        screen1111();
        gfx2->fillRect(136, 52, 6, 16, 0x8410); gfx2->fillRect(146, 52, 6, 16, 0x8410);        
        screen1111_state(); screen1111UP(); delay(500);

        stepper.setCurrentPosition(0); stepper.setMaxSpeed(Drop_Back_Feedrate * steps_mm / 60); stepper.enableOutputs();
        long initial_homing = 0; long current_position;
        while(!digitalRead(end_stop)){
          stepper.moveTo(initial_homing); initial_homing--; stepper.run(); current_position = stepper.currentPosition();
          if (current_position < -106799){
            stepper.disableOutputs(); homing_canceled = true;
            gfx2->fillRoundRect(5, 5, 150, 70, 7, BLACK); gfx2->fillRoundRect(7, 7, 146, 66, 5, RED); gfx2->fillRoundRect(9, 9, 142, 62, 3, BLACK);
            gfx2->fillRoundRect(16, 11, 5, 10, 1, RED); gfx2->fillCircle(18, 25, 2, RED); gfx2->setTextColor(WHITE); gfx2->setTextSize(1);
            gfx2->setCursor(27, 23); gfx2->println("Homing error,"); gfx2->setCursor(13, 41); gfx2->println("print canceled."); 
            gfx2->fillRoundRect(82, 51, 67, 18, 2,  0x879F); gfx2->setCursor(100, 64); gfx2->println("OK :(");
            while(digitalRead(buttonOK) == HIGH);
            break;  
          }
          if (Duration >= 500 && screen == 1111 && digitalRead(buttonOK) == LOW) { screen11111(); startTime = millis(); }
          Duration = millis()-startTime;
          if (Duration >= 500 && screen == 11111 && digitalRead(buttonOK) == LOW){ stepper.disableOutputs(); homing_canceled = true; break; }
          if (Duration >= 500 && screen == 11111 && digitalRead(buttonBack) == LOW){
            screen1111(); gfx2->fillRect(136, 52, 6, 16, 0x8410); gfx2->fillRect(146, 52, 6, 16, 0x8410);            
            screen1111_state(); screen1111UP();
          }
        }
        delay(50);
          
        if (homing_canceled != true){
          stepper.disableOutputs(); stepper.setCurrentPosition(0); digitalWrite(FAN, HIGH);
          if (screen != 11111){ gfx2->fillRect(136, 52, 6, 16, YELLOW); gfx2->fillRect(146, 52, 6, 16, YELLOW); }
        }
          
        if (Override_GCODE == 0) { parse_gcode_parameters(foldersel_long); } 
        else { Regular_Exposure = EEPROM.read(3); Base_Exposure = EEPROM.read(2); Light_Off_Delay = EEPROM.read(11); }
      
        while(!homing_canceled && !print_canceled){            
          current_layer++; // FIX: Layer incremented first to sync UI and Mask

          estimated_seconds = 0; estimated_hours = 0; estimated_minutes = 0; motor_updown_time_total = 0;
          if (current_layer <= Base_Layer) estimated_seconds += (Base_Layer - current_layer + 1) * Base_Exposure;                
          estimated_seconds += (layer_counter - current_layer + 1) * Regular_Exposure;            
          motor_updown_time_total += (layer_counter - current_layer) * motor_updown_time;            
          estimated_seconds += motor_updown_time_total;             
          estimated_hours = estimated_seconds / 3600; estimated_minutes = (estimated_seconds % 3600) / 60;
                        
          // FIX: UI is explicitly drawn BEFORE PNG masking to eliminate SPI collisions
          if (screen != 11111 && screen != 11112){                
            gfx2->fillRoundRect(2, 38, 116, 40, 3, BLACK); gfx2->setFont(&FreeSans8pt7b); gfx2->setTextColor(WHITE); gfx2->setTextSize(1);    
            gfx2->setCursor(6, 54); gfx2->print(current_layer); gfx2->print(" / "); gfx2->print(layer_counter);
            gfx2->setCursor(6, 74); gfx2->print(estimated_hours); gfx2->print("h "); gfx2->print(estimated_minutes); gfx2->print("min");
          }
          
          print_next_png();
            
          if (Light_Off_Delay > 0) {
            current_state = 9; 
            screen1111_state();
            startTime2 = millis(); 
            print_delay(Light_Off_Delay * 1000); 
          }

          if (current_state != 4 && current_state != 5){
            current_state = 1; 
            screen1111_state();
          }
                  
          float current_exposure_time = Regular_Exposure;
          if (current_layer <= Base_Layer) {
              current_exposure_time = Base_Exposure; Transition_Exposure = Base_Exposure;
          } else if (current_layer > Base_Layer && current_layer <= (Base_Layer + Transition_Layer)) {
              Transition_Exposure -= (Base_Exposure - Regular_Exposure) / (float)Transition_Layer; current_exposure_time = Transition_Exposure;
          }

          int pwm_val = map(UV_Power_Percent, 0, 100, 0, 255);
          ledcWrite(LED, pwm_val); 
          
          startTime2 = millis();
          print_delay(current_exposure_time * 1000);
          
          ledcWrite(LED, 0);
          digitalWrite(LED, LOW);
          
          gfx1->fillScreen(BLACK);
          
          if (Rest_Before_Lift > 0) {
            if (!print_paused && !print_canceled) { current_state = 9; screen1111_state(); }
            startTime2 = millis();
            print_delay(Rest_Before_Lift * 1000);
          }

          if (current_state != 4 && current_state != 5){
            current_state = 2;
            screen1111_state();
          }
          lift_print();
          delay(50);
          
          if(current_layer >= layer_counter) break;
                    
          if(print_paused == true){
            Position_before_pause = stepper.currentPosition();
            stepper.setMaxSpeed(Fast_Lift_Feedrate * steps_mm / 60); stepper.enableOutputs();
            if (Position_before_pause + (20 * steps_mm) <= max_height * steps_mm) stepper.move(20 * steps_mm);
            else stepper.moveTo(max_height * steps_mm);  
            while (stepper.distanceToGo()!= 0) stepper.run();
            stepper.disableOutputs(); delay(10); 

            current_state = 6; screen1111_state();
            gfx2->fillRect(136, 12, 16, 16, RED); gfx2->fillTriangle(136, 52, 136, 68, 152, 60, GREEN);            
            screen1111DOWN();
              
            while(print_paused == true){
              Duration2 = millis()-startTime2;
              if (Duration2 >= 500 && digitalRead(buttonUp) == LOW && screen == 1112){ screen1111UP(); Duration2 = 0; startTime2 = millis(); }
              if (Duration2 >= 500 && digitalRead(buttonDown) == LOW && screen == 1112){ screen1111DOWN(); Duration2 = 0; startTime2 = millis(); }
              if (Duration2 >= 500 && digitalRead(buttonOK) == LOW && printing_item_updown == 1 && screen != 11111){ screen11111(); Duration2 = 0; startTime2 = millis(); }
              if (Duration2 >= 500 && digitalRead(buttonOK) == LOW && printing_item_updown == 0 && screen != 11113){ screen11113(); Duration2 = 0; startTime2 = millis(); }
              if (Duration2 >= 500 && digitalRead(buttonBack) == LOW && screen == 11111){ screen1111(); screen1111_state(); screen1112(); screen1111UP(); Duration2 = 0; startTime2 = millis(); }
              if (Duration2 >= 500 && digitalRead(buttonBack) == LOW && screen == 11113){ screen1111(); screen1111_state(); screen1112(); screen1111DOWN(); Duration2 = 0; startTime2 = millis(); }
              if (Duration2 >= 500 && digitalRead(buttonOK) == LOW && screen == 11111){
                screen1111(); current_state = 4; screen1111_state(); screen1111UP(); print_canceled = true; print_paused = false;
              }  
              if (Duration2 >= 500 && digitalRead(buttonOK) == LOW && screen == 11113){
                screen1111(); current_state = 7; screen1111_state();           
                gfx2->fillRect(136, 12, 16, 16, 0x8410); gfx2->fillRect(136, 52, 6, 16, 0x8410); gfx2->fillRect(146, 52, 6, 16, 0x8410); gfx2->drawRoundRect(128, 44, 32, 32, 3, 0x8410);
                stepper.setMaxSpeed(Fast_Lift_Feedrate * steps_mm / 60); stepper.enableOutputs(); stepper.moveTo(Position_before_pause);  
                while (stepper.distanceToGo()!= 0) stepper.run();
                stepper.disableOutputs(); delay(10);
                gfx2->fillRect(136, 12, 16, 16, RED); gfx2->fillRect(136, 52, 6, 16, YELLOW); gfx2->fillRect(146, 52, 6, 16, YELLOW); gfx2->drawRoundRect(128, 44, 32, 32, 3, WHITE);
                print_paused = false;    
              }       
            }                     
          }
          
          if (!print_canceled){ current_state = 3; screen1111_state(); lower_print(); }           
        } 
        if (!homing_canceled){
          if (!print_canceled){
            current_state = 8; screen1111_state();
            gfx2->fillRect(136, 12, 16, 16, 0x8410); gfx2->fillRect(136, 52, 6, 16, 0x8410); gfx2->fillRect(146, 52, 6, 16, 0x8410);
            if(printing_item_updown == 1) gfx2->drawRoundRect(128, 4, 32, 32, 3, 0x8410);
            if(printing_item_updown == 0) gfx2->drawRoundRect(128, 44, 32, 32, 3, 0x8410);            
          } 
          lift_finished_print();
        }
        digitalWrite(FAN, LOW); screen1(); 
      } break;
      
      case 12:
      if (SD.begin(SDCS, SD_SCK_MHZ(16))){ root = SD.open("/"); screen11(); counter = 0; folderDown(root); } else { screen12(); }
        break;
      case 2: screen21(); break;
      case 21: screen211(); break;
      case 211: screen212(); break;
      case 212: screen213(); break;
      case 213: screen214(); break;
      case 22: screen221(); break;
      case 221: screen2211(); break;
      case 222: screen2221(); break;
      case 223: screen2231(); break; 
      case 23: screen231(); break;
      case 231: screen2311(); break; 
      case 2311: screen23111(); break;        
      case 3: setting_item = 1; screen31UP(); break;
      case 31: screen311(); break;
      case 311:
      if(setting_item_updown == 1){ setting_item ++; screen31UP(); }
      if(setting_item_updown == 0){ setting_item --; screen31DOWN(); }
        break; 
      case 3111:
      EEPROM.write(1, Layer_Height*100); EEPROM.write(2, Base_Exposure); EEPROM.write(3, Regular_Exposure);
      EEPROM.write(4, Base_Layer); EEPROM.write(5, Transition_Layer); EEPROM.write(6, Slow_Lift_Distance);
      EEPROM.write(7, Fast_Lift_Distance); EEPROM.write(8, Slow_Lift_Feedrate); EEPROM.write(9, Fast_Lift_Feedrate);
      EEPROM.write(10, Drop_Back_Feedrate); EEPROM.write(11, Light_Off_Delay); EEPROM.write(12, Rest_Before_Lift);
      EEPROM.write(13, UV_Power_Percent); EEPROM.write(14, Override_GCODE); EEPROM.commit(); 
      if(setting_item_updown == 1){ setting_item ++; screen31UP(); }
      if(setting_item_updown == 0){ setting_item --; screen31DOWN(); } 
        break;
    }
    delay(200);
  } 
}