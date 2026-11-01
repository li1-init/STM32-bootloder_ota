#include "stm32f10x.h"
#include "MySPI.h"

/* ============================ 引脚配置层 ============================ */

static void MySPI_W_SS(uint8_t BitValue)
{
    GPIO_WriteBit(MYSPI_GPIO_PORT, MYSPI_SS_PIN, (BitAction)BitValue);
}

static void MySPI_W_SCK(uint8_t BitValue)
{
    GPIO_WriteBit(MYSPI_GPIO_PORT, MYSPI_SCK_PIN, (BitAction)BitValue);
}

static void MySPI_W_MOSI(uint8_t BitValue)
{
    GPIO_WriteBit(MYSPI_GPIO_PORT, MYSPI_MOSI_PIN, (BitAction)BitValue);
}

static uint8_t MySPI_R_MISO(void)
{
    return GPIO_ReadInputDataBit(MYSPI_GPIO_PORT, MYSPI_MISO_PIN);
}

void MySPI_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(MYSPI_GPIO_CLK, ENABLE);

    /* CS / SCK / MOSI 推挽输出 */
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Pin   = MYSPI_SS_PIN | MYSPI_SCK_PIN | MYSPI_MOSI_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(MYSPI_GPIO_PORT, &GPIO_InitStructure);

    /* MISO 上拉输入 */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Pin  = MYSPI_MISO_PIN;
    GPIO_Init(MYSPI_GPIO_PORT, &GPIO_InitStructure);

    /* 空闲电平：CS 高、SCK 低 */
    MySPI_W_SS(1);
    MySPI_W_SCK(0);
}

/* ============================ 协议层 ============================ */

void MySPI_Start(void)
{
    MySPI_W_SS(0);
}

void MySPI_Stop(void)
{
    MySPI_W_SS(1);
}

/*
 * Mode 0：空闲低电平，上升沿采样
 *   先放 MOSI -> SCK 拉高（从机在上升沿采样）-> 读 MISO -> SCK 拉低
 * 注意：这里用的是"主机在 SCK 低电平时放数据、从机在上升沿采样"的写法，
 * 跟江协原始代码一致；W25Q64 只支持 Mode 0/Mode 3，这个写法兼容 Mode 0。
 */
uint8_t MySPI_SwapByte(uint8_t ByteSend)
{
    uint8_t i;
    uint8_t ByteReceive = 0x00;

    for (i = 0; i < 8; i++)
    {
        /* 高位先发 */
        MySPI_W_MOSI((ByteSend & 0x80) ? 1 : 0);
        ByteSend <<= 1;

        MySPI_W_SCK(1); /* 上升沿：从机采样 MOSI，主机此时读 MISO */
        if (MySPI_R_MISO())
        {
            ByteReceive |= (uint8_t)(0x80 >> i);
        }
        MySPI_W_SCK(0); /* 下降沿：从机准备下一位 */
    }

    return ByteReceive;
}
