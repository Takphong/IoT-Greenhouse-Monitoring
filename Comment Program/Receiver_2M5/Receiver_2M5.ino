#include <M5CoreS3.h> // Include M5CoreS3 hardware library [cite: 1]
#include <WiFi.h> // Include ESP32 WiFi library [cite: 1]
#include <PubSubClient.h> // Include MQTT client library [cite: 1]
#include <ArduinoJson.h> // Include JSON parsing library [cite: 1]

// ===========For CheckSum int=============
uint16_t generateChecksum(String data) { // Function to calculate a simple checksum [cite: 1]
  uint16_t sum = 0; // Initialize sum variable to 0 [cite: 1]
  for (int i = 0; i < data.length(); i++) { // Loop through every character in the string [cite: 2]
    sum += (uint8_t)data[i]; // Add the ASCII integer value of the character to sum [cite: 2]
  }

  return sum; // Return the final calculated checksum [cite: 2]
} // End of checksum function [cite: 3]

// ================= WIFI =================
const char* ssid = "Redmi 15 5G"; // Define WiFi SSID (Network name) [cite: 3]
const char* password = "ROME2011"; // Define WiFi Password [cite: 3]
const char* mqtt_server = "broker.emqx.io"; // Define MQTT broker address [cite: 3]
const char* data_topic = "greenhouse/fern/data"; // MQTT topic for receiving sensor data [cite: 4]
const char* control_topic = "greenhouse/fern/control"; // MQTT topic for manual pump control [cite: 4]
const char* mode_topic = "greenhouse/fern/mode"; // MQTT topic for system mode sync [cite: 4]

WiFiClient espClient; // Create WiFi client instance [cite: 4]
PubSubClient client(espClient); // Create MQTT client instance using WiFi [cite: 4]

// ===== Data From Sender =====
float temperature = 0; // Variable to store received temperature [cite: 5]
float humidity = 0; // Variable to store received humidity [cite: 5]
bool flame = false; // Variable to store flame detection status [cite: 5]
bool pump = false; // Variable to store pump status [cite: 6]

bool checksumOK = false; // Flag to verify if data integrity is valid [cite: 6]
uint16_t receivedChecksum = 0; // Variable to store checksum sent by ESP32 [cite: 6]
uint16_t calculatedChecksum = 0; // Variable to store locally calculated checksum [cite: 6]

// ===== IMU =====
float roll = 0; // Variable for X-axis tilt [cite: 7]
float pitch = 0; // Variable for Y-axis tilt [cite: 7]
float yaw = 0; // Variable for Z-axis rotation [cite: 7]

// CLEAN MODE
String systemMode = "AUTO"; // Default system mode set to AUTO [cite: 8]

int screenMode = 0; // Variable to track which display page is active [cite: 8]
unsigned long lastReconnectAttempt = 0; // Timer tracking for MQTT reconnection [cite: 8]

// ===== Swipe =====
int touchStartX = 0; // Store initial X coordinate of screen touch [cite: 9]
bool touching = false; // Flag to track if screen is currently being touched [cite: 9]

