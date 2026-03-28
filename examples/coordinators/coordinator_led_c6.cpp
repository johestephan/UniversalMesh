#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <esp_wifi.h>
#include <ArduinoJson.h>
#ifdef HAS_RGB_LED
#include <Adafruit_NeoPixel.h>
#endif
#include "secrets.h"
#include "UniversalMesh.h"
#include "web/web.h"

#ifdef LILYGO_T_ETH_ELITE
#include <ETH.h>
#include <Preferences.h>
#include <esp_now.h>
extern void setupETH();
extern void loopETH();
extern bool isEthConnected();
extern bool isEthLinkUp();

static Preferences _prefs;
static uint8_t _meshChannel = 1;  // default

static uint8_t loadMeshChannel() {
  _prefs.begin("mesh", true);
  uint8_t ch = _prefs.getUChar("channel", 1);
  _prefs.end();
  return (ch >= 1 && ch <= 13) ? ch : 1;
}

uint8_t getMeshChannel() { return _meshChannel; }

void setMeshChannel(uint8_t ch) {
  if (ch < 1 || ch > 13) return;
  _meshChannel = ch;
  // Persist
  _prefs.begin("mesh", false);
  _prefs.putUChar("channel", ch);
  _prefs.end();
  // Apply to radio
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  // Update ESP-NOW broadcast peer
  static const uint8_t broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcast, 6);
  peer.channel = ch;
  esp_now_mod_peer(&peer);
  Serial.printf("[MESH] Channel changed to %d\n", ch);
}
#endif

// --- RGB LED ---
#ifdef HAS_RGB_LED
constexpr uint8_t LED_PIN  = 8;
Adafruit_NeoPixel rgbLed(1, LED_PIN, NEO_GRB + NEO_KHZ800);

struct RGB { uint8_t r, g, b; };
constexpr RGB COLOR_OFF    = {  0,   0,   0};
constexpr RGB COLOR_GREEN  = {  0, 255,   0};
constexpr RGB COLOR_RED    = {255,   0,   0};
constexpr RGB COLOR_BLUE   = {  0,   0, 255};
constexpr RGB COLOR_YELLOW = {255, 200,   0};

enum LedState { LED_CONNECTING, LED_CONNECTED, LED_NO_WIFI, LED_TX_BLINK, LED_RX_BLINK };
LedState ledState     = LED_CONNECTING;
LedState ledPrevState = LED_CONNECTING;
unsigned long ledTimer = 0;
bool ledToggle = false;
constexpr unsigned long BLINK_INTERVAL = 300;  // ms between connecting blinks
constexpr unsigned long FLASH_ON  = 250;        // ms LED on  per TX/RX blink
constexpr unsigned long FLASH_OFF = 250;         // ms LED off per TX/RX blink
constexpr uint8_t       FLASH_COUNT = 3;        // number of TX/RX blinks

int8_t ledFlashRemaining = 0;  // blinks left (counts down)

void setColor(const RGB& c, uint8_t brightness = 50) {
  uint16_t s = (uint16_t)brightness * 255 / 100;
  rgbLed.setPixelColor(0, rgbLed.Color((c.r * s) / 255, (c.g * s) / 255, (c.b * s) / 255));
  rgbLed.show();
}

void ledFlash(LedState flashState) {
  if (ledState == LED_TX_BLINK || ledState == LED_RX_BLINK) return; // don't interrupt ongoing flash
  ledPrevState = ledState;
  ledState = flashState;
  ledFlashRemaining = FLASH_COUNT;
  ledToggle = true;
  ledTimer = millis();
  setColor(flashState == LED_TX_BLINK ? COLOR_BLUE : COLOR_YELLOW);
}

void ledUpdate() {
  unsigned long now = millis();
  switch (ledState) {
    case LED_CONNECTING:
      if (now - ledTimer >= BLINK_INTERVAL) {
        ledToggle = !ledToggle;
        setColor(ledToggle ? COLOR_GREEN : COLOR_OFF);
        ledTimer = now;
      }
      break;
    case LED_CONNECTED:
    case LED_NO_WIFI:
      break; // steady — color already set on transition
    case LED_TX_BLINK:
    case LED_RX_BLINK: {
      unsigned long phase = ledToggle ? FLASH_ON : FLASH_OFF;
      if (now - ledTimer >= phase) {
        ledToggle = !ledToggle;
        if (ledToggle) {
          // starting a new on-phase = new blink
          ledFlashRemaining--;
          if (ledFlashRemaining <= 0) {
            ledState = ledPrevState;
            setColor(ledState == LED_CONNECTED ? COLOR_GREEN : COLOR_RED);
            break;
          }
          setColor(ledState == LED_TX_BLINK ? COLOR_BLUE : COLOR_YELLOW);
        } else {
          setColor(COLOR_OFF);
        }
        ledTimer = now;
      }
      break;
    }
  }
}
#endif // HAS_RGB_LED



