#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>   // install via Library Manager: "ArduinoJson" by Benoit Blanchon

// ---- Firmware identity ----
#define FW_VERSION "2.0.0"
#define FW_BUILD   "2026-09-06"
#define DEVICE_ID  "VP-Farm-zone1"

// ---- Home WiFi credentials (Station mode — ESP32 JOINS this network) ----
const char* wifi_ssid = "<VP>";
const char* wifi_pass = "<VP>";

// ---- Laptop running the FastAPI /predict service (Stage 06) ----
// Must be on the SAME WiFi network as the ESP32. Re-check this if your laptop's
// IP changes (e.g. after reconnecting to WiFi) — run `ipconfig getifaddr en0` on Mac.
const char* PREDICT_HOST = "192.168.8.136";
const int   PREDICT_PORT = 8000;
const unsigned long PREDICT_TIMEOUT_MS = 3000;   // don't block the loop for long if laptop is off

// ---- Crop this zone is growing — sent with every prediction request ----
const char* CROP_TYPE = "tomato";   // change per zone: "tomato" | "chili" | "okra"

// ---- Pins ----
const int soilPin = 34;
const int ledPin  = 4;
#define ONE_WIRE_BUS 15

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature tempSensor(&oneWire);

WebServer server(80);

// ---- Calibration (your values) ----
int dryValue = 2460;
int wetValue = 960;

// ---- Moisture bands (%) — used ONLY as the local fallback rule now ----
const int BAND_STRESS_LOW  = 25;
const int BAND_OPTIMAL_LOW = 40;
const int BAND_OPTIMAL_HI  = 75;

// ---- 24h ring buffer ----
const int RB_SIZE = 288;
struct Sample { uint32_t t; int8_t m; int16_t c10; };
Sample ring[RB_SIZE];
int rbHead = 0, rbCount = 0;
const unsigned long SAMPLE_INTERVAL = 5000UL;
unsigned long lastSample = 0;

// ---- Prediction call cadence — don't hit the server every loop() tick ----
const unsigned long PREDICT_INTERVAL = 30000UL;  // ask the model every 30s
unsigned long lastPredict = 0;

// ---- Live cache ----
int    liveSoil = 0, liveRaw = 0;
float  liveTemp = 0;
float  liveHumidity = 60.0;   // placeholder: add a DHT11 if you want a real humidity reading
String liveBand = "OPTIMAL", liveRec = "OK", liveReason = "";
String liveSource = "rules_local";   // "model" | "rules_failsafe" (from laptop) | "rules_local" (ESP32 own fallback)
float  liveConfidence = -1;

int readSoilPercent(int raw){
  return constrain(map(raw, dryValue, wetValue, 0, 100), 0, 100);
}

// ---- ESP32's OWN local rule — the last line of defense if the laptop is unreachable ----
void classifyLocal(int soil){
  liveSource = "rules_local";
  liveConfidence = -1;
  if (soil < BAND_STRESS_LOW){
    liveBand="STRESS"; liveRec="WATER_NOW";
    liveReason="Soil is dry ("+String(soil)+"%). [local fallback — server unreachable]";
  } else if (soil < BAND_OPTIMAL_LOW){
    liveBand="CAUTION"; liveRec="WATER_SOON";
    liveReason="Soil moisture is getting low ("+String(soil)+"%). [local fallback]";
  } else if (soil <= BAND_OPTIMAL_HI){
    liveBand="OPTIMAL"; liveRec="OK";
    liveReason="Soil moisture is in the healthy range. [local fallback]";
  } else {
    liveBand="WET"; liveRec="HOLD";
    liveReason="Soil is very wet ("+String(soil)+"%). Hold watering. [local fallback]";
  }
}

// ---- Call the laptop's /predict endpoint. Returns true if it got a valid answer. ----
bool callPredictService(int soil, float tempC, float humidity){
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  String url = "http://" + String(PREDICT_HOST) + ":" + String(PREDICT_PORT) + "/predict";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(PREDICT_TIMEOUT_MS);

  // Build the request body — same shape as main.py's SensorReading
  StaticJsonDocument<200> reqDoc;
  reqDoc["crop_type"]     = CROP_TYPE;
  reqDoc["soil_moisture"] = soil;
  reqDoc["air_humidity"]  = humidity;
  reqDoc["temperature"]   = tempC;
  String reqBody;
  serializeJson(reqDoc, reqBody);

  int httpCode = http.POST(reqBody);

  if (httpCode != 200){
    Serial.printf("predict call failed, http=%d\n", httpCode);
    http.end();
    return false;
  }

  String resp = http.getString();
  http.end();

  // Parse the response — same shape as main.py's Decision
  StaticJsonDocument<300> respDoc;
  DeserializationError err = deserializeJson(respDoc, resp);
  if (err){
    Serial.println("predict response JSON parse failed");
    return false;
  }

  const char* decision   = respDoc["decision"];
  float confidence       = respDoc["confidence"];
  const char* source     = respDoc["source"];
  const char* why        = respDoc["why"];

  liveRec        = (String(decision) == "water") ? "WATER_NOW" : "OK";
  liveBand       = (String(decision) == "water") ? "STRESS" : "OPTIMAL";
  liveReason     = String(why);
  liveSource     = String(source);     // "model" or "rules_failsafe" — comes FROM the laptop
  liveConfidence = confidence;
  return true;
}

