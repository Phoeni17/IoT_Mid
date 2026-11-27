#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include "config.h"

#define DHTTYPE DHT22

DHT dht(PIN_DHT22, DHTTYPE);
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
MFRC522 mfrc522(PIN_RFID_SS, PIN_RFID_RST);

void writeServoAngle(int angle) {
  angle = constrain(angle, 0, 180);
  uint32_t pulseUs = map(angle, 0, 180, 500, 2400);
  uint32_t duty = (pulseUs * 16384UL) / 20000UL;
  ledcWrite(PIN_SERVO, duty);
}

WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);

bool isUnlocked = false;
unsigned long lastSensorReadMs = 0;
unsigned long lastMqttReconnectAttemptMs = 0;
unsigned long feedbackUntilMs = 0;
bool showingWrongCard = false;
bool showingGrantedBanner = false;

float currentTemp = NAN;
float currentHumidity = NAN;
int currentGas = 0;
int currentLdr = 0;

// Common-anode RGB LED: LOW = ON, HIGH = OFF
void setRGB(bool red, bool green, bool blue) {
  digitalWrite(PIN_RGB_R, red ? LOW : HIGH);
  digitalWrite(PIN_RGB_G, green ? LOW : HIGH);
  digitalWrite(PIN_RGB_B, blue ? LOW : HIGH);
}

void updateOLED() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  // Header: title + connection status
  display.setCursor(0, 0);
  display.print("IoT_Mid");

  display.setCursor(74, 0);
  if (WiFi.status() != WL_CONNECTED) {
    display.print("No WiFi");
  } else if (!mqttClient.connected()) {
    display.print("MQTT:OFF");
  } else {
    display.print("MQTT:ON");
  }

  display.drawLine(0, 10, OLED_WIDTH - 1, 10, SSD1306_WHITE);

  if (!isUnlocked) {
    if (showingWrongCard) {
      display.setTextSize(1);
      display.setCursor(0, 22);
      display.println("WRONG CARD!");
      display.setCursor(0, 38);
      display.println("Access Denied");
    } else {
      display.setTextSize(2);
      display.setCursor(0, 20);
      display.println("LOCKED");
      display.setTextSize(1);
      display.setCursor(0, 44);
      display.println("Scan RFID Card...");
    }
    display.display();
    return;
  }

  if (showingGrantedBanner) {
    display.setTextSize(1);
    display.setCursor(0, 22);
    display.println("CARD ACCEPTED!");
    display.setCursor(0, 38);
    display.println("System Unlocked");
    display.display();
    return;
  }

  // Unlocked live sensor parameters
  display.setCursor(0, 14);
  display.print("Temp: ");
  if (isnan(currentTemp)) {
    display.print("--.- C");
  } else {
    display.print(currentTemp, 1);
    display.print(" C");
  }

  display.setCursor(0, 26);
  display.print("Hum : ");
  if (isnan(currentHumidity)) {
    display.print("--.- %");
  } else {
    display.print(currentHumidity, 1);
    display.print(" %");
  }

  display.setCursor(0, 38);
  display.print("Gas : ");
  display.print(currentGas);

  display.setCursor(0, 50);
  display.print("LDR : ");
  display.print(currentLdr);

  display.display();
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  msg.reserve(length);
  for (unsigned int i = 0; i < length; i++) {
    msg += static_cast<char>(payload[i]);
  }
  msg.trim();
  msg.toLowerCase();

  Serial.printf("MQTT [%s]: %s\n", topic, msg.c_str());

  if (String(topic) == TOPIC_SERVO) {
    if (!isUnlocked) {
      Serial.println("System LOCKED - ignoring servo command.");
      return;
    }

    if (msg == "open") {
      writeServoAngle(0);
      Serial.println("Servo -> OPEN (0 deg)");
    } else if (msg == "close") {
      writeServoAngle(180);
      Serial.println("Servo -> CLOSE (180 deg)");
    } else {
      int angle = msg.toInt();
      if (angle >= 0 && angle <= 180 && (msg == "0" || angle > 0)) {
        writeServoAngle(angle);
        Serial.printf("Servo -> %d deg\n", angle);
      } else {
        Serial.println("Unknown servo payload (use 'open', 'close', or 0-180).");
      }
    }
  }
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.print("Connecting to Wi-Fi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL);

  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Wi-Fi connected. IP: ");
  Serial.println(WiFi.localIP());
}

bool reconnectMQTT() {
  Serial.print("Connecting to EMQX TLS broker...");
  if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
    Serial.println(" connected!");
    mqttClient.subscribe(TOPIC_SERVO);
    Serial.print("Subscribed to: ");
    Serial.println(TOPIC_SERVO);
    return true;
  }
  Serial.print(" failed, rc=");
  Serial.println(mqttClient.state());
  return false;
}

bool isAuthorizedUID(const byte* uid, byte uidSize) {
  if (uidSize != sizeof(AUTHORIZED_UID)) return false;
  for (byte i = 0; i < uidSize; i++) {
    if (uid[i] != AUTHORIZED_UID[i]) return false;
  }
  return true;
}

