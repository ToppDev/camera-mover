#ifndef WIFI_H
#define WIFI_H

#include "esp_wifi.h"
#include "esp_log.h"

#define WIFI_SSID "JARVIS"
#define WIFI_PASS "52057992906776358863"

// #define WIFI_SSID "Midgard"
// #define WIFI_PASS "5877945763053495"

// Interval for station to listen to beacon from AP. The unit of listen interval is one beacon interval.
//     For example, if beacon interval is 100 ms and listen interval is 3, the interval for station to listen
//     to beacon is 300 ms.
#define DEFAULT_LISTEN_INTERVAL 5 // [1 - 10]

// Power save mode for the esp32 to use. Modem sleep mode includes minimum and maximum power save modes.
// In minimum power save mode, station wakes up every DTIM to receive beacon. Broadcast data will not be
// lost because it is transmitted after DTIM. However, it can not save much more power if DTIM is short
// for DTIM is determined by AP.
// In maximum power save mode, station wakes up every listen interval to receive beacon. Broadcast data
// may be lost because station may be in sleep state at DTIM time. If listen interval is longer, more power
// is saved but broadcast data is more easy to lose.
// #define DEFAULT_PS_MODE WIFI_PS_MIN_MODEM
#define DEFAULT_PS_MODE WIFI_PS_MAX_MODEM
// #define DEFAULT_PS_MODE WIFI_PS_NONE

static const char *WIFI_TAG = "WiFi";

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(WIFI_TAG, "got ip: %s", ip4addr_ntoa(&event->ip_info.ip));
    }
}

/*init wifi as sta and set power save mode*/
static void wifi_power_save(void)
{
    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .listen_interval = DEFAULT_LISTEN_INTERVAL,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(WIFI_TAG, "esp_wifi_set_ps().");
    esp_wifi_set_ps(DEFAULT_PS_MODE);
}

#endif /* WIFI_H */