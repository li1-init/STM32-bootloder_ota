#include "stm32f10x.h"
#include "string.h"
#include "my_usart.h"
#include "struct_init.h"
#include "bootloader.h"
#include "app_update.h"

/*
 * ============================================================
 *  XMODEM receive for Factory (brick recovery)
 * ============================================================
 *  与 Boot 的 xmodem.c 不同点：
 *    - 不碰 W25Q64（Factory 必须能在外部 Flash 损坏时救砖）
 *    - 每收满 1KB 边收边擦写 APP 分区内部 Flash
 *    - 收完 EOT 后再算一次整段 CRC32 校验
 *
 *  数据来源：usart_ota_getchar()（rx_ota 字节流，非菜单 FIFO）
 */

#define X_SOH       0x01u
#define X_STX       0x02u
#define X_EOT       0x04u
#define X_ACK       0x06u
#define X_NAK       0x15u
#define X_CAN       0x18u
#define X_CRC_C     0x43u

#define X_PKT       128u
#define X_PKT_MAX   1024u
#define X_BLOCK     1024u   /* 内部 Flash 一次擦写单位 */
#define X_C_WAIT    800u

#define APP_BASE   stm32_a_saddr
#define APP_SIZE   stm32_a_size
#define APP_PAGE   stm32_page_size

