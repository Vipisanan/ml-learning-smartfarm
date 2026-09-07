# SmartFarm Local Prediction Service — Stage 06 (Tier 1)

A local FastAPI service that predicts irrigation decisions from sensor readings,
with a rules-based fail-safe. Runs on your laptop or a Raspberry Pi on the same
LAN as your ESP32 — no cloud, no internet dependency.

## Setup

```bash
python3 -m venv smartfarm-ml   # reuse your existing venv if you already made one
source smartfarm-ml/bin/activate
pip install -r requirements.txt
```

## 1. Train and save the model (run once, or whenever you retrain)

```bash
python3 train_and_save.py
```

This creates `model.pkl`, `feature_cols.pkl`, `known_crops.pkl`, `crop_ideal.pkl` in
the same folder — the server loads these at startup instead of training anything itself.

## 2. Start the server

```bash
uvicorn main:app --host 0.0.0.0 --port 8000
```

`--host 0.0.0.0` (not `127.0.0.1`) is what makes it reachable from other devices
on your LAN, like your ESP32 dashboard — not just from this machine.

## 3. Find this machine's LAN IP

```bash
# macOS
ipconfig getifaddr en0
# Linux
hostname -I
```

Your ESP32 dashboard calls: `http://<that-ip>:8000/predict`

## 4. Test it

```bash
curl -X POST http://localhost:8000/predict \
  -H "Content-Type: application/json" \
  -d '{"crop_type":"tomato","soil_moisture":35,"air_humidity":60,"temperature":32}'
```

Response:
```json
{
  "decision": "water",
  "confidence": 0.961,
  "source": "model",
  "why": "soil=35.0%, deficit=23.0 vs tomato's ideal (58%), temp=32.0°C"
}
```

## Fail-safe behaviour (by design, not a bug)

The service falls back to the crop-aware rules — never guesses with the model — when:
- Any sensor value is physically impossible (e.g. soil=255, temp=99) → returns a
  safe default of `no_water` rather than acting on garbage.
- The crop isn't one the model was trained on.
- The model's confidence is below 0.65.

Check `/health` to see which crops the model knows about.
