#include "stm32f10x.h" // Device header
#include "my_usart.h"
#include "struct_init.h"

/* ============================================================
 *  USART1: DMA + IDLE receive
 *
 *  usart_rx_buf : 2048-byte ring buffer
 *  U0CB         : start/end of each frame inside usart_rx_buf
 *  rx_fifo      : 16-byte FIFO for Boot menu
 *  rx_ota_buf   : 1.5KB byte stream for OTA frame parser
 *                 (raw bytes, parser does its own framing)
 * ============================================================ */

#define RX_FIFO_SIZE   16
#define RX_OTA_SIZE    1536

uint8_t usart_rx_buf[usart_rx_buf_size];

usart_data U0CB;

static volatile uint8_t  rx_fifo[RX_FIFO_SIZE];
static volatile uint16_t rx_fifo_head = 0u;
static volatile uint16_t rx_fifo_tail = 0u;

static volatile uint8_t  rx_ota[RX_OTA_SIZE];
static volatile uint16_t rx_ota_head = 0u;
static volatile uint16_t rx_ota_tail = 0u;


static void rx_fifo_push(const uint8_t *buf, uint16_t len)
{
    uint16_t i;

    for (i = 0u; i < len; i++)
    {
        uint16_t next = (uint16_t)((rx_fifo_head + 1u) % RX_FIFO_SIZE);
        if (next != rx_fifo_tail) /* full: drop newest */
        {
            rx_fifo[rx_fifo_head] = buf[i];
            rx_fifo_head = next;
        }
    }
}

static void rx_ota_push(const uint8_t *buf, uint16_t len)
{
    uint16_t i;

    for (i = 0u; i < len; i++)
    {
        uint16_t next = (uint16_t)((rx_ota_head + 1u) % RX_OTA_SIZE);
        if (next != rx_ota_tail) /* full: drop newest, parser will NACK */
        {
            rx_ota[rx_ota_head] = buf[i];
            rx_ota_head = next;
        }
    }
}

uint8_t usart_boot_getchar(void)
{
    uint8_t c = 0u;

    if (rx_fifo_head != rx_fifo_tail)
    {
        c = rx_fifo[rx_fifo_tail];
        rx_fifo_tail = (uint16_t)((rx_fifo_tail + 1u) % RX_FIFO_SIZE);
    }
    return c;
}

void usart_boot_rx_flush(void)
{
    rx_fifo_tail = rx_fifo_head;
}

uint8_t usart_ota_getchar(void)
{
    uint8_t c = 0u;

    if (rx_ota_head != rx_ota_tail)
    {
        c = rx_ota[rx_ota_tail];
        rx_ota_tail = (uint16_t)((rx_ota_tail + 1u) % RX_OTA_SIZE);
    }
    return c;
}

void usart_ota_flush(void)
{
    rx_ota_tail = rx_ota_head;
}

/* 杩愯鏃舵敼娉㈢壒鐜囷細BRR = USARTDIV * 16锛堥珮 12 浣嶆暣鏁帮紝浣?4 浣嶅皬鏁帮級
 * 72MHz锛?15200 -> 10000(0x2710)锛?21600 -> 1250(0x4E2)锛?60800 -> 2500(0x9C4)
 */
void usart_set_baud(uint32_t baud)
{
    uint32_t pclk2 = SystemCoreClock;                 /* USART1 鍦?APB2 */
    uint32_t brr   = (pclk2 * 16u + baud / 2u) / baud; /* 鍥涜垗浜斿叆鍒?1/16 */

    USART1->CR1 &= (uint16_t)~USART_CR1_UE;           /* 鍏堝叧锛岄伩鍏嶆敼 BRR 鏃朵贡鐮?*/
    USART1->BRR  = (uint16_t)brr;
    USART1->CR1 |= USART_CR1_UE;
}

void UORx_PtrInit(void)
{
    U0CB.URxDataIN = &U0CB.URxDataPtr[0];
    U0CB.URxDataOUT = &U0CB.URxDataPtr[0];
    U0CB.URxDataEND = &U0CB.URxDataPtr[num - 1];
    U0CB.URxDataIN->start = usart_rx_buf;
    U0CB.URxCounter = 0;
}

