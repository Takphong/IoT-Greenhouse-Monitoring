#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "mbedtls/sha256.h"
#include "DHT.h"

// ================= WIFI =================
const char* ssid = "「JAITP」";
const char* password = "MTFKISAS";

// ================= MQTT =================
const char* mqtt_server = "broker.emqx.io";
const int mqtt_port = 1883;

const char* pub_topic = "greenhouse/fern/data";
const char* sub_topic = "greenhouse/fern/control";

WiFiClient espClient;
PubSubClient client(espClient);

// ================= PINS =================
#define DHTPIN 4
#define DHTTYPE DHT11
#define FLAME_PIN 15
#define BUTTON_PIN 18
#define PUMP_PIN 27   // MOSFET control

DHT dht(DHTPIN, DHTTYPE);

// ================= VARIABLES =================
float temperature;
float humidity;
bool flameDetected;

bool pumpState = false;
bool lastButtonState = HIGH;

// MODE
bool autoMode = true;

// BUTTON HOLD
unsigned long buttonPressStart = 0;
bool buttonHolding = false;

// AUTO CYCLE
unsigned long pumpTimer = 0;
bool pumpCycleState = false;

unsigned long lastSend = 0;
unsigned long lastPrint = 0;   // for slower printing

// ================= HASH =================
String generateHash(String data) {
  byte hash[32];
  mbedtls_sha256_context ctx;

  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx,(const unsigned char*)data.c_str(),data.length());
  mbedtls_sha256_finish(&ctx, hash);
  mbedtls_sha256_free(&ctx);

  String result="";
  for(int i=0;i<32;i++){
    char buf[3];
    sprintf(buf,"%02x",hash[i]);
    result+=buf;
  }
  return result;
}

// ================= MQTT CALLBACK =================
void callback(char* topic, byte* payload, unsigned int length){
  String msg;

  for(int i=0;i<length;i++)
    msg += (char)payload[i];

  Serial.println("----- RECEIVED COMMAND -----");
  Serial.print("Message: ");
  Serial.println(msg);

  if (!autoMode) {
    if(msg=="WATER_ON") pumpState=true;
    if(msg=="WATER_OFF") pumpState=false;
  }
}

// ================= WIFI =================
void connectWiFi(){
  WiFi.begin(ssid,password);

  Serial.print("Connecting WiFi");
  while(WiFi.status()!=WL_CONNECTED){
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
}

// ================= MQTT =================
void reconnectMQTT(){
  while(!client.connected()){
    Serial.println("Connecting MQTT...");
    if(client.connect("FernPot_ESP32")){
      Serial.println("MQTT Connected!");
      client.subscribe(sub_topic);
    }else{
      Serial.println("MQTT Failed, retry...");
      delay(2000);
    }
  }
}

// ================= READ SENSORS =================
void readSensors(){

  temperature = dht.readTemperature();
  humidity = dht.readHumidity();

  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("DHT FAILED!");
    return;
  }

  flameDetected = digitalRead(FLAME_PIN) == LOW;

  bool buttonState = digitalRead(BUTTON_PIN);

  // ===== LONG PRESS =====
  if (buttonState == LOW && lastButtonState == HIGH) {
    buttonPressStart = millis();
    buttonHolding = true;
  }

  if (buttonState == LOW && buttonHolding) {
    if (millis() - buttonPressStart > 2000) {
      autoMode = !autoMode;
      buttonHolding = false;

      Serial.println("=== MODE CHANGED ===");
      Serial.println(autoMode ? "AUTO MODE" : "MANUAL MODE");
    }
  }

  // ===== SHORT PRESS =====
  if (buttonState == HIGH && lastButtonState == LOW) {
    if (!autoMode) {
      pumpState = !pumpState;
      Serial.println("Manual toggle pump");
    }
    buttonHolding = false;
  }

  lastButtonState = buttonState;

  // ===== PRINT (SLOWER) =====
  if (millis() - lastPrint > 2000) {
    lastPrint = millis();

    Serial.println("----- SENSOR READ -----");
    Serial.print("Temperature: "); Serial.println(temperature);
    Serial.print("Humidity: "); Serial.println(humidity);
    Serial.print("Flame: "); Serial.println(flameDetected ? "YES" : "NO");
    Serial.print("Mode: "); Serial.println(autoMode ? "AUTO" : "MANUAL");
    Serial.print("Pump: "); Serial.println(pumpState ? "ON" : "OFF");
  }
}

// ================= SEND DATA =================
void sendData(){

  StaticJsonDocument<256> finalDoc;
  JsonObject data = finalDoc.createNestedObject("data");

  data["temp"] = temperature;
  data["humidity"] = humidity;
  data["flame"] = flameDetected;
  data["led"] = pumpState;
  data["mode"] = autoMode ? "AUTO" : "MANUAL";

  String payload;
  serializeJson(finalDoc, payload);

  String hash = generateHash(payload);
  finalDoc["hash"] = hash;

  String finalMsg;
  serializeJson(finalDoc, finalMsg);

  client.publish(pub_topic, finalMsg.c_str());

  // 🔥 CLEAR PRINT
  Serial.println("===== MQTT SEND =====");
  Serial.print("Topic: ");
  Serial.println(pub_topic);

  Serial.print("Payload: ");
  Serial.println(finalMsg);
  Serial.println("=====================");
}

// ================= SETUP =================
void setup(){

  Serial.begin(115200);

  pinMode(PUMP_PIN,OUTPUT);
  pinMode(FLAME_PIN,INPUT);
  pinMode(BUTTON_PIN,INPUT_PULLUP);

  digitalWrite(PUMP_PIN, LOW);

  dht.begin();

  connectWiFi();

  client.setServer(mqtt_server,mqtt_port);
  client.setCallback(callback);

  reconnectMQTT();
}

// ================= LOOP =================
void loop(){

  if(WiFi.status()!=WL_CONNECTED)
    connectWiFi();

  if(!client.connected())
    reconnectMQTT();

  client.loop();

  readSensors();

  // ===== AUTO MODE =====
  if (autoMode) {

    unsigned long now = millis();

    if (!pumpCycleState && now - pumpTimer > 10000) {
      pumpCycleState = true;
      pumpTimer = now;
      Serial.println("AUTO: Pump ON");
    }
    else if (pumpCycleState && now - pumpTimer > 5000) {
      pumpCycleState = false;
      pumpTimer = now;
      Serial.println("AUTO: Pump OFF");
    }

    pumpState = pumpCycleState;
  }

  // ===== APPLY OUTPUT =====
  digitalWrite(PUMP_PIN, pumpState);

  // ===== SEND MQTT =====
  if(millis()-lastSend>5000){
    lastSend=millis();
    sendData();
  }
}
