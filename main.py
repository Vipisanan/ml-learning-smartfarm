"""
Stage 06 — local prediction service.

Think of this file as a Spring Boot @RestController with one @PostMapping endpoint.
FastAPI plays the role Spring plays: routing, request validation, JSON serialization.

Run it with:  uvicorn main:app --host 0.0.0.0 --port 8000
Then your ESP32 dashboard (same LAN) calls:  POST http://<this-machine-ip>:8000/predict

CRITICAL DESIGN RULE (non-negotiable, from the roadmap):
    If the model isn't confident, OR any sensor reading is physically impossible,
    OR the crop isn't one the model was trained on -> ALWAYS fall back to the
    crop-aware rules. A broken model must never be allowed to control water.
"""
from fastapi import FastAPI
from pydantic import BaseModel, Field
import joblib
import pandas as pd

# --- Load everything once at startup (like a Spring @PostConstruct / bean init) ---
app = FastAPI(title="SmartFarm Irrigation Predictor — Tier 1 (local)")

model = joblib.load("model.pkl")
FEATURE_COLS = joblib.load("feature_cols.pkl")
KNOWN_CROPS = joblib.load("known_crops.pkl")
CROP_IDEAL = joblib.load("crop_ideal.pkl")

CONFIDENCE_THRESHOLD = 0.65   # below this, we don't trust the model's call either way

# Same physical sanity limits as Stage 04's validation gate
VALID_RANGES = {
    "soil_moisture": (0, 100),
    "air_humidity":  (0, 100),
    "temperature":   (0, 60),
}


# --- Request/response shapes (pydantic = FastAPI's version of a Java DTO + @Valid) ---
class SensorReading(BaseModel):
    crop_type: str
    soil_moisture: float
    air_humidity: float
    temperature: float


class Decision(BaseModel):
    decision: str          # "water" or "no_water"
    confidence: float      # 0.0-1.0, or -1 when rules were used (no probability to report)
    source: str            # "model" or "rules_failsafe"
    why: str                # human-readable explanation for the farmer


# --- Stage 04's validation gate, reused unchanged ---
def validate_reading(r: SensorReading) -> bool:
    """True if every sensor value is physically plausible."""
    checks = {
        "soil_moisture": r.soil_moisture,
        "air_humidity": r.air_humidity,
        "temperature": r.temperature,
    }
    for col, value in checks.items():
        lo, hi = VALID_RANGES[col]
        if not (lo <= value <= hi):
            return False
    return True


# --- The fail-safe: same crop-aware rule used throughout the roadmap ---
def rules_decision(r: SensorReading) -> tuple[str, str]:
    """Returns (decision, why) using the plain crop-aware threshold rule."""
    threshold = CROP_IDEAL.get(r.crop_type, 50)  # unknown crop -> generic fallback threshold
    if r.soil_moisture < threshold:
        return "water", f"soil {r.soil_moisture}% is below {r.crop_type}'s threshold of {threshold}%"
    return "no_water", f"soil {r.soil_moisture}% is at or above {r.crop_type}'s threshold of {threshold}%"


@app.post("/predict", response_model=Decision)
def predict(reading: SensorReading) -> Decision:
    # --- Gate 1: sensor garbage -> rules, no exceptions ---
    if not validate_reading(reading):
        return Decision(decision="no_water", confidence=-1, source="rules_failsafe",
                         why=(f"FAIL-SAFE: sensor reading is physically impossible "
                              f"(soil={reading.soil_moisture}, humidity={reading.air_humidity}, "
                              f"temp={reading.temperature}) — refusing to act until a valid "
                              f"reading arrives"))

    # --- Gate 2: unseen crop -> the model was never trained on this, don't trust it ---
    if reading.crop_type not in KNOWN_CROPS:
        decision, why = rules_decision(reading)
        return Decision(decision=decision, confidence=-1, source="rules_failsafe",
                         why=f"FAIL-SAFE (unknown crop '{reading.crop_type}'): {why}")

    # --- Build the exact feature row the model expects, in the exact column order ---
    row = {
        "soil_moisture": reading.soil_moisture,
        "air_humidity": reading.air_humidity,
        "temperature": reading.temperature,
        "soil_deficit": CROP_IDEAL[reading.crop_type] - reading.soil_moisture,
    }
    for crop in KNOWN_CROPS:
        row[f"crop_{crop}"] = 1 if reading.crop_type == crop else 0

    X = pd.DataFrame([row])[FEATURE_COLS]   # reindex to guarantee training-time column order

    proba = model.predict_proba(X)[0]        # [P(no_water), P(water)]
    confidence = float(max(proba))
    model_says_water = proba[1] > 0.5

    # --- Gate 3: model isn't confident enough -> rules ---
    if confidence < CONFIDENCE_THRESHOLD:
        decision, why = rules_decision(reading)
        return Decision(decision=decision, confidence=confidence, source="rules_failsafe",
                         why=f"FAIL-SAFE (low model confidence {confidence:.2f}): {why}")

    # --- Model is trusted: return its call, WITH an explanation ---
    decision = "water" if model_says_water else "no_water"
    why = (f"soil={reading.soil_moisture}%, deficit={row['soil_deficit']:.1f} vs "
           f"{reading.crop_type}'s ideal ({CROP_IDEAL[reading.crop_type]}%), "
           f"temp={reading.temperature}°C")
    return Decision(decision=decision, confidence=confidence, source="model", why=why)


@app.get("/health")
def health():
    return {"status": "ok", "known_crops": KNOWN_CROPS}
