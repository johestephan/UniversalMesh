#include <Arduino.h>
#include <WiFi.h>
#include "UniversalMeshCoordinator.h"
#include "secrets.h"

UniversalMeshCoordinator bridge;

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n--- UniversalMesh Bridge Booting ---");

    // 1. Connect to Wi-Fi
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("[WIFI] Connecting");
    
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n[WIFI] Connected! IP: " + WiFi.localIP().toString());

    // 2. Start the Mesh-to-MQTT Bridge
    // Note: We use the random client ID generation to avoid broker collisions
    String clientId = "MeshBridge-" + String(random(0xffff), HEX);
    bridge.begin(6, MQTT_BROKER, MQTT_PORT, clientId.c_str());
}

void loop() {
    // The library completely handles ESP-NOW polling, MQTT reconnects, and message translation
    bridge.update();
}