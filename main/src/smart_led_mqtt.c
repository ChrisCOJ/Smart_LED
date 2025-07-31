#include <string.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

#include "esp_netif.h"
#include "lwip/sockets.h"

#include "esp_log.h"
#include "esp_event.h"

#include <inttypes.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>            // struct addrinfo
#include <arpa/inet.h>

#include "smart_led_mqtt.h"
#include "../../components/mqtt_protocl_lib/include/mqtt_parser.h"
#include "../../components/mqtt_protocl_lib/include/mqtt_protocol.h"
#include "../../components/mqtt_protocl_lib/include/mqtt_util.h"
#include "../../components/mqtt_protocl_lib/include/mqtt_client_api.h"
#include "env_config.h"


#define SERVER_PORT     1883
#define MQTT_TAG        "MQTT"
#define TCP_TAG         "TCP"
#define TAG             "Wi-fi"

#define WIFI_AUTHMODE WIFI_AUTH_WPA2_PSK

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1


static vector subscription_list = { .item_size = sizeof(app_subscription_entry) };

static const int WIFI_RETRY_ATTEMPT = 3;
static int wifi_retry_count = 0;

static esp_netif_t *network_if = NULL;
static esp_event_handler_instance_t ip_event_handler;
static esp_event_handler_instance_t wifi_event_handler;

static EventGroupHandle_t s_wifi_event_group = NULL;

// ------ Subsciption actions ------
extern void turn_on_led(void *arg);
extern void turn_off_led(void *arg);
// ---------------------------------


static void ip_event_cb(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    ESP_LOGI(TAG, "Handling IP event, event code 0x%" PRIx32, event_id);
    switch (event_id)
    {
    case (IP_EVENT_STA_GOT_IP):
        ip_event_got_ip_t *event_ip = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event_ip->ip_info.ip));
        wifi_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        break;
    case (IP_EVENT_STA_LOST_IP):
        ESP_LOGI(TAG, "Lost IP");
        break;
    case (IP_EVENT_GOT_IP6):
        ip_event_got_ip6_t *event_ip6 = (ip_event_got_ip6_t *)event_data;
        ESP_LOGI(TAG, "Got IPv6: " IPV6STR, IPV62STR(event_ip6->ip6_info.ip));
        wifi_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        break;
    default:
        ESP_LOGI(TAG, "IP event not handled");
        break;
    }
}

static void wifi_event_cb(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    ESP_LOGI(TAG, "Handling Wi-Fi event, event code 0x%" PRIx32, event_id);

    switch (event_id)
    {
    case (WIFI_EVENT_WIFI_READY):
        ESP_LOGI(TAG, "Wi-Fi ready");
        break;
    case (WIFI_EVENT_SCAN_DONE):
        ESP_LOGI(TAG, "Wi-Fi scan done");
        break;
    case (WIFI_EVENT_STA_START):
        ESP_LOGI(TAG, "Wi-Fi started, connecting to AP...");
        esp_wifi_connect();
        break;
    case (WIFI_EVENT_STA_STOP):
        ESP_LOGI(TAG, "Wi-Fi stopped");
        break;
    case (WIFI_EVENT_STA_CONNECTED):
        ESP_LOGI(TAG, "Wi-Fi connected");
        break;
    case (WIFI_EVENT_STA_DISCONNECTED):
        ESP_LOGI(TAG, "Wi-Fi disconnected");
        if (wifi_retry_count < WIFI_RETRY_ATTEMPT) {
            ESP_LOGI(TAG, "Retrying to connect to Wi-Fi network...");
            esp_wifi_connect();
            wifi_retry_count++;
        } else {
            ESP_LOGI(TAG, "Failed to connect to Wi-Fi network");
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        break;
    case (WIFI_EVENT_STA_AUTHMODE_CHANGE):
        ESP_LOGI(TAG, "Wi-Fi authmode changed");
        break;
    default:
        ESP_LOGI(TAG, "Wi-Fi event not handled");
        break;
    }
}


esp_err_t smart_led_wifi_init(void) {
    // Initialize Non-Volatile Storage (NVS)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    s_wifi_event_group = xEventGroupCreate();

    ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize TCP/IP network stack");
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create default event loop");
        return ret;
    }

    ret = esp_wifi_set_default_wifi_sta_handlers();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set default handlers");
        return ret;
    }

    network_if = esp_netif_create_default_wifi_sta();
    if (network_if == NULL) {
        ESP_LOGE(TAG, "Failed to create default WiFi STA interface");
        return ESP_FAIL;
    }

    // Wi-Fi stack configuration parameters
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_cb,
                                                        NULL,
                                                        &wifi_event_handler));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &ip_event_cb,
                                                        NULL,
                                                        &ip_event_handler));
    return ret;
}



esp_err_t smart_led_wifi_connect(char* wifi_ssid, char* wifi_password) {
    wifi_config_t wifi_config = {
        .sta = {
            // this sets the weakest authmode accepted in fast scan mode (default)
            .threshold.authmode = WIFI_AUTHMODE,
        },
    };

    strncpy((char*)wifi_config.sta.ssid, wifi_ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, wifi_password, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE)); // default is WIFI_PS_MIN_MODEM
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM)); // default is WIFI_STORAGE_FLASH

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    ESP_LOGI(TAG, "Connecting to Wi-Fi network: %s", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to Wi-Fi network: %s", wifi_config.sta.ssid);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to Wi-Fi network: %s", wifi_config.sta.ssid);
        return ESP_FAIL;
    }

    ESP_LOGE(TAG, "Unexpected Wi-Fi error");
    return ESP_FAIL;
}



