#ifndef THERMAL_RUNTIME_H
#define THERMAL_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

void thermal_runtime_init(void);
bool thermal_runtime_push(
    int32_t temperature_centi_c,
    uint32_t time_ms,
    bool valid,
    double *forecast_c
);

bool thermal_runtime_selftest(void);

#endif

