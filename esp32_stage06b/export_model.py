import joblib
import m2cgen as m2c

model = joblib.load("model.pkl")
feature_cols = joblib.load("feature_cols.pkl")

print("Feature order:", feature_cols)

c_code = m2c.export_to_c(model)
print(c_code)