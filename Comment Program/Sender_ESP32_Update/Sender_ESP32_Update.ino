//const char* ssid = "「JAITP」"; // Commented out alternate WiFi SSID [cite: 50]
//const char* password = "MTFKISAS"; // Commented out alternate WiFi Password [cite: 50]
#include <WiFi.h> // Include ESP32 WiFi library [cite: 51]
#include <PubSubClient.h> // Include MQTT client library [cite: 51]
#include <ArduinoJson.h> // Include JSON library [cite: 51]
#include "DHT.h" // Include DHT sensor library [cite: 51]

// ================= WIFI =================
const char* ssid = "Redmi 15 5G"; // Define active WiFi SSID [cite: 51]
const char* password = "ROME2011"; // Define active WiFi Password [cite: 51]
// ================= MQTT =================
const char* mqtt_server = "broker.emqx.io"; // Define MQTT broker address [cite: 52]
const int mqtt_port = 1883; // Define MQTT broker port [cite: 52]

const char* pub_topic = "greenhouse/fern/data"; // Topic to publish sensor data [cite: 52]
const char* sub_topic = "greenhouse/fern/control"; // Topic to listen for manual pump commands [cite: 53]
const char* mode_topic = "greenhouse/fern/mode"; // Topic to sync AUTO/MANUAL mode [cite: 53]
const char* button_topic = "greenhouse/fern/button"; // Topic to publish physical button events [cite: 53]

WiFiClient espClient; // Create WiFi client instance [cite: 53]
PubSubClient client(espClient); // Create MQTT client instance [cite: 53]
// ================= PINS =================
#define DHTPIN 4 // Define pin for DHT sensor [cite: 54]
#define DHTTYPE DHT11 // Define DHT sensor type as DHT11 [cite: 54]
#define FLAME_PIN 15 // Define pin for flame sensor [cite: 54]
#define BUTTON_PIN 18 // Define pin for physical button [cite: 54]
#define PUMP_PIN 17 // Define pin for water pump relay [cite: 54]
#define LED_ON_PIN 25 // Define pin for 'Pump ON' indicator LED [cite: 54]
#define LED_OFF_PIN 26 // Define pin for 'Pump OFF' indicator LED [cite: 54]

DHT dht(DHTPIN, DHTTYPE); // Initialize DHT sensor object [cite: 54]
// ================= VARIABLES =================
float temperature; // Variable to store current temperature [cite: 55]
float humidity; // Variable to store current humidity [cite: 55]
bool flameDetected; // Flag to store flame detection status [cite: 55]

bool pumpState = false; // Flag to track if pump is running [cite: 55]
bool lastButtonState = HIGH; // Track previous button state (pull-up defaults HIGH) [cite: 55]
bool autoMode = true; // Flag for system mode, defaults to AUTO [cite: 56]

// BUTTON HOLD
unsigned long buttonPressStart = 0; // Timer for button hold duration [cite: 56]
bool buttonHolding = false; // Flag tracking if button is currently being held [cite: 56]
bool longPressTriggered = false; // Flag to prevent single click firing after a long press [cite: 56]
// AUTO CYCLE
unsigned long pumpTimer = 0; // Timer tracking pump cycle intervals [cite: 57]
bool pumpCycleState = false; // Flag tracking state inside the auto cycle [cite: 57]

unsigned long lastSend = 0; // Timer for MQTT data publishing rate [cite: 57]
unsigned long lastPrint = 0; // Timer for Serial debugging output rate [cite: 58]

unsigned long lastButtonTime = 0; // Timer for button debounce [cite: 58]
const unsigned long debounceDelay = 150; // Delay in ms to ignore button noise [cite: 58]

float lastTemperature = 0; // Store last valid temp (for DHT fail fallback) [cite: 58]
float lastHumidity = 0; // Store last valid humidity (for DHT fail fallback) [cite: 59]

// ================= Checksum int =================
uint16_t generateChecksum(String data) { // Function to calculate a simple checksum [cite: 59]
  uint16_t sum = 0; // Initialize sum variable to 0 [cite: 59]
  for (int i = 0; i < data.length(); i++) { // Loop through every character in the string [cite: 60]
    sum += (uint8_t)data[i]; // Add ASCII integer value to sum [cite: 60]
  }

  return sum; // Return final checksum [cite: 60]
} // End checksum function [cite: 61]

