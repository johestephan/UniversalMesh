# UniversalMesh Library (v1.0.0)

A lightweight, Layer-3 mesh networking protocol built on top of ESP-NOW for ESP32 and ESP8266. Enables seamless, long-range communication between nodes without a central Wi-Fi router.

## Features
* **Auto-Relay:** Every node acts as a repeater (TTL-based) to extend range.
* **Self-Healing:** No fixed routing tables; packets find the path through broadcast/rebroadcast.
* **Transparent Discovery:** Built-in PING/PONG handling for network mapping.
* **De-duplication:** Prevents broadcast storms using unique Message IDs.
* **Hybrid Coordinator:** Bridge your mesh to the internet via Wi-Fi or Ethernet.
* **Node Announce:** Sensors broadcast their name on boot and heartbeat; coordinator stores and displays it.

## Architecture
The network consists of a **Coordinator** (the bridge) and multiple **Sensor Nodes**.

### Packet Structure (Binary)
The library uses a fixed-size packed struct:
- `Type`: `0x12` (PING), `0x13` (PONG), `0x14` (ACK), `0x15` (DATA)
- `TTL`: Time-To-Live hop limit
- `Src/Dest MAC`: 6-byte hardware addresses
- `AppId`: Application multiplexer — see table below
- `Payload`: Raw data buffer

### AppId Reference
| AppId | Name | Description |
| :--- | :--- | :--- |
| `0x01` | Text | Human-readable string payload |
| `0x02` | Raw Hex | Raw binary payload |
| `0x05` | Heartbeat | Periodic alive signal from sensor node |
| `0x06` | Node Announce | Node broadcasts its name (string); coordinator stores it in the node table |

## Supported Hardware

### Coordinators
| Environment | Board | Notes |
| :--- | :--- | :--- |
| `coordinator_c6` | ESP32-C6 DevKitC-1 | Default; RGB LED, USB CDC |
| `coordinator_s3` | ESP32-S3 DevKitC-1 | 4MB flash |
| `coordinator_t8_s3` | LilyGo T8-S3 | 16MB flash, PSRAM |
| `coordinator_eth_elite` | LilyGo T-ETH Elite | 16MB flash, PSRAM, Ethernet |
| `coordinator_eth_elite_ota` | LilyGo T-ETH Elite | Same as above + OTA upload over network |

### Sensor Nodes
| Environment | Board | Notes |
| :--- | :--- | :--- |
| `sensor_node_esp32` | Generic ESP32 Dev Module | 4MB flash |
| `sensor_node_c6` | ESP32-C6 DevKitC-1 | USB CDC |
| `sensor_node_t8_s3` | LilyGo T8-S3 | 16MB flash, PSRAM |
| `sensor_wemos_d1` | Wemos D1 / mini (ESP8266) | SHT30 sensor; optional OLED shield via `HAS_DISPLAY_SHIELD` flag |

### Display Gateway
| Environment | Board | Notes |
| :--- | :--- | :--- |
| `gateway_esp8266` | NodeMCU v2 (ESP8266) | Receives mesh packets and displays them on OLED |

## Coordinator Web UI & API
The coordinator exposes a web UI and REST API on port 80.

### Web UI
- Live packet log with relay origin, labelled packet types (heartbeat, discovery, data, etc.)
- Node table showing MAC, last-seen, and node name (from AppId `0x06`)
- Mesh channel configuration
- Serial log viewer
- Reboot button

### REST API
| Endpoint | Method | Description |
| :--- | :--- | :--- |
| `/api/tx` | POST | Send data to a node (`destMac`, `appId`, hex `payload`) |
| `/api/discover` | GET | Broadcast a PING to the whole mesh |
| `/api/nodes` | GET | Node table (MAC, last-seen, name) |
| `/api/status` | GET | Coordinator status (uptime, IP, channel, etc.) |
| `/api/log` | GET | Recent packet log entries |
| `/api/serial` | GET | Recent serial output |
| `/api/mesh/channel` | POST | Set ESP-NOW channel (optionally reboot) |
| `/api/reboot` | POST | Reboot the coordinator |

## Sensor Node Behaviour
Each sensor node:
1. On boot, sends an **AppId `0x06`** Node Announce packet with its `nodeName` string.
2. Every `HEARTBEAT_INTERVAL` ms (default 60 s), sends an **AppId `0x05`** heartbeat and re-sends the announce.
3. Sends sensor readings (e.g. temperature/humidity) as **AppId `0x01`** data packets.

The `nodeName` is defined per-node with `#define nodeName "my-node"` at the top of the source file.

## MQTT Bridge
Coming soon — the coordinator will forward mesh data to an MQTT broker.

## Installation
1. Clone to your `lib/` folder.
2. In `platformio.ini`, ensure you use the `pioarduino` platform for ESP32-C6/S3 support.
3. Create `include/secrets.h` with your Wi-Fi credentials.

## Build
Flash and monitor a specific environment:
```
pio run -e coordinator_c6 -t upload -t monitor
```
OTA upload (ETH Elite):
```
pio run -e coordinator_eth_elite_ota -t upload
```

## Development Stack
- **Core Logic:** C++17 (Arduino/ESP-IDF)
- **Coordinator MCU:** ESP32-C6 (RISC-V), ESP32-S3, LilyGo T-ETH Elite
- **Sensor MCU:** ESP32, ESP32-C6, ESP32-S3, ESP8266
- **Web Framework:** ESP Async WebServer
- **Serialization:** ArduinoJson 7.x
- **MQTT:** PubSubClient 2.x
- **Build System:** PlatformIO

## License
Licensed under the Apache License, Version 2.0.
