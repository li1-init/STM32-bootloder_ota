#include "stm32f10x.h"
#include "my_usart.h"
#include "struct_init.h"
#include "stdio.h"
#include "string.h"
#include "bootloader.h"
#include "W25Q64.h"
#include "ota_info.h"

/*
 * ===== APP v1.1.0 (SLOT A @0x08005000) =====
 *
 * 这是"升级后的新版本"。和 v1.0.0 的区别：
 *   1. 横幅打印 v1.1.0
 *   2. LED 200ms 快闪（v1.0 是 1000ms）
 *   3. 启动时打印 W25Q64 里的 OTA 信息，看到 version 已经是 v1.1.0
 *   4. 收到 'u' 时报告自己的版本和 OTA 信息
 *
 * 它同时是"救砖后能直接烧进 0x08005000 的完整 APP"。
 */

#define APP_VERSION       OTA_VER(1, 1, 0)
#define APP_VERSION_STR   "v1.1.0"

#ifndef LED_ON_LEVEL
#define LED_ON_LEVEL 0x00u
#endif

#define LED_OFF_LEVEL ((LED_ON_LEVEL == 0x00u) ? 0x01u : 0x00u)
#define LED_FAST_MS 200U

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
    ota_info_t info;
    uint32_t   last_toggle_time;
    uint8_t    c;

    SystemInit();

    SCB->VTOR = stm32_a_saddr;
    __DSB();
    __ISB();

    gpio_initstruct();
    pc13_write(LED_OFF_LEVEL);
    all_init();

    u1_printf("\r\n########## APP " APP_VERSION_STR " (SLOT A) ##########\r\n");
    u1_printf("VTOR = 0x%08lX\r\n", (unsigned long)SCB->VTOR);

    if (ota_info_load(&info) == 0)
    {
        ota_info_dump(&info);
        u1_printf("Version check: %s\r\n",
                  (info.version == APP_VERSION) ? "MATCH v1.1.0, OTA OK"
                                                : "MISMATCH, check OTA info");
    }
    else
    {
        u1_printf("W25Q64: no valid OTA info (W25Q64 not connected?)\r\n");
    }

    last_toggle_time = get_now();
    while (1)
    {
        c = usart_boot_getchar();
        if (c == (uint8_t)'u')
        {
            u1_printf("\r\n--- APP " APP_VERSION_STR " info ---\r\n");
            u1_printf("current version : " APP_VERSION_STR "\r\n");
            if (ota_info_load(&info) == 0)
            {
                ota_info_dump(&info);
            }
            else
            {
                u1_printf("No OTA info\r\n");
            }
            u1_printf("--------------------------------\r\n");
        }

        if ((get_now() - last_toggle_time) >= LED_FAST_MS)
        {
            last_toggle_time = get_now();
            pc13_toggle();
        }
    }
}
