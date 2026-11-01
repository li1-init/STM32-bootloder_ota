#include "stm32f10x.h"
#include "string.h"
#include "MySPI.h"
#include "W25Q64_Ins.h"
#include "W25Q64.h"

/* ============================ 基础驱动 ============================ */

void W25Q64_Init(void)
{
    MySPI_Init();

    /* 本工程独占整片 W25Q64，启动时清 BP/TB/SEC/SRP，防旧工程留下的写保护。 */
    MySPI_Start();
    MySPI_SwapByte(W25Q64_WRITE_ENABLE);
    MySPI_Stop();

    MySPI_Start();
    MySPI_SwapByte(W25Q64_WRITE_STATUS_REGISTER);
    MySPI_SwapByte(0x00u);
    MySPI_SwapByte(0x00u);
    MySPI_Stop();

    (void)W25Q64_WaitBusy();
}
void W25Q64_ReadID(uint8_t *MID, uint16_t *DID)
{
    MySPI_Start();
    MySPI_SwapByte(W25Q64_JEDEC_ID);
    *MID = MySPI_SwapByte(W25Q64_DUMMY_BYTE);
    *DID = MySPI_SwapByte(W25Q64_DUMMY_BYTE);
    *DID <<= 8;
    *DID |= MySPI_SwapByte(W25Q64_DUMMY_BYTE);
    MySPI_Stop();
}

uint8_t W25Q64_ReadStatus1(void)
{
    uint8_t value;

    MySPI_Start();
    MySPI_SwapByte(W25Q64_READ_STATUS_REGISTER_1);
    value = MySPI_SwapByte(W25Q64_DUMMY_BYTE);
    MySPI_Stop();
    return value;
}

int W25Q64_WriteEnable(void)
{
    uint8_t status;

    MySPI_Start();
    MySPI_SwapByte(W25Q64_WRITE_ENABLE);
    MySPI_Stop();

    /* 命令生效后 SR1.WEL 必须为 1，否则后面的擦除/编程会被芯片忽略。 */
    status = W25Q64_ReadStatus1();
    return ((status & 0x02u) != 0u) ? W25Q64_OK : W25Q64_ERR;
}

/*
 * 等待 Busy 位清零。
 * 原版超时后只是 break，上层无法知道写失败；这里返回 -1。
 * 超时计数放大到 0x200000：72MHz 下约几百 ms，足够 4KB 扇区擦除。
 */
int W25Q64_WaitBusy(void)
{
    uint32_t Timeout = 0x200000u;

    MySPI_Start();
    MySPI_SwapByte(W25Q64_READ_STATUS_REGISTER_1);
    while (MySPI_SwapByte(W25Q64_DUMMY_BYTE) & 0x01u)
    {
        if (--Timeout == 0u)
        {
            MySPI_Stop();
            return W25Q64_ERR;
        }
    }
    MySPI_Stop();
    return W25Q64_OK;
}

int W25Q64_PageProgram(uint32_t Address, uint8_t *DataArray, uint16_t Count)
{
    uint16_t i;

    if (Count == 0u)
    {
        return W25Q64_OK;
    }
    if (W25Q64_WriteEnable() != W25Q64_OK)
    {
        return W25Q64_ERR;
    }

    MySPI_Start();
    MySPI_SwapByte(W25Q64_PAGE_PROGRAM);
    MySPI_SwapByte((uint8_t)(Address >> 16));
    MySPI_SwapByte((uint8_t)(Address >> 8));
    MySPI_SwapByte((uint8_t)(Address));
    for (i = 0; i < Count; i++)
    {
        MySPI_SwapByte(DataArray[i]);
    }
    MySPI_Stop();

    return W25Q64_WaitBusy();
}

int W25Q64_SectorErase(uint32_t Address)
{
    if (W25Q64_WriteEnable() != W25Q64_OK)
    {
        return W25Q64_ERR;
    }

    MySPI_Start();
    MySPI_SwapByte(W25Q64_SECTOR_ERASE_4KB);
    MySPI_SwapByte((uint8_t)(Address >> 16));
    MySPI_SwapByte((uint8_t)(Address >> 8));
    MySPI_SwapByte((uint8_t)(Address));
    MySPI_Stop();

    return W25Q64_WaitBusy();
}

