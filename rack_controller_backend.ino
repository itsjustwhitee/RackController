#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "time.h"

// --- HARDWARE PINS ---
#define PIN_PWM_FAN      37  
#define PIN_TEMP_SENSOR  4   
#define PIN_SDA_OLED     1   
#define PIN_SCL_OLED     2   
#define OLED_ADDRESS 0x3C

// --- COSTANTS ---
const int PWM_STARTUP = 26; // ~10% minimum to startup fans

// --- CONF & VARIABLES ---
float tempOff, tempRampStart, tempMax;
int offsetDurMin = 60;
char wifiSSID[33];
char wifiPass[65];

// Conf. Night & Zero dB
bool nightModeEnabled = false;
int nightMaxPWM = 100;
int nightOffsetPWM = 30;
bool zeroDbMode = false;

// System Sttatus
float currentTemp = 0.0;
int currentPWM = 0;
int currentPercent = 0;
String mode = "AUTO"; 
int userOffsetPercent = 0;          
unsigned long overrideEndTime = 0;  
bool isNight = false;
bool isAPMode = false; // if true => config. mode

// WiFi Reconnect
unsigned long lastWifiCheck = 0;
const unsigned long WIFI_TIMEOUT = 30000; 

// Global Objects
Preferences prefs;
WebServer server(80);
Adafruit_SSD1306 display(128, 64, &Wire, -1);
OneWire oneWire(PIN_TEMP_SENSOR);
DallasTemperature sensors(&oneWire);

// --- SUPPORT FUNCTIONS ---

void setContrast(int contrast) {
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(contrast);
}

void loadSettings() {
  prefs.begin("rack-v16", false);
  tempOff       = prefs.getFloat("tOff", 30.0);
  tempRampStart = prefs.getFloat("tRamp", 40.0);
  tempMax       = prefs.getFloat("tMax", 55.0);
  
  nightModeEnabled = prefs.getBool("nEn", false);
  nightMaxPWM      = prefs.getInt("nMax", 100);
  nightOffsetPWM   = prefs.getInt("nOff", 25);
  zeroDbMode       = prefs.getBool("zEn", false);
  offsetDurMin     = prefs.getInt("dur", 60);

  String s = prefs.getString("ssid", "WiFi-Name"); // change with your wifi name (or use AP mode)
  String p = prefs.getString("pass", "WiFi-Pass"); // change with your wifi password (or use AP mode)
  
  s.toCharArray(wifiSSID, 33); 
  p.toCharArray(wifiPass, 65);
  prefs.end();
}

void saveSettings() {
  prefs.begin("rack-v16", false);
  prefs.putFloat("tOff", tempOff);
  prefs.putFloat("tRamp", tempRampStart);
  prefs.putFloat("tMax", tempMax);
  prefs.putBool("nEn", nightModeEnabled);
  prefs.putInt("nMax", nightMaxPWM);
  prefs.putInt("nOff", nightOffsetPWM);
  prefs.putBool("zEn", zeroDbMode);
  prefs.putInt("dur", offsetDurMin);
  prefs.end();
}

void enableCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

// --- HANDLERS WEB SERVER ---

void handleJSON() {
  enableCORS();
  int remMin = (overrideEndTime > millis()) ? (overrideEndTime - millis()) / 60000 : 0;

  String json = "{";
  json += "\"temp\":" + String(currentTemp, 1) + ",";
  json += "\"pwm\":" + String(currentPWM) + ",";
  json += "\"pct\":" + String(currentPercent) + ",";
  json += "\"mode\":\"" + mode + "\",";
  json += "\"offset\":" + String(userOffsetPercent) + ",";
  json += "\"rem\":" + String(remMin) + ",";
  json += "\"isNight\":" + String(isNight ? "true" : "false") + ",";
  
  json += "\"conf\":{";
  json += "\"tOff\":" + String(tempOff, 1) + ",";
  json += "\"tRamp\":" + String(tempRampStart, 1) + ",";
  json += "\"tMax\":" + String(tempMax, 1) + ",";
  json += "\"dur\":" + String(offsetDurMin) + ",";
  json += "\"nEn\":" + String(nightModeEnabled ? "true" : "false") + ",";
  json += "\"nMax\":" + String(nightMaxPWM) + ",";
  json += "\"nOff\":" + String(nightOffsetPWM) + ",";
  json += "\"zEn\":" + String(zeroDbMode ? "true" : "false");
  json += "}}";
  
  server.send(200, "application/json", json);
}

