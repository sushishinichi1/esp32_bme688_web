#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_BME680.h>

#include "secrets.h"

// GPIO19/20 are also used as ESP32-S3 USB D-/D+ pins. Avoid conflicts when USB is in use.
constexpr uint8_t I2C_SDA_PIN = 20;
constexpr uint8_t I2C_SCL_PIN = 19;
constexpr uint32_t I2C_CLOCK_HZ = 100000;
constexpr uint8_t BME688_I2C_ADDRESS = 0x76;

constexpr unsigned long SENSOR_INTERVAL_MS = 3000;
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000;

Adafruit_BME680 bme;
WebServer server(80);
bool bmeReady = false;

struct SensorReading {
  float temperatureC = NAN;
  float humidityPercent = NAN;
  float pressureHpa = NAN;
  float gasResistanceKohms = NAN;
  bool valid = false;
  unsigned long updatedAtMs = 0;
};

SensorReading latestReading;
unsigned long lastSensorReadMs = 0;

String formatFloat(float value, unsigned int digits) {
  if (isnan(value)) {
    return "--";
  }
  return String(value, digits);
}

const char* wifiStatusName(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS:
      return "WL_IDLE_STATUS";
    case WL_NO_SSID_AVAIL:
      return "WL_NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED:
      return "WL_SCAN_COMPLETED";
    case WL_CONNECTED:
      return "WL_CONNECTED";
    case WL_CONNECT_FAILED:
      return "WL_CONNECT_FAILED";
    case WL_CONNECTION_LOST:
      return "WL_CONNECTION_LOST";
    case WL_DISCONNECTED:
      return "WL_DISCONNECTED";
    default:
      return "UNKNOWN";
  }
}

bool readSensor() {
  if (!bmeReady) {
    latestReading.valid = false;
    return false;
  }

  if (!bme.performReading()) {
    Serial.println("BME688 measurement failed");
    latestReading.valid = false;
    return false;
  }

  latestReading.temperatureC = bme.temperature;
  latestReading.humidityPercent = bme.humidity;
  latestReading.pressureHpa = bme.pressure / 100.0F;
  latestReading.gasResistanceKohms = bme.gas_resistance / 1000.0F;
  latestReading.valid = true;
  latestReading.updatedAtMs = millis();

  Serial.println("Sensor values:");
  Serial.printf("  Temperature: %.2f C\n", latestReading.temperatureC);
  Serial.printf("  Humidity: %.2f %%\n", latestReading.humidityPercent);
  Serial.printf("  Pressure: %.2f hPa\n", latestReading.pressureHpa);
  Serial.printf("  Gas resistance: %.2f kOhm\n", latestReading.gasResistanceKohms);

  return true;
}

String sensorJson() {
  String json = "{";
  json += "\"valid\":";
  json += latestReading.valid ? "true" : "false";
  json += ",\"temperature_c\":";
  json += latestReading.valid ? formatFloat(latestReading.temperatureC, 2) : "null";
  json += ",\"humidity_percent\":";
  json += latestReading.valid ? formatFloat(latestReading.humidityPercent, 2) : "null";
  json += ",\"pressure_hpa\":";
  json += latestReading.valid ? formatFloat(latestReading.pressureHpa, 2) : "null";
  json += ",\"gas_resistance_kohm\":";
  json += latestReading.valid ? formatFloat(latestReading.gasResistanceKohms, 2) : "null";
  json += ",\"updated_at_ms\":";
  json += String(latestReading.updatedAtMs);
  json += "}";
  return json;
}

void handleRoot() {
  const char page[] PROGMEM = R"HTML(
<!doctype html>
<html lang="ja">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32-S3 BME688 Monitor</title>
  <style>
    :root {
      color-scheme: light dark;
      font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      background: #f4f7f8;
      color: #162025;
    }
    body {
      margin: 0;
      min-height: 100vh;
      display: grid;
      place-items: center;
      padding: 24px;
    }
    main {
      width: min(760px, 100%);
    }
    h1 {
      margin: 0 0 8px;
      font-size: clamp(1.7rem, 5vw, 2.6rem);
      letter-spacing: 0;
    }
    .status {
      margin: 0 0 24px;
      color: #52636b;
    }
    .grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
      gap: 12px;
    }
    .card {
      border: 1px solid #d9e2e5;
      border-radius: 8px;
      background: #ffffff;
      padding: 18px;
      box-shadow: 0 8px 24px rgba(22, 32, 37, 0.08);
    }
    .label {
      color: #52636b;
      font-size: 0.9rem;
    }
    .value {
      margin-top: 10px;
      font-size: clamp(1.6rem, 7vw, 2.4rem);
      font-weight: 700;
      line-height: 1.1;
      word-break: break-word;
    }
    .unit {
      font-size: 0.95rem;
      color: #52636b;
    }
    @media (prefers-color-scheme: dark) {
      :root {
        background: #101517;
        color: #edf4f5;
      }
      .status, .label, .unit {
        color: #a6b5bb;
      }
      .card {
        background: #192124;
        border-color: #2c3a40;
        box-shadow: none;
      }
    }
  </style>
