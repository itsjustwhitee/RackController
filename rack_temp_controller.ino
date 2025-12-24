#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFi.h>
#include <WebServer.h>
#include "time.h"

// ================= USER CONFIGURATION =================
const char* ssid     = "YOUR_WIFI_SSID";        // <--- ENTER WIFI NAME
const char* password = "YOUR_WIFI_PASSWORD";    // <--- ENTER WIFI PASSWORD

// --- HARDWARE PINS ---
#define PIN_PWM_FAN      37  // Blue wire from Fan
#define PIN_TEMP_SENSOR  4   // Data wire from DS18B20
#define PIN_SDA_OLED     1   // SDA Display
#define PIN_SCL_OLED     2   // SCL Display

// --- OLED ADDRESS ---
#define OLED_ADDRESS 0x3C

// --- TEMPERATURE THRESHOLDS (Celsius) ---
const float TEMP_OFF        = 30.0; // Below 30°C: Fans OFF
const float TEMP_SILENT     = 35.0; // 30-35°C: Silent Speed (10%)
const float TEMP_RAMP_START = 40.0; // 35-40°C: Silent Speed (10%)
const float TEMP_MAX        = 55.0; // Above 55°C: 100% Speed
const float TEMP_CRITIC     = 65.0; // Emergency: Above this, ignore Night Mode

// --- PWM SETTINGS (0-255) ---
const int PWM_SILENT     = 26;  // ~10% duty cycle (Minimum startup for Arctic P14)
const int PWM_RAMP_START = 40;  // ~15% duty cycle (Ramp start point)
const int NIGHT_MAX_PWM  = 102; // ~40% duty cycle (Max speed during Night Mode)

// --- NIGHT MODE SCHEDULE ---
const int NIGHT_START_HOUR = 23; // 11:00 PM
const int NIGHT_END_HOUR   = 7;  // 07:00 AM

// =========================================================

#define SCREEN_WIDTH 128 
#define SCREEN_HEIGHT 64 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
OneWire oneWire(PIN_TEMP_SENSOR);
DallasTemperature sensors(&oneWire);
WebServer server(80);

// STATE VARIABLES
float currentTemp = 0.0;
int currentPWM = 0;
int currentPercent = 0;
String mode = "AUTO"; 
bool wifiConnected = false;
bool isNight = false;

// OFFSET & TIMER VARIABLES
int userOffsetPercent = 0;          
unsigned long overrideEndTime = 0;  
// Default FALSE = 0dB Zone ACTIVE (Fans stay off if cold, ignoring positive offset)
// TRUE = Override allowed (Fans start immediately with offset)
bool allowZeroOverride = false;      

// TIME & PWM CONFIG
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;      // GMT+1
const int   daylightOffset_sec = 3600; // Daylight saving
const int pwmFreq = 25000;             // 25kHz standard for PC Fans
const int pwmResolution = 8;           // 8-bit resolution

// Helper Function: Set OLED Contrast (Fixes dim() turning off display on some modules)
void setContrast(int contrast) {
  if (contrast < 1) contrast = 1;
  if (contrast > 255) contrast = 255;
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(contrast);
}

