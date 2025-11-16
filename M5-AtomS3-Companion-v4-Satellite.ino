/*
  ------------------------------------------------------------
  M5 AtomS3 Companion v4
  Single Button Satellite
  Author: Adrian Davis
  URL: https://github.com/themusicnerd/M5-AtomS3-Companion-v4-Satellite
  License: MIT

  Features:
    - Companion v4 Satellite API support
    - Single-button surface + RGB LED
    - Automatic scaling of 72x72 bitmaps to full screen
    - External RGB LED PWM output (G5 RED/G6 GREEN/G8 BLUE + G7 GND)
    - WiFiManager config portal (hold 5s)
    - OTA firmware updates
    - Full MAC-based deviceID (AtomS3_MAC)
    - Auto reconnect, ping, and key-release failsafe
  ------------------------------------------------------------
*/

#include <M5Unified.h>
#include <M5GFX.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <memory>
#include <mbedtls/base64.h>
#include <vector>
#include <math.h>
#include <esp32-hal-ledc.h>

Preferences preferences;
WiFiManager wifiManager;
WiFiClient client;

// Companion Server
char companion_host[40] = "Companion IP";
char companion_port[6]  = "16622";

// Static IP (0.0.0.0 = DHCP)
IPAddress stationIP   = IPAddress(0, 0, 0, 0);
IPAddress stationGW   = IPAddress(0, 0, 0, 0);
IPAddress stationMask = IPAddress(255, 255, 255, 0);

// Device ID – full MAC will be appended
String deviceID = "";

// AP password for config portal (empty = open)
const char* AP_password = "";

// Timing
unsigned long lastPingTime = 0;
unsigned long lastConnectTry = 0;
const unsigned long connectRetryMs = 5000;
const unsigned long pingIntervalMs = 2000;

// Brightness (0–100)
int brightness = 100;

// External RGB LED (Jaycar RGB LED) --------------------------
const int LED_PIN_RED   = G8;
const int LED_PIN_GREEN = G5;
const int LED_PIN_BLUE  = G6;
const int LED_PIN_GND   = G7;

const uint32_t pwmFreq       = 5000;
const uint8_t  pwmResolution = 8;

uint8_t lastColorR = 0;
uint8_t lastColorG = 0;
uint8_t lastColorB = 0;
// ------------------------------------------------------------

WiFiManagerParameter* custom_companionIP;
WiFiManagerParameter* custom_companionPort;

void logger(const String& s, const String& type = "info") {
  Serial.println(s);
}

String getParam(const String& name) {
  if (wifiManager.server && wifiManager.server->hasArg(name))
    return wifiManager.server->arg(name);
  return "";
}

void saveParamCallback() {
  String str_companionIP   = getParam("companionIP");
  String str_companionPort = getParam("companionPort");

  preferences.begin("companion", false);
  if (str_companionIP.length() > 0)   preferences.putString("companionip", str_companionIP);
  if (str_companionPort.length() > 0) preferences.putString("companionport", str_companionPort);
  preferences.end();
}

// ------------------------------------------------------------
// External LED Handling (new LEDC API: ledcAttach / ledcWrite)
// ------------------------------------------------------------
void setExternalLedColor(uint8_t r, uint8_t g, uint8_t b) {
  lastColorR = r;
  lastColorG = g;
  lastColorB = b;

  // For *common cathode* (LED GND to G7):
  uint8_t scaledR = r * max(brightness, 15) / 100;
  uint8_t scaledG = g * max(brightness, 15) / 100;
  uint8_t scaledB = b * max(brightness, 15) / 100;

  // For *common anode* (LED VCC to +3V3), uncomment below:
  // scaledR = 255 - scaledR;
  // scaledG = 255 - scaledG;
  // scaledB = 255 - scaledB;

  // DEBUG: log raw + scaled values
  Serial.print("[LED] setExternalLedColor raw r/g/b = ");
  Serial.print(r); Serial.print("/");
  Serial.print(g); Serial.print("/");
  Serial.print(b);
  Serial.print("  scaled = ");
  Serial.print(scaledR); Serial.print("/");
  Serial.print(scaledG); Serial.print("/");
  Serial.println(scaledB);

  // New-style LEDC: duty is keyed by PIN
  ledcWrite(LED_PIN_RED,   scaledR);
  ledcWrite(LED_PIN_GREEN, scaledG);
  ledcWrite(LED_PIN_BLUE,  scaledB);
}

// ------------------------------------------------------------
// Display Helpers
// ------------------------------------------------------------
void clearScreen(uint16_t color = BLACK) {
  M5.Display.fillScreen(color);
}

