#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "MAX30105.h"
#include "heartRate.h"
#include "spo2_algorithm.h"
#include <ArduinoBLE.h>

// nRF52840 Supermini pins
#define PIN_BUZZER    2   // GPIO2 → Active piezo buzzer (warning sounds)
#define PIN_VIBRATOR  3   // GPIO3 → Coin vibration motor 10x2.7mm (haptic alerts)
#define PIN_SWITCH    4   // GPIO4 → Slide switch ON/OFF (INPUT_PULLUP)

// OLED SH1106 display (I2C — SDA/SCL shared with MAX30102)
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_ADDRESS  0x3C

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// MAX30102 heart rate, SpO2 and temperature sensor
MAX30105 sensor;

#define BUFFER_SIZE 100
uint32_t redBuffer[BUFFER_SIZE];
uint32_t irBuffer[BUFFER_SIZE];
int32_t  spo2;
int8_t   spo2Valid;
int32_t  bpm;
int8_t   bpmValid;

const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateIndex = 0;
long lastBeat = 0;
float beatsPerMinute = 0;

// BLE service and characteristics
BLEService healthService("180D");
BLEFloatCharacteristic charBPM  ("2A37", BLERead | BLENotify);
BLEFloatCharacteristic charSpO2 ("2A5F", BLERead | BLENotify);
BLEFloatCharacteristic charTemp ("2A1C", BLERead | BLENotify);
BLEIntCharacteristic   charAlert("2A3F", BLERead | BLENotify);

// Alert thresholds
#define BPM_MIN   50
#define BPM_MAX   120
#define SPO2_MIN  90
#define TEMP_MAX  37.8
#define TEMP_MIN  35.0

// Pulse graph buffer (full display width)
#define GRAPH_WIDTH  128
#define GRAPH_HEIGHT 16
int graphBuffer[GRAPH_WIDTH];
int graphIndex = 0;

float currentTemp = 0;
unsigned long lastRead = 0;
unsigned long lastDisplayUpdate = 0;
bool bleConnected = false;
int batteryLevel = 85;

void setup() {
  Serial.begin(115200);

  // PIN_BUZZER → Active piezo buzzer
  // PIN_VIBRATOR → Coin vibration motor
  // PIN_SWITCH → Slide switch (HIGH = OFF)
  pinMode(PIN_BUZZER,   OUTPUT);
  pinMode(PIN_VIBRATOR, OUTPUT);
  pinMode(PIN_SWITCH,   INPUT_PULLUP);
  digitalWrite(PIN_BUZZER,   LOW);
  digitalWrite(PIN_VIBRATOR, LOW);

  // OLED SH1106 display init
  if (!display.begin(OLED_ADDRESS, true)) {
    Serial.println("OLED not found");
    while (true);
  }
  display.clearDisplay();
  showWelcome();
  delay(2000);

  // MAX30102 sensor init
  if (!sensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found");
    showError("Sensor missing");
    while (true);
  }
  sensor.setup();
  sensor.setPulseAmplitudeRed(0x0A);
  sensor.setPulseAmplitudeGreen(0);

  for (int i = 0; i < BUFFER_SIZE; i++) {
    while (!sensor.available()) sensor.check();
    redBuffer[i] = sensor.getRed();
    irBuffer[i]  = sensor.getIR();
    sensor.nextSample();
  }

  for (int i = 0; i < GRAPH_WIDTH; i++) graphBuffer[i] = 0;

  // ArduinoBLE init
  if (!BLE.begin()) {
    Serial.println("BLE failed");
    showError("BLE failed");
    while (true);
  }
  BLE.setLocalName("HealthBand");
  BLE.setAdvertisedService(healthService);
  healthService.addCharacteristic(charBPM);
  healthService.addCharacteristic(charSpO2);
  healthService.addCharacteristic(charTemp);
  healthService.addCharacteristic(charAlert);
  BLE.addService(healthService);
  BLE.advertise();

  startupBeep();
}

void loop() {
  // PIN_SWITCH HIGH = band turned off
  if (digitalRead(PIN_SWITCH) == HIGH) {
    shutDown();
    return;
  }

  BLEDevice central = BLE.central();
  bleConnected = central ? true : false;

  if (millis() - lastRead >= 1000) {
    lastRead = millis();
    readSensor();
    checkAlerts();
    updateBLE();
  }

  if (millis() - lastDisplayUpdate >= 500) {
    lastDisplayUpdate = millis();
    drawScreen();
  }
}

