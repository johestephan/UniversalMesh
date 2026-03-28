#include <Arduino.h>
#include <WiFi.h>
#include <Adafruit_NeoPixel.h>
#include "UniversalMeshCoordinator.h"
#include "secrets.h"

// ESP32-C6 DevKitC-1 default RGB LED pin
#define PIN 8 
Adafruit_NeoPixel strip(1, PIN, NEO_GRB + NEO_KHZ800);

UniversalMeshCoordinator bridge;

// Quick helper function to change colors
void setLedColor(uint8_t r, uint8_t g, uint8_t b) {
    strip.setPixelColor(0, strip.Color(r, g, b));
    strip.show();
}

void setup() {
    Serial.begin(115200);
    
    // Initialize the LED immediately so we know it has power
    strip.begin();
    strip.setBrightness(20); // Keep it dim so it doesn't blind you
    setLedColor(255, 0, 0);  // RED: Booting
    
    // Give Fedora and PlatformIO time to mount the USB CDC
    delay(3000); 

    Serial.println("\n\n======================================");
    Serial.println("--- UniversalMesh Bridge Booting ---");
    Serial.println("======================================\n");

    // 1. Connect to Wi-Fi
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("[WIFI] Connecting");
    
    setLedColor(255, 255, 0); // YELLOW: Wi-Fi Connecting

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    
    // Grab the actual channel your router assigned us
    uint8_t currentWifiChannel = WiFi.channel();
    
    Serial.println("\n[WIFI] Connected! IP: " + WiFi.localIP().toString());
    Serial.printf("[WIFI] Operating on Channel: %d\n", currentWifiChannel);

    setLedColor(0, 0, 255); // BLUE: Starting Bridge
    String clientId = "MeshBridge-" + String(random(0xffff), HEX);
    
    if (bridge.begin(currentWifiChannel, MQTT_BROKER, MQTT_PORT, clientId.c_str(), MQTT_USER, MQTT_PASS)) {
        setLedColor(0, 255, 0); // GREEN: Bridge is online and listening!
    } else {
        setLedColor(255, 0, 0); // RED: Bridge failed to initialize
    }
}

void loop() {
    // The library completely handles ESP-NOW polling, MQTT reconnects, and message translation
    bridge.update();
}