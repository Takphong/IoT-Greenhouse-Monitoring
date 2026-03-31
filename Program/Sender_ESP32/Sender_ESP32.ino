//const char* ssid = "「JAITP」";
//const char* password = "MTFKISAS";

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "DHT.h"

// ================= WIFI =================
const char* ssid = "Redmi 15 5G";
const char* password = "ROME2011";

// ================= MQTT =================
const char* mqtt_server = "broker.emqx.io";
const int mqtt_port = 1883;

const char* pub_topic = "greenhouse/fern/data";
const char* sub_topic = "greenhouse/fern/control";
const char* mode_topic = "greenhouse/fern/mode";
const char* button_topic = "greenhouse/fern/button";

WiFiClient espClient;
PubSubClient client(espClient);

// ================= PINS =================
#define DHTPIN 4
#define DHTTYPE DHT11
#define FLAME_PIN 15
#define BUTTON_PIN 18
#define PUMP_PIN 17
#define LED_ON_PIN 25
#define LED_OFF_PIN 26

DHT dht(DHTPIN, DHTTYPE);

// ================= VARIABLES =================
float temperature;
float humidity;
bool flameDetected;

bool pumpState = false;
bool lastButtonState = HIGH;

bool autoMode = true;

// BUTTON HOLD
unsigned long buttonPressStart = 0;
bool buttonHolding = false;
bool longPressTriggered = false;

// AUTO CYCLE
unsigned long pumpTimer = 0;
bool pumpCycleState = false;

unsigned long lastSend = 0;
unsigned long lastPrint = 0;

unsigned long lastButtonTime = 0;
const unsigned long debounceDelay = 150;

float lastTemperature = 0;
float lastHumidity = 0;

// ================= Checksum int =================
uint16_t generateChecksum(String data) {
  uint16_t sum = 0;

  for (int i = 0; i < data.length(); i++) {
    sum += (uint8_t)data[i];
  }

  return sum;
}

// ================= MQTT CALLBACK =================
void callback(char* topic, byte* payload, unsigned int length){

  String msg;
  for(int i=0;i<length;i++)
    msg += (char)payload[i];

  String topicStr = String(topic);

  Serial.println("----- RECEIVED -----");
  Serial.print("Topic: "); Serial.println(topicStr);
  Serial.print("Message: "); Serial.println(msg);

  // MODE CONTROL
  if (topicStr == mode_topic) {
    if (msg == "AUTO") {
      autoMode = true;
      Serial.println("Switched to AUTO mode");
    }
    else if (msg == "MANUAL") {
      autoMode = false;
      Serial.println("Switched to MANUAL mode");
    }
    return;
  }

  // PUMP CONTROL (ONLY IN MANUAL)
  if (topicStr == sub_topic) {
    if (!autoMode) {
      if(msg=="WATER_ON") pumpState=true;
      if(msg=="WATER_OFF") pumpState=false;
    }
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
      client.subscribe(mode_topic);   // NEW

    }else{
      Serial.println("MQTT Failed, retry...");
      delay(2000);
    }
  }
}

