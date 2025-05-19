/**********************************************************************************
 * Combined IoT Plant Watering & Water Quality Monitoring System
 *
 * This integrated sketch uses an ESP32 to:
 *   - Monitor soil moisture via an analog sensor (GPIO34)
 *   - Read ambient temperature and humidity from a DHT11 (GPIO14)
 *   - Control a relay (water pump) automatically by moisture level or manually via Blynk/physical buttons   
 *   - Read a TDS sensor (GPIO35) for water quality assessment
 *   - Display all sensor readings on a 128×64 OLED screen where:
 *       • The moisture value is shown in a big font inside a square box.
 *       • The remaining sensor data (Temperature, Humidity, Water Quality, and TDS)
 *         are displayed on three lines to the right of the box.
 *   - Provide remote monitoring and control through Blynk virtual pins:
 *       V1: Moisture Percentage
 *       V2: Temperature (°C)
 *       V3: Humidity (%)
 *       V4: Mode Switch (Auto/Manual)
 *       V5: Relay (Water Pump) Status
 *       V6: Water Quality (TDS Interpretation)
 *
 * Hardware Connections (ESP32):
 *   - Soil Moisture Sensor: Analog on GPIO34
 *   - TDS Sensor: Analog on GPIO35
 *   - DHT11 Sensor: Digital on GPIO14
 *   - Relay (Water Pump): GPIO25
 *   - WiFi LED: GPIO2
 *   - Relay Control Button: GPIO32
 *   - Mode Switch Button: GPIO33
 *   - Buzzer: GPIO26
 *   - Mode LED: GPIO15
 *
 * WiFi credentials: adjust these to your network.
 *
 * Blynk Template Definitions:
 *   #define BLYNK_TEMPLATE_ID "TMPL6fWfSSBEN"
 *   #define BLYNK_TEMPLATE_NAME "ESP32 water plant monitor"
 *   #define BLYNK_AUTH_TOKEN "ke_3d_08pDT3s-iERwHT-z_fJp60uG3Y"
 **********************************************************************************/

// Blynk Template and Auth Token
#define BLYNK_TEMPLATE_ID "TMPL6fWfSSBEN"
#define BLYNK_TEMPLATE_NAME "ESP32 water plant monitor"
#define BLYNK_AUTH_TOKEN  "ke_3d_08pDT3s-iERwHT-z_fJp60uG3Y"

// Include libraries
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <DHT.h>
#include <AceButton.h>
using namespace ace_button;

// WiFi Credentials (choose one set)
// Uncomment the credentials you wish to use:
// Primary credentials:
// char ssid[] = "suresh_1_2.4";
// char pass[] = "GHALEYFAMILY@1";
// Alternative credentials:
char ssid[] = "pradeepdeuba_wnepal";
char pass[] = "434F002D93";

// OLED Display Settings
#define SCREEN_WIDTH 128  
#define SCREEN_HEIGHT 64  
#define OLED_RESET    -1  
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Global Sensor Data Variables
float tdsValue = 0.0;
String waterQuality = "";
int moisturePercentage = 0;
int temperature1 = 0;
int humidity1 = 0;
String currMode = "A";  // "A" = Automatic, "M" = Manual

// Sensor & Actuator Pin Definitions
#define SensorPin       34    // Soil Moisture Sensor
#define TDS_PIN         35    // TDS Sensor
#define DHTPin          14    // DHT11 Sensor
#define RelayPin        25    // Relay (Water Pump)
#define wifiLed         2     // WiFi Status LED
#define RelayButtonPin  32    // Relay Control Button
#define ModeSwitchPin   33    // Mode Switch Button
#define BuzzerPin       26    // Buzzer
#define ModeLed         15    // Mode LED

// Moisture Sensor Calibration & Thresholds
int wetSoilVal = 955;
int drySoilVal = 2206;
int moistPerLow = 20;   // Lower moisture threshold (%)
int moistPerHigh = 80;  // Upper moisture threshold (%)

// TDS Sensor Parameters
#define VREF           3.3
#define ADC_RESOLUTION 4095

// DHT Sensor Setup
#define DHTTYPE DHT11
DHT dht(DHTPin, DHTTYPE);

// Blynk Virtual Pins
#define VPIN_MoistPer      V1 
#define VPIN_TEMPERATURE   V2
#define VPIN_HUMIDITY      V3
#define VPIN_MODE_SWITCH   V4
#define VPIN_RELAY         V5
#define VPIN_WATER_QUALITY V6

