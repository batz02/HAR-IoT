#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <espnow.h>

extern "C" {
  #include <user_interface.h>
}

#include "secrets.h"
#include "payload.h"

uint8_t esp32_mac[] = SECRET_ESP32_MAC;
const char* WIFI_SSID = SECRET_WIFI_SSID;

int current_window = 0;

void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  Serial.print("Stato invio ESP-NOW: ");
  if (sendStatus == 0){
    Serial.println("Consegna OK -> Edge Gateway raggiunto");
  } else{
    Serial.println("Consegna Fallita -> Edge Gateway non trovato");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.println("Cerco il canale radio del Wi-Fi di casa...");
  int wifi_channel = 1; 
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == WIFI_SSID) {
      wifi_channel = WiFi.channel(i);
      Serial.printf("-> Wi-Fi %s trovato sul Canale %d!\n", WIFI_SSID, wifi_channel);
      break;
    }
  }

  wifi_promiscuous_enable(1);
  wifi_set_channel(wifi_channel);
  wifi_promiscuous_enable(0);

  if (esp_now_init() != 0) {
    Serial.println("Errore inizializzazione ESP-NOW");
    return;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_register_send_cb(OnDataSent);

  esp_now_add_peer(esp32_mac, ESP_NOW_ROLE_SLAVE, wifi_channel, NULL, 0);
  
  Serial.println("Nodo Sensore Pronto. Inizio trasmissione...");
}

void loop() {
  size_t row_size = sizeof(payload_data[current_window]);
  
  Serial.printf("\n[SENSORE] Inviando Finestra %d/%d all'Edge Gateway...\n", current_window + 1, NUM_WINDOWS);
  
  esp_now_send(esp32_mac, (uint8_t *)payload_data[current_window], row_size);
  
  current_window = (current_window + 1) % NUM_WINDOWS;
  
  delay(4000);
}