// --- WEB INTERFACE HANDLER ---
void handleRoot() {
  long timeLeftMin = 0;
  if (overrideEndTime > millis()) {
    timeLeftMin = (overrideEndTime - millis()) / 60000;
  } else if (overrideEndTime != 0) {
    // Timer expired, reset offset
    userOffsetPercent = 0;
    overrideEndTime = 0;
  }

  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  
  // --- CSS STYLES ---
  html += "<style>";
  html += "body { font-family: 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; background-color: #121212; color: #e0e0e0; margin: 0; padding: 20px; text-align: center; padding-bottom: 60px; }";
  html += ".container { max-width: 500px; margin: 0 auto; }";
  html += "h1 { font-size: 1.2rem; color: #666; text-transform: uppercase; letter-spacing: 2px; margin-bottom: 20px; }";
  
  // Cards
  html += ".card { background-color: #1e1e1e; border-radius: 15px; padding: 20px; margin-bottom: 20px; box-shadow: 0 4px 6px rgba(0,0,0,0.2); }";
  html += ".card-title { font-size: 0.9rem; color: #aaa; margin-bottom: 10px; display: block; font-weight: 600; text-transform: uppercase; letter-spacing: 0.5px; }";
  
  // Dashboard Grid
  html += ".dashboard { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; }";
  html += ".stat-value { font-size: 2.2rem; font-weight: bold; color: #fff; }";
  html += ".stat-unit { font-size: 1rem; color: #aaa; font-weight: normal; }";
  html += ".stat-sub { font-size: 0.8rem; color: #666; margin-top: 5px; }";

  // Controls
  html += ".control-row { display: flex; align-items: center; justify-content: space-between; background: #252525; border-radius: 12px; padding: 5px; margin-top: 10px; }";
  html += ".btn-ctrl { width: 50px; height: 50px; border: none; border-radius: 10px; font-size: 1.5rem; font-weight: bold; cursor: pointer; color: white; transition: background 0.2s; }";
  html += ".val-display { font-size: 1.2rem; font-weight: bold; width: 100px; }";
  html += ".timer-badge { background: #FF9800; color: #000; padding: 2px 8px; border-radius: 4px; font-size: 0.8rem; font-weight: bold; margin-left: 5px; vertical-align: middle; }";

  // Colors
  html += ".bg-blue { background-color: #2196F3; } .bg-blue:active { background-color: #1976D2; }";
  html += ".bg-red { background-color: #F44336; } .bg-red:active { background-color: #D32F2F; }";
  html += ".bg-grey { background-color: #424242; } .bg-grey:active { background-color: #333; }";
  html += ".bg-green { background-color: #4CAF50; } .bg-green:active { background-color: #388E3C; }";
  html += ".bg-orange { background-color: #FF9800; color: black; } .bg-orange:active { background-color: #F57C00; }";

  // Wide Buttons
  html += ".btn-block { width: 100%; padding: 12px; border: none; border-radius: 8px; font-size: 0.9rem; font-weight: bold; color: white; cursor: pointer; margin-top: 5px; }";
  
  // Master Buttons (Column layout)
  html += ".master-col { display: flex; flex-direction: column; gap: 12px; margin-top: 10px; }";
  html += ".btn-master { width: 100%; padding: 15px; border: none; border-radius: 10px; font-size: 1rem; font-weight: bold; color: white; cursor: pointer; text-align: center; }";
  
  html += ".active-mode { border: 2px solid #fff; box-shadow: 0 0 10px rgba(255,255,255,0.2); }";

  html += "</style>";
  // --- END CSS ---

  html += "</head><body>";
  html += "<div class='container'>";
  html += "<h1>Rack Control</h1>";

  // --- DASHBOARD ---
  html += "<div class='dashboard'>";
  
  // Temp Card
  html += "<div class='card'>";
  html += "<span class='card-title'>Temperatura</span>"; // "Temperature"
  html += "<div class='stat-value'>" + String(currentTemp, 1) + "<span class='stat-unit'>&deg;C</span></div>";
  html += "<div class='stat-sub'>";
  html += (isNight ? "Notte &#127769;" : "Giorno &#9728;"); // "Night" : "Day"
  html += "</div></div>";

  // Fan Card
  html += "<div class='card'>";
  html += "<span class='card-title'>Ventole</span>"; // "Fans"
  html += "<div class='stat-value'>" + String(currentPercent) + "<span class='stat-unit'>%</span></div>";
  html += "<div class='stat-sub'>" + mode + "</div>";
  html += "</div>";
  
  html += "</div>";

  // --- SPEED CONTROL ---
  html += "<div class='card'>";
  html += "<span class='card-title'>Velocit&agrave;</span>"; // "Speed"
  
  html += "<div class='control-row'>";
  html += "<a href='/speed_sub'><button class='btn-ctrl bg-blue'>-</button></a>";
  html += "<div class='val-display'>";
  if(userOffsetPercent > 0) html += "+";
  html += String(userOffsetPercent) + "%";
  html += "</div>";
  html += "<a href='/speed_add'><button class='btn-ctrl bg-red'>+</button></a>";
  html += "</div>";
  
  // 0dB Toggle
  html += "<br><span class='card-title' style='margin-top:10px;'>0 dB Zone</span>";
  if(!allowZeroOverride) {
    // 0dB Active (Respect silence) -> GREEN / ON
    html += "<a href='/toggle_zero'><button class='btn-block bg-green'>ON</button></a>";
  } else {
    // 0dB Inactive (Override allowed) -> GREY / OFF
    html += "<a href='/toggle_zero'><button class='btn-block bg-grey'>OFF</button></a>";
  }
  html += "</div>";

  // --- TIMER ---
  html += "<div class='card'>";
  html += "<span class='card-title'>Timer";
  if(timeLeftMin > 0) html += "<span class='timer-badge'>" + String(timeLeftMin) + "m</span>";
  html += "</span>";

  html += "<div class='control-row'>";
  html += "<a href='/time_sub'><button class='btn-ctrl bg-grey'>-</button></a>";
  html += "<div class='val-display'>";
  if(timeLeftMin > 0) {
     int h = timeLeftMin / 60;
     int m = timeLeftMin % 60;
     html += String(h)+"h " + String(m)+"m";
  } else {
     html += "OFF";
  }
  html += "</div>";
  html += "<a href='/time_add'><button class='btn-ctrl bg-grey'>+</button></a>";
  html += "</div>";
  html += "</div>"; 

  // --- MASTER MODE ---
  html += "<div class='card' style='border:1px solid #333;'>";
  html += "<span class='card-title'>Modalit&agrave;</span>"; // "Mode"
  
  html += "<div class='master-col'>";
  String styleAuto = "bg-blue"; if(mode == "AUTO") styleAuto += " active-mode";
  html += "<a href='/auto'><button class='btn-master " + styleAuto + "'>AUTO</button></a>";

  String styleOff = "bg-grey"; if(mode == "OFF") styleOff += " active-mode";
  html += "<a href='/off'><button class='btn-master " + styleOff + "'>SPENTO</button></a>"; // "OFF"

  String styleMax = "bg-red"; if(mode == "MAX") styleMax += " active-mode";
  html += "<a href='/max'><button class='btn-master " + styleMax + "'>MAX (100%)</button></a>";
  html += "</div></div>";

  // --- GLOBAL RESET BUTTON ---
  if(userOffsetPercent != 0 || timeLeftMin > 0 || mode != "AUTO") {
    html += "<div style='margin-top: 30px;'>";
    html += "<a href='/reset'><button class='btn-block bg-orange' style='padding: 15px; font-size: 1rem;'>RESET</button></a>";
    html += "</div>";
  }

  html += "</div></body></html>";
  server.send(200, "text/html", html);
}

