#ifndef __FACTORY_XMODEM_H
#define __FACTORY_XMODEM_H

#include "stm32f10x.h"

/*
 * Factory 救砖用 XMODEM 接收（128B/1K CRC）
 * 直接写入 APP 分区片内 Flash，不依赖 W25Q64
 *
 * @param out_len    收到的固件长度
 * @param out_crc32  固件 CRC32（已回读校验）
 * @return 0 成功；-1 失败
 */
int factory_xmodem_receive(uint32_t *out_len, uint32_t *out_crc32);

#endif
