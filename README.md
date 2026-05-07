# Health-Check-Elastic-Band
A elastic band with screen that constantly checks various health rates. Can be also connected to an app to see health graphs (Stasis 2026 Project)

What it does
The band reads health metrics continuously from a MAX30102 sensor and shows them on the wrist display. If any value goes outside safe thresholds, a buzzer and vibration motor fire an alert. All data is also sent to a companion Android app built in MIT App Inventor, where it is displayed live and logged.

Hardware
nRF52840 Supermini development board - microcontroller and BLE radio
MAX30102 Green Board - heart rate, SpO2, and skin temperature sensor
OLED display SH1106 1.3 inch white - wrist display
3.7V 600mAh LiPo battery - power source, charged via the board's USB-C port
Active buzzer 3V piezo - audible health alerts
Coin vibration motor 10x2.7mm - haptic alerts
Slide switch SPDT - physical ON/OFF
Elastic fabric band with 3D printed enclosure - wearable housing

Wiring
MAX30102 and OLED share the I2C bus on SDA and SCL. Both run on 3.3V from the board.
GPIO2 - buzzer positive
GPIO3 - vibration motor positive (via transistor)
GPIO4 - slide switch middle pin
SDA (GPIO6) - MAX30102 SDA and OLED SDA
SCL (GPIO7) - MAX30102 SCL and OLED SCL
3.3V - MAX30102 VIN and OLED VCC
GND - all ground connections
BAT+ - LiPo positive
BAT- - LiPo negative

Firmware
Written in Arduino IDE with the nRF52840 board package. Libraries used:
Adafruit SH110X - OLED display
Adafruit GFX - graphics primitives
SparkFun MAX3010x - heart rate and SpO2 sensor
ArduinoBLE - Bluetooth Low Energy
The firmware reads the sensor every second, updates the display at 500ms, checks alert thresholds, and notifies the app via BLE. When the slide switch is turned off, the board enters deep sleep using the Nordic SoftDevice sd_power_system_off() call.
Alert thresholds: heart rate below 50 or above 120 BPM, SpO2 below 90%, temperature above 37.8C or below 35.0C.

Display layout
The OLED shows heart rate large on the top left, SpO2 on the top right, temperature and battery bar on the second row, a divider line, and a real-time pulse graph along the bottom. A small BT indicator shows Bluetooth connection status.

Android App (In progress)
Built in MIT App Inventor. Connects to the band by name (HealthBand) over BLE and subscribes to four characteristics: BPM, SpO2, temperature, and alert code. Shows all values live on screen and plays a sound alert when the band sends a non-zero alert code.
BLE Service UUID: 180D
BPM characteristic: 2A37
SpO2 characteristic: 2A5F
Temperature characteristic: 2A1C
Alert code characteristic: 2A3F
Alert codes: 0 is normal, 1 is heart rate out of range, 2 is low SpO2, 3 is fever, 4 is low body temperature.
<img width="1463" height="709" alt="Captura de pantalla 2026-05-06 141453" src="https://github.com/user-attachments/assets/e5c737f8-dc47-4c4c-93ae-326c0eeee21e" />
<img width="1469" height="706" alt="Captura de pantalla 2026-05-06 141431" src="https://github.com/user-attachments/assets/272dd2bc-878e-471d-8a26-622bc940c12a" />