// --- ROUTING TABLE ---
#define MAX_NODES 20
struct KnownNode {
  uint8_t mac[6];
  unsigned long lastSeen;
  bool active;
};
KnownNode meshNodes[MAX_NODES];

// Function to register or update a node in the table
void updateNodeTable(uint8_t* mac) {
  // Check if we already know this node
  for (int i = 0; i < MAX_NODES; i++) {
    if (meshNodes[i].active && memcmp(meshNodes[i].mac, mac, 6) == 0) {
      meshNodes[i].lastSeen = millis(); // Update timestamp
      return;
    }
  }
  // If new, find an empty slot and add it
  for (int i = 0; i < MAX_NODES; i++) {
    if (!meshNodes[i].active) {
      memcpy(meshNodes[i].mac, mac, 6);
      meshNodes[i].lastSeen = millis();
      meshNodes[i].active = true;
      char discMsg[80];
      snprintf(discMsg, sizeof(discMsg), "[ROUTING] New node: %02X:%02X:%02X:%02X:%02X:%02X",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
      Serial.println(discMsg);
      addSerialLog(discMsg);
      return;
    }
  }
}


AsyncWebServer server(80);
UniversalMesh mesh;

// --- RECEIVE CALLBACK ---
// Triggers when the mesh library catches a packet meant for us (or broadcast)
void onMeshMessage(MeshPacket* packet, uint8_t* senderMac) {
#ifdef HAS_RGB_LED
  ledFlash(LED_RX_BLINK);
#endif

  // 1. Immediately log the node in the routing table (mutex protects meshNodes[] and _log[])
  lockMeshData();
  updateNodeTable(packet->srcMac);
  logPacket(packet->type, senderMac, packet->appId, packet->payload, packet->payloadLen);
  unlockMeshData();

  // 2. Process the packet types
  if (packet->type == MESH_TYPE_PONG) {
    char logMsg[80];
    snprintf(logMsg, sizeof(logMsg), "[DISCOVERY] PONG from %02X:%02X:%02X:%02X:%02X:%02X",
             senderMac[0], senderMac[1], senderMac[2],
             senderMac[3], senderMac[4], senderMac[5]);
    Serial.println(logMsg);
    addSerialLog(logMsg);
  }
  else if (packet->type == MESH_TYPE_DATA) {
    char payloadStr[65] = {0};
    uint8_t len = packet->payloadLen < 64 ? packet->payloadLen : 64;
    memcpy(payloadStr, packet->payload, len);
    char logMsg[128];
    snprintf(logMsg, sizeof(logMsg), "[DATA] src=%02X:%02X:%02X:%02X:%02X:%02X appId=0x%02X payload=%s",
             senderMac[0], senderMac[1], senderMac[2],
             senderMac[3], senderMac[4], senderMac[5],
             packet->appId, payloadStr);
    Serial.println(logMsg);
    addSerialLog(logMsg);
  }
}

void setup() {
  Serial.begin(115200);
  
  // ESP32-C6 Hardware USB CDC Wait
  uint32_t t = millis();
  while (!Serial && (millis() - t) < 5000) { delay(10); }
  
#ifdef HAS_RGB_LED
  rgbLed.begin();
  rgbLed.show();
  ledTimer = millis();
#endif

  Serial.println("\n=== MESH MASTER BRIDGE INITIALIZING ===");

  WiFi.mode(WIFI_STA);

#ifdef LILYGO_T_ETH_ELITE
  // --- ETH Elite: Ethernet first, WiFi fallback ---
  setupETH();

  // Phase 1: wait up to 5s for a cable (link up)
  Serial.print("[ETH] Waiting for cable");
  unsigned long ethStart = millis();
  while (!isEthLinkUp() && (millis() - ethStart) < 5000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  // Phase 2: cable detected — wait up to 60s for DHCP
  if (isEthLinkUp()) {
    Serial.print("[ETH] Cable detected, waiting for IP");
    ethStart = millis();
    while (!isEthConnected() && (millis() - ethStart) < 60000) {
      delay(500);
      Serial.print(".");
    }
    Serial.println();
  }

  uint8_t chan;
  if (isEthConnected()) {
    Serial.printf("[ETH] Online! IP: %s\n", ETH.localIP().toString().c_str());
    esp_wifi_set_ps(WIFI_PS_NONE);
    _meshChannel = loadMeshChannel();
    chan = _meshChannel;
    Serial.printf("[MESH] Using channel %d (from NVS)\n", chan);
    esp_wifi_set_channel(chan, WIFI_SECOND_CHAN_NONE);
  } else {
    Serial.println("[ETH] No link — falling back to WiFi...");
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    constexpr unsigned long WIFI_TIMEOUT_MS = 15000;
    unsigned long wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - wifiStart) < WIFI_TIMEOUT_MS) {
      delay(300);
      Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      esp_wifi_set_ps(WIFI_PS_NONE);
      Serial.printf("[WIFI] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
      Serial.println("[WIFI] Connection failed! Running offline.");
    }
    chan = (WiFi.status() == WL_CONNECTED) ? WiFi.channel() : 6;
  }

#else
  // --- Standard build: WiFi only ---
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[WIFI] Connecting to %s\n", WIFI_SSID);

  constexpr unsigned long WIFI_TIMEOUT_MS = 15000;
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - wifiStart) < WIFI_TIMEOUT_MS) {
#ifdef HAS_RGB_LED
    setColor(COLOR_GREEN); delay(300);
    setColor(COLOR_OFF);   delay(300);
#else
    delay(300);
#endif
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    esp_wifi_set_ps(WIFI_PS_NONE);
#ifdef HAS_RGB_LED
    ledState = LED_CONNECTED;
    setColor(COLOR_GREEN);
#endif
    Serial.printf("\n[WIFI] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[WIFI] Channel: %d\n", WiFi.channel());
  } else {
#ifdef HAS_RGB_LED
    ledState = LED_NO_WIFI;
    setColor(COLOR_RED);
#endif
    Serial.println("\n[WIFI] Connection failed! Running offline.");
  }

  uint8_t chan = (WiFi.status() == WL_CONNECTED) ? WiFi.channel() : 6;
