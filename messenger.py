import requests
import datetime
import random
import time

# --- CONFIGURATION ---
COORDINATOR_IP = "192.168.2.164"
DESTINATION_MAC = "FF:FF:FF:FF:FF:FF" # Broadcast to all nodes
APP_ID_TEXT = 1  # Tells the Gateway to decode as ASCII

# Random message pool
MESSAGES = [
    # Status
    "System Nominal",
    "All Systems Go",
    "Mesh Heartbeat",
    "Network Stable",
    "Amsterdam Lab Online",
    "Uptime Check OK",
    "Watchdog Reset",
    "Low Power Mode Active",
    "Clock Sync Complete",
    "Boot Sequence Done",
    # Nodes
    "Node Deep Sleep Test",
    "Node Wakeup Triggered",
    "Node Battery Critical",
    "Node Rejoined Mesh",
    "Node Timeout: Retry",
    "New Node Discovered",
    "Orphan Node Detected",
    "Relay Node Active",
    "Edge Node Reporting",
    "Node Pairing Request",
    # Signal / RF
    "Signal Strength: High",
    "Signal Strength: Low",
    "RSSI Threshold Exceeded",
    "Channel Noise Detected",
    "Packet Loss > 5%",
    "Link Quality: Stable",
    "Retransmit Attempt 1",
    "Interference Detected",
    # Data / Sensor
    "Entropy update: 0x42",
    "Sensor Read OK",
    "Temp: 21.4 C",
    "Humidity: 58%",
    "Pressure: 1013 hPa",
    "Motion Detected",
    "Door Event: Open",
    "Door Event: Closed",
    "Light Level: 320 lux",
    "CO2: 842 ppm",
    "Air Quality: Good",
    # Mesh ops
    "Routing Table Updated",
    "Broadcast Flood Limit Hit",
    "TTL Expired: Drop",
    "ACK Received",
    "ACK Timeout",
    "Mesh Rebalancing",
    "Gateway Sync Request",
    "OTA Check: No Update",
    "Config Push Received",
    "Mesh Partition Healed",
]

def send_mesh_update():
    url = f"http://{COORDINATOR_IP}/api/tx"
    
    # 1. Generate the data
    now = datetime.datetime.now().strftime("%H:%M:%S")
    msg = random.choice(MESSAGES)
    full_string = f"[{now}] {msg}"
    
    # 2. Convert string to Hex for the Coordinator API
    # "Hello" -> "48656C6C6F"
    hex_payload = full_string.encode('utf-8').hex().upper()
    
    payload = {
        "dest": DESTINATION_MAC,
        "appId": APP_ID_TEXT,
        "ttl": 4,
        "payload": hex_payload
    }

    print(f"Attempting to send: {full_string}")
    
    try:
        response = requests.post(url, json=payload, timeout=5)
        if response.status_code == 200:
            print(f"Success! Coordinator Response: {response.json()}")
        else:
            print(f"Error: Server returned {response.status_code}")
    except requests.exceptions.ConnectionError:
        print(f"Failed: Could not connect to Coordinator at {COORDINATOR_IP}")

if __name__ == "__main__":
    # Run once, or wrap in a loop for a "heartbeat" simulation
    send_mesh_update()