void handleUpdate() {
  enableCORS();
  
  if (server.hasArg("m")) mode = server.arg("m");
  
  // Manual Offset
  if (server.hasArg("sub")) { userOffsetPercent -= 5; overrideEndTime = millis() + (offsetDurMin * 60000); }
  if (server.hasArg("add")) { userOffsetPercent += 5; overrideEndTime = millis() + (offsetDurMin * 60000); }
  if (server.hasArg("reset")) { userOffsetPercent = 0; overrideEndTime = 0; }

  // Configs
  if (server.hasArg("tOff")) tempOff = server.arg("tOff").toFloat();
  if (server.hasArg("tRamp")) tempRampStart = server.arg("tRamp").toFloat();
  if (server.hasArg("tMax")) tempMax = server.arg("tMax").toFloat();
  if (server.hasArg("dur")) offsetDurMin = server.arg("dur").toInt();

  // Advanced Configs
  if (server.hasArg("nEn")) nightModeEnabled = (server.arg("nEn") == "1");
  if (server.hasArg("nMax")) nightMaxPWM = server.arg("nMax").toInt();
  if (server.hasArg("nOff")) nightOffsetPWM = server.arg("nOff").toInt();
  if (server.hasArg("zEn")) zeroDbMode = (server.arg("zEn") == "1");

  // Save if configs present
  if (server.hasArg("tOff") || server.hasArg("nEn")) saveSettings();

  server.send(200, "text/plain", "OK");
}

void handleSetWiFi() {
  enableCORS();
  if (server.hasArg("s") && server.arg("s") != "") {
    prefs.begin("rack-v16", false); 
    prefs.putString("ssid", server.arg("s"));
    if (server.hasArg("p")) prefs.putString("pass", server.arg("p"));
    prefs.end(); 
    
    String html = "<html><body><h1>Saving...</h1><p>ESP will reboot in 3 seconds.</p></body></html>";
    server.send(200, "text/html", html);
    delay(2000); 
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Missing SSID");
  }
}

