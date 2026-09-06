# On-Device Model Inference (No Laptop Server)

TODO item #2 from Stage 06: run the model directly on the ESP32, with no
laptop FastAPI server involved at all.

## Why

The Stage 06 laptop-server setup works, but it has a dependency: if the
laptop is off or unreachable, the ESP32 falls back to the plain rule (still
safe, but the model can't run at all). This approach removes that
dependency — the model itself lives on the ESP32.

## The breakdown (4 steps)

### 1. Install m2cgen

```bash
pip install m2cgen
```

`m2cgen` reads a trained scikit-learn model's internal weights and rewrites
them as plain code in another language (C, Java, Go, etc.) — no scikit-learn
or Python needed to *run* the model afterward, only to train it originally.

### 2. Export the trained model to a C function

```python
import joblib
import m2cgen as m2c

model = joblib.load("model.pkl")
feature_cols = joblib.load("feature_cols.pkl")

print("Feature order:", feature_cols)
print(m2c.export_to_c(model))
```

This printed one function:

```c
double score(double * input) {
    return -0.0910 + input[0]*-0.0612 + input[1]*0.0037 + ... ;
}
```

This is not a re-implementation or approximation — it's the *exact* same
logistic regression math scikit-learn runs internally (the same weights
seen back in Stage 02's `.coef_` inspection), just written out as a linear
expression instead of hidden inside a Python object.

### 3. Wrap it into an ESP32-ready header (`model.h`)

The raw `score()` function alone isn't enough — two things were added:

- **`sigmoid(x)`** — `score()` returns a raw number (can be any value,
  positive or negative). The model's actual output is a 0–1 probability.
  `sigmoid` is the exact conversion scikit-learn's `predict_proba()` uses
  internally for logistic regression.
- **`predictWaterProbability(soil, humidity, temp, crop)`** — builds the
  same 7-value feature array the model was trained on (soil, humidity,
  temp, `soil_deficit`, and the three one-hot crop columns), using the
  same crop thresholds from `train_and_save.py`'s `IDEAL` dict, then calls
  `score()` and passes the result through `sigmoid()`.

Feature order must match training exactly — this is written as a comment in
`model.h` as a permanent reminder, since the array position is all the C
code has to go on (no column names at runtime).

### 4. Wire it into the ESP32 firmware

- `#include "model.h"` at the top of the `.ino` file (must sit in the same
  sketch folder).
- Removed `HTTPClient`/`ArduinoJson` and the old `callPredictService()` —
  no network call needed anymore.
- Added `predictLocalModel()`, keeping the **same 3 fail-safe gates** as the
  laptop version (invalid sensor reading → rule, unknown crop → rule, low
  confidence → rule), just evaluated locally instead of over HTTP.
- WiFi is still connected — but now only for the dashboard (`/api/data`,
  `/`), not for prediction. The model runs whether or not WiFi is connected;
  only viewing the dashboard from a phone needs the network.

## Confirmed working

Flashed and running — `/api/data` now reports `"source": "model_onboard"`
when the model makes the call, same fail-safe behaviour as before when it
doesn't.

## What's given up

Retraining now means: re-run `train_and_save.py` → re-run the m2cgen export
→ regenerate `model.h` → re-flash the ESP32. Updating a laptop-side
`model.pkl` alone is no longer enough, since the model itself now lives
inside the firmware binary.
