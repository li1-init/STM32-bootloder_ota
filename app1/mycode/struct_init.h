#ifndef __STRUCT_INIT_H
#define __STRUCT_INIT_H

#include "stm32f10x.h"
#define U0_RX_SIZE 2048

extern volatile uint32_t now ;
extern volatile uint8_t  key_w_pressed;

void gpio_initstruct(void);
void my_usart1_init(void);
void tim3_init(void);
void TIM3_IRQHandler(void);
uint32_t get_now(void);

/* 毫秒延时（基于 TIM3 的 now） */
void delay_ms(uint32_t ms);

#endif
