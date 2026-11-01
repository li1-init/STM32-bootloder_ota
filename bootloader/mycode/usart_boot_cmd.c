#include "stm32f10x.h"
#include "string.h"
#include "stdio.h"
#include "my_usart.h"
#include "struct_init.h"
#include "bootloader.h"
#include "ota_info.h"
#include "W25Q64.h"
#include "xmodem.h"
#include "my_usart2.h"
#include "stm32f10x_flash.h"

/*
 * Bootloader serial menu
 *   [1] XMODEM download firmware -> W25Q64 staging -> flag -> reset
 *   [2] show OTA info
 *   [3] copy staging to APP now (manual)
 *   [q] quit menu, continue boot
 *   [r] reset
 */

static uint8_t rx_get(void)
{
    return usart_boot_getchar();
}

static uint8_t rx_get_timeout(uint32_t ms)
{
    uint32_t t0 = get_now();

    while ((get_now() - t0) < ms)
    {
        uint8_t c = rx_get();
        if (c != 0u)
        {
            return c;
        }
    }
    return 0u;
}

void uasrt_command_suf(void)
{
    u1_printf("===== Bootloader menu =====\r\n");
    u1_printf("[1] XMODEM download firmware\r\n");
    u1_printf("[2] show OTA info\r\n");
    u1_printf("[3] copy staging to APP\r\n");
    u1_printf("[4] erase APP page (test Factory)\r\n");
    u1_printf("[7] wireless OTA (USART2 / ESP32)\r\n");
    u1_printf("[q] quit menu, continue boot\r\n");
    u1_printf("[r] reset\r\n");
}

uint8_t uasrt_command(uint32_t time)
{
    uint32_t t0 = get_now();

    u1_printf("Wait %lu s, send 'w' to enter menu\r\n", (unsigned long)time);

    while ((get_now() - t0) < time * 1000u)
    {
        if (key_w_pressed || rx_get() == 'w')
        {
            key_w_pressed = 0u;
            return 1u;
        }
    }
    return 0u;
}

void boot_menu(void)
{
    uint8_t c;

    key_w_pressed = 0u;
    uasrt_command_suf();

    for (;;)
    {
        u1_printf("> ");
        c = rx_get_timeout(60000u);
        if (c == 0u)
        {
            continue;
        }
        u1_printf("%c\r\n", c);

        if (c == 'q')
        {
            u1_printf("Quit menu, continue boot\r\n");
            return;
        }
        if (c == 'r')
        {
            NVIC_SystemReset();
        }
        if (c == '2')
        {
            ota_info_t info;
            if (ota_info_load(&info) == 0)
            {
                ota_info_dump(&info);
            }
            else
            {
                u1_printf("OTA info invalid\r\n");
            }
        }
        if (c == '4')
        {
            /* Erase first page of APP: Boot will see APP invalid and jump to Factory */
            FLASH_Unlock();
            FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
            if (FLASH_ErasePage(stm32_a_saddr) == FLASH_COMPLETE)
            {
                u1_printf("APP first page erased, reset in 1s -> Factory\r\n");
            }
            else
            {
                u1_printf("erase failed\r\n");
            }
            FLASH_Lock();
            delay_ms(1000u);
            NVIC_SystemReset();
        }
        if (c == '3')
        {
            ota_info_t info;
            if (ota_info_load(&info) == 0 && info.flag == ota_flag_set)
            {
                info.flag = ota_flag_set;
                (void)ota_info_save(&info);
                u1_printf("Manual upgrade: len=%lu\r\n", (unsigned long)info.fw_len);
                NVIC_SystemReset();
            }
            else
            {
                u1_printf("no pending firmware\r\n");
            }
        }
        if (c == '7')
        {
            uint32_t len = 0;
            uint32_t crc = 0;

            usart2_init();
            u1_printf("Wireless OTA: USART2 ready, waiting ESP32\r\n");
            xmodem_set_source(1u);
            if (xmodem_receive(&len, &crc) == 0)
            {
                u1_printf("Reset in 1s, Bootloader will copy staging to APP\r\n");
                delay_ms(1000u);
                NVIC_SystemReset();
            }
            else
            {
                u1_printf("Wireless OTA failed\r\n");
            }
            xmodem_set_source(0u);
        }
        if (c == '1')
        {
            uint32_t len = 0;
            uint32_t crc = 0;

            if (xmodem_receive(&len, &crc) == 0)
            {
                u1_printf("Reset in 1s, Bootloader will copy staging to APP\r\n");
                delay_ms(1000u);
                NVIC_SystemReset();
            }
            else
            {
                u1_printf("XMODEM failed\r\n");
            }
        }
    }
}
