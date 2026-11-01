#include "onenet_ota.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "onenet_token.h"
#include "app_secrets.h"

#define TAG "onenet_ota"

/* 查询/上报响应用的小缓冲（固件下载走流式，不用这个） */
#define RESP_BUF_SIZE 2048
static char s_resp[RESP_BUF_SIZE];
static size_t s_resp_len;

/* 通用 HTTP 事件：把响应体收集到 s_resp */
static esp_err_t resp_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
        if (s_resp_len < RESP_BUF_SIZE - 1)
        {
            size_t copy = evt->data_len;
            if (copy > RESP_BUF_SIZE - 1 - s_resp_len)
            {
                copy = RESP_BUF_SIZE - 1 - s_resp_len;
            }
            memcpy(s_resp + s_resp_len, evt->data, copy);
            s_resp_len += copy;
            s_resp[s_resp_len] = '\0';
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

/* 生成设备级 token 并设置鉴权头 */
static void set_auth_headers(esp_http_client_handle_t client)
{
    static char token[256];
    memset(token, 0, sizeof(token));
    dev_token_generate(token, SIG_METHOD_SHA256, ONENET_TOKEN_TIMESTAMP,
                       ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, ONENET_ACCESS_KEY);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Authorization", token);
    esp_http_client_set_header(client, "host", "iot-api.heclouds.com");
}

/* ---------------- 上报版本 ---------------- */
esp_err_t onenet_ota_report_version(const char *version)
{
    char url[256];
    char body[160];

    snprintf(url, sizeof(url), ONENET_OTA_URL "/%s/%s/version",
             ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
    /* OneNET 要求 s_version 为软件版本，f_version 为固件版本 */
    snprintf(body, sizeof(body), "{\"s_version\":\"%s\",\"f_version\":\"%s\"}",
             version, version);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = resp_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client)
    {
        return ESP_FAIL;
    }
    set_auth_headers(client);
    esp_http_client_set_post_field(client, body, strlen(body));

    s_resp_len = 0;
    s_resp[0] = '\0';
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "report version: http err 0x%x", err);
        return err;
    }
    ESP_LOGI(TAG, "report version %s -> HTTP %d, body=%s", version, status, s_resp);

    cJSON *root = cJSON_Parse(s_resp);
    if (!root)
    {
        return ESP_FAIL;
    }
    cJSON *code = cJSON_GetObjectItem(root, "code");
    esp_err_t ret = (code && cJSON_GetNumberValue(code) == 0) ? ESP_OK : ESP_FAIL;
    cJSON_Delete(root);
    return ret;
}

/* ---------------- 查询任务 ---------------- */
esp_err_t onenet_ota_check_task(const char *current_version, onenet_ota_task_t *task)
{
    char url[320];

    memset(task, 0, sizeof(*task));
    snprintf(url, sizeof(url), ONENET_OTA_URL "/%s/%s/check?type=1&version=%s",
             ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, current_version);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = resp_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client)
    {
        return ESP_FAIL;
    }
    set_auth_headers(client);

    s_resp_len = 0;
    s_resp[0] = '\0';
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "check task: http err 0x%x", err);
        return err;
    }
    ESP_LOGI(TAG, "check task -> HTTP %d, body=%s", status, s_resp);

    cJSON *root = cJSON_Parse(s_resp);
    if (!root)
    {
        return ESP_FAIL;
    }

    cJSON *code = cJSON_GetObjectItem(root, "code");
    double code_v = code ? cJSON_GetNumberValue(code) : -1;
    cJSON *data = cJSON_GetObjectItem(root, "data");

    esp_err_t ret = ESP_FAIL;
    if (code_v == 0 && data)
    {
        cJSON *tid = cJSON_GetObjectItem(data, "tid");
        cJSON *target = cJSON_GetObjectItem(data, "target");
        cJSON *size = cJSON_GetObjectItem(data, "size");
        cJSON *task_status = cJSON_GetObjectItem(data, "status");
        if (tid && target)
        {
            task->tid = (int)cJSON_GetNumberValue(tid);
            task->status = task_status ? (int)cJSON_GetNumberValue(task_status) : 0;
            snprintf(task->target_version, sizeof(task->target_version), "%s",
                     cJSON_GetStringValue(target));
            task->fw_size = size ? (uint32_t)cJSON_GetNumberValue(size) : 0;

            if (task->status >= 3)
            {
                ESP_LOGW(TAG, "OTA task %d status=%d is finished; create a new task",
                         task->tid, task->status);
                ret = ESP_ERR_INVALID_STATE;
            }
            else
            {
                ret = ESP_OK;
            }
        }
    }
    else if (code_v == 12012)
    {
        ret = ESP_ERR_NOT_FOUND;   /* 云端没有升级包 */
    }
    cJSON_Delete(root);
    return ret;
}

/* ---------------- 流式下载 ---------------- */
esp_err_t onenet_ota_download(int tid, onenet_dl_cb_t cb, void *ctx, uint32_t *out_size)
{
    char url[256];

    snprintf(url, sizeof(url), ONENET_OTA_URL "/%s/%s/%d/download",
             ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, tid);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 30000,
        .buffer_size = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client)
    {
        return ESP_FAIL;
    }
    set_auth_headers(client);

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "open url fail 0x%x", err);
        esp_http_client_cleanup(client);
        return err;
    }

    int content_len = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "download: HTTP %d, content-length=%d", status, content_len);

    if (status != 200)
    {
        /* 非 200 时把错误体读出来打一行 */
        char errbuf[256];
        int n = esp_http_client_read(client, errbuf, sizeof(errbuf) - 1);
        if (n > 0)
        {
            errbuf[n] = '\0';
            ESP_LOGE(TAG, "server refused: %s", errbuf);
        }
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    uint8_t *buf = malloc(1024);
    if (!buf)
    {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    uint32_t total = 0;
    while (1)
    {
        int n = esp_http_client_read(client, (char *)buf, 1024);
        if (n < 0)
        {
            ESP_LOGE(TAG, "read fail %d", n);
            err = ESP_FAIL;
            break;
        }
        if (n == 0)
        {
            break;   /* 读完了 */
        }
        if (cb(buf, (size_t)n, ctx) != 0)
        {
            ESP_LOGE(TAG, "callback aborted at %lu", (unsigned long)total);
            err = ESP_FAIL;
            break;
        }
        total += n;
        if ((total % 8192) < 1024)
        {
            ESP_LOGI(TAG, "downloaded %lu bytes", (unsigned long)total);
        }
    }

    free(buf);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK && content_len > 0 && total != (uint32_t)content_len)
    {
        ESP_LOGE(TAG, "download size mismatch: got %lu expect %d",
                 (unsigned long)total, content_len);
        err = ESP_FAIL;
    }

    if (out_size)
    {
        *out_size = total;
    }
    ESP_LOGI(TAG, "download done, %lu bytes", (unsigned long)total);
    return err;
}

esp_err_t onenet_ota_report_status(int tid, int step)
{
    char url[256];
    char body[32];

    snprintf(url, sizeof(url), ONENET_OTA_URL "/%s/%s/%d/status",
             ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, tid);
    snprintf(body, sizeof(body), "{\"step\":%d}", step);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = resp_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client)
    {
        return ESP_FAIL;
    }
    set_auth_headers(client);
    esp_http_client_set_post_field(client, body, strlen(body));

    s_resp_len = 0;
    s_resp[0] = '\0';
    esp_err_t err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "report status %d -> %s", step, (err == ESP_OK) ? "ok" : "fail");
    return err;
}

esp_err_t onenet_ota_init(void)
{
    return ESP_OK;
}