void handleRoot() {
  if (isAPMode) {
    // Minimal Interface to setup WiFi
    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<style>body{font-family:sans-serif;padding:20px;background:#eee} input{padding:8px;width:100%;margin-bottom:10px} .btn{background:#333;color:white;padding:10px;width:100%;border:none}</style></head><body>";
    html += "<h2>Rack Setup</h2>";
    html += "<p>Insert WIFI credentials:</p>";
    html += "<form action='/set_wifi' method='POST'>";
    html += "SSID (WiFi Name): <br><input type='text' name='s'><br>";
    html += "Password: <br><input type='password' name='p'><br>";
    html += "<input type='submit' class='btn' value='Salva e Riavvia'>";
    html += "</form></body></html>";
    server.send(200, "text/html", html);
  } else {
    // Minimal Interface in connected mode
    server.send(200, "text/html", "<h1>Rack Controller Online</h1><p>Use the Dashboard to tune settings.</p><a href='/json'>See JSON data</a>");
  }
}

// --- SETUP ---
void setup() {
  Serial.begin(115200);
  loadSettings();
  
  Wire.begin(PIN_SDA_OLED, PIN_SCL_OLED);
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  
  sensors.begin();
  
  #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(PIN_PWM_FAN, 25000, 8);
  #else
    ledcSetup(0, 25000, 8); ledcAttachPin(PIN_PWM_FAN, 0);
  #endif

  // --- LOGICA WIFI (STA or AP) ---
  display.setTextSize(1);
  display.setCursor(0, 0);
  
  if (String(wifiSSID).length() > 1) {
    display.println("Connecting WiFi...");
    display.print(wifiSSID);
    display.display();
  
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSSID, wifiPass);
    
    int t = 0;
    while (WiFi.status() != WL_CONNECTED && t < 20) { // 10 seconds timeout
      delay(500);
      display.print("."); display.display();
      t++;
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    // CONNECTED
    configTime(3600, 3600, "pool.ntp.org"); // GMT+1 + DST
    display.clearDisplay();
    display.setCursor(0,0); display.println("WiFi Connected!");
    display.println(WiFi.localIP());
    display.display();
    delay(1000);
  } else {
    // FAIL -> AP MODE
    isAPMode = true;
    WiFi.disconnect();
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Rack-Setup", "12345678");
    
    display.clearDisplay();
    display.setCursor(0,0); 
    display.println("WiFi Failed!");
    display.println("Connect to WiFi:");
    display.println("Rack-Setup");
    display.println("Go to: 192.168.4.1");
    display.display();
    delay(3000); 
  }

  server.on("/", handleRoot);
  server.on("/json", handleJSON);
  server.on("/update", handleUpdate);
  server.on("/set_wifi", handleSetWiFi);
  server.begin();
}

// --- LOOP ---
void loop() {
  unsigned long currentMillis = millis();

  // 1. Server managment
  server.handleClient();

  // 2. WiFi reconnection (only if not in AP config mode)
  if (!isAPMode && (WiFi.status() != WL_CONNECTED) && (currentMillis - lastWifiCheck >= WIFI_TIMEOUT)) {
    WiFi.disconnect();
    WiFi.reconnect();
    lastWifiCheck = currentMillis;
  }

  // 3. Timer Override
  if (overrideEndTime != 0 && currentMillis > overrideEndTime) { 
    userOffsetPercent = 0; overrideEndTime = 0; 
  }

  // 4. Sensors read
  sensors.requestTemperatures();
  float t = sensors.getTempCByIndex(0);
  if(t > -50 && t < 110) currentTemp = t;

  // 5. PWM Calcultaion
  int calcPWM = 0;
  
  // Night detection
  struct tm ti; isNight = false;
  if (!isAPMode && WiFi.status() == WL_CONNECTED && getLocalTime(&ti)) {
    if (nightModeEnabled && (ti.tm_hour >= 23 || ti.tm_hour < 7)) isNight = true;
  }

  if (mode == "OFF") calcPWM = 0;
  else if (mode == "MAX") calcPWM = 255;
  else { // AUTO
    if (currentTemp < tempOff) calcPWM = 0;
    else if (currentTemp < tempRampStart) calcPWM = PWM_STARTUP;
    else calcPWM = map((int)(currentTemp*10), (int)(tempRampStart*10), (int)(tempMax*10), PWM_STARTUP, 255);
    
    // Night logic
    if (isNight && mode == "AUTO") {
       calcPWM -= nightOffsetPWM;
       if (calcPWM < 0) calcPWM = 0;
       if (calcPWM > nightMaxPWM) calcPWM = nightMaxPWM; 
    }
  }

  // 6. User Offset apply
  int finalPWM = calcPWM;
  if (mode == "AUTO") finalPWM = calcPWM + (userOffsetPercent * 2.55);
  
  // 7. Zero dB Mode (Killer Switch)
  // If active, turns off if under edge (flat zone), ignoring manual offset
  if (zeroDbMode && currentTemp < tempOff) finalPWM = 0;

  finalPWM = constrain(finalPWM, 0, 255);
  if (finalPWM > 0 && finalPWM < PWM_STARTUP) finalPWM = PWM_STARTUP;

  currentPWM = finalPWM;
  currentPercent = map(currentPWM, 0, 255, 0, 100);
  
  #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(PIN_PWM_FAN, currentPWM);
  #else
    ledcWrite(0, currentPWM);
  #endif

  // --- OLED ---
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  if (isNight) setContrast(1); else setContrast(255);

  display.setTextSize(1);
  display.setCursor(0,0);
  
  if (mode != "AUTO") display.print(mode);
  else if (userOffsetPercent != 0) { display.print("MAN "); display.print(userOffsetPercent>0?"+":""); display.print(userOffsetPercent); display.print("%"); }
  else display.print("AUTO");

  // Status icons
  int iconX = 120;
  if (isNight) { display.setCursor(iconX, 0); display.print("N"); iconX -= 10; }
  if (zeroDbMode) { display.setCursor(iconX, 0); display.print("Z"); }
  if (overrideEndTime > millis()) { display.setCursor(64, 0); display.print("TMR"); }

  display.setTextSize(2);
  display.setCursor(0, 16);
  display.print(currentTemp, 1); display.setTextSize(1); display.print("C");

  display.setTextSize(1);
  display.setCursor(0, 42);
  display.print("Fan: "); display.print(currentPercent); display.print("%");
  
  display.drawRect(0, 54, 128, 8, SSD1306_WHITE);
  if (currentPercent > 0) display.fillRect(2, 56, map(currentPercent, 0, 100, 0, 124), 4, SSD1306_WHITE);

  // Footer Info
  display.setCursor(70, 42);
  if (isAPMode) {
     display.setCursor(0, 54); 
     display.fillRect(0, 54, 128, 10, SSD1306_BLACK); // Delete fan bar to show text
     display.print("SETUP: 192.168.4.1");
  } else if(WiFi.status() == WL_CONNECTED) {
     String ip = WiFi.localIP().toString();
     display.print("." + ip.substring(ip.lastIndexOf('.')+1));
  } else {
    display.print("No WiFi");
  }
  
  display.display();
  delay(1000);
}