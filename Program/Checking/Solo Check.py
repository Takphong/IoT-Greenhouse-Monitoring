#Use This incase that you have only 1 M5Stack and want to test it's
import paho.mqtt.client as mqtt

def on_message(client, userdata, msg):
    print(msg.payload.decode())

client = mqtt.Client()
client.connect("localhost",1883,60)
client.subscribe("greenhouse/fern/data")
client.loop_forever()