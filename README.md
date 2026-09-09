# ESP32-S3 BME688 Web Monitor

ESP32-S3 reads a BME688 / SEN0617 sensor over I2C and shows the values on a small Web page.

## Hardware

- ESP32-S3
- BME688 / SEN0617
- BME688 SDA -> ESP32-S3 GPIO20
- BME688 SCL -> ESP32-S3 GPIO19
- BME688 VCC -> 3.3V
- BME688 GND -> GND
- I2C clock: 100 kHz

Note: GPIO19 and GPIO20 are also ESP32-S3 USB D-/D+ pins. Avoid USB pin conflicts when using this wiring.

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

`include/secrets.h` is ignored by Git and should contain the real Wi-Fi password.

## Wi-Fi Setup

Copy `include/secrets.example.h` to `include/secrets.h`, then edit only `include/secrets.h`.

Use this format:

```cpp
#pragma once

constexpr const char* WIFI_SSID = "JCOM_CHDE";
constexpr const char* WIFI_PASSWORD = "your-real-wifi-password";
```

Do not write the Wi-Fi password directly in `src/main.cpp`.

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

On a normal boot with working BME688 wiring and Wi-Fi, the Serial Monitor should show lines similar to:

```text
Boot start
I2C setup
I2C SDA = GPIO20
I2C SCL = GPIO19
I2C clock = 100000 Hz
BME688 detected
Wi-Fi connection start
SSID: JCOM_CHDE
WiFi connected
SSID: JCOM_CHDE
IP: 192.168.40.xxx
Open:
http://192.168.40.xxx/
HTTP server started
Web URL:
http://192.168.40.xxx/
```

If the sensor is not found, the ESP32 should keep running and print:

```text
BME688 not detected
Check wiring, power, I2C pins, and the I2C address 0x76/0x77.
```

If Wi-Fi connection fails or times out, the ESP32 should print a status message such as:

```text
Wi-Fi connection failed or timed out
SSID: JCOM_CHDE
Wi-Fi status: WL_CONNECT_FAILED (4)
Check SSID, password, router, and 2.4 GHz network availability.
```

## Open From iPhone

Make sure the iPhone is connected to the same Wi-Fi network, then check the ESP32 IP address shown in Serial Monitor.

If Serial Monitor shows:

```text
IP: 192.168.40.123
Open:
http://192.168.40.123/
```

Open this URL in iPhone Safari:

```text
http://192.168.40.123/
```

Your iPhone IP is `192.168.40.107` and the router is `192.168.40.1`, so the ESP32 address should normally be another `192.168.40.xxx` address on the same network.

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
