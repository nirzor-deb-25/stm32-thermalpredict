from pathlib import Path
import hashlib
import json
import numpy as np
import pandas as pd

ROOT = Path(__file__).resolve().parents[1]
MODEL = ROOT / "models/thermal_v2/model.json"
SOURCE = ROOT / "data/raw/test01_20261022_025631_681.csv"
OUT = ROOT / "docs/evaluation"

model_bytes = MODEL.read_bytes()
model_hash = hashlib.sha256(model_bytes).hexdigest()
model = json.loads(model_bytes)

expected_features = [
    "change_5s_c", "change_15s_c", "change_30s_c"
]
if (
    model["model_type"] != "ridge_temperature_change"
    or model["feature_order"] != expected_features
    or model["history_seconds"] != 30
    or model["forecast_horizon_seconds"] != 30
    or model["sample_interval_ms"] != 1000
):
    raise ValueError("Unexpected model contract")

center = np.asarray(model["feature_center"], dtype=float)
scale = np.asarray(model["feature_scale"], dtype=float)
weights = np.asarray(model["weights"], dtype=float)
intercept = float(model["intercept"])

if (
    any(a.shape != (3,) for a in (center, scale, weights))
    or not all(np.isfinite(a).all() for a in (center, scale, weights))
    or not np.isfinite(intercept)
    or np.any(scale <= 0)
):
    raise ValueError("Invalid model parameters")

df = pd.read_csv(SOURCE)
if (
    len(df) <= 60
    or set(df["session_id"]) != {"test01"}
    or not df["status"].eq("OK").all()
):
    raise ValueError("Invalid test recording")

values = df[
    ["seq", "time_ms", "temperature_centi_c"]
].to_numpy(dtype=float)

if (
    not np.isfinite(values).all()
    or not np.equal(values, np.floor(values)).all()
):
    raise ValueError("Invalid numeric data")

seq = values[:, 0].astype(np.int64)
times = values[:, 1].astype(np.int64)
temperature = values[:, 2] / 100.0

if np.any(np.diff(seq) != 1):
    raise ValueError("Sequence gap")
if np.any(np.abs(np.diff(times) - 1000) > 50):
    raise ValueError("Sampling gap")
if np.any((temperature < -40) | (temperature > 85)):
    raise ValueError("Temperature outside sensor range")

indices = np.arange(30, len(df) - 30)
for offset in (5, 15, 30):
    if np.any(np.abs(
        times[indices] - times[indices - offset] - offset * 1000
    ) > 50):
        raise ValueError("History timing mismatch")

if np.any(np.abs(
    times[indices + 30] - times[indices] - 30000
) > 50):
    raise ValueError("Forecast timing mismatch")

current = temperature[indices]
features = np.column_stack([
    current - temperature[indices - offset]
    for offset in (5, 15, 30)
])
actual = temperature[indices + 30]
predicted = current + intercept + ((features - center) / scale) @ weights

results = []
for name, prediction in (
    ("persistence", current),
    ("ridge_v2", predicted),
):
    error = prediction - actual
    results.append({
        "model": name,
        "examples": len(actual),
        "MAE_C": float(np.mean(np.abs(error))),
        "RMSE_C": float(np.sqrt(np.mean(error ** 2))),
        "max_abs_error_C": float(np.max(np.abs(error))),
    })

OUT.mkdir(parents=True, exist_ok=True)
pd.DataFrame({
    "time_ms": times[indices],
    "current_c": current,
    "actual_30s_ahead_c": actual,
    "persistence_c": current,
    "ridge_v2_c": predicted,
}).to_csv(OUT / "test_predictions.csv", index=False)

report = {
    "model_sha256": model_hash,
    "test_source": SOURCE.name,
    "test_source_sha256": hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
    "results": results,
    "limitations": [
        "One test recording with two planned warming periods.",
        "Forecast examples overlap and are correlated.",
        "Hand-warming demonstration, not industrial validation.",
    ],
}
(OUT / "test_report.json").write_text(
    json.dumps(report, indent=2), encoding="utf-8"
)

if MODEL.read_bytes() != model_bytes:
    raise RuntimeError("Model changed during evaluation")

print(pd.DataFrame(results).round(3).to_string(index=False))
print(f"\nModel SHA256: {model_hash}")
print(f"Reports saved: {OUT}")
print("Frozen model evaluated: no retraining or parameter adjustment.")
