#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "mbedtls/sha256.h"
#include "DHT.h"

// WIFI
const char* ssid = "「JAITP」";
const char* password = "MTFKISAS";

// MQTT
const char* mqtt_server = "172.20.10.2";
const int mqtt_port = 1883;

const char* pub_topic = "greenhouse/fern/data";
const char* sub_topic = "greenhouse/fern/control";

WiFiClient espClient;
PubSubClient client(espClient);

// PINS
#define DHTPIN 4
#define DHTTYPE DHT11
#define FLAME_PIN 15
#define BUTTON_PIN 18
#define LED_PIN 26   // pump indicator

DHT dht(DHTPIN, DHTTYPE);

// VARIABLES
float temperature;
float humidity;
bool flameDetected;

bool pumpState = false;
bool lastButtonState = HIGH;

unsigned long lastSend = 0;


// HASH
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


// MQTT CALLBACK
void callback(char* topic, byte* payload, unsigned int length){

  String msg;

  for(int i=0;i<length;i++)
    msg+=(char)payload[i];

  Serial.println(msg);

  if(msg=="WATER_ON") pumpState=true;
  if(msg=="WATER_OFF") pumpState=false;
}


// WIFI CONNECT
void connectWiFi(){

  WiFi.begin(ssid,password);

  while(WiFi.status()!=WL_CONNECTED){
    delay(500);
    Serial.print(".");
  }

  Serial.println("WiFi connected");
}


// MQTT RECONNECT
void reconnectMQTT(){

  while(!client.connected()){

    if(client.connect("FernPot_ESP32")){

      client.subscribe(sub_topic);

    }else{

      delay(2000);

    }
  }
}


// READ SENSORS
void readSensors(){

  temperature=dht.readTemperature();
  humidity=dht.readHumidity();

  flameDetected=digitalRead(FLAME_PIN)==LOW;

  bool buttonState=digitalRead(BUTTON_PIN);

  if(lastButtonState==HIGH && buttonState==LOW){

    pumpState=!pumpState;
    Serial.println("Button toggled LED");

  }

  lastButtonState=buttonState;
}


// SEND DATA
void sendData(){

  StaticJsonDocument<256> finalDoc;
  JsonObject data = finalDoc.createNestedObject("data");

  data["temp"] = temperature;
  data["humidity"] = humidity;
  data["flame"] = flameDetected;
  data["led"] = pumpState;

  String payload;
  serializeJson(finalDoc, payload);   // FIXED

  String hash = generateHash(payload);
  finalDoc["hash"] = hash;

  String finalMsg;
  serializeJson(finalDoc, finalMsg);

  client.publish(pub_topic, finalMsg.c_str());

  Serial.println(finalMsg);
}

// SETUP
void setup(){

  Serial.begin(115200);

  pinMode(LED_PIN,OUTPUT);
  pinMode(FLAME_PIN,INPUT);
  pinMode(BUTTON_PIN,INPUT_PULLUP);

  dht.begin();

  connectWiFi();

  client.setServer(mqtt_server,mqtt_port);
  client.setCallback(callback);

  reconnectMQTT();
}


// LOOP
void loop(){

  if(WiFi.status()!=WL_CONNECTED)
    connectWiFi();

  if(!client.connected())
    reconnectMQTT();

  client.loop();

  readSensors();

  digitalWrite(LED_PIN,pumpState);

  if(millis()-lastSend>5000){

    lastSend=millis();
    sendData();

  }

}