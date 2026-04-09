import os
import json
import joblib
import numpy as np
import xgboost
import paho.mqtt.client as mqtt

IDX_TO_PAMAP2 = [1, 2, 3, 4, 5, 6, 7, 12, 13, 16, 17]

ACTIVITY_NAMES = {
    1: "Lying (Sdraiato)",
    2: "Sitting (Seduto)",
    3: "Standing (In piedi)",
    4: "Walking (Camminata)",
    5: "Running (Corsa)",
    6: "Cycling (Bici)",
    7: "Nordic Walk",
    12: "Stairs Up (Salire le scale)",
    13: "Stairs Down (Scendere le scale)",
    16: "Vacuuming (Passare l'aspirapolvere)",
    17: "Ironing (Stirare)"
}

BROKER = os.getenv("MQTT_BROKER", "localhost")
PORT = int(os.getenv("MQTT_PORT", 1883))
TOPIC = os.getenv("MQTT_TOPIC", "iot/har/offload")

MODEL_PATH = "/app/model/xgb_fog.pkl" 

print(f"[FOG] Caricamento modello XGBoost da {MODEL_PATH}...")
try:
    xgb_model = joblib.load(MODEL_PATH)
    print("[FOG] Modello caricato con successo!")
except Exception as e:
    print(f"[FOG] Errore nel caricamento del modello: {e}")
    exit(1)

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"[FOG] Connesso al Broker MQTT. In ascolto sul topic: {TOPIC}")
        client.subscribe(TOPIC)
    else:
        print(f"[FOG] Connessione fallita, codice errore: {rc}")

def on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode('utf-8'))
        
        if "features" in payload:
            features = payload["features"]
            
            if len(features) == 28:
                features_array = np.array(features).reshape(1, -1)
                
                pred_idx = int(xgb_model.predict(features_array)[0])
                real_pamap_id = IDX_TO_PAMAP2[pred_idx]
                activity_name = ACTIVITY_NAMES.get(real_pamap_id, "Unknown/Other")
                
                print("--------------------------------------------------")
                print(f"[FOG INFERENCE] Ricevuto task dall'Edge Gateway.")
                print(f"[FOG INFERENCE] Attività Riconosciuta: {activity_name} (ID PAMAP2: {real_pamap_id})")
                
                response_payload = json.dumps({"activity": activity_name, "id": real_pamap_id})
                client.publish("iot/har/results", response_payload)
                print(f"[FOG MQTT] Risultato inoltrato all'Edge su 'iot/har/results'")
                print("--------------------------------------------------")
            else:
                print(f"[FOG ERROR] Il payload ha {len(features)} feature invece di 28. Controllo Asimmetria fallito.")
    except json.JSONDecodeError:
        print("[FOG ERROR] Ricevuto JSON non valido.")
    except Exception as e:
        print(f"[FOG ERROR] Errore imprevisto durante l'inferenza: {e}")

client = mqtt.Client("FogWorkerNode")
client.on_connect = on_connect
client.on_message = on_message

print(f"[FOG] Connessione al broker {BROKER}:{PORT} in corso...")
client.connect(BROKER, PORT, 60)

client.loop_forever()