// ================= MQTT CALLBACK =================
void callback(char* topic, byte* payload, unsigned int length) { // Function triggered on incoming MQTT message [cite: 10]

  String msg = ""; // Initialize empty string for message payload [cite: 10]
  for (int i = 0; i < length; i++) // Loop through payload bytes [cite: 11]
    msg += (char)payload[i]; // Append characters to message string [cite: 11]

  String topicStr = String(topic); // Convert topic char array to String object [cite: 11]

  // ===== MODE SYNC (ONLY SOURCE OF TRUTH) =====
  if (topicStr == mode_topic) { // If message is from the mode topic [cite: 12]

    systemMode = msg; // Update local system mode [cite: 12]
    Serial.println("---- MODE SYNC ----"); // Print sync header to Serial [cite: 13]
    Serial.print("Mode: "); Serial.println(systemMode); // Print new mode [cite: 13]

    return; // Exit callback [cite: 13]
  }

  // ===== CONTROL =====
  if (topicStr == control_topic) { // If message is from the control topic [cite: 13]

    if (msg == "AUTO") { // Check if command is AUTO [cite: 13]
      systemMode = "AUTO"; // Set mode to AUTO [cite: 13]
    } // End if [cite: 14]
    else if (msg == "WATER_ON" || msg == "WATER_OFF") { // Check if command is manual water control [cite: 14]
      systemMode = "MANUAL"; // Force system into MANUAL mode [cite: 14]
    } // End else if [cite: 15]

    return; // Exit callback [cite: 15]
  }

  // ===== SENSOR DATA =====
  if (topicStr == data_topic) { // If message is from the data topic [cite: 15]

    StaticJsonDocument<768> doc; // Create a JSON document buffer [cite: 15]
    DeserializationError error = deserializeJson(doc, msg); // Parse incoming JSON string [cite: 16]
    if (error) { // If parsing fails [cite: 16]
      Serial.print("JSON Error: "); // Print error header [cite: 16]
      Serial.println(error.c_str()); // Print specific error message [cite: 16]
      return; // Exit callback [cite: 16]
    } // End error check [cite: 17]

    if (!doc.containsKey("data")) { // Check if JSON has a "data" object [cite: 17]
      Serial.println("No data object!"); // Warn if missing [cite: 17]
      return; // Exit callback [cite: 17]
    } // End check [cite: 18]

    // ===== CHECKSUM VERIFY =====
    if (!doc.containsKey("checksum")) { // Check if JSON has a "checksum" value [cite: 18]
      Serial.println("No checksum!"); // Warn if missing [cite: 18]
      return; // Exit callback [cite: 19]
    }

    receivedChecksum = doc["checksum"]; // Extract received checksum [cite: 19]

    String dataStr; // String to hold serialized inner data [cite: 19]
    serializeJson(doc["data"], dataStr); // Convert "data" object back to string [cite: 19]

    calculatedChecksum = generateChecksum(dataStr); // Calculate checksum on raw data string [cite: 19]
    if (receivedChecksum != calculatedChecksum) { // Compare received vs calculated [cite: 20]
      Serial.println("❌ CHECKSUM FAILED!"); // Alert on mismatch [cite: 20]

     Serial.print("Received: "); // Print header [cite: 20]
     Serial.println(receivedChecksum); // Print received value [cite: 20]

     Serial.print("Calculated: "); // Print header [cite: 20]
     Serial.println(calculatedChecksum); // Print calculated value [cite: 20]
     checksumOK = false; // Mark checksum flag as failed [cite: 21]

     return; // Reject packet and exit [cite: 21]
    } else { // If checksums match [cite: 21]
     Serial.println("✅ CHECKSUM OK"); // Alert success [cite: 21]
     checksumOK = true; // Mark checksum flag as valid [cite: 21]
    } // End else [cite: 22]

    JsonObject data = doc["data"]; // Extract the inner "data" object [cite: 22]

    temperature = data["temp"] | temperature; // Update temp, fallback to old value if missing [cite: 22]
    humidity    = data["humidity"] | // Update humidity, fallback to old value [cite: 22]
                  humidity; // Fallback value [cite: 23]
    flame       = data["flame"] | flame; // Update flame status [cite: 23]
    pump        = data["led"] | pump; // Update pump status [cite: 24]

    Serial.println("---- RECEIVED FROM ESP32 ----"); // Print header [cite: 24]
    Serial.print("Mode: "); Serial.println(systemMode); // Print current mode [cite: 24]
    Serial.print("Pump: "); Serial.println(pump); // Print pump state [cite: 25]
  }
}

// ================= WIFI =================
void connectWiFi() { // Function to connect to WiFi [cite: 25]
  WiFi.begin(ssid, password); // Start connection with credentials [cite: 25]
  while (WiFi.status() != WL_CONNECTED) // Wait until connected [cite: 25]
    delay(500); // Check every 500ms [cite: 26]
} // End WiFi function [cite: 26]

// ================= MQTT =================
void reconnectMQTT() { // Function to reconnect MQTT [cite: 26]
  if (client.connect("FernMonitor")) { // Attempt connection with client ID [cite: 26]
    client.subscribe(data_topic); // Subscribe to data topic [cite: 26]
    client.subscribe(control_topic); // Subscribe to control topic [cite: 26]
    client.subscribe(mode_topic); // Subscribe to mode topic [cite: 26]
  } // End if [cite: 27]
} // End MQTT function [cite: 27]

