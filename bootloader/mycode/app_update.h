#ifndef __APP_UPDATE_H
#define __APP_UPDATE_H

#include "stm32f10x.h"

/*
 * APP 更新中标志，放在 APP2 区域的第一个 1KB 页：
 *   APP2 原范围 0x0800A800 - 0x0800FFFF
 *   其中 0x0800A800 - 0x0800ABFF 预留给更新状态，不存 APP2 固件。
 *
 * Boot:
 *   标志有效 -> APP1 正在更新，禁止跳 APP1，转 Factory。
 * Factory XMODEM:
 *   开始接收前写入标志；完整接收且 CRC 回读通过后擦除标志。
 */
#define APP_UPDATE_FLAG_ADDR   0x0800A800u
#define APP_UPDATE_MAGIC       0x41505055u  /* "APPU" */
#define APP_UPDATE_MAGIC_INV   (~APP_UPDATE_MAGIC)

int app_update_pending(void);
int app_update_begin(void);
int app_update_finish(void);

#endif
