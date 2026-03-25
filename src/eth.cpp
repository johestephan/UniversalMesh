#include <Arduino.h>

#ifdef LILYGO_T_ETH_ELITE

#include <ETH.h>
#include <WiFi.h>  // WiFi.onEvent() is the correct bus for ETH events

// LilyGo T-ETH Elite ESP32-S3 — W5500 SPI Ethernet pin assignments
#define ETH_MISO_PIN   47
#define ETH_MOSI_PIN   21
#define ETH_SCLK_PIN   48
#define ETH_CS_PIN     45
#define ETH_INT_PIN    14
#define ETH_RST_PIN    -1
#define ETH_PHY_ADDR    1

static bool ethConnected = false;

bool   isEthConnected()  { return ethConnected; }
String getEthLocalIP()   { return ETH.localIP().toString(); }
String getEthMAC()       { return ETH.macAddress(); }
String getEthGateway()   { return ETH.gatewayIP().toString(); }
int    getEthLinkSpeed() { return ETH.linkSpeed(); }
bool   isEthFullDuplex() { return ETH.fullDuplex(); }

static void onEthEvent(arduino_event_id_t event) {
    switch (event) {
        case ARDUINO_EVENT_ETH_START:
            Serial.println("[ETH] Started");
            ETH.setHostname("universalmesh");
            break;
        case ARDUINO_EVENT_ETH_CONNECTED:
            Serial.println("[ETH] Link Up");
            break;
        case ARDUINO_EVENT_ETH_GOT_IP:
            Serial.printf("[ETH] IP: %s  MAC: %s  %dMbps %s\n",
                ETH.localIP().toString().c_str(),
                ETH.macAddress().c_str(),
                ETH.linkSpeed(),
                ETH.fullDuplex() ? "FULL_DUPLEX" : "HALF_DUPLEX");
            ethConnected = true;
            break;
        case ARDUINO_EVENT_ETH_LOST_IP:
            Serial.println("[ETH] Lost IP");
            ethConnected = false;
            break;
        case ARDUINO_EVENT_ETH_DISCONNECTED:
            Serial.println("[ETH] Link Down");
            ethConnected = false;
            break;
        case ARDUINO_EVENT_ETH_STOP:
            Serial.println("[ETH] Stopped");
            ethConnected = false;
            break;
        default:
            break;
    }
}

void setupETH() {
    Serial.println("[ETH] Initializing W5500 (LilyGo T-ETH Elite)...");

    // Must use WiFi.onEvent — same event bus the ETH driver fires on
    WiFi.onEvent(onEthEvent);

    if (!ETH.begin(ETH_PHY_W5500, ETH_PHY_ADDR,
                   ETH_CS_PIN, ETH_INT_PIN, ETH_RST_PIN,
                   SPI3_HOST,
                   ETH_SCLK_PIN, ETH_MISO_PIN, ETH_MOSI_PIN)) {
        Serial.println("[ETH] ERROR: Failed to initialize W5500!");
    } else {
        Serial.println("[ETH] W5500 init OK — waiting for link...");
        delay(2000);  // Give the chip time to negotiate link
    }
}

void loopETH() {
    // ETH state is fully event-driven; nothing to poll.
}

#else  // Stub for non-ETH builds (e.g. coordinator_c6, coordinator_s3)

void setupETH() {}
void loopETH() {}

#endif  // LILYGO_T_ETH_ELITE
