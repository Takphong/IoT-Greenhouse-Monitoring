#include <M5CoreS3.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "mbedtls/sha256.h"

const char* ssid = "Nope";
const char* password = "Nuh-uh";
const char* mqtt_server = "192.168.1.100";

const char* sub_topic = "greenhouse/fern/data";
const char* pub_topic = "greenhouse/fern/control";

WiFiClient espClient;
PubSubClient client(espClient);

float soil, temp, lightL, tiltY;
bool pump;
int mode = 0;

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

void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (int i = 0; i < length; i++) msg += (char)payload[i];

  StaticJsonDocument<512> doc;
  deserializeJson(doc, msg);

  String receivedHash = doc["hash"];
  String rawData;
  serializeJson(doc["data"], rawData);

  if (generateHash(rawData) == receivedHash) {
    soil = doc["data"]["soil"];
    temp = doc["data"]["temp"];
    lightL = doc["data"]["light"];
    tiltY = doc["data"]["tiltY"];
    pump = doc["data"]["pump"];
  }
}

void connectWiFi() {
  WiFi.begin(ssid, password);

  M5.Lcd.clear();
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(ORANGE);
  M5.Lcd.setCursor(10, 100);
  M5.Lcd.println("Connecting WiFi");

  const char spinner[] = {'-', '\\', '|', '/'};
  int i = 0;

  while (WiFi.status() != WL_CONNECTED) {
    M5.Lcd.setCursor(10, 140);
    M5.Lcd.printf("Waiting for connection %c ", spinner[i % 4]);

    i++;
    delay(300);
  }

  M5.Lcd.clear();
  M5.Lcd.setCursor(10, 120);
  M5.Lcd.println("WiFi Connected!");
  delay(1000);
}

void reconnectMQTT() {
  while (!client.connected()) {
    if (client.connect("FernMonitor")) {
      client.subscribe(sub_topic);
    } else {
      delay(2000);
    }
  }
}

void displayMode() {
  M5.Lcd.clear();

  if (mode == 0) {
    M5.Lcd.printf("Soil: %.0f\nTemp: %.1f\n", soil, temp);
  }
  if (mode == 1) {
    M5.Lcd.printf("Light: %.0f\nTiltY: %.2f\n", lightL, tiltY);
  }
  if (mode == 2) {
    M5.Lcd.printf("Pump: %s\n", pump ? "ON" : "OFF");
  }
}

void setup() {
  M5.begin();
  connectWiFi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void loop() {
  M5.update();

  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!client.connected()) reconnectMQTT();
  client.loop();

  if (M5.BtnA.wasPressed()) {
    mode++;
    if (mode > 2) mode = 0;
  }

  if (M5.BtnB.wasPressed()) {
    client.publish(pub_topic, "WATER_ON");
  }

  if (M5.BtnC.wasPressed()) {
    client.publish(pub_topic, "WATER_OFF");
  }

  displayMode();
}
