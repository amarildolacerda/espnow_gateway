#include "espnow_handler.h"
#include "config.h"
#include "sensor_registry.h"
#include "bridge_client.h"
#include <espnow.h>
#include <ESP8266WiFi.h>
#include <Arduino.h>

static bool s_pairing_mode = false;
static unsigned long s_pairing_start = 0;
static uint8_t s_gateway_mac[6];
static unsigned long s_last_heartbeat = 0;
static unsigned long s_rx_count = 0;
static unsigned long s_ack_count = 0;
static unsigned long s_crc_errors = 0;

static void send_ack(const uint8_t *mac, uint16_t sequence, uint8_t status, uint8_t slot);
static void send_pair_response(const uint8_t *mac, uint16_t sequence, uint16_t slot);

extern "C" void espnow_recv_cb(uint8_t *mac, uint8_t *data, uint8_t len) {
    if (len < sizeof(espnow_header_t)) {
        s_crc_errors++;
        return;
    }

    espnow_header_t *header = (espnow_header_t*)data;
    
    if (header->version != ESPNOW_PROTOCOL_VERSION) {
        s_crc_errors++;
        return;
    }

    s_rx_count++;
    int slot = sensor_registry_find_by_mac(mac);

    switch (header->msg_type) {
        case ESPNOW_MSG_PAIR_REQUEST: {
            if (!s_pairing_mode) {
                send_ack(mac, header->sequence, PAIR_STATUS_DENIED, 0xFF);
                return;
            }
            
            espnow_pair_request_t *req = (espnow_pair_request_t*)data;
            int free_slot = sensor_registry_find_free_slot();
            
            if (free_slot < 0) {
                send_ack(mac, header->sequence, PAIR_STATUS_FULL, 0xFF);
                return;
            }
            
            char default_name[32];
            snprintf(default_name, sizeof(default_name), "%s %d", 
                     (req->sensor_type == SENSOR_TYPE_TEMP_HUM) ? "Temp+Hum" :
                     (req->sensor_type == SENSOR_TYPE_CONTACT) ? "Contato" :
                     (req->sensor_type == SENSOR_TYPE_MOTION) ? "Movimento" :
                     (req->sensor_type == SENSOR_TYPE_GAS) ? "Gas" :
                     (req->sensor_type == SENSOR_TYPE_RAIN) ? "Chuva" : "Tanque",
                     free_slot + 1);
            
            sensor_registry_add(mac, req->sensor_type, free_slot, default_name);
            send_pair_response(mac, header->sequence, free_slot);
            
            bridge_client_register_sensor(sensor_registry_get(free_slot));
            
            Serial.printf("[ESP-NOW] Paired sensor slot %d: %02X:%02X:%02X:%02X:%02X:%02X type=%d\n",
                          free_slot, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], req->sensor_type);
            break;
        }
        
        case ESPNOW_MSG_SENSOR_DATA: {
            if (slot < 0) {
                send_ack(mac, header->sequence, PAIR_STATUS_DENIED, 0xFF);
                return;
            }
            
            sensor_registry_update_state(slot, header, header->payload, header->payload_len);
            send_ack(mac, header->sequence, PAIR_STATUS_OK, slot);
            s_ack_count++;
            
            bridge_client_send_state(sensor_registry_get(slot));
            break;
        }
        
        case ESPNOW_MSG_HEARTBEAT: {
            if (slot >= 0) {
                sensor_registry_get(slot)->last_seen = millis();
                sensor_registry_get(slot)->online = true;
                send_ack(mac, header->sequence, PAIR_STATUS_OK, slot);
            }
            break;
        }
        
        default:
            break;
    }
}

void send_ack(const uint8_t *mac, uint16_t sequence, uint8_t status, uint8_t slot) {
    espnow_ack_t ack = {
        .msg_type = ESPNOW_MSG_ACK,
        .sequence = sequence,
        .sensor_mac = {0},
        .status = status,
        .assigned_slot = slot
    };
    mac_copy(ack.sensor_mac, mac);
    
    esp_now_send((uint8_t*)mac, (uint8_t*)&ack, sizeof(ack));
}

void send_pair_response(const uint8_t *mac, uint16_t sequence, uint16_t slot) {
    espnow_pair_response_t resp = {
        .msg_type = ESPNOW_MSG_PAIR_RESPONSE,
        .sequence = sequence,
        .sensor_mac = {0},
        .gateway_mac = {0},
        .status = PAIR_STATUS_OK,
        .assigned_slot = slot
    };
    mac_copy(resp.sensor_mac, mac);
    mac_copy(resp.gateway_mac, s_gateway_mac);
    
    esp_now_send((uint8_t*)mac, (uint8_t*)&resp, sizeof(resp));
}

bool espnow_handler_init() {
    WiFi.mode(WIFI_STA);
    WiFi.macAddress(s_gateway_mac);
    
    if (esp_now_init() != 0) {
        Serial.println("[ESP-NOW] Init failed");
        return false;
    }
    
    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    esp_now_register_recv_cb(espnow_recv_cb);
    wifi_set_channel(ESP_NOW_CHANNEL);
    
    Serial.printf("[ESP-NOW] Initialized on channel %d, MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  ESP_NOW_CHANNEL, s_gateway_mac[0], s_gateway_mac[1], s_gateway_mac[2],
                  s_gateway_mac[3], s_gateway_mac[4], s_gateway_mac[5]);
    return true;
}

void espnow_handler_loop() {
    if (s_pairing_mode && millis() - s_pairing_start > PAIRING_WINDOW_MS) {
        s_pairing_mode = false;
        digitalWrite(STATUS_LED_GPIO, HIGH);
        Serial.println("[ESP-NOW] Pairing mode timeout");
    }
    
    if (millis() - s_last_heartbeat > HEARTBEAT_INTERVAL_MS) {
        s_last_heartbeat = millis();
        
        for (int i = 0; i < MAX_VIRTUAL_SENSORS; i++) {
            virtual_sensor_t *s = sensor_registry_get(i);
            if (s && s->paired) {
                unsigned long elapsed = millis() - s->last_seen;
                if (s->online && elapsed > SENSOR_TIMEOUT_MS) {
                    s->online = false;
                    Serial.printf("[ESP-NOW] Sensor slot %d offline (last seen %lu ms ago)\n", i, elapsed);
                }
            }
        }
    }
}

bool espnow_start_pairing() {
    if (sensor_registry_count_paired() >= MAX_VIRTUAL_SENSORS) {
        Serial.println("[ESP-NOW] Max sensors reached");
        return false;
    }
    s_pairing_mode = true;
    s_pairing_start = millis();
    digitalWrite(STATUS_LED_GPIO, LOW);
    Serial.println("[ESP-NOW] Pairing mode started (60s)");
    return true;
}

void espnow_stop_pairing() {
    s_pairing_mode = false;
    digitalWrite(STATUS_LED_GPIO, HIGH);
    Serial.println("[ESP-NOW] Pairing mode stopped");
}

bool espnow_is_pairing() {
    return s_pairing_mode;
}

unsigned long espnow_get_rx_count() { return s_rx_count; }
unsigned long espnow_get_ack_count() { return s_ack_count; }
unsigned long espnow_get_crc_errors() { return s_crc_errors; }
uint8_t* espnow_get_gateway_mac() { return s_gateway_mac; }