#ifndef __ONENET_OTA_H
#define __ONENET_OTA_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/*
 * OneNET OTA 客户端（HTTP 接口）
 *   - 上报 STM32 当前版本
 *   - 查询升级任务（拿 tid / 目标版本）
 *   - 流式下载固件（不落 ESP32 Flash，直接回调给调用者）
 */

#define ONENET_OTA_URL "https://iot-api.heclouds.com/fuse-ota"

/* 升级任务信息 */
typedef struct
{
    int      tid;               /* 任务 id */
    int      status;            /* OneNET 任务状态；3 表示已完成 */
    char     target_version[32];/* 目标版本，如 "2.0.0" */
    uint32_t fw_size;           /* 固件大小，0 表示未知 */
} onenet_ota_task_t;

/* 流式下载回调：每收到一段数据调用一次，返回 0 表示继续，非 0 表示中止 */
typedef int (*onenet_dl_cb_t)(const uint8_t *data, size_t len, void *ctx);

esp_err_t onenet_ota_init(void);

/* 上报当前 STM32 版本 */
esp_err_t onenet_ota_report_version(const char *version);

/* 查询升级任务
 *   ESP_OK          = 有新任务，task 已填充
 *   ESP_ERR_NOT_FOUND = 已是最新
 *   其它            = 网络/鉴权错误
 */
esp_err_t onenet_ota_check_task(const char *current_version, onenet_ota_task_t *task);

/* 流式下载固件，边下边回调 */
esp_err_t onenet_ota_download(int tid, onenet_dl_cb_t cb, void *ctx, uint32_t *out_size);

/* 上报升级状态：step 10=下载中 100=完成 200=失败 */
esp_err_t onenet_ota_report_status(int tid, int step);

#endif /* __ONENET_OTA_H */