// ================= MQTT CALLBACK =================
void callback(char* topic, byte* payload, unsigned int length){ // Triggered on incoming MQTT msg [cite: 61]

  String msg; // Initialize empty string [cite: 61]
  for(int i=0;i<length;i++) // Loop through payload bytes [cite: 62]
    msg += (char)payload[i]; // Append characters to string [cite: 62]

  String topicStr = String(topic); // Convert topic array to String [cite: 62]

  Serial.println("----- RECEIVED -----"); // Print debug header [cite: 62]
  Serial.print("Topic: "); Serial.println(topicStr); // Print received topic [cite: 62]
  Serial.print("Message: "); // Print header [cite: 62]
  Serial.println(msg); // Print received message [cite: 63]

  // MODE CONTROL
  if (topicStr == mode_topic) { // If message is a mode update [cite: 63]
    if (msg == "AUTO") { // If command is AUTO [cite: 63]
      autoMode = true; // Set system to AUTO [cite: 63]
      Serial.println("Switched to AUTO mode"); // Print debug confirmation [cite: 64]
    }
    else if (msg == "MANUAL") { // If command is MANUAL [cite: 64]
      autoMode = false; // Set system to MANUAL [cite: 64]
      Serial.println("Switched to MANUAL mode"); // Print debug confirmation [cite: 65]
    }
    return; // Exit callback [cite: 65]
  }

  // PUMP CONTROL (ONLY IN MANUAL)
  if (topicStr == sub_topic) { // If message is a pump command [cite: 65]
    if (!autoMode) { // Execute ONLY if in MANUAL mode [cite: 65]
      if(msg=="WATER_ON") pumpState=true; // Turn pump ON if commanded [cite: 65]
      if(msg=="WATER_OFF") pumpState=false; // Turn pump OFF if commanded [cite: 66]
    }
  }
} // End callback [cite: 66]

// ================= WIFI =================
void connectWiFi(){ // Function to handle WiFi connection [cite: 66]
  WiFi.begin(ssid,password); // Start connection with credentials [cite: 66]

  Serial.print("Connecting WiFi"); // Print debug message [cite: 66]
  while(WiFi.status()!=WL_CONNECTED){ // Wait while not connected [cite: 66]
    delay(500); // Check every 500ms [cite: 66]
    Serial.print("."); // Print loading dots [cite: 66]
  } // End while [cite: 67]
  Serial.println("\nWiFi connected"); // Confirm connection [cite: 67]
} // End WiFi connect [cite: 67]

// ================= MQTT =================
void reconnectMQTT(){ // Function to handle MQTT reconnection [cite: 67]
  while(!client.connected()){ // Loop until connected [cite: 67]
    Serial.println("Connecting MQTT..."); // Print attempt msg [cite: 67]
    if(client.connect("FernPot_ESP32")){ // Attempt connection with specific client ID [cite: 68]
      Serial.println("MQTT Connected!"); // Confirm connection [cite: 68]

      client.subscribe(sub_topic); // Subscribe to control topic [cite: 68]
      client.subscribe(mode_topic); // Subscribe to mode topic [cite: 68]
      // NEW [cite: 69]

    }else{ // If connection failed [cite: 69]
      Serial.println("MQTT Failed, retry..."); // Print failure msg [cite: 69]
      delay(2000); // Wait 2 seconds before retrying [cite: 69]
    } // End if/else [cite: 70]
  } // End while [cite: 70]
} // End reconnect function [cite: 70]

