#include "UniversalMeshCoordinator.h"

UniversalMeshCoordinator* UniversalMeshCoordinator::_instance = nullptr;

UniversalMeshCoordinator::UniversalMeshCoordinator() : _mqtt(_wifiClient) {
    _instance = this;
    _lastMqttReconnect = 0;
}

bool UniversalMeshCoordinator::begin(uint8_t channel, const char* mqttBroker, uint16_t mqttPort, const char* clientId) {
    _brokerIp = mqttBroker;
    _brokerPort = mqttPort;
    _clientId = clientId;

    _mqtt.setServer(_brokerIp, _brokerPort);
    _mqtt.setBufferSize(256); 

    if (_mesh.begin(channel, MESH_COORDINATOR)) {
        _mesh.onReceive(meshCallbackWrapper);
        UM_DEBUG_PRINTLN("[BRIDGE] Mesh Radio Online. Role: Coordinator.");
        return true;
    }
    UM_DEBUG_PRINTLN("[BRIDGE] FATAL: ESP-NOW Init Failed!");
    return false;
}

void UniversalMeshCoordinator::update() {
    _mesh.update();

    if (WiFi.status() == WL_CONNECTED) {
        if (!_mqtt.connected()) {
            if (millis() - _lastMqttReconnect > 5000) {
                _lastMqttReconnect = millis();
                reconnectMQTT();
            }
        } else {
            _mqtt.loop();
        }
    }
}

void UniversalMeshCoordinator::reconnectMQTT() {
    UM_DEBUG_PRINT("[BRIDGE] Connecting to MQTT Broker... ");
    if (_mqtt.connect(_clientId)) {
        UM_DEBUG_PRINTLN("Connected!");
    } else {
        UM_DEBUG_PRINTF("Failed, rc=%d. Retrying in 5s.\n", _mqtt.state());
    }
}

void UniversalMeshCoordinator::meshCallbackWrapper(MeshPacket* packet, uint8_t* senderMac) {
    if (_instance) {
        _instance->handleMeshMessage(packet, senderMac);
    }
}

void UniversalMeshCoordinator::handleMeshMessage(MeshPacket* packet, uint8_t* senderMac) {
    if (packet->type == MESH_TYPE_DATA) {
        char macStr[13];
        sprintf(macStr, "%02X%02X%02X%02X%02X%02X", 
                packet->srcMac[0], packet->srcMac[1], packet->srcMac[2],
                packet->srcMac[3], packet->srcMac[4], packet->srcMac[5]);

        String topic = "mesh/telemetry/" + String(macStr) + "/" + String(packet->appId);
        
        String hexPayload = "";
        for (int i = 0; i < packet->payloadLen; i++) {
            char hex[3];
            sprintf(hex, "%02X", packet->payload[i]);
            hexPayload += hex;
        }

        if (_mqtt.connected()) {
            _mqtt.publish(topic.c_str(), hexPayload.c_str());
            UM_DEBUG_PRINTF("[MQTT OUT] %s -> %s\n", topic.c_str(), hexPayload.c_str());
        }
    } 
    else if (packet->type == MESH_TYPE_PING && packet->payloadLen > 0) {
        char nodeName[65] = {0};
        memcpy(nodeName, packet->payload, packet->payloadLen);
        UM_DEBUG_PRINTF("[DISCOVERY] New Node Joined: %s\n", nodeName);
    }
}