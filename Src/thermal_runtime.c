#include "thermal_runtime.h"
#include "thermal_model.h"
#include "timing.h"
#include <math.h>

static int32_t history[THERMAL_HISTORY_SAMPLES];
static uint32_t next_index;
static uint32_t count;
static uint32_t previous_time;
static bool have_previous;

/* Available through debugger/SWD. */
volatile uint32_t thermal_forecasts;
volatile uint32_t thermal_history_resets;
volatile uint32_t thermal_compute_us;
volatile uint32_t thermal_max_compute_us;
volatile double thermal_last_forecast_c;

static void clear_history(void)
{
    next_index = 0U;
    count = 0U;
    have_previous = false;
}

void thermal_runtime_init(void)
{
    clear_history();
    thermal_forecasts = 0U;
    thermal_history_resets = 0U;
    thermal_compute_us = 0U;
    thermal_max_compute_us = 0U;
    thermal_last_forecast_c = NAN;
}

bool thermal_runtime_push(
    int32_t temperature_centi_c,
    uint32_t time_ms,
    bool valid,
    double *forecast_c
)
{
    if (forecast_c == 0) return false;
    *forecast_c = NAN;

    if (!valid ||
        temperature_centi_c < -4000 ||
        temperature_centi_c > 8500) {
        clear_history();
        thermal_history_resets++;
        thermal_last_forecast_c = NAN;
        return false;
    }

    if (have_previous) {
        uint32_t interval = time_ms - previous_time;
        if (interval < 950U || interval > 1050U) {
            clear_history();
            thermal_history_resets++;
            thermal_last_forecast_c = NAN;
        }
    }

    previous_time = time_ms;
    have_previous = true;

    uint32_t current_index = next_index;
    history[current_index] = temperature_centi_c;
    next_index = (next_index + 1U) % THERMAL_HISTORY_SAMPLES;

    if (count < THERMAL_HISTORY_SAMPLES) count++;
    if (count < THERMAL_HISTORY_SAMPLES) return false;

    uint32_t start = timing_cycles();

    double current = history[current_index] / 100.0;
    const uint32_t lags[3] = {5U, 15U, 30U};
    double changes[3];

    for (uint32_t i = 0U; i < 3U; i++) {
        uint32_t index = (
            current_index + THERMAL_HISTORY_SAMPLES - lags[i]
        ) % THERMAL_HISTORY_SAMPLES;
        changes[i] = current - history[index] / 100.0;
    }

    double prediction = NAN;
    bool success = thermal_predict(current, changes, &prediction);

    thermal_compute_us = (timing_cycles() - start) / 16U;
    if (thermal_compute_us > thermal_max_compute_us)
        thermal_max_compute_us = thermal_compute_us;

    if (!success) {
        clear_history();
        thermal_history_resets++;
        thermal_last_forecast_c = NAN;
        return false;
    }

    thermal_forecasts++;
    thermal_last_forecast_c = prediction;
    *forecast_c = prediction;
    return true;
}
