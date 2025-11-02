// ====================================================================
// Star Track Project - ESP32 WiFi Communications Gateway (WiFiComm)
// Purpose: Handles Wi-Fi, NTP time synchronization, hosts a status web server,
//          and relays tracking data/commands to the Arduino Uno via Serial.
// ====================================================================
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <HardwareSerial.h>

// --- Configuration ---
// Replace with your local Wi-Fi credentials
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;   // UTC time (GMT offset 0 seconds)
const int daylightOffset_sec = 0; // No daylight savings time offset

// --- Hardware Pins and Serial Communication ---
// ESP32 uses Serial0 for debug and Serial2 for communication with Arduino Uno
HardwareSerial UnoSerial(2); // Use Serial Port 2 on the ESP32

// Uno RX/TX pins connected to ESP32 TX/RX pins (as per build_instructions.md)
const int UNO_TX_PIN = 21; // ESP32 D21 -> Uno RX
const int UNO_RX_PIN = 22; // ESP32 D22 -> Uno TX

// Baud rate must match the Arduino Uno firmware's rate
const long BAUD_RATE = 115200;

// --- Global State and Buffers ---
// The ESP32 is the source of truth for time and status
WebServer server(80);
String currentStatus = "INIT"; // States: INIT, IDLE, TRACKING, ERROR
unsigned long lastStatusRequestTime = 0;
unsigned long lastNTPUpdateTime = 0;
const unsigned long NTP_UPDATE_INTERVAL = 300000; // 5 minutes

// Buffer to hold incoming trajectory data from Flask
#define MAX_TRAJECTORY_SIZE 4096 
char trajectoryBuffer[MAX_TRAJECTORY_SIZE];
int bufferLength = 0;

// ====================================================================
// Utility Functions
// ====================================================================

// Gets current UTC time in Unix Epoch format (seconds since 1970)
long getEpochTime() {
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  return (long)mktime(&timeinfo);
}

// Attempts to update time via NTP
void timeSync() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  if (!getLocalTime(NULL, 10000)) { // Wait up to 10 seconds
    Serial.println("Failed to obtain time from NTP.");
  } else {
    lastNTPUpdateTime = millis();
    Serial.print("Time synchronized. Current Epoch: ");
    Serial.println(getEpochTime());
    
    // Relay current time to Uno immediately after sync
    String timeCommand = "TIME:" + String(getEpochTime()) + "\n";
    UnoSerial.print(timeCommand);
    Serial.print("Relayed time to Uno: ");
    Serial.print(timeCommand);
  }
}

// ====================================================================
// Web Server Handlers (Flask API Endpoints)
// ====================================================================

// Endpoint: /status (GET)
// Flask polls this to get the current state of the rotator.
void handleStatus() {
  // We need to query the Uno for the real-time status before responding.
  // Send a status request command to the Uno
  UnoSerial.print("QUERY:STATUS\n"); 
  
  // For now, we respond with the local stored status, and the Uno will update it
  // asynchronously or in the main loop. A simple response structure:
  String jsonResponse = "{";
  jsonResponse += "\"status\": \"" + currentStatus + "\", ";
  jsonResponse += "\"epoch_time\": " + String(getEpochTime()) + ", ";
  jsonResponse += "\"ip\": \"" + WiFi.localIP().toString() + "\"";
  
  // NOTE: In a production app, the Uno would constantly send back Az/El/State updates.
  // For prototyping, we use the local state `currentStatus`.
  
  // Read any available status from Uno Serial (optional, for fast updates)
  while (UnoSerial.available()) {
    String serialResponse = UnoSerial.readStringUntil('\n');
    // Simple logic to parse and update local status
    if (serialResponse.startsWith("STATUS_UPDATE:")) {
      currentStatus = serialResponse.substring(14); // e.g., TRACKING
      Serial.print("Uno Status Update: ");
      Serial.println(currentStatus);
      // Rebuild JSON response with new status if necessary...
      jsonResponse = "{ \"status\": \"" + currentStatus + "\", \"epoch_time\": " + String(getEpochTime()) + ", \"ip\": \"" + WiFi.localIP().toString() + "\" }";
    }
  }

  jsonResponse += "}";
  server.send(200, "application/json", jsonResponse);
  lastStatusRequestTime = millis();
}

