#include <WiFi.h>
#include <WebSocketsClient.h>

// ESP32 connects to a Raspberry Pi running as an soft Access Point.
// evt. use #include "arduino_secrets.h" https://docs.arduino.cc/arduino-cloud/cloud-editor/store-your-sensitive-data-safely-when-sharing/
const char* ssid     = "ESP-NET";
const char* password = "esp32test123";

// WebSocket server info (running on the Raspberry Pi)
const char* ws_host = "192.168.4.1";
const uint16_t ws_port = 8080;
const char* ws_path = "/ws";

// WebSocket client instance
WebSocketsClient webSocket;


// Connects the ESP32 to Wi‑Fi.
// This is a simple blocking approach, good enough for now.

void connectToWiFi() {
  Serial.println("WiFi Connecting...");

  // Station mode = ESP32 acts as a client
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  // Wait here until we get a connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi Connected");
  Serial.print("WiFi IP address: ");
  Serial.println(WiFi.localIP());
}

/*
  This function is called whenever something happens
  on the WebSocket connection.
*/
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {

  switch (type) {

    // Successfully connected to the server
    case WStype_CONNECTED:
      Serial.println("WS Connected to server");

      // Let the server know we're alive
      webSocket.sendTXT("ESP32 connected");
      break;

    // Connection was lost
    case WStype_DISCONNECTED:
      Serial.println("WS Disconnected from server");
      break;

    // Incoming text message
    case WStype_TEXT:
      Serial.print("WS Received: ");
      Serial.println((char*)payload);
      break;

    // Generic error (timeout, network issue, etc.)
    case WStype_ERROR:
      Serial.println("WS Error");
      break;

    default:
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Connect to Wi‑Fi first
  connectToWiFi();

  // Start the WebSocket client
  webSocket.begin(ws_host, ws_port, ws_path);

  // Attach our event handler
  webSocket.onEvent(webSocketEvent);

  // If the connection drops, try again every 3 seconds
  webSocket.setReconnectInterval(3000);

  Serial.println("[Setup] Complete");
}

void loop() {

  // This keeps the WebSocket connection alive.
  // If this is not called often, things will break.
  webSocket.loop();

  // Send a message every 5 seconds (example / test)
  static unsigned long lastSend = 0;

  if (millis() - lastSend > 5000 && webSocket.isConnected()) {
    lastSend = millis();

    String msg = "Hello from ESP32, uptime(ms): ";
    msg += millis();

    webSocket.sendTXT(msg);
  }
}
