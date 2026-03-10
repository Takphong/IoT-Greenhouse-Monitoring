#include <M5CoreS3.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ================= WIFI =================
const char* ssid = "name";
const char* password = "pass";
const char* mqtt_server = "broker.emqx.io"; //Change this if use Mosquitto server

const char* data_topic = "greenhouse/fern/data";
const char* control_topic = "greenhouse/fern/control";

WiFiClient espClient;
PubSubClient client(espClient);

// ===== Data From Sender =====
float soil = 0;
float lightL = 0;
float roll = 0;
float pitch = 0;
float yaw = 0;
bool pump = false;

String systemMode = "AUTO";
int screenMode = 0;

unsigned long lastReconnectAttempt = 0;

// ===== Swipe detection =====
int touchStartX = 0;
bool touching = false;

// ================= MQTT CALLBACK =================
void callback(char* topic, byte* payload, unsigned int length) {

  String msg = "";
  for (int i = 0; i < length; i++)
    msg += (char)payload[i];

  String topicStr = String(topic);

  // ===== CONTROL =====
  if (topicStr == control_topic) {

    if (msg == "WATER_ON") {
      systemMode = "MANUAL ON";
      pump = true;
    }
    else if (msg == "WATER_OFF") {
      systemMode = "MANUAL OFF";
      pump = false;
    }
    else if (msg == "AUTO") {
      systemMode = "AUTO";
    }
    return;
  }

  // ===== SENSOR DATA =====
  if (topicStr == data_topic) {

    StaticJsonDocument<512> doc;

    DeserializationError error = deserializeJson(doc, msg);
    if (error) {
      Serial.print("JSON Error: ");
      Serial.println(error.c_str());
      return;
    }

    if (!doc.containsKey("data")) {
      Serial.println("No data object!");
      return;
    }

    JsonObject data = doc["data"];

    soil   = data["soil"]  | soil;
    lightL = data["light"] | lightL;

    //gyro
    roll  = data["gyroX"] | 0.0;
    pitch = data["gyroY"] | 0.0;
    yaw   = data["gyroZ"] | 0.0;

    pump = data["pump"] | pump;

    //this also gyro but me printing
    Serial.println("---- RECEIVED GYRO ----");
    Serial.print("X: "); Serial.println(roll);
    Serial.print("Y: "); Serial.println(pitch);
    Serial.print("Z: "); Serial.println(yaw);
  }
}

// ================= WIFI =================
void connectWiFi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
    delay(500);
}

// ================= MQTT RECONNECT =================
void reconnectMQTT() {
  if (client.connect("FernMonitor")) {
    client.subscribe(data_topic);
    client.subscribe(control_topic);
  }
}

// ================= DISPLAY =================
void displayScreen() {

  M5.Lcd.clear();
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(10, 10);

  // WiFi status
  if (WiFi.status() == WL_CONNECTED)
    M5.Lcd.setTextColor(GREEN);
  else
    M5.Lcd.setTextColor(RED);

  M5.Lcd.println("WiFi");

  // Mode
  M5.Lcd.setCursor(10, 30);
  if (systemMode == "AUTO")
    M5.Lcd.setTextColor(GREEN);
  else if (systemMode == "MANUAL ON")
    M5.Lcd.setTextColor(BLUE);
  else
    M5.Lcd.setTextColor(RED);

  M5.Lcd.printf("Mode: %s\n\n", systemMode.c_str());

  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.println("--------------------------\n");
  M5.Lcd.setCursor(10, 100);

  if (screenMode == 0) {
    M5.Lcd.setTextColor(RED);
    M5.Lcd.println("PAGE 1\n");
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.println("--------------------------\n");
    M5.Lcd.setTextColor(GREENYELLOW);
    M5.Lcd.printf("Soil: %.0f %%\n", soil);
  }

  else if (screenMode == 1) {
    M5.Lcd.setTextColor(ORANGE);
    M5.Lcd.println("PAGE 2\n");
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.println("--------------------------\n");
    M5.Lcd.setTextColor(DARKCYAN);
    M5.Lcd.printf("Roll: %.1f\n", roll);
    M5.Lcd.setTextColor(OLIVE);
    M5.Lcd.printf("Pitch: %.1f\n", pitch);
    M5.Lcd.setTextColor(PURPLE);
    M5.Lcd.printf("Yaw: %.1f\n", yaw);
  }

  else if (screenMode == 2) {
    M5.Lcd.setTextColor(YELLOW);
    M5.Lcd.println("PAGE 3\n");
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.println("--------------------------\n");
    M5.Lcd.setTextColor(LIGHTGREY);
    M5.Lcd.printf("Light: %.0f\n", lightL);
    M5.Lcd.setTextColor(CYAN);
    M5.Lcd.printf("Pump: %s\n", pump ? "ON" : "OFF");
  }
}

// ================= SETUP =================
void setup() {

  M5.begin();
  Serial.begin(115200);

  connectWiFi();

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

// ================= LOOP =================
void loop() {

  M5.update();

  // ===== Swipe detection =====
  auto t = M5.Touch.getDetail();

  if (t.isPressed() && !touching) {
    touchStartX = t.x;
    touching = true;
  }

  if (!t.isPressed() && touching) {

    int diff = t.x - touchStartX;

    if (diff > 80) {
      screenMode++;
      if (screenMode > 2) screenMode = 0;
    }
    else if (diff < -80) {
      screenMode--;
      if (screenMode < 0) screenMode = 2;
    }

    touching = false;
  }

  // MQTT
  if (WiFi.status() != WL_CONNECTED)
    connectWiFi();

  if (!client.connected()) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > 3000) {
      lastReconnectAttempt = now;
      reconnectMQTT();
    }
  } else {
    client.loop();
  }

  displayScreen();
  delay(200);
}
