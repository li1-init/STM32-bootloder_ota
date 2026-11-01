/* ==========================================================================
 *  ESP32-S3 OneNET OTA Bridge for STM32
 *
 *  功能：
 *    1. 连 WiFi
 *    2. 向 OneNET 上报 STM32 当前版本（本机宏 STM32_CURRENT_VERSION）
 *    3. 查询是否有升级任务
 *    4. 有任务：流式下载固件，边下边用 XMODEM 发给 STM32（UART2）
 *    5. STM32 收完置 flag -> 复位 -> Boot 搬运 -> 新 APP
 *
 *  硬件接线：
 *    ESP32-S3 GPIO17 (TX) -> STM32 PA3 (USART2_RX)
 *    ESP32-S3 GPIO18 (RX) <- STM32 PA2 (USART2_TX)
 *    GND 共地
 *
 *  使用前提：STM32 Bootloader 处于菜单 [7] USART2 无线 OTA 等待状态
 * ========================================================================== */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "app_secrets.h"
#include "wifi_sta.h"
#include "onenet_ota.h"
#include "xmodem_tx.h"

#define TAG "ota_bridge"

/* STM32 当前运行的固件版本（升级后要手动改成新版本） */
#ifndef STM32_CURRENT_VERSION
#define STM32_CURRENT_VERSION "1.0.0"
#endif

/* ============================ 下载回调 ============================ */
/* OneNET 下载回调：每收到一块数据就通过 XMODEM 发给 STM32 */
static int on_data_from_cloud(const uint8_t *data, size_t len, void *ctx)
{
    uint32_t *sent = (uint32_t *)ctx;

    int n = xmodem_tx_data(data, len);
    *sent += n;

    if ((size_t)n != len)
    {
        ESP_LOGE(TAG, "xmodem send short: %d/%u", n, (unsigned)len);
        return -1;
    }
    return 0;
}

/* ============================ 主流程 ============================ */
static void ota_task(void *arg)
{
    onenet_ota_task_t task;
    uint32_t sent = 0;

    ESP_LOGI(TAG, "===== OneNET OTA bridge start =====");
    ESP_LOGI(TAG, "STM32 current version: %s", STM32_CURRENT_VERSION);

    /* 1. WiFi */
    if (wifi_sta_connect(20000) != ESP_OK)
    {
        ESP_LOGE(TAG, "wifi connect failed");
        vTaskDelete(NULL);
        return;
    }

    /* 2. 上报版本 */
    if (onenet_ota_report_version(STM32_CURRENT_VERSION) != ESP_OK)
    {
        ESP_LOGE(TAG, "report version failed");
        vTaskDelete(NULL);
        return;
    }

    /* 3. 查任务 */
    esp_err_t err = onenet_ota_check_task(STM32_CURRENT_VERSION, &task);
    if (err == ESP_ERR_NOT_FOUND)
    {
        ESP_LOGI(TAG, "already latest, nothing to do");
        vTaskDelete(NULL);
        return;
    }
    if (err == ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "cloud OTA task is already finished; create a new OTA task");
        vTaskDelete(NULL);
        return;
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "check task failed");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "new version available: %s (tid=%d, size=%lu)",
             task.target_version, task.tid, (unsigned long)task.fw_size);

    /* 4. 上报"下载中" */
    onenet_ota_report_status(task.tid, 10);

    /* 5. 等 STM32 进入 XMODEM 等待（Boot 菜单按 1） */
    ESP_LOGI(TAG, ">>> please put STM32 Bootloader into menu [7] wireless OTA <<<");
    if (xmodem_tx_begin(60000) != ESP_OK)
    {
        ESP_LOGE(TAG, "STM32 not ready (no 'C')");
        onenet_ota_report_status(task.tid, 200);
        vTaskDelete(NULL);
        return;
    }

    /* 6. 流式下载 + 边发 STM32 */
    err = onenet_ota_download(task.tid, on_data_from_cloud, &sent, NULL);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "download failed, %lu bytes sent", (unsigned long)sent);
        xmodem_tx_cancel();
        onenet_ota_report_status(task.tid, 200);
        vTaskDelete(NULL);
        return;
    }

    /* 防止 HTTP/回调重复发送：实际发送量必须等于云端声明大小 */
    if (task.fw_size != 0u && sent != task.fw_size)
    {
        ESP_LOGE(TAG, "xmodem size mismatch: sent=%lu expect=%lu",
                 (unsigned long)sent, (unsigned long)task.fw_size);
        xmodem_tx_cancel();
        onenet_ota_report_status(task.tid, 200);
        vTaskDelete(NULL);
        return;
    }

    /* 7. 发 EOT 结束传输 */
    if (xmodem_tx_end() != ESP_OK)
    {
        ESP_LOGE(TAG, "EOT failed");
        onenet_ota_report_status(task.tid, 200);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "===== transfer done, %lu bytes =====", (unsigned long)sent);
    ESP_LOGI(TAG, "STM32 will reboot and Bootloader applies the update");

    /* 8. 上报完成（STM32 此时在搬运，ESP32 不需要等） */
    onenet_ota_report_status(task.tid, 100);

    vTaskDelete(NULL);
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    xmodem_tx_init();
    onenet_ota_init();

    xTaskCreate(ota_task, "ota_task", 8192, NULL, 5, NULL);
}
