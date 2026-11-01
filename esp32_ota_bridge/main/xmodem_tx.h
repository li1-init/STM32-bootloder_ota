#ifndef __XMODEM_TX_H
#define __XMODEM_TX_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/*
 * XMODEM 发送端（128 字节 CRC 版），通过 UART2 发给 STM32 Bootloader
 *
 * 流程：
 *   1. xmodem_tx_begin()   等待 STM32 发 'C'
 *   2. xmodem_tx_data()    逐块发送，内部会切成 128B 包并等 ACK
 *   3. xmodem_tx_end()     发 EOT，等 ACK
 */

/* 配置 UART（引脚、波特率） */
void xmodem_tx_init(void);

/* 等待接收方发 'C'，超时返回错误 */
esp_err_t xmodem_tx_begin(uint32_t timeout_ms);

/* 发送一段数据（内部按 128B 切包），返回发送成功的字节数 */
int xmodem_tx_data(const uint8_t *data, size_t len);

/* 发送 EOT 结束 */
esp_err_t xmodem_tx_end(void);

/* 取消（发 CAN） */
void xmodem_tx_cancel(void);

#endif /* __XMODEM_TX_H */
