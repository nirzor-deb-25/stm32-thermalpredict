#include "thermal_runtime.h"
#include "thermal_selftest.h"
#include "i2c.h"
#include "timing.h"
#include "uart.h"

#define SENSOR_ADDRESS 0x77U

static uint16_t calibration_t1;
static int32_t calibration_t2;
static int32_t calibration_t3;

static bool read_byte(uint8_t reg, uint8_t *value)
{
    i2c_status_t result = i2c_read_register(SENSOR_ADDRESS, reg, value);
    if (result != I2C_OK) {
        uart_write_text("# TEMP_DIAG read reg=0x");
        uart_write_hex32(reg);
        uart_write_text(" error=");
        uart_write_text(i2c_status_name(result));
        uart_write_text("\r\n");
    }
    return result == I2C_OK;
}

static bool write_byte(uint8_t reg, uint8_t value)
{
    i2c_status_t result = i2c_write_register(SENSOR_ADDRESS, reg, value);
    if (result != I2C_OK) {
        uart_write_text("# TEMP_DIAG write reg=0x");
        uart_write_hex32(reg);
        uart_write_text(" error=");
        uart_write_text(i2c_status_name(result));
        uart_write_text("\r\n");
    }
    return result == I2C_OK;
}

static int32_t signed_word(uint16_t value)
{
    return (value & 0x8000U) ?
        (int32_t)value - 65536 : (int32_t)value;
}

static bool wait_sensor_idle(void)
{
    uint32_t start = timing_cycles();

    for (;;)
    {
        uint8_t status;
        if (!read_byte(0xF3U, &status)) return false;

        /* measuring and calibration-copy flags must both clear. */
        if ((status & 0x09U) == 0U) return true;
        if (timing_has_elapsed(start, 100U)) {
            uart_write_text("# TEMP_DIAG sensor idle timeout\r\n");
            return false;
        }

        timing_delay_ms(1U);
    }
}

static bool sensor_init(void)
{
    uint8_t id;
    uint8_t calibration[6];

    if (!read_byte(0xD0U, &id) || id != 0x60U) return false;

    /* Soft reset, then wait for calibration copying to finish. */
    if (!write_byte(0xE0U, 0xB6U)) return false;
    timing_delay_ms(10U);
    if (!wait_sensor_idle()) return false;

    for (uint32_t i = 0U; i < 6U; i++)
    {
        if (!read_byte((uint8_t)(0x88U + i), &calibration[i]))
            return false;
    }

    calibration_t1 = (uint16_t)(
        (uint16_t)calibration[0] |
        ((uint16_t)calibration[1] << 8)
    );
    calibration_t2 = signed_word((uint16_t)(
        (uint16_t)calibration[2] |
        ((uint16_t)calibration[3] << 8)
    ));
    calibration_t3 = signed_word((uint16_t)(
        (uint16_t)calibration[4] |
        ((uint16_t)calibration[5] << 8)
    ));

    if (calibration_t1 == 0U || calibration_t1 == 0xFFFFU)
        return false;

    /* Humidity off, filter off; leave sensor asleep. */
    return write_byte(0xF2U, 0x00U) &&
           write_byte(0xF5U, 0x00U) &&
           write_byte(0xF4U, 0x00U);
}

static bool read_temperature(int32_t *centi_c)
{
    uint8_t bytes[3];

    /* Temperature x1, pressure off, forced measurement. */
    if (!write_byte(0xF4U, 0x21U)) return false;

    timing_delay_ms(10U);
    if (!wait_sensor_idle()) return false;

    /*
     * Forced mode returns to sleep after conversion.
     * No new conversion starts during these three reads.
     */
    for (uint32_t i = 0U; i < 3U; i++)
    {
        if (!read_byte((uint8_t)(0xFAU + i), &bytes[i]))
            return false;
    }

    uint32_t adc = ((uint32_t)bytes[0] << 12) |
                   ((uint32_t)bytes[1] << 4) |
                   ((uint32_t)bytes[2] >> 4);

    if (adc == 0x80000UL) {
        uart_write_text("# TEMP_DIAG raw temperature unavailable\r\n");

        uint8_t control = 0U;
        uint8_t status = 0U;
        bool control_ok = read_byte(0xF4U, &control);
        bool status_ok = read_byte(0xF3U, &status);

        uart_write_text("# TEMP_DIAG ctrl_meas_read_ok=");
        uart_write_u32(control_ok ? 1U : 0U);
        uart_write_text(" ctrl_meas=0x");
        uart_write_hex32(control);
        uart_write_text(" status_read_ok=");
        uart_write_u32(status_ok ? 1U : 0U);
        uart_write_text(" status=0x");
        uart_write_hex32(status);
        uart_write_text("\r\n");
        return false;
    }

    /* Bosch double-precision temperature compensation. */
    double var1 = ((double)adc / 16384.0 -
                  (double)calibration_t1 / 1024.0) *
                  (double)calibration_t2;

    double delta = (double)adc / 131072.0 -
                   (double)calibration_t1 / 8192.0;

    double var2 = delta * delta * (double)calibration_t3;
    double temperature = (var1 + var2) / 5120.0;

    /* This comparison also rejects NaN. */
    if (!(temperature >= -40.0 && temperature <= 85.0)) {
        uart_write_text("# TEMP_DIAG temperature outside range; adc=");
        uart_write_u32(adc);
        uart_write_text("\r\n");
        return false;
    }

    double scaled = temperature * 100.0;
    *centi_c = (int32_t)(
        scaled >= 0.0 ? scaled + 0.5 : scaled - 0.5
    );
    return true;
}

