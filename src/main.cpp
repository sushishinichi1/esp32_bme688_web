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
constexpr uint8_t BME688_I2C_ADDRESS = 0x77;
constexpr uint8_t BME688_CHIP_ID_REGISTER = 0xD0;
constexpr uint8_t BME688_VARIANT_ID_REGISTER = 0xF0;

constexpr unsigned long SENSOR_INTERVAL_MS = 1000;

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

void scanI2cBus() {
  uint8_t foundCount = 0;

  Serial.println("I2C scan start");
  for (uint8_t address = 0x01; address <= 0x7E; address++) {
    Wire.beginTransmission(address);
    const uint8_t error = Wire.endTransmission();

    if (error == 0) {
      Serial.printf("I2C device found at 0x%02X\n", address);
      foundCount++;
    }
  }

  if (foundCount == 0) {
    Serial.println("No I2C devices found");
  }
  Serial.println("I2C scan done");
}

bool readI2cRegister(uint8_t address, uint8_t reg, uint8_t& value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  const uint8_t writeError = Wire.endTransmission(false);
  if (writeError != 0) {
    return false;
  }

  const uint8_t bytesRead = Wire.requestFrom(address, static_cast<uint8_t>(1));
  if (bytesRead != 1 || !Wire.available()) {
    return false;
  }

  value = Wire.read();
  return true;
}

