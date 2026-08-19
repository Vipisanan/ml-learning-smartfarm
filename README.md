# VP Farm — SmartFarm ML Track

The machine‑learning / data track for the VP Farm platform. This repo is a **staged, learning‑oriented pipeline** for turning raw sensor readings into a trustworthy irrigation model — built deliberately step by step, with data‑quality discipline first and modelling later.

> This is **not** the firmware or the platform. It's the offline data‑science workspace where sensor exports are explored, cleaned, and modelled before anything ships.

---

## Philosophy

Most farm‑ML projects fail the same way: a model is trained on a clean CSV, then falls apart on live sensor garbage in the field. So the pipeline starts by **looking at the data** — spotting impossible values and missing readings on day one — before any model is built.

Two rules the track enforces from the start:

1. **Always plot first.** A bad number is easy to miss in a table but obvious in a chart.
2. **Never leak the label.** The target column is not a feature. (The full lesson lands in Stage 01.)

---

## Setup

Requires Python 3.10+.

```bash
# 1. Create and activate a virtual environment
python -m venv smartfarm-ml

# macOS / Linux
source smartfarm-ml/bin/activate
# Windows
smartfarm-ml\Scripts\activate

# 2. Install dependencies
pip install jupyterlab pandas numpy matplotlib scikit-learn

# 3. Launch
jupyter lab
```

> `scikit-learn` is installed now even though it isn't used until Stage 02 — so the environment doesn't need touching again.

---

## Repository structure

```
smartfarm-ml/
├── irrigation_readings.csv          # sensor readings (starter dataset)
├── stage00_data_exploration.ipynb   # Stage 00 notebook
└── README.md
```

Keep the notebook and `irrigation_readings.csv` **in the same folder** — the notebook loads the CSV from its own directory.

---

## Stage 00 — Look at the data

**Goal:** understand the dataset and find its garbage. **No cleaning, no modelling yet — just look.**

Run the notebook top to bottom. Each step answers a specific question:

| Step | Question it answers |
|---|---|
| `df.head()` / `df.info()` | Are the dtypes correct? Is `timestamp` a real datetime (needed for trend features later)? Do non‑null counts reveal missing readings? |
| `df.describe()` | **Your first garbage detector.** Check every column's min/max. Soil moisture must be 0–100%; sentinel errors like `-1` or `255` are sensor faults, not real signal — if a model learns them, that's the danger. |
| Scatter plots | Impossible rows stretch the axis and jump out visually. This is *why* "always plot first" is a rule. |

### The `irrigated` column
The dataset includes an `irrigated` column. **Do not feed it to a model yet.** Deciding how to use it correctly *is* the label‑leakage lesson (Stage 01). For now, treat it as just another column to observe.

### Stage 00 deliverable
At the end of the notebook, answer three questions in your own words:

1. Which column has the most missing values? Which has impossible values?
2. Do the different crops sit in different soil‑moisture bands? (eyeball the last chart)
3. Write **3–4 sentences** describing the dataset in plain language.

Those 3–4 sentences are the real output of Stage 00 — your data reading, not the code.

---

## Roadmap

| Stage | Focus | Status |
|---|---|---|
| **00** | Data exploration — look, don't clean | ✅ current |
| **01** | ML mindset + label leakage | planned |
| **02** | First model (scikit‑learn) | planned |
| **03** | Evaluation (confusion matrix, feature importance) | planned |
| **04** | ⭐ Data cleaning — handle garbage & missing values | planned |
| **05** | Trend / time‑based features (needs proper `timestamp` dtype) | planned |

Stage 04 is starred because everything downstream depends on it — the exploration habit from Stage 00 is what makes it possible.

---

## Notes

- The starter CSV is **synthetic and intentionally contains sensor garbage** (impossible values, missing rows) so the exploration steps have something real to catch. It stands in until a real `sensor-service` export is available.
- Data cleaning happens in Stage 04 — not before. Stage 00 is observation only.

---

## Author

**Vipisanan** — VP Farm / SmartFarm Studio
