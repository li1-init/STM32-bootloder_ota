#include "stm32f10x.h"
#include "my_usart2.h"

/*
 * USART2 + DMA1_Channel6 + IDLE 中断，环形缓冲
 *
 * 引脚：
 *   PA2 = USART2_TX
 *   PA3 = USART2_RX
 * 波特率：115200 8N1
 *
 * DMA 在每次 IDLE 中断后重新装载，把收到的字节推进环形缓冲；
 * XMODEM 128B 数据包整帧为 133B，1K 包整帧为 1029B，
 * 因此 DMA 单次缓冲必须大于 1029B。
 */

/* 环形缓冲 */
static volatile uint8_t  s_rx_buf[USART2_RX_SIZE];
static volatile uint16_t s_rx_head = 0u;
static volatile uint16_t s_rx_tail = 0u;

/* DMA 单次接收的目标缓冲（IDLE 中断里搬运） */
#define USART2_DMA_CHUNK 1200u
static volatile uint8_t  s_dma_buf[USART2_DMA_CHUNK];

static void rx_push(const uint8_t *p, uint16_t n)
{
    uint16_t i;
    for (i = 0; i < n; i++)
    {
        uint16_t next = (uint16_t)((s_rx_head + 1u) % USART2_RX_SIZE);
        if (next != s_rx_tail)   /* 满了丢最新 */
        {
            s_rx_buf[s_rx_head] = p[i];
            s_rx_head = next;
        }
    }
}

void usart2_init(void)
{
    USART_InitTypeDef usart = {0};
    GPIO_InitTypeDef  gpio  = {0};
    DMA_InitTypeDef   dma   = {0};
    NVIC_InitTypeDef  nvic  = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    /* PA2 = TX 复用推挽 */
    gpio.GPIO_Pin   = GPIO_Pin_2;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    /* PA3 = RX 上拉输入 */
    gpio.GPIO_Pin  = GPIO_Pin_3;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &gpio);

    usart.USART_BaudRate            = 115200;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    usart.USART_Parity              = USART_Parity_No;
    usart.USART_StopBits            = USART_StopBits_1;
    usart.USART_WordLength          = USART_WordLength_8b;
    USART_Init(USART2, &usart);

    /* DMA1 Channel6 = USART2_RX */
    DMA_DeInit(DMA1_Channel6);
    dma.DMA_PeripheralBaseAddr = (uint32_t)&USART2->DR;
    dma.DMA_MemoryBaseAddr     = (uint32_t)s_dma_buf;
    dma.DMA_DIR                = DMA_DIR_PeripheralSRC;
    dma.DMA_BufferSize         = USART2_DMA_CHUNK;
    dma.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    dma.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
    dma.DMA_Mode               = DMA_Mode_Normal;
    dma.DMA_Priority           = DMA_Priority_Medium;
    dma.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel6, &dma);

    USART_DMACmd(USART2, USART_DMAReq_Rx, ENABLE);
    DMA_Cmd(DMA1_Channel6, ENABLE);

    USART_ITConfig(USART2, USART_IT_IDLE, ENABLE);

    nvic.NVIC_IRQChannel                   = USART2_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 2;
    nvic.NVIC_IRQChannelSubPriority        = 1;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);

    USART_Cmd(USART2, ENABLE);
}

int usart2_getchar(void)
{
    int c = -1;

    if (s_rx_head != s_rx_tail)
    {
        c = (int)s_rx_buf[s_rx_tail];
        s_rx_tail = (uint16_t)((s_rx_tail + 1u) % USART2_RX_SIZE);
    }
    return c;
}

void usart2_putc(uint8_t c)
{
    while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET)
        ;
    USART_SendData(USART2, c);
}

void usart2_flush(void)
{
    s_rx_tail = s_rx_head;
}

void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_IDLE) != RESET)
    {
        uint16_t remain;
        uint16_t recv;

        /* 清 IDLE：先读 SR 再读 DR */
        (void)USART_GetFlagStatus(USART2, USART_FLAG_IDLE);
        (void)USART_ReceiveData(USART2);

        remain = (uint16_t)DMA_GetCurrDataCounter(DMA1_Channel6);
        recv   = (uint16_t)(USART2_DMA_CHUNK - remain);

        if (recv > 0u)
        {
            rx_push((const uint8_t *)s_dma_buf, recv);
        }

        /* 重新装载 DMA */
        DMA_Cmd(DMA1_Channel6, DISABLE);
        DMA_SetCurrDataCounter(DMA1_Channel6, USART2_DMA_CHUNK);
        DMA1_Channel6->CMAR = (uint32_t)s_dma_buf;
        DMA_Cmd(DMA1_Channel6, ENABLE);
    }
}