// ================= IMU =================
void updateIMU() { // Function to read inertial measurement unit [cite: 27]

  float ax, ay, az; // Variables for accelerometer data [cite: 27]
  float gx, gy, gz; // Variables for gyroscope data [cite: 27]

  M5.Imu.getAccelData(&ax, &ay, &az); // Fetch accelerometer readings [cite: 27]
  M5.Imu.getGyroData(&gx, &gy, &gz); // Fetch gyroscope readings [cite: 28]

  roll  = atan2(ay, az) * 57.3; // Calculate roll angle in degrees [cite: 28]
  pitch = atan2(-ax, sqrt(ay * ay + az * az)) * 57.3; // Calculate pitch angle in degrees [cite: 29]
  yaw   = gz; // Set yaw from Z-axis gyro [cite: 29]
} // End IMU function [cite: 30]

// ================= DISPLAY =================
void displayScreen() { // Function to render UI on screen [cite: 30]

  M5.Lcd.clear(); // Clear the screen [cite: 30]
  M5.Lcd.setTextSize(2); // Set text size multiplier [cite: 30]
  M5.Lcd.setCursor(10, 10); // Set drawing cursor position [cite: 30]
  // WiFi
  M5.Lcd.setTextColor(WiFi.status() == WL_CONNECTED ? GREEN : RED); // Set text color based on WiFi status [cite: 31]
  M5.Lcd.println("WiFi"); // Print WiFi indicator [cite: 31]

  // ===== MODE (CLEAN) =====
  M5.Lcd.setCursor(10, 30); // Move cursor down [cite: 31]
  if (systemMode == "AUTO") // Check if mode is AUTO [cite: 32]
    M5.Lcd.setTextColor(GREEN); // Set color to green [cite: 32]
  else // If MANUAL [cite: 32]
    M5.Lcd.setTextColor(BLUE); // Set color to blue [cite: 32]

  M5.Lcd.printf("Mode: %s\n\n", systemMode.c_str()); // Print current mode [cite: 32]

  M5.Lcd.setTextColor(WHITE); // Reset text to white [cite: 32]
  M5.Lcd.println("--------------------------\n"); // Print separator line [cite: 32]
  M5.Lcd.setCursor(10, 100); // Move cursor down for main content [cite: 32]
  // ===== PAGE 1 =====
  if (screenMode == 0) { // If on the first page [cite: 33]

    M5.Lcd.setTextColor(RED); // Set title color [cite: 33]
    M5.Lcd.println("PAGE 1\n"); // Print page title [cite: 33]

    M5.Lcd.setTextColor(WHITE); // Reset color [cite: 33]
    M5.Lcd.println("--------------------------\n"); // Print separator [cite: 33]

    M5.Lcd.setTextColor(GREENYELLOW); // Set color for temperature [cite: 33]
    M5.Lcd.printf("Temperature: %.1f C\n", temperature); // Print temp value [cite: 34]

    M5.Lcd.setTextColor(CYAN); // Set color for humidity [cite: 34]
    M5.Lcd.printf("Humidity: %.1f %%\n", humidity); // Print humidity value [cite: 34]

    M5.Lcd.setTextColor(ORANGE); // Set color for flame [cite: 34]
    M5.Lcd.printf("Flame Detected: %s\n", flame ? "YES" : "NO"); // Print flame status [cite: 34]
  } // End page 1 [cite: 35]

  // ===== PAGE 2 =====
  else if (screenMode == 1) { // If on the second page [cite: 35]

    M5.Lcd.setTextColor(ORANGE); // Set title color [cite: 35]
    M5.Lcd.println("PAGE 2\n"); // Print page title [cite: 35]
    M5.Lcd.setTextColor(WHITE); // Reset color [cite: 36]
    M5.Lcd.println("--------------------------\n"); // Print separator [cite: 36]

    M5.Lcd.setTextColor(DARKCYAN); // Set color for roll [cite: 36]
    M5.Lcd.printf("Roll: %.1f\n", roll); // Print roll angle [cite: 36]

    M5.Lcd.setTextColor(OLIVE); // Set color for pitch [cite: 36]
    M5.Lcd.printf("Pitch: %.1f\n", pitch); // Print pitch angle [cite: 36]

    M5.Lcd.setTextColor(PURPLE); // Set color for yaw [cite: 36]
    M5.Lcd.printf("Yaw: %.1f\n", yaw); // Print yaw angle [cite: 36]
  } // End page 2 [cite: 37]

  // ===== PAGE 3 =====
  else if (screenMode == 2) { // If on the third page [cite: 37]

    M5.Lcd.setTextColor(YELLOW); // Set title color [cite: 37]
    M5.Lcd.println("PAGE 3\n"); // Print page title [cite: 37]
    M5.Lcd.setTextColor(WHITE); // Reset color [cite: 38]
    M5.Lcd.println("--------------------------\n"); // Print separator [cite: 38]

    M5.Lcd.setTextColor(CYAN); // Set pump color [cite: 38]
    M5.Lcd.printf("Pump: %s\n", pump ? "ON" : "OFF"); // Print pump status [cite: 38]

    if (checksumOK) { // If last checksum was valid [cite: 38]
    M5.Lcd.setTextColor(GREEN); // Set color to green [cite: 38]
    M5.Lcd.println("Checksum: OK"); // Print success [cite: 38]
    } else { // If checksum failed [cite: 39]
    M5.Lcd.setTextColor(RED); // Set color to red [cite: 39]
    M5.Lcd.println("Checksum: FAIL"); // Print failure [cite: 39]
  } // End if/else [cite: 39]

  M5.Lcd.setTextColor(WHITE); // Reset color [cite: 39]
  M5.Lcd.printf("RX: %u\n", receivedChecksum); // Print received raw checksum [cite: 39]
  M5.Lcd.printf("CAL: %u\n", calculatedChecksum); // Print calculated raw checksum [cite: 39]
  } // End page 3 [cite: 40]
}

