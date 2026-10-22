from pathlib import Path
import hashlib
import json
import numpy as np
import pandas as pd

ROOT = Path(__file__).resolve().parents[1]
RAW = ROOT / "data" / "raw"
OUT = ROOT / "models" / "thermal_v1"

FILES = {
    "training": "train02_20261022_012315_710.csv",
    "validation": "validation01_20261022_024425_387.csv",
}
FEATURES = [
    "temperature_c",
    "change_5s_c",
    "change_15s_c",
    "change_30s_c",
]
HISTORY = 30
HORIZON = 30
ALPHA = 1.0

def load_examples(filename, session):
    path = RAW / filename
    df = pd.read_csv(path)

    if len(df) <= HISTORY + HORIZON:
        raise ValueError(f"{filename}: insufficient data")
    if set(df["session_id"]) != {session}:
        raise ValueError(f"{filename}: unexpected session")
    if not df["status"].eq("OK").all():
        raise ValueError(f"{filename}: sensor read errors")

    columns = ["seq", "time_ms", "temperature_centi_c"]
    values = df[columns].to_numpy(dtype=np.float64)
    if not np.isfinite(values).all():
        raise ValueError(f"{filename}: nonfinite data")
    if not np.equal(values, np.floor(values)).all():
        raise ValueError(f"{filename}: noninteger data")

    seq = values[:, 0].astype(np.int64)
    time = values[:, 1].astype(np.int64)
    temperature = values[:, 2] / 100.0

    if np.any(np.diff(seq) != 1):
        raise ValueError(f"{filename}: sequence gap")
    if np.any(np.abs(np.diff(time) - 1000) > 50):
        raise ValueError(f"{filename}: sampling gap")
    if np.any((temperature < -40) | (temperature > 85)):
        raise ValueError(f"{filename}: temperature outside range")

    indices = np.arange(HISTORY, len(df) - HORIZON)

    for offset in (5, 15, 30):
        if np.any(np.abs(
            time[indices] - time[indices - offset] - offset * 1000
        ) > 50):
            raise ValueError(f"{filename}: history timing mismatch")

    if np.any(np.abs(
        time[indices + HORIZON] - time[indices] - 30000
    ) > 50):
        raise ValueError(f"{filename}: forecast timing mismatch")

    current = temperature[indices]
    X = np.column_stack([
        current,
        current - temperature[indices - 5],
        current - temperature[indices - 15],
        current - temperature[indices - 30],
    ])
    target = temperature[indices + HORIZON]

    return X, current, target, time[indices], path

train = load_examples(FILES["training"], "train02")
validation = load_examples(FILES["validation"], "validation01")

X, current, target, _, _ = train

# Fit scaling and model exclusively on training data.
center = X.mean(axis=0)
scale = np.maximum(X.std(axis=0, ddof=0), 0.01)
Z = (X - center) / scale

# Predict the future change, then add the current temperature.
change = target - current
intercept = float(change.mean())
A = np.vstack([Z, np.sqrt(ALPHA) * np.eye(len(FEATURES))])
b = np.concatenate([change - intercept, np.zeros(len(FEATURES))])
weights = np.linalg.lstsq(A, b, rcond=None)[0]

model = {
    "schema_version": 1,
    "model_type": "ridge_temperature_change",
    "sample_interval_ms": 1000,
    "history_seconds": HISTORY,
    "forecast_horizon_seconds": HORIZON,
    "feature_order": FEATURES,
    "feature_center": center.tolist(),
    "feature_scale": scale.tolist(),
    "weights": weights.tolist(),
    "intercept": intercept,
    "ridge_alpha": ALPHA,
    "prediction": "current_c + intercept + dot((features-center)/scale, weights)",
    "training_session": "train02",
    "validation_session": "validation01",
    "source_sha256": {
        item[4].name: hashlib.sha256(item[4].read_bytes()).hexdigest()
        for item in (train, validation)
    },
}

OUT.mkdir(parents=True, exist_ok=False)
(OUT / "model.json").write_text(
    json.dumps(model, indent=2), encoding="utf-8"
)

reports = []
for split, item in (("training", train), ("validation", validation)):
    X, current, target, times, _ = item
    predicted = current + intercept + ((X - center) / scale) @ weights

    for name, prediction in (
        ("persistence", current),
        ("ridge", predicted),
    ):
        error = prediction - target
        reports.append({
            "split": split,
            "model": name,
            "examples": len(target),
            "MAE_C": float(np.mean(np.abs(error))),
            "RMSE_C": float(np.sqrt(np.mean(error ** 2))),
            "max_abs_error_C": float(np.max(np.abs(error))),
        })

    pd.DataFrame({
        "time_ms": times,
        "current_c": current,
        "actual_30s_ahead_c": target,
        "persistence_c": current,
        "ridge_c": predicted,
    }).to_csv(OUT / f"{split}_predictions.csv", index=False)

report = pd.DataFrame(reports)
report.to_csv(OUT / "metrics.csv", index=False)
print(report.round(3).to_string(index=False))
print(f"\nSaved: {OUT}")
print("No final test evaluation performed.")
print("Examples overlap; each split contains only one recording.")
