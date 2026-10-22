from pathlib import Path
import hashlib
import json
import numpy as np
import pandas as pd

ROOT = Path(__file__).resolve().parents[1]
model_path = ROOT / "models/thermal_v2/model.json"
model_bytes = model_path.read_bytes()
model_hash = hashlib.sha256(model_bytes).hexdigest()

if model_hash != (
    "fbd153500b17f93892c94e202dd9c977"
    "f1f818e02bb65c3964e430df62ebb689"
):
    raise ValueError("Frozen model hash mismatch")

model = json.loads(model_bytes)
center = np.asarray(model["feature_center"])
scale = np.asarray(model["feature_scale"])
weights = np.asarray(model["weights"])

files = [
    ("train02_20261022_012315_710.csv", "train02"),
    ("validation01_20261022_024425_387.csv", "validation01"),
    ("test01_20261022_025631_681.csv", "test01"),
]
cases = []

for filename, session in files:
    df = pd.read_csv(ROOT / "data/raw" / filename)
    if set(df["session_id"]) != {session} or not df["status"].eq("OK").all():
        raise ValueError(f"{filename}: invalid metadata or read errors")

    t = df["temperature_centi_c"].to_numpy(dtype=float) / 100.0
    times = df["time_ms"].to_numpy(dtype=np.int64)
    seq = df["seq"].to_numpy(dtype=np.int64)

    if not np.isfinite(t).all():
        raise ValueError(f"{filename}: invalid temperatures")
    if np.any(np.diff(seq) != 1):
        raise ValueError(f"{filename}: sequence gap")
    if np.any(np.abs(np.diff(times) - 1000) > 50):
        raise ValueError(f"{filename}: timestamp gap")

    count = 0
    for i in range(30, len(t) - 30):
        temperatures = [t[i], t[i-5], t[i-15], t[i-30]]
        changes = t[i] - np.array(temperatures[1:])
        prediction = (
            t[i] + model["intercept"] +
            ((changes - center) / scale) @ weights
        )
        cases.append([*temperatures, float(prediction)])
        count += 1
    print(f"{session}: {count} reference cases")

lines = [
    '#include "thermal_selftest.h"',
    '#include "thermal_model.h"',
    '#include "uart.h"',
    '#include <math.h>',
    '',
    '/* current, 5-second lag, 15-second lag, 30-second lag, forecast */',
    'static const double references[][5] = {',
]
for case in cases:
    lines.append("    {" + ", ".join(float(v).hex() for v in case) + "},")
lines += [
    '};',
    '',
    r'''
bool thermal_selftest(void)
{
    unsigned int failures = 0U;
    unsigned int total = sizeof(references) / sizeof(references[0]);

    for (unsigned int i = 0U; i < total; i++) {
        const double *r = references[i];
        double changes[3] = {
            r[0] - r[1], r[0] - r[2], r[0] - r[3]
        };
        double forecast = NAN;

        if (!thermal_predict(r[0], changes, &forecast) ||
            !isfinite(forecast) ||
            fabs(forecast - r[4]) > 1e-9) {
            failures++;
        }
    }

    double changes[3] = {0.0, 0.0, 0.0};
    double forecast;
    if (thermal_predict(NAN, changes, &forecast)) failures++;
    if (thermal_predict(20.0, 0, &forecast)) failures++;
    if (thermal_predict(20.0, changes, 0)) failures++;
    changes[1] = INFINITY;
    if (thermal_predict(20.0, changes, &forecast)) failures++;

    uart_write_text("# THERMAL_SELFTEST model=");
    uart_write_text(THERMAL_MODEL_ID);
    uart_write_text(" cases=");
    uart_write_u32(total);
    uart_write_text(" failures=");
    uart_write_u32(failures);
    uart_write_text(" tolerance_C=1e-9\r\n");

    return failures == 0U;
}
'''
]
(ROOT / "Src/thermal_selftest.c").write_text(
    "\n".join(lines), encoding="ascii"
)
(ROOT / "Inc/thermal_selftest.h").write_text(
    "#ifndef THERMAL_SELFTEST_H\n#define THERMAL_SELFTEST_H\n"
    "#include <stdbool.h>\nbool thermal_selftest(void);\n#endif\n",
    encoding="ascii"
)
print(f"Generated {len(cases)} model parity cases plus invalid-input checks.")
print("History-buffer and live-runtime checks are still pending.")