// Endpoint: /upload_trajectory (POST)
// Flask posts the trajectory data (DSV string) here.
void handleUpload() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Missing body");
    return;
  }

  String payload = server.arg("plain");
  bufferLength = payload.length();
  
  if (bufferLength >= MAX_TRAJECTORY_SIZE) {
    server.send(413, "text/plain", "Payload too large");
    return;
  }

  // Copy payload into buffer and null-terminate it
  payload.toCharArray(trajectoryBuffer, bufferLength + 1);
  
  // 1. Send Trajectory Start Command and current time to Uno
  UnoSerial.print("CMD:START_TRAJ\n");
  
  // 2. Send the current time to Uno so it knows when the trajectory officially starts
  // This is redundant but ensures the time is fresh right before tracking starts
  String timeCommand = "TIME:" + String(getEpochTime()) + "\n";
  UnoSerial.print(timeCommand);
  
  // 3. Send the Trajectory Data itself
  // We send the raw DSV string over Serial to the Uno
  UnoSerial.print("DATA:" + payload + "\n");

  // Update local status and send response
  currentStatus = "RECEIVING_DATA";
  server.send(200, "text/plain", "Trajectory data received and relayed to Uno. Length: " + String(bufferLength));
  Serial.print("Received and relayed trajectory data to Uno. Length: ");
  Serial.println(bufferLength);
}

// Endpoint: /command (POST)
// Flask sends simple commands (HOME, STOP) here.
void handleCommand() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Missing body");
    return;
  }

  String payload = server.arg("plain");
  // Simple JSON parsing for command: {"cmd": "STOP"} or {"cmd": "HOME"}
  
  String command = "";
  if (payload.indexOf("\"cmd\":\"STOP\"") != -1) {
    command = "STOP";
  } else if (payload.indexOf("\"cmd\":\"HOME\"") != -1) {
    command = "HOME";
  }
  
  if (command != "") {
    // Relay command to Uno
    String unoCommand = "CMD:" + command + "\n";
    UnoSerial.print(unoCommand);
    Serial.print("Relayed command to Uno: ");
    Serial.println(unoCommand);
    
    // Update local status
    if (command == "STOP") currentStatus = "IDLE";
    if (command == "HOME") currentStatus = "HOMING";

    server.send(200, "text/plain", "Command relayed: " + command);
  } else {
    server.send(400, "text/plain", "Invalid or unsupported command.");
  }
}

// Handle non-existent endpoints
void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  server.send(404, "text/plain", message);
}

// ====================================================================
// Setup and Loop
// ====================================================================
void setup() {
  Serial.begin(115200); // Debug serial monitor
  
  // Start the Serial connection to the Arduino Uno
  // Pin definitions are: RX, TX
  UnoSerial.begin(BAUD_RATE, SERIAL_8N1, UNO_RX_PIN, UNO_TX_PIN);

  // --- Wi-Fi Connection ---
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected.");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    currentStatus = "IDLE";
    
    // --- NTP Time Synchronization ---
    timeSync();

    // --- Web Server Setup ---
    server.on("/", handleStatus); // Default root should show status
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/upload_trajectory", HTTP_POST, handleUpload);
    server.on("/command", HTTP_POST, handleCommand);
    server.onNotFound(handleNotFound);
    
    server.begin();
    Serial.println("HTTP server started.");
  } else {
    Serial.println("\nWiFi connection failed. Entering ERROR state.");
    currentStatus = "ERROR";
  }
}

void loop() {
  // Handle incoming HTTP requests
  server.handleClient();
  
  // Periodically check for time drift and re-sync
  if (currentStatus != "ERROR" && (millis() - lastNTPUpdateTime > NTP_UPDATE_INTERVAL)) {
    timeSync();
  }
  
  // Check for status updates from the Uno
  // The Uno will periodically send back its current status (e.g., Az/El/State)
  if (UnoSerial.available()) {
    String serialResponse = UnoSerial.readStringUntil('\n');
    Serial.print("<- Uno: ");
    Serial.println(serialResponse);
    
    // Example: Uno sends STATUS_UPDATE:TRACKING
    if (serialResponse.startsWith("STATUS_UPDATE:")) {
      currentStatus = serialResponse.substring(14); 
    }
  }
  
  delay(10); 
}