#endif  // LILYGO_T_ETH_ELITE

  // 2. Initialize Universal Mesh on the Router's Channel
  if (mesh.begin(chan)) {
    Serial.println("[SYSTEM] Universal Mesh Online.");
    mesh.onReceive(onMeshMessage);
    char meshMsg[64];
    snprintf(meshMsg, sizeof(meshMsg), "[MESH] Online on channel %d", chan);
    addSerialLog(meshMsg);
  } else {
    Serial.println("[ERROR] Mesh Initialization Failed!");
    addSerialLog("[ERROR] Mesh init failed!");
  }

  // 3. Setup REST API Endpoint for Injecting Packets
  server.on("/api/tx", HTTP_POST,
    [](AsyncWebServerRequest *request) {},
    nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      JsonDocument doc;
      deserializeJson(doc, data, len);

      uint8_t destMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
      const char* macStr = doc["dest"];
      if (macStr) {
        int temp[6];
        if (sscanf(macStr, "%x:%x:%x:%x:%x:%x", &temp[0], &temp[1], &temp[2], &temp[3], &temp[4], &temp[5]) == 6) {
          for (int i = 0; i < 6; i++) destMac[i] = (uint8_t)temp[i];
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

      mesh.send(destMac, MESH_TYPE_DATA, appId, payloadBytes, payloadLen, ttl);
#ifdef HAS_RGB_LED
      ledFlash(LED_TX_BLINK);
#endif
      request->send(200, "application/json", "{\"status\":\"data_sent\"}");
    }
  );

  // 2. Discovery Endpoint
  server.on("/api/discover", HTTP_GET, [](AsyncWebServerRequest *request) {
    uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    mesh.send(broadcastMac, MESH_TYPE_PING, 0x00, nullptr, 0, 4);
    request->send(200, "application/json", "{\"status\":\"discovery_initiated\"}");
  });

  server.on("/api/nodes", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Snapshot under lock to prevent PSRAM cache coherency race with onMeshMessage
    KnownNode snap[MAX_NODES];
    lockMeshData();
    memcpy(snap, meshNodes, sizeof(snap));
    unlockMeshData();

    JsonDocument doc;
    JsonArray nodesArray = doc["nodes"].to<JsonArray>();
    unsigned long now = millis();
    for (int i = 0; i < MAX_NODES; i++) {
      if (snap[i].active) {
        JsonObject nodeObj = nodesArray.add<JsonObject>();
        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 snap[i].mac[0], snap[i].mac[1], snap[i].mac[2],
                 snap[i].mac[3], snap[i].mac[4], snap[i].mac[5]);
        nodeObj["mac"] = macStr;
        nodeObj["last_seen_seconds_ago"] = (now - snap[i].lastSeen) / 1000;
      }
    }

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  initWebDashboard(server);
  server.begin();
  Serial.println("[SYSTEM] REST API Gateway Ready.");
  addSerialLog("[SYSTEM] REST API ready - waiting for nodes...");
}

void loop() {
#ifdef LILYGO_T_ETH_ELITE
  loopETH();
#endif
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
#ifdef HAS_RGB_LED
      ledFlash(LED_TX_BLINK);
#endif
    }
  }

#ifdef HAS_RGB_LED
  ledUpdate();
#endif
}