import requests
import datetime
import random
import time

# --- CONFIGURATION ---
COORDINATOR_IP = "192.168.12.154"
DESTINATION_MAC = "FF:FF:FF:FF:FF:FF" # Broadcast to all nodes
APP_ID_TEXT = 1  # Tells the Gateway to decode as ASCII

# Random message pool
MESSAGES = [
    "System Nominal",
    "Node Deep Sleep Test",
    "Mesh Heartbeat",
    "Signal Strength: High",
    "Entropy update: 0x42",
    "Amsterdam Lab Online"
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