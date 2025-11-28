M5-AtomS3-Companion-v4-Satellite

A compact, single-button satellite surface built for Companion v4 using the M5 AtomS3.
Includes external RGB rear LED, full-screen upscaled bitmap mode, a new ultra-fast text mode, WiFi config portal, OTA updates, and auto MAC-based deviceID.
Perfect for enhancing productions with on-stage triggers, tally, timers, cues, and status text.

Features
- Two display modes:
  • Bitmap Mode — Renders Companion’s 72×72 bitmaps upscaled to 128×128
  • Text Mode (v1.3) — Fast, low-latency, no bitmap fetches
    - 1–2 chars → Extra-big font
    - 3 chars → Ultra-large font
    - 4–6 chars → Large centered font
    - >6 chars → Auto-wrap multi-line
    - Supports COLOR= and TEXTCOLOR=
- External RGB LED output on G8/G5/G6 (G7 = Ground), mirrors key colour
- WiFiManager config portal (hold button for 5 seconds)
- OTA updates via ArduinoOTA
- Auto deviceID: AtomS3_XXXXXX (full MAC)
- Supports Companion v4 Satellite API: TEXT, BITMAP, COLOR, TEXTCOLOR, BRIGHTNESS, KEY-STATE, PING
- Clean, stable, production-ready codebase
- M5Burner-ready binaries available

Hardware Connections
G8 – LED Red
G5 – LED Green
G6 – LED Blue
G7 – LED Ground
Use a common-cathode RGB LED (G7 = Ground)

Recommended LED
AU: https://www.jaycar.com.au/tricolour-rgb-5mm-led-600-1000mcd-round-diffused/
USA: https://www.adafruit.com/product/302

Installation & Usage
1. Clone this repository.
2. Open CompanionSatelliteSingleButton.ino in Arduino IDE.
3. Select M5AtomS3 via ESP32 board manager.
4. Install libraries: M5Unified, WiFiManager, Preferences.
5. Flash to the AtomS3.
6. On boot, device shows BOOTING then “Waiting for Companion”.
7. In Companion v4: Surfaces → Add Device → deviceID appears automatically.
8. Hold front button for 5 seconds to open WiFi config portal (SSID = deviceID).
9. Configure Companion IP/Port and choose bitmap or text mode.
10. Press button to send KEY-PRESS to Companion. LED mirrors key color.

OTA Firmware Update
- OTA enabled by default.
- Hostname = deviceID
- Password = companion-satellite
- Update from Arduino IDE using Network Ports.

Troubleshooting
Only one letter shows in text mode:
- Update to v1.3 which fixes base64 decoding issues.

LED colours wrong:
- Confirm LED is common-cathode (G7 = Ground).
- Re-check wiring if colors appear swapped.

Not connecting to Companion:
- Check IP and Port in WiFi config portal.
- Ensure device and Companion are on same network.
- Check firewall rules.

Blank screen or crashes:
- Ensure sendAddDevice() uses BITMAPS=72 (required by bitmap mode).

Version History
v1.3
- Text-only mode with zero bitmap requests
- Faster UI, reduced latency
- Fixed multi-letter text decoding
- Improved color handling
- Performance improvements

v1.2
- General stability and UI tweaks
- Improved LED behaviour

v1.1
- Fixed PWM LED output
