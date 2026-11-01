#include "stm32f10x.h"
#include "my_usart.h"
#include "struct_init.h"
#include "stdio.h"
#include "string.h"
#include "bootloader.h"
#include "W25Q64.h"
#include "ota_info.h"

/*
 * ===== APP v1.0.0 (SLOT A @0x08006000) =====
 *
 * 正常业务程序（不含任何下载协议）。
 *
 * 升级方式：Bootloader 菜单 -> 串口 XMODEM -> W25Q64 暂存区 -> 置 flag -> 复位
 *            -> Boot 校验 CRC32 -> 擦 APP -> 搬运 -> 跳新 APP
 *
 *   - LED 1000ms 心跳
 *   - 'u' 打印当前版本和 W25Q64 里的 OTA 信息（只读）
 */

#define APP_VERSION       OTA_VER(1, 0, 0)
#define APP_VERSION_STR   "v1.0.0"

#ifndef LED_ON_LEVEL
#define LED_ON_LEVEL 0x00u
#endif

#define LED_OFF_LEVEL ((LED_ON_LEVEL == 0x00u) ? 0x01u : 0x00u)
#define LED_SLOW_MS 1000U

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
    u1_printf("upgrade: reset -> 'w' within 3s -> menu [2] XMODEM\r\n");

    if (ota_info_load(&info) == 0)
    {
        ota_info_dump(&info);
    }

    last_toggle_time = get_now();
    while (1)
    {
        c = usart_boot_getchar();
        if (c == (uint8_t)'u')
        {
            u1_printf("\r\n--- APP " APP_VERSION_STR " ---\r\n");
            if (ota_info_load(&info) == 0)
            {
                ota_info_dump(&info);
            }
            else
            {
                u1_printf("no valid OTA info\r\n");
            }
            u1_printf("----------------------\r\n");
        }

        if ((get_now() - last_toggle_time) >= LED_SLOW_MS)
        {
            last_toggle_time = get_now();
            pc13_toggle();
        }
    }
}
