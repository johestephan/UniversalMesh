## Need a GUI  and more examples?
**For more examples (Lilygo, esp32, esp8266, etc) please check out https://github.com/javastraat/UniversalMesh-Gui**

---

# UniversalMesh (v0.1.0_alpha)

A lightweight, Layer-3 mesh networking protocol built on top of ESP-NOW for ESP32 and ESP8266. UniversalMesh eliminates the need for per-device Wi-Fi credentials by utilizing a single **Coordinator** that acts as a transparent bridge between your low-power RF mesh and your MQTT broker.

## 🚀 Key Features
* **MQTT-Native Bridge:** The Coordinator seamlessly translates raw ESP-NOW mesh traffic directly into MQTT topics (`mesh/telemetry/[MAC]`).
* **Zero-Config Nodes:** Sensors automatically sweep Wi-Fi channels to discover the Coordinator in the background. No hardcoded MAC addresses required.
* **Deep-Sleep Optimized:** Nodes cache the Coordinator's routing data in RTC memory, allowing them to wake up, transmit, and sleep in under 100ms.
* **Auto-Relay & Self-Healing:** Every powered node acts as a TTL-based repeater to extend range. Packets dynamically find their path without fixed routing tables.
* **De-duplication:** Built-in Message ID tracking prevents broadcast storms.
* **Modular Architecture:** Clean separation between the RF-layer (`UniversalMesh`) and the IT-layer (`UniversalMeshCoordinator`).

---

## 🏗 Architecture
The network topology is a Star-Mesh hybrid. The network consists of a **Coordinator** (the bridge) and multiple **Nodes** (Sensors, Relays, or Displays). Nodes use the mesh to find the best path, but the logical flow is always Northbound (to the Coordinator/MQTT) or Southbound (from MQTT to a specific node).

### Packet Structure
The protocol relies on a highly efficient packed struct for routing and payload delivery.

| Field | Size (bytes) | Description |
| :--- | :--- | :--- |
| `type` | 1 | Message Type |
| `ttl` | 1 | Time-To-Live (hop limit), decremented at each relay. |
| `msgId` | 4 | Unique Message ID for de-duplication. |
| `destMac` | 6 | Destination node's MAC address (or broadcast `FF:FF:FF:FF:FF:FF`). |
| `srcMac` | 6 | Originating node's MAC address. |
| `appId` | 1 | Application multiplexer (e.g., `0x01` Text, `0x03` Sensor Data). |
| `payloadLen`| 1 | Length of the actual payload. |
| `payload` | 64 | Raw data buffer for the application. |

---
## Message Types
| name | opcode | description |
| :--- | :--- | :--- |
| MESH_TYPE_PING |0x12 | Ping and discovery sweep |
| MESH_TYPE_PONG | 0x13 | reply to PING |
| MESH_TYPE_ACK  | 0x14 | Acknowledgement |
| MESH_TYPE_DATA | 0x15 | Data packet |
| MESH_TYPE_KEY_REQ | 0x16 | Node asks for key, Coordinator replies with key |
| MESH_TYPE_SECURE_DATA | 0x17 | Secure Data packet, Coordinator decrypts and forwards |
| MESH_TYPE_PARANOID_DATA | 0x18 | Secure Data packet, Coordinator acts as a dumb pipe (End-to-End Encrypted E2EE) |

## 📡 The Coordinator: ESP-NOW ↔ MQTT
The production Coordinator utilizes the `UniversalMeshCoordinator` extension. It listens for incoming `DATA` packets from the mesh and automatically bridges them to MQTT.

* **Incoming Mesh Data:** Automatically published to `mesh/telemetry/[SRC_MAC]/[APP_ID]`
* **Node Discovery:** When a node broadcasts a `PING` with its device name, the Coordinator logs the discovery.

---


## 💻 Installation & Project Structure
This project uses a modular `platformio.ini` setup to keep environments clean for contributors.

1. Clone the repository.
2. Provide your Wi-Fi and MQTT credentials by copying `include/secrets.example.h` to `include/secrets.h`.
3. Select your desired environment from the `ini/` configurations.

### Directory Layout
* `examples/coordinators/` - Mesh Coordinators and Production MQTT bridges.
* `examples/sensors/` - Mesh nodes
* `lib/UniversalMesh/` - The core ESP-NOW routing protocol.
* `lib/UniversalMeshCoordinator/` - The MQTT/Wi-Fi bridging extension.

## 🛠 Build & Flash
Dont forget to adjust the **secrets.h**
```C++
#ifndef SECRETS_H
#define SECRETS_H

#define WIFI_SSID "YOUR SSID HERE"
#define WIFI_PASS "YOUR PASSWORD HERE"

#define MQTT_BROKER "MQTT BROKER IP HERE" // Your Home Assistant or Mosquitto IP
#define MQTT_PORT   1883
#define MQTT_USER "your_ha_username"
#define MQTT_PASS "your_ha_password"

#endif
```

Use PlatformIO to build and upload to your specific hardware, currently this only supports esp32-c6, for more platformio.ini's go to **https://github.com/javastraat/UniversalMesh-Gui**

```bash
# Build the production ESP32-C6 Coordinator
pio run -e coordinator_c6 -t upload -t monitor

# Build a generic ESP32 sensor node
pio run -e sensor_node_c6 -t upload -t monitor
```

## ⚙️ Development Stack
* **Core Logic:** C++17 (Arduino/ESP-IDF)
* **dev and test Hardware:** ESP32-C6 (RISC-V)
* **Bridge Protocol:** MQTT 3.1.1 via PubSubClient
* **Serialization:** ArduinoJson 7.x
* **Build Environment:** PlatformIO on Fedora 43

## ⚖️ License
Licensed under the Apache License, Version 2.0.
