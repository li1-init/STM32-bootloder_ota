#ifndef __BOOTLOADER_H
#define __BOOTLOADER_H

#include "stm32f10x.h"
#include "string.h"
#include "stdio.h"

/* 统一 Flash/SRAM 分区定义见 common/partition_layout.h */
#include "partition_layout.h"

#define ota_flag_set 0xAABB1122u
#define ota_magic    0x55AA5A5Au

#endif
