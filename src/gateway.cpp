#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "UniversalMesh.h"

// --- CONFIGURATION ---
// MUST match the Master's operating channel!
#define WIFI_CHANNEL 6 

// HW364A I2C Pins
#define OLED_SDA 14
#define OLED_SCL 12

UniversalMesh mesh;
Adafruit_SSD1306 display(128, 64, &Wire, -1);

// WDT-Safe Buffer for OLED updates
volatile bool newDataReady = false;
uint8_t lastAppId = 0;
char lastPayload[65] = {0};

// --- RECEIVE CALLBACK ---
// Triggers when the mesh library catches a packet meant for us (or broadcast)
void onMeshMessage(MeshPacket* packet, uint8_t* senderMac) {
  // Only update the screen for Data payloads
  if (packet->type == MESH_TYPE_DATA) {
    lastAppId = packet->appId;
    memset(lastPayload, 0, sizeof(lastPayload)); // Clear old payload
    
    if (packet->payloadLen > 0) {
      memcpy(lastPayload, packet->payload, packet->payloadLen);
    }
    
    // Flag the main loop to update the display
    newDataReady = true;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== MESH GATEWAY DISPLAY BOOTING ===");
  
  // 1. Initialize Display
  Wire.begin(OLED_SDA, OLED_SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  
  display.setCursor(0,0);
  display.println("MESH GATEWAY");
  display.printf("CHANNEL: %d\n", WIFI_CHANNEL);
  display.println("INITIALIZING...");
  display.display();

  // 2. Initialize Mesh
  if (mesh.begin(WIFI_CHANNEL)) {
    Serial.println("[SYSTEM] Gateway Radio Online");
    mesh.onReceive(onMeshMessage);
    
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("MESH GATEWAY");
    display.println("ONLINE & LISTENING");
    display.display();
  } else {
    Serial.println("[ERROR] Mesh Initialization Failed!");
    display.println("RADIO ERROR!");
    display.display();
  }
}

void loop() {
  // Keep the mesh library running in the background
  mesh.update();
  
  // Safely update the screen outside of the hardware interrupt
  if (newDataReady) {
    Serial.printf("[DISPLAY] Updating screen with AppId: %02X\n", lastAppId);
    
    display.clearDisplay();
    display.setCursor(0,0);
    display.println(">> INCOMING MSG <<");
    display.printf("APP ID: %02X\n", lastAppId);
    
    display.setCursor(0, 25);
    display.setTextSize(1);
    display.println(lastPayload);
    display.display();
    
    newDataReady = false;
  }
  
  // Small delay to keep the ESP8266 Watchdog Timer happy
  delay(10);
}