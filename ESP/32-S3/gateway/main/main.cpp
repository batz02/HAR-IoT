#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_log.h"
#include "esp_event.h"
#include "mqtt_client.h"

#include "secrets.h"
#include "rf_model.h"
Eloquent::ML::Port::RandomForest classifier;

static const char *TAG = "EDGE_GATEWAY";

#define WIFI_SSID      SECRET_WIFI_SSID
#define WIFI_PASS      SECRET_WIFI_PASS
#define BROKER_URI     SECRET_BROKER_URI

esp_mqtt_client_handle_t mqtt_client = NULL;
bool is_mqtt_connected = false;

extern "C" {
    void app_main(void);
    void espnow_recv_cb(const esp_now_recv_info_t * esp_now_info, const uint8_t *data, int data_len);
}

const int IDX_TO_PAMAP2[11] = {1, 2, 3, 4, 5, 6, 7, 12, 13, 16, 17};

const char* get_activity_name(int pamap2_id) {
    switch(pamap2_id) {
        case 1: return "Lying (Sdraiato)";
        case 2: return "Sitting (Seduto)";
        case 3: return "Standing (In piedi)";
        case 4: return "Walking (Camminata)";
        case 5: return "Running (Corsa)";
        case 6: return "Cycling (Bici)";
        case 7: return "Nordic Walk";
        case 12: return "Stairs Up (Salire le scale)";
        case 13: return "Stairs Down (Scendere le scale)";
        case 16: return "Vacuuming (Passare l'aspirapolvere)";
        case 17: return "Ironing (Stirare)";
        default: return "Sconosciuta/Errore";
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "=== MQTT CONNESSO! Abilito Offloading al Fog ===");
            is_mqtt_connected = true;
            esp_mqtt_client_subscribe(mqtt_client, "iot/har/results", 0);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "=== MQTT DISCONNESSO! Fallback su Edge ML Locale ===");
            is_mqtt_connected = false;
            break;
        case MQTT_EVENT_DATA:
            if (strncmp(event->topic, "iot/har/results", event->topic_len) == 0) {
                ESP_LOGI(TAG, ">> [RISPOSTA DAL FOG]: %.*s", event->data_len, event->data);
            }
            break;
        case MQTT_EVENT_ERROR:
            if (!is_mqtt_connected) {
                ESP_LOGW(TAG, "Impossibile raggiungere il broker MQTT. Riprovo tra poco...");
            } else {
                ESP_LOGE(TAG, "Errore generico MQTT!");
            }
            break;
        default:
            break;
    }
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Connesso al Wi-Fi!");
        
        uint8_t mac[6];
        esp_wifi_get_mac(WIFI_IF_STA, mac);
        ESP_LOGI(TAG, "Indirizzo MAC: {0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X}", 
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        if (mqtt_client == NULL) {
            ESP_LOGI(TAG, "Inizializzazione MQTT in corso...");
            esp_mqtt_client_config_t mqtt_cfg = {};
            mqtt_cfg.broker.address.uri = BROKER_URI;

            mqtt_cfg.network.reconnect_timeout_ms = 10000; 

            mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
            esp_mqtt_client_register_event(mqtt_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
            esp_mqtt_client_start(mqtt_client);
        }
    }
}

void espnow_recv_cb(const esp_now_recv_info_t * esp_now_info, const uint8_t *data, int data_len) {
    ESP_LOGI(TAG, "Ricevuto pacchetto ESP-NOW di %d bytes", data_len);
    float *features = (float *)data;

    char json_payload[1024];
    strcpy(json_payload, "{\"features\": [");
    for(int i=0; i<28; i++) {
        char val_str[32];
        snprintf(val_str, sizeof(val_str), "%.5f%s", features[i], (i < 27) ? ", " : "");
        strcat(json_payload, val_str);
    }
    strcat(json_payload, "]}");

    if (is_mqtt_connected) {
        ESP_LOGI(TAG, "Decisione: Eseguo OFFLOADING del task verso il FOG NODE.");
        esp_mqtt_client_publish(mqtt_client, "iot/har/offload", json_payload, 0, 1, 0);
    } else {
        ESP_LOGW(TAG, "Decisione: Rete Fog non disponibile. Eseguo EDGE ML LOCALE...");
        
        int pred_idx = classifier.predict(features);
        
        int real_pamap_id = IDX_TO_PAMAP2[pred_idx];
        
        ESP_LOGI(TAG, "Risultato Edge: %s (ID PAMAP2: %d)\n", get_activity_name(real_pamap_id), real_pamap_id);
    }
}

void init_wifi_sta() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char*)wifi_config.sta.password, WIFI_PASS);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA)); 
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    uint8_t mac_ap[6];
    esp_read_mac(mac_ap, ESP_MAC_WIFI_SOFTAP);
    ESP_LOGI(TAG, "Indirizzo MAC (SOFTAP): {0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X}", 
             mac_ap[0], mac_ap[1], mac_ap[2], mac_ap[3], mac_ap[4], mac_ap[5]);
}

void app_main(void) {
    esp_log_level_set("esp-tls", ESP_LOG_NONE);
    esp_log_level_set("transport_base", ESP_LOG_NONE);
    esp_log_level_set("mqtt_client", ESP_LOG_WARN);

    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    init_wifi_sta();

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));

    ESP_LOGI(TAG, "=== EDGE GATEWAY IOT AVVIATO ===");
}