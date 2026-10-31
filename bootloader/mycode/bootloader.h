#ifndef __BOOTLOADER_H
#define __BOOTLOADER_H

#include "stm32f10x.h"
#include "string.h"
#include "stdio.h"
#include "ota_info.h"

/* 64KB Flash，三分区（STM32F103C8T6，真实擦除页 1KB）
 *   Boot    0x08000000 ~ 0x080037FF  14KB
 *   Factory 0x08003800 ~ 0x08005FFF  10KB
 *   APP1    0x08006000 ~ 0x0800A7FF  18KB
 *   APP2    0x0800A800 ~ 0x0800ABFF   1KB（更新状态标志）
 *           0x0800AC00 ~ 0x0800FFFF  21KB（预留）
 */
#define stm32_flash_saddr 0x08000000u
#define stm32_page_size   1024u
#define stm32_page_num    64u

#define stm32_b_saddr     0x08000000u
#define stm32_b_size      0x3800u
#define stm32_b_eaddr     (stm32_b_saddr + stm32_b_size - 1u)

#define stm32_f_saddr     0x08003800u
#define stm32_f_size      0x2800u
#define stm32_f_eaddr     (stm32_f_saddr + stm32_f_size - 1u)

#define stm32_a_saddr     0x08006000u
#define stm32_a_size      0x4800u
#define stm32_a_eaddr     (stm32_a_saddr + stm32_a_size - 1u)

/* SRAM 20KB */
#define sram_begin 0x20000000u
#define sram_end   0x20005000u

#define ota_flag_set 0xAABB1122u
#define ota_magic    0x55AA5A5Au

uint8_t app_is_valid(uint32_t base, uint32_t size);
void    jump_to_addr(uint32_t base, uint32_t size);
void    jump_to_app(void);
void    jump_to_factory(void);

int      boot_copy_staging_to_app(uint32_t fw_len, uint32_t fw_crc32);
uint32_t boot_staging_crc32(uint32_t len);
void     boot_set_powerloss_test(uint32_t mode, uint32_t page);
uint8_t  boot_main_process(void);

uint8_t uasrt_command(uint32_t time);
void    uasrt_command_suf(void);
void    boot_menu(void);

#endif
