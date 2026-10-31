#include "stm32f10x.h"
#include "my_usart.h"
#include "stdio.h"
#include "string.h"
#include "bootloader.h"
#include "struct_init.h"
#include "stm32f10x_flash.h"
#include "W25Q64.h"
#include "app_update.h"

/*
 * Bootloader core:
 *   - app_is_valid / jump_to_addr
 *   - W25Q64 staging -> APP flash copy
 *   - boot_main_process
 *
 * All log strings are ASCII: avoids GBK/UTF-8 garbling when the Keil
 * project is copied between machines.
 */

typedef void (*jumpapp_fun)(void);

/* ============================================================
 *              1. Partition check and jump
 * ============================================================ */

uint8_t app_is_valid(uint32_t base, uint32_t size)
{
    uint32_t msp_value  = *(volatile uint32_t *)base;
    uint32_t reset_addr = *(volatile uint32_t *)(base + 4u);

    /* MSP: 4-byte aligned and inside SRAM */
    if ((msp_value & 3u) != 0u || msp_value < sram_begin || msp_value > sram_end)
    {
        return 0;
    }
    /* Reset_Handler: inside this partition, LSB = 1 (Thumb) */
    if ((reset_addr & 1u) == 0u || reset_addr < base || reset_addr >= base + size)
    {
        return 0;
    }
    return 1;
}

void jump_to_addr(uint32_t base, uint32_t size)
{
    uint32_t app_msp_value  = *(volatile uint32_t *)base;
    uint32_t app_reset_addr = *(volatile uint32_t *)(base + 4u);
    jumpapp_fun app_reset_handler;

    if (!app_is_valid(base, size))
    {
        u1_printf("jump_to_addr: 0x%08lX invalid\r\n", (unsigned long)base);
        while (1)
            ;
    }

    u1_printf("jump_to_addr: base=0x%08lX MSP=0x%08lX RST=0x%08lX\r\n",
              (unsigned long)base,
              (unsigned long)app_msp_value,
              (unsigned long)app_reset_addr);

    /* wait for UART to finish before RCC_DeInit */
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

void jump_to_app(void)
{
    jump_to_addr(stm32_a_saddr, stm32_a_size);
}

void jump_to_factory(void)
{
    jump_to_addr(stm32_f_saddr, stm32_f_size);
}

/* ============================================================
 *              2. W25Q64 staging -> APP flash
 * ============================================================ */

#define OTA_COPY_BUF_SIZE 1024u

/* 掉电测试钩子：>0 时在第 N 个 1KB 写完/擦完时主动复位，模拟断电 */
static volatile uint32_t s_pl_test_page = 0u;
static volatile uint32_t s_pl_test_mode = 0u; /* 1=擦除中 2=写入中 */

void boot_set_powerloss_test(uint32_t mode, uint32_t page)
{
    s_pl_test_mode = mode;
    s_pl_test_page = page;
}

int boot_copy_staging_to_app(uint32_t fw_len, uint32_t fw_crc32)
{
    uint8_t  buf[OTA_COPY_BUF_SIZE];
    uint32_t offset = 0;
    uint32_t crc;
    uint32_t addr;
    uint32_t page_no = 0u;

    if (fw_len == 0u || fw_len > stm32_a_size)
    {
        u1_printf("Boot: bad fw_len %lu\r\n", (unsigned long)fw_len);
        return -1;
    }

    /* 0. mark APP update in progress; power loss keeps Boot in FACTORY */
    if (app_update_begin() != 0)
    {
        u1_printf("Boot: set update flag failed\r\n");
        return -1;
    }

    /* 1. erase APP, 1KB per page */
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);

    for (addr = 0; addr < fw_len; addr += stm32_page_size)
    {
        if (FLASH_ErasePage(stm32_a_saddr + addr) != FLASH_COMPLETE)
        {
            FLASH_Lock();
            u1_printf("Boot: erase fail 0x%08lX\r\n", (unsigned long)(stm32_a_saddr + addr));
            return -1;
        }
        /* power-loss test hook: reset while erasing */
        if (s_pl_test_mode == 1u && ++page_no == s_pl_test_page)
        {
            u1_printf("\r\n[PL-TEST] reset during ERASE page %lu\r\n", (unsigned long)page_no);
            FLASH_Lock();
            delay_ms(20u);
            NVIC_SystemReset();
        }
    }

    /* 2. read staging, program half-word */
    page_no = 0u;
    while (offset < fw_len)
    {
        uint32_t chunk = (fw_len - offset > OTA_COPY_BUF_SIZE) ? OTA_COPY_BUF_SIZE : (fw_len - offset);
        uint32_t i;
        uint32_t word;

        W25Q64_ReadData(OTA_STAGING_ADDR + offset, buf, chunk);

        for (i = 0; i < chunk; i += 2u)
        {
            uint16_t half;
            if (i + 1u < chunk)
            {
                half = (uint16_t)(buf[i] | ((uint16_t)buf[i + 1u] << 8));
            }
            else
            {
                half = (uint16_t)(buf[i] | 0xFF00u); /* pad odd byte with 0xFF */
            }
            word = stm32_a_saddr + offset + i;
            if (FLASH_ProgramHalfWord(word, half) != FLASH_COMPLETE)
            {
                FLASH_Lock();
                u1_printf("Boot: program fail 0x%08lX\r\n", (unsigned long)word);
                return -1;
            }
            if (*(volatile uint16_t *)word != half)
            {
                FLASH_Lock();
                u1_printf("Boot: readback mismatch 0x%08lX\r\n", (unsigned long)word);
                return -1;
            }
        }

        offset += chunk;

        /* power-loss test hook: reset while programming */
        if (s_pl_test_mode == 2u && ++page_no == s_pl_test_page)
        {
            u1_printf("\r\n[PL-TEST] reset while PROGRAMMING page %lu\r\n", (unsigned long)page_no);
            FLASH_Lock();
            delay_ms(20u);
            NVIC_SystemReset();
        }
    }

    FLASH_Lock();

    /* 3. CRC again after copy */
    crc = ota_crc32((const void *)stm32_a_saddr, fw_len);
    if (crc != fw_crc32)
    {
        u1_printf("Boot: CRC mismatch calc=0x%08lX expect=0x%08lX\r\n",
                  (unsigned long)crc, (unsigned long)fw_crc32);
        return -1;
    }

    if (app_update_finish() != 0)
    {
        u1_printf("Boot: clear update flag failed\r\n");
        return -1;
    }

    return 0;
}

