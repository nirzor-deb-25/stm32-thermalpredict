#include "i2c.h"
#include "timing.h"
#include "stm32_regs.h"

#define I2C_CR1_PE       (1UL << 0)
#define I2C_CR1_START    (1UL << 8)
#define I2C_CR1_STOP     (1UL << 9)
#define I2C_CR1_ACK      (1UL << 10)
#define I2C_CR1_POS      (1UL << 11)

#define I2C_SR1_SB       (1UL << 0)
#define I2C_SR1_ADDR     (1UL << 1)
#define I2C_SR1_BERR     (1UL << 8)
#define I2C_SR1_ARLO     (1UL << 9)
#define I2C_SR1_AF       (1UL << 10)
#define I2C_SR1_OVR      (1UL << 11)

#define I2C_SR2_MSL      (1UL << 0)
#define I2C_SR2_BUSY     (1UL << 1)

#define I2C_TIMEOUT_MS   100U

/*
 * Probe diagnostic stages:
 * 0 = entry / address validation
 * 1 = waiting for an idle bus before START
 * 2 = generating START and waiting for SB
 * 3 = sending the address and waiting for ADDR
 * 4 = generating STOP and waiting for an idle bus
 * 5 = probe completed successfully
 *
 * A failed probe leaves the stage at its failure point.
 */
volatile uint32_t i2c_debug_stage = 0U;

static bool wait_gpio_levels(uint32_t mask, uint32_t expected)
{
    uint32_t start = timing_cycles();

    while ((GPIOB_IDR & mask) != expected)
    {
        if (timing_has_elapsed(start, 2U))
        {
            return false;
        }
    }

    return true;
}

static void controller_reset(void)
{
    const uint32_t scl = (1UL << 8);
    const uint32_t sda = (1UL << 9);
    const uint32_t both = scl | sda;

    /* Disable I2C before controlling the pins through GPIO. */
    I2C1_CR1 &= ~I2C_CR1_PE;

    /* Release both open-drain pins. */
    GPIOB_BSRR = both;
    GPIOB_OTYPER |= both;

    /* PB8/PB9: general-purpose output mode. */
    GPIOB_MODER =
        (GPIOB_MODER & ~((3UL << 16) | (3UL << 18))) |
        (1UL << 16) |
        (1UL << 18);

    if (!wait_gpio_levels(both, both))
    {
        goto restore_peripheral;
    }

    /* SDA high-to-low while SCL is high. */
    GPIOB_BSRR = sda << 16;

    if (!wait_gpio_levels(sda, 0U))
    {
        goto restore_peripheral;
    }

    timing_delay_ms(1U);

    /* Drive SCL low. */
    GPIOB_BSRR = scl << 16;

    if (!wait_gpio_levels(scl, 0U))
    {
        goto restore_peripheral;
    }

    timing_delay_ms(1U);

    /* Release SCL and wait for it to rise. */
    GPIOB_BSRR = scl;

    if (!wait_gpio_levels(scl, scl))
    {
        goto restore_peripheral;
    }

    timing_delay_ms(1U);

    /* Release SDA while SCL is high: STOP condition. */
    GPIOB_BSRR = sda;

    if (!wait_gpio_levels(sda, sda))
    {
        goto restore_peripheral;
    }

    timing_delay_ms(1U);

restore_peripheral:
    /* Leave both pins released. */
    GPIOB_BSRR = both;

    /* Restore alternate-function mode. */
    GPIOB_MODER =
        (GPIOB_MODER & ~((3UL << 16) | (3UL << 18))) |
        (2UL << 16) |
        (2UL << 18);

    /* Reset the I2C peripheral through SWRST. */
    I2C1_CR1 = (1UL << 15);
    __asm volatile ("dsb" ::: "memory");
    I2C1_CR1 = 0U;

    /* Restore 16 MHz APB1, 10 kHz standard-mode settings for diagnosis. */
    I2C1_CR2 = 16U;
    I2C1_OAR1 = (1UL << 14);
    I2C1_OAR2 = 0U;
    I2C1_CCR = 800U;
    I2C1_TRISE = 17U;

    I2C1_CR1 = I2C_CR1_PE | I2C_CR1_ACK;
}