void drawCenterText(const String& txt, uint16_t color = WHITE, uint16_t bg = BLACK) {
  M5.Display.fillScreen(bg);
  M5.Display.setTextColor(color, bg);
  M5.Display.setTextDatum(middle_center);

  // Split message into lines
  std::vector<String> lines;
  int start = 0;
  while (true) {
    int idx = txt.indexOf('\n', start);
    if (idx < 0) {
      lines.push_back(txt.substring(start));
      break;
    }
    lines.push_back(txt.substring(start, idx));
    start = idx + 1;
  }

  int lineHeight = 16;               // Adjust if needed
  int totalHeight = lineHeight * lines.size();
  int topY = (M5.Display.height() - totalHeight) / 2 + (lineHeight / 2);

  for (int i = 0; i < (int)lines.size(); i++) {
    int y = topY + i * lineHeight;
    M5.Display.drawString(lines[i], M5.Display.width() / 2, y);
  }
}

void drawBitmapRGB888FullScreen(uint8_t* rgb, int size) {
  int sw = M5.Display.width();
  int sh = M5.Display.height();

  for (int y = 0; y < sh; y++) {
    int srcY = (y * size) / sh;
    for (int x = 0; x < sw; x++) {
      int srcX = (x * size) / sw;
      int idx = (srcY * size + srcX) * 3;
      M5.Display.drawPixel(x, y, M5.Display.color565(rgb[idx], rgb[idx+1], rgb[idx+2]));
    }
  }
}

// ------------------------------------------------------------
// Companion / Satellite API
// ------------------------------------------------------------
void sendAddDevice() {
  String cmd = "ADD-DEVICE DEVICEID=" + deviceID +
               " PRODUCT_NAME=\"M5 AtomS3\" KEYS_TOTAL=1 KEYS_PER_ROW=1 BITMAPS=72 COLORS=rgb TEXT=true";
  client.println(cmd);
  Serial.println("[API] Sent: " + cmd);
}

void handleKeyState(const String& line) {
  // DEBUG: log the whole KEY-STATE line
  Serial.println("[API] KEY-STATE line: " + line);

  // -------- COLOR=rgba(...): LED only --------
  int colorPos = line.indexOf("COLOR=");
  if (colorPos >= 0) {
    int start = colorPos + 6;
    int end = line.indexOf(' ', start);
    if (end < 0) end = line.length();
    String c = line.substring(start, end);
    c.trim();

    // DEBUG: raw COLOR field
    Serial.println("[API] COLOR raw: " + c);

    if (c.startsWith("\"") && c.endsWith("\"")) c = c.substring(1, c.length() - 1);

    if (c.startsWith("rgba(")) {
      c.replace("rgba(", "");
      c.replace(")", "");
      c.replace(" ", "");
      int p1 = c.indexOf(',');
      int p2 = c.indexOf(',', p1+1);
      int p3 = c.indexOf(',', p2+1);
      int r = c.substring(0, p1).toInt();
      int g = c.substring(p1+1, p2).toInt();
      int b = c.substring(p2+1, p3).toInt();

      // DEBUG: parsed rgba components (alpha ignored)
      Serial.print("[API] Parsed COLOR r/g/b = ");
      Serial.print(r); Serial.print("/");
      Serial.print(g); Serial.print("/");
      Serial.println(b);

      setExternalLedColor(r, g, b);
    } else {
      Serial.println("[API] COLOR is not rgba(), ignoring for LED.");
    }
  } else {
    // No COLOR in this KEY-STATE
    Serial.println("[API] No COLOR= field in KEY-STATE.");
  }

  // -------- BITMAP --------
  int bmpPos = line.indexOf("BITMAP=");
  if (bmpPos >= 0) {
    int start = bmpPos + 7;
    int end = line.indexOf(' ', start);
    if (end < 0) end = line.length();
    String bmp = line.substring(start, end);
    bmp.trim();

    if (bmp.startsWith("\"") && bmp.endsWith("\""))
      bmp = bmp.substring(1, bmp.length() - 1);

    int inLen = bmp.length();
    if (inLen <= 0) {
      Serial.println("[API] BITMAP present but empty.");
      return;
    }

    Serial.println("[API] BITMAP base64 length: " + String(inLen));

    size_t out_max = (inLen * 3) / 4 + 4;
    std::unique_ptr<uint8_t[]> buf(new uint8_t[out_max]);
    size_t out_len = 0;

    int res = mbedtls_base64_decode(buf.get(), out_max, &out_len,
                                    (const unsigned char*)bmp.c_str(), inLen);
    if (res != 0) {
      Serial.println("[API] Base64 decode failed, res=" + String(res) + " out_len=" + String(out_len));
      return;
    }

    Serial.println("[API] Decoded BITMAP bytes: " + String(out_len));

    int sizeRGB  = sqrt(out_len / 3);
    int sizeRGBA = sqrt(out_len / 4);

    bool isRGB  = (sizeRGB  * sizeRGB  * 3 == (int)out_len);
    bool isRGBA = (sizeRGBA * sizeRGBA * 4 == (int)out_len);

    if (isRGB) {
      Serial.println("[API] BITMAP detected as RGB, size=" + String(sizeRGB));
      drawBitmapRGB888FullScreen(buf.get(), sizeRGB);
    } else if (isRGBA) {
      Serial.println("[API] BITMAP detected as RGBA, size=" + String(sizeRGBA));
      int pixels = sizeRGBA * sizeRGBA;
      std::unique_ptr<uint8_t[]> rgb(new uint8_t[pixels * 3]);
      uint8_t* s = buf.get();
      uint8_t* d = rgb.get();
      for (int i = 0; i < pixels; i++) {
        d[0] = s[0];
        d[1] = s[1];
        d[2] = s[2];
        s += 4;
        d += 3;
      }
      drawBitmapRGB888FullScreen(rgb.get(), sizeRGBA);
    } else {
      Serial.println("[API] BITMAP size mismatch, cannot infer square dimensions.");
    }
  }
}

