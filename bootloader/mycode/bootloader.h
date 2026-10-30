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
#define stm32_flash_saddr 0x08000000u // FLASH起始地址
#define stm32_page_size 2048u         // STM32‑F1每页大小：2048字节！！
#define stm32_page_num 64u            // 总页数，128KB / 2048 = 64页

#define stm32_b_page_num 8u // B区页数，8页

#define stm32_a_page_num (stm32_page_num - stm32_b_page_num)                     // A区页数，56页
#define stm32_a_start_page stm32_b_page_num                                      // A区起始页，第8页
#define stm32_a_saddr (stm32_flash_saddr + stm32_a_start_page * stm32_page_size) // A区起始地址  //A区起始地址：0x08000000 + 8*2048 = 0x08004000

// 可选补充边界，用于ota烧录时判断地址越界
#define stm32_b_eaddr (stm32_flash_saddr + stm32_b_page_num * stm32_page_size - 1u)
#define stm32_a_eaddr (stm32_a_saddr + stm32_a_page_num * stm32_page_size - 1u)

// sram范围 stm32f103c8t6
#define sram_begin 0x20000000u
#define sram_end 0x20004fffu

/**
 * @brief 定义OTA相关的标志位
 *
 */
#define ota_flag_set 0xAABB1122u
#define OTA_W25Q_ADDR 0x000000U // W25Q64存放标志的地址
#define ota_magic        0x55AA5A5AU  //魔数的标志

typedef __packed struct
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



uint8_t uasrt_command(uint32_t time);



void uasrt_command_test(void);

#endif
