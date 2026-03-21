#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_wifi.h>
#include <ArduinoJson.h>
#include "UniversalMesh.h"

// --- CONFIGURATION ---
const char* ssid = "scn";
const char* password = "Gagmxyto0815";

WebServer server(80);
UniversalMesh mesh;

// --- RECEIVE CALLBACK ---
// Triggers when the mesh library catches a packet meant for us (or broadcast)
void onMeshMessage(MeshPacket* packet, uint8_t* senderMac) {
  JsonDocument doc;
  
  doc["type"] = packet->type;
  doc["msgId"] = packet->msgId;
  doc["ttl"] = packet->ttl;
  doc["appId"] = packet->appId;
  
  char macStr[18];
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", 
          senderMac[0], senderMac[1], senderMac[2], 
          senderMac[3], senderMac[4], senderMac[5]);
  doc["src"] = macStr;
  
  if (packet->payloadLen > 0) {
    char payloadStr[65] = {0};
    memcpy(payloadStr, packet->payload, packet->payloadLen);
    doc["payload"] = payloadStr;
  }

  // Print as a clean JSON string to the Serial Monitor (for the Node.js server to read)
  serializeJson(doc, Serial);
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  
  // ESP32-C6 Hardware USB CDC Wait
  uint32_t t = millis();
  while (!Serial && (millis() - t) < 5000) { delay(10); }
  
  Serial.println("\n=== MESH MASTER BRIDGE INITIALIZING ===");

  // 1. Connect to Home Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.printf("[WIFI] Connecting to %s\n", ssid);
  
  while (WiFi.status() != WL_CONNECTED) { 
    delay(500); 
    Serial.print("."); 
  }
  esp_wifi_set_ps(WIFI_PS_NONE); 
  
  uint8_t chan = WiFi.channel();
  Serial.printf("\n[WIFI] Connected! API IP: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("[WIFI] Operating Channel: %d\n", chan);

  // 2. Initialize Universal Mesh on the Router's Channel
  if (mesh.begin(chan)) {
    Serial.println("[SYSTEM] Universal Mesh Online.");
    mesh.onReceive(onMeshMessage);
  } else {
    Serial.println("[ERROR] Mesh Initialization Failed!");
  }

  // 3. Setup REST API Endpoint for Injecting Packets
server.on("/api/tx", HTTP_POST, []() {
    if (server.hasArg("plain")) {
      JsonDocument doc;
      deserializeJson(doc, server.arg("plain"));
      
      uint8_t destMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
      const char* macStr = doc["dest"];
      if (macStr) {
        int temp[6];
        if (sscanf(macStr, "%x:%x:%x:%x:%x:%x", &temp[0], &temp[1], &temp[2], &temp[3], &temp[4], &temp[5]) == 6) {
          for(int i=0; i<6; i++) destMac[i] = (uint8_t)temp[i];
        }
      }

      uint8_t ttl = doc["ttl"] | 4;
      uint8_t appId = doc["appId"] | 0x00;
      String payloadHex = doc["payload"] | "";
      
      uint8_t payloadBytes[64] = {0};
      uint8_t payloadLen = payloadHex.length() / 2;
      if (payloadLen > 64) payloadLen = 64; 
      
      for (int i = 0; i < payloadLen; i++) {
        String byteString = payloadHex.substring(i * 2, i * 2 + 2);
        payloadBytes[i] = (uint8_t) strtol(byteString.c_str(), NULL, 16);
      }
      
      // Hardcoded to MESH_TYPE_DATA (0x15)
      mesh.send(destMac, MESH_TYPE_DATA, appId, payloadBytes, payloadLen, ttl);
      server.send(200, "application/json", "{\"status\":\"data_sent\"}");
    }
  });

  // 2. Discovery Endpoint
  server.on("/api/discover", HTTP_GET, []() {
    uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    // Broadcast a PING (0x12) to the whole network
    mesh.send(broadcastMac, MESH_TYPE_PING, 0x00, nullptr, 0, 4);
    
    // Return immediately. The PONGs will stream out via Serial as they arrive.
    server.send(200, "application/json", "{\"status\":\"discovery_initiated\"}");
  });

  server.begin();
  Serial.println("[SYSTEM] REST API Gateway Ready.");
}

void loop() {
  server.handleClient();
  mesh.update();

  // Temporary USB Serial Bypass for testing
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "SEND") {
      Serial.println("[INJECT] Firing test packet into mesh via Serial...");
      uint8_t destMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
      const char* payload = "Serial Test Pkt";
      mesh.send(destMac, MESH_TYPE_DATA, 0x01, (const uint8_t*)payload, strlen(payload), 4);
    }
  }
}