/* CRC16-CCITT, init 0x0000 */
static uint16_t x_crc16(const uint8_t *p, uint16_t n)
{
    uint16_t crc = 0x0000u;
    uint16_t i;
    uint8_t  b;

    for (i = 0; i < n; i++)
    {
        crc ^= (uint16_t)((uint16_t)p[i] << 8);
        for (b = 0; b < 8u; b++)
        {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* CRC32-IEEE，增量 */
static uint32_t crc32_upd(uint32_t crc, const uint8_t *p, uint32_t n)
{
    uint32_t i, j;
    for (i = 0; i < n; i++)
    {
        crc ^= p[i];
        for (j = 0; j < 8u; j++)
        {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
    }
    return crc;
}

static int x_getc(uint32_t ms)
{
    uint32_t t0 = get_now();
    int c;

    while ((get_now() - t0) < ms)
    {
        c = usart_ota_getchar();
        if (c >= 0)
        {
            return c;
        }
    }
    return -1;
}

/* 把 1KB 缓冲擦写进 APP 分区：先擦 1KB 页，再按半字写 */
static int flash_write_block(uint32_t offset, const uint8_t *buf, uint16_t len)
{
    uint16_t i;
    uint32_t addr = APP_BASE + offset;

    if ((offset % APP_PAGE) == 0u)
    {
        if (FLASH_ErasePage(addr) != FLASH_COMPLETE)
        {
            return -1;
        }
    }

    for (i = 0; i < len; i += 2u)
    {
        uint16_t half = (uint16_t)buf[i];
        if ((uint16_t)(i + 1u) < len)
        {
            half |= (uint16_t)((uint16_t)buf[i + 1u] << 8);
        }
        else
        {
            half |= 0xFF00u; /* 奇数长度：最后一个半字高字节填 0xFF */
        }
        if (FLASH_ProgramHalfWord(addr + i, half) != FLASH_COMPLETE)
        {
            return -1;
        }
        if (*(volatile uint16_t *)(addr + i) != half)
        {
            return -1;
        }
    }
    return 0;
}

/*
 * 返回 0 成功，len/crc32 通过输出参数返回
 */
int factory_xmodem_receive(uint32_t *out_len, uint32_t *out_crc32)
{
    static uint8_t blk[X_BLOCK];
    uint16_t blk_used = 0u;
    uint32_t written = 0u;
    uint32_t crc_run = 0xFFFFFFFFu;
    uint32_t last_c = 0xFFFFFFFFu;
    static uint8_t data[X_PKT_MAX];
    int      c;

    *out_len = 0u;
    *out_crc32 = 0u;

    usart_ota_flush();

    /* 进入接收前先写“更新中”标志。中途断电时 Boot 会强制回到 Factory。 */
    if (app_update_begin() != 0)
    {
        u1_printf("FACTORY: set update flag failed\r\n");
        return -1;
    }

    u1_printf("FACTORY: XMODEM ready (CRC), send APP bin now\r\n");
    u1_printf("FACTORY: will erase APP at 0x%08lX\r\n", (unsigned long)APP_BASE);

    /* 先擦掉 APP 的前 1KB，让 Boot 立即判定 APP 无效（防止旧 APP 顶掉） */
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
    if (FLASH_ErasePage(APP_BASE) != FLASH_COMPLETE)
    {
        FLASH_Lock();
        u1_printf("FACTORY: erase first page failed\r\n");
        return -1;
    }

    for (;;)
    {
        if (written == 0u && blk_used == 0u)
        {
            if ((get_now() - last_c) >= X_C_WAIT)
            {
                u1_putc(X_CRC_C);
                last_c = get_now();
            }
        }

        c = x_getc(50u);
        if (c < 0)
        {
            continue;
        }
        if ((uint8_t)c == X_EOT)
        {
            u1_putc(X_ACK);
            break;
        }
        if ((uint8_t)c == X_CAN)
        {
            FLASH_Lock();
            u1_printf("\r\nFACTORY: sender cancelled\r\n");
            return -1;
        }
        if ((uint8_t)c == X_CRC_C || (uint8_t)c == X_ACK || (uint8_t)c == X_NAK)
        {
            continue;
        }
        if ((uint8_t)c != X_SOH && (uint8_t)c != X_STX)
        {
            continue;
        }

        {
            uint16_t pkt = ((uint8_t)c == X_SOH) ? 128u : 1024u;
            int b1 = x_getc(1000u);
            int b2 = x_getc(1000u);
            uint16_t i;

            if (b1 < 0 || b2 < 0)
            {
                u1_putc(X_NAK);
                continue;
            }
            if (((uint8_t)b1 ^ (uint8_t)b2) != 0xFFu)
            {
                u1_putc(X_NAK);
                continue;
            }
            if (pkt > X_PKT_MAX || (written + blk_used + pkt) > APP_SIZE)
            {
                FLASH_Lock();
                u1_putc(X_CAN);
                u1_putc(X_CAN);
                u1_printf("\r\nFACTORY: firmware too large\r\n");
                return -1;
            }
            for (i = 0; i < pkt; i++)
            {
                c = x_getc(1000u);
                if (c < 0)
                {
                    break;
                }
                data[i] = (uint8_t)c;
            }
            if (i < pkt || c < 0)
            {
                u1_putc(X_NAK);
                continue;
            }
            {
                int hi = x_getc(1000u);
                int lo = x_getc(1000u);
                uint16_t crc_recv;

                if (hi < 0 || lo < 0)
                {
                    u1_putc(X_NAK);
                    continue;
                }
                crc_recv = (uint16_t)(((uint16_t)hi << 8) | (uint16_t)lo);
                if (x_crc16(data, pkt) != crc_recv)
                {
                    u1_putc(X_NAK);
                    continue;
                }
            }

            /* 塞进 1KB 块，满了就写 Flash */
            {
                uint16_t off = 0u;
                while (off < pkt)
                {
                    uint16_t room = (uint16_t)(X_BLOCK - blk_used);
                    uint16_t take = (uint16_t)((pkt - off) > room ? room : (pkt - off));

                    memcpy(&blk[blk_used], &data[off], take);
                    blk_used = (uint16_t)(blk_used + take);
                    off = (uint16_t)(off + take);

                    if (blk_used == X_BLOCK)
                    {
                        if (written + X_BLOCK > APP_SIZE)
                        {
                            FLASH_Lock();
                            u1_putc(X_CAN);
                            u1_printf("\r\nFACTORY: firmware too large\r\n");
                            return -1;
                        }
                        crc_run = crc32_upd(crc_run, blk, X_BLOCK);
                        if (flash_write_block(written, blk, X_BLOCK) != 0)
                        {
                            FLASH_Lock();
                            u1_putc(X_CAN);
                            u1_printf("\r\nFACTORY: flash write fail at %lu\r\n", (unsigned long)written);
                            return -1;
                        }
                        written += X_BLOCK;
                        blk_used = 0u;
                    }
                }
            }

            u1_putc(X_ACK);
            u1_printf("\r  blk=%3u  %lu bytes", (unsigned)(uint8_t)b1, (unsigned long)(written + blk_used));
        }
    }

    /* 处理最后不足 1KB 的块：去掉 0x1A 填充 */
    if (blk_used > 0u)
    {
        uint16_t i = blk_used;
        while (i > 0u && blk[i - 1u] == 0x1Au)
        {
            i--;
        }
        if (i > 0u)
        {
            crc_run = crc32_upd(crc_run, blk, i);
            if (flash_write_block(written, blk, i) != 0)
            {
                FLASH_Lock();
                u1_printf("\r\nFACTORY: flash write fail at tail\r\n");
                return -1;
            }
            written += i;
        }
    }

    FLASH_Lock();

    if (written == 0u)
    {
        u1_printf("\r\nFACTORY: empty firmware\r\n");
        return -1;
    }

    *out_len = written;
    *out_crc32 = ~crc_run;

    /* 回读校验：整个 APP 区的 CRC32 */
    {
        uint32_t crc2 = 0xFFFFFFFFu;
        uint32_t off = 0;
        while (off < written)
        {
            uint32_t n = (written - off > 256u) ? 256u : (written - off);
            crc2 = crc32_upd(crc2, (const uint8_t *)(APP_BASE + off), n);
            off += n;
        }
        crc2 = ~crc2;
        u1_printf("\r\nFACTORY: recv crc32=0x%08lX readback crc32=0x%08lX %s\r\n",
                  (unsigned long)*out_crc32, (unsigned long)crc2,
                  (crc2 == *out_crc32) ? "OK" : "MISMATCH");
        if (crc2 != *out_crc32)
        {
            return -1;
        }
    }

    if (app_update_finish() != 0)
    {
        u1_printf("FACTORY: clear update flag failed\r\n");
        return -1;
    }

    u1_printf("FACTORY: APP image committed\r\n");
    return 0;
}