void printBme688IdRegisters() {
  uint8_t chipId = 0;
  uint8_t variantId = 0;

  Serial.printf("I2C address: 0x%02X\n", BME688_I2C_ADDRESS);
  if (readI2cRegister(BME688_I2C_ADDRESS, BME688_CHIP_ID_REGISTER, chipId)) {
    Serial.printf("chip ID register 0x%02X: 0x%02X\n", BME688_CHIP_ID_REGISTER, chipId);
  } else {
    Serial.printf("chip ID register 0x%02X: read failed\n", BME688_CHIP_ID_REGISTER);
  }

  if (readI2cRegister(BME688_I2C_ADDRESS, BME688_VARIANT_ID_REGISTER, variantId)) {
    Serial.printf("variant ID register 0x%02X: 0x%02X\n", BME688_VARIANT_ID_REGISTER, variantId);
  } else {
    Serial.printf("variant ID register 0x%02X: read failed\n", BME688_VARIANT_ID_REGISTER);
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

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="ja">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32-S3 BME688 Coffee Odor Monitor</title>
  <style>
    :root {
      color-scheme: light dark;
      font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      background: #f3f5f2;
      color: #17201b;
      --panel: #ffffff;
      --border: #d8ded8;
      --muted: #4f5b53;
      --accent: #12664f;
      --warn: #a95d18;
      --bad: #a13c3c;
      --ink: #17201b;
    }
    body {
      margin: 0;
      min-height: 100vh;
      padding: 16px;
      background: #f3f5f2;
    }
    main {
      width: min(980px, 100%);
      margin: 0 auto;
    }
    h1 {
      margin: 0;
      font-size: 1rem;
      letter-spacing: 0;
      text-transform: uppercase;
    }
    h2 {
      margin: 0 0 10px;
      font-size: 1rem;
    }
    button, input {
      font: inherit;
    }
    button {
      border: 1px solid #b8c3bb;
      border-radius: 8px;
      background: #ffffff;
      color: var(--ink);
      min-height: 42px;
      padding: 0 14px;
    }
    button.primary {
      background: var(--accent);
      border-color: var(--accent);
      color: #ffffff;
    }
    button:disabled {
      opacity: 0.48;
    }
    input {
      box-sizing: border-box;
      width: 100%;
      border: 1px solid #b8c3bb;
      border-radius: 8px;
      min-height: 42px;
      padding: 0 12px;
      background: #ffffff;
      color: var(--ink);
    }
    .top {
      display: grid;
      gap: 4px;
      margin: 4px 0 16px;
    }
    .device {
      color: var(--muted);
      font-size: 0.86rem;
      font-weight: 700;
    }
    .status-line {
      color: var(--muted);
      font-size: 0.95rem;
    }
    .dot {
      display: inline-block;
      width: 0.68em;
      height: 0.68em;
      border-radius: 50%;
      background: var(--bad);
      margin-right: 6px;
    }
    .dot.ok {
      background: var(--accent);
    }
    .grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
      gap: 12px;
      margin-bottom: 12px;
    }
    .overview-grid {
      display: grid;
      gap: 12px;
      margin-bottom: 12px;
    }
    .environment-kpis {
      display: contents;
    }
    .card {
      border: 1px solid var(--border);
      border-radius: 8px;
      background: var(--panel);
      padding: 14px;
    }
    .label {
      color: var(--muted);
      font-size: 0.82rem;
      font-weight: 700;
    }
    .value {
      margin-top: 8px;
      font-size: 1.7rem;
      font-weight: 700;
      line-height: 1.1;
      word-break: break-word;
    }
    .unit {
      font-size: 0.88rem;
      color: var(--muted);
    }
    .delta {
      color: var(--muted);
      font-size: 0.92rem;
      margin-top: 6px;
    }
    .delta.drop {
      color: var(--warn);
    }
    .panel {
      border-top: 1px solid var(--border);
      padding: 16px 0;
    }
    .chart-wrap {
      background: var(--panel);
      border: 1px solid var(--border);
      border-radius: 8px;
      padding: 10px;
      margin-bottom: 12px;
    }
    .chart-grid {
      display: grid;
      gap: 12px;
    }
    .odor-panel {
      border: 1px solid var(--border);
      border-radius: 8px;
      background: var(--panel);
      padding: 10px;
    }
    .odor-head {
      display: flex;
      align-items: baseline;
      justify-content: space-between;
      gap: 12px;
      margin-bottom: 6px;
    }
    .odor-kpi {
      color: var(--muted);
      font-size: 0.86rem;
      white-space: nowrap;
    }
    canvas {
      display: block;
      width: 100%;
      height: 190px;
    }
    .controls {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
      gap: 10px;
      align-items: end;
    }
    .field {
      display: grid;
      gap: 6px;
    }
    .readout {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(130px, 1fr));
      gap: 10px;
      margin-top: 12px;
    }
    .mini {
      border: 1px solid var(--border);
      border-radius: 8px;
      padding: 10px;
      background: var(--panel);
    }
    .mini .value {
      font-size: 1.25rem;
    }
    .summary {
      white-space: pre-line;
      color: var(--ink);
      line-height: 1.55;
      min-height: 44px;
    }
    @media (min-width: 769px) {
      body {
        padding: 6px 24px 10px;
        font-size: 12px;
      }
      main {
        width: min(94vw, 1500px);
      }
      h1 {
        font-size: 0.95rem;
      }
      h2 {
        font-size: 0.88rem;
        margin-bottom: 5px;
      }
      button,
      input {
        min-height: 28px;
        font-size: 0.82rem;
      }
      button {
        padding: 0 9px;
      }
      input {
        padding: 0 8px;
      }
      .top {
        grid-template-columns: 1fr;
        align-items: center;
        gap: 0;
        margin: 0 0 4px;
      }
      .status-line {
        font-size: 0.75rem;
      }
      .grid {
        grid-template-columns: repeat(5, minmax(0, 1fr));
        gap: 5px;
        margin-bottom: 5px;
      }
      .overview-grid {
        grid-template-columns: 220px minmax(0, 1fr);
        gap: 10px;
        align-items: stretch;
        margin-bottom: 5px;
      }
      .environment-kpis {
        display: grid;
        grid-template-rows: repeat(2, minmax(0, 1fr));
        gap: 10px;
      }
      .card {
        min-height: 48px;
        padding: 7px 8px;
      }
      .label {
        font-size: 0.72rem;
      }
      .value {
        margin-top: 3px;
        font-size: 1.22rem;
      }
      .unit {
        font-size: 0.7rem;
      }
      .delta {
        font-size: 0.72rem;
        margin-top: 3px;
      }
      .panel {
        padding: 5px 0;
      }
      .chart-grid {
        display: grid;
        grid-template-columns: minmax(0, 1fr);
        gap: 0;
      }
      .odor-panel {
        padding: 6px;
      }
      .odor-head {
        margin-bottom: 4px;
      }
      .odor-kpi {
        font-size: 0.72rem;
      }
      .chart-wrap {
        padding: 5px;
        margin-bottom: 0;
      }
      canvas {
        height: 165px;
      }
      .controls {
        grid-template-columns: repeat(auto-fit, minmax(145px, 1fr));
        gap: 6px;
      }
      .field {
        gap: 3px;
      }
      .readout {
        grid-template-columns: repeat(3, minmax(0, 1fr));
        gap: 6px;
        margin-top: 6px;
      }
      .mini {
        padding: 6px;
      }
      .mini .value {
        margin-top: 3px;
        font-size: 0.84rem;
      }
      .readout .mini .value {
        font-size: 0.98rem;
      }
      .summary {
        font-size: 0.8rem;
        line-height: 1.35;
        min-height: 0;
      }
      .panel > .status-line {
        margin: 2px 0 0;
      }
      h2 .status-line {
        margin-left: 8px;
        font-size: 0.72rem;
        font-weight: 400;
      }
    }
    @media (prefers-color-scheme: dark) {
      :root {
        background: #101411;
        color: #edf3ee;
        --panel: #19201b;
        --border: #303b33;
        --muted: #c6d2ca;
        --accent: #42b883;
        --warn: #e5a14d;
        --bad: #e46d6d;
        --ink: #edf3ee;
      }
      body {
        background: #101411;
      }
      button, input {
        background: #19201b;
        border-color: #425046;
        color: var(--ink);
      }
    }
  </style>
