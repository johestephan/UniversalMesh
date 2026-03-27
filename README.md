# UniversalMesh Library (v1.0.0)

A lightweight, Layer-3 mesh networking protocol built on top of ESP-NOW for ESP32 and ESP8266. Enables seamless, long-range communication between nodes without a central Wi-Fi router.

## Features

- **Auto-Relay:** Every node acts as a repeater (TTL-based) to extend range.
- **Self-Healing:** No fixed routing tables; packets find their path through broadcast/rebroadcast.
- **Transparent Discovery:** Built-in PING/PONG handling for network mapping.
- **De-duplication:** Prevents broadcast storms using unique Message IDs.
- **Hybrid Coordinator:** Bridge your mesh to the internet via Wi-Fi or Ethernet.
- **Node Announce:** Sensors broadcast their name on boot and heartbeat; coordinator stores and displays it.
- **MQTT Bridge:** Coordinator automatically forwards mesh data to an MQTT broker (ETH Elite).
- **Real-Time Web Dashboard:** Live SSE-pushed updates — no polling. Packet log, node table, topology map and serial console update the instant a packet arrives.
- **Force-Directed Topology Map:** Visual graph of the mesh — nodes and relay paths inferred from packet headers, animated in real-time.
- **NTP Time Sync:** Coordinator syncs to NTP; packet timestamps shown as wall-clock time in the dashboard.
- **PWA Support:** Dashboard is installable on mobile as a standalone app (iOS & Android).

## Architecture

The network consists of a **Coordinator** (the bridge) and multiple **Sensor Nodes**.

### Packet Structure (Binary)

The library uses a fixed-size packed struct:

| Field | Size | Description |
| :--- | :--- | :--- |
| `type` | 1 byte | `0x12` PING, `0x13` PONG, `0x14` ACK, `0x15` DATA |
| `ttl` | 1 byte | Time-to-live hop limit (default 4) |
| `msgId` | 4 bytes | Unique message ID for deduplication |
| `destMac` | 6 bytes | Destination or `FF:FF:FF:FF:FF:FF` for broadcast |
| `srcMac` | 6 bytes | Original sender MAC |
| `appId` | 1 byte | Application multiplexer — see table below |
| `payloadLen` | 1 byte | Payload length (max 64) |
| `payload` | 64 bytes | Raw binary payload |

### AppId Reference

| AppId | Name | Description |
| :--- | :--- | :--- |
| `0x00` | Protocol | Reserved for mesh control (PING/PONG) |
| `0x01` | Text | Human-readable string payload |
| `0x02` | Raw Hex | Raw binary payload |
| `0x05` | Heartbeat | Periodic alive signal (single byte) |
| `0x06` | Node Announce | Node broadcasts its name; coordinator stores it in the node table |

## Supported Hardware

### Coordinators

| Environment | Board | Flash | PSRAM | Ethernet | OTA | RGB LED |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `coordinator_c6` | ESP32-C6 DevKitC-1 | 4 MB | — | — | — | ✓ |
| `coordinator_s3` | ESP32-S3 DevKitC-1 | 4 MB | — | — | — | — |
| `coordinator_t8_s3` | LilyGo T8-S3 | 16 MB | ✓ | — | — | — |
| `coordinator_eth_elite` | LilyGo T-ETH Elite | 16 MB | ✓ | ✓ | ✓ | — |
| `coordinator_eth_elite_ota` | LilyGo T-ETH Elite | 16 MB | ✓ | ✓ | ✓ (IP) | — |

### Sensor Nodes

| Environment | Board | Notes |
| :--- | :--- | :--- |
| `sensor_node_esp32` | Generic ESP32 Dev Module | 4 MB flash |
| `sensor_node_c6` | ESP32-C6 DevKitC-1 | USB CDC |
| `sensor_node_t8_s3` | LilyGo T8-S3 | 16 MB flash, PSRAM |
| `sensor_wemos_d1` | Wemos D1 / mini (ESP8266) | SHT30 sensor; optional OLED via `HAS_DISPLAY_SHIELD` |

### Display Gateway

| Environment | Board | Notes |
| :--- | :--- | :--- |
| `gateway_esp8266` | NodeMCU v2 (ESP8266) | Receives mesh packets and displays them on SSD1306 OLED |

## Coordinator Web Dashboard

The coordinator serves a responsive single-page dashboard on port 80.

### Panels

| Panel | Description |
| :--- | :--- |
| **ESP** | Chip model, cores, CPU MHz, flash size, free heap, MAC, uptime, NTP time |
| **WiFi** | SSID, IP, gateway, RSSI, channel |
| **Ethernet** | Status, IP, subnet, gateway, DNS, MAC, link speed/duplex *(ETH Elite only)* |
| **Mesh Nodes** | Live list of known nodes — MAC, last-seen counter, resolved name. Green dot = seen <120 s, red = stale |
| **Mesh Channel** | Dropdown to switch ESP-NOW channel (1–13), persisted to NVS *(ETH Elite only)* |
| **Send Message** | Inject a text packet to any node or broadcast directly from the browser |
| **Packet Log** | Paginated live log (200 entries with PSRAM, 10 without). Shows type, sender, app ID, payload, timestamp. Relayed packets highlighted |
| **Topology Map** | Force-directed canvas graph of all nodes and relay paths, inferred from packet headers. Click a node for details |
| **Serial Console** | Live stream of the coordinator's internal log — like a web-based serial monitor |

