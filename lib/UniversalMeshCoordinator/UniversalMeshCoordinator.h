#ifndef UNIVERSAL_MESH_COORDINATOR_H
#define UNIVERSAL_MESH_COORDINATOR_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "UniversalMesh.h"

// --- DEBUG MACROS ---
// Defaults to 1. To disable, add `-D UM_DEBUG_MODE=0` to build_flags in platformio.ini
#ifndef UM_DEBUG_MODE
#define UM_DEBUG_MODE 1 
#endif

#if UM_DEBUG_MODE
  #define UM_DEBUG_PRINT(x) Serial.print(x)
  #define UM_DEBUG_PRINTLN(x) Serial.println(x)
  #define UM_DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define UM_DEBUG_PRINT(x)
  #define UM_DEBUG_PRINTLN(x)
  #define UM_DEBUG_PRINTF(...)
#endif

class UniversalMeshCoordinator {
  public:
    UniversalMeshCoordinator();

    // Initializes the mesh radio and configures the MQTT broker
    bool begin(uint8_t channel, const char* mqttBroker, uint16_t mqttPort, const char* clientId = "MeshBridge");

    // The single function to call in loop()
    void update();

  private:
    UniversalMesh _mesh;
    WiFiClient _wifiClient;
    PubSubClient _mqtt;

    const char* _brokerIp;
    uint16_t _brokerPort;
    const char* _clientId;
    unsigned long _lastMqttReconnect;

    void reconnectMQTT();

    static UniversalMeshCoordinator* _instance;
    static void meshCallbackWrapper(MeshPacket* packet, uint8_t* senderMac);
    void handleMeshMessage(MeshPacket* packet, uint8_t* senderMac);
};

#endif