</head>
<body>
  <main>
    <header class="top">
      <div class="status-line"><span class="dot" id="status-dot"></span>Sensor <span id="status">Loading...</span></div>
    </header>

    <section class="overview-grid" aria-live="polite">
      <div class="environment-kpis">
        <div class="card">
          <div class="label">Temperature</div>
          <div class="value"><span id="temperature">--</span> <span class="unit">C</span></div>
          <div class="delta">Pressure <span id="pressure">--</span> hPa</div>
        </div>
        <div class="card">
          <div class="label">Humidity</div>
          <div class="value"><span id="humidity">--</span> <span class="unit">%</span></div>
          <div class="delta">Humidity Δ <span id="humidity-delta">--</span>%</div>
        </div>
      </div>
      <div class="odor-panel">
        <div class="odor-head">
          <h2>Odor Response</h2>
          <div class="odor-kpi">
            Gas <span id="gas">--</span> kOhm |
            Ratio <span id="gas-ratio-kpi">--</span> |
            Change <span id="gas-change-kpi">--</span>%
          </div>
        </div>
        <div class="delta" id="gas-change">Baseline not set</div>
        <div class="chart-grid">
          <div class="chart-wrap"><canvas id="odor-chart" width="1200" height="320"></canvas></div>
        </div>
      </div>
    </section>

    <section class="panel">
      <h2>Baseline <span class="status-line" id="baseline-status">Ready</span></h2>
      <div class="controls">
        <button id="baseline-button" class="primary">Set Baseline</button>
        <div class="mini">
          <div class="label">Baseline Gas</div>
          <div class="value"><span id="baseline-gas">--</span> <span class="unit">kOhm</span></div>
        </div>
        <div class="mini">
          <div class="label">Delta</div>
          <div class="value"><span id="gas-delta">--</span> <span class="unit">kOhm</span></div>
        </div>
        <div class="mini">
          <div class="label">Ratio</div>
          <div class="value"><span id="gas-ratio">--</span></div>
        </div>
        <div class="mini">
          <div class="label">Current Gas</div>
          <div class="value"><span id="current-gas">--</span> <span class="unit">kOhm</span></div>
        </div>
        <div class="mini">
          <div class="label">Change</div>
          <div class="value"><span id="baseline-change">--</span> <span class="unit">%</span></div>
        </div>
      </div>
    </section>

    <section class="panel">
      <h2>Measurement</h2>
      <div class="controls">
        <label class="field">
          <input id="sample-label" list="label-suggestions" placeholder="coffee">
        </label>
        <datalist id="label-suggestions">
          <option value="coffee"></option>
          <option value="alcohol"></option>
          <option value="perfume"></option>
          <option value="clean_air"></option>
          <option value="room_air"></option>
        </datalist>
        <button id="start-button" class="primary">Start Measurement</button>
        <button id="stop-button" disabled>Stop Measurement</button>
        <button id="clear-button">Clear</button>
        <button id="export-button">Export CSV</button>
      </div>
      <div class="readout">
        <div class="mini">
          <div class="label">Measurement</div>
          <div class="value" id="measurement-state">Idle</div>
        </div>
        <div class="mini">
          <div class="label">Elapsed</div>
          <div class="value" id="elapsed">00:00</div>
        </div>
        <div class="mini">
          <div class="label">Measurement Time</div>
          <div class="value" id="measurement-time">0 <span class="unit">ms</span></div>
        </div>
      </div>
    </section>

    <section class="panel">
      <h2>Measurement Summary</h2>
      <div class="summary" id="summary">No measurement yet.</div>
    </section>
  </main>
  <script>
    const HISTORY_SECONDS = 600;
    const BASELINE_SECONDS = 30;
    const history = [];
    const samples = [];
    let latest = null;
    let baselineGas = null;
    let baselineHumidity = null;
    let baselineStart = 0;
    let baselineValues = [];
    let baselineHumidityValues = [];
    let measuring = false;
    let measurementStart = 0;
    let measurementLabel = "";

    const $ = (id) => document.getElementById(id);
    const fmt = (value, digits = 2) => Number.isFinite(value) ? value.toFixed(digits) : "--";

    function setText(id, value, digits = 2) {
      $(id).textContent = fmt(value, digits);
    }

    function formatElapsed(ms) {
      const total = Math.max(0, Math.floor(ms / 1000));
      const minutes = Math.floor(total / 60);
      const seconds = total % 60;
      return `${String(minutes).padStart(2, "0")}:${String(seconds).padStart(2, "0")}`;
    }

    function pushHistory(point) {
      history.push(point);
      const cutoff = point.elapsed - HISTORY_SECONDS;
      while (history.length && history[0].elapsed < cutoff) {
        history.shift();
      }
    }

    function prepareCanvas(canvas) {
      const rect = canvas.getBoundingClientRect();
      const dpr = window.devicePixelRatio || 1;
      const width = Math.max(1, Math.round(rect.width));
      const height = Math.max(1, Math.round(rect.height));
      const scaledWidth = Math.round(width * dpr);
      const scaledHeight = Math.round(height * dpr);
      if (canvas.width !== scaledWidth || canvas.height !== scaledHeight) {
        canvas.width = scaledWidth;
        canvas.height = scaledHeight;
      }
      const ctx = canvas.getContext("2d");
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      return { ctx, width, height };
    }

    function drawChart(canvas, points, field, unit, stroke, zeroLine = null) {
      const prepared = prepareCanvas(canvas);
      const ctx = prepared.ctx;
      const width = prepared.width;
      const height = prepared.height;
      ctx.clearRect(0, 0, width, height);

      const plot = { x: 44, y: 14, w: width - 58, h: height - 44 };
      ctx.strokeStyle = getComputedStyle(document.documentElement).getPropertyValue("--border").trim();
      ctx.lineWidth = 1;
      ctx.strokeRect(plot.x, plot.y, plot.w, plot.h);

      const values = points.map((p) => p[field]).filter(Number.isFinite);
      ctx.fillStyle = getComputedStyle(document.documentElement).getPropertyValue("--muted").trim();
      ctx.font = `${width >= 900 ? 14 : 12}px system-ui, sans-serif`;
      if (values.length < 2) {
        ctx.fillText("Waiting for data", plot.x + 12, plot.y + 26);
        return;
      }

      let min = Math.min(...values);
      let max = Math.max(...values);
      if (min === max) {
        min -= 1;
        max += 1;
      }
      const pad = (max - min) * 0.12;
      min -= pad;
      max += pad;

      const start = points[0].elapsed;
      const end = Math.max(points[points.length - 1].elapsed, start + 1);
      const xFor = (elapsed) => plot.x + ((elapsed - start) / (end - start)) * plot.w;
      const yFor = (value) => plot.y + plot.h - ((value - min) / (max - min)) * plot.h;

      ctx.fillText(`${max.toFixed(1)} ${unit}`, 4, plot.y + 12);
      ctx.fillText(`${min.toFixed(1)} ${unit}`, 4, plot.y + plot.h);
      ctx.fillText(`${Math.round(end - start)} sec`, plot.x + plot.w - 48, height - 12);

      if (Number.isFinite(zeroLine) && zeroLine >= min && zeroLine <= max) {
        const zeroY = yFor(zeroLine);
        ctx.strokeStyle = getComputedStyle(document.documentElement).getPropertyValue("--muted").trim();
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(plot.x, zeroY);
        ctx.lineTo(plot.x + plot.w, zeroY);
        ctx.stroke();
        ctx.fillText(`${zeroLine.toFixed(0)} ${unit}`, plot.x + 6, zeroY - 4);
      }

      ctx.strokeStyle = stroke;
      ctx.lineWidth = 2;
      ctx.beginPath();
      let started = false;
      for (const point of points) {
        const value = point[field];
        if (!Number.isFinite(value)) {
          continue;
        }
        const x = xFor(point.elapsed);
        const y = yFor(value);
        if (!started) {
          ctx.moveTo(x, y);
          started = true;
        } else {
          ctx.lineTo(x, y);
        }
      }
      ctx.stroke();
    }

    function updateCharts() {
      const hasBaseline = Number.isFinite(baselineGas) && baselineGas !== 0;
      const odorPoints = history.map((point) => ({
        ...point,
        odor: hasBaseline ? ((point.gas - baselineGas) / baselineGas) * 100 : point.gas
      }));
      drawChart(
        $("odor-chart"),
        odorPoints,
        "odor",
        hasBaseline ? "%" : "kOhm",
        "#12664f",
        hasBaseline ? 0 : null
      );
    }

    function updateBaseline(point) {
      if (baselineStart === 0 || !Number.isFinite(point.gas)) {
        return;
      }

      const elapsedSec = Math.floor((Date.now() - baselineStart) / 1000);
      if (elapsedSec <= BASELINE_SECONDS) {
        baselineValues.push(point.gas);
        baselineHumidityValues.push(point.humidity);
        $("baseline-status").textContent = `Collecting baseline... ${Math.min(elapsedSec, BASELINE_SECONDS)} / ${BASELINE_SECONDS} sec`;
      }

      if (elapsedSec >= BASELINE_SECONDS) {
        baselineStart = 0;
        baselineGas = baselineValues.reduce((sum, value) => sum + value, 0) / baselineValues.length;
        baselineHumidity = baselineHumidityValues.reduce((sum, value) => sum + value, 0) / baselineHumidityValues.length;
        $("baseline-gas").textContent = fmt(baselineGas, 1);
        $("baseline-status").textContent = "Baseline set";
        $("baseline-button").disabled = false;
      }
    }

    function gasChangePercent(gas) {
      if (!Number.isFinite(gas) || !Number.isFinite(baselineGas) || baselineGas === 0) {
        return null;
      }
      return ((gas - baselineGas) / baselineGas) * 100;
    }

    function gasDeltaKohm(gas) {
      if (!Number.isFinite(gas) || !Number.isFinite(baselineGas)) {
        return null;
      }
      return gas - baselineGas;
    }

    function gasRatio(gas) {
      if (!Number.isFinite(gas) || !Number.isFinite(baselineGas) || baselineGas === 0) {
        return null;
      }
      return gas / baselineGas;
    }

    function updateBaselineReadout(gas) {
      $("current-gas").textContent = fmt(gas, 1);
      const change = gasChangePercent(gas);
      const delta = gasDeltaKohm(gas);
      const ratio = gasRatio(gas);
      $("baseline-change").textContent = Number.isFinite(change) ? change.toFixed(1) : "--";
      $("gas-delta").textContent = Number.isFinite(delta) ? delta.toFixed(2) : "--";
      $("gas-ratio").textContent = Number.isFinite(ratio) ? ratio.toFixed(4) : "--";
      $("gas-ratio-kpi").textContent = Number.isFinite(ratio) ? ratio.toFixed(4) : "--";
      $("gas-change-kpi").textContent = Number.isFinite(change) ? change.toFixed(1) : "--";
      $("gas-change").textContent = Number.isFinite(change)
        ? `${change >= 0 ? "+" : ""}${change.toFixed(1)}% from baseline`
        : "Baseline not set";
      $("gas-change").classList.toggle("drop", Number.isFinite(change) && change < 0);
    }

    function updateHumidityDelta(humidity) {
      const delta = Number.isFinite(humidity) && Number.isFinite(baselineHumidity)
        ? humidity - baselineHumidity
        : null;
      $("humidity-delta").textContent = Number.isFinite(delta)
        ? `${delta >= 0 ? "+" : ""}${delta.toFixed(1)}`
        : "--";
    }

    function recordMeasurement(point) {
      if (!measuring || !point.valid) {
        return;
      }
      const elapsedMs = point.timestamp - measurementStart;
      const delta = gasDeltaKohm(point.gas);
      const ratio = gasRatio(point.gas);
      samples.push({
        timestamp_ms: point.timestamp,
        elapsed_ms: elapsedMs,
        label: measurementLabel,
        temperature_c: point.temperature,
        humidity_percent: point.humidity,
        pressure_hpa: point.pressure,
        gas_resistance_kohm: point.gas,
        baseline_gas_kohm: baselineGas,
        gas_delta_kohm: delta,
        gas_ratio: ratio
      });
    }

    function buildSummary() {
      if (!samples.length) {
        $("summary").textContent = "No measurement data.";
        return;
      }
      const gases = samples.map((s) => s.gas_resistance_kohm).filter(Number.isFinite);
      const humidities = samples.map((s) => s.humidity_percent).filter(Number.isFinite);
      const temps = samples.map((s) => s.temperature_c).filter(Number.isFinite);
      const avg = (values) => values.reduce((sum, value) => sum + value, 0) / values.length;
      const first = samples[0];
      const last = samples[samples.length - 1];
      const startGas = first.gas_resistance_kohm;
      const endGas = last.gas_resistance_kohm;
      const startHumidity = first.humidity_percent;
      const maxHumidity = Math.max(...humidities);
      const gasChange = Number.isFinite(startGas) && startGas !== 0 ? ((endGas - startGas) / startGas) * 100 : null;
      const humidityChange = Number.isFinite(startHumidity) ? maxHumidity - startHumidity : null;
      const lines = [
        `Label: ${measurementLabel || "unlabeled"}`,
        `Duration: ${Math.round(last.elapsed_ms / 1000)} sec`,
        `Start Gas: ${fmt(startGas)} kOhm`,
        `Minimum Gas: ${fmt(Math.min(...gases))} kOhm`,
        `Maximum Gas: ${fmt(Math.max(...gases))} kOhm`,
        `Average Gas: ${fmt(avg(gases))} kOhm`,
        `Gas Change: ${Number.isFinite(gasChange) ? gasChange.toFixed(2) : "--"} %`,
        `Baseline Gas: ${fmt(baselineGas)} kOhm`,
        `Last Delta: ${fmt(gasDeltaKohm(endGas))} kOhm`,
        `Last Ratio: ${Number.isFinite(gasRatio(endGas)) ? gasRatio(endGas).toFixed(4) : "--"}`,
        `Start Humidity: ${fmt(startHumidity)} %`,
        `Maximum Humidity: ${fmt(maxHumidity)} %`,
        `Humidity Change: ${Number.isFinite(humidityChange) ? humidityChange.toFixed(2) : "--"} %`,
        `Average Temperature: ${fmt(avg(temps))} C`
      ];
      $("summary").textContent = lines.join("\n");
    }

    function exportCsv() {
      const headers = [
        "label",
        "timestamp_ms",
        "elapsed_ms",
        "temperature_c",
        "humidity_percent",
        "pressure_hpa",
        "gas_resistance_kohm",
        "baseline_gas_kohm",
        "gas_delta_kohm",
        "gas_ratio"
      ];
      const escapeCsv = (value) => `"${String(value ?? "").replaceAll('"', '""')}"`;
      const rows = samples.map((row) => headers.map((key) => escapeCsv(row[key])).join(","));
      const csv = [headers.join(","), ...rows].join("\r\n");
      const blob = new Blob([csv], { type: "text/csv;charset=utf-8" });
      const url = URL.createObjectURL(blob);
      const link = document.createElement("a");
      link.href = url;
      link.download = `${measurementLabel || "bme688-measurement"}.csv`;
      link.click();
      URL.revokeObjectURL(url);
    }

    async function updateSensor() {
      try {
        const response = await fetch("/api/sensor", { cache: "no-store" });
        const data = await response.json();
        const now = Date.now();
        const point = {
          timestamp: now,
          elapsed: history.length ? history[history.length - 1].elapsed + 1 : 0,
          valid: data.valid === true,
          temperature: Number(data.temperature_c),
          humidity: Number(data.humidity_percent),
          pressure: Number(data.pressure_hpa),
          gas: Number(data.gas_resistance_kohm)
        };

        latest = point;
        $("status").textContent = point.valid ? "Connected" : "Sensor unavailable";
        $("status-dot").classList.toggle("ok", point.valid);
        setText("temperature", point.temperature);
        setText("humidity", point.humidity);
        setText("pressure", point.pressure);
        setText("gas", point.gas);
        updateBaseline(point);
        updateBaselineReadout(point.gas);
        updateHumidityDelta(point.humidity);
        pushHistory(point);
        recordMeasurement(point);
        updateCharts();
      } catch (error) {
        $("status").textContent = "Connection lost";
        $("status-dot").classList.remove("ok");
      }
    }

    $("baseline-button").addEventListener("click", () => {
      baselineStart = Date.now();
      baselineValues = [];
      baselineHumidityValues = [];
      $("baseline-status").textContent = `Collecting baseline... 0 / ${BASELINE_SECONDS} sec`;
      $("baseline-button").disabled = true;
    });

    $("start-button").addEventListener("click", () => {
      measurementLabel = $("sample-label").value.trim() || "unlabeled";
      measurementStart = Date.now();
      samples.length = 0;
      measuring = true;
      $("measurement-state").textContent = "Recording";
      $("summary").textContent = "Recording...";
      $("start-button").disabled = true;
      $("stop-button").disabled = false;
    });

    $("stop-button").addEventListener("click", () => {
      measuring = false;
      $("measurement-state").textContent = "Stopped";
      $("start-button").disabled = false;
      $("stop-button").disabled = true;
      buildSummary();
    });

    $("clear-button").addEventListener("click", () => {
      measuring = false;
      measurementStart = 0;
      samples.length = 0;
      history.length = 0;
      measurementLabel = "";
      $("measurement-state").textContent = "Idle";
      $("elapsed").textContent = "00:00";
      $("measurement-time").innerHTML = '0 <span class="unit">ms</span>';
      $("summary").textContent = "No measurement yet.";
      $("start-button").disabled = false;
      $("stop-button").disabled = true;
      updateCharts();
    });

    $("export-button").addEventListener("click", exportCsv);
    window.addEventListener("resize", updateCharts);

    setInterval(() => {
      if (measuring) {
        const elapsedMs = Date.now() - measurementStart;
        $("elapsed").textContent = formatElapsed(elapsedMs);
        $("measurement-time").innerHTML = `${elapsedMs} <span class="unit">ms</span>`;
      }
    }, 250);

    updateSensor();
    setInterval(updateSensor, 1000);
  </script>