// Global Variables for Relay & Mode Control
int sensorVal = 0;
bool toggleRelay = LOW;
bool prevMode = true;  // true = Automatic; false = Manual

// Blynk Timer
BlynkTimer timer;

// AceButton Setup for Physical Buttons
ButtonConfig config1;
AceButton button1(&config1);
ButtonConfig config2;
AceButton button2(&config2);

// Function Prototypes
void button1Handler(AceButton* button, uint8_t eventType, uint8_t buttonState);
void button2Handler(AceButton* button, uint8_t eventType, uint8_t buttonState);
void checkBlynkStatus();
void sendSensor();
void controlBuzzer(int duration);
void controlMoist();
String getWaterQuality(float tdsVal);
float getTDSValue();
void getMoisture();
void getWeather();
void updateDisplay();

// Utility Functions

String getWaterQuality(float tdsVal) {
  if (tdsVal < 50) return "Very Pure";
  else if (tdsVal < 150) return "Excellent";
  else if (tdsVal < 300) return "Good";
  else if (tdsVal < 500) return "Fair";
  else if (tdsVal < 1000) return "Poor";
  else return "Very Poor";
}

float getTDSValue() {
  int adcTDS = analogRead(TDS_PIN);
  float voltage = (adcTDS * VREF) / ADC_RESOLUTION;
  return (voltage * 0.5) * 1000;
}

void getMoisture() {
  sensorVal = analogRead(SensorPin);
  if (sensorVal > (wetSoilVal - 100) && sensorVal < (drySoilVal + 100)) {
    moisturePercentage = map(sensorVal, drySoilVal, wetSoilVal, 0, 100);
  } else {
    moisturePercentage = 0;
  }
  delay(100);
}

void getWeather() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (!isnan(h) && !isnan(t)) {
    humidity1 = (int) h;
    temperature1 = (int) t;
  } else {
    Serial.println("Failed to read from DHT sensor!");
  }
}

// Update the OLED display:
// - A 50×50 pixel box on the left shows the moisture percentage in big text.
// - To its right, three lines show Temperature/Humidity (line 1),
//   Water Quality (line 2), and TDS (line 3).
void updateDisplay() {
  waterQuality = getWaterQuality(tdsValue);
  display.clearDisplay();

  // --- Draw moisture box ---
  int boxSize = 50;
  display.drawRect(0, 0, boxSize, boxSize, WHITE);

  // Big moisture text centered in the box
  display.setTextSize(2);
  display.setTextColor(WHITE);
  String moistText = String(moisturePercentage) + "%";
  int textWidth = moistText.length() * 12;  // approximate width at text size 2
  int xPos = (boxSize - textWidth) / 2;
  int yPos = (boxSize - 16) / 2;  // approximate height (16 pixels) at text size 2
  display.setCursor(xPos, yPos);
  display.print(moistText);

  // --- Display additional sensor data in three lines to the right of the box ---
  display.setTextSize(1);
  
  // Line 1: Temperature and Humidity
  display.setCursor(55, 5);
  display.print("T:");
  display.print(temperature1);
  display.print("C H:");
  display.print(humidity1);
  display.print("%");

  // Line 2: Water Quality
  display.setCursor(55, 20);
  display.print("Q:");
  display.print(waterQuality);

  // Line 3: TDS Value
  display.setCursor(55, 35);
  display.print("TDS:");
  display.print(tdsValue, 0);
  display.print("p");

  display.display();
}

void controlBuzzer(int duration) {
  digitalWrite(BuzzerPin, HIGH);
  delay(duration);
  digitalWrite(BuzzerPin, LOW);
}

void sendSensor() {
  getMoisture();
  getWeather();
  tdsValue = getTDSValue();
  
  Serial.print("TDS ADC Reading: ");
  Serial.println(analogRead(TDS_PIN));
  Serial.print("TDS Value (ppm): ");
  Serial.println(tdsValue);

  updateDisplay();

  Blynk.virtualWrite(VPIN_MoistPer, moisturePercentage);
  Blynk.virtualWrite(VPIN_TEMPERATURE, temperature1);
  Blynk.virtualWrite(VPIN_HUMIDITY, humidity1);
  Blynk.virtualWrite(VPIN_WATER_QUALITY, waterQuality);
}

