#include "stm32f10x.h"
#include "my_usart.h"
#include "struct_init.h"
#include "stdio.h"
#include "bootloader.h"

/*
 * Factory 救砖程序：
 *   1. 独立分区 0x08003000，独立中断向量表
 *   2. 不依赖 W25Q64，只依赖 MCU + 串口 + 片内 Flash
 *   3. 现在先做 LED 心跳 + 串口打印；后续加串口收 BIN 直接写 APP
 */
#ifndef LED_ON_LEVEL
#define LED_ON_LEVEL 0x00u /* PC13 为低时点亮；接法相反改成 0x01u */
#endif

#define LED_OFF_LEVEL ((LED_ON_LEVEL == 0x00u) ? 0x01u : 0x00u)
#define LED_TOGGLE_INTERVAL_MS 500U

static void pc13_write(uint8_t level)
{
    GPIO_WriteBit(GPIOC, GPIO_Pin_13, (BitAction)level);
}

static void pc13_toggle(void)
{
    if (GPIO_ReadOutputDataBit(GPIOC, GPIO_Pin_13) == (BitAction)LED_ON_LEVEL)
    {
        pc13_write(LED_OFF_LEVEL);
    }
    else
    {
        pc13_write(LED_ON_LEVEL);
    }
}

int main(void)
{
    SystemInit();

    SCB->VTOR = stm32_f_saddr;
    __DSB();
    __ISB();

    gpio_initstruct();
    pc13_write(LED_OFF_LEVEL);

    all_init();

    uint32_t last_toggle_time = get_now();

    u1_printf("FACTORY running | VTOR=0x%08lX\r\n", (unsigned long)SCB->VTOR);

    while (1)
    {
        if ((get_now() - last_toggle_time) >= LED_TOGGLE_INTERVAL_MS)
        {
            last_toggle_time = get_now();
            pc13_toggle();
        }
    }
}