void sampleNow(){
  liveRaw  = analogRead(soilPin);
  liveSoil = readSoilPercent(liveRaw);
  tempSensor.requestTemperatures();
  liveTemp = tempSensor.getTempCByIndex(0);

  // DEBUG: print every raw reading so we can see it live in Serial Monitor
  Serial.printf("[sample] raw=%d  soil%%=%d  tempC=%.1f\n", liveRaw, liveSoil, liveTemp);

  // LED mirrors the recommendation (advisory only), whichever source produced it
  digitalWrite(ledPin, liveRec=="WATER_NOW" ? HIGH : LOW);
}

void pushRing(){
  ring[rbHead] = { (uint32_t)(millis()/1000), (int8_t)liveSoil, (int16_t)(liveTemp*10) };
  rbHead = (rbHead+1) % RB_SIZE;
  if (rbCount < RB_SIZE) rbCount++;
}

// ---- Handlers ----
void handleData(){
  String j = "{";
  j += "\"soil\":"+String(liveSoil)+",";
  j += "\"soilRaw\":"+String(liveRaw)+",";
  j += "\"tempC\":"+String(liveTemp,1)+",";
  j += "\"band\":\""+liveBand+"\",";
  j += "\"recommendation\":\""+liveRec+"\",";
  j += "\"reason\":\""+liveReason+"\",";
  j += "\"source\":\""+liveSource+"\",";        // NEW: where this decision came from
  j += "\"confidence\":"+String(liveConfidence,2)+",";  // NEW: -1 if a rule made the call
  j += "\"ts\":"+String(millis()/1000);
  j += "}";
  server.send(200,"application/json",j);
}

void handleHistory(){
  String j = "{\"interval\":300,\"count\":"+String(rbCount)+",\"capacity\":"+String(RB_SIZE)+",\"points\":[";
  for (int i=0;i<rbCount;i++){
    int idx = (rbHead - rbCount + i + RB_SIZE) % RB_SIZE;
    if (i) j += ",";
    j += "{\"t\":"+String(ring[idx].t)+",\"m\":"+String(ring[idx].m)
        +",\"c\":"+String(ring[idx].c10/10.0,1)+"}";
  }
  j += "]}";
  server.send(200,"application/json",j);
}

void handleInfo(){
  String j = "{";
  j += "\"fw\":\""FW_VERSION"\",";
  j += "\"build\":\""FW_BUILD"\",";
  j += "\"device\":\""DEVICE_ID"\",";
  j += "\"mode\":\"Station\",";                       // CHANGED from SoftAP
  j += "\"ssid\":\""+String(wifi_ssid)+"\",";
  j += "\"ip\":\""+WiFi.localIP().toString()+"\",";   // CHANGED: station IP, not AP IP
  j += "\"predictHost\":\""+String(PREDICT_HOST)+"\",";
  j += "\"uptime\":"+String(millis()/1000);
  j += "}";
  server.send(200,"application/json",j);
}

void setup(){
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESP32 booting, Serial OK ===");
  pinMode(ledPin, OUTPUT);
  pinMode(soilPin, INPUT);
  tempSensor.begin();

  if(!LittleFS.begin(true)) Serial.println("LittleFS mount failed");

  // ---- Station mode: JOIN the home WiFi instead of creating our own network ----
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid, wifi_pass);
  Serial.print("Connecting to WiFi");
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000){
    delay(300);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED){
    Serial.println("\nConnected! ESP32 IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi FAILED — will keep retrying in loop(), using local rules until connected.");
  }

  server.on("/api/data",    handleData);
  server.on("/api/history", handleHistory);
  server.on("/api/info",    handleInfo);

  server.serveStatic("/", LittleFS, "/");
  server.on("/", []() {
    File file = LittleFS.open("/index.html", "r");
    if (!file) {
      server.send(404, "text/plain", "index.html not found");
      return;
    }
    server.streamFile(file, "text/html");
    file.close();
  });

  server.begin();

  sampleNow();
  classifyLocal(liveSoil);   // seed with local rule until the first predict call succeeds
  pushRing();
  lastSample = millis();
  lastPredict = millis();
}

void loop(){
  // Keep WiFi alive — Station mode can drop and needs reconnect logic
  if (WiFi.status() != WL_CONNECTED){
    WiFi.reconnect();
  }

  server.handleClient();

  unsigned long now = millis();
  sampleNow();   // always refresh raw sensor readings

  if (now - lastSample >= SAMPLE_INTERVAL){
    lastSample = now;
    pushRing();
  }

  // Ask the local prediction service periodically (not every loop tick — avoid hammering it)
  if (now - lastPredict >= PREDICT_INTERVAL){
    lastPredict = now;
    bool ok = callPredictService(liveSoil, liveTemp, liveHumidity);
    if (!ok){
      // DOUBLE FAIL-SAFE: laptop/service unreachable -> ESP32's own local rule takes over.
      // The dashboard always shows SOME decision, never a blank/error state.
      Serial.println("predict service unreachable -> using local rule fallback");
      classifyLocal(liveSoil);
    }
  }
}