// ================= READ SENSORS =================
void readSensors(){

  float newTemp = dht.readTemperature();
  float newHum = dht.readHumidity();

  bool dhtFail = false;

  if (isnan(newTemp) || isnan(newHum)) {
    dhtFail = true;

    //  keep old values
    temperature = lastTemperature;
    humidity = lastHumidity;

  } else {

    // update values
    temperature = newTemp;
    humidity = newHum;

    lastTemperature = newTemp;
    lastHumidity = newHum;
  }

  if (temperature >= 50) {
    flameDetected = true;
  } else {
    flameDetected = false;
  }

  bool buttonState = digitalRead(BUTTON_PIN);

  // debounce protection
  if (millis() - lastButtonTime < debounceDelay) {
   lastButtonState = buttonState;
   return;
  }

  // LONG PRESS → CHANGE MODE
  if (buttonState == LOW && lastButtonState == HIGH) {
    lastButtonTime = millis();
    buttonPressStart = millis();
    buttonHolding = true;
  }

  if (buttonState == LOW && buttonHolding) {
    if (millis() - buttonPressStart > 2000) {

      autoMode = !autoMode;

     Serial.println("=== MODE CHANGED ===");
    Serial.println(autoMode ? "AUTO" : "MANUAL");

      client.publish(mode_topic, autoMode ? "AUTO" : "MANUAL");

      buttonHolding = false;
     longPressTriggered = true;   // 🔥 IMPORTANT
    }
  }

// SHORT PRESS → MANUAL CONTROL
if (buttonState == HIGH && lastButtonState == LOW) {

   // ❌ IGNORE if it was long press
    if (longPressTriggered) {
     longPressTriggered = false;
   } else {

     // SEND BUTTON EVENT TO NODE-RED
      StaticJsonDocument<64> doc;
      doc["event"] = "button";
      doc["action"] = "toggle";

     String msg;
     serializeJson(doc, msg);
     client.publish(button_topic, msg.c_str());

     // Only control pump in MANUAL mode
      if (!autoMode) {
       pumpState = !pumpState;
       Serial.println("Manual toggle pump");
     }
    }

   buttonHolding = false;
  }
  lastButtonState = buttonState;

  // PRINT
  if (millis() - lastPrint > 2000) {
    lastPrint = millis();

    Serial.println("----- SENSOR READ -----");
    Serial.print("Temperature: ");
    if(dhtFail) Serial.println("DHT FAIL");
    else Serial.println(temperature);

    Serial.print("Humidity: ");
    if(dhtFail) Serial.println("DHT FAIL");
    else Serial.println(humidity);
    Serial.print("Flame: "); Serial.println(flameDetected ? "YES" : "NO");
    Serial.print("Mode: "); Serial.println(autoMode ? "AUTO" : "MANUAL");
    Serial.print("Pump: "); Serial.println(pumpState ? "ON" : "OFF");
  }
}

// ================= SEND DATA =================
void sendData(){

  // Create DATA only
  StaticJsonDocument<192> dataDoc;

  dataDoc["temp"] = temperature;
  dataDoc["humidity"] = humidity;
  dataDoc["flame"] = flameDetected;
  dataDoc["led"] = pumpState;
  dataDoc["mode"] = autoMode ? "AUTO" : "MANUAL";

  String dataStr;
  serializeJson(dataDoc, dataStr);

  // Generate checksum from DATA ONLY
  uint16_t checksum = generateChecksum(dataStr);

  // Create FINAL message
  StaticJsonDocument<384> finalDoc;
  finalDoc["data"] = dataDoc;
  finalDoc["checksum"] = checksum;

  String finalMsg;
  serializeJson(finalDoc, finalMsg);

  // Send
  client.publish(pub_topic, finalMsg.c_str());

  Serial.println("===== MQTT SEND =====");
  Serial.println(finalMsg);
}

// ================= SETUP =================
void setup(){

  Serial.begin(115200);

  pinMode(PUMP_PIN,OUTPUT);
  pinMode(FLAME_PIN,INPUT);
  pinMode(BUTTON_PIN,INPUT_PULLUP);

  // Force DAC pins to digital mode
  pinMode(LED_ON_PIN, OUTPUT);
  pinMode(LED_OFF_PIN, OUTPUT);
  dacWrite(LED_ON_PIN, 0);  // Reset DAC
  dacWrite(LED_OFF_PIN, 0); // Reset DAC

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

  // AUTO MODE
  if (autoMode) {

    unsigned long now = millis();

    if (!pumpCycleState && now - pumpTimer > 10000) {
      pumpCycleState = true;
      pumpTimer = now;
      Serial.println("AUTO: Pump ON");
    }
    else if (pumpCycleState && now - pumpTimer > 15000) {
      pumpCycleState = false;
      pumpTimer = now;
      Serial.println("AUTO: Pump OFF");
    }

    pumpState = pumpCycleState;
  }

  digitalWrite(LED_ON_PIN, pumpState);
  digitalWrite(LED_OFF_PIN, !pumpState);
  digitalWrite(PUMP_PIN, pumpState ? LOW : HIGH);
  delay(500);

  if(millis()-lastSend>2000){
    lastSend=millis();
    sendData();
  }
}
