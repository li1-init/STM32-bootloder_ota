#ifndef __W25Q64_H
#define __W25Q64_H

#include "stm32f10x.h"

/* ---------------- 容量 ---------------- */
#define W25Q64_FLASH_SIZE  0x800000u  /* 8MB = 8 * 1024 * 1024 */
#define W25Q64_PAGE_SIZE   256u       /* 页编程最大 256B，不能跨页 */
#define W25Q64_SECTOR_SIZE 4096u      /* 最小擦除单位 4KB */
#define W25Q64_BLOCK_SIZE  65536u     /* 大块擦除 64KB */

/* ---------------- 返回码 ---------------- */
#define W25Q64_OK    0
#define W25Q64_ERR  (-1)

/* ---------------- 基础驱动（江协风格） ---------------- */
void W25Q64_Init(void);
void W25Q64_ReadID(uint8_t *MID, uint16_t *DID);
uint8_t W25Q64_ReadStatus1(void);
int  W25Q64_WriteEnable(void);                                   /* 0=WEL 已置位 -1=失败 */
int  W25Q64_WaitBusy(void);                                      /* 0=空闲 -1=超时 */
int  W25Q64_PageProgram(uint32_t Address, uint8_t *DataArray, uint16_t Count);
int  W25Q64_SectorErase(uint32_t Address);
void W25Q64_ReadData(uint32_t Address, uint8_t *DataArray, uint32_t Count);

/* ---------------- 安全包装：地址检查 + 跨页拆分 + 回读校验 ---------------- */
int W25Q64_EraseSector(uint32_t addr);                            /* addr 必须 4KB 对齐 */
int W25Q64_EraseSectors(uint32_t addr, uint32_t len);             /* 自动按 4KB 拆分 */
int W25Q64_Write(uint32_t addr, const uint8_t *buf, uint32_t len);/* 任意长度、任意对齐 */
int W25Q64_Verify(uint32_t addr, const uint8_t *buf, uint32_t len);/* 回读比较 */
int W25Q64_IsBlank(uint32_t addr, uint32_t len);                  /* 是否全 0xFF */

#endif
