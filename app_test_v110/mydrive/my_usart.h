#ifndef __MY_USART_H
#define __MY_USART_H

#include "stdint.h"
#include "stdarg.h"
#include "stdio.h"
#include "string.h"

/* USART1 receive buffer (used by APP firmware download) */
#define usart_rx_buf_size 2048
#define usart_tx_buf_size 2048
#define usart_rx_buf_max  256
#define num               10

typedef struct
{
    uint8_t *start;
    uint8_t *end;
} usart_rx_ct;

typedef struct
{
    uint16_t URxCounter;
    usart_rx_ct URxDataPtr[num];
    usart_rx_ct *URxDataIN;
    usart_rx_ct *URxDataOUT;
    usart_rx_ct *URxDataEND;
} usart_data;

void all_init(void);
void UORx_PtrInit(void);
void u1_printf(const char *fmt, ...);
void u1_tell(const char *str, ...);
void my_usart1_init(void);
void USART1_IRQHandler(void);

/* Boot menu: get one received char (0 = none), flush FIFO */
uint8_t usart_boot_getchar(void);
void    usart_boot_rx_flush(void);

#endif
