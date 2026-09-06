#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "model.h"   // AUTO-GENERATED — see export_model_to_c.py. Regenerate on retrain.

// ---- Firmware identity ----
#define FW_VERSION "2.0.0"
#define FW_BUILD   "2026-09-06"
#define DEVICE_ID  "VP-Farm-zone1"

// ---- Home WiFi credentials (Station mode) — still used for the dashboard,
// but no longer needed to reach a laptop server: inference now runs on-device ----
const char* wifi_ssid = "<Your_WiFi_SSID>";
const char* wifi_pass = "<Your_WiFi_Password>";

// ---- Crop this zone is growing ----
const char* CROP_TYPE = "tomato";   // change per zone: "tomato" | "chili" | "okra"
const double CONFIDENCE_THRESHOLD = 0.65;   // same threshold as the old laptop service

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

// ---- Same physical sanity limits as before — still needed, garbage is still possible ----
bool sensorReadingValid(int soil, float tempC){
  return (soil >= 0 && soil <= 100) && (tempC >= 0 && tempC <= 60);
}

// ---- ON-DEVICE INFERENCE — no server, no network call, no laptop needed ----
// Same 3 fail-safe gates as the old laptop service, just running locally now.
void predictLocalModel(int soil, float tempC, float humidity){
  if (!sensorReadingValid(soil, tempC)){
    liveSource = "rules_local";
    liveConfidence = -1;
    classifyLocal(soil);   // garbage -> defer to the plain threshold rule, same as before
    liveReason += " [FAIL-SAFE: invalid sensor reading]";
    return;
  }

  bool knownCrop = (strcmp(CROP_TYPE,"tomato")==0 || strcmp(CROP_TYPE,"chili")==0 || strcmp(CROP_TYPE,"okra")==0);
  if (!knownCrop){
    liveSource = "rules_local";
    liveConfidence = -1;
    classifyLocal(soil);
    liveReason += " [FAIL-SAFE: unknown crop]";
    return;
  }

  // This is the actual model call — runs entirely on the ESP32's own CPU.
  double confidenceWater = predictWaterProbability(soil, humidity, tempC, CROP_TYPE);
  double confidence = (confidenceWater > 0.5) ? confidenceWater : (1.0 - confidenceWater);

  if (confidence < CONFIDENCE_THRESHOLD){
    liveSource = "rules_local";
    liveConfidence = confidence;
    classifyLocal(soil);
    liveReason += " [FAIL-SAFE: low model confidence]";
    return;
  }

  // Model is trusted — use ITS call, not the rule's.
  liveSource     = "model_onboard";
  liveConfidence = confidence;
  bool modelSaysWater = confidenceWater > 0.5;
  liveRec    = modelSaysWater ? "WATER_NOW" : "OK";
  liveBand   = modelSaysWater ? "STRESS" : "OPTIMAL";
  liveReason = "soil=" + String(soil) + "%, P(water)=" + String(confidenceWater,2) +
               ", crop=" + String(CROP_TYPE) + " [on-device model]";
}

void sampleNow(){
  liveRaw  = analogRead(soilPin);
  liveSoil = readSoilPercent(liveRaw);
  tempSensor.requestTemperatures();
  liveTemp = tempSensor.getTempCByIndex(0);

  // DEBUG: print every raw reading so we can see it live in Serial Monitor
  Serial.printf("[sample->] raw=%d  soil%%=%d  tempC=%.1f\n", liveRaw, liveSoil, liveTemp);

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
  j += "\"inference\":\"on-device\",";                 // NEW: no laptop server involved anymore
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

  // Run on-device inference periodically (not every loop tick — keep it light)
  if (now - lastPredict >= PREDICT_INTERVAL){
    lastPredict = now;
    predictLocalModel(liveSoil, liveTemp, liveHumidity);
  }
}
