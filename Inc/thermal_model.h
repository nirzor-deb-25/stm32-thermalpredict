#ifndef THERMAL_MODEL_H
#define THERMAL_MODEL_H

#include <stdbool.h>
#include <math.h>

#define THERMAL_MODEL_ID "fbd153500b17f938"
#define THERMAL_FEATURE_COUNT 3U
#define THERMAL_HISTORY_SAMPLES 31U
#define THERMAL_HORIZON_SECONDS 30U

/*
 * Celsius inputs.
 * Features: current-T[t-5], current-T[t-15], current-T[t-30].
 * Returns false for invalid input or a nonfinite result.
 * No clipping is applied to the forecast.
 */
static inline bool thermal_predict(
    double current_c,
    const double changes[THERMAL_FEATURE_COUNT],
    double *forecast_c
)
{
    static const double center[3] = { 0x1.6f09c847da036p-4, 0x1.1d2595b5bee59p-2, 0x1.2b4a8a5368498p-1 };
    static const double scale[3] = { 0x1.2ba467999fb1fp-1, 0x1.9b6326a374861p+0, 0x1.6866152909495p+1 };
    static const double weights[3] = { 0x1.ee95c6b7dfad5p+0, -0x1.3db7453d2c338p-2, -0x1.6f1ed02e81036p-3 };

    if (forecast_c == 0) return false;
    *forecast_c = NAN;

    if (changes == 0 || !isfinite(current_c)) return false;

    double change = 0x1.e82c6474790cbp-2;
    for (unsigned int i = 0U; i < THERMAL_FEATURE_COUNT; i++) {
        if (!isfinite(changes[i])) return false;
        change += ((changes[i] - center[i]) / scale[i]) * weights[i];
    }

    double forecast = current_c + change;
    if (!isfinite(forecast)) return false;

    *forecast_c = forecast;
    return true;
}

#endif