void i2c_init(void)
{
    /* Enable GPIOB and I2C1 clocks. */
    RCC_AHB1ENR |= (1UL << 1);
    (void)RCC_AHB1ENR;

    RCC_APB1ENR |= (1UL << 21);
    (void)RCC_APB1ENR;

    /* Release PB8/PB9 before selecting alternate function. */
    GPIOB_BSRR = (1UL << 8) | (1UL << 9);

    /* PB8/PB9: open-drain outputs. */
    GPIOB_OTYPER |= (1UL << 8) | (1UL << 9);

    /* Medium speed. */
    GPIOB_OSPEEDR =
        (GPIOB_OSPEEDR & ~((3UL << 16) | (3UL << 18))) |
        (1UL << 16) |
        (1UL << 18);

    /*
     * Internal pull-ups enabled temporarily for diagnosis.
     * The physical I2C bus needs external pull-ups to 3.3 V.
     */
    GPIOB_PUPDR =
        (GPIOB_PUPDR & ~((3UL << 16) | (3UL << 18))) |
        (1UL << 16) |
        (1UL << 18);

    /* PB8/PB9 AF4: I2C1 SCL/SDA. */
    GPIOB_AFRH =
        (GPIOB_AFRH & ~((15UL << 0) | (15UL << 4))) |
        (4UL << 0) |
        (4UL << 4);

    /* Alternate-function mode. */
    GPIOB_MODER =
        (GPIOB_MODER & ~((3UL << 16) | (3UL << 18))) |
        (2UL << 16) |
        (2UL << 18);

    controller_reset();

    i2c_debug_stage = 0U;
}

static i2c_status_t wait_bus_free(uint32_t start)
{
    while ((I2C1_SR2 & I2C_SR2_BUSY) != 0U)
    {
        if (timing_has_elapsed(start, I2C_TIMEOUT_MS))
        {
            return I2C_BUSY;
        }
    }

    return I2C_OK;
}

static i2c_status_t wait_sr1(uint32_t flag, uint32_t start)
{
    for (;;)
    {
        uint32_t status = I2C1_SR1;

        if ((status & I2C_SR1_BERR) != 0U)
        {
            return I2C_BUS_ERROR;
        }

        if ((status & I2C_SR1_ARLO) != 0U)
        {
            return I2C_ARBITRATION_LOST;
        }

        if ((status & I2C_SR1_AF) != 0U)
        {
            return I2C_NACK;
        }

        if ((status & I2C_SR1_OVR) != 0U)
        {
            return I2C_OVERRUN;
        }

        if ((status & flag) != 0U)
        {
            return I2C_OK;
        }

        if (timing_has_elapsed(start, I2C_TIMEOUT_MS))
        {
            return I2C_TIMEOUT;
        }
    }
}

static void clear_address_flag(void)
{
    /* This read sequence clears ADDR. */
    (void)I2C1_SR1;
    (void)I2C1_SR2;
}

static i2c_status_t abort_transaction(i2c_status_t error)
{
    /*
     * Request STOP only while we still own the bus.
     * Do not generate STOP after losing arbitration.
     */
    if ((error != I2C_ARBITRATION_LOST) &&
        ((I2C1_SR2 & I2C_SR2_MSL) != 0U))
    {
        I2C1_CR1 |= I2C_CR1_STOP;

        uint32_t start = timing_cycles();

        while ((I2C1_SR2 & I2C_SR2_BUSY) != 0U)
        {
            if (timing_has_elapsed(start, 2U))
            {
                break;
            }
        }
    }

    /*
     * Restore the peripheral.
     * Keep i2c_debug_stage unchanged so the failure point
     * remains available to the application.
     */
    controller_reset();

    return error;
}

i2c_status_t i2c_probe(uint8_t address)
{
    i2c_debug_stage = 0U;

    /* Exclude reserved I2C address ranges. */
    if ((address < 0x08U) || (address > 0x77U))
    {
        return I2C_INVALID_ARGUMENT;
    }

    uint32_t start = timing_cycles();

    /* Stage 1: wait for the bus to become idle. */
    i2c_debug_stage = 1U;

    i2c_status_t result = wait_bus_free(start);

    if (result != I2C_OK)
    {
        return abort_transaction(result);
    }

    I2C1_CR1 &= ~I2C_CR1_POS;
    I2C1_CR1 |= I2C_CR1_ACK;

    /* Stage 2: generate START and wait for SB. */
    i2c_debug_stage = 2U;

    I2C1_CR1 |= I2C_CR1_START;

    result = wait_sr1(I2C_SR1_SB, start);

    if (result != I2C_OK)
    {
        return abort_transaction(result);
    }

    /* Stage 3: send the address and wait for ADDR. */
    i2c_debug_stage = 3U;

    /* Write direction: R/W = 0. */
    I2C1_DR = (uint32_t)address << 1;

    result = wait_sr1(I2C_SR1_ADDR, start);

    if (result != I2C_OK)
    {
        return abort_transaction(result);
    }

    clear_address_flag();

    /* Stage 4: generate STOP and wait for an idle bus. */
    i2c_debug_stage = 4U;

    I2C1_CR1 |= I2C_CR1_STOP;

    result = wait_bus_free(start);

    if (result != I2C_OK)
    {
        return abort_transaction(result);
    }

    /* Stage 5: successful address probe. */
    i2c_debug_stage = 5U;

    return I2C_OK;
}