void checkBlynkStatus() {
  if (!Blynk.connected()) digitalWrite(wifiLed, LOW);
  else digitalWrite(wifiLed, HIGH);
}

void controlMoist() {
  if (prevMode) {
    if (moisturePercentage < moistPerLow && toggleRelay == LOW) {
      controlBuzzer(500);
      digitalWrite(RelayPin, HIGH);
      toggleRelay = HIGH;
      Blynk.virtualWrite(VPIN_RELAY, toggleRelay);
      delay(1000);
    }
    if (moisturePercentage > moistPerHigh && toggleRelay == HIGH) {
      controlBuzzer(500);
      digitalWrite(RelayPin, LOW);
      toggleRelay = LOW;
      Blynk.virtualWrite(VPIN_RELAY, toggleRelay);
      delay(1000);
    }
  } else {
    button1.check();
  }
}

BLYNK_CONNECTED() {
  Blynk.syncVirtual(VPIN_MoistPer);
  Blynk.syncVirtual(VPIN_RELAY);
  Blynk.syncVirtual(VPIN_TEMPERATURE);
  Blynk.syncVirtual(VPIN_HUMIDITY);
  Blynk.virtualWrite(VPIN_MODE_SWITCH, prevMode);
}

BLYNK_WRITE(VPIN_RELAY) {
  if (!prevMode) {
    toggleRelay = param.asInt();
    digitalWrite(RelayPin, toggleRelay);
  } else {
    Blynk.virtualWrite(VPIN_RELAY, toggleRelay);
  }
}

BLYNK_WRITE(VPIN_MODE_SWITCH) {
  if (prevMode != param.asInt()) {
    prevMode = param.asInt();
    currMode = prevMode ? "A" : "M";
    digitalWrite(ModeLed, prevMode);
    controlBuzzer(500);
    if (!prevMode && toggleRelay == HIGH) {
      digitalWrite(RelayPin, LOW);
      toggleRelay = LOW;
      Blynk.virtualWrite(VPIN_RELAY, toggleRelay);
    }
  }
}

void button1Handler(AceButton* button, uint8_t eventType, uint8_t buttonState) {
  if (eventType == AceButton::kEventReleased) {
    digitalWrite(RelayPin, !digitalRead(RelayPin));
    toggleRelay = digitalRead(RelayPin);
    Blynk.virtualWrite(VPIN_RELAY, toggleRelay);
  }
}

void button2Handler(AceButton* button, uint8_t eventType, uint8_t buttonState) {
  if (eventType == AceButton::kEventReleased) {
    if (prevMode && toggleRelay == HIGH) {
      digitalWrite(RelayPin, LOW);
      toggleRelay = LOW;
      Blynk.virtualWrite(VPIN_RELAY, toggleRelay);
    }
    prevMode = !prevMode;
    currMode = prevMode ? "A" : "M";
    digitalWrite(ModeLed, prevMode);
    Blynk.virtualWrite(VPIN_MODE_SWITCH, prevMode);
    controlBuzzer(500);
  }
}

void setup() {
  Serial.begin(115200);
  
  pinMode(RelayPin, OUTPUT);
  pinMode(wifiLed, OUTPUT);
  pinMode(ModeLed, OUTPUT);
  pinMode(BuzzerPin, OUTPUT);
  pinMode(RelayButtonPin, INPUT_PULLUP);
  pinMode(ModeSwitchPin, INPUT_PULLUP);
  pinMode(SensorPin, INPUT);
  pinMode(TDS_PIN, INPUT);

  digitalWrite(wifiLed, LOW);
  digitalWrite(ModeLed, LOW);
  digitalWrite(BuzzerPin, LOW);

  dht.begin();

  config1.setEventHandler(button1Handler);
  config2.setEventHandler(button2Handler);
  button1.init(RelayButtonPin);
  button2.init(ModeSwitchPin);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    while (true);
  }
  delay(1000);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 20);
  display.println("Sensor Init");
  display.display();
  delay(2000);

  WiFi.begin(ssid, pass);
  Blynk.config(BLYNK_AUTH_TOKEN);

  timer.setInterval(2000L, checkBlynkStatus);
  timer.setInterval(3000L, sendSensor);

  controlBuzzer(1000);
  digitalWrite(ModeLed, prevMode);
}

void loop() {
  Blynk.run();
  timer.run();
  button2.check();
  controlMoist();
}
