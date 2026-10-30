/**
  ******************************************************************************
  1. A区 存放程序
  2. B区 存放bootloader
  ******************************************************************************
  */

#ifndef __BOOTLOADER_H
#define __BOOTLOADER_H

#include "stm32f10x.h"
#include "string.h"
#include "stdio.h"

/**
 * @brief 定义bootloader的地址
 *
 */
/*
 * 64KB Flash 三分区（STM32F103C8T6，真实擦除页 1KB）
 *   Boot    0x08000000 ~ 0x08002FFF  12KB
 *   Factory 0x08003000 ~ 0x08004FFF   8KB
 *   APP1    0x08005000 ~ 0x0800A7FF  22KB
 *   APP2    0x0800A800 ~ 0x0800FFFF  22KB（预留）
 */
#define stm32_flash_saddr 0x08000000u
#define stm32_page_size   1024u
#define stm32_page_num    64u

#define stm32_b_saddr     0x08000000u
#define stm32_b_size      0x3000u
#define stm32_b_eaddr     (stm32_b_saddr + stm32_b_size - 1u)

#define stm32_f_saddr     0x08003000u
#define stm32_f_size      0x2000u
#define stm32_f_eaddr     (stm32_f_saddr + stm32_f_size - 1u)

#define stm32_a_saddr     0x08005000u
#define stm32_a_size      0x5800u
#define stm32_a_eaddr     (stm32_a_saddr + stm32_a_size - 1u)

/* SRAM 20KB，栈顶可以等于 0x20005000 */
#define sram_begin 0x20000000u
#define sram_end   0x20005000u

/**
 * @brief 定义OTA相关的标志位
 *
 */
#define ota_flag_set 0xAABB1122u
#define OTA_W25Q_ADDR 0x000000U // W25Q64存放标志的地址
#define ota_magic        0x55AA5A5AU  //魔数的标志

typedef __PACKED_STRUCT
{
    uint32_t magic;          //魔数，固定 0x55AA5A5A
    uint32_t OTA_flag;       //升级标志 ota_flag_set=0xAABB1122u
    uint32_t fw_len;         //新固件字节长度
    uint32_t fw_crc;         //新固件CRC32校验值
    uint8_t  version[16];    //固件版本字符串
    uint32_t crc;            //前面所有数据的校验和;
} OTA_t;

#define ota_t_size sizeof(OTA_t)

extern OTA_t OTA_Info;

void jump_to_app(void);

// 检查某个分区起始处是否是合法的程序镜像
uint8_t app_is_valid(uint32_t base, uint32_t size);

// 跳转到任意分区（APP1 / APP2 / Factory）
void jump_to_addr(uint32_t base, uint32_t size);



uint8_t uasrt_command(uint32_t time);



void uasrt_command_test(void);

#endif