// --- COMMAND HANDLERS ---
void handleSpeedSub() { 
  if(mode=="AUTO") {
    userOffsetPercent -= 5; 
    if(overrideEndTime == 0) overrideEndTime = millis() + 3600000;
  }
  handleRoot(); 
}
void handleSpeedAdd() { 
  if(mode=="AUTO") {
    userOffsetPercent += 5; 
    if(overrideEndTime == 0) overrideEndTime = millis() + 3600000;
  }
  handleRoot(); 
}
void handleTimeSub() {
  if (overrideEndTime > millis()) {
    overrideEndTime -= 1800000;
    if (overrideEndTime < millis()) overrideEndTime = 0;
  }
  handleRoot();
}
void handleTimeAdd() {
  if (overrideEndTime == 0) overrideEndTime = millis() + 1800000;
  else overrideEndTime += 1800000;
  handleRoot();
}
void handleToggleZero() {
  allowZeroOverride = !allowZeroOverride;
  handleRoot();
}
void handleReset() { 
  userOffsetPercent = 0; 
  overrideEndTime = 0; 
  mode = "AUTO"; 
  handleRoot(); 
}
void handleAuto() { mode = "AUTO"; handleReset(); }
void handleOff()  { mode = "OFF";  handleReset(); }
void handleMax()  { mode = "MAX";  handleReset(); }

void checkNightMode() {
  if (!wifiConnected) { isNight = false; return; }
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){ isNight = false; return; }
  int hour = timeinfo.tm_hour;
  if (hour >= NIGHT_START_HOUR || hour < NIGHT_END_HOUR) isNight = true;
  else isNight = false;
}

