#ifndef __MYSPI_H
#define __MYSPI_H

#include "stm32f10x.h"

/*
 * 软件 SPI，江协风格，Mode 0（CPOL=0, CPHA=0）
 * 引脚（跟旧工程 bootloader_b 一致）：
 *   SS/CS -> PA4
 *   SCK   -> PA5
 *   MISO  -> PA6
 *   MOSI  -> PA7
 * 如果板子接线不同，只改这里 4 个宏 + MySPI.c 里的 GPIO_Init。
 */
#define MYSPI_GPIO_PORT   GPIOA
#define MYSPI_GPIO_CLK    RCC_APB2Periph_GPIOA

#define MYSPI_SS_PIN      GPIO_Pin_4
#define MYSPI_SCK_PIN     GPIO_Pin_5
#define MYSPI_MISO_PIN    GPIO_Pin_6
#define MYSPI_MOSI_PIN    GPIO_Pin_7

void MySPI_Init(void);
void MySPI_Start(void);
void MySPI_Stop(void);
uint8_t MySPI_SwapByte(uint8_t ByteSend);

#endif
