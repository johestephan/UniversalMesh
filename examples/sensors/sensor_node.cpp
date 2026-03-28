#include <Arduino.h>
#include <WiFi.h>
#include "UniversalMesh.h"

#ifndef NODE_NAME
#define NODE_NAME "C6-Dummy-Sensor"
#endif

#define APP_ID_TEMP_HUMID 0x03
#define SLEEP_SECONDS 30

UniversalMesh mesh;

// --- RTC MEMORY ---
RTC_DATA_ATTR uint8_t rtcCoordinatorMac[6] = {0};
RTC_DATA_ATTR uint8_t rtcChannel = 0; 
RTC_DATA_ATTR bool rtcIsConfigured = false;

void setup() {
    Serial.begin(115200);
    delay(3000); // For Native USB debugging
    
    Serial.println("\n=== Sensor Node Waking Up ===");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // 1. DISCOVERY (Only runs on first boot or dropped connection)
    if (!rtcIsConfigured || rtcChannel == 0) {
        Serial.println("[BOOT] Network unknown. Sweeping channels...");
        mesh.begin(1); 
        rtcChannel = mesh.findCoordinatorChannel(NODE_NAME);
        
        if (rtcChannel > 0) {
            mesh.getCoordinatorMac(rtcCoordinatorMac);
            rtcIsConfigured = true;
            Serial.printf("[BOOT] Locked on Channel %d. Saved to RTC.\n", rtcChannel);
        } else {
            Serial.println("[BOOT] Coordinator unreachable.");
            esp_sleep_enable_timer_wakeup(SLEEP_SECONDS * 1000000ULL);
            esp_deep_sleep_start();
        }
    }

    // 2. TRANSMIT
    if (rtcIsConfigured) {
        mesh.begin(rtcChannel);
        mesh.setCoordinatorMac(rtcCoordinatorMac);
        
        // --- Pack Payload ---
        uint16_t temp = 2250; // 22.50 C
        uint16_t hum = 4500;  // 45.00 %
        uint8_t payload[4] = {
            (uint8_t)(temp >> 8), (uint8_t)(temp & 0xFF),
            (uint8_t)(hum >> 8),  (uint8_t)(hum & 0xFF)
        };
        
        // --- Send via UniversalMesh Core ---
        if (mesh.sendToCoordinator(APP_ID_TEMP_HUMID, payload, sizeof(payload))) {
            Serial.println("[TX] Telemetry sent successfully!");
        } else {
            Serial.println("[TX] Delivery failed. Wiping RTC state.");
            rtcIsConfigured = false;
        }
        
        // 3. SLEEP
        Serial.printf("[SLEEP] Shutting down for %d seconds.\n", SLEEP_SECONDS);
        delay(20); 
        esp_sleep_enable_timer_wakeup(SLEEP_SECONDS * 1000000ULL);
        esp_deep_sleep_start();
    }
}

void loop() {}