/*
 * Pengering Gabah — ESP32
 *
 * Library Arduino IDE:
 *   - DHT sensor library (Adafruit)
 *   - Adafruit Unified Sensor
 *
 * Wiring (ESP32 DevKit):
 *   DHT22          VCC->3V3  DATA->GPIO4   GND->GND
 *   Soil moisture  VCC->3V3  AOUT->GPIO34  GND->GND   (pakai 3.3V, jangan 5V)
 *   LDR 4-pin      VCC->3V3  AO->GPIO35    DO->GPIO32  GND->GND
 *   Relay IN       GPIO26    VCC sesuai modul  GND->GND
 *                  Lampu pijar lewat NO/COM relay (sisi AC terpisah dari ESP)
 *
 * Android dan ESP harus satu WiFi. Serial Monitor 115200 menampilkan IP.
 * Jika WiFi gagal, ESP membuka hotspot: PengeringGabah / 12345678  (IP 192.168.4.1)
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <DHT.h>
#include <Preferences.h>

// ====== UBAH SESUAI WiFi RUMAH ======
const char *WIFI_SSID = "NAMA_WIFI";
const char *WIFI_PASSWORD = "PASSWORD_WIFI";

const char *AP_SSID = "PengeringGabah";
const char *AP_PASSWORD = "12345678";
const char *MDNS_NAME = "pengering-gabah";

const int PIN_DHT = 4;
const int PIN_MOISTURE = 34;
const int PIN_LDR_AO = 35;
const int PIN_LDR_DO = 32;
const int PIN_RELAY = 26;

const bool RELAY_ACTIVE_LOW = true;

DHT dht(PIN_DHT, DHT22);
WebServer server(80);
Preferences prefs;

float temperatureC = NAN;
float humidityRh = NAN;
int moistureRaw = 0;
float grainMoisture = 0;
int lightRaw = 0;
float lightPercent = 0;
bool isDark = false;

bool heaterOn = false;
bool autoMode = true;
float targetMoisture = 14.0f;
float maxTemperature = 50.0f;
int dryRaw = 3000;
int wetRaw = 1400;

unsigned long lastSensorMs = 0;
const unsigned long SENSOR_INTERVAL_MS = 2000;

void setHeater(bool on) {
  heaterOn = on;
  digitalWrite(PIN_RELAY, RELAY_ACTIVE_LOW ? (on ? LOW : HIGH) : (on ? HIGH : LOW));
}

float mapRange(int raw, int fromHigh, int fromLow) {
  if (fromHigh == fromLow) {
    return 0;
  }
  float value = (float)(fromHigh - raw) * 100.0f / (float)(fromHigh - fromLow);
  if (value < 0) value = 0;
  if (value > 100) value = 100;
  return value;
}

void sendCors() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void sendJson(int code, const String &body) {
  sendCors();
  server.send(code, "application/json; charset=utf-8", body);
}

String jsonStatus() {
  char buf[768];
  snprintf(
      buf,
      sizeof(buf),
      "{"
      "\"ok\":true,"
      "\"temperature\":%.1f,"
      "\"humidity\":%.1f,"
      "\"grainMoisture\":%.1f,"
      "\"moistureRaw\":%d,"
      "\"lightLevel\":%.1f,"
      "\"lightRaw\":%d,"
      "\"isDark\":%s,"
      "\"heaterOn\":%s,"
      "\"mode\":\"%s\","
      "\"targetMoisture\":%.1f,"
      "\"maxTemperature\":%.1f,"
      "\"dryRaw\":%d,"
      "\"wetRaw\":%d,"
      "\"wifiRssi\":%d,"
      "\"ip\":\"%s\","
      "\"uptime\":%lu"
      "}",
      isnan(temperatureC) ? 0.0 : temperatureC,
      isnan(humidityRh) ? 0.0 : humidityRh,
      grainMoisture,
      moistureRaw,
      lightPercent,
      lightRaw,
      isDark ? "true" : "false",
      heaterOn ? "true" : "false",
      autoMode ? "auto" : "manual",
      targetMoisture,
      maxTemperature,
      dryRaw,
      wetRaw,
      WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0,
      WiFi.getMode() == WIFI_AP ? WiFi.softAPIP().toString().c_str() : WiFi.localIP().toString().c_str(),
      millis() / 1000UL);
  return String(buf);
}

float extractNumber(const String &body, const char *key, float fallback) {
  String token = String("\"") + key + "\":";
  int idx = body.indexOf(token);
  if (idx < 0) {
    return fallback;
  }
  idx += token.length();
  while (idx < (int)body.length() && (body[idx] == ' ')) {
    idx++;
  }
  return body.substring(idx).toFloat();
}

bool extractBool(const String &body, const char *key, bool fallback) {
  String token = String("\"") + key + "\":";
  int idx = body.indexOf(token);
  if (idx < 0) {
    return fallback;
  }
  idx += token.length();
  String rest = body.substring(idx);
  rest.trim();
  if (rest.startsWith("true")) return true;
  if (rest.startsWith("false")) return false;
  return fallback;
}

void applyAutoControl() {
  if (!autoMode) {
    return;
  }

  bool overheat = !isnan(temperatureC) && temperatureC >= maxTemperature;
  bool dryEnough = grainMoisture <= targetMoisture;
  bool stillWet = grainMoisture > (targetMoisture + 1.5f);

  if (overheat || dryEnough) {
    setHeater(false);
  } else if (stillWet) {
    setHeater(true);
  }
}

void readSensors() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) {
    temperatureC = t;
  }
  if (!isnan(h)) {
    humidityRh = h;
  }

  moistureRaw = analogRead(PIN_MOISTURE);
  grainMoisture = mapRange(moistureRaw, dryRaw, wetRaw);

  lightRaw = analogRead(PIN_LDR_AO);
  // Modul LDR 4-pin: AO naik saat lebih terang. DO biasanya HIGH saat gelap.
  lightPercent = mapRange(lightRaw, 0, 4095);
  isDark = digitalRead(PIN_LDR_DO) == HIGH;

  applyAutoControl();
}

void handleOptions() {
  sendCors();
  server.send(204);
}

void handleStatus() {
  sendJson(200, jsonStatus());
}

void handleControl() {
  String body = server.arg("plain");

  if (body.indexOf("\"mode\"") >= 0) {
    if (body.indexOf("\"manual\"") >= 0) {
      autoMode = false;
    } else if (body.indexOf("\"auto\"") >= 0) {
      autoMode = true;
    }
  }

  if (body.indexOf("\"heaterOn\"") >= 0) {
    autoMode = false;
    setHeater(extractBool(body, "heaterOn", heaterOn));
  }

  if (body.indexOf("\"targetMoisture\"") >= 0) {
    float value = extractNumber(body, "targetMoisture", targetMoisture);
    if (value >= 5.0f && value <= 40.0f) {
      targetMoisture = value;
    }
  }

  if (body.indexOf("\"maxTemperature\"") >= 0) {
    float value = extractNumber(body, "maxTemperature", maxTemperature);
    if (value >= 25.0f && value <= 80.0f) {
      maxTemperature = value;
    }
  }

  if (body.indexOf("\"dryRaw\"") >= 0) {
    dryRaw = (int)extractNumber(body, "dryRaw", dryRaw);
  }
  if (body.indexOf("\"wetRaw\"") >= 0) {
    wetRaw = (int)extractNumber(body, "wetRaw", wetRaw);
  }

  prefs.putBool("autoMode", autoMode);
  prefs.putFloat("target", targetMoisture);
  prefs.putFloat("maxTemp", maxTemperature);
  prefs.putInt("dryRaw", dryRaw);
  prefs.putInt("wetRaw", wetRaw);

  if (autoMode) {
    applyAutoControl();
  }

  sendJson(200, jsonStatus());
}

void startWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Menghubungkan WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(400);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi OK, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi gagal. Membuka hotspot PengeringGabah / 12345678");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial.print("IP hotspot: ");
    Serial.println(WiFi.softAPIP());
  }

  if (MDNS.begin(MDNS_NAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS: http://pengering-gabah.local");
  }
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_LDR_DO, INPUT);
  setHeater(false);

  dht.begin();

  prefs.begin("dryer", false);
  autoMode = prefs.getBool("autoMode", true);
  targetMoisture = prefs.getFloat("target", 14.0f);
  maxTemperature = prefs.getFloat("maxTemp", 50.0f);
  dryRaw = prefs.getInt("dryRaw", 3000);
  wetRaw = prefs.getInt("wetRaw", 1400);

  startWifi();

  server.on("/", HTTP_GET, handleStatus);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/control", HTTP_POST, handleControl);
  server.on("/control", HTTP_OPTIONS, handleOptions);
  server.on("/status", HTTP_OPTIONS, handleOptions);
  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) {
      handleOptions();
      return;
    }
    sendJson(404, "{\"ok\":false,\"error\":\"not_found\"}");
  });
  server.begin();
  Serial.println("HTTP server :80 siap");
}

void loop() {
  server.handleClient();
  unsigned long now = millis();
  if (now - lastSensorMs >= SENSOR_INTERVAL_MS) {
    lastSensorMs = now;
    readSensors();
  }
}
