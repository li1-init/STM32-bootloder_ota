#ifndef __OTA_INFO_H
#define __OTA_INFO_H

#include "stm32f10x.h"

/* ============================ OTA 信息结构（W25Q64）============================ */
/*
 * 放在 W25Q64，主/备两份，各占一个 4KB 扇区：
 *   0x000000  主份
 *   0x001000  备份
 *
 * 写入规则：永远写"旧的那一份"（当前有效份的对侧），保留最新一份。
 * 掉电时最多丢一次提交，另一份仍可读。
 */

#define OTA_INFO_MAGIC    0x55AA5A5Au
#define OTA_FLAG_SET      0xAABB1122u
#define OTA_FLAG_NONE     0x00000000u

#define OTA_INFO_PRIMARY  0x000000u
#define OTA_INFO_BACKUP   0x001000u
#define OTA_INFO_SECTOR   4096u

/* 固件暂存区 */
#define OTA_STAGING_ADDR  0x010000u
#define OTA_STAGING_SIZE  0x20000u /* 128KB */

/* 版本号：0x00MMmmpp，例如 v1.2.3 = 0x00010203 */
#define OTA_VER(major, minor, patch) \
    ((((uint32_t)(major) & 0xFFu) << 16) | (((uint32_t)(minor) & 0xFFu) << 8) | ((uint32_t)(patch) & 0xFFu))

typedef __PACKED_STRUCT
{
    uint32_t magic;          /* OTA_INFO_MAGIC */
    uint32_t seq;            /* 每次写 +1，取最大 */
    uint32_t flag;           /* OTA_FLAG_SET=待搬运 / OTA_FLAG_NONE=无 */
    uint32_t fw_len;         /* 固件真实长度 */
    uint32_t fw_crc32;       /* 整包 CRC32 */
    uint32_t version;        /* 当前版本 */
    uint32_t target_version; /* 目标版本 */
    uint32_t block_count;    /* 已收分片数 */
    uint32_t active;         /* 0=APP1 1=APP2（预留） */
    uint32_t pending;        /* 0xFF=无（预留） */
    uint32_t state;          /* 0=IDLE 1=TRIAL 2=CONFIRMED（预留） */
    uint32_t retry;          /* TRIAL 剩余次数（预留） */
    uint32_t info_crc;       /* 前 48 字节的 CRC32 */
} ota_info_t;                /* sizeof == 52 */

/* ============================ 基础 CRC ============================ */

uint32_t ota_crc32(const void *buf, uint32_t len);     /* CRC32-IEEE，与 PC 的 zlib.crc32 一致 */
uint32_t ota_crc32_update(uint32_t crc, const void *buf, uint32_t len); /* crc 初值传 0xFFFFFFFF */
uint32_t ota_crc32_final(uint32_t crc);

/* ============================ OTA 信息读写 ============================ */

int      ota_info_load(ota_info_t *info);        /* 0=读到有效记录 -1=两份都无效 */
int      ota_info_save(const ota_info_t *info);  /* 写"旧的那一份"，返回 0 成功 */
void     ota_info_clear(ota_info_t *info);       /* 填默认值：seq=0 flag=NONE */
void     ota_info_dump(const ota_info_t *info);  /* 打印一份记录（调试用） */
int      ota_info_format(void);                  /* 把主/备两份都擦成 0xFF */

/* ============================ 自测 ============================ */

int ota_selftest_w25q64(void);  /* 读 ID + 擦除 + 跨页写读 + 返回 0 通过 */

#endif