// ================= SETUP =================
void setup() { // Main setup block runs once on boot [cite: 40]

  M5.begin(); // Initialize M5 device [cite: 40]
  Serial.begin(115200); // Start serial monitor at 115200 baud [cite: 40]

  connectWiFi(); // Initial WiFi connection [cite: 40]

  client.setServer(mqtt_server, 1883); // Configure MQTT broker details [cite: 40]
  client.setCallback(callback); // Attach the MQTT message handler [cite: 40]
} // End setup [cite: 41]

// ================= LOOP =================
void loop() { // Main program loop runs continuously [cite: 41]

  M5.update(); // Update M5 button/touch states [cite: 41]
  updateIMU(); // Fetch latest IMU data [cite: 41]

  // ===== TOUCH =====
  auto t = M5.Touch.getDetail(); // Get touch sensor details [cite: 41]
  if (t.isPressed() && !touching) { // If screen is pressed for the first time [cite: 42]
    touchStartX = t.x; // Record starting X position [cite: 42]
    touching = true; // Set touching flag to true [cite: 42]
  } // End if [cite: 43]

  if (!t.isPressed() && touching) { // If screen was touched but is now released [cite: 43]

    int diff = t.x - touchStartX; // Calculate swipe distance [cite: 43]
    if (diff > 40) screenMode++; // Swipe right, next page [cite: 44]
    else if (diff < -40) screenMode--; // Swipe left, previous page [cite: 44]
    else if (touchStartX > 160) screenMode++; // Tap right half, next page [cite: 44]
    else screenMode--; // Tap left half, previous page [cite: 44]
    if (screenMode > 2) screenMode = 0; // Wrap around if over max pages [cite: 45]
    if (screenMode < 0) screenMode = 2; // Wrap around if under min pages [cite: 45]

    touching = false; // Reset touch flag [cite: 45]
  } // End if [cite: 46]

  // MQTT
  if (WiFi.status() != WL_CONNECTED) // Check if WiFi dropped [cite: 46]
    connectWiFi(); // Reconnect WiFi [cite: 46]
  if (!client.connected()) { // Check if MQTT dropped [cite: 47]
    unsigned long now = millis(); // Get current time [cite: 47]
    if (now - lastReconnectAttempt > 3000) { // Check if 3 seconds passed since last attempt [cite: 48]
      lastReconnectAttempt = now; // Update reconnect timer [cite: 48]
      reconnectMQTT(); // Attempt to reconnect MQTT [cite: 48]
    } // End if [cite: 49]
  } else { // If MQTT is connected [cite: 49]
    client.loop(); // Process incoming MQTT packets [cite: 49]
  } // End if/else [cite: 49]

  displayScreen(); // Render the current screen [cite: 49]
  delay(200); // Small delay to stabilize loop [cite: 49]
}