### UI Features

- **SSE push** — the browser opens a single `GET /api/events` stream; the ESP32 pushes `packet` and `serial` events the instant they happen. No polling for the log, topology or console.
- **Dark / light theme** toggle, persisted in `localStorage`
- **Installable PWA** — add to home screen on iOS/Android for a standalone app experience
- **NTP timestamps** — packet log shows wall-clock time (e.g. `23:18:12`) instead of a relative counter when NTP is synced

### REST API

| Endpoint | Method | Description |
| :--- | :--- | :--- |
| `/api/events` | GET (SSE) | Server-Sent Events stream — `packet` and `serial` event types |
| `/api/status` | GET | Coordinator status (uptime, heap, IP, channel, NTP time, ETH info) |
| `/api/nodes` | GET | Node table (MAC, last-seen seconds, name) |
| `/api/log` | GET | Recent packet log (type, src, origSrc, appId, payload, age) |
| `/api/serial` | GET | Recent serial output (last 40 lines) |
| `/api/tx` | POST | Send a packet into the mesh (`dest`, `appId`, hex `payload`, `ttl`) |
| `/api/discover` | GET | Broadcast a PING to discover all mesh nodes |
| `/api/mesh/channel` | POST | Set ESP-NOW channel 1–13, optionally reboot *(ETH Elite only)* |
| `/api/reboot` | POST | Reboot the coordinator |

## Sensor Node Behaviour

Each sensor node:
1. On boot, sends an **AppId `0x06`** Node Announce packet with its `nodeName` string.
2. Every `HEARTBEAT_INTERVAL` ms (default 60 s), sends an **AppId `0x05`** heartbeat and re-sends the announce.
3. Sends sensor readings (e.g. temperature/humidity) as **AppId `0x01`** data packets.

The `nodeName` is defined per-node with `#define nodeName "my-node"` at the top of the source file.

## ETH Elite Features

The `coordinator_eth_elite` / `coordinator_eth_elite_ota` builds add:

- **W5500 Ethernet** as primary network. On boot, waits up to 5 s for cable, then up to 60 s for DHCP. Falls back to WiFi if no cable is detected.
- **MQTT Bridge** — DATA packets (appId `0x01`, `0x05`, `0x06`) are automatically published to:
  ```
  {MESH_NETWORK}/{MESH_HOSTNAME}/nodes/{nodeName}/{appId}
  ```
  Broker, port and credentials are set in `secrets.h`.
- **NTP sync** — wall-clock time displayed in dashboard and packet log.
- **OTA updates** — flash over the network:
  ```
  pio run -e coordinator_eth_elite_ota -t upload
  ```
- **mDNS** — reachable at `universalmesh.local` (or your custom `MESH_HOSTNAME`).
- **Mesh channel selector** — change ESP-NOW channel (1–13) via the dashboard, persisted to NVS across reboots.

## RGB LED (ESP32-C6 Coordinator)

The C6 coordinator uses pin 8 (NeoPixel) as a status indicator:

| State | Colour | Pattern |
| :--- | :--- | :--- |
| Connecting to WiFi | Green | Slow blink |
| WiFi connected | Green | Steady |
| WiFi failed | Red | Steady |
| Packet received | Yellow | 3 quick flashes |
| Packet transmitted | Blue | 3 quick flashes |

## `messenger.py` — Python Test Client

A CLI tool to inject packets into the mesh via the coordinator REST API.

```
python3 messenger.py                          # send one random message
python3 messenger.py -t "Hello mesh"          # send custom text
python3 messenger.py -l -i 2.0               # loop, one message every 2 s
python3 messenger.py -l -n 10 -i 1.0         # send 10 messages, 1 s apart
```

Edit `COORDINATOR_IP` at the top of the file to point to your coordinator.

## Configuration — `include/secrets.h`

Create this file before building:

```cpp
// WiFi (all coordinators)
#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-password"

// MQTT (ETH Elite only)
#define MQTT_BROKER "192.168.1.x"   // broker IP or hostname
#define MQTT_PORT   1883
#define MQTT_USER   ""              // leave empty if not required
#define MQTT_PASS   ""

// OTA (ETH Elite only, optional override)
// #define OTA_PASSWORD "mesh1234"

// NTP (ETH Elite only, optional overrides)
// #define NTP_SERVER              "pool.ntp.org"
// #define NTP_GMT_OFFSET_SEC      3600    // e.g. UTC+1
// #define NTP_DAYLIGHT_OFFSET_SEC 3600
```

## Installation

1. Clone or copy the `lib/UniversalMesh/` folder into your project's `lib/` directory.
2. Create `include/secrets.h` with your credentials (see above).
3. In `platformio.ini`, use the `pioarduino` platform for ESP32-C6/S3 support.

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

| Component | Technology |
| :--- | :--- |
| Core firmware | C++17 (Arduino / ESP-IDF) |
| Web server | ESP Async WebServer (mathieucarbou fork) |
| JSON | ArduinoJson 7.x |
| MQTT | PubSubClient 2.x |
| Build system | PlatformIO |
| Coordinator MCU | ESP32-C6 (RISC-V), ESP32-S3, LilyGo T-ETH Elite |
| Sensor MCU | ESP32, ESP32-C6, ESP32-S3, ESP8266 |

## License

Licensed under the Apache License, Version 2.0.
