#include "stm32f10x.h"
#include "my_usart.h"
#include "stdio.h"
#include "bootloader.h"
#include "struct_init.h"
#include "stm32f10x_flash.h"
OTA_t OTA_Info = {0};

/**
 * @brief  函数指针类型：app复位入口函数格式，无参无返回
 */
typedef void (*jumpapp_fun)(void);

/**
 * @brief  检查某个分区起始处是否是合法的程序镜像
 * @param  base: 分区起始地址
 * @param  size: 分区大小
 * @retval 1=合法 0=空白/损坏
 */
uint8_t app_is_valid(uint32_t base, uint32_t size)
{
    uint32_t msp_value  = *(volatile uint32_t *)base;
    uint32_t reset_addr = *(volatile uint32_t *)(base + 4u);

    /* 栈顶必须在 SRAM 区间，且 4 字节对齐 */
    if ((msp_value & 3u) != 0u || msp_value < sram_begin || msp_value > sram_end)
    {
        return 0;
    }
    /* Reset_Handler 必须落在本分区内，且 Thumb 位为 1 */
    if ((reset_addr & 1u) == 0u || reset_addr < base || reset_addr >= base + size)
    {
        return 0;
    }
    return 1;
}

/**
 * @brief  跳转到指定分区（APP1 / APP2 / Factory）
 * @param  base: 分区起始地址
 * @param  size: 分区大小
 * @note   关中断 -> 清 NVIC -> RCC_DeInit -> 停 SysTick -> 设 VTOR -> 设 MSP -> 开中断 -> 跳 Reset
 */
void jump_to_addr(uint32_t base, uint32_t size)
{
    uint32_t app_msp_value  = *(volatile uint32_t *)base;
    uint32_t app_reset_addr = *(volatile uint32_t *)(base + 4u);
    jumpapp_fun app_reset_handler;

    if (!app_is_valid(base, size))
    {
        u1_printf("jump_to_addr: 0x%08lX vec invalid MSP=0x%08lX RST=0x%08lX\r\n",
                  (unsigned long)base,
                  (unsigned long)app_msp_value,
                  (unsigned long)app_reset_addr);
        while (1)
            ;
    }

    u1_printf("jump_to_addr: base=0x%08lX MSP=0x%08lX RST=0x%08lX\r\n",
              (unsigned long)base,
              (unsigned long)app_msp_value,
              (unsigned long)app_reset_addr);

    /* 跳转前把串口发完，再加 10ms 空转，防止最后几个字节因为 RCC_DeInit 被截断 */
    __disable_irq();
    for (volatile uint32_t i = 0; i < 200000u; i++)
    {
    }
    __enable_irq();

    __disable_irq();

    NVIC->ICER[0] = 0xFFFFFFFF;
    NVIC->ICPR[0] = 0xFFFFFFFF;

    RCC_DeInit();

    SysTick->CTRL = 0x00;
    SysTick->LOAD = 0x00;
    SysTick->VAL  = 0x00;

    SCB->VTOR = base;
    __DSB();
    __ISB();

    __set_MSP(app_msp_value);
    __enable_irq();

    app_reset_handler = (jumpapp_fun)app_reset_addr;
    app_reset_handler();

    while (1)
        ;
}

/**
 * @brief  bootloader跳转到a分区app程序
 * @retval 无
 */
void jump_to_app(void)
{
    jump_to_addr(stm32_a_saddr, stm32_a_size);
}

/**
 * @brief  bootloader跳转到factory救砖程序
 * @retval 无
 */
void jump_to_factory(void)
{
    jump_to_addr(stm32_f_saddr, stm32_f_size);
}

extern volatile uint32_t now;
extern uint8_t usart_rx_buf[usart_rx_buf_size];

uint8_t uasrt_command(uint32_t time)
{
    uint32_t start_ms = now;
    u1_printf("\r\n请在%2d秒内输入 “w” 进入命令行\r\n", time);

    while (get_now() - start_ms < time * 1000)
    {
        if (usart_rx_buf[0] == 'w')
        {
            return 1;
        }
    }
    return 0;
}

void uasrt_command_suf(void)
{
    u1_printf("进入bootloader命令行\r\n");
    u1_printf("\r\n");
    u1_printf("[1]擦除A区\r\n");
    u1_printf("[2]串口IAP下载A区程序\r\n");
    u1_printf("[3]设置OTA版本号\r\n");
    u1_printf("[4]查询OTA版本号\r\n");
    u1_printf("[5]向外部Flash下载程序\r\n");
    u1_printf("[6]使用外部Flash内程序\r\n");
    u1_printf("[7]重启\r\n");
}

