#include "wifi_sta.h"

#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "app_secrets.h"

#define TAG "wifi"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_MAX_RETRY     10

static EventGroupHandle_t s_evt;
static int s_retry;

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry < WIFI_MAX_RETRY)
        {
            s_retry++;
            ESP_LOGW(TAG, "disconnected, retry %d/%d", s_retry, WIFI_MAX_RETRY);
            esp_wifi_connect();
        }
        else
        {
            xEventGroupSetBits(s_evt, WIFI_FAIL_BIT);
        }
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&e->ip_info.ip));
        s_retry = 0;
        xEventGroupSetBits(s_evt, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_sta_connect(uint32_t timeout_ms)
{
    if (!s_evt)
    {
        s_evt = xEventGroupCreate();
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_create_default_wifi_sta();

        wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&ic));

        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

        wifi_config_t wc = { 0 };
        strncpy((char *)wc.sta.ssid, WIFI_SSID, sizeof(wc.sta.ssid) - 1);
        strncpy((char *)wc.sta.password, WIFI_PASSWORD, sizeof(wc.sta.password) - 1);
        wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
        ESP_ERROR_CHECK(esp_wifi_start());

        ESP_LOGI(TAG, "connecting to \"%s\" ...", WIFI_SSID);
    }
    else
    {
        esp_wifi_connect();
    }

    EventBits_t bits = xEventGroupWaitBits(
        s_evt, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE,
        pdMS_TO_TICKS(timeout_ms));

    if (bits & WIFI_CONNECTED_BIT)
    {
        return ESP_OK;
    }
    if (bits & WIFI_FAIL_BIT)
    {
        return ESP_FAIL;
    }
    return ESP_ERR_TIMEOUT;
}

int wifi_sta_is_connected(void)
{
    wifi_ap_record_t ap;
    return (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? 1 : 0;
}
