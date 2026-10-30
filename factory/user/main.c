
#include "stm32f10x.h"
#include "my_usart.h"
#include "stdio.h"
#include "bootloader.h"

int main(void)
{

    all_init();
    uint16_t flash_kb = *(volatile uint16_t *)0x1FFFF7E0;
    u1_tell("FLASH_SIZE = %u KB", (unsigned)flash_kb);
    while (1)
    {
    }
}
