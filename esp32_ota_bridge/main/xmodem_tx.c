#include "xmodem_tx.h"

#include <string.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "xmodem_tx"

/* ============================ 引脚配置 ============================
 * ESP32-S3                    STM32F103C8T6
 *   GPIO17 (TX)  ───────────>  PA3  (USART2_RX)
 *   GPIO18 (RX)  <───────────  PA2  (USART2_TX)
 *   GND          ─────────────  GND
 * 如果接线不同，只改这两个宏
 */
#define XM_UART_NUM      UART_NUM_2
#define XM_UART_TX_PIN   17
#define XM_UART_RX_PIN   18
#define XM_UART_BAUD     115200

/* ============================ 协议常量 ============================ */
#define X_SOH     0x01
#define X_EOT     0x04
#define X_ACK     0x06
#define X_NAK     0x15
#define X_CAN     0x18
#define X_CRC_C   'C'
#define X_PKT     128

#define X_ACK_TIMEOUT_MS   1000    /* 等 ACK 的超时 */
#define X_MAX_RETRY        5       /* 单包最大重试 */
#define X_BEGIN_TIMEOUT_MS 30000   /* 等 'C' 的超时 */

static uint8_t  s_block = 1;       /* 包序号，1 开始 */
static uint8_t  s_pkt[X_PKT];

/* CRC16-CCITT, poly 0x1021, init 0x0000（XMODEM 用的是这个） */
static uint16_t xmodem_crc16(const uint8_t *p, size_t n)
{
    uint16_t crc = 0x0000;
    size_t i;
    int b;

    for (i = 0; i < n; i++)
    {
        crc ^= (uint16_t)p[i] << 8;
        for (b = 0; b < 8; b++)
        {
            if (crc & 0x8000)
            {
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            }
            else
            {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

void xmodem_tx_init(void)
{
    const uart_config_t cfg = {
        .baud_rate  = XM_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    /* 装驱动：接收缓冲 1024，发送缓冲 1024 */
    ESP_ERROR_CHECK(uart_driver_install(XM_UART_NUM, 1024, 1024, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(XM_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(XM_UART_NUM,
                                 XM_UART_TX_PIN,
                                 XM_UART_RX_PIN,
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));
    ESP_LOGI(TAG, "uart%d ready, tx=%d rx=%d baud=%d",
             XM_UART_NUM, XM_UART_TX_PIN, XM_UART_RX_PIN, XM_UART_BAUD);
}

/* 从 STM32 读一个字节，超时返回 -1 */
static int xm_getc(uint32_t timeout_ms)
{
    uint8_t c;
    int n = uart_read_bytes(XM_UART_NUM, &c, 1, pdMS_TO_TICKS(timeout_ms));
    return (n == 1) ? (int)c : -1;
}

/* 等 STM32 发来的 'C' */
esp_err_t xmodem_tx_begin(uint32_t timeout_ms)
{
    if (timeout_ms == 0)
    {
        timeout_ms = X_BEGIN_TIMEOUT_MS;
    }

    uart_flush_input(XM_UART_NUM);
    s_block = 1;

    ESP_LOGI(TAG, "waiting for 'C' from STM32 ...");
    TickType_t t0 = xTaskGetTickCount();
    while ((xTaskGetTickCount() - t0) < pdMS_TO_TICKS(timeout_ms))
    {
        int c = xm_getc(200);
        if (c == X_CRC_C)
        {
            ESP_LOGI(TAG, "got 'C', start sending");
            return ESP_OK;
        }
        if (c >= 0)
        {
            ESP_LOGD(TAG, "ignore 0x%02X", c);
        }
    }
    ESP_LOGE(TAG, "timeout waiting 'C'");
    return ESP_ERR_TIMEOUT;
}

/* 发一包，等 ACK，失败重试 */
static esp_err_t xm_send_packet(void)
{
    int retry;
    uint16_t crc = xmodem_crc16(s_pkt, X_PKT);

    for (retry = 0; retry < X_MAX_RETRY; retry++)
    {
        uint8_t hdr[3] = { X_SOH, s_block, (uint8_t)(~s_block) };
        uint8_t tail[2] = { (uint8_t)(crc >> 8), (uint8_t)(crc & 0xFF) };

        uart_write_bytes(XM_UART_NUM, hdr, sizeof(hdr));
        uart_write_bytes(XM_UART_NUM, s_pkt, X_PKT);
        uart_write_bytes(XM_UART_NUM, tail, sizeof(tail));
        uart_wait_tx_done(XM_UART_NUM, pdMS_TO_TICKS(500));

        int c = xm_getc(X_ACK_TIMEOUT_MS);
        if (c == X_ACK)
        {
            return ESP_OK;
        }
        if (c == X_CAN)
        {
            ESP_LOGE(TAG, "receiver cancelled");
            return ESP_FAIL;
        }
        /* NAK 或超时：重发这一包 */
        ESP_LOGW(TAG, "blk %u no ACK (got 0x%02X), retry %d", s_block, c, retry + 1);
    }
    return ESP_FAIL;
}

/* 发送一段数据：内部按 128B 切包 */
int xmodem_tx_data(const uint8_t *data, size_t len)
{
    size_t sent = 0;

    while (sent < len)
    {
        size_t n = len - sent;
        if (n > X_PKT)
        {
            n = X_PKT;
        }

        memcpy(s_pkt, data + sent, n);
        if (n < X_PKT)
        {
            memset(s_pkt + n, 0x1A, X_PKT - n);   /* XMODEM 填充 */
        }

        if (xm_send_packet() != ESP_OK)
        {
            ESP_LOGE(TAG, "send blk %u failed", s_block);
            return (int)sent;
        }

        s_block++;
        sent += n;
    }
    return (int)sent;
}

esp_err_t xmodem_tx_end(void)
{
    int retry;

    for (retry = 0; retry < X_MAX_RETRY; retry++)
    {
        uint8_t eot = X_EOT;
        uart_write_bytes(XM_UART_NUM, &eot, 1);
        uart_wait_tx_done(XM_UART_NUM, pdMS_TO_TICKS(500));

        int c = xm_getc(2000);
        if (c == X_ACK)
        {
            ESP_LOGI(TAG, "EOT acknowledged, transfer done (%u blocks)", s_block - 1);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "EOT no ACK (0x%02X), retry %d", c, retry + 1);
    }
    return ESP_FAIL;
}

void xmodem_tx_cancel(void)
{
    uint8_t can[2] = { X_CAN, X_CAN };
    uart_write_bytes(XM_UART_NUM, can, sizeof(can));
    uart_wait_tx_done(XM_UART_NUM, pdMS_TO_TICKS(500));
    ESP_LOGW(TAG, "cancelled");
}
