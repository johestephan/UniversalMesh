---

## 💡 Example Usage

### Adding Secure Data communication (0x17)

In **secrets.h**
```cpp
#define MESH_SECURE_KEY "MySuperSecretKey" // Must be max 16 chars!
``` 
In **coordinator.cpp**
```cpp
bridge.setNetworkKey(MESH_SECURE_KEY); 
// bridge.begin(...)
```
In **sensor_node.cpp**
```cpp
mesh.setNetworkKey(MESH_SECURE_KEY);
// ... Later, when you want to send data securely:
mesh.sendSecureToCoordinator(APP_ID_TEMP_HUMID, jsonOutput); 
```

### Minimal Coordinator (`coordinator.cpp`)
The coordinator connects to your local Wi-Fi and MQTT broker, then starts a mesh network on the same Wi-Fi channel. It requires a `secrets.h` file for credentials.

```cpp
// examples/coordinators/coordinator.cpp
#include <Arduino.h>
#include <WiFi.h>
#include "UniversalMeshCoordinator.h"
#include "secrets.h" // For WiFi and MQTT credentials

UniversalMeshCoordinator bridge;

void setup() {
    Serial.begin(115200);
    delay(2000); // Wait for Serial

    Serial.println("--- UniversalMesh Coordinator/Bridge Booting ---");

    // 1. Connect to local Wi-Fi
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("Connecting to WiFi...");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Connected! IP: " + WiFi.localIP().toString());

    // 2. Start the Mesh Coordinator & MQTT Bridge
    // The bridge will listen for mesh packets on the same channel as the Wi-Fi.
    uint8_t meshChannel = WiFi.channel();
    String clientId = "UniversalMeshBridge-" + String(random(0xffff), HEX);
    
    if (bridge.begin(meshChannel, MQTT_BROKER, MQTT_PORT, clientId.c_str(), MQTT_USER, MQTT_PASS)) {
        Serial.printf("Mesh bridge started on channel %d.\n", meshChannel);
    } else {
        Serial.println("FATAL: Mesh bridge failed to initialize!");
    }
}

void loop() {
    // The library handles all mesh and MQTT activity internally.
    bridge.update();
}
```

This will look like this

```bash

======================================
--- UniversalMesh Bridge Booting ---
======================================

[WIFI] Connecting...
[WIFI] Connected! IP: 192.168.1.160
[WIFI] Operating on Channel: 11
[BRIDGE] Mesh Radio Online. Role: Coordinator.
[BRIDGE] Connecting to MQTT Broker... Connected!
[DISCOVERY] New Node Joined: 588C8136AF90 (C6-Dummy-Sensor)
[DATA] Received from 588C8136AF90: {"temp":22.5,"hum":45}
```

### Minimal Sensor Node (`sensor_node.cpp`)
A sensor node automatically finds the coordinator, sends a JSON payload, and goes into deep sleep to conserve power. It uses RTC memory to remember the network configuration between wake-ups.

```cpp
// examples/sensors/sensor_node.cpp
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include "UniversalMesh.h"

#ifndef NODE_NAME
#define NODE_NAME "MyNewSensor"
#endif

#define APP_ID_TELEMETRY 0x03
#define SLEEP_SECONDS 60

UniversalMesh mesh;

// RTC Memory is used to store the network state during deep sleep
RTC_DATA_ATTR uint8_t rtcCoordinatorMac[6] = {0};
RTC_DATA_ATTR uint8_t rtcChannel = 0; 
RTC_DATA_ATTR bool rtcIsConfigured = false;

void setup() {
    Serial.begin(115200);
    delay(2000); // Wait for Serial
    
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    bool justDiscovered = false;

    // On first boot (or if connection is lost), sweep for the coordinator
    if (!rtcIsConfigured || rtcChannel == 0) {
        Serial.println("Network unknown. Sweeping for coordinator...");
        mesh.begin(1); // Start on a temporary channel
        rtcChannel = mesh.findCoordinatorChannel(NODE_NAME);
        
        if (rtcChannel > 0) {
            mesh.getCoordinatorMac(rtcCoordinatorMac);
            rtcIsConfigured = true;
            Serial.printf("Coordinator found on Channel %d. Saved to RTC.\n", rtcChannel);
            justDiscovered = true;
        } else {
            Serial.println("Coordinator not found. Sleeping.");
            esp_sleep_enable_timer_wakeup(SLEEP_SECONDS * 1000000ULL);
            esp_deep_sleep_start();
        }
    }

    // If we have a valid configuration, connect and transmit
    if (rtcIsConfigured) {
        mesh.begin(rtcChannel);
        mesh.setCoordinatorMac(rtcCoordinatorMac);

        // On first connection, announce presence to the coordinator
        if(justDiscovered) {
            mesh.send(rtcCoordinatorMac, MESH_TYPE_PING, 0x00, (const uint8_t*)NODE_NAME, strlen(NODE_NAME));
            delay(100); // Allow packet to send
        }
        
        // Create a JSON payload
        JsonDocument doc;
        doc["temperature"] = 24.7;
        doc["humidity"] = 55.2;
        doc["voltage"] = 3.31;

        String jsonOutput;
        serializeJson(doc, jsonOutput);
        
        // Send the JSON string to the coordinator
        if (mesh.sendToCoordinator(APP_ID_TELEMETRY, jsonOutput)) {
            Serial.println("Telemetry sent successfully!");
        } else {
            Serial.println("Delivery failed. Wiping RTC state to force rediscovery.");
            rtcIsConfigured = false; // This will trigger a new scan on next wake-up
        }
        
        // Go to sleep
        Serial.printf("Sleeping for %d seconds.\n", SLEEP_SECONDS);
        esp_sleep_enable_timer_wakeup(SLEEP_SECONDS * 1000000ULL);
        esp_deep_sleep_start();
    }
}

void loop() {
    // This part is never reached due to deep sleep
}
```
Which will look like this
```bash
=== Sensor Node Waking Up ===
[BOOT] Network unknown. Sweeping channels...
[MESH] Pinging Channel 1...
[MESH] Pinging Channel 2...
[MESH] Pinging Channel 3...
[MESH] Pinging Channel 4...
[MESH] Pinging Channel 5...
[MESH] Pinging Channel 6...
[MESH] Pinging Channel 7...
[MESH] Pinging Channel 8...
[MESH] Pinging Channel 9...
[MESH] Pinging Channel 10...
[MESH] Pinging Channel 11...
[MESH] PONG received on Channel 11!
[BOOT] Locked on Channel 11. Saved to RTC.
[MESH] ESP-NOW Initialized 58:8C:81:36:AF:90
[BOOT] Announcing presence to coordinator...
[TX] Telemetry sent successfully!
[SLEEP] Shutting down for 30 seconds.
```


## 💻 Installation & Project Structure
This project uses a modular `platformio.ini` setup to keep environments clean for contributors.

