#include "stm32f10x.h"
#include "string.h"
#include "my_usart.h"
#include "struct_init.h"
#include "bootloader.h"
#include "ota_info.h"
#include "W25Q64.h"
#include "xmodem.h"

/*
 * ============================================================
 *  XMODEM receive (128-byte CRC mode)
 * ============================================================
 *  Frame : SOH(0x01) | BLCK | ~BLCK | DATA(128B) | CRC_H CRC_L
 *  Flow  : receiver sends 'C' first, then ACK/NAK per packet, EOT ends.
 *
 *  IMPORTANT: the Boot menu consumes single chars through a small 16-byte
 *  FIFO. XMODEM needs a much larger, timed stream, so it reads the raw
 *  USART IDLE buffer (rx_ota) and drains both buffers before starting.
 *  Mixing the two buffers is why an early version stalled at 0%.
 */

#define X_SOH       0x01u
#define X_STX       0x02u
#define X_EOT       0x04u
#define X_ACK       0x06u
#define X_NAK       0x15u
#define X_CAN       0x18u
#define X_CRC_C     0x43u /* 'C' */
#define X_PKT       128u
#define X_PKT_MAX   1024u
#define X_C_WAIT    800u  /* ms between 'C' retries while waiting first block */

/* CRC16-CCITT, poly 0x1021, init 0x0000 (XMODEM) */
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
            if (crc & 0x8000u)
            {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            }
            else
            {
                crc = (uint16_t)(crc << 1);
            }
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

int xmodem_receive(uint32_t *out_len, uint32_t *out_crc32)
{
    static ota_info_t info;
    static uint8_t  data[X_PKT_MAX];
    uint16_t last_pkt = 0u;
    uint32_t written = 0;
    uint32_t crc_run = 0xFFFFFFFFu;
    uint32_t last_c = 0xFFFFFFFFu;
    int      c;

    *out_len = 0;
    *out_crc32 = 0;

    /* W25Q64 self check */
    {
        uint8_t  mid = 0;
        uint16_t did = 0;
        W25Q64_Init();
        W25Q64_ReadID(&mid, &did);
        if ((mid != 0xEFu) || (did != 0x4017u && did != 0x4018u))
        {
            u1_printf("XMODEM: W25Q64 not found (MID=0x%02X DID=0x%04X)\r\n", mid, did);
            return -1;
        }
    }

    if (ota_info_format() != 0)
    {
        u1_printf("XMODEM: format OTA info failed\r\n");
        return -1;
    }
    if (W25Q64_EraseSectors(OTA_STAGING_ADDR, OTA_STAGING_SIZE) != W25Q64_OK)
    {
        u1_printf("XMODEM: erase staging failed\r\n");
        return -1;
    }

    /* 清空两个接收缓冲：菜单里按 '1' 的残余字符不能进 XMODEM */
    usart_ota_flush();
    usart_boot_rx_flush();

    u1_printf("XMODEM: ready (128B CRC), send file now\r\n");

    for (;;)
    {
        /* 首帧之前，每 X_C_WAIT ms 发一个 'C' */
        if (written == 0u)
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
            u1_printf("\r\nXMODEM: sender cancelled\r\n");
            return -1;
        }
        if ((uint8_t)c == X_CRC_C || (uint8_t)c == X_ACK || (uint8_t)c == X_NAK)
        {
            continue; /* 可能是回显 */
        }
        if ((uint8_t)c != X_SOH && (uint8_t)c != X_STX)
        {
            continue; /* 不是帧头 */
        }

        /* BLCK + ~BLCK + DATA + CRC */
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
            if (pkt > X_PKT_MAX || (written + pkt) > OTA_STAGING_SIZE)
            {
                u1_putc(X_CAN);
                u1_putc(X_CAN);
                u1_printf("\r\nXMODEM: packet too large\r\n");
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

            if (W25Q64_Write(OTA_STAGING_ADDR + written, data, pkt) != W25Q64_OK)
            {
                u1_putc(X_CAN);
                u1_printf("\r\nXMODEM: W25Q64 write fail at %lu\r\n", (unsigned long)written);
                return -1;
            }
            crc_run = ota_crc32_update(crc_run, data, pkt);
            written += pkt;
            last_pkt = pkt;

            u1_putc(X_ACK);
            u1_printf("\r  blk=%3u  %lu bytes", (unsigned)(uint8_t)b1, (unsigned long)written);
        }
    }

    /* strip 0x1A padding of the last packet, then recompute CRC32 */
    {
        uint32_t len = written;

        if (last_pkt > 0u && len >= last_pkt)
        {
            uint32_t i = last_pkt;

            W25Q64_ReadData(OTA_STAGING_ADDR + len - last_pkt, data, last_pkt);
            while (i > 0u && data[i - 1u] == 0x1Au)
            {
                i--;
            }

            {
                uint8_t  buf[256];
                uint32_t off = 0;
                uint32_t crc = 0xFFFFFFFFu;

                len = len - last_pkt + i;
                while (off < len)
                {
                    uint32_t chunk = (len - off > sizeof(buf)) ? (uint32_t)sizeof(buf) : (len - off);
                    W25Q64_ReadData(OTA_STAGING_ADDR + off, buf, (uint16_t)chunk);
                    crc = ota_crc32_update(crc, buf, chunk);
                    off += chunk;
                }
                crc_run = ota_crc32_final(crc);
            }
        }

        if (len == 0u || len > stm32_a_size)
        {
            u1_printf("\r\nXMODEM: bad length %lu\r\n", (unsigned long)len);
            return -1;
        }

        *out_len = len;
        *out_crc32 = crc_run;
    }

    memset(&info, 0, sizeof(info));
    ota_info_clear(&info);
    info.flag            = ota_flag_set;
    info.fw_len          = *out_len;
    info.fw_crc32        = *out_crc32;
    info.version         = OTA_VER(1, 0, 0);
    info.target_version  = OTA_VER(1, 1, 0);
    info.block_count     = (*out_len + 1023u) / 1024u;

    if (ota_info_save(&info) != 0)
    {
        u1_printf("\r\nXMODEM: save OTA info failed\r\n");
        return -1;
    }

    u1_printf("\r\nXMODEM: done, len=%lu crc32=0x%08lX\r\n",
              (unsigned long)*out_len, (unsigned long)*out_crc32);
    return 0;
}