const char *i2c_status_name(i2c_status_t status)
{
    switch (status)
    {
        case I2C_OK:
            return "OK";

        case I2C_INVALID_ARGUMENT:
            return "INVALID_ARGUMENT";

        case I2C_BUSY:
            return "BUSY";

        case I2C_TIMEOUT:
            return "TIMEOUT";

        case I2C_NACK:
            return "NACK";

        case I2C_BUS_ERROR:
            return "BUS_ERROR";

        case I2C_ARBITRATION_LOST:
            return "ARBITRATION_LOST";

        case I2C_OVERRUN:
            return "OVERRUN";

        default:
            return "UNKNOWN";
    }
}
i2c_status_t i2c_read_register(
    uint8_t address, uint8_t reg, uint8_t *value
)
{
    if (address < 0x08U || address > 0x77U || value == 0)
        return I2C_INVALID_ARGUMENT;

    uint32_t start = timing_cycles();
    i2c_status_t result = wait_bus_free(start);
    if (result != I2C_OK) return abort_transaction(result);

    I2C1_CR1 &= ~I2C_CR1_POS;
    I2C1_CR1 |= I2C_CR1_ACK;
    I2C1_CR1 |= I2C_CR1_START;

    result = wait_sr1(I2C_SR1_SB, start);
    if (result != I2C_OK) return abort_transaction(result);

    I2C1_DR = (uint32_t)address << 1;
    result = wait_sr1(I2C_SR1_ADDR, start);
    if (result != I2C_OK) return abort_transaction(result);
    clear_address_flag();

    /* TXE: transmit register address. BTF: wait until sent. */
    result = wait_sr1(1UL << 7, start);
    if (result != I2C_OK) return abort_transaction(result);
    I2C1_DR = reg;

    result = wait_sr1(1UL << 2, start);
    if (result != I2C_OK) return abort_transaction(result);

    I2C1_CR1 |= I2C_CR1_START;
    result = wait_sr1(I2C_SR1_SB, start);
    if (result != I2C_OK) return abort_transaction(result);

    /* One-byte receive: disable ACK before clearing ADDR. */
    I2C1_CR1 &= ~(I2C_CR1_ACK | I2C_CR1_POS);
    I2C1_DR = ((uint32_t)address << 1) | 1U;

    result = wait_sr1(I2C_SR1_ADDR, start);
    if (result != I2C_OK) return abort_transaction(result);

    uint32_t saved_primask;
    __asm volatile (
        "mrs %0, primask\n"
        "cpsid i"
        : "=r" (saved_primask) :: "memory"
    );
    clear_address_flag();
    I2C1_CR1 |= I2C_CR1_STOP;
    __asm volatile (
        "msr primask, %0" :: "r" (saved_primask) : "memory"
    );

    /* RXNE: received byte is ready. */
    result = wait_sr1(1UL << 6, start);
    if (result != I2C_OK) return abort_transaction(result);
    uint8_t received = (uint8_t)I2C1_DR;

    result = wait_bus_free(start);
    if (result != I2C_OK) return abort_transaction(result);

    I2C1_CR1 |= I2C_CR1_ACK;
    *value = received;
    return I2C_OK;
}

i2c_status_t i2c_write_register(
    uint8_t address, uint8_t reg, uint8_t value
)
{
    if (address < 0x08U || address > 0x77U)
        return I2C_INVALID_ARGUMENT;

    uint32_t start = timing_cycles();
    i2c_status_t result = wait_bus_free(start);
    if (result != I2C_OK) return abort_transaction(result);

    I2C1_CR1 |= I2C_CR1_START;
    result = wait_sr1(I2C_SR1_SB, start);
    if (result != I2C_OK) return abort_transaction(result);

    I2C1_DR = (uint32_t)address << 1;
    result = wait_sr1(I2C_SR1_ADDR, start);
    if (result != I2C_OK) return abort_transaction(result);
    clear_address_flag();

    result = wait_sr1(1UL << 7, start);
    if (result != I2C_OK) return abort_transaction(result);
    I2C1_DR = reg;

    result = wait_sr1(1UL << 7, start);
    if (result != I2C_OK) return abort_transaction(result);
    I2C1_DR = value;

    result = wait_sr1(1UL << 2, start);
    if (result != I2C_OK) return abort_transaction(result);

    I2C1_CR1 |= I2C_CR1_STOP;
    result = wait_bus_free(start);
    if (result != I2C_OK) return abort_transaction(result);

    return I2C_OK;
}
