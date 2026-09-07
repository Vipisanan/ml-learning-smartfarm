"""
Stage 06 — train the Stage 05 crop-aware model and serialize it to disk.

Run this once (and again whenever you retrain). The server (main.py) just loads
the saved files — it never trains anything itself, exactly like a Spring Boot
service loading a pre-built resource at startup rather than rebuilding it per request.
"""
import pandas as pd
import joblib
from sklearn.linear_model import LogisticRegression
from sklearn.model_selection import train_test_split

# --- 1. Load the same honest dataset used in Stage 05 ---
df = pd.read_csv("irrigation_honest.csv")

# --- 2. Same feature engineering as Stage 05: one-hot crop + soil_deficit interaction ---
IDEAL = {"tomato": 58, "chili": 45, "okra": 50}  # crop -> ideal soil moisture (%)

df_enc = pd.get_dummies(df, columns=["crop_type"], prefix="crop")
crop_cols = [c for c in df_enc.columns if c.startswith("crop_")]
df_enc["soil_deficit"] = df["crop_type"].map(IDEAL) - df["soil_moisture"]

FEATURE_COLS = ["soil_moisture", "air_humidity", "temperature", "soil_deficit"] + crop_cols

X = df_enc[FEATURE_COLS]
y = df_enc["irrigate"]
X_train, X_test, y_train, y_test = train_test_split(
    X, y, test_size=0.3, random_state=0, stratify=y)

# --- 3. Train ---
model = LogisticRegression(max_iter=1000).fit(X_train, y_train)
print(f"test accuracy: {model.score(X_test, y_test):.3f}")

# --- 4. Save everything the server needs to reproduce the exact feature pipeline ---
# joblib.dump is like Java serialization — writes the trained object to a binary file
# that can be loaded back later without retraining.
joblib.dump(model, "model.pkl")
joblib.dump(FEATURE_COLS, "feature_cols.pkl")   # exact column order the model expects
joblib.dump(list(IDEAL.keys()), "known_crops.pkl")  # which crops the model was trained on

# The rules baseline is plain Python data (a dict), not a trained model — no joblib needed,
# but we save it alongside so the server has ONE place to load both the model and its fail-safe.
joblib.dump(IDEAL, "crop_ideal.pkl")

print("Saved: model.pkl, feature_cols.pkl, known_crops.pkl, crop_ideal.pkl")
