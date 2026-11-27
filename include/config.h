#pragma once

// ==========================================
// Wi-Fi Configuration (Wokwi Virtual Wi-Fi)
// ==========================================
#define WIFI_SSID     "Wokwi-GUEST"
#define WIFI_PASSWORD ""
#define WIFI_CHANNEL  6

// ==========================================
// EMQX Serverless MQTT Broker (TLS / SSL)
// ==========================================
// Replace with your EMQX Cloud Serverless deployment host, username, and password
#define MQTT_BROKER   "l0600bab.ala.asia-southeast1.emqxsl.com"
#define MQTT_PORT     8883
#define MQTT_USER     "Phonesy"
#define MQTT_PASSWORD "okay"
#define MQTT_CLIENT_ID "esp32s3-iot-mid"

// ==========================================
// MQTT Topics (Separate per sensor parameter)
// ==========================================
#define TOPIC_TEMPERATURE "iot_mid/dht22/temperature"
#define TOPIC_HUMIDITY    "iot_mid/dht22/humidity"
#define TOPIC_GAS         "iot_mid/mq2/gas"
#define TOPIC_LDR         "iot_mid/ldr/light"
#define TOPIC_SERVO       "iot_mid/servo/angle"

// ==========================================
// Hardware Pin Definitions (from diagram.json)
// ==========================================
#define PIN_DHT22     18
#define PIN_MQ2_AOUT  6
#define PIN_LDR_AO    7
#define PIN_OLED_SDA  10
#define PIN_OLED_SCL  9

#define PIN_SERVO     37
#define PIN_BUZZER    38

// Common-Anode RGB LED (COM -> 3V3)
#define PIN_RGB_R     45
#define PIN_RGB_G     48
#define PIN_RGB_B     47

// MFRC522 RFID (SPI)
#define PIN_RFID_SS   4
#define PIN_RFID_RST  5
#define PIN_RFID_SCK  12
#define PIN_RFID_MOSI 11
#define PIN_RFID_MISO 13

// Authorized RFID Card UID (4 bytes)
// Every scanned UID is printed to Serial so you can update this if needed.
static const uint8_t AUTHORIZED_UID[4] = {0x01, 0x02, 0x03, 0x04};

#define OLED_WIDTH    128
#define OLED_HEIGHT   64
#define OLED_ADDR     0x3C

// Publish / read interval in milliseconds
#define SENSOR_INTERVAL_MS 2000
