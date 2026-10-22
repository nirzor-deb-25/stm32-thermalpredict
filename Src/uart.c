#include "uart.h"
#include "timing.h"
#include "stm32_regs.h"

#define UART_TXE           (1UL << 7)
#define UART_TX_TIMEOUT_MS  100U

void uart_init(void)
{
    /* Enable GPIOD and USART3 clocks. */
    RCC_AHB1ENR |= (1UL << 3);
    (void)RCC_AHB1ENR;

    RCC_APB1ENR |= (1UL << 18);
    (void)RCC_APB1ENR;

    /* Reset USART3. */
    RCC_APB1RSTR |= (1UL << 18);
    RCC_APB1RSTR &= ~(1UL << 18);

    /* PD8 and PD9: push-pull, medium speed, pull-up. */
    GPIOD_OTYPER &= ~((1UL << 8) | (1UL << 9));

    GPIOD_OSPEEDR =
        (GPIOD_OSPEEDR & ~((3UL << 16) | (3UL << 18))) |
        (1UL << 16) |
        (1UL << 18);

    GPIOD_PUPDR =
        (GPIOD_PUPDR & ~((3UL << 16) | (3UL << 18))) |
        (1UL << 16) |
        (1UL << 18);

    /* AF7 selects USART3. */
    GPIOD_AFRH =
        (GPIOD_AFRH & ~((15UL << 0) | (15UL << 4))) |
        (7UL << 0) |
        (7UL << 4);

    /* Alternate-function mode. */
    GPIOD_MODER =
        (GPIOD_MODER & ~((3UL << 16) | (3UL << 18))) |
        (2UL << 16) |
        (2UL << 18);

    /*
     * 16 MHz APB1, approximately 115200 baud.
     * 8 data bits, no parity, one stop bit.
     */
    USART3_CR1 = 0U;
    USART3_CR2 = 0U;
    USART3_CR3 = 0U;
    USART3_BRR = 139U;

    USART3_CR1 =
        (1UL << 13) |
        (1UL << 3) |
        (1UL << 2);
}

bool uart_write_byte(uint8_t value)
{
    uint32_t start = timing_cycles();

    while ((USART3_SR & UART_TXE) == 0U)
    {
        if (timing_has_elapsed(start, UART_TX_TIMEOUT_MS))
        {
            return false;
        }
    }

    USART3_DR = value;
    return true;
}

bool uart_write_text(const char *text)
{
    if (text == 0)
    {
        return false;
    }

    while (*text != '\0')
    {
        if (!uart_write_byte((uint8_t)*text))
        {
            return false;
        }

        text++;
    }

    return true;
}

bool uart_write_u32(uint32_t value)
{
    char digits[10];
    uint32_t count = 0U;

    /* Collect digits in reverse order, including zero. */
    do
    {
        digits[count] = (char)('0' + (value % 10U));
        count++;
        value /= 10U;
    }
    while (value != 0U);

    /* Transmit in normal order. */
    while (count != 0U)
    {
        count--;

        if (!uart_write_byte((uint8_t)digits[count]))
        {
            return false;
        }
    }

    return true;
}

bool uart_write_i32(int32_t value)
{
    uint32_t magnitude;

    if (value < 0)
    {
        if (!uart_write_byte((uint8_t)'-'))
        {
            return false;
        }

        /* Also handles INT32_MIN without signed overflow. */
        magnitude = (uint32_t)(-(value + 1)) + 1U;
    }
    else
    {
        magnitude = (uint32_t)value;
    }

    return uart_write_u32(magnitude);
}

bool uart_write_hex32(uint32_t value)
{
    static const char digits[] = "0123456789ABCDEF";

    for (uint32_t index = 0U; index < 8U; index++)
    {
        uint32_t shift = 28U - index * 4U;
        uint32_t digit = (value >> shift) & 0x0FUL;

        if (!uart_write_byte((uint8_t)digits[digit]))
        {
            return false;
        }
    }

    return true;
}