#ifndef __WIFI_STA_H
#define __WIFI_STA_H

#include "esp_err.h"

/* 连接 WiFi（阻塞直到连上或超时），超时返回 ESP_ERR_TIMEOUT */
esp_err_t wifi_sta_connect(uint32_t timeout_ms);

/* 是否已连接 */
int wifi_sta_is_connected(void);

#endif /* __WIFI_STA_H */