void setup() {
  delay(2000); 
  Serial.begin(115200);

  // 1. OLED Init
  Wire.begin(PIN_SDA_OLED, PIN_SCL_OLED);
  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) Serial.println("OLED Fail");
  else {
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.setCursor(0,0);
    display.println("RACK SYSTEM v8.0");
    display.println("Booting...");
    display.display();
  }

  // 2. Sensor & PWM Init
  sensors.begin();
  
  #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(PIN_PWM_FAN, pwmFreq, pwmResolution);
  #else
    ledcSetup(0, pwmFreq, pwmResolution);
    ledcAttachPin(PIN_PWM_FAN, 0);
  #endif

  // 3. WiFi Init
  WiFi.begin(ssid, password);
  int t = 0;
  while (WiFi.status() != WL_CONNECTED && t < 15) {
    delay(500);
    t++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    // Register Web Routes
    server.on("/", handleRoot);
    server.on("/auto", handleAuto); server.on("/off", handleOff); server.on("/max", handleMax);
    server.on("/speed_sub", handleSpeedSub); server.on("/speed_add", handleSpeedAdd);
    server.on("/time_sub", handleTimeSub);   server.on("/time_add", handleTimeAdd);
    server.on("/toggle_zero", handleToggleZero); 
    server.on("/reset", handleReset);
    server.begin();
  } else {
    wifiConnected = false;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
}

void loop() {
  if (wifiConnected) {
    server.handleClient();
    checkNightMode();
  } else {
    isNight = false; 
  }

  // Timer Check
  if (overrideEndTime != 0 && millis() > overrideEndTime) {
    userOffsetPercent = 0;
    overrideEndTime = 0;
  }

  // Read Temperature
  sensors.requestTemperatures();
  float t = sensors.getTempCByIndex(0);
  if(t > -100 && t < 125 && t != 85.0) currentTemp = t;

  // --- FAN LOGIC ---
  int basePWM = 0;

  if (mode == "OFF") basePWM = 0;
  else if (mode == "MAX") basePWM = 255;
  else { // AUTO
    if (currentTemp < TEMP_OFF) basePWM = 0; 
    else if (currentTemp < TEMP_SILENT) basePWM = PWM_SILENT;
    else if (currentTemp < TEMP_RAMP_START) basePWM = PWM_SILENT;
    else basePWM = map((int)currentTemp, TEMP_RAMP_START, TEMP_MAX, PWM_RAMP_START, 255);
    
    if(basePWM > 255) basePWM = 255;
    if (isNight && currentTemp < TEMP_CRITIC) {
      if (basePWM > NIGHT_MAX_PWM) basePWM = NIGHT_MAX_PWM;
    }
  }

  int offsetValue = userOffsetPercent * 2.55; 
  int finalPWM = basePWM;
  
  if (mode == "AUTO") {
    // If 0dB Zone is ON (not allowed to override) and base is 0, keep it 0
    if (!allowZeroOverride && basePWM == 0 && offsetValue > 0) {
       finalPWM = 0; 
    } else {
       finalPWM = basePWM + offsetValue;
    }
  }
  
  if (finalPWM > 255) finalPWM = 255;
  if (finalPWM < 0) finalPWM = 0;
  // Anti-Stall Protection (Minimum 26)
  if (finalPWM > 0 && finalPWM < 26) finalPWM = 26; 

  currentPWM = finalPWM;
  currentPercent = map(currentPWM, 0, 255, 0, 100);
  
  #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(PIN_PWM_FAN, currentPWM);
  #else
    ledcWrite(0, currentPWM);
  #endif

  // --- OLED UPDATE ---
  display.clearDisplay();
  if (isNight) setContrast(10); else setContrast(255);

  display.setTextSize(1);
  display.setCursor(0,0);
  
  if (mode != "AUTO") {
    display.print(mode);
  } else {
    if (userOffsetPercent != 0) {
      display.print("MANUAL "); 
      if(userOffsetPercent>0) display.print("+"); 
      display.print(userOffsetPercent); display.print("%");
    } else {
       if (isNight) display.print("AUTO (NIGHT)");
       else display.print("AUTO (DAY)");
    }
  }

  if (overrideEndTime > millis()) {
    display.setCursor(115, 0); display.print("T");
  }

  display.setTextSize(2);
  display.setCursor(0, 16);
  display.print(currentTemp, 1); display.setTextSize(1); display.print("C");

  display.setTextSize(1);
  display.setCursor(0, 42);
  display.print("Fan: "); display.print(currentPercent); display.print("%");
  
  display.drawRect(0, 54, 128, 8, WHITE);
  display.fillRect(2, 56, map(currentPercent, 0, 100, 0, 124), 4, WHITE);

  if(wifiConnected) {
     display.setCursor(85, 0); 
     String ip = WiFi.localIP().toString();
     display.print("." + ip.substring(ip.lastIndexOf('.')+1));
  }

  display.display();
  delay(1000); 
}
