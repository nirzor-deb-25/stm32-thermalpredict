#include "thermal_runtime.h"
#include "thermal_model.h"
#include "uart.h"
#include <math.h>

/* Deterministic, changing input; no physical sensor required. */
static int32_t test_temperature(uint32_t index)
{
    return 2000 + (int32_t)((index * index) % 137U);
}

static bool matches_reference(uint32_t index, double actual)
{
    double current = test_temperature(index) / 100.0;
    double changes[3] = {
        current - test_temperature(index - 5U) / 100.0,
        current - test_temperature(index - 15U) / 100.0,
        current - test_temperature(index - 30U) / 100.0
    };
    double expected = NAN;

    return thermal_predict(current, changes, &expected) &&
           isfinite(actual) &&
           fabs(actual - expected) <= 1e-9;
}

bool thermal_runtime_selftest(void)
{
    unsigned int failures = 0U;
    unsigned int checks = 0U;
    double forecast = NAN;
    uint32_t timestamp = 0xFFFFB1DFUL;

    thermal_runtime_init();

    /* Warm-up, multiple ring wraps, and uint32 timestamp wrap. */
    for (uint32_t i = 0U; i < 100U; i++) {
        bool ready = thermal_runtime_push(
            test_temperature(i), timestamp, true, &forecast
        );

        if (ready != (i >= 30U))
            failures++;
        else if (ready && !matches_reference(i, forecast))
            failures++;
        else if (!ready && !isnan(forecast))
            failures++;

        checks++;
        timestamp += 1000U;
    }

    /* Invalid sample must clear the history and invalidate output. */
    if (thermal_runtime_push(2000, timestamp, false, &forecast) ||
        !isnan(forecast))
        failures++;
    checks++;
    timestamp += 1000U;

    for (uint32_t i = 100U; i <= 130U; i++) {
        bool ready = thermal_runtime_push(
            test_temperature(i), timestamp, true, &forecast
        );
        if (ready != (i == 130U))
            failures++;
        else if (ready && !matches_reference(i, forecast))
            failures++;
        checks++;
        timestamp += 1000U;
    }

    /* A long gap starts a fresh history with the current sample. */
    timestamp += 1500U;
    if (thermal_runtime_push(
            test_temperature(140U), timestamp, true, &forecast) ||
        !isnan(forecast))
        failures++;
    checks++;

    for (uint32_t i = 141U; i <= 170U; i++) {
        timestamp += 1000U;
        bool ready = thermal_runtime_push(
            test_temperature(i), timestamp, true, &forecast
        );
        if (ready != (i == 170U))
            failures++;
        else if (ready && !matches_reference(i, forecast))
            failures++;
        checks++;
    }

    /* Out-of-range measurement must also invalidate history. */
    timestamp += 1000U;
    if (thermal_runtime_push(9000, timestamp, true, &forecast) ||
        !isnan(forecast))
        failures++;
    checks++;

    uart_write_text("# THERMAL_RUNTIME_SELFTEST checks=");
    uart_write_u32(checks);
    uart_write_text(" failures=");
    uart_write_u32(failures);
    uart_write_text("\r\n");

    /* Keep synthetic test data out of live history and counters. */
    thermal_runtime_init();
    return failures == 0U;
}