</head>
<body>
  <main>
    <h1>ESP32-S3 BME688 Monitor</h1>
    <p class="status" id="status">Loading sensor data...</p>
    <section class="grid" aria-live="polite">
      <div class="card">
        <div class="label">Temperature</div>
        <div class="value"><span id="temperature">--</span> <span class="unit">C</span></div>
      </div>
      <div class="card">
        <div class="label">Humidity</div>
        <div class="value"><span id="humidity">--</span> <span class="unit">%</span></div>
      </div>
      <div class="card">
        <div class="label">Pressure</div>
        <div class="value"><span id="pressure">--</span> <span class="unit">hPa</span></div>
      </div>
      <div class="card">
        <div class="label">Gas Resistance</div>
        <div class="value"><span id="gas">--</span> <span class="unit">kOhm</span></div>
      </div>
    </section>
  </main>
  <script>
    const setText = (id, value) => {
      document.getElementById(id).textContent =
        typeof value === "number" ? value.toFixed(2) : "--";
    };

    async function updateSensor() {
      try {
        const response = await fetch("/api/sensor", { cache: "no-store" });
        const data = await response.json();
        setText("temperature", data.temperature_c);
        setText("humidity", data.humidity_percent);
        setText("pressure", data.pressure_hpa);
        setText("gas", data.gas_resistance_kohm);
        document.getElementById("status").textContent =
          data.valid ? "Updated just now" : "Sensor reading is not available";
      } catch (error) {
        document.getElementById("status").textContent = "Could not fetch sensor data";
      }
    }

    updateSensor();
    setInterval(updateSensor, 3000);
  </script>
</body>
</html>
)HTML";

  server.send(200, "text/html; charset=utf-8", page);
}

void handleSensorApi() {
  server.send(200, "application/json", sensorJson());
}

void connectWiFi() {
  Serial.println("Wi-Fi connection start");
  Serial.printf("SSID: %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < WIFI_CONNECT_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected");
    Serial.printf("SSID: %s\n", WiFi.SSID().c_str());
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.println("Open:");
    Serial.printf("http://%s/\n", WiFi.localIP().toString().c_str());
  } else {
    const wl_status_t status = WiFi.status();
    Serial.println("Wi-Fi connection failed or timed out");
    Serial.printf("SSID: %s\n", WIFI_SSID);
    Serial.printf("Wi-Fi status: %s (%d)\n", wifiStatusName(status), static_cast<int>(status));
    Serial.println("Check SSID, password, router, and 2.4 GHz network availability.");
  }
}

void setupBme688() {
  Serial.println("I2C setup");
  Serial.printf("I2C SDA = GPIO%u\n", I2C_SDA_PIN);
  Serial.printf("I2C SCL = GPIO%u\n", I2C_SCL_PIN);
  Serial.printf("I2C clock = %lu Hz\n", I2C_CLOCK_HZ);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_CLOCK_HZ);

  if (!bme.begin(BME688_I2C_ADDRESS, &Wire)) {
    Serial.println("BME688 not detected");
    Serial.println("Check wiring, power, I2C pins, and the I2C address 0x76/0x77.");
    bmeReady = false;
    return;
  }

  bmeReady = true;
  Serial.println("BME688 detected");
  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150);

  readSensor();
}

void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/sensor", HTTP_GET, handleSensorApi);
  server.begin();
  Serial.println("HTTP server started");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Web URL:");
    Serial.printf("http://%s/\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("Web URL unavailable because Wi-Fi is not connected.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("Boot start");

  setupBme688();
  connectWiFi();
  setupWebServer();
}

void loop() {
  server.handleClient();

  if (millis() - lastSensorReadMs >= SENSOR_INTERVAL_MS) {
    lastSensorReadMs = millis();
    readSensor();
  }
}
