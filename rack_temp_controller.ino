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
const int PWM_STARTUP = 26; // ~10% - Minimum value to avoid stalls

// --- CONFIGURATION ---
float tempOff, tempRampStart, tempMax;
int offsetDurMin = 60; // Timer duration
char wifiSSID[33], wifiPass[65];

// --- STATE ---
float currentTemp = 0.0;
int currentPWM = 0;
int currentPercent = 0;
String mode = "AUTO"; 
int userOffsetPercent = 0;          
unsigned long overrideEndTime = 0;  
bool wifiConnected = false, isNight = false;

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
  prefs.begin("rack-v15", false);
  tempOff       = prefs.getFloat("tOff", 30.0);
  tempRampStart = prefs.getFloat("tRamp", 40.0);
  tempMax       = prefs.getFloat("tMax", 55.0);
  String s = prefs.getString("ssid", "WIFI-Name");
  String p = prefs.getString("pass", "PASSWORD");
  s.toCharArray(wifiSSID, 33); p.toCharArray(wifiPass, 65);
  prefs.end();
}

void saveSettings() {
  prefs.begin("rack-v15", false);
  prefs.putFloat("tOff", tempOff);
  prefs.putFloat("tRamp", tempRampStart);
  prefs.putFloat("tMax", tempMax);
  prefs.end();
}

// --- WEB PAGE ---
void handleRoot() {
  String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<link rel='stylesheet' href='https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.5.1/css/all.min.css'>";
  html += "<style>body{font-family:sans-serif;background:#121212;color:#eee;text-align:center;padding:10px;}";
  html += ".card{background:#1e1e1e;border-radius:12px;padding:15px;margin-bottom:15px;border:1px solid #333;}";
  html += ".btn{background:#333;color:#fff;border:none;padding:10px 15px;border-radius:8px;text-decoration:none;display:inline-block;margin:5px;cursor:pointer;font-weight:bold;}";
  html += ".blue{background:#2196F3;} .red{background:#F44336;} .green{background:#4CAF50;} .orange{background:#FF9800;}";
  html += "input{background:#252525;color:#fff;border:1px solid #444;padding:8px;width:80px;border-radius:6px;text-align:center;}";
  html += "canvas{background:#000;border:1px solid #444;margin-top:10px;width:100%;max-width:300px;height:150px;}";
  html += "table{width:100%;} td{padding:5px 0;text-align:left;}</style></head><body>";
  
  html += "<h1>Rack Control</h1>";
  html += "<div class='card'><i class='fa-solid fa-temperature-full'></i> <b id='t' style='font-size:1.8rem;'>--</b> °C <br><i class='fa-solid fa-fan fa-spin'></i> <b id='s' style='font-size:2rem;'>--</b> %</div>";

  // OVERRIDE
  html += "<div class='card'><h3>Manual Control</h3>";
  html += "<a href='/speed_sub' class='btn'>-5%</a> <span id='o' style='font-size:1.2rem;'>0%</span> <a href='/speed_add' class='btn'>+5%</a><br>";
  html += "<div id='tmr' style='color:#FF9800;margin:5px 0;font-size:0.8rem;'></div>";
  html += "<a href='/set_mode?m=AUTO' class='btn blue'>AUTO</a><a href='/set_mode?m=OFF' class='btn'>OFF</a><a href='/set_mode?m=MAX' class='btn red'>MAX</a></div>";

  // CURVE WITH PREVIEW
  html += "<div class='card'><h3>Temperature Curve</h3>";
  html += "<canvas id='curveChart' ></canvas>";
  html += "<form action='/set_thresh'><table>";
  html += "<tr><td>Off to Low</td><td><input type='number' step='0.5' id='inOff' name='off' value='"+String(tempOff,1)+"' oninput='draw()'> °C</td></tr>";
  html += "<tr><td>Ramp Start</td><td><input type='number' step='0.5' id='inSta' name='sta' value='"+String(tempRampStart,1)+"' oninput='draw()'> °C</td></tr>";
  html += "<tr><td>Full Speed (100%);</td><td><input type='number' step='0.5' id='inMax' name='max' value='"+String(tempMax,1)+"' oninput='draw()'> °C</td></tr>";
  html += "</table><input type='submit' class='btn green' style='width:40%' value='Save'></form></div>";

  // TIMER (NON PERSISTENT PERSISTENTE) AND WIFI
  html += "<div class='card'><h3>Timer Configuration</h3><form action='/set_timer'><input type='number' name='odur' value='"+String(offsetDurMin)+"'> min <input type='submit' class='btn blue' value='Reload'></form></div>";
  html += "<div class='card'><h3>WiFi Network</h3><form action='/set_wifi'><input type='text' name='s' placeholder='SSID' style='width:60%'><br><input type='password' name='p' placeholder='Password' style='width:60%'><br><input type='submit' class='btn orange' style='width:40%' value='Save & Reboot'></form></div>";

  html += "<script>";
  html += "function draw(){";
  html += " const canvas=document.getElementById('curveChart');const ctx=canvas.getContext('2d');";
  html += " const off=parseFloat(document.getElementById('inOff').value);";
  html += " const sta=parseFloat(document.getElementById('inSta').value);";
  html += " const max=parseFloat(document.getElementById('inMax').value);";
  html += " ctx.clearRect(0,0,300,150);ctx.strokeStyle='#007AFF';ctx.lineWidth=2;ctx.beginPath();";
  html += " function getX(t){return (t-20)*300/50;} function getY(p){return 150-(p*150/255);}";
  html += " ctx.moveTo(getX(20),getY(0)); ctx.lineTo(getX(off),getY(0)); ctx.lineTo(getX(off),getY(30));";
  html += " ctx.lineTo(getX(sta),getY(30)); ctx.lineTo(getX(max),getY(255)); ctx.lineTo(getX(70),getY(255)); ctx.stroke();";
  html += " ctx.fillStyle='#444';ctx.font='10px Arial';ctx.fillText('20°C',5,145);ctx.fillText('70°C',270,145);";
  html += "}";
  html += "setInterval(()=>{fetch('/json').then(r=>r.json()).then(d=>{";
  html += "document.getElementById('t').innerText=d.t;document.getElementById('s').innerText=d.s;";
  html += "document.getElementById('o').innerText=(d.o>0?'+':'')+d.o+'%';";
  html += "document.getElementById('tmr').innerText=d.rem>0?'Reset tra '+d.rem+' min':'';});},2000);";
  html += "window.onload=draw;";
  html += "</script></body></html>";
  server.send(200, "text/html", html);
}

