# ESP32-S3 BME688 Outdoor Monitor

ESP32-S3 reads a BME688 / SEN0617 sensor over I2C and serves the Coffee Odor Monitor page over its own Wi-Fi access point. No home Wi-Fi or Internet connection is required.

## Hardware

- ESP32-S3
- BME688 / SEN0617
- BME688 SDA -> ESP32-S3 GPIO20
- BME688 SCL -> ESP32-S3 GPIO19
- BME688 VCC -> 3.3V
- BME688 GND -> GND
- I2C clock: 100 kHz

Note: GPIO19 and GPIO20 are also ESP32-S3 native USB D-/D+ pins. `platformio.ini` selects COM3 and the board's default `esptool` upload protocol, normally using the USB-to-UART port. Use that port for upload/serial monitoring; do not use the native USB OTG port with the sensor connected. If the board's COM3 is the native USB port, disconnect the sensor during upload and reconnect it afterward.

## Files

```text
03_esp32_bme688_web/
  platformio.ini
  src/
    main.cpp
  include/
    secrets.example.h
    secrets.h
  lib/
  .gitignore
  README.md
```

`include/secrets.h` is ignored by Git and stores the access point credentials locally.

## Access Point Setup

Copy the AP settings from `include/secrets.example.h` into `include/secrets.h` (or add them to the existing file). Set `AP_SSID` to `TownSensor` and set `AP_PASSWORD` to the locally agreed development password. Do not commit `include/secrets.h` or put the password in `src/main.cpp`.

The device uses `192.168.4.1` and serves HTTP on port 80. The browser API uses a relative URL, so it works from the access point without a home-network address.

```cpp
constexpr const char* AP_SSID = "TownSensor";
constexpr const char* AP_PASSWORD = "set-locally";
```

## Build

From this folder:

```powershell
python -m platformio run
```

## Upload To ESP32-S3

Do this only when you are ready to write the firmware to the board:

```powershell
python -m platformio run --target upload
```

This project uses the `esp32-s3-devkitc-1` PlatformIO environment from `platformio.ini`.

## Serial Monitor

After uploading, open the Serial Monitor:

```powershell
python -m platformio device monitor
```

The monitor speed is `115200`, as configured in `platformio.ini`.

## Expected Serial Output

On boot, the Serial Monitor should show lines similar to:

```text
Boot start
I2C setup
I2C SDA = GPIO20
I2C SCL = GPIO19
I2C clock = 100000 Hz
BME688 detected
Wi-Fi access point start
SSID: TownSensor
Access point ready
IP: 192.168.4.1
Open:
http://192.168.4.1/
HTTP server started
Web URL:
http://192.168.4.1/
```

If the sensor is not found, the ESP32 should keep running and print:

```text
BME688 not detected
Check wiring, power, I2C pins, and the I2C address 0x76/0x77.
```

If the access point fails to start, the ESP32 should print:

```text
Access point start failed
```

## Outdoor Use From iPad

1. Power the ESP32-S3 from a mobile battery.
2. In iPad Wi-Fi settings, connect to `TownSensor` using the password configured in `include/secrets.h`.
3. Open `http://192.168.4.1/` in Safari.
4. Set a baseline and start the measurement.
5. Stop the measurement and use Export CSV to save the file from Safari.

The access point has no Internet connection; iPad may report that the Wi-Fi network has no Internet access. Stay connected to it while using the monitor.

Sensor reads continue on the ESP32 while the iPad is disconnected. However, Baseline, Measurement samples, graph history, summary, and CSV are held in the browser page's memory, not on the ESP32. A disconnect/reload can therefore pause collection and lose the current page's measurement data; the missing interval is not backfilled after reconnecting. Keep Safari open and connected until CSV export is complete.

## Web Page

The root page `/` shows:

- temperature
- humidity
- pressure
- gas resistance
- sensor status

The JSON API is available at:

```text
/api/sensor
```

Example:

```json
{
  "valid": true,
  "temperature_c": 24.50,
  "humidity_percent": 48.20,
  "pressure_hpa": 1012.30,
  "gas_resistance_kohm": 18.75,
  "updated_at_ms": 12345
}
```