</body>
</html>
)HTML";

void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void handleSensorApi() {
  server.send(200, "application/json", sensorJson());
}

void startAccessPoint() {
  const IPAddress apIp(192, 168, 4, 1);
  const IPAddress subnet(255, 255, 255, 0);

  Serial.println("Wi-Fi access point start");
  Serial.printf("SSID: %s\n", AP_SSID);
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAPConfig(apIp, apIp, subnet)) {
    Serial.println("Access point IP configuration failed");
  }
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    Serial.println("Access point start failed");
    return;
  }

  Serial.println("Access point ready");
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.println("Open:");
  Serial.println("http://192.168.4.1/");
}

void setupBme688() {
  Serial.println("I2C setup");
  Serial.printf("I2C SDA = GPIO%u\n", I2C_SDA_PIN);
  Serial.printf("I2C SCL = GPIO%u\n", I2C_SCL_PIN);
  Serial.printf("I2C clock = %lu Hz\n", I2C_CLOCK_HZ);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_CLOCK_HZ);
  scanI2cBus();
  printBme688IdRegisters();

  const bool beginOk = bme.begin(BME688_I2C_ADDRESS);
  Serial.printf("BME688 begin result: %d\n", beginOk ? 1 : 0);

  if (!beginOk) {
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
  Serial.println("Web URL:");
  Serial.println("http://192.168.4.1/");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("Boot start");

  setupBme688();
  startAccessPoint();
  setupWebServer();
}

void loop() {
  server.handleClient();

  if (millis() - lastSensorReadMs >= SENSOR_INTERVAL_MS) {
    lastSensorReadMs = millis();
    readSensor();
  }
}
