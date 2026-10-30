#include "stm32f10x.h"
#include "my_usart.h"
#include "struct_init.h"
#include "stdio.h"
#include "bootloader.h"
#include "stdint.h"

/*
 * 板上 LED 接法：
 *   PC13 为低 -> LED 亮（灌电流接法，江协 / 最小系统板常见）
 *   如果板上是反过来的，把 LED_ON_LEVEL 改成 0x01u
 */
#ifndef LED_ON_LEVEL
#define LED_ON_LEVEL 0x00u
#endif

#define LED_OFF_LEVEL ((LED_ON_LEVEL == 0x00u) ? 0x01u : 0x00u)
#define LED_TOGGLE_INTERVAL_MS 1000U

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
    SystemInit(); /* 初始化系统时钟 */

    /* 中断向量表搬到 APP1 起始地址 0x08005000 */
    SCB->VTOR = stm32_a_saddr;
    __DSB();
    __ISB();

    gpio_initstruct();

    /* 先熄灭：能区分“进了 APP 但没跑起来”和“APP 在跑但 LED 没闪” */
    pc13_write(LED_OFF_LEVEL);

    all_init();

    uint32_t last_toggle_time = get_now();

    u1_printf("APP1 running | VTOR=0x%08lX\r\n", (unsigned long)SCB->VTOR);

    while (1)
    {
        if ((get_now() - last_toggle_time) >= LED_TOGGLE_INTERVAL_MS)
        {
            last_toggle_time = get_now();
            pc13_toggle();
            u1_printf("APP1 tick %lu\r\n", (unsigned long)get_now());
        }
    }
}