void W25Q64_ReadData(uint32_t Address, uint8_t *DataArray, uint32_t Count)
{
    uint32_t i;

    MySPI_Start();
    MySPI_SwapByte(W25Q64_READ_DATA);
    MySPI_SwapByte((uint8_t)(Address >> 16));
    MySPI_SwapByte((uint8_t)(Address >> 8));
    MySPI_SwapByte((uint8_t)(Address));
    for (i = 0; i < Count; i++)
    {
        DataArray[i] = MySPI_SwapByte(W25Q64_DUMMY_BYTE);
    }
    MySPI_Stop();
}

/* ============================ 安全包装 ============================ */

int W25Q64_EraseSector(uint32_t addr)
{
    if ((addr & (W25Q64_SECTOR_SIZE - 1u)) != 0u)
    {
        return W25Q64_ERR; /* 必须 4KB 对齐 */
    }
    if (addr >= W25Q64_FLASH_SIZE)
    {
        return W25Q64_ERR;
    }

    if (W25Q64_SectorErase(addr) != W25Q64_OK)
    {
        return W25Q64_ERR;
    }

    /* 擦除命令返回不等于真的擦掉了；必须回读确认整扇区为 0xFF。 */
    return W25Q64_IsBlank(addr, W25Q64_SECTOR_SIZE);
}

int W25Q64_EraseSectors(uint32_t addr, uint32_t len)
{
    uint32_t end = addr + len;

    if (len == 0u || end > W25Q64_FLASH_SIZE)
    {
        return W25Q64_ERR;
    }

    for (; addr < end; addr += W25Q64_SECTOR_SIZE)
    {
        if (W25Q64_EraseSector(addr) != W25Q64_OK)
        {
            return W25Q64_ERR;
        }
    }
    return W25Q64_OK;
}

/*
 * 任意长度写：
 *   1. 检查范围
 *   2. 按"本页剩余空间"拆分，保证每次 PageProgram 不跨 256B 页
 */
int W25Q64_Write(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    if (buf == 0 || len == 0u || (addr + len) > W25Q64_FLASH_SIZE)
    {
        return W25Q64_ERR;
    }

    while (len > 0u)
    {
        uint32_t chunk = W25Q64_PAGE_SIZE - (addr % W25Q64_PAGE_SIZE);

        if (chunk > len)
        {
            chunk = len;
        }

        if (W25Q64_PageProgram(addr, (uint8_t *)buf, (uint16_t)chunk) != W25Q64_OK)
        {
            return W25Q64_ERR;
        }

        addr += chunk;
        buf  += chunk;
        len  -= chunk;
    }
    return W25Q64_OK;
}

int W25Q64_Verify(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    uint32_t i;
    uint8_t  chunk[64];

    if (buf == 0 || (addr + len) > W25Q64_FLASH_SIZE)
    {
        return W25Q64_ERR;
    }

    while (len > 0u)
    {
        uint32_t n = (len > sizeof(chunk)) ? sizeof(chunk) : len;

        W25Q64_ReadData(addr, chunk, n);
        for (i = 0; i < n; i++)
        {
            if (chunk[i] != buf[i])
            {
                return W25Q64_ERR;
            }
        }

        addr += n;
        buf  += n;
        len  -= n;
    }
    return W25Q64_OK;
}

int W25Q64_IsBlank(uint32_t addr, uint32_t len)
{
    uint32_t i;
    uint8_t  chunk[64];

    if ((addr + len) > W25Q64_FLASH_SIZE)
    {
        return W25Q64_ERR;
    }

    while (len > 0u)
    {
        uint32_t n = (len > sizeof(chunk)) ? sizeof(chunk) : len;

        W25Q64_ReadData(addr, chunk, n);
        for (i = 0; i < n; i++)
        {
            if (chunk[i] != 0xFFu)
            {
                return W25Q64_ERR;
            }
        }

        addr += n;
        len  -= n;
    }
    return W25Q64_OK;
}
