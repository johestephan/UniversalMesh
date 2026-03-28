# UniversalMesh Library (v1.0.0)

A lightweight, Layer-3 mesh networking protocol built on top of ESP-NOW for ESP32 and ESP8266. This library enables seamless, long-range communication between nodes without the need for a central Wi-Fi router for the mesh itself.

## 🚀 Features
* **Auto-Relay:** Every node acts as a repeater (TTL-based) to extend range.
* **Self-Healing:** No fixed routing tables; packets find the path through broadcast/rebroadcast.
* **Transparent Discovery:** Built-in PING/PONG handling for network mapping.
* **De-duplication:** Prevents broadcast storms using unique Message IDs.
* **De-duplication:** Prevents broadcast storms by tracking unique Message IDs.
* **Security:** (Optional) ESP-NOW level encryption (CCMP) can be enabled for secure communication.
* **Hybrid Coordinator:** Bridge your mesh to the internet via the ESP32-C6.

## 🛠 Architecture
The network consists of a **Coordinator** (the bridge) and multiple **Nodes** (Relays or End-devices).

### Packet Structure (Binary)
The library uses a 112-byte packed struct:
- `Type`: 0x12 (PING), 0x13 (PONG), 0x15 (DATA).
- `TTL`: Time-To-Live (Hop limit).
- `Src/Dest MAC`: 6-byte hardware addresses.
- `AppId`: Application multiplexer (e.g., 1 for Text, 2 for Raw Hex).
- `Payload`: 64-byte raw data buffer.
The library uses a 112-byte packed struct. This structure is designed to be efficient and includes fields for routing, de-duplication, and application data.

| Field | Size (bytes) | Description |
| :--- | :--- | :--- |
| `msgId` | 16 | Unique Message ID (e.g., a UUID) for de-duplication. |
| `type` | 1 | Message Type: `0x12` (PING), `0x13` (PONG), `0x15` (DATA). |
| `ttl` | 1 | Time-To-Live (hop limit), decremented at each relay. |
| `srcMac` | 6 | Originating node's MAC address. |
| `destMac` | 6 | Destination node's MAC address (or broadcast `FF:FF:FF:FF:FF:FF`). |
| `appId` | 2 | Application multiplexer (e.g., 1 for Text, 2 for Sensor). |
| `payload` | 64 | Raw data buffer for the application. |
| `reserved`| 16 | Reserved for future use (padding to 112 bytes). |

## 📡 API Endpoints (Pseudo - Coordinator)
The Coordinator (coordinator.cpp) is used to send messages as a test. It was not meant for production but as a capanbnility pilot. 
It exposes a REST API on Port 80:

| Endpoint | Method | Description |
| :--- | :--- | :--- |
| `/api/tx` | POST | Send data to a node (Hex payload) |
| `/api/tx` | POST | Send data to a specific node. |
| `/api/discover` | GET | Broadcast a PING to the whole mesh |
| `/api/nodes` | GET | View the current Routing Table (Known Nodes) |
| `/api/nodes` | GET | View the list of dynamically discovered nodes. |

### Example: Sending Data via `/api/tx`

**Request Body (JSON)**
```json
{
  "dest": "AA:BB:CC:DD:EE:FF",
  "payload": "48656c6c6f204d657368"
}
```

## 📡 EspNow -> MQTT Coordinator
(coming soon)

## 💻 Installation
1. Clone to your `lib/` folder.
2. In `platformio.ini`, ensure you use the `pioarduino` platform for ESP32-C6 support.
3. Define your `secrets.h` in the `include/` folder for Wi-Fi credentials.

## 🛠 Build
To build the different examples
```
pio run -e coordinator_c6 -t upload -t monitor
```

## Development
- **Core Logic:** C++17 (Arduino/ESP-IDF)
- **Coordination:** ESP32-C6 (RISC-V)
- **Bridge Protocol:** MQTT 3.1.1 via PubSubClient
- **Serialization:** ArduinoJson 7.x
- **Development:** PlatformIO on Fedora 43

## ⚖️ License
Licensed under the Apache License, Version 2.0.