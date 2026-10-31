#include "stm32f10x.h"
#include "my_usart.h"
#include "stdio.h"
#include "struct_init.h"
#include "bootloader.h"
#include "W25Q64.h"
#include "app_update.h"

/*
 * Bootloader boot flow
 *   1. init + banner
 *   2. 3s rescue window, send 'w' to enter menu
 *      (m = make test OTA task, i = info, q = quit, r = reset)
 *   3. OTA task: check len + staging CRC32 -> erase APP -> copy -> CRC -> clear flag
 *   4. APP1 valid -> jump APP1
 *   5. APP1 invalid -> jump FACTORY
 *   6. both invalid -> stay in BOOT
 */

#ifndef BOOT_DEBUG_NO_JUMP
#define BOOT_DEBUG_NO_JUMP 0
#endif

#define BOOT_WINDOW_MS 3000u

int main(void)
{
    all_init();
    W25Q64_Init();
    u1_printf("\r\n===== BOOT START =====\r\n");

    /* ---------- 3s rescue window ---------- */
    if (uasrt_command(BOOT_WINDOW_MS / 1000u))
    {
        boot_menu();
    }

    /* ---------- OTA task ---------- */
    (void)boot_main_process();

    /* ---------- boot partition selection ---------- */
    if (app_update_pending())
    {
        u1_printf("APP update incomplete, skip APP1\r\n");
    }
    else if (app_is_valid(stm32_a_saddr, stm32_a_size))
    {
        u1_printf("APP1 valid, jump\r\n");
#if (BOOT_DEBUG_NO_JUMP == 0)
        jump_to_addr(stm32_a_saddr, stm32_a_size);
#endif
    }
    else
    {
        u1_printf("APP1 invalid\r\n");
    }

    if (app_is_valid(stm32_f_saddr, stm32_f_size))
    {
        u1_printf("FACTORY valid, jump\r\n");
#if (BOOT_DEBUG_NO_JUMP == 0)
        jump_to_addr(stm32_f_saddr, stm32_f_size);
#endif
    }
    else
    {
        u1_printf("FACTORY invalid\r\n");
    }

    u1_printf("Stay in BOOT\r\n");

    while (1)
    {
    }
}
