#include "stm32f10x.h"
#include "my_usart.h"
#include "stdio.h"
#include "bootloader.h"
#include "struct_init.h"
OTA_t OTA_Info = {0};

/**
 * @brief  函数指针类型：app复位入口函数格式，无参无返回
 */
typedef void (*jumpapp_fun)(void);

/**
 * @brief  bootloader跳转到a分区app程序
 * @retval 无
 */
/**
 * @brief 跳转到应用程序主函数
 * @note 此函数实现从bootloader跳转到用户应用程序的功能，完整复刻了芯片冷上电流程
 * @warning 跳转前会进行安全校验，确保应用程序栈地址合法，否则会进入死循环
 */
void jump_to_app(void)
{

    //========步骤1：读取app向量表前两项========
    // 向量表偏移0：app初始栈顶msp
    uint32_t app_msp_value = *(volatile uint32_t *)stm32_a_saddr;
    // 向量表偏移+4：app复位入口reset_handler地址
    uint32_t app_reset_addr = *(volatile uint32_t *)(stm32_a_saddr + 4);

    //========步骤2：安全校验栈地址是否合法========
    // 栈顶必须落在单片机sram区间 0x20000000~0x20004fff
    if (app_msp_value < sram_begin || app_msp_value > sram_end)
    {
        // app固件损坏，栈地址非法，放弃跳转，死循环卡住
        while (1)
            ;
    }

    //========步骤3：跳转前清理，防止外设、中断残留干扰app========
    __disable_irq(); // 关闭全局中断，跳转期间禁止中断触发

    //【必须新增】清除所有NVIC中断使能 & 清除挂起中断标志
    NVIC->ICER[0] = 0xFFFFFFFF;    
    NVIC->ICPR[0] = 0xFFFFFFFF;
    
    RCC_DeInit(); // 标准库函数，复位所有外设时钟回到上电默认状态

    // 清空systick滴答定时器，清除boot延时定时器残留
    SysTick->CTRL = 0x00;
    SysTick->LOAD = 0x00;
    SysTick->VAL = 0x00;
    
    __enable_irq();                 //✅【关键修复】放开全局中断！清除PRIMASK锁死！

    //========步骤4：复刻芯片冷上电硬件流程========
    __set_MSP(app_msp_value); // 手动设置主栈指针msp，等价于上电硬件读取0地址设置sp

    jumpapp_fun app_reset_handler; // 定义函数指针，保存app复位入口地址

    app_reset_handler = (jumpapp_fun)app_reset_addr; // 复位入口地址赋值给函数指针

    app_reset_handler(); // 跳转，修改pc寄存器，进入app程序，不再返回boot

    // 防护代码，正常永远不会运行到此
    while (1)
        ;
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

