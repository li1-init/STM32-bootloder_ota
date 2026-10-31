#include "stm32f10x.h"
#include "string.h"
#include "stdio.h"
#include "my_usart.h"
#include "W25Q64.h"
#include "ota_info.h"

/* ============================ CRC32 ============================ */

uint32_t ota_crc32_update(uint32_t crc, const void *buf, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t i, j;

    for (i = 0; i < len; i++)
    {
        crc ^= p[i];
        for (j = 0; j < 8u; j++)
        {
            if (crc & 1u)
            {
                crc = (crc >> 1) ^ 0xEDB88320u;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return crc;
}

uint32_t ota_crc32_final(uint32_t crc)
{
    return ~crc;
}

uint32_t ota_crc32(const void *buf, uint32_t len)
{
    return ota_crc32_final(ota_crc32_update(0xFFFFFFFFu, buf, len));
}

/* ============================ OTA 淇℃伅 ============================ */

#define OTA_INFO_CRC_LEN (sizeof(ota_info_t) - sizeof(uint32_t)) /* 鍓?48B */

void ota_info_clear(ota_info_t *info)
{
    memset(info, 0, sizeof(ota_info_t));
    info->magic  = OTA_INFO_MAGIC;
    info->flag   = OTA_FLAG_NONE;
    info->pending = 0xFFu;
    info->info_crc = ota_crc32(info, OTA_INFO_CRC_LEN);
}

/* 鏍￠獙涓€浠借褰曪細magic銆乮nfo_crc銆佸熀鏈寖鍥?*/
static int ota_info_check(const ota_info_t *info)
{
    if (info->magic != OTA_INFO_MAGIC)
    {
        return 0;
    }
    if (info->info_crc != ota_crc32(info, OTA_INFO_CRC_LEN))
    {
        return 0;
    }
    if (info->fw_len > OTA_STAGING_SIZE)
    {
        return 0;
    }
    if (info->flag != OTA_FLAG_SET && info->flag != OTA_FLAG_NONE)
    {
        return 0;
    }
    return 1;
}

static int ota_read_slot(uint32_t addr, ota_info_t *info)
{
    W25Q64_ReadData(addr, (uint8_t *)info, sizeof(ota_info_t));
    return ota_info_check(info);
}

/*
 * 璇昏褰曪細涓や唤閮借锛屽彇 seq 澶х殑鏈夋晥浠姐€? * seq 姣旇緝鐢ㄦ湁绗﹀彿宸€硷紝澶勭悊濂?0xFFFFFFFF -> 0 鐨勫洖缁曘€? */
int ota_info_load(ota_info_t *info)
{
    ota_info_t a, b;
    int ok_a = ota_read_slot(OTA_INFO_PRIMARY, &a);
    int ok_b = ota_read_slot(OTA_INFO_BACKUP, &b);

    if (ok_a && ok_b)
    {
        if ((int32_t)(a.seq - b.seq) >= 0)
        {
            *info = a;
        }
        else
        {
            *info = b;
        }
        return 0;
    }
    if (ok_a)
    {
        *info = a;
        return 0;
    }
    if (ok_b)
    {
        *info = b;
        return 0;
    }

    ota_info_clear(info);
    return -1;
}

/* 杩斿洖褰撳墠鏈夋晥浠芥墍鍦ㄧ殑鎵囧尯鍦板潃锛涗袱浠介兘鏃犳晥杩斿洖 0 */
static uint32_t ota_info_current_addr(void)
{
    ota_info_t a, b;
    int ok_a = ota_read_slot(OTA_INFO_PRIMARY, &a);
    int ok_b = ota_read_slot(OTA_INFO_BACKUP, &b);

    if (ok_a && ok_b)
    {
        return ((int32_t)(a.seq - b.seq) >= 0) ? OTA_INFO_PRIMARY : OTA_INFO_BACKUP;
    }
    if (ok_a)
    {
        return OTA_INFO_PRIMARY;
    }
    if (ok_b)
    {
        return OTA_INFO_BACKUP;
    }
    return 0;
}

/*
 * 鍐欒褰曪細
 *   1. 鎵惧綋鍓嶆湁鏁堜唤 current_addr
 *   2. 鐩爣 = 瀵逛晶鎵囧尯锛坈urrent 鏃犳晥鍒欏啓涓讳唤锛? *   3. seq = max(涓? 澶? + 1
 *   4. 鎿?4KB -> 鍐?52B -> 鍥炶姣旇緝
 */
int ota_info_save(const ota_info_t *info)
{
    ota_info_t a, b;
    uint32_t target;
    uint32_t base_seq = 0;
    ota_info_t tmp;

    tmp = *info;

    /* 璁＄畻 seq 鍩哄€硷細涓や唤閲屾渶澶х殑 seq */
    if (ota_read_slot(OTA_INFO_PRIMARY, &a))
    {
        base_seq = a.seq;
    }
    if (ota_read_slot(OTA_INFO_BACKUP, &b))
    {
        if ((int32_t)(b.seq - base_seq) > 0)
        {
            base_seq = b.seq;
        }
    }
    tmp.seq = base_seq + 1u;

    /* 閫夌洰鏍囨墖鍖猴細鍐?鏃х殑閭ｄ竴浠? */
    target = ota_info_current_addr();
    if (target == OTA_INFO_PRIMARY)
    {
        target = OTA_INFO_BACKUP;
    }
    else if (target == OTA_INFO_BACKUP)
    {
        target = OTA_INFO_PRIMARY;
    }
    else
    {
        target = OTA_INFO_PRIMARY; /* 涓や唤閮芥棤鏁堬紝鍏堝啓涓讳唤 */
    }

    tmp.magic    = OTA_INFO_MAGIC;
    tmp.info_crc = ota_crc32(&tmp, OTA_INFO_CRC_LEN);

    if (W25Q64_EraseSector(target) != W25Q64_OK)
    {
        u1_printf("ota_info_save: erase 0x%08lX failed\r\n", (unsigned long)target);
        return -1;
    }
    if (W25Q64_Write(target, (const uint8_t *)&tmp, sizeof(ota_info_t)) != W25Q64_OK)
    {
        u1_printf("ota_info_save: write 0x%08lX failed\r\n", (unsigned long)target);
        return -1;
    }
    {
        ota_info_t chk;
        W25Q64_ReadData(target, (uint8_t *)&chk, sizeof(ota_info_t));
        if (memcmp(&chk, &tmp, sizeof(ota_info_t)) != 0)
        {
            u1_printf("ota_info_save: verify fail at 0x%08lX\r\n", (unsigned long)target);
            u1_printf("  write =");
            {
                const uint8_t *p = (const uint8_t *)&tmp;
                uint32_t k;
                for (k = 0u; k < sizeof(ota_info_t); k++)
                {
                    if ((k % 16u) == 0u) u1_printf("\r\n    %02lu:", (unsigned long)k);
                    u1_printf(" %02X", p[k]);
                }
            }
            u1_printf("\r\n  read  =");
            {
                const uint8_t *p = (const uint8_t *)&chk;
                uint32_t k;
                for (k = 0u; k < sizeof(ota_info_t); k++)
                {
                    if ((k % 16u) == 0u) u1_printf("\r\n    %02lu:", (unsigned long)k);
                    u1_printf(" %02X", p[k]);
                }
            }
            u1_printf("\r\n");
            return -1;
        }
    }
    return 0;
}

/* 把主/备两份都擦成 0xFF，相当于清空所有 OTA 记录。
 * 用于制作测试任务前先清场，避免旧记录影响 seq 和目标扇区选择。 */
int ota_info_format(void)
{
    if (W25Q64_EraseSector(OTA_INFO_PRIMARY) != W25Q64_OK)
    {
        u1_printf("ota_info_format: erase primary failed\r\n");
        return -1;
    }
    if (W25Q64_EraseSector(OTA_INFO_BACKUP) != W25Q64_OK)
    {
        u1_printf("ota_info_format: erase backup failed\r\n");
        return -1;
    }
    u1_printf("ota_info_format: primary + backup erased\r\n");
    return 0;
}

void ota_info_dump(const ota_info_t *info)
{
    u1_printf("OTA magic=0x%08lX seq=%lu flag=0x%08lX fw_len=%lu crc=0x%08lX\r\n",
              (unsigned long)info->magic, (unsigned long)info->seq,
              (unsigned long)info->flag, (unsigned long)info->fw_len,
              (unsigned long)info->fw_crc32);
    u1_printf("    ver=%lu.%lu.%lu target=%lu.%lu.%lu block=%lu active=%lu pending=0x%02lX state=%lu\r\n",
              (unsigned long)((info->version >> 16) & 0xFFu),
              (unsigned long)((info->version >> 8) & 0xFFu),
              (unsigned long)(info->version & 0xFFu),
              (unsigned long)((info->target_version >> 16) & 0xFFu),
              (unsigned long)((info->target_version >> 8) & 0xFFu),
              (unsigned long)(info->target_version & 0xFFu),
              (unsigned long)info->block_count,
              (unsigned long)info->active,
              (unsigned long)info->pending,
              (unsigned long)info->state);
}

/* ============================ W25Q64 鑷祴 ============================ */

/*
 * 鑷祴锛? *   1. 璇?ID
 *   2. 4KB 鎿﹂櫎 -> 搴斿叏 0xFF
 *   3. 0x0103FF 闈炲榻愬啓 256B -> 鍥炶姣旇緝锛堣繖鏉℃渶瀹规槗鏆撮湶璺ㄩ〉 bug锛? *   4. 杩斿洖 0 閫氳繃
 */
int ota_selftest_w25q64(void)
{
    uint8_t mid = 0;
    uint16_t did = 0;
    uint8_t buf[256];
    uint16_t i;

    W25Q64_Init();
    W25Q64_ReadID(&mid, &did);
    u1_printf("W25Q64 MID=0x%02X DID=0x%04X\r\n", mid, did);

    if (mid != 0xEF && did != 0x4017 && did != 0x4018)
    {
        u1_printf("W25Q64 璇讳笉鍒?ID锛屾鏌ユ帴绾?渚涚數/CS\r\n");
        return -1;
    }

    if (W25Q64_EraseSector(0x010000u) != W25Q64_OK)
    {
        u1_printf("鎵囧尯鎿﹂櫎澶辫触\r\n");
        return -1;
    }
    if (W25Q64_IsBlank(0x010000u, W25Q64_SECTOR_SIZE) != W25Q64_OK)
    {
        u1_printf("鎿﹂櫎鍚庝笉鏄叏 0xFF\r\n");
        return -1;
    }

    for (i = 0; i < sizeof(buf); i++)
    {
        buf[i] = (uint8_t)i;
    }
    if (W25Q64_Write(0x0103FFu, buf, sizeof(buf)) != W25Q64_OK)
    {
        u1_printf("闈炲榻愬啓澶辫触\r\n");
        return -1;
    }
    if (W25Q64_Verify(0x0103FFu, buf, sizeof(buf)) != W25Q64_OK)
    {
        u1_printf("闈炲榻愬啓璇讳笉涓€鑷碶r\n");
        return -1;
    }

    u1_printf("W25Q64 鑷祴閫氳繃\r\n");
    return 0;
}
