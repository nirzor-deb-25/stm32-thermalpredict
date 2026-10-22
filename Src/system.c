#include "stm32_regs.h"

/* Called before C global variables are initialized. */
void SystemInit(void)
{
    /* Standalone firmware starts at the Flash base. */
    SCB_VTOR = 0x08000000UL;

    __asm volatile ("dsb" ::: "memory");
    __asm volatile ("isb" ::: "memory");
}

/* Initialization hook required by the C runtime. */
void _init(void)
{
}