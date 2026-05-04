#include <WiFi.h>
#include <WebSocketsClient_Generic.h>

// Raspberry Pi WebSocket server
const char* ws_host_homenet = "192.168.1.169";
const char* ws_host = "192.168.4.1";
const uint16_t ws_port = 8080;
const char* ws_path = "/ws";

// Objects
WebSocketsClient webSocket;

// Optional: Wi‑Fi connect function
// (replace with your own if you want)
void connectToWiFi() {
  Serial.println("[WiFi] Connecting...");

  WiFi.mode(WIFI_STA);
  WiFi.begin("Pandora_Wifi","M120702D300603");
  //WiFi.begin("badboynet","badboy123");
  // Wait until connected
  Serial.print("trying to connect...");
  while(WiFi.status() !=WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }
    Serial.println(WiFi.localIP());
  
  Serial.println();
  Serial.println("[WiFi] Connected");
  Serial.print("[WiFi] IP address: ");
  Serial.println(WiFi.localIP());
}

// WebSocket event handler
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {

  switch (type) {

    case WStype_CONNECTED:
      Serial.println("[WS] Connected to server");

      // Send something immediately after connect
      webSocket.sendTXT("ESP32 connected");
      break;

    case WStype_DISCONNECTED:
      Serial.println("[WS] Disconnected from server");
      break;

    case WStype_TEXT:
      Serial.print("[WS] Received: ");
      Serial.println((char*)payload);
      break;

    case WStype_ERROR:
      Serial.println("[WS] Error");
      break;

    default:
      break;
  }
}

// Setup
void setup() {
  Serial.begin(115200);
  delay(1000);

  // ---- Connect to Wi‑Fi ----
  connectToWiFi();

  // ---- Start WebSocket client ----
  webSocket.begin(ws_host_homenet, ws_port, ws_path);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(3000);  // auto‑reconnect every 3s

  Serial.println("[Setup] Complete");
}

// Loop
void loop() {
  // Keep WebSocket alive
  webSocket.loop();

  // Example: send data every 5 seconds
  static unsigned long lastSend = 0;
  if (millis() - lastSend > 5000 && webSocket.isConnected()) {
    lastSend = millis();

    String msg = "Hello from ESP32, uptime(ms): ";
    msg += millis();

    webSocket.sendTXT(msg);
  }
}
