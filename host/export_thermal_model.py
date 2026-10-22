from pathlib import Path
import hashlib
import json
import math

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "models/thermal_v2/model.json"
data = SOURCE.read_bytes()
digest = hashlib.sha256(data).hexdigest()

EXPECTED_HASH = (
    "fbd153500b17f93892c94e202dd9c977"
    "f1f818e02bb65c3964e430df62ebb689"
)
if digest != EXPECTED_HASH:
    raise ValueError("Model differs from the evaluated frozen model")

model = json.loads(data)
if (
    model["model_type"] != "ridge_temperature_change"
    or model["feature_order"] != [
        "change_5s_c", "change_15s_c", "change_30s_c"
    ]
    or model["history_seconds"] != 30
    or model["forecast_horizon_seconds"] != 30
    or model["sample_interval_ms"] != 1000
):
    raise ValueError("Unexpected model contract")

center = model["feature_center"]
scale = model["feature_scale"]
weights = model["weights"]
intercept = model["intercept"]

if (
    any(len(v) != 3 for v in (center, scale, weights))
    or not all(math.isfinite(v)
               for v in center + scale + weights + [intercept])
    or any(v <= 0 for v in scale)
):
    raise ValueError("Invalid parameters")

def constants(values):
    return ", ".join(float(v).hex() for v in values)

header = f"""#ifndef THERMAL_MODEL_H
#define THERMAL_MODEL_H

#include <stdbool.h>
#include <math.h>

#define THERMAL_MODEL_ID "{digest[:16]}"
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
{{
    static const double center[3] = {{ {constants(center)} }};
    static const double scale[3] = {{ {constants(scale)} }};
    static const double weights[3] = {{ {constants(weights)} }};

    if (forecast_c == 0) return false;
    *forecast_c = NAN;

    if (changes == 0 || !isfinite(current_c)) return false;

    double change = {float(intercept).hex()};
    for (unsigned int i = 0U; i < THERMAL_FEATURE_COUNT; i++) {{
        if (!isfinite(changes[i])) return false;
        change += ((changes[i] - center[i]) / scale[i]) * weights[i];
    }}

    double forecast = current_c + change;
    if (!isfinite(forecast)) return false;

    *forecast_c = forecast;
    return true;
}}

#endif
"""

output = ROOT / "Inc/thermal_model.h"
output.write_text(header, encoding="ascii")

manifest = {
    "model_sha256": digest,
    "header_sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
    "model_id": digest[:16],
    "numeric_type": "double",
    "feature_order": model["feature_order"],
    "compiled_device_parity": "pending",
}
(ROOT / "models/thermal_v2/export_manifest.json").write_text(
    json.dumps(manifest, indent=2), encoding="utf-8"
)

if SOURCE.read_bytes() != data:
    raise RuntimeError("Model changed during export")

print(f"Model ID: {digest[:16]}")
print(f"Saved: {output}")
print("Model unchanged. Device parity checks are next.")
