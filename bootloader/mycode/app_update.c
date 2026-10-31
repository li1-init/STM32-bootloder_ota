#include "stm32f10x.h"
#include "app_update.h"

static int app_update_erase(void)
{
    if (FLASH_ErasePage(APP_UPDATE_FLAG_ADDR) != FLASH_COMPLETE)
    {
        return -1;
    }
    if (*(volatile uint32_t *)APP_UPDATE_FLAG_ADDR != 0xFFFFFFFFu)
    {
        return -1;
    }
    return 0;
}

int app_update_pending(void)
{
    uint32_t magic = *(volatile uint32_t *)APP_UPDATE_FLAG_ADDR;
    uint32_t inv   = *(volatile uint32_t *)(APP_UPDATE_FLAG_ADDR + 4u);
    return (magic == APP_UPDATE_MAGIC && inv == APP_UPDATE_MAGIC_INV) ? 1 : 0;
}

int app_update_begin(void)
{
    uint32_t magic = APP_UPDATE_MAGIC;
    uint32_t inv   = APP_UPDATE_MAGIC_INV;
    int ret = -1;

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);

    if (app_update_erase() == 0 &&
        FLASH_ProgramHalfWord(APP_UPDATE_FLAG_ADDR, (uint16_t)(magic & 0xFFFFu)) == FLASH_COMPLETE &&
        FLASH_ProgramHalfWord(APP_UPDATE_FLAG_ADDR + 2u, (uint16_t)(magic >> 16)) == FLASH_COMPLETE &&
        FLASH_ProgramHalfWord(APP_UPDATE_FLAG_ADDR + 4u, (uint16_t)(inv & 0xFFFFu)) == FLASH_COMPLETE &&
        FLASH_ProgramHalfWord(APP_UPDATE_FLAG_ADDR + 6u, (uint16_t)(inv >> 16)) == FLASH_COMPLETE)
    {
        ret = app_update_pending() ? 0 : -1;
    }

    FLASH_Lock();
    return ret;
}

int app_update_finish(void)
{
    int ret;

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
    ret = app_update_erase();
    FLASH_Lock();
    return ret;
}