/*
 * Extend the 32-bit cycle counter across wraps.
 * Call frequently: at 16 MHz it wraps about every 268 seconds.
 */
static uint32_t temperature_clock_ms(void)
{
    static uint32_t previous;
    static uint64_t accumulated;
    uint32_t now = timing_cycles();
    accumulated += (uint32_t)(now - previous);
    previous = now;
    return (uint32_t)(accumulated / 16000ULL);
}

int main(void)
{
    timing_init();
    uart_init();

    if (!thermal_selftest() || !thermal_runtime_selftest()) {
        for (;;) {
            uart_write_text("# ERROR: model self-test failed\r\n");
            timing_delay_ms(1000U);
        }
    }
    i2c_init();
    timing_delay_ms(100U);

    uart_write_text("# ThermalPredict temperature stream v1\r\n");

    if (!sensor_init())
    {
        for (;;)
        {
            uart_write_text("# ERROR: BME280 initialization failed\r\n");
            timing_delay_ms(1000U);
        }
    }

    uart_write_text("# Calibration T1=");
    uart_write_u32(calibration_t1);
    uart_write_text(" T2=");
    uart_write_i32(calibration_t2);
    uart_write_text(" T3=");
    uart_write_i32(calibration_t3);
    uart_write_text("\r\n");

    uart_write_text("seq,time_ms,temperature_centi_c,status\r\n");

    thermal_runtime_init();
    uint32_t sequence = 0U;
    uint32_t next_sample = temperature_clock_ms();

    for (;;)
    {
        uint32_t now = temperature_clock_ms();

        if ((int32_t)(now - next_sample) < 0)
        {
            timing_delay_ms(1U);
            continue;
        }

        /* Timestamp marks the start of the measurement attempt. */
        uint32_t sample_time = now;
        int32_t temperature = 0;
        bool valid = read_temperature(&temperature);

        uart_write_u32(sequence++);
        uart_write_text(",");
        uart_write_u32(sample_time);
        uart_write_text(",");

        if (valid)
        {
            uart_write_i32(temperature);
            uart_write_text(",OK\r\n");
        }
        else
        {
            /* Preserve the failed attempt with an empty temperature. */
            uart_write_text(",READ_ERROR\r\n");
        }

        double forecast_c;
        bool ready = thermal_runtime_push(
            temperature, sample_time, valid, &forecast_c
        );

        if (ready) {
            /*
             * Guard the integer conversion used for UART output.
             * Forecasts themselves are not clipped.
             */
            if (forecast_c >= -21474836.0 &&
                forecast_c <= 21474836.0) {
                double scaled = forecast_c * 100.0;
                int32_t forecast_centi = (int32_t)(
                    scaled >= 0.0 ? scaled + 0.5 : scaled - 0.5
                );

                uart_write_text("# F seq=");
                uart_write_u32(sequence - 1U);
                uart_write_text(" time_ms=");
                uart_write_u32(sample_time);
                uart_write_text(" current_centi_c=");
                uart_write_i32(temperature);
                uart_write_text(" forecast_30s_centi_c=");
                uart_write_i32(forecast_centi);
                uart_write_text("\r\n");
            }
            else {
                uart_write_text("# ERROR: forecast cannot be displayed\r\n");
            }
        }
        else {
            uart_write_text("# F history_not_ready_or_invalid\r\n");
        }

        next_sample += 1000U;
        now = temperature_clock_ms();

        /* Avoid a burst of catch-up measurements after a long stall. */
        if ((int32_t)(now - next_sample) >= 0)
        {
            uart_write_text("# WARNING: measurement deadline missed\r\n");
            next_sample = now + 1000U;
        }
    }
}