uint32_t boot_staging_crc32(uint32_t len)
{
    uint8_t  buf[OTA_COPY_BUF_SIZE];
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t off = 0;

    while (off < len)
    {
        uint32_t chunk = (len - off > OTA_COPY_BUF_SIZE) ? OTA_COPY_BUF_SIZE : (len - off);
        W25Q64_ReadData(OTA_STAGING_ADDR + off, buf, chunk);
        crc = ota_crc32_update(crc, buf, chunk);
        off += chunk;
    }
    return ota_crc32_final(crc);
}

uint8_t boot_main_process(void)
{
    ota_info_t info;
    uint32_t   crc;

    if (ota_info_load(&info) != 0)
    {
        u1_printf("Boot: no valid OTA info\r\n");
        return 0;
    }
    if (info.flag != ota_flag_set)
    {
        return 0;
    }

    u1_printf("Boot: OTA task len=%lu crc=0x%08lX\r\n",
              (unsigned long)info.fw_len, (unsigned long)info.fw_crc32);

    if (info.fw_len == 0u || info.fw_len > stm32_a_size)
    {
        u1_printf("Boot: bad fw_len, keep flag\r\n");
        return 0;
    }

    crc = boot_staging_crc32(info.fw_len);
    if (crc != info.fw_crc32)
    {
        u1_printf("Boot: staging CRC bad calc=0x%08lX expect=0x%08lX, APP untouched\r\n",
                  (unsigned long)crc, (unsigned long)info.fw_crc32);
        return 0;
    }

    if (boot_copy_staging_to_app(info.fw_len, info.fw_crc32) != 0)
    {
        u1_printf("Boot: copy failed, keep flag\r\n");
        return 0;
    }

    info.flag = 0;
    info.version = info.target_version;
    (void)ota_info_save(&info);
    u1_printf("Boot: upgrade done, flag cleared\r\n");
    return 1;
}