// ================= READ SENSORS =================
void readSensors(){ // Function to sample all sensors [cite: 70]

  float newTemp = dht.readTemperature(); // Read temp from DHT sensor [cite: 70]
  float newHum = dht.readHumidity(); // Read humidity from DHT sensor [cite: 70]
  bool dhtFail = false; // Flag to track if DHT read failed [cite: 71]

  if (isnan(newTemp) || isnan(newHum)) { // Check if reading is "Not a Number" (failed) [cite: 71]
    dhtFail = true; // Mark fail flag [cite: 71]
    //  keep old values [cite: 72]
    temperature = lastTemperature; // Restore last valid temp [cite: 72]
    humidity = lastHumidity; // Restore last valid humidity [cite: 72]
  } else { // If read was successful [cite: 73]

    // update values [cite: 73]
    temperature = newTemp; // Set actual temp [cite: 73]
    humidity = newHum; // Set actual humidity [cite: 73]
    lastTemperature = newTemp; // Save to fallback temp [cite: 74]
    lastHumidity = newHum; // Save to fallback humidity [cite: 74]
  } // End if/else [cite: 74]

  if (temperature >= 50) { // Check if temperature indicates a fire (simulated flame detection) [cite: 74]
    flameDetected = true; // Trigger flame alert [cite: 74]
  } else { // If normal temps [cite: 75]
    flameDetected = false; // No flame [cite: 75]
  } // End if/else [cite: 75]

  bool buttonState = digitalRead(BUTTON_PIN); // Read hardware button state [cite: 75]
  // debounce protection [cite: 76]
  if (millis() - lastButtonTime < debounceDelay) { // Ignore rapid successive state changes (noise) [cite: 76]
   lastButtonState = buttonState; // Sync state to avoid triggers [cite: 76]
   return; // Exit sensor logic early [cite: 76]
  } // End debounce [cite: 77]

  // LONG PRESS → CHANGE MODE [cite: 77]
  if (buttonState == LOW && lastButtonState == HIGH) { // If button was just pressed down [cite: 77]
    lastButtonTime = millis(); // Update debounce timer [cite: 77]
    buttonPressStart = millis(); // Record exact time button was pressed [cite: 78]
    buttonHolding = true; // Mark as holding [cite: 78]
  } // End if [cite: 78]

  if (buttonState == LOW && buttonHolding) { // If button is STILL being held down [cite: 78]
    if (millis() - buttonPressStart > 2000) { // Check if hold duration exceeds 2 seconds [cite: 78]

      autoMode = !autoMode; // Toggle system mode [cite: 78]
      Serial.println("=== MODE CHANGED ==="); // Debug print [cite: 79]
    Serial.println(autoMode ? "AUTO" : "MANUAL"); // Print new mode [cite: 79]

      client.publish(mode_topic, autoMode ? "AUTO" : "MANUAL"); // Sync new mode via MQTT [cite: 79]

      buttonHolding = false; // Reset hold flag so it doesn't trigger repeatedly [cite: 79]
      longPressTriggered = true;   // 🔥 IMPORTANT: Mark that a long press happened [cite: 80]
    } // End time check [cite: 80]
  } // End button holding check [cite: 80]

// SHORT PRESS → MANUAL CONTROL [cite: 80]
if (buttonState == HIGH && lastButtonState == LOW) { // If button is released [cite: 80]

   // ❌ IGNORE if it was long press [cite: 80]
    if (longPressTriggered) { // If this release is from a 2+ sec hold [cite: 80]
     longPressTriggered = false; // Reset flag, do nothing else [cite: 80]
    } else { // It was a short press [cite: 81]

     // SEND BUTTON EVENT TO NODE-RED [cite: 81]
      StaticJsonDocument<64> doc; // Create small JSON doc [cite: 81]
      doc["event"] = "button"; // Add event metadata [cite: 82]
      doc["action"] = "toggle"; // Add action metadata [cite: 82]

     String msg; // String buffer for JSON [cite: 82]
     serializeJson(doc, msg); // Convert JSON to string [cite: 82]
     client.publish(button_topic, msg.c_str()); // Send button event [cite: 82]
      // Only control pump in MANUAL mode [cite: 83]
      if (!autoMode) { // If system is in MANUAL [cite: 83]
       pumpState = !pumpState; // Toggle pump ON/OFF [cite: 83]
       Serial.println("Manual toggle pump"); // Debug print [cite: 84]
     } // End if manual [cite: 84]
    } // End else short press [cite: 84]

   buttonHolding = false; // Ensure hold state is cleared on release [cite: 84]
  } // End button release logic [cite: 84]
  lastButtonState = buttonState; // Save current button state for next loop [cite: 84]
  // PRINT [cite: 85]
  if (millis() - lastPrint > 2000) { // Every 2 seconds [cite: 85]
    lastPrint = millis(); // Update print timer [cite: 85]

    Serial.println("----- SENSOR READ -----"); // Header [cite: 85]
    Serial.print("Temperature: "); // Temp prefix [cite: 86]
    if(dhtFail) Serial.println("DHT FAIL"); // Warn if sensor failed [cite: 86]
    else Serial.println(temperature); // Print valid temp [cite: 86]

    Serial.print("Humidity: "); // Humidity prefix [cite: 86]
    if(dhtFail) Serial.println("DHT FAIL"); // Warn if sensor failed [cite: 86]
    else Serial.println(humidity); // Print valid humidity [cite: 86]
    Serial.print("Flame: "); // Flame prefix [cite: 86]
    Serial.println(flameDetected ? "YES" : "NO"); // Print flame status [cite: 87]
    Serial.print("Mode: "); Serial.println(autoMode ? "AUTO" : "MANUAL"); // Print current mode [cite: 87]
    Serial.print("Pump: "); Serial.println(pumpState ? "ON" : "OFF"); // Print current pump state [cite: 87]
  } // End print timer [cite: 88]
} // End readSensors function [cite: 88]