void parseAPI(const String& apiData) {
  if (apiData.length() == 0) return;
  if (apiData.startsWith("PONG")) return;

  // DEBUG: log all incoming API lines except very frequent PONG
  Serial.println("[API] RX: " + apiData);

  if (apiData.startsWith("PING")) {
    String payload = apiData.substring(apiData.indexOf(' ') + 1);
    client.println("PONG " + payload);
    return;
  }

  if (apiData.startsWith("BRIGHTNESS")) {
    int valPos = apiData.indexOf("VALUE=");
    String v = apiData.substring(valPos + 6);
    brightness = v.toInt();
    Serial.println("[API] BRIGHTNESS set to " + String(brightness));
    setExternalLedColor(lastColorR, lastColorG, lastColorB);
    return;
  }

  if (apiData.startsWith("KEYS-CLEAR")) {
    Serial.println("[API] KEYS-CLEAR received");
    clearScreen(BLACK);
    setExternalLedColor(0,0,0);
    return;
  }

  if (apiData.startsWith("KEY-STATE")) {
    handleKeyState(apiData);
    return;
  }
}

// ------------------------------------------------------------
// WiFi + Config Portal
// ------------------------------------------------------------
void connectToNetwork() {
  if (stationIP != IPAddress(0,0,0,0))
    wifiManager.setSTAStaticIPConfig(stationIP, stationGW, stationMask);

  WiFi.mode(WIFI_STA);

  custom_companionIP   = new WiFiManagerParameter("companionIP", "Companion IP", companion_host, 40);
  custom_companionPort = new WiFiManagerParameter("companionPort", "Satellite Port", companion_port, 6);

  wifiManager.addParameter(custom_companionIP);
  wifiManager.addParameter(custom_companionPort);
  wifiManager.setSaveParamsCallback(saveParamCallback);

  std::vector<const char*> menu = { "wifi", "param", "info", "sep", "restart", "exit" };
  wifiManager.setMenu(menu);
  wifiManager.setClass("invert");
  wifiManager.setConfigPortalTimeout(120);

  bool res = wifiManager.autoConnect(deviceID.c_str(), AP_password);

  if (!res) {
    Serial.println("[WiFi] Failed to connect, showing WiFi ERR");
    drawCenterText("WiFi ERR", RED, BLACK);
  } else {
    Serial.println("[WiFi] Connected to AP, IP=" + WiFi.localIP().toString());
    drawCenterText("WiFi OK", GREEN, BLACK);
    delay(500);
  }
}

