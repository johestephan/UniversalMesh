# UniversalMesh Library (v1.0.0)

A lightweight, Layer-3 mesh networking protocol built on top of ESP-NOW for ESP32 and ESP8266. This library enables seamless, long-range communication between nodes without the need for a central Wi-Fi router for the mesh itself.

## 🚀 Features
* **Auto-Relay:** Every node acts as a repeater (TTL-based) to extend range.
* **Self-Healing:** No fixed routing tables; packets find the path through broadcast/rebroadcast.
* **Transparent Discovery:** Built-in PING/PONG handling for network mapping.
* **De-duplication:** Prevents broadcast storms using unique Message IDs.
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

## 📡 API Endpoints (Coordinator)
The Coordinator exposes a REST API on Port 80:

| Endpoint | Method | Description |
| :--- | :--- | :--- |
| `/api/tx` | POST | Send data to a node (Hex payload) |
| `/api/discover` | GET | Broadcast a PING to the whole mesh |
| `/api/nodes` | GET | View the current Routing Table (Known Nodes) |

## 💻 Installation
1. Clone to your `lib/` folder.
2. In `platformio.ini`, ensure you use the `pioarduino` platform for ESP32-C6 support.
3. Define your `secrets.h` in the `include/` folder for Wi-Fi credentials.

## ⚖️ License
Licensed under the Apache License, Version 2.0.