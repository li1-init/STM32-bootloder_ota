#include "stm32f10x.h"
#include "my_usart.h"
#include "struct_init.h"
#include "stdio.h"
#include "string.h"
#include "bootloader.h"
#include "factory_xmodem.h"

/*
 * Factory (brick recovery) image @ 0x08003000
 *
 *  - Boot 发现 APP1 无效 / 搬运失败时会跳到这里
 *  - 通过串口 XMODEM 接收 APP bin，直接写片内 Flash
 *  - 不依赖 W25Q64，外部 Flash 坏了也能救
 *  - 收到 'r' 复位回 Boot；收到 'x' 进入 XMODEM 接收
 *
 * 独立中断向量表：SCB->VTOR = 0x08003000
 */

#ifndef LED_ON_LEVEL
#define LED_ON_LEVEL 0x00u
#endif

#define LED_OFF_LEVEL ((LED_ON_LEVEL == 0x00u) ? 0x01u : 0x00u)
#define LED_FAST_MS 200U   /* Factory 里 LED 快闪，一眼区分 */

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

static uint8_t rc_get(void)
{
    return usart_boot_getchar();
}

int main(void)
{
    uint32_t last_toggle_time;
    uint32_t last_hint = 0xFFFFFFFFu;
    uint8_t  c;

    SystemInit();

    SCB->VTOR = stm32_f_saddr;
    __DSB();
    __ISB();

    gpio_initstruct();
    pc13_write(LED_OFF_LEVEL);
    all_init();

    u1_printf("\r\n============================================\r\n");
    u1_printf("  FACTORY RECOVERY @0x%08lX\r\n", (unsigned long)stm32_f_saddr);
    u1_printf("  APP partition will be written at 0x%08lX\r\n", (unsigned long)stm32_a_saddr);
    u1_printf("  [x] XMODEM download APP bin\r\n");
    u1_printf("  [r] reset to Bootloader\r\n");
    u1_printf("============================================\r\n");
    u1_printf("Press 'x' to start recovery...\r\n");

    last_toggle_time = get_now();

    while (1)
    {
        c = rc_get();
        if (c == (uint8_t)'r')
        {
            u1_printf("Reset to Bootloader\r\n");
            delay_ms(100u);
            NVIC_SystemReset();
        }
        if (c == (uint8_t)'x')
        {
            uint32_t len = 0;
            uint32_t crc = 0;

            if (factory_xmodem_receive(&len, &crc) == 0)
            {
                u1_printf("FACTORY: recovery OK, len=%lu crc32=0x%08lX\r\n",
                          (unsigned long)len, (unsigned long)crc);
                u1_printf("FACTORY: resetting to Boot, will jump to new APP\r\n");
                delay_ms(500u);
                NVIC_SystemReset();
            }
            else
            {
                u1_printf("FACTORY: recovery failed, press 'x' to retry\r\n");
            }
            last_hint = get_now();
        }

        if ((get_now() - last_toggle_time) >= LED_FAST_MS)
        {
            last_toggle_time = get_now();
            pc13_toggle();
        }

        /* 每 5 秒提示一次 */
        if ((get_now() - last_hint) >= 5000u)
        {
            last_hint = get_now();
        }
    }
}
