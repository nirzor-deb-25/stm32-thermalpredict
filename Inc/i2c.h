#ifndef I2C_H
#define I2C_H

#include <stdint.h>

typedef enum
{
    I2C_OK = 0,
    I2C_INVALID_ARGUMENT,
    I2C_BUSY,
    I2C_TIMEOUT,
    I2C_NACK,
    I2C_BUS_ERROR,
    I2C_ARBITRATION_LOST,
    I2C_OVERRUN
} i2c_status_t;

void i2c_init(void);

/* Pass a 7-bit address, not an address shifted left. */
i2c_status_t i2c_probe(uint8_t address);

const char *i2c_status_name(i2c_status_t status);

/* Read one register using a repeated START. */
i2c_status_t i2c_read_register(
    uint8_t address, uint8_t reg, uint8_t *value
);

i2c_status_t i2c_write_register(
    uint8_t address, uint8_t reg, uint8_t value
);

#endif