// ================= SEND DATA =================
void sendData(){ // Function to package and send MQTT data [cite: 88]

  // Create DATA only [cite: 88]
  StaticJsonDocument<192> dataDoc; // Create JSON doc for inner data [cite: 88]

  dataDoc["temp"] = temperature; // Append temperature [cite: 88]
  dataDoc["humidity"] = humidity; // Append humidity [cite: 88]
  dataDoc["flame"] = flameDetected; // Append flame status [cite: 89]
  dataDoc["led"] = pumpState; // Append pump status (labeled as 'led') [cite: 89]
  dataDoc["mode"] = autoMode ? "AUTO" : "MANUAL"; // Append active mode [cite: 89]

  String dataStr; // String for serialized data [cite: 89]
  serializeJson(dataDoc, dataStr); // Convert object to string [cite: 89]
  // Generate checksum from DATA ONLY [cite: 90]
  uint16_t checksum = generateChecksum(dataStr); // Generate hash based on string [cite: 90]

  // Create FINAL message [cite: 90]
  StaticJsonDocument<384> finalDoc; // Outer JSON doc for payload [cite: 90]
  finalDoc["data"] = dataDoc; // Nest the data object inside [cite: 91]
  finalDoc["checksum"] = checksum; // Append calculated checksum [cite: 91]

  String finalMsg; // String for entire payload [cite: 91]
  serializeJson(finalDoc, finalMsg); // Convert final doc to string [cite: 91]

  // Send [cite: 91]
  client.publish(pub_topic, finalMsg.c_str()); // Transmit via MQTT [cite: 91]

  Serial.println("===== MQTT SEND ====="); // Debug header [cite: 91]
  Serial.println(finalMsg); // Print actual JSON payload sent [cite: 91]
} // End sendData function [cite: 92]

// ================= SETUP =================
void setup(){ // Initial hardware setup on boot [cite: 92]

  Serial.begin(115200); // Start serial comms [cite: 92]

  pinMode(PUMP_PIN,OUTPUT); // Set pump relay pin as output [cite: 92]
  pinMode(FLAME_PIN,INPUT); // Set flame sensor pin as input [cite: 92]
  pinMode(BUTTON_PIN,INPUT_PULLUP); // Set button pin with internal pullup resistor [cite: 92]

  // Force DAC pins to digital mode [cite: 92]
  pinMode(LED_ON_PIN, OUTPUT); // Set ON LED as output [cite: 92]
  pinMode(LED_OFF_PIN, OUTPUT); // Set OFF LED as output [cite: 93]
  dacWrite(LED_ON_PIN, 0);  // Reset DAC to ensure purely digital switching [cite: 93]
  dacWrite(LED_OFF_PIN, 0); // Reset DAC to ensure purely digital switching [cite: 93]

  digitalWrite(PUMP_PIN, LOW); // Turn off pump by default [cite: 93]

  dht.begin(); // Initialize the DHT sensor [cite: 93]

  connectWiFi(); // Connect to network [cite: 93]

  client.setServer(mqtt_server,mqtt_port); // Configure broker [cite: 93]
  client.setCallback(callback); // Attach callback for receiving data [cite: 93]
  reconnectMQTT(); // Establish initial MQTT connection [cite: 94]
} // End setup [cite: 94]

// ================= LOOP =================
void loop(){ // Main endless loop [cite: 94]

  if(WiFi.status()!=WL_CONNECTED) // If WiFi disconnects [cite: 94]
    connectWiFi(); // Reconnect [cite: 94]

  if(!client.connected()) // If MQTT drops [cite: 94]
    reconnectMQTT(); // Reconnect [cite: 94]

  client.loop(); // Handle background MQTT tasks [cite: 94]

  readSensors(); // Sample hardware and process buttons [cite: 94]
  // AUTO MODE [cite: 95]
  if (autoMode) { // If system is running in automatic cycle [cite: 95]

    unsigned long now = millis(); // Get current timestamp [cite: 95]
    if (!pumpCycleState && now - pumpTimer > 10000) { // If pump OFF, wait 10 seconds [cite: 96]
      pumpCycleState = true; // Switch cycle to ON [cite: 96]
      pumpTimer = now; // Reset timer [cite: 96]
      Serial.println("AUTO: Pump ON"); // Debug print [cite: 97]
    }
    else if (pumpCycleState && now - pumpTimer > 15000) { // If pump ON, wait 15 seconds [cite: 97]
      pumpCycleState = false; // Switch cycle to OFF [cite: 97]
      pumpTimer = now; // Reset timer [cite: 98]
      Serial.println("AUTO: Pump OFF"); // Debug print [cite: 98]
    }

    pumpState = pumpCycleState; // Apply cycle state to actual pumpState variable [cite: 98]
  } // End AUTO logic [cite: 98]

  digitalWrite(LED_ON_PIN, pumpState); // Turn ON LED on if pump is running [cite: 98]
  digitalWrite(LED_OFF_PIN, !pumpState); // Turn OFF LED on if pump is stopped [cite: 98]
  digitalWrite(PUMP_PIN, pumpState ? LOW : HIGH); // Actuate pump relay (Active-Low configuration) [cite: 99]
  delay(500); // 500ms cycle stabilization delay [cite: 99]

  if(millis()-lastSend>2000){ // Check if 2 seconds passed since last MQTT publish [cite: 99]
    lastSend=millis(); // Reset send timer [cite: 99]
    sendData(); // Compile JSON and publish over MQTT [cite: 99]
  } // End if [cite: 99]
} // End loop [cite: 99]