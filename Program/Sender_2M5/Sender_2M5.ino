#include <M5CoreS3.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "mbedtls/sha256.h"

// ================= WIFI =================
const char* ssid = "「JAITP」";
const char* password = "MTFKISAS";

// ================= MQTT =================
const char* mqtt_server = "broker.emqx.io";
const int   mqtt_port   = 1883;

const char* pub_topic = "greenhouse/fern/data";
const char* sub_topic = "greenhouse/fern/control";

WiFiClient espClient;
PubSubClient client(espClient);

// ================= PINS =================
#define SOIL_PIN 1
#define LIGHT_PIN 2
#define PUMP_PIN 3

// ================= VARIABLES =================
float soilMoisture;
float temperature;
float lightLevel;

float gyroX;
float gyroY;
float gyroZ;

bool pumpState = false;
bool autoMode = true;

unsigned long lastSend = 0;

// ================= HASH FUNCTION =================
String generateHash(String data) {
  byte hash[32];
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx, (const unsigned char*)data.c_str(), data.length());
  mbedtls_sha256_finish(&ctx, hash);
  mbedtls_sha256_free(&ctx);

  String result = "";
  for (int i = 0; i < 32; i++) {
    char buf[3];
    sprintf(buf, "%02x", hash[i]);
    result += buf;
  }
  return result;
}

// ================= MQTT CALLBACK =================
void callback(char* topic, byte* payload, unsigned int length) {

  String msg;
  for (int i = 0; i < length; i++)
    msg += (char)payload[i];

  Serial.print("Message received: ");
  Serial.println(msg);

  if (msg == "WATER_ON") {
    pumpState = true;
    autoMode = false;
  }
  else if (msg == "WATER_OFF") {
    pumpState = false;
    autoMode = false;
  }
  else if (msg == "AUTO") {
    autoMode = true;
  }
}

// ================= WIFI CONNECT =================
void connectWiFi() {

  WiFi.begin(ssid, password);

  Serial.println("Connecting to WiFi...");

  M5.Lcd.clear();
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(ORANGE);
  M5.Lcd.setCursor(40, 120);
  M5.Lcd.println("Connecting WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected!");
  Serial.println(WiFi.localIP());

  M5.Lcd.clear();
  M5.Lcd.setCursor(40, 120);
  M5.Lcd.setTextColor(GREEN);
  M5.Lcd.println("WiFi Connected!");
  delay(1000);
}

// ================= MQTT RECONNECT =================
void reconnectMQTT() {

  while (!client.connected()) {

    Serial.println("Connecting to MQTT...");

    if (client.connect("FernPot_Client")) {
      Serial.println("MQTT Connected!");
      client.subscribe(sub_topic);
    } else {
      Serial.print("Failed, rc=");
      Serial.println(client.state());
      delay(2000);
    }
  }
}

// ================= READ SENSORS =================
void readSensors() {

  soilMoisture = analogRead(SOIL_PIN);
  lightLevel   = analogRead(LIGHT_PIN);
  temperature  = random(250, 350) / 10.0;

  float gx, gy, gz;
  M5.Imu.getGyroData(&gx, &gy, &gz);

  gyroX = gx;
  gyroY = gy;
  gyroZ = gz;

  if (autoMode) {
    if (soilMoisture < 1500)
      pumpState = true;
    else
      pumpState = false;
  }
}

// ================= SEND DATA =================
void sendData() {

  StaticJsonDocument<512> finalDoc;
  JsonObject data = finalDoc.createNestedObject("data");

  data["soil"]  = soilMoisture;
  data["temp"]  = temperature;
  data["light"] = lightLevel;

  // 🔥 Send full gyro values
  data["gyroX"] = gyroX;
  data["gyroY"] = gyroY;
  data["gyroZ"] = gyroZ;

  data["pump"]  = pumpState;

  String payload;
  serializeJson(data, payload);

  String hash = generateHash(payload);
  finalDoc["hash"] = hash;

  String finalMsg;
  serializeJson(finalDoc, finalMsg);

  client.publish(pub_topic, finalMsg.c_str());

  Serial.println("Published:");
  Serial.println(finalMsg);
}

// ================= SETUP =================
void setup() {

  Serial.begin(115200);

  M5.begin();
  M5.Imu.begin();

  pinMode(PUMP_PIN, OUTPUT);

  connectWiFi();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  reconnectMQTT();
}

// ================= LOOP =================
void loop() {

  M5.update();

  if (WiFi.status() != WL_CONNECTED)
    connectWiFi();

  if (!client.connected())
    reconnectMQTT();

  client.loop();

  readSensors();
  digitalWrite(PUMP_PIN, pumpState);

  if (millis() - lastSend > 5000) {
    lastSend = millis();
    sendData();
  }
}