#ifndef __BOOTLOADER_H
#define __BOOTLOADER_H

#include "stm32f10x.h"
#include "string.h"
#include "stdio.h"
#include "ota_info.h"

/* 统一 Flash/SRAM 分区定义见 common/partition_layout.h */
#include "partition_layout.h"

#define ota_flag_set 0xAABB1122u
#define ota_magic    0x55AA5A5Au

uint8_t app_is_valid(uint32_t base, uint32_t size);
void    jump_to_addr(uint32_t base, uint32_t size);
void    jump_to_app(void);
void    jump_to_factory(void);

int      boot_copy_staging_to_app(uint32_t fw_len, uint32_t fw_crc32);
uint32_t boot_staging_crc32(uint32_t len);
uint8_t  boot_main_process(void);

uint8_t uasrt_command(uint32_t time);
void    uasrt_command_suf(void);
void    boot_menu(void);

#endif
