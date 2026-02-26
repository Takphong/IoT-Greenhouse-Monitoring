import paho.mqtt.client as mqtt
import json
import hashlib

BROKER = "broker.emqx.io"
PORT = 1883
DATA_TOPIC = "greenhouse/fern/data"
CONTROL_TOPIC = "greenhouse/fern/control"

# ---------- VERIFY HASH ----------
def verify_hash(payload_json, received_hash):
    calculated = hashlib.sha256(payload_json.encode()).hexdigest()
    return calculated == received_hash

# ---------- ON CONNECT ----------
def on_connect(client, userdata, flags, rc):
    print("Connected to MQTT")
    client.subscribe(DATA_TOPIC)

# ---------- ON MESSAGE ----------
def on_message(client, userdata, msg):
    print("\n📥 Data received from Pot")

    data = json.loads(msg.payload.decode())

    raw_payload = json.dumps(data["data"])
    received_hash = data["hash"]

    if verify_hash(raw_payload, received_hash):
        print("✅ Hash verified")

        print("Soil:", data["data"]["soil"])
        print("Temp:", data["data"]["temp"])
        print("Light:", data["data"]["light"])
        print("TiltY:", data["data"]["tiltY"])
        print("Pump:", data["data"]["pump"])
    else:
        print("❌ Hash mismatch!")

# ---------- CREATE CLIENT ----------
client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

client.connect(BROKER, 1883, 60)

# ---------- CONTROL LOOP ----------
print("Type 1 = WATER ON")
print("Type 2 = WATER OFF")

client.loop_start()

while True:
    cmd = input("Command: ")

    if cmd == "1":
        client.publish(CONTROL_TOPIC, "WATER_ON")
        print("💧 Sent WATER_ON")

    elif cmd == "2":
        client.publish(CONTROL_TOPIC, "WATER_OFF")
        print("🛑 Sent WATER_OFF")