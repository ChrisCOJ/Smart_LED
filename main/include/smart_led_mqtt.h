#ifndef SMART_LED_MQTT_H
#define SMART_LED_MQTT_H


#include "esp_err.h"
#include "esp_log.h"

#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"


enum MQTT_CONN_FAIL_CODES {
    SOCKET_CREATION_FAILED = -1,
    SOCKET_CONNECTION_FAILED = -2,
    INVALID_ADDRESS = -3,
    SERVER_IP_NOT_FOUND = -4,
};


int setup_mqtt_connection();

void process_broker_messages(void *arg);

esp_err_t smart_led_wifi_init(void);

esp_err_t smart_led_wifi_connect(char* wifi_ssid, char* wifi_password);

esp_err_t smart_led_wifi_disconnect(void);

esp_err_t smart_led_wifi_deinit(void);

#endif