// ------------------------------------------------------------
// SETUP
// ------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("\n[M5AtomS3] Booting...");

  // Build deviceID from MAC every boot (do NOT store it)
  WiFi.mode(WIFI_STA);   // ensure MAC is initialised
  delay(100);

  // -------- ID = AtomS3_FULLMAC --------
  uint8_t mac[6];
  WiFi.macAddress(mac);

  char macBuf[13];
  sprintf(macBuf, "%02X%02X%02X%02X%02X%02X",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  deviceID = "AtomS3_";
  deviceID += macBuf;
  deviceID.toUpperCase();

  Serial.println("[ID] deviceID = " + deviceID);

  // Store this ID
  preferences.begin("companion", false);
  preferences.putString("deviceid", deviceID);

  if (preferences.getString("companionip").length() > 0)
    preferences.getString("companionip").toCharArray(companion_host, sizeof(companion_host));

  if (preferences.getString("companionport").length() > 0)
    preferences.getString("companionport").toCharArray(companion_port, sizeof(companion_port));

  preferences.end();

  Serial.print("[Prefs] Companion IP: ");
  Serial.println(companion_host);
  Serial.print("[Prefs] Companion Port: ");
  Serial.println(companion_port);

  auto cfg = M5.config();
  M5.begin(cfg);

  M5.Display.setRotation(0);
  M5.Display.setTextSize(1);
  clearScreen(BLACK);
  String bootMsg =
    "BOOTING\n\n"
    "M5AtomS3\n"
    "Companion v4\n"
    "Single Button\n"
    "Satellite";

  drawCenterText(bootMsg, WHITE, BLACK);

  // ---- External LED Setup ----
  pinMode(LED_PIN_GND, OUTPUT);
  digitalWrite(LED_PIN_GND, LOW);

  Serial.println("[LED] Initialising PWM pins with ledcAttach...");
  if (ledcAttach(LED_PIN_RED,   pwmFreq, pwmResolution)) {
    Serial.println("[LED] PWM attached to RED pin");
  } else {
    Serial.println("[LED] ERROR attaching PWM to RED pin");
  }
  if (ledcAttach(LED_PIN_GREEN, pwmFreq, pwmResolution)) {
    Serial.println("[LED] PWM attached to GREEN pin");
  } else {
    Serial.println("[LED] ERROR attaching PWM to GREEN pin");
  }
  if (ledcAttach(LED_PIN_BLUE,  pwmFreq, pwmResolution)) {
    Serial.println("[LED] PWM attached to BLUE pin");
  } else {
    Serial.println("[LED] ERROR attaching PWM to BLUE pin");
  }

  setExternalLedColor(0,0,0);

  // --- LED TEST: flash R, G, B in sequence ---
  Serial.println("[LED] Running power-on colour test (R/G/B)...");
  setExternalLedColor(255, 0, 0);  // Red
  delay(300);
  setExternalLedColor(0, 255, 0);  // Green
  delay(300);
  setExternalLedColor(0, 0, 255);  // Blue
  delay(300);
  setExternalLedColor(0, 0, 0);    // Off
  // ------------------------------------------

  WiFi.setHostname(deviceID.c_str());

  connectToNetwork();

  ArduinoOTA.setHostname(deviceID.c_str());
  ArduinoOTA.setPassword("companion-satellite");
  ArduinoOTA.begin();

  clearScreen(BLACK);
  String waitMsg =
    "Waiting For\n"
    "Companion\n"
    "on\n" +
    String(companion_host) + "\n" + String(companion_port);

  drawCenterText(waitMsg, WHITE, BLACK);
  Serial.println("[System] Setup complete, entering main loop.");
}

// ------------------------------------------------------------
// LOOP
// ------------------------------------------------------------
void loop() {
  M5.update();
  ArduinoOTA.handle();

  if (M5.BtnA.pressedFor(5000)) {

      // Force a KEY RELEASE in Companion so it never stays “held”
      if (client.connected()) {
          Serial.println("[BTN] Long press, sending KEY RELEASE before config portal");
          client.println("KEY-PRESS DEVICEID=" + deviceID + " KEY=0 PRESSED=false");
      }

      // Pause briefly to ensure Companion receives the release
      delay(50);

      String msg =
        "CONFIG PORTAL\n\n"
        "Connect to WiFi:\n" +
        deviceID +
        "\nThen go to:\n"
        "192.168.4.1";

      drawCenterText(msg, WHITE, BLACK);

      // Start WiFiManager AP for configuration
      while (wifiManager.startConfigPortal(deviceID.c_str(), AP_password)) {}

      ESP.restart();
  }

  unsigned long now = millis();
  if (!client.connected() && (now - lastConnectTry >= connectRetryMs)) {
    lastConnectTry = now;
    Serial.print("[NET] Connecting to Companion ");
    Serial.print(companion_host);
    Serial.print(":");
    Serial.println(companion_port);

    if (client.connect(companion_host, atoi(companion_port))) {
      Serial.println("[NET] Connected to Companion API");
      drawCenterText("Connected", GREEN, BLACK);
      delay(200);
      sendAddDevice();
      lastPingTime = millis();
    } else {
      Serial.println("[NET] Companion connect failed");
    }
  }

  if (client.connected()) {
    while (client.available()) {
      String line = client.readStringUntil('\n');
      line.trim();
      if (line.length() > 0) parseAPI(line);
    }

    if (M5.BtnA.wasPressed()) {
      Serial.println("[BTN] Short press -> KEY=0 PRESSED=true");
      client.println("KEY-PRESS DEVICEID=" + deviceID + " KEY=0 PRESSED=true");
    }

    if (M5.BtnA.wasReleased()) {
      Serial.println("[BTN] Release -> KEY=0 PRESSED=false");
      client.println("KEY-PRESS DEVICEID=" + deviceID + " KEY=0 PRESSED=false");
    }

    if (now - lastPingTime >= pingIntervalMs) {
      client.println("PING atoms3");
      lastPingTime = now;
    }
  }

  delay(10);
}