void handleJSON() {
  String json = "{\"t\":"+String(currentTemp,1)+",\"s\":"+String(currentPercent)+",\"o\":"+String(userOffsetPercent)+",\"rem\":"+String((overrideEndTime > millis()) ? (overrideEndTime - millis()) / 60000 : 0)+"}";
  server.send(200, "application/json", json);
}

void handleSetThresh() {
  if (server.hasArg("off")) tempOff = server.arg("off").toFloat();
  if (server.hasArg("sta")) tempRampStart = server.arg("sta").toFloat();
  if (server.hasArg("max")) tempMax = server.arg("max").toFloat();
  saveSettings(); handleRoot();
}

void handleSetTimer() { if (server.hasArg("odur")) offsetDurMin = server.arg("odur").toInt(); handleRoot(); }

void handleSetWiFi() {
  if (server.hasArg("s") && server.arg("s") != "") {
    prefs.begin("rack-v15", false); prefs.putString("ssid", server.arg("s"));
    if (server.hasArg("p") && server.arg("p") != "") prefs.putString("pass", server.arg("p"));
    prefs.end(); server.send(200, "text/html", "Riavvio in corso..."); delay(2000); ESP.restart();
  }
}

void handleSpeedSub() { userOffsetPercent -= 5; overrideEndTime = millis() + (offsetDurMin * 60000); handleRoot(); }
void handleSpeedAdd() { userOffsetPercent += 5; overrideEndTime = millis() + (offsetDurMin * 60000); handleRoot(); }
void handleSetMode() { if (server.hasArg("m")) mode = server.arg("m"); handleRoot(); }

// --- SETUP ---
void setup() {
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

  WiFi.begin(wifiSSID, wifiPass);
  int t = 0; while (WiFi.status() != WL_CONNECTED && t++ < 15) delay(500);

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    configTime(3600, 3600, "pool.ntp.org");
    server.on("/", handleRoot); server.on("/json", handleJSON);
    server.on("/set_thresh", handleSetThresh); server.on("/set_wifi", handleSetWiFi);
    server.on("/set_timer", handleSetTimer); server.on("/speed_sub", handleSpeedSub);
    server.on("/speed_add", handleSpeedAdd); server.on("/set_mode", handleSetMode);
    server.begin();
  }
}

// --- LOOP ---
void loop() {
  if (wifiConnected) server.handleClient();
  if (overrideEndTime != 0 && millis() > overrideEndTime) { userOffsetPercent = 0; overrideEndTime = 0; }

  sensors.requestTemperatures();
  float t = sensors.getTempCByIndex(0);
  if(t > -50 && t < 110) currentTemp = t;

  int basePWM = 0;
  if (mode == "OFF") basePWM = 0;
  else if (mode == "MAX") basePWM = 255;
  else { // AUTO
    if (currentTemp < tempOff) basePWM = 0;
    else if (currentTemp < tempRampStart) basePWM = PWM_STARTUP;
    else basePWM = map((int)(currentTemp*10), (int)(tempRampStart*10), (int)(tempMax*10), PWM_STARTUP, 255);

    struct tm ti; isNight = false;
    if (wifiConnected && getLocalTime(&ti)) {
      if (ti.tm_hour >= 23 || ti.tm_hour < 7) { isNight = true; if(basePWM > 102) basePWM = 102; }
    }
  }

  int finalPWM = basePWM;
  if (mode == "AUTO") finalPWM = basePWM + (userOffsetPercent * 2.55);
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
  if (isNight) setContrast(10); else setContrast(255);

  display.setTextSize(1);
  display.setCursor(0,0);
  if (mode != "AUTO") display.print(mode);
  else {
    if (userOffsetPercent != 0) {
      display.print("MANUAL "); if(userOffsetPercent>0) display.print("+"); 
      display.print(userOffsetPercent); display.print("%");
    } else display.print(isNight ? "AUTO (NIGHT)" : "AUTO (DAY)");
  }

  if (overrideEndTime > millis()) { display.setCursor(115, 0); display.print("T"); }

  display.setTextSize(2);
  display.setCursor(0, 16);
  display.print(currentTemp, 1); display.setTextSize(1); display.print("C");

  display.setTextSize(1);
  display.setCursor(0, 42);
  display.print("Fan: "); display.print(currentPercent); display.print("%");
  
  display.drawRect(0, 54, 128, 8, SSD1306_WHITE);
  display.fillRect(2, 56, map(currentPercent, 0, 100, 0, 124), 4, SSD1306_WHITE);

  if(wifiConnected) {
     display.setCursor(85, 0);
     String ip = WiFi.localIP().toString();
     display.print("." + ip.substring(ip.lastIndexOf('.')+1));
  }
  display.display();
  delay(1000);
}