void readSensor() {
  for (int i = 25; i < BUFFER_SIZE; i++) {
    redBuffer[i - 25] = redBuffer[i];
    irBuffer[i - 25]  = irBuffer[i];
  }
  for (int i = 75; i < BUFFER_SIZE; i++) {
    while (!sensor.available()) sensor.check();
    redBuffer[i] = sensor.getRed();
    irBuffer[i]  = sensor.getIR();
    sensor.nextSample();
  }

  maxim_heart_rate_and_oxygen_saturation(
    irBuffer, BUFFER_SIZE, redBuffer,
    &spo2, &spo2Valid, &bpm, &bpmValid
  );

  // MAX30102 built-in die temperature (skin temp approximation)
  currentTemp = sensor.readTemperature();

  int normalized = map(constrain(bpm, 40, 180), 40, 180, 0, GRAPH_HEIGHT);
  graphBuffer[graphIndex] = normalized;
  graphIndex = (graphIndex + 1) % GRAPH_WIDTH;
}

void drawScreen() {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  display.setCursor(0, 0);
  display.print("\x03 ");
  display.setTextSize(2);
  display.print(bpmValid ? String(bpm) : "--");
  display.setTextSize(1);
  display.print(" BPM");

  display.setCursor(80, 0);
  display.print("O2:");
  display.print(spo2Valid ? String(spo2) + "%" : "--%");

  display.setCursor(0, 18);
  display.print("\xF8""C ");
  display.print(currentTemp, 1);
  drawBattery(90, 18, batteryLevel);

  display.drawFastHLine(0, 30, 128, SH110X_WHITE);
  drawGraph();

  display.setCursor(115, 56);
  display.print(bleConnected ? "BT" : "  ");

  display.display();
}

void drawGraph() {
  int yBase = 63;
  for (int x = 0; x < GRAPH_WIDTH - 1; x++) {
    int i1 = (graphIndex + x)     % GRAPH_WIDTH;
    int i2 = (graphIndex + x + 1) % GRAPH_WIDTH;
    display.drawLine(x, yBase - graphBuffer[i1], x + 1, yBase - graphBuffer[i2], SH110X_WHITE);
  }
}

void drawBattery(int x, int y, int level) {
  display.drawRect(x, y, 28, 8, SH110X_WHITE);
  display.fillRect(x + 28, y + 2, 2, 4, SH110X_WHITE);
  display.fillRect(x + 1, y + 1, map(level, 0, 100, 0, 26), 6, SH110X_WHITE);
}

void checkAlerts() {
  int alertCode = 0;

  if (bpmValid && (bpm < BPM_MIN || bpm > BPM_MAX)) {
    alertCode = 1;
    criticalAlert();
  } else if (spo2Valid && spo2 < SPO2_MIN) {
    alertCode = 2;
    criticalAlert();
  } else if (currentTemp > TEMP_MAX) {
    alertCode = 3;
    mediumAlert();
  } else if (currentTemp < TEMP_MIN && currentTemp > 20) {
    alertCode = 4;
    mediumAlert();
  }

  charAlert.writeValue(alertCode);
}

void updateBLE() {
  if (!bleConnected) return;
  charBPM.writeValue((float)bpm);
  charSpO2.writeValue((float)spo2);
  charTemp.writeValue(currentTemp);
}

// PIN_BUZZER + PIN_VIBRATOR → 3 fast beeps (critical health alert)
void criticalAlert() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_BUZZER,   HIGH);
    digitalWrite(PIN_VIBRATOR, HIGH);
    delay(200);
    digitalWrite(PIN_BUZZER,   LOW);
    digitalWrite(PIN_VIBRATOR, LOW);
    delay(100);
  }
}

// PIN_BUZZER → 2 slow beeps (moderate health alert)
void mediumAlert() {
  for (int i = 0; i < 2; i++) {
    digitalWrite(PIN_BUZZER, HIGH);
    delay(400);
    digitalWrite(PIN_BUZZER, LOW);
    delay(200);
  }
}

// PIN_BUZZER → startup confirmation beep
void startupBeep() {
  digitalWrite(PIN_BUZZER, HIGH); delay(100);
  digitalWrite(PIN_BUZZER, LOW);  delay(50);
  digitalWrite(PIN_BUZZER, HIGH); delay(200);
  digitalWrite(PIN_BUZZER, LOW);
}

void showWelcome() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(10, 10);
  display.println("HEALTH");
  display.setCursor(10, 30);
  display.println("BAND");
  display.setTextSize(1);
  display.setCursor(20, 52);
  display.println("Starting...");
  display.display();
}

void showError(const char* msg) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 20);
  display.println("ERROR:");
  display.println(msg);
  display.display();
}

// PIN_SWITCH HIGH → nRF52840 deep sleep (SoftDevice)
void shutDown() {
  display.clearDisplay();
  display.display();
  digitalWrite(PIN_BUZZER,   LOW);
  digitalWrite(PIN_VIBRATOR, LOW);
  BLE.stopAdvertise();
  sd_power_system_off();
}
