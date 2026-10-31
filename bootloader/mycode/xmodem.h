#ifndef __XMODEM_H
#define __XMODEM_H

#include "stm32f10x.h"

/*
 * XMODEM 接收（128 字节 CRC 版）
 *   数据直接写入 W25Q64 暂存区 OTA_STAGING_ADDR
 *   成功时写 OTA 信息（flag=SET）
 *
 * @param out_len    收到的固件长度（已去掉 0x1A 填充）
 * @param out_crc32  固件整包 CRC32
 * @return 0 成功；-1 失败
 */
int xmodem_receive(uint32_t *out_len, uint32_t *out_crc32);

#endif