void checkRFID() {
  if (isUnlocked) return;
  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  Serial.print("Scanned Card UID:");
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    Serial.printf(" %02X", mfrc522.uid.uidByte[i]);
  }
  Serial.println();

  if (isAuthorizedUID(mfrc522.uid.uidByte, mfrc522.uid.size)) {
    Serial.println("Card AUTHORIZED! Unlocking system...");
    isUnlocked = true;
    showingWrongCard = false;
    showingGrantedBanner = true;
    feedbackUntilMs = millis() + 1200;

    setRGB(false, true, false); // Hold Green
    tone(PIN_BUZZER, 1600, 250); // High tone
    updateOLED();
  } else {
    Serial.println("Card UNAUTHORIZED! Access denied.");
    showingWrongCard = true;
    feedbackUntilMs = millis() + 1500;

    setRGB(true, false, false); // Red
    tone(PIN_BUZZER, 300, 400); // Low tone
    updateOLED();
  }

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}

void readAndPublishSensors() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  int gasRaw = analogRead(PIN_MQ2_AOUT);
  int ldrRaw = analogRead(PIN_LDR_AO);

  currentGas = gasRaw;
  currentLdr = ldrRaw;

  if (isnan(h) || isnan(t)) {
    Serial.println("Failed to read from DHT22 sensor!");
  } else {
    currentTemp = t;
    currentHumidity = h;
  }

  Serial.printf("Temp: %.1f C | Hum: %.1f %% | Gas: %d | LDR: %d\n",
                currentTemp, currentHumidity, currentGas, currentLdr);

  if (mqttClient.connected()) {
    char buf[16];

    if (!isnan(currentTemp)) {
      dtostrf(currentTemp, 1, 2, buf);
      mqttClient.publish(TOPIC_TEMPERATURE, buf);
    }
    if (!isnan(currentHumidity)) {
      dtostrf(currentHumidity, 1, 2, buf);
      mqttClient.publish(TOPIC_HUMIDITY, buf);
    }

    snprintf(buf, sizeof(buf), "%d", currentGas);
    mqttClient.publish(TOPIC_GAS, buf);

    snprintf(buf, sizeof(buf), "%d", currentLdr);
    mqttClient.publish(TOPIC_LDR, buf);
  }

  updateOLED();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== IoT_Mid Booting ===");

  // RGB LED (Common Anode) -> start Blue (locked, waiting for card)
  pinMode(PIN_RGB_R, OUTPUT);
  pinMode(PIN_RGB_G, OUTPUT);
  pinMode(PIN_RGB_B, OUTPUT);
  setRGB(false, false, true);

  pinMode(PIN_BUZZER, OUTPUT);

  // Servo (50 Hz, 14-bit LEDC PWM)
  ledcAttach(PIN_SERVO, 50, 14);
  writeServoAngle(0);
  Serial.println("Servo initialized (0 deg).");

  // I2C & SSD1306 OLED
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("SSD1306 allocation failed!");
    for (;;) {
      delay(1000);
    }
  }

  updateOLED();

  // SPI & MFRC522 RFID (let MFRC522 manage SS pin directly as GPIO)
  SPI.begin(PIN_RFID_SCK, PIN_RFID_MISO, PIN_RFID_MOSI);
  delay(50);
  mfrc522.PCD_Init();
  mfrc522.PCD_DumpVersionToSerial();
  Serial.println("RFID Reader initialized. Waiting for authorized card...");

  // Sensors
  dht.begin();
  analogReadResolution(12); // 0 - 4095 for ESP32-S3 ADC

  // Wi-Fi & EMQX TLS MQTT
  connectWiFi();
  espClient.setInsecure();
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  updateOLED();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  if (!mqttClient.connected()) {
    unsigned long now = millis();
    if (now - lastMqttReconnectAttemptMs >= 5000) {
      lastMqttReconnectAttemptMs = now;
      if (reconnectMQTT()) {
        lastMqttReconnectAttemptMs = 0;
        updateOLED();
      }
    }
  } else {
    mqttClient.loop();
  }

  // Handle temporary RFID feedback timers (wrong card Red -> Blue, or granted banner -> sensors)
  unsigned long now = millis();
  if (showingWrongCard && now >= feedbackUntilMs) {
    showingWrongCard = false;
    setRGB(false, false, true); // Back to Blue
    updateOLED();
  }
  if (showingGrantedBanner && now >= feedbackUntilMs) {
    showingGrantedBanner = false;
    lastSensorReadMs = now - SENSOR_INTERVAL_MS; // Trigger immediate first sensor read
  }

  checkRFID();

  if (isUnlocked && !showingGrantedBanner) {
    if (now - lastSensorReadMs >= SENSOR_INTERVAL_MS) {
      lastSensorReadMs = now;
      readAndPublishSensors();
    }
  }
}