esp_err_t smart_led_wifi_disconnect(void) {
    if (s_wifi_event_group) {
        vEventGroupDelete(s_wifi_event_group);
    }

    return esp_wifi_disconnect();
}

esp_err_t smart_led_wifi_deinit(void) {
    esp_err_t ret = esp_wifi_stop();
    if (ret == ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGE(TAG, "Wi-Fi stack not initialized");
        return ret;
    }

    ESP_ERROR_CHECK(esp_wifi_deinit());
    ESP_ERROR_CHECK(esp_wifi_clear_default_wifi_driver_and_handlers(network_if));
    esp_netif_destroy(network_if);

    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, ip_event_handler));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler));

    return ESP_OK;
}



void process_broker_messages(void *arg) {
    int sock = *(int *)arg;
    int msg_number = 0;
    uint16_t packet_id = 1;     // Packet ID 0 is not allowed

    while (1) {
        mqtt_packet packet = {0};
        uint8_t *original_buffer = malloc(DEFAULT_BUFF_SIZE);
        if (!original_buffer) vTaskDelete(NULL);;
        uint8_t *buffer = original_buffer;

        int bytes_read = read(sock, buffer, DEFAULT_BUFF_SIZE);
        if (bytes_read <= 0) {
            ESP_LOGE(MQTT_TAG, "bytes read = %d\n", bytes_read);
            ESP_LOGE(MQTT_TAG, "Server communication channel closed!");
            free(original_buffer);
            vTaskDelete(NULL);;
        }
        ESP_LOGI(MQTT_TAG, "Buffer Size = %d\n", bytes_read);
        for (int i = 0; i < bytes_read; ++i) {
            ESP_LOGI(MQTT_TAG, "%02X\n", buffer[i]);
        }

        // Parse the message received from the client.
        int packet_type = unpack(&packet, &buffer, bytes_read);  // Reconstruct bytestream as mqtt_packet and store in packet
        if (msg_number == 0 && packet_type != MQTT_CONNACK) {
            ESP_LOGE(MQTT_TAG, "Unexpected MQTT packet type. First packet from server MUST be MQTT_CONNACK, dropping connection...\n");
            vTaskDelete(NULL);;
        }
        if (msg_number > 0 && packet_type == MQTT_CONNACK) {
            ESP_LOGE(MQTT_TAG, "Duplicate MQTT_CONNACK packet detected, dropping connection...\n");
            vTaskDelete(NULL);;
        }

        switch(packet_type) {
            case MQTT_CONNACK: {
                mqtt_connack connack = packet.type.connack;
                if (connack.return_code != 0) {
                    ESP_LOGI(MQTT_TAG, "Connection rejected by the broker, return code = %d\n", connack.return_code);
                    vTaskDelete(NULL);;
                }
                ESP_LOGI(MQTT_TAG, "Received CONNACK correctly, connection with broker validated.\n");
                
                // Pack and send subscribe request
                char *topic_name = "home/chris/smart_led";
                subscribe_tuples sub_properties = {
                    .topic = topic_name,
                    .qos = 1,
                    .topic_len = strlen(topic_name),
                };
                // Store app actions associated with the subscription
                app_subscription_entry sub_entry = {
                    .sub_properties = sub_properties,
                    .commands = {
                        { .command_name = "on", .callback = turn_on_led },
                        { .command_name = "off", .callback = turn_off_led },
                    },
                    .command_count = 2,
                };
                int ret = mqtt_client_subscribe_to_topic(sub_properties, &packet_id, sock);
                if (ret) vTaskDelete(NULL);;
                push(&subscription_list, &sub_entry);
                break;
            }
            case MQTT_PUBLISH: {
                mqtt_publish pub = packet.type.publish;
                int err = mqtt_client_handle_publish(pub, subscription_list, sock);
                if (err) vTaskDelete(NULL);;
                break;
            }
            case MQTT_PUBACK: {
                mqtt_puback puback = packet.type.puback;
                ESP_LOGI(MQTT_TAG, "Puback packet ID: %d", puback.pkt_id);
                break;
            }
            case MQTT_SUBACK: {
                mqtt_suback suback = packet.type.suback;
                for (int i = 0; i < suback.rc_len; ++i) {
                    ESP_LOGI(MQTT_TAG, "Suback%d return code = %02X\n", i, suback.return_codes[i]);
                }
                break;
            }
            case MQTT_PINGRESP: {
                break;
            }
            default:
                ESP_LOGE(MQTT_TAG, "Encountered error while parsing server message!\n");
                break;
        }
        ++msg_number;
        free(original_buffer);
    }
}


int setup_mqtt_connection() {
    int sock = 0;
    struct sockaddr_in serv_addr;

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        ESP_LOGE(TCP_TAG, "Socket creation error");
        return SOCKET_CREATION_FAILED;
    }

    // Set server details
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);

    // Convert IP
    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        ESP_LOGE(TCP_TAG, "Invalid address/Address not supported");
        return INVALID_ADDRESS;
    }

    // Connect to server
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        ESP_LOGE(TCP_TAG, "Connection Failed");
        return SOCKET_CONNECTION_FAILED;
    }
    ESP_LOGI(MQTT_TAG, "Connected to MQTT server.\n");

    mqtt_client_send_connect_packet(sock);
    return sock;
}
