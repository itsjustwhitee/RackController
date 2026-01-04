# RackController: A Smart Rack Cooling System (ESP32)

A DIY intelligent cooling system for home server racks or DIY cabinets, built with ESP32-S3, Arctic PWM fans, and DS18B20 temperature sensor. 
It features a responsive Mobile Web Interface, Night Mode (silent operation), and granular manual controls.

## 🚀 Features

*   **♨️Automatic Temp Control:** Linear PWM ramp based on temperature.
*   **🤫Zero dB Mode:** Fans completely stop when temperature is below threshold (30°C).
*   **🌙Night Mode:** Automatically limits fan speed (Max 40%) between 23:00 and 07:00 for silence (unless critical temp is reached).
*   **🌐Web Interface (Dark Mode):**
    *   Real-time monitoring (Temp & Fan %).
    *   Manual Speed Offset (+/- 5%).
    *   Timer for manual overrides.
    *   Master Controls (AUTO, MAX, OFF).
    *   Responsive design for Smartphones.
*   **🖥️OLED Display:** Shows live stats, IP address, and automatically dims at night.
*   **❌Offline Mode:** Works autonomously even without a Wi-Fi connection.

## 🛠 Hardware Required (specifically what I used)

*   **Microcontroller:** ESP32-S3 (N8R2 used here, but any ESP32 works).
*   **Fans:** Arctic P14 PRO PWM PST (or P12) or similar (at least one for intake and one for exhaust).
*   **Temp Sensor:** DS18B20 (Waterproof probe recommended).
*   **Power:** 12V 2A Power Supply + LM2596 Step-down Module (12V to 5V for ESP32).
*   **Display:** 0.96" OLED I2C (SSD1306).
*   **Misc:** Simil Wago connectors, 20AWG silicone wires, DC Jack adapter, PWM extensions (just not to cut fan and power supply cables).

## 🔌 Wiring

| Component | ESP32 Pin | Note |
| :--- | :--- | :--- |
| **Fan PWM** | GPIO 37 | Blue wire of the fan |
| **DS18B20 Data** | GPIO 4 | Requires 4.7k Pull-up resistor (often built-in on modules) |
| **OLED SDA** | GPIO 1 | I2C Data |
| **OLED SCL** | GPIO 2 | I2C Clock |

*Note: Fans must be powered directly by the 12V PSU. The ESP32 and OLED are powered by 5V (from LM2596). Only grounds (GND) must be shared.*
*Note: almost all GPIO pins are equivalent*

``` text
                                          MAIN POWER SUPPLY
                                        (12V 2A Power Adapter)
                                                 |
            +------------------------------------+------------------------------------+
            |                                                                         |
        [+] 12V (Red Wire)                                                        [-] GND (Black Wire)
            |                                                                         |
            |   +--------------------------[ WAGO / CONNECTORS ]----------------------+
            |   |                                     |                               |
            |   | (12V Line)                          | (12V Line)                    | (Common Ground)
            |   v                                     v                               v
    +-------+-------+                       +---------+---------+           +---------+---------+
    |  ARCTIC FANS  |                       |   LM2596 (Buck)   |           |    ALL GNDs     |
    | (PST Series)  |                       |  12V -> 5V Step   |           | (Common Ground) |
    +-------+-------+                       +---------+---------+           +---------+---------+
            |                                         |                               |
            |                                     [+] 5V OUT                      [-] GND OUT
            |                                         |                               |
            |                                         v                               v
            |                               +---------+-------------------------------+---------+
            |                               |              ESP32-S3 (N8R2)                      |
            |                               |           (Green Breakout Board)                  |
            |                               +---------------------------------------------------+
            |                               |                                                   |
            |                               | [5Vin] <----- Red Wire from LM2596 (5V)           |
            |                               | [G]    <----- Black Wire from LM2596 (GND)        |
            |                               |                                                   |
            +----(Blue PWM)---------------->| [IO 37]  <--- Fan Speed Control                   |
                                            |                                                   |
                                            |                SENSORS AND DISPLAY                |
                                            |                                                   |
    +------------------+                    | [3V3]  -----> Sensor & OLED Power (+)             |
    |  DS18B20 SENSOR  |<----(Data)---------| [IO 4]   <--- Temperature Data                    |
    | (Thermal Probe)  |<----(GND)----------| [G]      ---- Sensor Ground (-)                   |
    +------------------+                    |                                                   |
                                            |                                                   |
    +------------------+                    | [IO 1]   ---> SDA (Display Data)                  |
    |   OLED DISPLAY   |<----(SDA)----------| [IO 2]   ---> SCL (Display Clock)                 |
    |     0.96" I2C    |<----(SCL)----------| [3V3]    ---- Display VCC (+)                     |
    +------------------+                    | [G]      ---- Display GND (-)                     |
                                            +---------------------------------------------------+
```

## 📦 Installation

1.  **Install Arduino IDE** (Legacy 1.8.x or 2.0+, this could give throubles while downloading).
2.  Install the **ESP32 Board Manager** (by Espressif).
3.  Install required libraries via Library Manager:
    *   `Adafruit SSD1306`
    *   `Adafruit GFX`
    *   `DallasTemperature`
    *   `OneWire`
4.  Open the `.ino` file.
5.  **Edit lines 11-12** with your Wi-Fi credentials:
    ```cpp
    const char* ssid     = "YOUR_WIFI_NAME";
    const char* password = "YOUR_WIFI_PASSWORD";
    ```
    *Note: Change also pin numbers if you used different ones.*
6.  Select your board (e.g., `ESP32S3 Dev Module`) and Upload.
    *Tip: If using ESP32-S3 N8R2, set Flash Mode to `DIO` and PSRAM to `Disabled` (or `QSPI`) to avoid boot loops.*

## 📱 Usage

1.  Power on the system.
2.  The OLED will display the (last group of numbers of) IP address (e.g., `45` for `192.168.1.45`).
3.  Open that IP in your browser.
4.  **Controls:**
    *   ⚡**Speed:** Add/Remove speed percentage relative to the automatic logic.
    *   ⌚**Timer:** Set a duration for your manual changes.
    *   🤫**0dB Zone:** Toggle ON to strictly respect silence below 30°C, or OFF to force start fans even when cold.
    *   👑**Master:** Force MAX speed or turn OFF everything.
<p align="center">
   <img width="30%" alt="image" src="https://github.com/user-attachments/assets/7e2fdaec-18a5-4f06-afc0-d03761823629" />
</p>


## ⚙️ Logic Configuration

You can tweak these constants in the code to fit your needs:

```cpp
const float TEMP_OFF        = 30.0; // Fans turn OFF below this
const float TEMP_MAX        = 55.0; // Fans hit 100% above this
const int   NIGHT_START_HOUR= 23;   // Night mode start
const int   NIGHT_MAX_PWM   = 102;  // Max speed during night (~40%)
```

## 📜 License

This project is open-source. Feel free to modify and distribute.
