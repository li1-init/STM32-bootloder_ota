#ifndef __MY_USART2_H
#define __MY_USART2_H

#include "stdint.h"

/*
 * USART2: ESP32-S3 无线 OTA 通道
 *   PA2 = USART2_TX
 *   PA3 = USART2_RX
 *   DMA1 Channel6 + IDLE 中断 + 环形缓冲
 */

#define USART2_RX_SIZE 2048u

void    usart2_init(void);

/* 从 USART2 环形缓冲取一个字节，没有返回 -1 */
int     usart2_getchar(void);

/* 向 USART2 发一个字节 */
void    usart2_putc(uint8_t c);

/* 清空接收缓冲 */
void    usart2_flush(void);

#endif
