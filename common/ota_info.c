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

/* ============================ OTA 娣団剝浼?============================ */

#define OTA_INFO_CRC_LEN (sizeof(ota_info_t) - sizeof(uint32_t)) /* 閸?48B */

void ota_info_clear(ota_info_t *info)
{
    memset(info, 0, sizeof(ota_info_t));
    info->magic  = OTA_INFO_MAGIC;
    info->flag   = OTA_FLAG_NONE;
    info->pending = 0xFFu;
    info->info_crc = ota_crc32(info, OTA_INFO_CRC_LEN);
}

/* 閺嶏繝鐛欐稉鈧禒鍊燁唶瑜版洩绱癿agic閵嗕巩nfo_crc閵嗕礁鐔€閺堫剝瀵栭崶?*/
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
 * 鐠囨槒顔囪ぐ鏇窗娑撱倓鍞ら柈鍊燁嚢閿涘苯褰?seq 婢堆呮畱閺堝鏅ユ禒濮愨偓? * seq 濮ｆ棁绶濋悽銊︽箒缁楋箑褰垮顔尖偓纭风礉婢跺嫮鎮婃總?0xFFFFFFFF -> 0 閻ㄥ嫬娲栫紒鏇樷偓? */
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

/* 鏉╂柨娲栬ぐ鎾冲閺堝鏅ユ禒鑺ュ閸︺劎娈戦幍鍥у隘閸︽澘娼冮敍娑楄⒈娴犱粙鍏橀弮鐘虫櫏鏉╂柨娲?0 */
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
 * 閸愭瑨顔囪ぐ鏇窗
 *   1. 閹垫儳缍嬮崜宥嗘箒閺佸牅鍞?current_addr
 *   2. 閻╊喗鐖?= 鐎甸€涙櫠閹靛洤灏敍鍧坲rrent 閺冪姵鏅ラ崚娆忓晸娑撹鍞ら敍? *   3. seq = max(娑? 婢? + 1
 *   4. 閹?4KB -> 閸?52B -> 閸ョ偠顕板В鏃囩窛
 */
int ota_info_save(const ota_info_t *info)
{
    ota_info_t a, b;
    uint32_t target;
    uint32_t base_seq = 0;
    ota_info_t tmp;

    tmp = *info;

    /* 鐠侊紕鐣?seq 閸╁搫鈧》绱版稉銈勫敜闁插本娓舵径褏娈?seq */
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

    /* 闁娲伴弽鍥ㄥ閸栫尨绱伴崘?閺冄呮畱闁絼绔存禒? */
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
        target = OTA_INFO_PRIMARY; /* 娑撱倓鍞ら柈鑺ユ￥閺佸牞绱濋崗鍫濆晸娑撹鍞?*/
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
            u1_printf("ota_info_save: verify fail 0x%08lX\r\n", (unsigned long)target);
            return -1;
        }
    }
    return 0;
}

/* 鎶婁富/澶囦袱浠介兘鎿︽垚 0xFF锛岀浉褰撲簬娓呯┖鎵€鏈?OTA 璁板綍銆?
 * 鐢ㄤ簬鍒朵綔娴嬭瘯浠诲姟鍓嶅厛娓呭満锛岄伩鍏嶆棫璁板綍褰卞搷 seq 鍜岀洰鏍囨墖鍖洪€夋嫨銆?*/
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

/* ============================ W25Q64 閼奉亝绁?============================ */

/*
 * 閼奉亝绁撮敍? *   1. 鐠?ID
 *   2. 4KB 閹匡箓娅?-> 鎼存柨鍙?0xFF
 *   3. 0x0103FF 闂堢偛顕鎰晸 256B -> 閸ョ偠顕板В鏃囩窛閿涘牐绻栭弶鈩冩付鐎硅妲楅弳鎾苟鐠恒劑銆?bug閿? *   4. 鏉╂柨娲?0 闁俺绻?
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

    if ((mid != 0xEFu) || (did != 0x4017u && did != 0x4018u))
    {
        u1_printf("W25Q64 鐠囪绗夐崚?ID閿涘本顥呴弻銉﹀复缁?娓氭稓鏁?CS\r\n");
        return -1;
    }

    if (W25Q64_EraseSector(0x010000u) != W25Q64_OK)
    {
        u1_printf("閹靛洤灏幙锕傛珟婢惰精瑙r\n");
        return -1;
    }
    if (W25Q64_IsBlank(0x010000u, W25Q64_SECTOR_SIZE) != W25Q64_OK)
    {
        u1_printf("閹匡箓娅庨崥搴濈瑝閺勵垰鍙?0xFF\r\n");
        return -1;
    }

    for (i = 0; i < sizeof(buf); i++)
    {
        buf[i] = (uint8_t)i;
    }
    if (W25Q64_Write(0x0103FFu, buf, sizeof(buf)) != W25Q64_OK)
    {
        u1_printf("闂堢偛顕鎰晸婢惰精瑙r\n");
        return -1;
    }
    if (W25Q64_Verify(0x0103FFu, buf, sizeof(buf)) != W25Q64_OK)
    {
        u1_printf("闂堢偛顕鎰晸鐠囪绗夋稉鈧懛纰秗\n");
        return -1;
    }

    u1_printf("W25Q64 閼奉亝绁撮柅姘崇箖\r\n");
    return 0;
}
