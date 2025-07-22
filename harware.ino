#include <WiFi.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include "DHT.h"

// WiFi credentials
const char* ssid = "TANSAM";
const char* password = "T@n$@m5.0";

// HiveMQ broker (no auth)
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

// RFID setup
#define SS_PIN 5
#define RST_PIN 22
MFRC522 rfid(SS_PIN, RST_PIN);

// DHT11 setup
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// Mode: input or output (controlled from Node-RED)
String mode = "input";

// Timers
unsigned long lastDHTSend = 0;

void setup() {
  Serial.begin(115200);
  SPI.begin();
  rfid.PCD_Init();
  dht.begin();

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);
}

void loop() {
  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop();

  // Send temperature and humidity every 5 seconds
  if (millis() - lastDHTSend > 5000) {
    float temp = dht.readTemperature();
    float hum = dht.readHumidity();

    if (!isnan(temp) && !isnan(hum)) {
      String dhtPayload = "{\"temperature\":" + String(temp, 1) + ",\"humidity\":" + String(hum, 1) + "}";
      client.publish("warehouse/dht", dhtPayload.c_str());
      Serial.println("Sent DHT data: " + dhtPayload);
    }
    lastDHTSend = millis();
  }

  // RFID card detected
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    unsigned long decimalUID = 0;
    for (byte i = 0; i < rfid.uid.size; i++) {
      decimalUID = (decimalUID << 8) | rfid.uid.uidByte[i];
    }
String rfidPayload = "{\"uid\":\"" + String(decimalUID) + "\",\"mode\":\"" + mode + "\"}";
    client.publish("warehouse/rfid", rfidPayload.c_str());
    Serial.println("Sent RFID: " + rfidPayload);

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    delay(1000); // debounce
  }
}

// Handle incoming mode message
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  if (String(topic) == "warehouse/mode") {
    mode = msg;
    Serial.println("Mode changed to: " + mode);
  }
}

// Reconnect to MQTT broker if needed
void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT...");
    String clientId = "ESP32Client_" + String(random(1000, 9999));
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      client.subscribe("warehouse/mode"); // Subscribing to input/output toggle
    } else {
      Serial.print(" failed, rc=");
      Serial.print(client.state());
      delay(2000);
    }
  }
}