#ifndef __BOOTLOADER_H
#define __BOOTLOADER_H

#include "stm32f10x.h"
#include "string.h"
#include "stdio.h"

/* 64KB Flash，三分区
 *   Boot    0x08000000 ~ 0x080037FF  14KB
 *   Factory 0x08003800 ~ 0x08005FFF  10KB
 *   APP1    0x08006000 ~ 0x0800A7FF  18KB
 *   APP2    0x0800A800 ~ 0x0800ABFF   1KB（更新状态标志）
 *           0x0800AC00 ~ 0x0800FFFF  21KB（预留）
 */
#define stm32_flash_saddr 0x08000000u
#define stm32_page_size   1024u

#define stm32_b_saddr     0x08000000u
#define stm32_b_size      0x3800u

#define stm32_f_saddr     0x08003800u
#define stm32_f_size      0x2800u

#define stm32_a_saddr     0x08006000u
#define stm32_a_size      0x4800u
#define stm32_a_eaddr     (stm32_a_saddr + stm32_a_size - 1u)

#define sram_begin 0x20000000u
#define sram_end   0x20005000u

#define ota_flag_set 0xAABB1122u
#define ota_magic    0x55AA5A5Au

#endif
