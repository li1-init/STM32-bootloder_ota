
#include "stm32f10x.h"
#include "my_usart.h"
#include "stdio.h"
#include "struct_init.h"
#include "bootloader.h"

/*
 * 上电流程：
 *   1. APP1 有效          -> 跳 APP1
 *   2. APP1 无效          -> 跳 Factory 救砖
 *   3. APP1/Factory 都无效 -> 停在 Bootloader
 *
 * 调试开关：BOOT_DEBUG_NO_JUMP = 1 时只打印，不跳转，用来单独观察 APP1 的向量表
 */
#ifndef BOOT_DEBUG_NO_JUMP
#define BOOT_DEBUG_NO_JUMP 0
#endif

int main(void)
{
    all_init();
    u1_tell("进入bootloader区");

    /* 把 APP1 / Factory 向量表前两个字打印出来，方便判断是"没烧进去"还是"跳转失败" */
    u1_printf("APP1   @0x%08lX  MSP=0x%08lX RST=0x%08lX\r\n",
              (unsigned long)stm32_a_saddr,
              (unsigned long)(*(volatile uint32_t *)stm32_a_saddr),
              (unsigned long)(*(volatile uint32_t *)(stm32_a_saddr + 4u)));
    u1_printf("FACTORY@0x%08lX  MSP=0x%08lX RST=0x%08lX\r\n",
              (unsigned long)stm32_f_saddr,
              (unsigned long)(*(volatile uint32_t *)stm32_f_saddr),
              (unsigned long)(*(volatile uint32_t *)(stm32_f_saddr + 4u)));

    if (app_is_valid(stm32_a_saddr, stm32_a_size))
    {
        u1_tell("准备进入app1区");
#if (BOOT_DEBUG_NO_JUMP == 0)
        jump_to_addr(stm32_a_saddr, stm32_a_size);
#endif
    }
    else
    {
        u1_tell("app1区无效");
    }

    if (app_is_valid(stm32_f_saddr, stm32_f_size))
    {
        u1_tell("准备进入factory区");
#if (BOOT_DEBUG_NO_JUMP == 0)
        jump_to_addr(stm32_f_saddr, stm32_f_size);
#endif
    }
    else
    {
        u1_tell("factory区无效");
    }

    u1_tell("停留在bootloader");

    while (1)
    {
    }
}
