#include "UniversalMesh.h"

UniversalMesh* UniversalMesh::_instance = nullptr;

UniversalMesh::UniversalMesh() {
  _instance = this;
  _userCallback = nullptr;
  _role = MESH_NODE;
  _coordinatorFound = false;
  _lastDiscoveryPing = 0;
  memset(_coordinatorMac, 0, 6);
}

bool UniversalMesh::begin(uint8_t channel, MeshRole role) {
  _role = role;
  
  #if defined(ESP8266)
    wifi_set_channel(channel);
  #elif defined(ESP32)
    // ESP32 requires promiscuous mode to force a channel change while disconnected
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
  #endif
  
  #if defined(ESP32)
    esp_wifi_get_mac(WIFI_IF_STA, _myMac);
  #else
    WiFi.macAddress(_myMac);
  #endif

  if (esp_now_init() != 0) return false;

  #if defined(ESP8266)
    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    esp_now_register_recv_cb(espNowRecvWrapper);
    esp_now_add_peer(_broadcastMac, ESP_NOW_ROLE_COMBO, channel, NULL, 0);
  #elif defined(ESP32)
    esp_now_register_recv_cb(espNowRecvWrapper);
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, _broadcastMac, 6);
    peer.channel = channel;
    esp_now_add_peer(&peer);
  #endif

  return true;
}

void UniversalMesh::onReceive(MeshReceiveCallback callback) {
  _userCallback = callback;
}

bool UniversalMesh::send(uint8_t destMac[6], uint8_t type, uint8_t appId, const uint8_t* payload, uint8_t len, uint8_t ttl) {
  MeshPacket p = {};
  p.type = type;
  p.ttl = ttl;
  
  #if defined(ESP8266)
    p.msgId = (uint32_t)os_random();
  #elif defined(ESP32)
    p.msgId = esp_random() % 1000000000; 
  #endif

  memcpy(p.destMac, destMac, 6);
  memcpy(p.srcMac, _myMac, 6);
  p.appId = appId;
  
  if (payload != nullptr && len > 0) {
    p.payloadLen = (len > 64) ? 64 : len;
    memcpy(p.payload, payload, p.payloadLen);
  } else {
    p.payloadLen = 0;
  }

  return (esp_now_send(_broadcastMac, (uint8_t*)&p, sizeof(p)) == 0);
}

// --- LAZY SENDER LOGIC ---
bool UniversalMesh::sendToCoordinator(uint8_t appId, uint8_t* payload, uint8_t len) {
  if (!_coordinatorFound) return false; 
  return send(_coordinatorMac, MESH_TYPE_DATA, appId, payload, len, 4);
}

// --- CHANNEL SWEEPER & DISCOVERY ---
uint8_t UniversalMesh::findCoordinatorChannel(const char* nodeName) {
  _coordinatorFound = false;
  uint8_t nameLen = (nodeName != nullptr) ? strlen(nodeName) : 0;
  if (nameLen > 64) nameLen = 64;

  for (uint8_t ch = 1; ch <= 13; ch++) {
    // Force Radio to new channel
    #if defined(ESP32)
      esp_wifi_set_promiscuous(true);
      esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
      esp_wifi_set_promiscuous(false);
    #elif defined(ESP8266)
      wifi_set_channel(ch);
    #endif

    // Fire PING, tucking the name into the payload
    send(_broadcastMac, MESH_TYPE_PING, 0x00, (const uint8_t*)nodeName, nameLen, 4);

    // Wait up to 150ms for the Coordinator to reply
    unsigned long startWait = millis();
    while (millis() - startWait < 150) {
      delay(10); // Yield
      if (_coordinatorFound) return ch; // Success!
    }
  }
  return 0; // Failed to find Coordinator
}

// --- RTC MEMORY HELPERS ---
void UniversalMesh::setCoordinatorMac(uint8_t* mac) {
  memcpy(_coordinatorMac, mac, 6);
  _coordinatorFound = true;
}

void UniversalMesh::getCoordinatorMac(uint8_t* mac) {
  memcpy(mac, _coordinatorMac, 6);
}

bool UniversalMesh::isCoordinatorFound() {
  return _coordinatorFound;
}

// --- DEDUPLICATION ---
bool UniversalMesh::isSeen(uint32_t id, uint8_t type) {
  for(int i=0; i<SEEN_SIZE; i++) {
    if(_seen[i].msgId == id && _seen[i].type == type) return true;
  }
  return false;
}

void UniversalMesh::markSeen(uint32_t id, uint8_t type) {
  _seen[_sIdx] = {id, type};
  _sIdx = (_sIdx + 1) % SEEN_SIZE;
}

// --- RECEIVE LOGIC & AUTO-RELAY ---
#if defined(ESP8266)
void UniversalMesh::espNowRecvWrapper(uint8_t *mac, uint8_t *data, uint8_t len) {
  if (_instance) _instance->handleReceive(mac, data, len);
}
#elif defined(ESP32)
void UniversalMesh::espNowRecvWrapper(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (_instance) _instance->handleReceive((uint8_t*)info->src_addr, (uint8_t*)data, len);
}
#endif

void UniversalMesh::handleReceive(uint8_t *mac, uint8_t *data, uint8_t len) {
  if (len < sizeof(MeshPacket)) return;
  MeshPacket* p = (MeshPacket*)data;

  if (!isSeen(p->msgId, p->type)) {
    markSeen(p->msgId, p->type);
    
    // --- 1. PROTOCOL HANDSHAKES ---
    if (p->type == MESH_TYPE_PING) {
      // If we are the Coordinator, flag our PONG with appId 0xFF
      uint8_t replyAppId = (_role == MESH_COORDINATOR) ? 0xFF : 0x00;
      send(p->srcMac, MESH_TYPE_PONG, replyAppId, nullptr, 0, p->ttl);
      
      // If a Node sent a name in the PING, pass it up to the Coordinator's sketch!
      if (_role == MESH_COORDINATOR && p->payloadLen > 0 && _userCallback) {
        _userCallback(p, mac); 
      }
    }

    // Node silently catches the Coordinator's 0xFF PONG
    if (_role == MESH_NODE && p->type == MESH_TYPE_PONG && p->appId == 0xFF) {
      memcpy(_coordinatorMac, p->srcMac, 6);
      _coordinatorFound = true;
    }

    // --- 2. ROUTING LOGIC ---
    bool isForMe = (memcmp(p->destMac, _myMac, 6) == 0) || (memcmp(p->destMac, _broadcastMac, 6) == 0);
    bool terminalData = (p->type == MESH_TYPE_DATA && memcmp(p->destMac, _myMac, 6) == 0);

    // --- 3. APPLICATION CALLBACK ---
    if (isForMe && _userCallback && p->type != MESH_TYPE_PING) {
      _userCallback(p, mac);
    }

    // --- 4. AUTO-RELAY LOGIC ---
    if (p->ttl > 0 && !terminalData) {
      uint8_t relayBuffer[112]; 
      memcpy(relayBuffer, data, len);
      ((MeshPacket*)relayBuffer)->ttl--;
      esp_now_send(_broadcastMac, relayBuffer, len);
    }
  }
}

void UniversalMesh::update() {
  // Background discovery if we lose the Coordinator
  if (_role == MESH_NODE && !_coordinatorFound) {
    if (millis() - _lastDiscoveryPing > 10000) { 
      _lastDiscoveryPing = millis();
      send(_broadcastMac, MESH_TYPE_PING, 0x00, nullptr, 0, 4);
    }
  }
}