void my_usart1_init(void)
{
    USART_InitTypeDef usart_struct = {0};
    GPIO_InitTypeDef  DPA = {0};
    DMA_InitTypeDef   dma_rx_struct = {0};
    NVIC_InitTypeDef  nvic_struct = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);

    usart_struct.USART_BaudRate = UART_BAUD_DEBUG;
    usart_struct.USART_HardwareFlowControl = DISABLE;
    usart_struct.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    usart_struct.USART_Parity = USART_Parity_No;
    usart_struct.USART_StopBits = USART_StopBits_1;
    usart_struct.USART_WordLength = USART_WordLength_8b;
    USART_Init(USART1, &usart_struct);

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    DPA.GPIO_Pin = GPIO_Pin_9;
    DPA.GPIO_Mode = GPIO_Mode_AF_PP;
    DPA.GPIO_Speed = GPIO_Speed_10MHz;
    GPIO_Init(GPIOA, &DPA);
    DPA.GPIO_Mode = GPIO_Mode_IPU;
    DPA.GPIO_Pin = GPIO_Pin_10;
    GPIO_Init(GPIOA, &DPA);

    /* DMA1 Channel5 = USART1_RX */
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    dma_rx_struct.DMA_PeripheralBaseAddr = (uint32_t)&USART1->DR;
    dma_rx_struct.DMA_MemoryBaseAddr     = (uint32_t)usart_rx_buf;
    dma_rx_struct.DMA_DIR                = DMA_DIR_PeripheralSRC;
    dma_rx_struct.DMA_BufferSize         = usart_rx_buf_max + 1;
    dma_rx_struct.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    dma_rx_struct.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    dma_rx_struct.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    dma_rx_struct.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
    dma_rx_struct.DMA_Mode               = DMA_Mode_Normal;
    dma_rx_struct.DMA_Priority           = DMA_Priority_Medium;
    dma_rx_struct.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel5, &dma_rx_struct);

    USART_DMACmd(USART1, USART_DMAReq_Rx, ENABLE);
    DMA_Cmd(DMA1_Channel5, ENABLE);

    USART_ITConfig(USART1, USART_IT_IDLE, ENABLE);

    nvic_struct.NVIC_IRQChannel = USART1_IRQn;
    nvic_struct.NVIC_IRQChannelPreemptionPriority = 1;
    nvic_struct.NVIC_IRQChannelSubPriority = 1;
    nvic_struct.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic_struct);

    UORx_PtrInit();

    USART_Cmd(USART1, ENABLE);
}

void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_IDLE) != RESET)
    {
        uint16_t remain;
        uint16_t recv_len;
        uint16_t start;
        uint16_t k;
        uint16_t n;
        uint8_t  frame[32];

        (void)USART_GetFlagStatus(USART1, USART_FLAG_IDLE);
        (void)USART_ReceiveData(USART1);

        remain   = DMA_GetCurrDataCounter(DMA1_Channel5);
        recv_len = (uint16_t)((usart_rx_buf_max + 1) - remain);

        if (recv_len == 0u)
        {
            DMA_Cmd(DMA1_Channel5, DISABLE);
            DMA_SetCurrDataCounter(DMA1_Channel5, usart_rx_buf_max + 1);
            DMA1_Channel5->CMAR = (uint32_t)U0CB.URxDataIN->start;
            DMA_Cmd(DMA1_Channel5, ENABLE);
            return;
        }

        start = (uint16_t)((uint32_t)U0CB.URxDataIN->start - (uint32_t)usart_rx_buf);

        U0CB.URxCounter += recv_len;
        U0CB.URxDataIN->end = &usart_rx_buf[U0CB.URxCounter - 1];

        /* OTA 瀛楄妭娴侊細鏁村抚 push */
        {
            uint16_t off = 0u;
            uint8_t  chunk[64];
            while (off < recv_len)
            {
                uint16_t m = (uint16_t)((recv_len - off) > sizeof(chunk) ? sizeof(chunk) : (recv_len - off));
                for (k = 0u; k < m; k++)
                {
                    chunk[k] = usart_rx_buf[(start + off + k) % usart_rx_buf_size];
                }
                rx_ota_push(chunk, m);
                off = (uint16_t)(off + m);
            }
        }

        /* Boot 鑿滃崟 FIFO锛氭渶澶?32 瀛楄妭 */
        n = (recv_len > sizeof(frame)) ? (uint16_t)sizeof(frame) : recv_len;
        for (k = 0u; k < n; k++)
        {
            frame[k] = usart_rx_buf[(start + k) % usart_rx_buf_size];
        }
        rx_fifo_push(frame, n);

        for (k = 0u; k < n; k++)
        {
            if (frame[k] == (uint8_t)'w')
            {
                key_w_pressed = 1u;
                break;
            }
        }

        U0CB.URxDataIN++;
        if (U0CB.URxDataIN == U0CB.URxDataEND)
        {
            U0CB.URxDataIN = &U0CB.URxDataPtr[0];
        }

        if (usart_rx_buf_size - U0CB.URxCounter >= usart_rx_buf_max)
        {
            U0CB.URxDataIN->start = &usart_rx_buf[U0CB.URxCounter];
        }
        else
        {
            U0CB.URxDataIN->start = usart_rx_buf;
            U0CB.URxCounter = 0;
        }

        DMA_Cmd(DMA1_Channel5, DISABLE);
        DMA_SetCurrDataCounter(DMA1_Channel5, usart_rx_buf_max + 1);
        DMA1_Channel5->CMAR = (uint32_t)U0CB.URxDataIN->start;
        DMA_Cmd(DMA1_Channel5, ENABLE);
    }
}

uint8_t usart_tx_buf[usart_tx_buf_size];

void u1_putc(uint8_t c)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET)
        ;
    USART_SendData(USART1, c);
}

void u1_printf(const char *fmt, ...)
{
    uint16_t i = 0;
    va_list  listdata;

    va_start(listdata, fmt);
    vsprintf((char *)usart_tx_buf, fmt, listdata);
    va_end(listdata);

    for (i = 0; i < strlen((char *)usart_tx_buf); i++)
    {
        u1_putc(usart_tx_buf[i]);
    }
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET)
        ;
    USART_ClearFlag(USART1, USART_FLAG_TC);
}

void u1_tell(const char *str, ...)
{
    u1_printf(str);
    u1_printf("\r\n");
    u1_printf("/**********************************************/");
    u1_printf("\r\n");
}

void all_init(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    my_usart1_init();
    tim3_init();
}
